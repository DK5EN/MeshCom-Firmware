#!/usr/bin/env python3
"""OLED-board bench harness (Heltec V3/V4, T-Beam, ... -- every U8g2 display
board) driven over the USB console, sister of tdeck_harness.py.

    python3 tools/bench/oled_harness.py --list
    python3 tools/bench/oled_harness.py --scenario all
    python3 tools/bench/oled_harness.py --scenario pages,display --port /dev/cu.usbserial-573C0005841

Opening the port reboots the node; the run waits for CLIENT STARTED and
[BOOT];ready, then drives the firmware's test hooks: --btn click/double/triple
(the OneButton handlers that switch pages), --injectmsg (text message as if
received via LoRa -> message page), --injectpos (position frame -> position
page), --display off/on, --oledstat (page state), --oledlog on ([OLED];frame
per push with its duration), --instr/--instreset. Every scenario asserts on
those lines; whether the pixels are right stays the operator's eye test.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tdeck_harness import TDeckSession  # noqa: E402

DEFAULT_PORT = "/dev/cu.usbserial-0001"      # Heltec V3 DK5EN-93
CRASH = r"Guru Meditation|rst:0x|Backtrace:|abort\(\)|assert failed"
PAGE_MAX = 6


class OledSession(TDeckSession):
    def __init__(self, port: str, boot_timeout: float, ready_timeout: float) -> None:
        super().__init__(port=port, boot_timeout=boot_timeout, ready_timeout=ready_timeout,
                         log_path=Path(f"oled_run_{time.strftime('%Y%m%d-%H%M%S')}.log"))
        self.probe_cmd = "--oledstat"
        self.probe_pattern = r"\[OLEDSTAT\]"
        self.wake_cmd = None


def oledstat(s: OledSession) -> Optional[Dict[str, int]]:
    idx = s.send("--oledstat")
    m = s.wait_for(r"\[OLEDSTAT\];(.*)", 2.0, since=idx)
    if m is None:
        return None
    parts = m.group(1).split(";")
    out: Dict[str, int] = {}
    for k, v in zip(parts[0::2], parts[1::2]):
        try:
            out[k] = int(v)
        except ValueError:
            pass
    return out


def frames_since(s: OledSession, idx: int) -> List[Dict[str, int]]:
    out = []
    for _, _, l in s.records_since(idx):
        m = re.search(r"\[OLED\];frame;us;(\d+);n;(\d+);page;(-?\d+);last;(-?\d+);lines;(\d+)", l)
        if m:
            out.append({"us": int(m[1]), "n": int(m[2]), "page": int(m[3]), "last": int(m[4]), "lines": int(m[5])})
    return out


def step(s: OledSession, cmd: str, ack: str, settle: float = 1.2) -> Dict[str, Any]:
    """Send a command with the frame log on; report ack, frames pushed and crash."""
    idx = s.send(cmd)
    m = s.wait_for(ack + "|" + CRASH, 3.0, since=idx)
    time.sleep(settle)
    fr = frames_since(s, idx)
    crashed = m is not None and re.search(CRASH, m.string) is not None
    return {
        "cmd": cmd, "acked": m is not None and not crashed,
        "ack_line": m.string.strip()[:100] if m else None,
        "frames": len(fr), "frame_us_max": max((f["us"] for f in fr), default=None),
        "page_after": fr[-1]["page"] if fr else None, "crashed": crashed,
    }


# ------------------------------------------------------------------ scenarios

def scenario_boot(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Boot log: version, display init line (Wire1 ack on Heltec), ready time,
    crash markers, initial page state."""
    lines = [l for _, _, l in s.records_since(0)]
    ver = next((l for l in lines if re.search(r"\[INIT\]\.\.\.build|MeshCom 4\.", l)), None)
    oled = next((l for l in lines if "OLED on Wire" in l or "Display type" in l or "OLED Display is" in l), None)
    crashes = [l for l in lines if re.search(CRASH, l)][1:]   # [0] is our own port-open reset
    st = oledstat(s)
    ok = st is not None and st.get("u8g2", 0) == 1 and not crashes
    return {"ok": ok, "version": ver, "display_init": oled, "ready_ms": s.boot_ready_ms,
            "crash_lines": crashes[:3], "oledstat": st}


