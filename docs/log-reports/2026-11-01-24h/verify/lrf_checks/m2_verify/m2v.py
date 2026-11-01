#!/usr/bin/env python3
"""Independent verification of LRF M2 (own-beacon age-floor replay). Stdlib only.

Usage: python3 -I m2v.py <cmd> <normdir> [node ...]
  cmd: counts | airtime | gaps | replay | relays | all
Nodes default to oe3xoc oe3xia. Written without reading M2's scripts.
"""
import math
import os
import re
import sys
from datetime import datetime

SF, BW, CRX = 11, 250000.0, 6  # CR 4/6 -> 6 symbols per 4 bits group
TSYM = (2 ** SF) / BW
TTL = {"WX": 120.0, "HEY": 180.0}
OWN = {"user_wx": "WX", "auto_pos": "HEY"}
CAD_S = 0.12

_base = {}


def ts(line):
    d = line[:10]
    b = _base.get(d)
    if b is None:
        try:
            b = datetime.strptime(d, "%Y-%m-%d").timestamp()
        except ValueError:
            return None
        _base[d] = b
    try:
        return b + int(line[11:13]) * 3600 + int(line[14:16]) * 60 + float(line[17:23])
    except ValueError:
        return None


def toa(n, pre):
    pl = max(math.ceil((8 * n - 4 * SF + 28 + 16) / (4.0 * SF)) * CRX, 0)
    return (pre + 4.25 + 8 + pl) * TSYM


R_RW = re.compile(r"RING_WRITE slot=(\d+) type=([0-9A-F]+) status=\S+ len=(\d+) msg_id=([0-9A-F]{8}) queued=\S+ src=(\w+)")
R_PRIO = re.compile(r"RING_PRIO slot=(\d+) prio=(\d)")
R_TXR = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]+) status=\S+ len=(\d+) msg_id=([0-9A-F]{8})")
R_DROP = re.compile(r"RING_DROP_STALE slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]{8}) reason=(\w+)")
R_SUP = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=(\d) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})")
R_RXT = re.compile(r"ONRXDONE_TIME ms=(\d+)")
R_RXF = re.compile(r"RX_FRAME len=(\d+)")
R_CADB = re.compile(r"CAD_BUSY attempt=(\d+)")
R_CADF = re.compile(r"CAD_FREE attempt=(\d+)")


def parse(path):
    ev = []  # (t, kind, data)
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace")
            if len(line) < 26 or line[4] != "-":
                continue
            body = line[25:]
            k = None
            if "ONRXDONE_TIME" in body:
                m = R_RXT.search(body)
                if m:
                    k = ("RXT", int(m.group(1)))
            elif "RX_FRAME" in body:
                m = R_RXF.search(body)
                if m:
                    k = ("RXF", int(m.group(1)))
            elif body.startswith("OnRxDone"):
                k = ("RXD", None)
            elif body.startswith("OnRxError"):
                k = ("RXE", None)
            elif body.startswith("OnTXDone"):
                k = ("TXD", None)
            elif "TX_START" in body:
                k = ("TXS", None)
            elif "RING_" in body:
                for name, rx in (("RW", R_RW), ("PRIO", R_PRIO), ("TXR", R_TXR), ("DROP", R_DROP), ("SUP", R_SUP)):
                    m = rx.search(body)
                    if m:
                        k = (name, m.groups())
                        break
            elif "CAD_" in body:
                m = R_CADF.search(body)
                if m:
                    k = ("CADF", int(m.group(1)))
                else:
                    m = R_CADB.search(body)
                    if m:
                        k = ("CADB", int(m.group(1)))
            if k is None:
                continue
            t = ts(line)
            if t is None:
                continue
            ev.append((t, k[0], k[1]))
    return ev


