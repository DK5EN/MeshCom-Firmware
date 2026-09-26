#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""W3 upgrade check -- field-by-field diff of a node's configExportJson before
and after the settings-layout cutover (docs/bench/w3-baseline/README.md).

`W3`'s acceptance criterion is that a node configured on the previous
firmware keeps every setting across the upgrade. The "before" is one full
`configExportJson` per bench node, captured while it still ran a
`FLASH_STRUCT_VERSION 20260724` image and committed at
`docs/bench/w3-baseline/*.json`. After the cutover the same node is
re-exported and the two files are diffed here -- by hand across 103-107
fields is exactly how a silent single-field regression ships.

    python3 tools/bench/w3_upgrade_check.py <baseline.json> <new.json>
    python3 tools/bench/w3_upgrade_check.py <baseline.json> <node-ip>
    python3 tools/bench/w3_upgrade_check.py --gps-node <baseline.json> <node-ip>
    python3 tools/bench/w3_upgrade_check.py --self-test

The second argument is either a path to a previously saved export, or a bare
IP/hostname to fetch fresh from the node's web server
(`GET /config.json`, `src/web_functions/web_functions.cpp:656`). This wave
never exercises that path against real hardware -- the cutover has not
happened yet -- but the CLI supports it for the human who runs this after.

EXPORT SHAPE (src/config_json.h)
---------------------------------

    {"meshcom_config":{
        "layout":  <int>,       FLASH_STRUCT_VERSION
        "fw":      "<string>",  firmware version
        "hw":      <int>,       BOARD_HARDWARE (informational, never checked)
        "settings":{ "<nvs key>": "<value>", ... },
        "crc32":   "<8 hex digits>"
    }}

Every field in "settings", plus "layout"/"fw"/"hw"/"crc32" from the envelope,
is compared. All settings values are exported as JSON strings (including
numbers) -- see config_json.h's note on ArduinoJson's parser being off by a
ulp on plenty of inputs -- so "differs only in rendering" is a real case,
not a hypothetical: the committed RAK baseline itself has
`node_freq":"4.33175e+08"`.

BUCKETS
-------

  PRESERVED   same key, same value (numerically or exactly -- see below).
              Reported as a count, not a list: this is the expected case.
  CHANGED     same key, different value. The finding that matters most: a
              migration that silently alters a value is worse than one that
              drops it, because nothing looks wrong afterwards.
  LOST        in the baseline, absent from the new export. Listed
              individually.
  ADDED       new key, not in the baseline. Usually benign (a field gained
              an export row) but listed so it is a decision, not a surprise.
  ALLOWLISTED fields that legitimately differ across a reboot/upgrade (see
              ALLOWLIST below). Shown so the difference stays visible, but
              never counted toward CHANGED/LOST and never fails the gate.
              With --gps-node this bucket also absorbs the three live-GPS
              fields (GPS_DRIFT) -- opt-in, because on a node with a fixed
              configured position they are settings like any other.
  REMOVED BY  in the baseline, gone from the new export because the member no
  DESIGN      longer exists in the firmware (REMOVED_BY_DESIGN). Reported
              separately from LOST so the two expected nRF52 rows cannot hide
              a real one.

COMPARISON METHOD
-----------------

The export writes every value as a string, and a float may re-render
differently between two firmware builds (`4.33175e+08` vs `433175000`) with
no change in the underlying value. So: when BOTH sides of a field parse as a
number, they are compared numerically (equal floats are PRESERVED, however
they are spelled); when either side does not parse as a number, they are
compared as exact strings. Each CHANGED line says which method fired, so a
reader can tell a real value change from a parsing surprise.

Exit code: 0 if there is no CHANGED and no LOST field, 1 otherwise -- so this
can gate a bench run.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional
from urllib.error import URLError

from identity_guard import IdentityError, read_info_net, require

