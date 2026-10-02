#!/usr/bin/env python3
"""TM-18 trackball roll test: roll the ball continuously while this runs.

Opens the T-Deck session (the device reboots, ~15 s), then prints the ISR edge
counters and the consumed events every 10 s -- one minute in EDGE mode (edges
counted in the ISR and consumed by mouse_read) and one minute in LEVEL mode
(the original 10 ms level compare). Lost steps show as edges > events in LEVEL
mode; EDGE mode must report edges == events.

    python3 tools/bench/experiments/rolltest.py
"""
import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tdeck_harness as h  # noqa: E402

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--tab", type=int, default=0, help="tab to roll on (default 0; 3 = map)")
ap.add_argument("--seconds", type=int, default=60, help="seconds per mode (default 60)")
ap.add_argument("--modes", default="edge,level", help="modes to run, comma separated (default edge,level)")
args = ap.parse_args()

s = h.TDeckSession("/dev/cu.usbmodem1101", 115200, 40.0,
                   log_path=Path("tools/bench/runs/roll_test.log"))
print("opening session, device reboots ...", flush=True)
s.open()
s.send(f"--tab {args.tab}")
time.sleep(1.0)
s.send("--redrawlog on")
start_idx = s.length()
print(f"READY -- roll the trackball now on tab {args.tab}, keep going for ~{len(args.modes.split(','))*args.seconds} s", flush=True)


def counters(tag: str) -> None:
    idx = s.send("--balledges reset")
    m = s.wait_for(r"\[BALLEDGE\];mode", 3.0, since=idx)
    line = m.string.strip() if m else "no answer"
    mm = re.search(r"edges;r;(\d+);u;(\d+);l;(\d+);d;(\d+);pending;(\d+);(\d+);(\d+);(\d+);events;(\d+)", line)
    if mm:
        e = sum(int(x) for x in mm.groups()[:4]); pend = sum(int(x) for x in mm.groups()[4:8])
        print(f"{tag}: edges={e} (r{mm[1]} u{mm[2]} l{mm[3]} d{mm[4]}) events={mm[9]} pending={pend}", flush=True)
    else:
        print(tag, line, flush=True)


for mode in args.modes.split(","):
    s.send("--balledge on" if mode.strip() == "edge" else "--balledge off")
    time.sleep(0.3)
    counters(f"{mode.upper()} start")
    for i in range(max(1, args.seconds // 10)):
        t_end = time.time() + 10
        while time.time() < t_end:
            s.send("--tft on")
            time.sleep(2.5)
        counters(f"{mode.upper()} +{(i + 1) * 10:3d}s")
s.send("--redrawlog off")
time.sleep(0.3)
# repaint cost per trackball read and the steps-per-read distribution
steps = {}
refr_ms = []
for _, _, l in s.records_since(start_idx):
    m = re.search(r"\[BALL\];.*steps;(\d+)", l)
    if m:
        k = int(m.group(1)); steps[k] = steps.get(k, 0) + 1
    m = re.search(r"\[REFR\];ms;\d+;px;(\d+);t_ms;(\d+)", l)
    if m:
        refr_ms.append(int(m.group(2)))
refr_ms.sort()
if refr_ms:
    print(f"repaints={len(refr_ms)} t_ms p50={refr_ms[len(refr_ms)//2]} p95={refr_ms[int(0.95*(len(refr_ms)-1))]} max={refr_ms[-1]}", flush=True)
print("steps per read:", " ".join(f"{k}x{v}" for k, v in sorted(steps.items())), flush=True)
s.close()
print("=== DONE", flush=True)