def own_entries(ev):
    """Own WX/HEY entries with outcome."""
    ent = {}
    order = []
    txr, drop, sup = {}, {}, {}
    for t, k, d in ev:
        if k == "RW" and d[4] in OWN:
            mid = d[3]
            if mid not in ent:
                ent[mid] = dict(id=mid, cls=OWN[d[4]], t=t, len=int(d[2]), slot=int(d[0]))
                order.append(mid)
        elif k == "TXR":
            txr.setdefault(d[4], t)
        elif k == "DROP":
            drop.setdefault(d[3], (t, d[4], int(d[2])))
        elif k == "SUP":
            sup.setdefault(d[2], t)
    for mid in order:
        e = ent[mid]
        if mid in txr:
            e["out"], e["tend"] = "aired", txr[mid]
        elif mid in drop:
            e["out"], e["tend"] = "ttl", drop[mid][0]
            e["drop_reason"] = drop[mid][1]
        elif mid in sup:
            e["out"], e["tend"] = "superseded", sup[mid]
        else:
            e["out"], e["tend"] = "open", None
    return [ent[m] for m in order]


def cmd_counts(nodes, evs):
    print("## counts: own entries by class and outcome")
    print("node cls total aired ttl superseded open | lost share of non-superseded")
    for n in nodes:
        E = own_entries(evs[n])
        t0 = evs[n][0][0]
        t1 = evs[n][-1][0]
        for c in ("WX", "HEY"):
            es = [e for e in E if e["cls"] == c]
            cnt = {o: sum(1 for e in es if e["out"] == o) for o in ("aired", "ttl", "superseded", "open")}
            # in-scope as M2: drop entries in first 300 s or still open at end
            scope = [e for e in es if e["t"] - t0 >= 300 and e["out"] != "open"]
            sc = {o: sum(1 for e in scope if e["out"] == o) for o in ("aired", "ttl", "superseded")}
            print(f"{n} {c} {len(es)} {cnt['aired']} {cnt['ttl']} {cnt['superseded']} {cnt['open']} | scope(>300s,closed) n={len(scope)} {sc}")
        print(f"{n} span {(t1 - t0) / 3600:.2f} h")