def scenario_pages(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Button single clicks through the message ring (PAGE_MAX + 1 clicks):
    every click must be acknowledged and push a frame; the page pointer must
    move; no crash."""
    for _ in range(8):                      # let the post-message hold expire
        st0 = oledstat(s)
        if st0 and st0.get("hold", 1) == 0:
            break
        time.sleep(1.0)
    s.send("--oledlog on"); time.sleep(0.2)
    before = oledstat(s)
    steps = [step(s, "--btn click", r"\[BTN\];click") for _ in range(PAGE_MAX + 1)]
    s.send("--oledlog off")
    after = oledstat(s)
    pages = [st["page_after"] for st in steps if st["page_after"] is not None]
    ok = (all(st["acked"] for st in steps) and not any(st["crashed"] for st in steps)
          and sum(st["frames"] for st in steps) >= len(steps) and len(set(pages)) >= 2)
    return {"ok": ok, "steps": steps, "pages_seen": pages, "before": before, "after": after}


def scenario_inject(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--injectmsg N times: [INJECT];ok, a frame per message, the ring's last
    pointer advancing, no crash."""
    s.send("--oledlog on"); time.sleep(0.2)
    before = oledstat(s)
    steps = []
    for i in range(args.inject_count):
        steps.append(step(s, f"--injectmsg 9999 oled bench {i}", r"\[INJECT\];ok", settle=args.inject_spacing))
    s.send("--oledlog off")
    after = oledstat(s)
    advanced = before is not None and after is not None and after.get("last") != before.get("last")
    ok = (all(st["acked"] and st["frames"] >= 1 for st in steps) and not any(st["crashed"] for st in steps)
          and advanced)
    return {"ok": ok, "steps": steps, "last_before": before.get("last") if before else None,
            "last_after": after.get("last") if after else None}


def scenario_pos(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--injectpos: a position frame from a foreign station must be
    acknowledged and drawn (position page / distance line)."""
    # sendDisplayPosition() draws nothing while the message ring is held
    # (pageHold > 0 for ~5 s after a message) or with the position display
    # off (--msg mode); wait the hold out and switch to --all for the test.
    st0 = None
    for _ in range(12):
        st0 = oledstat(s)
        if st0 and st0.get("hold", 1) == 0:
            break
        time.sleep(1.0)
    if st0 and st0.get("offwait_ms", 0) > 0:
        time.sleep(min(60.0, st0["offwait_ms"] / 1000.0 + 0.5))   # text shown before: wait it out
    restore_msg = bool(st0 and st0.get("posdisp") == 0)
    if restore_msg:
        s.send("--all"); time.sleep(0.5)
    s.send("--oledlog on"); time.sleep(0.2)
    st = step(s, "--injectpos DK5EN-77 48.41 11.75", r"\[INJECTPOS\];ok", settle=2.5)
    for _ in range(12):                     # a drawn position holds the page again
        stx = oledstat(s)
        if stx and stx.get("hold", 1) == 0:
            break
        time.sleep(1.0)
    st2 = step(s, "--injectpos DK5EN-78 47.90 11.20", r"\[INJECTPOS\];ok", settle=2.5)
    s.send("--oledlog off")
    if restore_msg:
        s.send("--msg"); time.sleep(0.3)
    ok = st["acked"] and st2["acked"] and st["frames"] >= 1 and st2["frames"] >= 1 and not (st["crashed"] or st2["crashed"])
    return {"ok": ok, "steps": [st, st2], "hold_before": st0.get("hold") if st0 else None,
            "offwait_before_ms": st0.get("offwait_ms") if st0 else None,
            "posdisp_switched": restore_msg}


def scenario_display(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """--display off then on, three times: both are redraw events; the off
    flag must follow, frames must be pushed for both, no crash."""
    s.send("--oledlog on"); time.sleep(0.2)
    steps = []
    # --display prints no ack line of its own: the ack is the off flag in
    # --oledstat following the command.
    for _ in range(3):
        for cmd, want_off in (("--display off", 1), ("--display on", 0)):
            st = step(s, cmd, r"\[OLED\];frame", settle=1.5)
            st["oledstat"] = oledstat(s)
            st["acked"] = bool(st["oledstat"]) and st["oledstat"].get("off") == want_off
            steps.append(st)
    s.send("--oledlog off")
    flags_ok = all(st["acked"] for st in steps)
    ok = (flags_ok and not any(st["crashed"] for st in steps) and all(st["frames"] >= 1 for st in steps))
    return {"ok": ok, "steps": steps, "flags_ok": flags_ok}


def scenario_track(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Triple click toggles the track (GPS) page on and off; the track flag in
    --oledstat must follow and each toggle must redraw."""
    s.send("--oledlog on"); time.sleep(0.2)
    on = step(s, "--btn triple", r"\[BTN\];triple", settle=2.0); on["oledstat"] = oledstat(s)
    off = step(s, "--btn triple", r"\[BTN\];triple", settle=2.0); off["oledstat"] = oledstat(s)
    s.send("--oledlog off")
    ok = (on["acked"] and off["acked"] and not (on["crashed"] or off["crashed"])
          and bool(on["oledstat"] and on["oledstat"].get("track") == 1)
          and bool(off["oledstat"] and off["oledstat"].get("track") == 0)
          and on["frames"] >= 1 and off["frames"] >= 1)
    return {"ok": ok, "on": on, "off": off}


def scenario_timing(s: OledSession, args: argparse.Namespace) -> Dict[str, Any]:
    """Frame push cost and loop stalls over a window with one injected message
    per 5 s: [INSTR-FLUSH] avg/max (the OLED push) and [INSTR-LOOP] max."""
    idx = s.send("--instreset"); s.wait_for(r"\[INSTR", 2.0, since=idx)
    t_end = time.time() + args.timing_seconds
    i = 0
    while time.time() < t_end:
        s.send(f"--injectmsg 9999 timing {i}"); i += 1
        time.sleep(5.0)
    idx = s.send("--instr")
    lines = s.collect(1.5, since=idx)
    fl = next((l for l in lines if "INSTR-FLUSH" in l), None)
    lp = next((l for l in lines if "INSTR-LOOP" in l), None)
    def field(line: Optional[str], key: str) -> Optional[int]:
        m = re.search(key + r"[; ](\d+)", line or "")
        return int(m.group(1)) if m else None
    flush_avg, flush_max = field(fl, "avg_us"), field(fl, "max_us")
    loop_max = field(lp, "max_us")
    ok = flush_avg is not None and flush_avg < args.flush_budget_us and loop_max is not None and loop_max < args.loop_budget_us
    return {"ok": ok, "flush_n": field(fl, r"\bn"), "flush_avg_us": flush_avg, "flush_max_us": flush_max,
            "loop_max_us": loop_max, "flush_line": fl, "loop_line": lp}


SCENARIOS = {
    "boot": scenario_boot, "pages": scenario_pages, "inject": scenario_inject, "pos": scenario_pos,
    "display": scenario_display, "track": scenario_track, "timing": scenario_timing,
}
ORDER = ["boot", "pos", "inject", "pages", "display", "track", "timing"]
HELP = {
    "boot": "boot log, display init (Wire1 ack), ready time, crash markers",
    "pages": "button clicks through the message ring, a frame per click",
    "inject": "--injectmsg N messages -> message page, ring advances",
    "pos": "--injectpos foreign positions -> position page (runs before any message: a shown text blocks positions for the ping time)",
    "display": "--display off/on x3, both must redraw, off flag follows",
    "track": "triple click: track (GPS) page on and off",
    "timing": "OLED frame push avg/max and loop max over a window",
}


def print_summary(summary: Dict[str, Dict[str, Any]]) -> None:
    for name, r in summary.items():
        print(f"[{name}] {'PASS' if r.get('ok') else 'FAIL'}")
        if name == "boot":
            print(f"  {r.get('version')}\n  {r.get('display_init')}\n  ready_ms={r.get('ready_ms')} oledstat={r.get('oledstat')}")
        elif name in ("pages", "inject", "display"):
            for st in r.get("steps", []):
                extra = f" off={st['oledstat'].get('off')}" if st.get("oledstat") else ""
                print(f"  {st['cmd']:32s} acked={st['acked']} frames={st['frames']} max_us={st['frame_us_max']} page={st['page_after']}{extra}")
            if name == "pages":
                print(f"  pages_seen={r.get('pages_seen')}")
            if name == "inject":
                print(f"  ring last {r.get('last_before')} -> {r.get('last_after')}")
        elif name == "pos":
            for st in r.get("steps", []):
                print(f"  {st['cmd']:40s} acked={st['acked']} frames={st['frames']} max_us={st['frame_us_max']}")
        elif name == "track":
            for k in ("on", "off"):
                st = r[k]; print(f"  {k}: acked={st['acked']} frames={st['frames']} track={st['oledstat'].get('track') if st.get('oledstat') else None}")
        elif name == "timing":
            print(f"  flush n={r.get('flush_n')} avg_us={r.get('flush_avg_us')} max_us={r.get('flush_max_us')}  loop max_us={r.get('loop_max_us')}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--scenario", default="all", help="name, comma list, or all (default all)")
    p.add_argument("--skip", default="", help="comma list to leave out")
    p.add_argument("--list", action="store_true", help="list scenarios and exit")
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--boot-timeout", type=float, default=40.0)
    p.add_argument("--ready-timeout", type=float, default=60.0)
    p.add_argument("--inject-count", type=int, default=4)
    p.add_argument("--inject-spacing", type=float, default=2.0)
    p.add_argument("--timing-seconds", type=float, default=30.0)
    p.add_argument("--flush-budget-us", type=int, default=60000, help="OLED frame push budget (default 60 ms)")
    p.add_argument("--loop-budget-us", type=int, default=250000, help="loop max budget (default 250 ms)")
    p.add_argument("--out", default="oled_summary.json")
    args = p.parse_args(argv)
    if args.list:
        for n in ORDER:
            print(f"  {n:8s} {HELP[n]}")
        return 0
    wanted = ORDER if args.scenario == "all" else [x.strip() for x in args.scenario.split(",") if x.strip()]
    skip = {x.strip() for x in args.skip.split(",") if x.strip()}
    bad = [x for x in wanted + sorted(skip) if x not in SCENARIOS]
    if bad:
        print(f"unknown scenario(s): {bad}", file=sys.stderr); return 2
    order = [x for x in wanted if x not in skip]

    s = OledSession(args.port, args.boot_timeout, args.ready_timeout)
    try:
        s.open()
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr); return 2
    summary: Dict[str, Dict[str, Any]] = {}
    ok = True
    # Track (GPS) page mode is a persisted setting; in that mode single clicks
    # do nothing, positions are suppressed and display toggles draw the track
    # page. Normalise to track off for the run and restore afterwards.
    st0 = oledstat(s)
    track_was_on = bool(st0 and st0.get("track") == 1)
    if track_was_on:
        print("=== node boots in track mode: --track off for the run, restored at the end ===", file=sys.stderr)
        s.send("--track off"); time.sleep(1.5)
    try:
        for name in order:
            print(f"=== running scenario: {name} ===", file=sys.stderr)
            r = SCENARIOS[name](s, args)
            summary[name] = r
            ok = ok and bool(r.get("ok"))
    finally:
        if track_was_on:
            s.send("--track on"); time.sleep(1.0)
        s.close()
    if track_was_on:
        summary.setdefault("boot", {})["track_was_on"] = True
    print_summary(summary)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, default=str)
    print(f"summary written to {args.out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
