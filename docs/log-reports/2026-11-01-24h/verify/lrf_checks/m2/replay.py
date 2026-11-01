#!/usr/bin/env python3
"""M2: replay of own prio-4/5 TX pool entries against the D2 age-floor variants.

Usage: python3 -I replay.py <norm_dir> <out_json> [--gate-ms 120] [--err-air-ms 500]
                            [--nodes oe3xoc,oe3xia]

Input: normalised --loradebug logs <norm_dir>/<node>.log (lines "YYYY-MM-DD HH:MM:SS.mmm  body").

Model (first order, see verify/lrf_m2.md):
  * Entry = RING_WRITE+RING_PRIO of an own prio-4/5 slot (src user_wx / auto_pos).
    Own WX/position: type 21 src user_wx (prio 4, TTL 120 s); own HEY: type 40 src auto_pos
    (prio 5, TTL 180 s). Measured end: tx (RING_TX_READ) / ttl (RING_DROP_STALE) /
    superseded / evicted / overwritten / open.
  * Head = alive entry with lowest (prio, enqueue time), as getNextTxSlot(). Alive set is
    rebuilt from all RING_* lines of the node (pre-log occupants are unknown: own entries
    enqueued in the first 300 s of the log are skipped).
  * Arm events while the entry is head: end of every received frame (ONRXDONE_TIME line;
    age floor applies) and end of every own TX (OnTXDone; csma_reset(), nofloor, attempt 0).
  * Window after an arm event r: silence until the start of the next frame
    (end - airtime(len), len from RX_FRAME; RX errors count as a frame of --err-air-ms;
    own TX start counts), capped by the end of the head interval and the entry horizon.
  * Hold = csma_compute_timeout_slot(): today (5500 ms base for prio 4/5, 5/6 at attempt 1,
    2/3 at attempt 2, 100 ms at attempt >= 3; + 0..10 x 35 ms), then the variant's floor
    (csmaApplyAgeFloor semantics: min(today, aged), RX re-arms only, attempt < 3 only).
    cad_attempt is taken from the log (CAD_BUSY attempt=N, reset by CAD_FREE / OnTXDone).
  * Predicted TX = first arm event with r + hold + gate <= window end. Gate = CAD scan (~120 ms).
  * Jitter modes: worst (max jitter), mean (mean jitter), prob (exact enumeration of the
    uniform jitter draws; probabilities / expected counts).
"""
from __future__ import annotations

import json
import zlib
import math
import re
import sys
from bisect import bisect_left, bisect_right
from datetime import datetime

TS = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  ")
W = re.compile(r"\] RING_WRITE slot=(\d+) type=([0-9A-F]+) status=([0-9A-F]+) len=(\d+) msg_id=([0-9A-F]{8}) queued=(\d+)/\d+ src=(\w+)")
P = re.compile(r"\] RING_PRIO slot=(\d+) prio=(\d)")
SUP = re.compile(r"\] RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]+) new_id=([0-9A-F]+)")
DPR = re.compile(r"\] RING_DROP_PRIO slot=(\d+) prio=\d type=[0-9A-F]+ msg_id=([0-9A-F]+)")
TXR = re.compile(r"\] RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]+) status=[0-9A-F]+ len=(\d+) msg_id=([0-9A-F]+) retry=\d+ queued=(\d+)/\d+ lat=(\d+)ms")
STL = re.compile(r"\] RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]+) reason=(\w+)")
CADB = re.compile(r"CAD_BUSY attempt=(\d+)")
RXF = re.compile(r"RX_FRAME len=(\d+)")
GATE = re.compile(r"cad_attempt=(\d+)")
RADIO = re.compile(r"RADIO_TX len=(\d+)")

# firmware constants (src/configuration_global.h, src/csma_timing.h)
BASE = {1: 3000, 2: 3000, 3: 4500, 4: 5500, 5: 5500}
SLOT = 35
SLOTS_T = 10          # CSMA_PRIO_SLOTS_4/5
SLOTS_A = 5           # CSMA_AGE_SLOTS
RAPID = 100           # CSMA_RAPID_RX_MS
MAXATT = 3            # CSMA_MAX_ATTEMPTS
ACKWIN, GUARD, SMIN, SMAX = 2850, 175, 500, 1500
TTL_MS = {4: 120000, 5: 180000}   # RING_TTL_POS_MS (prio 4, DONE), RING_BG_MAX_AGE_MS (prio 5)
TTL_TICK_MS = 1000    # txRingAgeBackground 2-s tick: mean +1 s over nominal (measured age_s 121 / 181)
RAPID_LO, RAPID_HI = 100, 275

