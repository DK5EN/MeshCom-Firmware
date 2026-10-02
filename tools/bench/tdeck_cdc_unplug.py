#!/usr/bin/env python3
"""CDC-01 unplug proof (semi-automated, needs a hand on the cable).

The HWCDC TX path only blocks when the USB host really stops issuing IN
transfers -- pausing the host reader does not do it (macOS keeps draining
into its own buffer), so the operator pulls the cable. The evidence is kept in
RTC memory by the firmware and printed at the next boot as [INSTR-PREV]
(instrument.cpp), because opening the port again resets the chip.

    python3 tools/bench/tdeck_cdc_unplug.py [--port /dev/cu.usbmodemXXXX] [--roll-seconds 30]

Sequence: open (node reboots, wait for the GUI) -> arm -> "UNPLUG NOW" ->
operator rolls the trackball and taps the screen for --roll-seconds ->
"PLUG BACK" -> the script waits for the port, reopens it, reads [INSTR-PREV].
Verdict: gaps == 0 and loop_max_us below the gap threshold.
"""
import argparse, os, re, sys, time
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tdeck_harness import TDeckSession, DEFAULT_PORT

def wait_port(port: str, present: bool, timeout: float) -> bool:
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if os.path.exists(port) == present:
            return True
        time.sleep(0.2)
    return False

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--roll-seconds", type=float, default=30.0)
    ap.add_argument("--log", default=None)
    ap.add_argument("--wait-seconds", type=float, default=600.0, help="how long to wait for the unplug and for the replug (default: 600)")
    a = ap.parse_args()
    log = Path(a.log or f"tdeck_cdc_unplug_{time.strftime('%Y%m%d-%H%M%S')}.log")
    s = TDeckSession(port=a.port, log_path=log)
    s.open()
    s.send("--tft on"); time.sleep(0.3)
    i = s.send("--instreset"); s.wait_for(r"\[INSTR\][; ]reset", 3.0, since=i)   # arms the RTC carry, drops the boot gap
    print(f"\nARMED. UNPLUG the USB cable NOW, then roll the trackball and tap the screen for {int(a.roll_seconds)} s.", flush=True)
    if not wait_port(a.port, False, a.wait_seconds):
        print("port never disappeared -- aborting"); return 2
    t_unplug = time.monotonic()
    print("unplugged; rolling window running ...", flush=True)
    try:
        s._stop.set()
    except Exception:
        pass
    time.sleep(a.roll_seconds)
    print("PLUG the cable BACK IN now.", flush=True)
    if not wait_port(a.port, True, a.wait_seconds):
        print("port never came back -- aborting"); return 2
    time.sleep(2.0)
    s2 = TDeckSession(port=a.port, log_path=log)
    s2.open()                                   # reopening resets the chip; [INSTR-PREV] comes with the boot lines
    m = s2.wait_for(r"\[INSTR-PREV\];valid;(\d);?(?:gaps;(\d+);loop_max_us;(\d+);loop_n;(\d+);up_ms;(\d+);threshold_ms;(\d+)(?:;worst_ms;(\d+);in;(\S+))?)?", 5.0, since=0)
    if not m or m.group(1) != "1":
        print("no valid [INSTR-PREV] line after reboot"); return 2
    gaps, loop_max, loop_n, up_ms, thr = (int(m.group(k)) for k in range(2, 7))
    worst_ms = int(m.group(7)) if m.group(7) else None
    worst_in = m.group(8) or "?"
    away_s = round(time.monotonic() - t_unplug, 1)
    ok = gaps == 0 and loop_max < thr * 1000
    print(f"\n[cdc_unplug] previous boot: gaps={gaps} loop_max_us={loop_max} loop_n={loop_n} up_ms={up_ms} worst={worst_ms} ms in {worst_in} (threshold {thr} ms, cable away ~{away_s} s)")
    print(f"[cdc_unplug] {'PASS' if ok else 'FAIL'} -- log {log}")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