# ---------------------------------------------------------------------------
# Allow-list: fields that legitimately differ across the exact reboot this
# tool exists to check, and are therefore not configuration drift.
#
# Derived from src/config_json.cpp's canon_head()/configExportJson(), not
# guessed: "hw" (BOARD_HARDWARE) is deliberately NOT here -- it is compiled
# per physical board and stays constant across a firmware upgrade on the same
# node, so a change in "hw" is a real anomaly (wrong export, wrong device)
# and must fail the gate like any other field.
# ---------------------------------------------------------------------------
ALLOWLIST: dict[str, str] = {
    "layout": (
        "FLASH_STRUCT_VERSION -- the whole point of the W3 cutover is to "
        "move a node from one layout generation to the next; this is "
        "expected to change, never a defect."
    ),
    "fw": (
        "the firmware version string (SOURCE_VERSION + SOURCE_VERSION_SUB) "
        "-- always a new build on the far side of an upgrade."
    ),
    "crc32": (
        "config_json.cpp's canon_head() feeds layout, fw and hw into the "
        "same CRC as every settings field (canon_begin() -> canon_head() -> "
        "per-field canon_kv()). Since layout and fw are allowed to change "
        "by design, the CRC changes with them even when every setting is "
        "byte-identical -- it carries no independent signal here."
    ),
}


# ---------------------------------------------------------------------------
# Fields a GPS-equipped node rewrites on its own, independently of any
# firmware change. NOT in ALLOWLIST above, and deliberately opt-in per run
# (--gps-node): on a node with a fixed configured position these three ARE
# settings, and silently excusing them would stop the gate checking the very
# thing an operator is most likely to notice going missing.
#
# Measured on DK5EN-93 2026-09-16: two GET /config.json 20 s apart with NO
# flash between them already disagreed on node_alt (482 -> 484). Across the
# W3 migration boot the same node moved node_lat/node_lon/node_alt and
# nothing else, which is what a false FAIL looks like.
# ---------------------------------------------------------------------------
GPS_DRIFT: dict[str, str] = {
    key: (
        "live GPS output on a node with a fix -- the node rewrites this "
        "between any two exports, with or without a firmware change "
        "(measured on DK5EN-93, 20 s apart, no flash). Excused only because "
        "--gps-node was given."
    )
    for key in ("node_lat", "node_lon", "node_alt")
}

# ---------------------------------------------------------------------------
# Keys that exist in a pre-cutover baseline and are GONE from the firmware on
# the far side, by decision rather than by accident. Their absence is the
# correct outcome, so it is reported in its own bucket instead of as LOST.
#
# Unconditional: these members no longer exist in s_meshcom_settings at all
# (removed in the D1-04 merge), so there is no build in which they could come
# back and no regression the LOST bucket could still catch for them.
# ---------------------------------------------------------------------------
REMOVED_BY_DESIGN: dict[str, str] = {
    "send_repeat_time": (
        "LoRaWAN OTAA leftover, removed from s_meshcom_settings in the D1-04 "
        "merge together with the timer scaffolding behind it "
        "(src/config_json.h, 'nRF52: nothing.' note). nRF52-only export key, "
        "so this bucket is normally empty on an ESP32 node."
    ),
    "auto_join": (
        "LoRaWAN OTAA leftover, removed from s_meshcom_settings in the D1-04 "
        "merge together with the timer scaffolding behind it "
        "(src/config_json.h, 'nRF52: nothing.' note). nRF52-only export key, "
        "so this bucket is normally empty on an ESP32 node."
    ),
}