# variant: (stage1_ms, stage2_ms), 0 = never; None = no floor for prio 4/5 (today)
VARIANTS = {"base": None, "A1": (20000, 60000), "A2": (20000, 0), "A3": (40000, 0)}
MODES = ("worst", "mean", "prob")


def ts(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def air_formula(n: int) -> float:
    """LoRa SF11 BW250 CR4/6 preamble 8, explicit header, CRC (ms)."""
    sf, bw = 11, 250000.0
    nsym = 8 + math.ceil((8 * n - 4 * sf + 28 + 16) / (4 * sf)) * 6
    return (8 + 4.25 + nsym) * (2 ** sf / bw) * 1000


class Node:
    pass


def parse(path: str) -> Node:
    nd = Node()
    nd.rx: list[list] = []        # [t_end, len|None]
    nd.err: list[float] = []
    nd.tx: list[list] = []        # [t_radio_tx, t_done|None, len]
    nd.cad: list[tuple[float, int]] = []
    nd.gates: list[list] = []     # [t, attempt, outcome free|busy|None]
    gate_cur = None
    nd.entries: list[dict] = []
    occ: dict[int, dict] = {}
    last_write = None
    last_txread = None
    nd.t0 = nd.t1 = None
    with open(path, "rb") as fh:
        for raw in fh:
            line = raw.decode("latin-1", "replace")
            m0 = TS.match(line)
            if not m0:
                continue
            t = ts(m0.group(1))
            if nd.t0 is None:
                nd.t0 = t
            nd.t1 = t
            b = line[25:]
            if b.startswith("[MC-DBG] "):
                if b.startswith("[MC-DBG] RING_"):
                    if (m := W.search(b)):
                        slot = int(m.group(1))
                        old = occ.get(slot)
                        e = {"t": t, "slot": slot, "type": m.group(2), "len": int(m.group(4)), "id": m.group(5),
                             "src": m.group(7), "prio": None, "end": None, "tend": None, "age_s": None,
                             "lat": None, "prev": None, "air": None}
                        if old is not None and old["end"] is None:
                            old["end"], old["tend"] = "overwritten", t
                            e["_old"] = old
                        occ[slot] = e
                        nd.entries.append(e)
                        last_write = e
                    elif (m := P.search(b)):
                        if last_write and int(m.group(1)) == last_write["slot"]:
                            last_write["prio"] = int(m.group(2))
                    elif (m := SUP.search(b)):
                        cur = occ.get(int(m.group(1)))
                        if cur and cur.get("_old") and cur["_old"]["id"] == m.group(2):
                            cur["_old"]["end"] = "superseded"
                            cur["prev"] = cur["_old"]
                    elif (m := DPR.search(b)):
                        cur = occ.get(int(m.group(1)))
                        if cur and cur.get("_old") and cur["_old"]["id"] == m.group(2):
                            cur["_old"]["end"] = "evicted"
                    elif (m := TXR.search(b)):
                        cur = occ.get(int(m.group(1)))
                        if cur and cur["id"] == m.group(5) and cur["end"] is None:
                            cur["end"], cur["tend"] = "tx", t
                            cur["lat"] = int(m.group(7)) / 1000
                            last_txread = cur
                    elif (m := STL.search(b)):
                        cur = occ.get(int(m.group(1)))
                        if cur and cur["id"] == m.group(4) and cur["end"] is None:
                            cur["end"], cur["tend"], cur["age_s"] = "ttl", t, int(m.group(3))
                elif b.startswith("[MC-DBG] ONRXDONE_TIME"):
                    nd.rx.append([t, None])
                elif b.startswith("[MC-DBG] RX_ERROR"):
                    nd.err.append(t)
                elif b.startswith("[MC-DBG] RADIO_TX len="):
                    mm = RADIO.search(b)
                    nd.tx.append([t, None, int(mm.group(1))])
                    if last_txread is not None and t - last_txread["tend"] < 1.0:
                        last_txread["_txi"] = len(nd.tx) - 1
                        last_txread = None
                elif b.startswith("[MC-DBG] TX_GATE_ENTER"):
                    gate_cur = [t, int(GATE.search(b).group(1)), None]
                    nd.gates.append(gate_cur)
                elif b.startswith("[MC-DBG] CAD_BUSY attempt="):
                    nd.cad.append((t, int(CADB.search(b).group(1))))
                    if gate_cur and gate_cur[2] is None:
                        gate_cur[2] = "busy"
                elif b.startswith("[MC-DBG] CAD_FREE"):
                    nd.cad.append((t, 0))
                    if gate_cur and gate_cur[2] is None:
                        gate_cur[2] = "free"
            elif b.startswith("[MC-TEST] RX_FRAME len="):
                if nd.rx and nd.rx[-1][1] is None and t - nd.rx[-1][0] < 1.0:
                    nd.rx[-1][1] = int(RXF.search(b).group(1))
            elif b.startswith("OnTXDone"):
                if nd.tx and nd.tx[-1][1] is None:
                    nd.tx[-1][1] = t
                    nd.cad.append((t, 0))
    nd.cad.sort()
    for e in nd.entries:
        i = e.pop("_txi", None)
        if i is not None and nd.tx[i][1] is not None:
            e["air"] = (nd.tx[i][1] - nd.tx[i][0]) * 1000
        e.pop("_old", None)
    return nd


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else None


def cls_of(e) -> str | None:
    if e["prio"] == 4 and e["src"] == "user_wx" and e["type"] == "21":
        return "WX"
    if e["prio"] == 5 and e["src"] == "auto_pos" and e["type"] == "40":
        return "HEY"
    return None


# ---------------------------------------------------------------- hold model
def today_hold(attempt: int, prio: int, jit) -> float:
    """csma_compute_timeout_prio(); jit = jitter slots (0..10)."""
    if attempt >= MAXATT:
        return RAPID
    base = BASE[prio]
    if attempt >= 2:
        base = base * 2 // 3
    elif attempt >= 1:
        base = base * 5 // 6
    return base + jit * SLOT


def aged_hold(stage: int, air_ms: float, jit) -> float:
    """csmaAgedHoldMs(); jit = jitter slots (0..5)."""
    h = RAPID if stage >= 2 else min(max(ACKWIN - air_ms - GUARD, SMIN), SMAX)
    return h + jit * SLOT


def stage_of(age_ms: float, st) -> int:
    s1, s2 = st
    if s2 != 0 and age_ms >= s2:
        return 2
    if s1 != 0 and age_ms >= s1:
        return 1
    return 0


def build(nd: Node, err_air_ms: float):
    """frame list sorted by end (barriers), arm events sorted by time."""
    diffs = sorted((t[1] - t[0]) * 1000 - air_formula(t[2]) for t in nd.tx if t[1] is not None)
    nd.air_off = diffs[len(diffs) // 2] if diffs else 0.0   # fit of the formula to measured TX airtime
    rxf = []
    for t, n in nd.rx:
        a = (air_formula(n) + nd.air_off) / 1000 if n else 0.5
        rxf.append((t, t - a))
    nd.fr_end_rx = [f[0] for f in rxf]       # decoded frames only (for measure_q)
    nd.fr_start_rx = [f[1] for f in rxf]
    fr = list(rxf)
    if err_air_ms > 0:                       # optional: RX errors as barriers (default off, see lrf_m2.md)
        fr += [(t, t - err_air_ms / 1000) for t in nd.err]
    fr += [(t1, t0) for t0, t1, _ in nd.tx if t1 is not None]   # own TX
    fr.sort()
    nd.fr_end = [x[0] for x in fr]
    nd.fr_start = [x[1] for x in fr]
    arms = sorted([(t, "rx") for t, _ in nd.rx] + [(t1, "tx") for _, t1, _ in nd.tx if t1 is not None])
    nd.arm_t = [a[0] for a in arms]
    nd.arm_k = [a[1] for a in arms]
    nd.cad_t = [c[0] for c in nd.cad]


def attempt_at(nd: Node, t: float) -> int:
    i = bisect_right(nd.cad_t, t) - 1
    return nd.cad[i][1] if i >= 0 else 0


def head_intervals(E, horizon, blockers_sorted, bt):
    """complement of blocker lifetimes inside [E.t, horizon]."""
    lo = E["t"]
    ivs = []
    i0 = bisect_left(bt, lo - 400)
    i1 = bisect_right(bt, horizon)
    key = (E["prio"], E["t"])
    for B in blockers_sorted[i0:i1]:
        if B is E:
            continue
        if (B["prio"], B["t"]) >= key:
            continue
        te = B["tend"] if B["tend"] is not None else float("inf")
        if te <= lo or B["t"] >= horizon:
            continue
        ivs.append((max(B["t"], lo), min(te, horizon)))
    ivs.sort()
    free, cur = [], lo
    for a, b in ivs:
        if a > cur:
            free.append((cur, a))
        cur = max(cur, b)
    if cur < horizon:
        free.append((cur, horizon))
    return free


def measure_q(nd: Node) -> dict:
    """P(CAD free | gate entered) per cad_attempt (0,1,2,3+), clean gates only: no decoded
    frame on air between the gate and gate+0.25 s (frames from ONRXDONE_TIME + RX_FRAME len)."""
    fs = sorted(zip(nd.fr_end_rx, nd.fr_start_rx))
    ends = [f[0] for f in fs]
    cnt = {a: [0, 0] for a in range(4)}
    for t, a, o in nd.gates:
        if o is None:
            continue
        i = bisect_left(ends, t)
        busy_by_frame = any(fs[k][0] - t <= 2.0 and fs[k][1] <= t + 0.25 for k in range(i, min(i + 3, len(fs))))
        if busy_by_frame:
            continue
        c = cnt[min(a, 3)]
        c[0] += 1
        c[1] += o == "free"
    return {a: (c[1] / c[0] if c[0] else 1.0) for a, c in cnt.items()}, {a: c[0] for a, c in cnt.items()}


def entry_events(nd: Node, E, horizon, free):
    """arm events inside head intervals."""
    ev = []
    for a, b in free:
        i = bisect_left(nd.arm_t, a)
        if a == E["t"] and i > 0 and a - nd.arm_t[i - 1] < 10.0:   # head from birth: timer already running
            i -= 1
        while i < len(nd.arm_t) and nd.arm_t[i] < b:
            r = nd.arm_t[i]
            j = bisect_right(nd.fr_end, r)
            nxt = nd.fr_start[j] if j < len(nd.fr_end) else float("inf")
            w = min(max(nxt, r), b, horizon)
            ev.append((r, nd.arm_k[i], w, max(0.0, r - E["t"]) * 1000, attempt_at(nd, r)))
            i += 1
    return ev


GATE_OK = 0.120      # fire -> RING_TX_READ with a free channel (median 0.119 s, measured)
GATE_BUSY = 0.220    # fire -> CAD_BUSY decision (CAD scan + double-check, ~0.2 s)


def simulate(ev, prio, air_ms, var, jmode, qmode, q, rng):
    """One run. -> (aired, t_tx, rapid_any, rapid_floor). jmode worst|mean|rand, qmode ideal|cad."""
    st = VARIANTS[var]
    a = ev[0][4] if ev else 0
    for (r, k, w, age, a_log) in ev:
        if qmode == "ideal":
            a = a_log
        elif k == "tx":
            a = 0
        t_arm, first = r, True
        while True:
            if jmode == "worst":
                jt, ja = SLOTS_T, SLOTS_A
            elif jmode == "mean":
                jt, ja = SLOTS_T / 2, SLOTS_A / 2
            else:
                jt, ja = rng.randint(0, SLOTS_T), rng.randint(0, SLOTS_A)
            td = today_hold(a, prio, jt)
            h = td
            if first and k == "rx" and st is not None and a < MAXATT:
                stage = stage_of(age, st)
                if stage:
                    h = min(td, aged_hold(stage, air_ms, ja))
            f = t_arm + h / 1000
            if f + GATE_OK > w:
                break
            if qmode == "ideal" or rng.random() < q[min(a, 3)]:
                rap = RAPID_LO <= h <= RAPID_HI
                return True, f + GATE_OK, rap, rap and td > RAPID_HI
            a = min(a + 1, MAXATT)
            t_arm, first = f + GATE_BUSY, False
    return False, None, False, False


MODES = ("ideal/worst", "ideal/mean", "cad/worst", "cad/mean", "cad/rand")


def evaluate(ev, prio, air_ms, var, mode, q, nrun, seed):
    import random
    qmode, jmode = mode.split("/")
    det = qmode == "ideal"
    rng = random.Random(seed)
    n = 1 if det else nrun
    na = ts_ = 0.0
    ra = rf = 0.0
    tl = []
    for _ in range(n):
        ok, t, x, y = simulate(ev, prio, air_ms, var, jmode, qmode, q, rng)
        if ok:
            na += 1
            tl.append(t)
            ra += x
            rf += y
    return {"p": round(na / n, 4), "t": round(sum(tl) / len(tl), 1) if tl else None,
            "rapid_any": round(ra / n, 4), "rapid_floor": round(rf / n, 4)}


def run_node(name: str, path: str, nrun: int, err_air_ms: float):
    nd = parse(path)
    build(nd, err_air_ms)
    q, qn = measure_q(nd)
    allp = sorted((e for e in nd.entries if e["prio"] is not None), key=lambda e: e["t"])
    bt = [e["t"] for e in allp]
    res = {"log": path.split("/")[-1], "span_h": round((nd.t1 - nd.t0) / 3600, 2), "rx_frames": len(nd.rx),
           "rx_err": len(nd.err), "own_tx": len(nd.tx), "airtime_offset_ms": round(nd.air_off, 1),
           "own_tx_airtime_s": round(sum(t[1] - t[0] for t in nd.tx if t[1] is not None), 1),
           "cad_free_prob_by_attempt": {str(a): round(v, 3) for a, v in q.items()}, "cad_clean_gates_by_attempt": qn}
    own = [e for e in allp if cls_of(e)]
    air_cls = {}
    for c in ("WX", "HEY"):
        v = [e["air"] for e in own if cls_of(e) == c and e["end"] == "tx" and e["air"] is not None]
        air_cls[c] = (sum(v) / len(v)) if v else None
        res[f"mean_air_ms_{c}"] = round(air_cls[c], 1) if v else None
        res[f"n_air_{c}"] = len(v)
    recs = []
    skipped = 0
    for E in own:
        c = cls_of(E)
        ttl = TTL_MS[E["prio"]] / 1000
        if E["end"] == "ttl":
            hor = E["tend"]
        elif E["end"] in ("superseded", "evicted", "overwritten"):
            hor = min(E["tend"], E["t"] + ttl + TTL_TICK_MS / 1000)
        else:
            hor = E["t"] + ttl + TTL_TICK_MS / 1000
        if E["t"] < nd.t0 + 300 or (E["end"] is None) or hor > nd.t1:
            skipped += 1
            continue
        free = head_intervals(E, hor, allp, bt)
        ev = entry_events(nd, E, hor, free)
        air_ms = air_formula(E["len"]) + nd.air_off    # ring frame length -> airtime for the stage-1 hold
        meas_end = E["tend"] if E["end"] else hor
        head_meas = sum(max(0.0, min(b, meas_end) - a) for a, b in free)
        sils = [round(x[2] - x[0], 3) for x in ev if x[1] == "rx"]
        rec = {"id": E["id"], "cls": c, "t": datetime.fromtimestamp(E["t"]).strftime("%m-%d %H:%M:%S"),
               "end": E["end"], "life_s": round(meas_end - E["t"], 1), "head_s": round(head_meas, 1),
               "n_arm": len(ev), "n_head_iv": len(free), "air_ms": round(air_ms, 1), "chain_prev": E["prev"]["id"] if E["prev"] else None,
               "tx_s": round(E["tend"] - E["t"], 1) if E["end"] == "tx" else None,
               "sil_max": round(max(sils, default=0), 2), "pred": {}, "_sils": sils}
        for var in VARIANTS:
            for mode in MODES:
                # common random numbers across variants: seed depends on entry and mode only
                rec["pred"][f"{var}/{mode}"] = evaluate(ev, E["prio"], air_ms, var, mode, q, nrun, seed=zlib.crc32(f"{E['id']}/{mode}".encode()))
                p_ = rec["pred"][f"{var}/{mode}"]
                if p_["t"] is not None:
                    p_["t"] = round(p_["t"] - E["t"], 1)
        recs.append(rec)
    res["skipped_entries"] = skipped
    ends = [t for t, _ in nd.rx]
    e2e = sorted(b - a for a, b in zip(ends, ends[1:]) if b - a < 600)
    sil = sorted(max(0.0, nd.fr_start_rx[i + 1] - ends[i]) for i in range(len(ends) - 1) if ends[i + 1] - ends[i] < 600)
    res["global_gaps"] = {"n": len(sil), "end_to_end_p50_s": round(pct(e2e, .5), 2), "silence_p50_s": round(pct(sil, .5), 2),
                          "silence_share_ge_1.8s_pct": round(100 * sum(1 for x in sil if x >= 1.8) / len(sil), 1),
                          "silence_share_ge_5.8s_pct": round(100 * sum(1 for x in sil if x >= 5.8) / len(sil), 1)}
    res["entries"] = recs
    res["summary"] = summarise(recs, air_cls)
    return res


def summarise(recs, air_cls):
    out = {}
    for c in ("WX", "HEY"):
        R = [r for r in recs if r["cls"] == c]
        s = {"entries": len(R)}
        meas = {}
        for r in R:
            meas[r["end"] or "open"] = meas.get(r["end"] or "open", 0) + 1
        s["measured_end"] = meas
        byid = {r["id"]: r for r in R}
        nxt = {r["chain_prev"]: r for r in R if r["chain_prev"]}
        starts = [r for r in R if not r["chain_prev"] or r["chain_prev"] not in byid]
        ep = ep_aired = 0
        for st in starts:
            cur, ch = st, [st]
            while cur["id"] in nxt:
                cur = nxt[cur["id"]]
                ch.append(cur)
            ep += 1
            ep_aired += any(x["end"] == "tx" for x in ch)
        s["episodes"], s["episodes_aired_measured"] = ep, ep_aired
        s["episodes_aired_measured_pct"] = round(100 * ep_aired / ep, 1) if ep else None
        s["head_share_of_life_pct"] = round(100 * sum(r["head_s"] for r in R) / max(1e-9, sum(r["life_s"] for r in R)), 1)
        sil = sorted(x for r in R for x in r["_sils"])
        s["rx_arm_events_at_head"] = len(sil)
        s["silence_s_p50_p90_max"] = [pct(sil, .5), pct(sil, .9), sil[-1] if sil else None]
        s["head_intervals"] = sum(r["n_head_iv"] for r in R)
        s["head_s_total"] = round(sum(r["head_s"] for r in R), 1)
        s["rx_arm_silence_share_pct"] = {f">= {th} s": round(100 * sum(1 for x in sil if x >= th) / len(sil), 1) if sil else None
                                         for th in (0.4, 1.8, 5.8)}
        s["variants"] = {}
        for var in VARIANTS:
            for mode in MODES:
                key = f"{var}/{mode}"
                P = lambda r: r["pred"][key]["p"]
                v = {"pred_aired": round(sum(P(r) for r in R), 1)}
                tp = sum(P(r) for r in R if r["end"] == "tx")
                fp = sum(P(r) for r in R if r["end"] != "tx")
                n_m = sum(1 for r in R if r["end"] == "tx")
                v["confusion"] = {"meas_aired_pred_aired": round(tp, 1), "meas_aired_pred_lost": round(n_m - tp, 1),
                                  "meas_lost_pred_aired": round(fp, 1), "meas_lost_pred_lost": round(len(R) - n_m - fp, 1)}
                lost = [r for r in R if r["end"] != "tx"]
                v["mean_p_meas_aired"] = round(tp / n_m, 3) if n_m else None
                v["mean_p_meas_lost"] = round(fp / len(lost), 3) if lost else None
                v["recovered_ttl"] = round(sum(P(r) for r in R if r["end"] == "ttl"), 1)
                bk = key.replace(var + "/", "base/", 1)
                v["recovered_ttl_net_of_base_model"] = round(sum(max(0.0, P(r) - r["pred"][bk]["p"]) for r in R if r["end"] == "ttl"), 1)
                v["extra_other"] = round(sum(P(r) for r in R if r["end"] not in ("tx", "ttl")), 1)
                v["rapid_any"] = round(sum(r["pred"][key]["rapid_any"] for r in R), 1)
                v["rapid_floor"] = round(sum(r["pred"][key]["rapid_floor"] for r in R), 1)
                a = air_cls[c]
                v["extra_airtime_s_ttl_only"] = round(v["recovered_ttl"] * a / 1000, 1) if a else None
                v["extra_airtime_s_all"] = round((v["recovered_ttl"] + v["extra_other"]) * a / 1000, 1) if a else None
                s["variants"][key] = v
        for mode in MODES:
            d = [r["pred"][f"base/{mode}"]["t"] - r["tx_s"] for r in R if r["end"] == "tx" and r["pred"][f"base/{mode}"]["t"] is not None]
            s[f"base_{mode}_time_within_1s_of_pred"] = [sum(1 for r in R if r["end"] == "tx" and r["pred"][f"base/{mode}"]["t"] is not None and abs(r["pred"][f"base/{mode}"]["t"] - r["tx_s"]) <= 1.0),
                                                        sum(1 for r in R if r["end"] == "tx" and r["pred"][f"base/{mode}"]["t"] is not None)]
            s[f"base_{mode}_time_err_s_p10_p50_p90"] = [round(pct(d, .1), 1), round(pct(d, .5), 1), round(pct(d, .9), 1)] if d else None
        out[c] = s
    return out


def main(argv):
    opt, args = {}, []
    it = iter(argv[1:])
    for a in it:
        if a.startswith("--"):
            opt[a] = next(it)
        else:
            args.append(a)
    norm, outp = args[0], args[1]
    nrun = int(opt.get("--runs", 200))
    err_air = float(opt.get("--err-air-ms", 0))
    nodes = opt.get("--nodes", "oe3xoc,oe3xia").split(",")
    out = {"params": {"gate_ok_ms": GATE_OK * 1000, "gate_busy_ms": GATE_BUSY * 1000, "mc_runs": nrun,
                      "err_air_ms": err_air, "ttl_ms": TTL_MS, "ttl_tick_ms": TTL_TICK_MS,
                      "variants": VARIANTS, "modes": MODES, "rapid_window_ms": [RAPID_LO, RAPID_HI]}, "nodes": {}}
    for n in nodes:
        out["nodes"][n] = run_node(n, f"{norm.rstrip('/')}/{n}.log", nrun, err_air)
    for n, r in out["nodes"].items():
        print(f"== {n}: span {r['span_h']} h, rx {r['rx_frames']}, rx_err {r['rx_err']}, own tx {r['own_tx']}, airtime offset {r['airtime_offset_ms']} ms, skipped {r['skipped_entries']}")
        print(f"   global gaps {r['global_gaps']}")
        print(f"   P(CAD free | gate) by attempt {r['cad_free_prob_by_attempt']} (clean gates {r['cad_clean_gates_by_attempt']})")
        for c, s in r["summary"].items():
            print(f"-- {n} {c}: entries {s['entries']} measured {s['measured_end']} episodes {s['episodes']} aired {s['episodes_aired_measured']} ({s['episodes_aired_measured_pct']} %), "
                  f"mean air {r['mean_air_ms_' + c]} ms (n={r['n_air_' + c]}), head share of life {s['head_share_of_life_pct']} %, "
                  f"rx arms at head {s['rx_arm_events_at_head']}, silence p50/p90/max {s['silence_s_p50_p90_max']}")
            for key, v in s["variants"].items():
                print(f"   {key:16s} pred_aired {v['pred_aired']:6} p_aired/p_lost {v['mean_p_meas_aired']}/{v['mean_p_meas_lost']} recov_ttl {v['recovered_ttl']:5} extra_other {v['extra_other']:5} "
                      f"rapid_any {v['rapid_any']:5} rapid_floor {v['rapid_floor']:5} airtime_s {v['extra_airtime_s_all']} conf {list(v['confusion'].values())}")
    for n, r in out["nodes"].items():
        for rec in r["entries"]:
            rec.pop("_sils", None)
    with open(outp, "w") as fh:
        json.dump(out, fh, indent=1)


if __name__ == "__main__":
    main(sys.argv)
