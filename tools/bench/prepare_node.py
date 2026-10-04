#!/usr/bin/env python3
"""Put one bench-fleet node into the state the board harnesses assume (idempotent).

    prepare_node.py --port /dev/cu.usbmodem2101 --node t-deck-14
    prepare_node.py --port /dev/cu.usbserial-0001 --node heltec-1 --dry-run   # read only
    prepare_node.py --port ... --node rak-90 --out prepare.json

Operator decision 2026-10-04: bench nodes carry the groups ``9;20;232;262`` and
the driver may write node settings. Per board (docs/automation-runner-runbook.md
section 1 and the harness docstrings):

    t_deck_plus, t_deck       --debug on, --debug csv
    heltec_wifi_lora_32_V3    --debug off, GPS on
    ttgo_tbeam                GPS on, track off
    wiscore_rak4631           nothing beyond the groups

Sequence: open the port (a USB-UART bridge resets an ESP32 on open, so wait for
``[BOOT];ready`` when boot text shows up), read ``--info`` (and ``--pos`` for the
GPS/Track state, which ``--info`` does not print), run the identity guard
(identity_guard.require) BEFORE any write, send exactly the missing commands,
re-read, verify. Nothing is sent when the state is already right, and
``--dry-run`` never writes.

Exit 0 = node is in the wanted state, 1 = refused by the guard, a read failed or
the state is still wrong after the writes, 2 = usage.

Pure logic (parse_state, plan_changes) does no I/O; the session class is
rak_harness.RakSession (DTR per port family via dtr_for).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from identity_guard import IdentityError, fleet_problems, load_fleet, require  # noqa: E402

WANTED_GROUPS: List[int] = [9, 20, 232, 262]
# Bench QTH for nodes without a GPS (the RAK): the T-Beam's fix on 2026-10-04
# rounded to three decimals (~100 m). sendPosition() returns at once when
# lat and lon are both 0, so an unpositioned node never answers --sendpos
# (mesh exchange run 6: DK5EN-90 tx_evidence 0, pair 90->92 missing).
BENCH_POSITION = (48.408, 11.738, 484)
SETGRC_CMD = "--setgrc " + ";".join(str(g) for g in WANTED_GROUPS)

# Wanted per board env (fleet.json "board"). Keys: debug (bool), separator
# ("csv"|"man"), gps (bool), track (bool). Anything absent is left alone.
BOARD_STATE: Dict[str, Dict[str, Any]] = {
    "t_deck_plus": {"debug": True, "separator": "csv"},
    "t_deck": {"debug": True, "separator": "csv"},
    "heltec_wifi_lora_32_V3": {"debug": False, "gps": True},
    "ttgo_tbeam": {"gps": True, "track": False},
    "wiscore_rak4631": {"position": BENCH_POSITION},
}

# src/command_functions.cpp, the --info block (~6188-6255):
#   "...DEBUG %s ...DEBUG %s\n"   csv|man, en|de
#   "...DEBUG %s ...LORADEBUG %s ..."   on|off first
#   "GC-%i:%i "                   one token per non-zero group slot
# and the --pos block (~6417):  "...GPS: %s\n...Track: %s\n"  (not in --info).
_GC = re.compile(r"GC-(\d+):(\d+)")
_DEBUG_ONOFF = re.compile(r"\.\.\.DEBUG (on|off)\b")
_DEBUG_SEP = re.compile(r"\.\.\.DEBUG (csv|man)\b")
_DEBUG_LANG = re.compile(r"\.\.\.DEBUG (en|de)\b")
_GPS = re.compile(r"\.\.\.GPS: (on|off)\b")
_LAT = re.compile(r"\.\.\.LAT: (-?\d+\.\d+) ([NS])")
_LON = re.compile(r"\.\.\.LON: (-?\d+\.\d+) ([EW])")
_TRACK = re.compile(r"\.\.\.Track: (on|off)\b", re.IGNORECASE)


# ---------------------------------------------------------------- pure logic

def _last(rx: "re.Pattern[str]", text: str) -> Optional[str]:
    found = rx.findall(text)
    return found[-1] if found else None


def _onoff(v: Optional[str]) -> Optional[bool]:
    return None if v is None else v == "on"


def parse_state(info_text: str) -> Dict[str, Any]:
    """State of a node from its ``--info`` (plus ``--pos``) reply text.

    ``groups`` = the non-zero GC-n values of the last group list, in slot order
    (the firmware prints no token for an empty slot; a printed ``:0`` is dropped
    too). The other keys are None when the text does not contain them.
    """
    text = info_text.replace("\r", "")
    gc = list(_GC.finditer(text))
    starts = [i for i, m in enumerate(gc) if m.group(1) == "1"]
    tail = gc[starts[-1]:] if starts else []
    groups = [int(m.group(2)) for m in tail if int(m.group(2)) > 0]
    return {
        "groups": groups,
        "debug": _onoff(_last(_DEBUG_ONOFF, text)),
        "separator": _last(_DEBUG_SEP, text),
        "language": _last(_DEBUG_LANG, text),
        "gps": _onoff(_last(_GPS, text)),
        "track": _onoff(_last(_TRACK, text)),
        "lat": _signed(_LAT, text, "S"),
        "lon": _signed(_LON, text, "W"),
    }


def _signed(rx: "re.Pattern[str]", text: str, neg: str) -> Optional[float]:
    ms = list(rx.finditer(text))
    if not ms:
        return None
    v = float(ms[-1].group(1))
    return -v if ms[-1].group(2) == neg else v


def needs_pos(board: str) -> bool:
    """True when the board requires a GPS/Track state (only ``--pos`` prints it)."""
    want = BOARD_STATE.get(board, {})
    return "gps" in want or "track" in want or "position" in want


def plan_changes(state: Dict[str, Any], board: str) -> List[str]:
    """Commands that bring ``state`` to the wanted state of ``board``.

    Empty when nothing is missing. Order: groups, debug (on/off, then separator),
    gps, track. A value the reply did not show (None) counts as missing: the
    commands are idempotent, and the post-write verify then fails loudly if the
    node never shows the item.
    """
    cmds: List[str] = []
    if sorted(state.get("groups") or []) != sorted(WANTED_GROUPS):
        cmds.append(SETGRC_CMD)
    want = BOARD_STATE.get(board, {})
    if "debug" in want and state.get("debug") is not want["debug"]:
        cmds.append("--debug on" if want["debug"] else "--debug off")
    if "separator" in want and state.get("separator") != want["separator"]:
        cmds.append(f"--debug {want['separator']}")
    if "gps" in want and state.get("gps") is not want["gps"]:
        cmds.append("--gps on" if want["gps"] else "--gps off")
    if "track" in want and state.get("track") is not want["track"]:
        cmds.append("--track on" if want["track"] else "--track off")
    if "position" in want:
        lat, lon = state.get("lat"), state.get("lon")
        if lat is None or lon is None or (abs(lat) < 1e-4 and abs(lon) < 1e-4):
            plat, plon, palt = want["position"]
            cmds += [f"--setlat {plat}", f"--setlon {plon}", f"--setalt {palt}"]
    return cmds


# ------------------------------------------------------------------- driver

def make_session(port: str, dtr: str, log_dir: Path) -> Any:
    """Open a rak_harness.RakSession (the only place a serial port is opened)."""
    import rak_harness  # lazy: pulls extudp_peer, keeps the pure parts import-light

    dtr_flag = None if dtr == "auto" else (dtr == "on")
    stamp = time.strftime("%Y%m%d-%H%M%S")
    name = re.sub(r"[^A-Za-z0-9]+", "_", Path(port).name)
    log_dir.mkdir(parents=True, exist_ok=True)
    s = rak_harness.RakSession(port, log_path=log_dir / f"prepare_node_{name}_{stamp}.log", dtr=dtr_flag)
    s.open()
    return s


def wait_boot(s: Any, boot_wait: float = 45.0) -> bool:
    """Let a node that reset on open finish booting; True when it did.

    A USB-UART bridge resets an ESP32 on open, and the T-Deck ignores serial for
    ~11 s after ``CLIENT STARTED``: when boot text shows up within 3 s wait for
    ``[BOOT];ready`` (up to ``boot_wait`` s) and settle 2 s. Silence in the first
    3 s means the node did not reboot (RAK, native USB): continue at once.
    """
    s.pump(3.0)
    seen = "\n".join(s.lines_since(0))
    if not re.search(r"CLIENT|\[BOOT\]|rst:", seen):
        return False
    m = s.wait_for(r"\[BOOT\];ready;ms;\d+;ip;(\d)", boot_wait, since=0)
    if m is not None and m.group(1) == "0":
        # main loop up, WiFi not joined yet: the ESP32 identity guard needs
        # the IP, give the join up to 30 s (T-Beam ~20 s after a reboot).
        s.wait_for(r"got_ip|IP address", 30.0, since=0)
    s.pump(2.0)
    return True


def read_state_text(s: Any, board: str, tries: int = 2) -> str:
    """``--info`` (and ``--pos`` when the board needs GPS/Track) as one text."""
    text = ""
    for _ in range(max(1, tries)):
        idx = s.send("--info")
        s.wait_for(r"\.\.\.Call: <", 6.0, since=idx)
        s.pump(2.0)
        text = "\n".join(s.lines_since(idx))
        if "...Call: <" in text:
            break
    if needs_pos(board) and "...Call: <" in text:
        idx = s.send("--pos")
        s.wait_for(r"\.\.\.Track: ", 4.0, since=idx)
        s.pump(0.5)
        text += "\n" + "\n".join(s.lines_since(idx))
    return text


def guard(info_text: str, node: str, fleet: Dict[str, Any]) -> None:
    """Identity guard on the --info text; raises IdentityError."""
    require(info_text, node, fleet)


def prepare(s: Any, node: str, board: str, fleet: Dict[str, Any], dry_run: bool = False,
            echo: Callable[[str], None] = print) -> Dict[str, Any]:
    """Read, guard, write the missing commands, re-read, verify. Returns the result dict."""
    result: Dict[str, Any] = {"node": node, "board": board, "port": getattr(s, "port", None),
                              "dry_run": dry_run, "changes": [], "ok": False}
    text = read_state_text(s, board)
    try:
        guard(text, node, fleet)
    except IdentityError as exc:
        result["error"] = str(exc)
        return result
    before = parse_state(text)
    changes = plan_changes(before, board)
    result.update(before=before, planned=changes)
    if dry_run:
        for c in changes:
            echo(f"prepare {node}: plan {c}")
        echo(f"prepare {node}: dry-run, {len(changes)} change(s) pending")
        result["ok"] = True      # a dry run succeeds when the read and the guard did
        return result

    for c in changes:
        echo(f"prepare {node}: {c}")
        s.send(c)
        s.pump(1.5)          # save_settings() flash write; the T-Deck takes a char per loop pass
        result["changes"].append(c)
    after_state = before
    if changes:
        text2 = read_state_text(s, board)
        try:
            guard(text2, node, fleet)       # still the same node after the writes
        except IdentityError as exc:
            result["error"] = f"after writes: {exc}"
            return result
        after_state = parse_state(text2)
    result["after"] = after_state
    missing = plan_changes(after_state, board)
    if missing:
        result["error"] = "state still wrong after writes, missing: " + ", ".join(missing)
        return result
    result["ok"] = True
    echo(f"prepare {node}: {len(changes)} change(s), state ok")
    return result


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description="put a bench node into the state the harnesses assume (groups, debug, GPS, track)")
    ap.add_argument("--port", required=True, help="serial port of the node")
    ap.add_argument("--node", required=True, help="fleet.json node name (identity guard, mandatory)")
    ap.add_argument("--dtr", choices=["auto", "on", "off"], default="auto",
                    help="DTR on open (auto: on for usbmodem native-USB, off for USB-UART bridges)")
    ap.add_argument("--dry-run", action="store_true", help="read --info (and --pos) only, print the plan, write nothing")
    ap.add_argument("--out", type=Path, default=None, help="write the result JSON here")
    ap.add_argument("--log-dir", type=Path, default=HERE / "runs", help="raw session log (default tools/bench/runs)")
    return ap


def main(argv: Optional[Sequence[str]] = None,
         session_factory: Optional[Callable[..., Any]] = None,
         fleet: Optional[Dict[str, Any]] = None) -> int:
    a = build_parser().parse_args(argv)
    fleet = fleet if fleet is not None else load_fleet()
    problems = fleet_problems(fleet)
    if problems:
        print("fleet.json inconsistent: " + "; ".join(problems), file=sys.stderr)
        return 1
    entry = fleet["nodes"].get(a.node)
    if entry is None:
        print(f"prepare {a.node}: refused -- node is not registered in fleet.json", file=sys.stderr)
        return 1
    board = entry["board"]

    factory = session_factory or make_session
    s = None
    try:
        s = factory(a.port, a.dtr, a.log_dir)
        wait_boot(s)
        result = prepare(s, a.node, board, fleet, dry_run=a.dry_run)
    finally:
        if s is not None:
            try:
                s.close()
            except Exception:  # noqa: BLE001
                pass

    if result.get("error"):
        print(f"prepare {a.node}: FAIL -- {result['error']}", file=sys.stderr)
    if a.out:
        a.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(f"wrote {a.out}")
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