def try_float(value: str) -> Optional[float]:
    """Parse value as a float, or return None. Used to decide the comparison
    method per field -- see the module docstring's COMPARISON METHOD."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def flatten_config(config: dict[str, Any]) -> dict[str, str]:
    """Flatten one configExportJson document into {field_name: str(value)}.

    Top-level envelope fields (layout/fw/hw/crc32) and every key inside
    "settings" share one flat namespace -- config_json.h documents the
    settings keys as the NVS key names, which never collide with the four
    envelope names.
    """
    mc = config.get("meshcom_config", config)
    fields: dict[str, str] = {}
    for key in ("layout", "fw", "hw"):
        if key in mc:
            fields[key] = str(mc[key])
    settings = mc.get("settings", {})
    for key, value in settings.items():
        fields[key] = str(value)
    if "crc32" in mc:
        fields["crc32"] = str(mc["crc32"])
    return fields


@dataclass
class DiffResult:
    preserved: int = 0
    changed: list[tuple[str, str, str, str]] = field(default_factory=list)
    lost: list[tuple[str, str]] = field(default_factory=list)
    added: list[tuple[str, str]] = field(default_factory=list)
    allowlisted: list[tuple[str, Optional[str], Optional[str]]] = field(default_factory=list)
    removed_by_design: list[tuple[str, str]] = field(default_factory=list)
    # The allow-list this particular run applied: ALLOWLIST, plus GPS_DRIFT when
    # --gps-node was given. Carried on the result so the report can print the
    # right reason per row without the caller having to reconstruct it.
    allowlist_used: dict[str, str] = field(default_factory=lambda: dict(ALLOWLIST))

    @property
    def failed(self) -> bool:
        return bool(self.changed or self.lost)


def compare_value(old: str, new: str) -> tuple[bool, str]:
    """Returns (equal, method). method is "numeric" when both sides parse as
    a number (a float re-rendering is then PRESERVED, not CHANGED), "exact"
    otherwise."""
    old_num, new_num = try_float(old), try_float(new)
    if old_num is not None and new_num is not None:
        return old_num == new_num, "numeric"
    return old == new, "exact"


def diff_configs(baseline: dict[str, str], new: dict[str, str],
                 gps_node: bool = False) -> DiffResult:
    """Bucket every field of the two exports.

    gps_node=True additionally excuses the three live-GPS fields (GPS_DRIFT).
    It defaults to False so the gate stays strict unless the operator states
    that this node has a fix -- see GPS_DRIFT's own comment.
    """
    allowlist = dict(ALLOWLIST)
    if gps_node:
        allowlist.update(GPS_DRIFT)

    result = DiffResult(allowlist_used=allowlist)
    for key in sorted(set(baseline) | set(new)):
        if key in allowlist:
            result.allowlisted.append((key, baseline.get(key), new.get(key)))
            continue
        if key in baseline and key in new:
            equal, method = compare_value(baseline[key], new[key])
            if equal:
                result.preserved += 1
            else:
                result.changed.append((key, baseline[key], new[key], method))
        elif key in baseline:
            # Gone on the far side. Intentional removals get their own bucket
            # so a real regression stays visible in LOST instead of being
            # crowded out by two rows that are expected on every nRF52 run.
            if key in REMOVED_BY_DESIGN:
                result.removed_by_design.append((key, baseline[key]))
            else:
                result.lost.append((key, baseline[key]))
        else:
            result.added.append((key, new[key]))
    return result


def load_config(source: str) -> dict[str, Any]:
    """Load a configExportJson document from a file path, or fetch it from a
    live node's web server if `source` is not a file that exists."""
    path = Path(source)
    if path.is_file():
        return json.loads(path.read_text())
    url = f"http://{source}/config.json"
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except URLError as exc:
        raise SystemExit(f"could not read '{source}' as a file, and fetching "
                          f"{url} failed: {exc}")


def format_report(baseline_name: str, new_name: str, result: DiffResult) -> str:
    lines = [f"W3 upgrade check: {baseline_name} -> {new_name}", ""]

    lines.append(f"PRESERVED: {result.preserved} field(s) unchanged")
    lines.append("")

    lines.append(
        f"ALLOWLISTED ({len(result.allowlisted)}): expected to differ across "
        f"the upgrade, not counted as failures")
    if result.allowlisted:
        for k, old, new in result.allowlisted:
            reason = result.allowlist_used[k]
            old_s = "(missing)" if old is None else old
            new_s = "(missing)" if new is None else new
            same = "unchanged" if old == new else "changed"
            lines.append(f"  {k}: {old_s} -> {new_s}  [{same}] -- {reason}")
    lines.append("")

    lines.append(f"CHANGED ({len(result.changed)}): same key, different value")
    for k, old, new, method in result.changed:
        lines.append(f"  {k}: {old!r} -> {new!r}  [{method} comparison]")
    lines.append("")

    lines.append(f"LOST ({len(result.lost)}): in the baseline, absent from the new export")
    for k, old in result.lost:
        lines.append(f"  {k}: {old!r}")
    lines.append("")

    lines.append(
        f"REMOVED BY DESIGN ({len(result.removed_by_design)}): in the baseline, "
        f"deliberately gone from the firmware -- not counted as failures")
    for k, old in result.removed_by_design:
        lines.append(f"  {k}: {old!r} -- {REMOVED_BY_DESIGN[k]}")
    lines.append("")

    lines.append(f"ADDED ({len(result.added)}): new key, not in the baseline")
    for k, new in result.added:
        lines.append(f"  {k}: {new!r}")
    lines.append("")

    lines.append("RESULT: FAIL" if result.failed else "RESULT: PASS")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

