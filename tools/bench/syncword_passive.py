#!/usr/bin/env python3
"""Passive sync-word comparison: what the RAK4631 hears from field stations against DK5EN-98 (SYNC-NRF-01).

Listening only. The RAK capture sends ``--info`` and ``--setlog on`` (restored at the end); nothing is
transmitted on purpose. DK5EN-98 is read from its meshlogger file (``--no-debug-flags``), never touched.

    syncword_passive.py capture --rak-port /dev/cu.usbmodem2101 --minutes 60 --out DIR/A-passive
    syncword_passive.py compare --rak DIR/A-passive/rak.log --ref 98.log --label A [--start EPOCH --end EPOCH]

Compare: every copy on air that DK5EN-98 decoded inside the RAK window, sent by a station outside the bench (the
last call of the path is not DK5EN-*), is looked up by message id and last-hop call in the RAK's decoded frames.
The transmitter's board comes from LH (last-hop hardware). Rows per transmitter class (nRF52 = old 0x242B on air, SX126x ESP32, SX127x) and per SNR
band at DK5EN-98. The two receivers sit a few metres apart with different antennas, so only the A/B difference
of the ratio carries meaning, not the ratio itself.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
import time
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

NRF52 = {7, 9, 54}                       # T-Echo, RAK4631, T114 (configuration_global.h)
SX127X = {1, 2, 3, 4, 6, 10, 11}         # T-LoRa, T-Beam SX1276/8, Heltec V1/V2
BENCH = re.compile(r"^DK5EN-")
_RX = re.compile(r"\[LOG\]\s+\d+\s+\S\s+x([0-9A-F]{8})\s.*?\s([A-Z0-9]+-\d+(?:,[A-Z0-9-]+)*)>"
                 r".*?\sHW:(\d+)\s.*?LH:([0-9A-F]{2})\s.*?RSSI:(-?\d+)\s+SNR:(-?\d+)")
BANDS = [(-99, -10), (-10, -5), (-5, 0), (0, 5), (5, 99)]


def chip_class(hw: int) -> str:
    if hw in NRF52:
        return "nRF52 (0x242B)"
    if hw in SX127X:
        return "SX127x"
    return "SX126x ESP32"


def parse(line: str) -> Optional[Tuple[str, str, bool, int, int, int]]:
    """(id, last-hop call, direct, transmitter hw, rssi, snr) of a decoded frame line.

    The copy on air was sent by the LAST call of the path; its board is LH & 0x7F when bit 0x80 is set
    (msg_last_hw = BOARD_HARDWARE | 0x80, lora_functions.cpp), else -1 (unknown, not classified)."""
    m = _RX.search(line)
    if not m:
        return None
    path = m.group(2).split(",")
    lh = int(m.group(4), 16)
    hw = lh & 0x7F if lh & 0x80 else (int(m.group(3)) if len(path) == 1 else -1)
    return m.group(1), path[-1], len(path) == 1, hw, int(m.group(5)), int(m.group(6))


def epoch_of(line: str) -> Optional[float]:
    """Epoch prefix (syncword captures) or 'YYYY-MM-DD HH:MM:SS[.mmm]' prefix (meshlogger)."""
    head = line.split(" ", 1)[0]
    try:
        return float(head)
    except ValueError:
        pass
    m = re.match(r"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?)", line)
    if not m:
        return None
    fmt = "%Y-%m-%d %H:%M:%S.%f" if "." in m.group(1) else "%Y-%m-%d %H:%M:%S"
    return dt.datetime.strptime(m.group(1), fmt).timestamp()


def frames(lines: Iterable[str], t0: float, t1: float) -> Dict[str, Tuple[str, bool, int, int, int]]:
    """One entry per copy on air: key "id/last-hop call" (the same id relayed by two nodes is two copies)."""
    out: Dict[str, Tuple[str, bool, int, int, int]] = {}
    for ln in lines:
        t = epoch_of(ln)
        if t is None or not (t0 <= t <= t1):
            continue
        p = parse(ln)
        if p:
            out.setdefault(f"{p[0]}/{p[1]}", p[1:])
    return out


def compare(rak: Dict[str, tuple], ref: Dict[str, tuple]) -> List[dict]:
    rows: Dict[Tuple[str, str], dict] = {}
    for mid, (last, _direct, hw, _rssi, snr) in ref.items():
        if hw < 0 or BENCH.match(last):
            continue
        band = next(f"{lo}..{hi}" for lo, hi in BANDS if lo <= snr < hi)
        for key in ((chip_class(hw), "all"), (chip_class(hw), band)):
            r = rows.setdefault(key, {"class": key[0], "snr_at_98": key[1], "ref": 0, "rak": 0, "stations": set()})
            r["ref"] += 1
            r["rak"] += 1 if mid in rak else 0
            r["stations"].add(last)
    out = []
    for key in sorted(rows):
        r = rows[key]
        r["stations"] = len(r["stations"])
        r["ratio"] = round(r["rak"] / r["ref"], 3) if r["ref"] else None
        out.append(r)
    return out


def render(rows: List[dict], label: str, t0: float, t1: float) -> str:
    a = dt.datetime.fromtimestamp(t0).strftime("%Y-%m-%d %H:%M")
    b = dt.datetime.fromtimestamp(t1).strftime("%H:%M")
    out = [f"### Passive {label} ({a} to {b})", "",
           "| Transmitter | SNR at DK5EN-98 | Frames at DK5EN-98 | Also at RAK | Ratio | Stations |",
           "| ----------- | --------------- | ------------------ | ----------- | ----- | -------- |"]
    for r in rows:
        out.append(f"| {r['class']} | {r['snr_at_98']} | {r['ref']} | {r['rak']} | "
                   f"{'-' if r['ratio'] is None else r['ratio']} | {r['stations']} |")
    return "\n".join(out) + "\n"


def cmd_capture(a: argparse.Namespace) -> int:
    from identity_guard import IdentityError, require
    from syncword_ab import SerialCapture, ask, _SETLOG
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cap = SerialCapture("DK5EN-90", a.rak_port, out / "rak.log")
    cap.start()
    restore = None
    try:
        time.sleep(2)
        info = ask(cap, "--info", 5)
        try:
            require(info, "rak-90")
        except IdentityError as e:
            print(f"identity guard refused: {e}", file=sys.stderr)
            return 1
        m = _SETLOG.search(info)
        restore = f"--setlog {m.group(1) if m else 'off'}"
        ask(cap, "--setlog on", 2)
        t0 = time.time()
        time.sleep(a.minutes * 60)
        (out / "window.json").write_text(json.dumps({"start": t0, "end": time.time()}))
    finally:
        if restore:
            ask(cap, restore, 2)
        cap.close()
    return 0


def cmd_compare(a: argparse.Namespace) -> int:
    rak_lines = Path(a.rak).read_text(encoding="utf-8", errors="replace").splitlines()
    ref_lines = Path(a.ref).read_text(encoding="utf-8", errors="replace").splitlines()
    w = Path(a.rak).with_name("window.json")
    t0 = a.start if a.start is not None else json.loads(w.read_text())["start"]
    t1 = a.end if a.end is not None else json.loads(w.read_text())["end"]
    rows = compare(frames(rak_lines, t0, t1 + 5), frames(ref_lines, t0, t1))
    md = render(rows, a.label, t0, t1)
    Path(a.rak).with_name("passive.md").write_text(md)
    Path(a.rak).with_name("passive.json").write_text(json.dumps(rows, indent=1))
    print(md)
    return 0


def self_test() -> int:
    l98 = ("2026-11-24 16:00:01.000  16:00:01 [LOG] 049 : x00000001 H04 S0 T0 M00 OE1ABC-3>9999:x HW:09 MOD:8/8 "
           "FCS:0 FW:40:a LH:89 RSSI:-120 SNR:-12 DUP:n OWN:- t=1")
    lrly = ("2026-11-24 16:00:02.000  16:00:02 [LOG] 049 : x00000002 H03 S0 T0 M00 OE1ABC-3,OE1XYZ-1>9999:x HW:09 "
            "MOD:8/8 FCS:0 FW:40:a LH:89 RSSI:-90 SNR:2 DUP:n OWN:- t=1")
    lrak = "1795532401.000 16:00:01 [LOG] 049 : x00000001 H04 S0 T0 M00 OE1ABC-3>9999:x HW:09 RSSI:-118 SNR:-11 DUP:n"
    assert parse(l98)[:4] == ("00000001", "OE1ABC-3", True, 9)
    assert parse(lrly)[1:4] == ("OE1XYZ-1", False, 9)
    assert chip_class(9).startswith("nRF52") and chip_class(43) == "SX126x ESP32" and chip_class(4) == "SX127x"
    t = epoch_of(l98)
    ref = frames([l98, lrly], t - 1, t + 5)
    rak = {"00000001/OE1ABC-3": parse(lrak.replace("HW:09", "HW:09 LH:89"))[1:]}
    rows = {(r["class"], r["snr_at_98"]): r for r in compare(rak, ref)}
    r = rows[("nRF52 (0x242B)", "all")]
    assert (r["ref"], r["rak"], r["ratio"]) == (2, 1, 0.5), r  # the relayed copy (OE1XYZ-1) was not at the RAK
    assert rows[("nRF52 (0x242B)", "-99..-10")]["ref"] == 1
    print("syncword_passive self-test ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("capture")
    c.add_argument("--rak-port", required=True)
    c.add_argument("--minutes", type=float, default=60)
    c.add_argument("--out", required=True)
    k = sub.add_parser("compare")
    k.add_argument("--rak", required=True)
    k.add_argument("--ref", required=True)
    k.add_argument("--label", default="")
    k.add_argument("--start", type=float)
    k.add_argument("--end", type=float)
    sub.add_parser("self-test")
    a = ap.parse_args()
    if a.cmd == "capture":
        return cmd_capture(a)
    if a.cmd == "compare":
        return cmd_compare(a)
    if a.cmd == "self-test":
        return self_test()
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