def cmd_airtime(nodes, evs):
    print("## airtime: own TX duration TX_START -> OnTXDone (logger clock)")
    for n in nodes:
        ev = evs[n]
        E = {e["id"]: e for e in own_entries(ev)}
        pend = None
        start = None
        dur = {"WX": [], "HEY": [], "other": []}
        lens = {"WX": [], "HEY": [], "other": []}
        allown = []
        for t, k, d in ev:
            if k == "TXR":
                pend = (d[4], int(d[3]))
                start = None
            elif k == "TXS" and pend:
                start = t
            elif k == "TXD" and pend and start is not None:
                mid, ln = pend
                c = E[mid]["cls"] if mid in E else "other"
                dur[c].append(t - start)
                lens[c].append(ln)
                allown.append((ln, t - start))
                pend = None
                start = None
        for c in ("WX", "HEY", "other"):
            v = sorted(dur[c])
            if not v:
                continue
            ml = sorted(lens[c])[len(v) // 2]
            print(f"{n} {c}: n={len(v)} mean={sum(v)/len(v):.3f}s median={v[len(v)//2]:.3f}s median_len={ml} "
                  f"toa(len,pre=8)={toa(ml, 8):.3f} toa(len,pre=32)={toa(ml, 32):.3f}")
        # fit preamble: minimise median residual
        best = None
        for pre in range(4, 40):
            res = sorted(dd - toa(ln, pre) for ln, dd in allown)
            med = res[len(res) // 2]
            if best is None or abs(med) < abs(best[1]):
                best = (pre, med)
        print(f"{n} preamble fit over all own TX (n={len(allown)}): pre={best[0]} median residual={best[1]*1000:.0f} ms")
        tot = sum(sum(v) for v in dur.values())
        print(f"{n} total own TX airtime {tot:.0f} s")


def channel(ev, pre):
    """Arm events and frame starts. Returns list of (t_arm, kind, silence_to_next_frame_or_tx)."""
    rx = []  # (t_arm, t_end, len)
    last_rxt = None
    for t, k, d in ev:
        if k == "RXT":
            if last_rxt is not None:
                rx.append(last_rxt)
            last_rxt = [t, t - d / 1000.0, None]
        elif k == "RXF" and last_rxt is not None and last_rxt[2] is None and t - last_rxt[0] < 1.0:
            last_rxt[2] = d
    if last_rxt is not None:
        rx.append(last_rxt)
    starts = []  # channel occupancy starts: RX frame starts and own TX starts
    arms = []
    for a, end, ln in rx:
        air = toa(ln if ln is not None else 40, pre)
        starts.append((end - air, "rx"))
        arms.append((a, "rx"))
    rxonly = os.environ.get("M2V_RXONLY") == "1"
    cur = None
    for t, k, d in ev:
        if k == "TXR":
            cur = d[4]
        elif k == "TXS" and not rxonly:
            starts.append((t, "tx:" + (cur or "?")))
        elif k == "TXD":
            arms.append((t, "tx"))
    starts.sort()
    arms.sort()
    out = []
    j = 0
    for a, kind in arms:
        while j < len(starts) and starts[j][0] <= a:
            j += 1
        nxt = starts[j][0] if j < len(starts) else None
        # second terminator: the next RX start after a (skipping own TX), for the replay's self-exclusion
        jj = j
        while jj < len(starts) and starts[jj][1] != "rx":
            jj += 1
        nrx = starts[jj][0] if jj < len(starts) else None
        out.append((a, kind, (nxt - a) if nxt is not None else None, starts[j][1] if j < len(starts) else None,
                    (nrx - a) if nrx is not None else None))
    return out, rx


def pct(v, q):
    return v[min(len(v) - 1, int(q * len(v)))]


def cmd_gaps(nodes, evs, pre):
    print(f"## gaps (frame airtime with preamble {pre})")
    for n in nodes:
        ev = evs[n]
        rxd = [t for t, k, d in ev if k == "RXD"]
        rxt = [t for t, k, d in ev if k == "RXT"]
        nrxe = sum(1 for t, k, d in ev if k == "RXE")
        nrxf = sum(1 for t, k, d in ev if k == "RXF")

        def g(v):
            return sorted(b - a for a, b in zip(v, v[1:]) if b - a < 600)

        gd, gt = g(rxd), g(rxt)
        print(f"{n}: OnRxDone lines={len(rxd)} ONRXDONE_TIME={len(rxt)} RX_FRAME={nrxf} OnRxError={nrxe}")
        print(f"  end-to-end gap median: OnRxDone-only {pct(gd, .5):.2f}s | all ONRXDONE_TIME {pct(gt, .5):.2f}s")
        arms, rx = channel(ev, pre)
        sil = sorted(s for a, k, s, nk, _ in arms if k == "rx" and s is not None and s < 600)
        print(f"  silence after RX arm to next frame/own-TX start: n={len(sil)} median={pct(sil, .5):.2f}s")
        for th in (1.8, 1.795, 5.8, 5.97):
            print(f"    share >= {th}s: {100.0 * sum(1 for s in sil if s >= th) / len(sil):.1f} %")
        # rx frames with missing RX_FRAME pairing
        print(f"  RX arms without RX_FRAME len: {sum(1 for r in rx if r[2] is None)}")


def heads(ev, arms):
    """For each arm event, the head entry at that moment (pool reconstructed from RING_*)."""
    slots = {}  # slot -> [prio, enq, id]
    pend_prio = {}
    res = []
    ai = 0
    ring = [e for e in ev if e[1] in ("RW", "PRIO", "TXR", "DROP", "SUP")]
    ri = 0
    while ai < len(arms):
        a = arms[ai]
        while ri < len(ring) and ring[ri][0] <= a[0]:
            t, k, d = ring[ri]
            if k == "RW":
                s = int(d[0])
                slots[s] = [None, t, d[3], d[1]]
            elif k == "PRIO":
                s = int(d[0])
                if s in slots and slots[s][0] is None:
                    slots[s][0] = int(d[1])
            elif k == "TXR":
                s = int(d[0])
                if s in slots and slots[s][2] == d[4]:
                    del slots[s]
            elif k == "DROP":
                s = int(d[0])
                if s in slots and slots[s][2] == d[3]:
                    del slots[s]
            elif k == "SUP":
                pass  # the RW of the new id already replaced the slot
            ri += 1
        best = None
        for s, (p, enq, mid, ty) in slots.items():
            pp = p if p is not None else 3
            key = (pp, enq)
            if best is None or key < best[0]:
                best = (key, mid, enq)
        res.append(best)
        ai += 1
    return res


def stage_hold(variant, age, base, jw):
    if variant == "base":
        return base
    s1, s2 = {"A1": (20, 60), "A2": (20, None), "A3": (40, None)}[variant]
    if s2 is not None and age >= s2:
        return min(base, 0.100 + jw)
    if age >= s1:
        return min(base, 1.500 + jw)  # own WX/HEY airtime >= 0.44 s -> clamp 1500 ms
    return base


def cmd_replay(nodes, evs, pre):
    print(f"## simplified replay (pre={pre}); ideal = CAD always free, plite = per-opportunity P(CAD free) = node CAD_FREE/(CAD_FREE+CAD_BUSY)")
    print("   arms: RX end (floor applies) and own TX end (no floor). Hold must expire + 0.12 s CAD before next frame/own-TX start.")
    tot = {}
    for n in nodes:
        ev = evs[n]
        E = own_entries(ev)
        Eid = {e["id"]: e for e in E}
        t0 = ev[0][0]
        nf = sum(1 for t, k, d in ev if k == "CADF")
        nb = sum(1 for t, k, d in ev if k == "CADB")
        p = nf / (nf + nb)
        arms, _ = channel(ev, pre)
        hd = heads(ev, [(a[0],) for a in arms])
        opp = {}  # id -> list of (t_arm, kind, silence)
        for (a, kind, sil, nk, silrx), h in zip(arms, hd):
            if h is None or sil is None:
                continue
            mid = h[1]
            if nk == "tx:" + mid:  # the entry's own real TX does not end its own silence
                sil = silrx if silrx is not None else 1e9
            if mid in Eid:
                opp.setdefault(mid, []).append((a, kind, sil))
        print(f"{n}: P(CAD free)={p:.3f}")
        for c in ("WX", "HEY"):
            es = [e for e in E if e["cls"] == c and e["t"] - t0 >= 300 and e["out"] != "open"]
            for jl, jb, jv in (("worst", 0.350, 0.175), ("mean", 0.175, 0.0875)):
                row = []
                for var in ("base", "A1", "A2", "A3"):
                    jw = jb if var == "base" else jv
                    pred_air_ideal = 0
                    pred_air_p = 0.0
                    rec_ideal = 0
                    rec_p = 0.0
                    rapid = 0
                    rapid_all = [0, 0.0]
                    for e in es:
                        deadline = e["tend"] if e["out"] != "aired" else e["t"] + TTL[c]
                        q = 1.0
                        first = None
                        for a, kind, sil in opp.get(e["id"], []):
                            if a >= deadline:
                                break
                            age = a - e["t"]
                            base = 5.5 + jb
                            hold = base if kind == "tx" else stage_hold(var, age, base, jw)
                            if sil >= hold + CAD_S and a + hold < deadline:
                                if first is None:
                                    first = (a, hold)
                                if hold <= 0.275:
                                    rapid_all[1] += q * p
                                q *= (1 - p)
                        pa = 1 - q
                        if first is not None:
                            pred_air_ideal += 1
                            if first[1] <= 0.275:
                                rapid_all[0] += 1
                        pred_air_p += pa
                        if e["out"] == "ttl":
                            if first is not None:
                                rec_ideal += 1
                                if first[1] <= 0.275:
                                    rapid += 1
                            rec_p += pa
                    row.append((var, pred_air_ideal, pred_air_p, rec_ideal, rec_p, f"{rapid}/{rapid_all[0]}/p{rapid_all[1]:.1f}"))
                    tk = (var, jl)
                    tt = tot.setdefault(tk, [0, 0.0, 0])
                    if var != "base":
                        tt[0] += rec_ideal
                        tt[1] += rec_p
                        tt[2] += rapid
                meas = sum(1 for e in es if e["out"] == "aired")
                lost = sum(1 for e in es if e["out"] == "ttl")
                s = " | ".join(f"{v}: air i{ai}/p{ap:.1f} rec_lost i{ri}/p{rp:.1f} rapid(lost-i/all-i/all-p){rr}" for v, ai, ap, ri, rp, rr in row)
                print(f"  {c} {jl} n={len(es)} measured aired={meas} ttl={lost} || {s}")
    print("## totals (both nodes, WX+HEY, TTL-lost entries recovered): ideal / plite / ideal-first-TX-in-100-275ms")
    for (var, jl), v in sorted(tot.items()):
        if var != "base":
            print(f"  {var} {jl}: {v[0]} / {v[1]:.1f} / {v[2]}")


def cmd_relays(nodes, evs, allnodes):
    print("## relays per aired own beacon: neighbour RING_TX_READ same msg_id in [t-2, t+300] s")
    idx = {}
    span = {}
    for m in allnodes:
        ev = evs[m]
        if not ev:
            continue
        span[m] = (ev[0][0], ev[-1][0])
        d = {}
        srcmap = {}
        for t, k, x in ev:
            if k == "TXR":
                d.setdefault(x[4], []).append(t)
            elif k == "RW":
                srcmap.setdefault(x[3], x[4])
        idx[m] = (d, srcmap)
    for n in nodes:
        E = own_entries(evs[n])
        others = [m for m in idx if m != n]
        for c in ("WX", "HEY"):
            aired = [e for e in E if e["cls"] == c and e["out"] == "aired"]
            hist = {}
            tot = 0
            outside = 0
            nonrelay = 0
            lags = []
            for e in aired:
                t = e["tend"]
                k = 0
                for m in others:
                    d, srcmap = idx[m]
                    a, b = span[m]
                    hits = [x for x in d.get(e["id"], []) if t - 2 <= x <= t + 300]
                    outside += sum(1 for x in d.get(e["id"], []) if not (t - 2 <= x <= t + 300))
                    if hits:
                        lags.append(min(hits) - t)
                        k += 1
                        if srcmap.get(e["id"]) not in ("rx_relay",):
                            nonrelay += 1
                hist[k] = hist.get(k, 0) + 1
                tot += k
            mean = tot / len(aired) if aired else 0
            h = " / ".join(str(hist.get(i, 0)) for i in range(0, max(hist) + 1)) if hist else ""
            print(f"{n} {c}: aired={len(aired)} mean={mean:.2f} hist0..={h} hits-outside-window={outside} hits-not-rx_relay-src={nonrelay}")
            if lags:
                lags.sort()
                print(f"    first-hit lag s: min={lags[0]:.1f} median={lags[len(lags)//2]:.1f} max={lags[-1]:.1f} "
                      f"share<=60s={100.0*sum(1 for x in lags if x <= 60)/len(lags):.0f}% n<0={sum(1 for x in lags if x < 0)}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    cmd, nd = sys.argv[1], sys.argv[2]
    nodes = sys.argv[3:] or ["oe3xoc", "oe3xia"]
    allnodes = nodes
    if cmd in ("relays", "all"):
        allnodes = sorted(f[:-4] for f in os.listdir(nd) if f.endswith(".log"))
    evs = {m: parse(os.path.join(nd, m + ".log")) for m in set(allnodes) | set(nodes)}
    pre = int(os.environ.get("M2V_PRE", "8"))
    if cmd in ("counts", "all"):
        cmd_counts(nodes, evs)
    if cmd in ("airtime", "all"):
        cmd_airtime(nodes, evs)
    if cmd in ("gaps", "all"):
        cmd_gaps(nodes, evs, pre)
    if cmd in ("replay", "all"):
        cmd_replay(nodes, evs, pre)
    if cmd in ("relays", "all"):
        cmd_relays(nodes, evs, allnodes)


if __name__ == "__main__":
    main()
