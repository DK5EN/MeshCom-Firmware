#!/usr/bin/env python3
"""Back up and compare bench-node configurations (test plan P0.3).

Every golden run reflashes the bench nodes, so their settings have to be
restorable first. The node serves its whole configuration as one JSON object
on `GET /config.json` (CS-03, `web_functions.cpp:656`) and takes it back on
`POST /config`. The test plan called this an `--export` serial command; there
is no such command, the export is HTTP only.

**The export contains live secrets in plaintext** -- the WiFi password
(`node_lpwd`), the AP password, the web password and the BLE pairing code. A
real backup therefore never lands in the repository. This tool writes two
files:

- the **restorable** backup, complete and unmasked, outside the repository
  (`--vault`, default `~/MeshCom-bench-backups/`);
- a **masked** copy under `test/golden/nodes/<node>/settings-base.json`, safe
  to commit, so a before/after settings diff stays reviewable and the field
  set is on record.

The masked copy is not restorable and says so in its own `_masked` key. Do not
POST it back to a node.

    python3 test/golden/backup_nodes.py --node rak-90=192.168.68.68 \\
                                        --node heltec-93=192.168.68.69
    python3 test/golden/backup_nodes.py --compare rak-90 heltec-93
    python3 test/golden/backup_nodes.py --self-test

No third-party dependencies: stdlib + type hints only.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, Iterable, List, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
MASKED_DIR = REPO_ROOT / "test" / "golden" / "nodes"
DEFAULT_VAULT = Path.home() / "MeshCom-bench-backups"

# Every field whose value is a credential. Masked in the committed copy.
SECRET_KEYS = (
    "node_pwd",      # AP password
    "node_lpwd",     # WiFi password of the joined network
    "node_webpwd",   # web GUI password
    "node_passwd",   # node password
    "bt_code",       # BLE pairing code
    "node_bpin",     # BLE PIN
)

# Per-node facts that are not secret but differ by node; kept, because the
# whole point of the committed copy is to show what each node was set to.
MASK_TOKEN = "<masked>"

# Fields the node rewrites from its own hardware, so a restore can never bring
# them back and a mismatch here is not a failure. Found by verifying a restore:
# node_alt came back as 503 against a backup of 488 -- the GPS had moved the
# altitude, not the import.
LIVE_FIELDS = ("node_lat", "node_lon", "node_alt", "node_lat_c", "node_lon_c")


def fetch(ip: str, timeout: float = 20.0) -> Dict[str, Any]:
    with urllib.request.urlopen(f"http://{ip}/config.json", timeout=timeout) as fh:
        return json.loads(fh.read().decode("utf-8"))


def mask(config: Dict[str, Any]) -> Dict[str, Any]:
    """A committable copy: every credential replaced, nothing else touched."""
    out = json.loads(json.dumps(config))          # deep copy, no aliasing
    settings = out.get("meshcom_config", {}).get("settings", {})
    masked: List[str] = []
    for key in SECRET_KEYS:
        if settings.get(key):                      # empty stays empty: that is
            settings[key] = MASK_TOKEN             # itself a fact worth keeping
            masked.append(key)
    out["_masked"] = {
        "note": "credentials replaced; NOT restorable, do not POST to a node",
        "keys": masked,
    }
    return out


def settings_of(config: Dict[str, Any]) -> Dict[str, str]:
    return config.get("meshcom_config", {}).get("settings", {})


def compare(a: Dict[str, Any], b: Dict[str, Any],
            name_a: str, name_b: str) -> Tuple[List[str], List[str], List[Tuple[str, str, str]]]:
    """(only in a, only in b, differing values) over the settings objects."""
    sa, sb = settings_of(a), settings_of(b)
    only_a = sorted(set(sa) - set(sb))
    only_b = sorted(set(sb) - set(sa))
    differ = [
        (k, sa[k], sb[k])
        for k in sorted(set(sa) & set(sb))
        if sa[k] != sb[k]
    ]
    return only_a, only_b, differ


def backup(name: str, ip: str, vault: Path) -> Tuple[Path, Path]:
    config = fetch(ip)
    vault.mkdir(parents=True, exist_ok=True)
    real = vault / f"{name}-settings-base.json"
    real.write_text(json.dumps(config, indent=2) + "\n")
    real.chmod(0o600)

    masked_dir = MASKED_DIR / name
    masked_dir.mkdir(parents=True, exist_ok=True)
    masked_path = masked_dir / "settings-base.json"
    masked_path.write_text(json.dumps(mask(config), indent=2) + "\n")
    return real, masked_path


def verify_masked(root: Path = MASKED_DIR) -> List[str]:
    """Fail-closed check over every committed backup.

    Run as part of the tooling gate, not only after a backup: the hazard is a
    future backup written straight into the repository by hand, or a new
    credential field added upstream that `SECRET_KEYS` does not yet name. A
    secret field may hold exactly two things here -- empty, or the mask token.
    """
    problems: List[str] = []
    if not root.exists():
        return problems
    for path in sorted(root.rglob("settings-base.json")):
        doc = json.loads(path.read_text())
        if "_masked" not in doc:
            problems.append(f"{path}: no _masked marker -- is this an unmasked backup?")
        settings = settings_of(doc)
        for key in SECRET_KEYS:
            value = settings.get(key, "")
            if value not in ("", MASK_TOKEN):
                problems.append(f"{path}: {key} holds a live value")
    return problems


def restore(name: str, ip: str, vault: Path, timeout: float = 30.0) -> str:
    """POST the unmasked backup back to the node; it reboots on success.

    **Every capture run has to start here.** The golden corpora are full of
    setters -- the command script alone drives `--<cmd> 1 / 999999 / abc` for
    every setter in the ladder, and even the small BLE corpus contains
    `--maxhop 5`. Measured on T-Beam-92 2026-09-11: one BLE capture moved
    `max_hop_text` from 4 to 5 permanently, and the next run's frames carried
    the new hop budget, so two runs of the same firmware differed. A capture
    that does not restore first is measuring the previous run's leftovers.

    The masked copies in the repository are deliberately not restorable; this
    reads the vault.
    """
    src = vault / f"{name}-settings-base.json"
    body = src.read_bytes()
    req = urllib.request.Request(
        f"http://{ip}/config", data=body, method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as fh:
            return fh.read().decode("utf-8", errors="replace").strip()
    except (TimeoutError, urllib.error.URLError, ConnectionError) as exc:
        # The node reboots as part of the import, so it often never finishes
        # the HTTP response. That is success, not failure -- but say so only
        # after the node is back and the values actually match.
        return f"(no response, node rebooted: {type(exc).__name__})"


def verify_restore(name: str, ip: str, vault: Path, wait: float = 25.0) -> List[str]:
    """Re-read the node and report every field that did not come back.

    Without this, "restored" rests on an HTTP call that usually times out by
    design. Volatile fields are not compared -- only what the backup set.
    """
    import time

    expected = json.loads((vault / f"{name}-settings-base.json").read_text())
    deadline = time.monotonic() + wait
    live = None
    while time.monotonic() < deadline:
        try:
            live = fetch(ip, timeout=5.0)
            break
        except Exception:
            time.sleep(2.0)
    if live is None:
        return [f"{name}: node did not answer within {wait:.0f} s after the restore"]

    exp, got = settings_of(expected), settings_of(live)
    return [
        f"{name}: {k} is {got.get(k)!r}, backup had {exp[k]!r}"
        for k in sorted(exp)
        if k in got and got[k] != exp[k] and k not in LIVE_FIELDS
    ]


def _self_test() -> int:
    failures = 0
    sample = {
        "meshcom_config": {
            "layout": 20260724,
            "settings": {
                "node_call": "DK5EN-93",
                "node_lpwd": "hunter2",
                "node_pwd": "",
                "bt_code": "100000",
            },
        }
    }
    out = mask(sample)
    s = settings_of(out)
    if s["node_lpwd"] != MASK_TOKEN or s["bt_code"] != MASK_TOKEN:
        failures += 1
        print("FAIL: a credential survived masking")
    if s["node_pwd"] != "":
        failures += 1
        print("FAIL: an empty credential was replaced; empty is a fact to keep")
    if s["node_call"] != "DK5EN-93":
        failures += 1
        print("FAIL: a non-secret field was altered")
    if settings_of(sample)["node_lpwd"] != "hunter2":
        failures += 1
        print("FAIL: mask() mutated its input")
    if "hunter2" in json.dumps(out):
        failures += 1
        print("FAIL: the secret is still somewhere in the masked document")

    other = json.loads(json.dumps(sample))
    other["meshcom_config"]["settings"]["node_freq"] = "4.33e+08"
    del other["meshcom_config"]["settings"]["bt_code"]
    only_a, only_b, differ = compare(sample, other, "a", "b")
    if only_a != ["bt_code"] or only_b != ["node_freq"]:
        failures += 1
        print(f"FAIL: key diff wrong: {only_a} {only_b}")

    problems = verify_masked()
    if problems:
        failures += len(problems)
        for p in problems:
            print(f"FAIL: {p}")

    print("backup_nodes.py self-test: " + ("ok" if failures == 0 else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(argv: Iterable[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--node", action="append", default=[], metavar="NAME=IP",
                    help="bench node to back up; repeatable")
    ap.add_argument("--vault", type=Path, default=DEFAULT_VAULT,
                    help=f"where the unmasked backups go (default: {DEFAULT_VAULT})")
    ap.add_argument("--compare", nargs=2, metavar="NAME",
                    help="diff two already-committed masked backups")
    ap.add_argument("--restore", action="append", default=[], metavar="NAME=IP",
                    help="POST the unmasked vault backup back to the node and let it "
                         "reboot; run this before every capture")
    ap.add_argument("--verify-masked", action="store_true",
                    help="check every committed backup for live credentials")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if args.self_test:
        return _self_test()

    if args.restore:
        for spec in args.restore:
            name, _, ip = spec.partition("=")
            answer = restore(name, ip, args.vault)
            print(f"{name:12} POST /config -> {answer[:100]}", file=sys.stderr)
            problems = verify_restore(name, ip, args.vault)
            for p in problems:
                print(f"  {p}", file=sys.stderr)
            print(f"{'':12} verified: "
                  + ("all fields match the backup" if not problems
                     else f"{len(problems)} field(s) did NOT come back"),
                  file=sys.stderr)
            if problems:
                return 1
        return 0

    if args.verify_masked:
        problems = verify_masked()
        for p in problems:
            print(p, file=sys.stderr)
        print(f"verify-masked: {len(problems)} problem(s)", file=sys.stderr)
        return 1 if problems else 0

    if args.compare:
        a_name, b_name = args.compare
        a = json.loads((MASKED_DIR / a_name / "settings-base.json").read_text())
        b = json.loads((MASKED_DIR / b_name / "settings-base.json").read_text())
        only_a, only_b, differ = compare(a, b, a_name, b_name)
        print(f"only in {a_name}: {', '.join(only_a) or '-'}")
        print(f"only in {b_name}: {', '.join(only_b) or '-'}")
        print(f"differing values ({len(differ)}):")
        for k, va, vb in differ:
            print(f"  {k:20} {a_name}={va!r:22} {b_name}={vb!r}")
        return 0

    if not args.node:
        ap.error("pass --node NAME=IP at least once, or --compare")

    for spec in args.node:
        name, _, ip = spec.partition("=")
        real, masked_path = backup(name, ip, args.vault)
        print(f"{name:12} -> {real} (unmasked, mode 600)", file=sys.stderr)
        print(f"{'':12}    {masked_path.relative_to(REPO_ROOT)} (masked, committable)",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