def _wrap(settings: dict[str, str], layout: int = 20260724, fw: str = "4.35t",
          hw: int = 9, crc32: str = "deadbeef") -> dict[str, Any]:
    return {"meshcom_config": {"layout": layout, "fw": fw, "hw": hw,
                                "settings": settings, "crc32": crc32}}


def self_test() -> int:
    ok = True

    def check(name: str, condition: bool) -> None:
        nonlocal ok
        if condition:
            print(f"  ok  {name}")
        else:
            print(f"SELF-TEST FAIL: {name}")
            ok = False

    # 1. Clean round trip: identical documents, nothing allow-listed differs.
    base = _wrap({"node_call": "DK5EN-90", "node_sf": "11"})
    new = _wrap(dict(base["meshcom_config"]["settings"]))
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("clean round trip: no changed/lost/added",
          not r.changed and not r.lost and not r.added)
    check("clean round trip: preserved counts every settings field + hw",
          r.preserved == 3)  # node_call, node_sf, hw
    check("clean round trip: layout/fw/crc32 allow-listed, not failing",
          len(r.allowlisted) == 3 and not r.failed)
    check("clean round trip: exits 0", not r.failed)

    # 2. A changed value.
    base = _wrap({"node_sf": "11"})
    new = _wrap({"node_sf": "10"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("changed value: caught as CHANGED",
          r.changed == [("node_sf", "11", "10", "numeric")])
    check("changed value: fails the gate", r.failed)

    # 2b. A changed value that is not a number on either side -- exact
    # string comparison, not numeric.
    base = _wrap({"node_call": "DK5EN-90"})
    new = _wrap({"node_call": "DK5EN-91"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("changed string value: caught as CHANGED with exact method",
          r.changed == [("node_call", "DK5EN-90", "DK5EN-91", "exact")])

    # 3. A lost field.
    base = _wrap({"node_sf": "11", "node_via": ""})
    new = _wrap({"node_sf": "11"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("lost field: caught as LOST", r.lost == [("node_via", "")])
    check("lost field: fails the gate", r.failed)

    # 4. An added field.
    base = _wrap({"node_sf": "11"})
    new = _wrap({"node_sf": "11", "node_bfakt": "0"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("added field: caught as ADDED", r.added == [("node_bfakt", "0")])
    check("added field: does not fail the gate", not r.failed)

    # 5. A float that differs only in rendering -- the real RAK baseline
    # shape: node_freq stored as scientific notation on one side.
    base = _wrap({"node_freq": "4.33175e+08"})
    new = _wrap({"node_freq": "433175000"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("float rendering-only diff: PRESERVED, not CHANGED",
          not r.changed and r.preserved == 2)  # node_freq + hw

    # 6. A float that differs in value.
    base = _wrap({"node_freq": "4.33175e+08"})
    new = _wrap({"node_freq": "4.33180e+08"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("float value diff: caught as CHANGED with numeric method",
          r.changed == [("node_freq", "4.33175e+08", "4.33180e+08", "numeric")])
    check("float value diff: fails the gate", r.failed)

    # 7. An allow-listed field that changed (layout, the expected case).
    base = _wrap({"node_sf": "11"}, layout=20260724, fw="4.35t")
    new = _wrap({"node_sf": "11"}, layout=20260901, fw="4.35u")
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("allow-listed field changed: not counted as CHANGED",
          not r.changed and not r.failed)
    layout_row = next(row for row in r.allowlisted if row[0] == "layout")
    check("allow-listed field changed: still visible in its own bucket",
          layout_row == ("layout", "20260724", "20260901"))

    # 8. Live-GPS drift. Regression for the false FAIL every GPS node produced
    #    before --gps-node existed: DK5EN-93's migration run reported exactly
    #    these three fields as CHANGED and nothing else.
    base = _wrap({"node_sf": "11", "node_lat": "48.4077", "node_lon": "11.7385",
                  "node_alt": "484"})
    new = _wrap({"node_sf": "11", "node_lat": "48.4076", "node_lon": "11.7386",
                 "node_alt": "492"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("GPS drift without --gps-node: still CHANGED, still fails "
          "(a fixed-position node must not be excused)",
          len(r.changed) == 3 and r.failed)

    r = diff_configs(flatten_config(base), flatten_config(new), gps_node=True)
    check("GPS drift with --gps-node: not CHANGED, passes",
          not r.changed and not r.failed)
    check("GPS drift with --gps-node: still visible in the allow-list bucket",
          sorted(row[0] for row in r.allowlisted)
          == ["crc32", "fw", "layout", "node_alt", "node_lat", "node_lon"])
    check("GPS drift with --gps-node: a real setting still fails alongside it",
          diff_configs(flatten_config(_wrap({"node_sf": "11", "node_alt": "484"})),
                       flatten_config(_wrap({"node_sf": "10", "node_alt": "492"})),
                       gps_node=True).failed)

    # 9. Keys removed from the struct by decision (D1-04). Expected on every
    #    nRF52 upgrade run; must not be able to mask a genuine loss.
    base = _wrap({"node_sf": "11", "send_repeat_time": "0", "auto_join": "0"})
    new = _wrap({"node_sf": "11"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("removed-by-design keys: own bucket, not LOST",
          not r.lost and sorted(k for k, _ in r.removed_by_design)
          == ["auto_join", "send_repeat_time"])
    check("removed-by-design keys: do not fail the gate", not r.failed)

    base = _wrap({"node_sf": "11", "send_repeat_time": "0", "node_via": "X"})
    new = _wrap({"node_sf": "11"})
    r = diff_configs(flatten_config(base), flatten_config(new))
    check("a genuine loss alongside a by-design removal still fails",
          r.lost == [("node_via", "X")] and r.failed)

    return 0 if ok else 1


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline", nargs="?", help="committed baseline JSON (docs/bench/w3-baseline/*.json)")
    ap.add_argument("new", nargs="?", help="new export: a JSON file, or a bare IP/hostname to fetch from")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument(
        "--gps-node", action="store_true",
        help="this node has a live GPS fix: treat node_lat/node_lon/node_alt as "
             "drifting output rather than as settings. Off by default -- on a "
             "node with a fixed configured position those three are real "
             "settings and a change in them is a real defect.")
    ap.add_argument("--node", default=None,
                    help="fleet.json node name for the identity guard; required when "
                         "'new' is a live host (not a saved export file) unless "
                         "--no-identity-guard is given")
    ap.add_argument("--no-identity-guard", action="store_true",
                    help="skip the identity guard -- only for setting up a node's identity")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    if not args.baseline or not args.new:
        ap.error("baseline and new are required unless --self-test is given")

    # A saved export file is a pure offline replay -- no live node is touched,
    # so the identity guard does not apply. Only a bare IP/hostname fetch
    # ("new export... to fetch from") reaches a live node's web server.
    new_is_live = not Path(args.new).is_file()
    if new_is_live:
        if not args.node and not args.no_identity_guard:
            ap.error("--node NAME is required (fleet.json node name) when 'new' is a "
                      "live host unless --no-identity-guard is given for identity setup")
        if args.node and not args.no_identity_guard:
            info_text = read_info_net(args.new)
            try:
                require(info_text, args.node)
            except IdentityError as e:
                print(str(e), file=sys.stderr)
                return 1

    baseline_doc = load_config(args.baseline)
    new_doc = load_config(args.new)

    result = diff_configs(flatten_config(baseline_doc), flatten_config(new_doc),
                          gps_node=args.gps_node)
    print(format_report(args.baseline, args.new, result))
    return 1 if result.failed else 0


if __name__ == "__main__":
    sys.exit(main())
