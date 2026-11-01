#!/usr/bin/env python3
"""M5 (a)-(c): OE3XIA-12 RF-path test, inside vs outside the receive silences.

Silence definition (copied from health_checks/xia_tx_heard_by_others.py): consecutive receptions of
oe3xia.log (ONRXDONE_TIME + RX_ERROR, ACKs included) more than 300 s apart; the silence is the open
interval (last reception, next reception). All clock values are logger-local wall time (CET, UTC+1
on 31.10.-01.11.2026), read as naive epoch seconds.

(a) frames logged by the other loggers (oe3xhl, oe3xir, oe3xoc, oe3xwj) whose path's LAST callsign is
    OE3XIA-12 (the transmitter of that hop): RSSI/SNR inside vs outside the silences. 'Outside' is
    limited to the time span in which both oe3xia.log and the receiving logger exist (and has a
    second variant: from the first silence start on, to remove the night hours).
(b) own TX of OE3XIA ('[LOG] TX x<id> <type>'): heard = a frame with the same msg_id at one of the
    other loggers, within +-30 s, last hop OE3XIA-12. ACK TX (type 'A') carry no path at the
    receiver and cannot be matched; they are listed separately.
(c) CAD_SCAN result=-702 is busy, everything else free; share inside vs outside, plus the same
    split for the other loggers over the same wall-clock windows (control).
Usage: python3 -I m5_rf.py <norm-dir> <out.json>
"""
from __future__ import annotations
import bisect, collections, datetime as dt, json, math, re, statistics, sys
from pathlib import Path

PFX = re.compile(r"^(2026-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
FRAME = re.compile(r"\[LOG\] \d{3} (.) x([0-9A-F]{8}) H\d\d .*? M\d\d (\S+?)>.* RSSI:(-?\d+) SNR:(-?\d+)")
TXL = re.compile(r"\[LOG\] TX x([0-9A-F]{8}) (\S) ")
OTH = ["oe3xhl", "oe3xir", "oe3xoc", "oe3xwj"]
ME = "OE3XIA-12"
GAP = 300.0


def t(s: str) -> float:
    return dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=dt.timezone.utc).timestamp()


def fmt(x: float) -> str:
    return dt.datetime.fromtimestamp(x, dt.timezone.utc).strftime("%m-%d %H:%M:%S")


def load(p: Path) -> dict:
    d = {"rx": [], "frames": [], "tx": [], "cad": [], "first": None, "last": None}
    with open(p, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = PFX.match(ln)
            if not m:
                continue
            ts = t(m.group(1)); b = m.group(2)
            if d["first"] is None:
                d["first"] = ts
            d["last"] = ts
            if b.startswith("[MC-DBG] ONRXDONE_TIME ms=") or b.startswith("[MC-DBG] RX_ERROR rssi="):
                d["rx"].append(ts)
            elif b.startswith("[MC-DBG] CAD_SCAN"):
                d["cad"].append((ts, "result=-702" in b))
            elif "[LOG] " in b:
                f = FRAME.search(b)
                if f:
                    d["frames"].append((ts, f.group(2), f.group(3).split(",")[-1], int(f.group(4)), int(f.group(5)), f.group(1)))
                else:
                    x = TXL.search(b)
                    if x:
                        d["tx"].append((ts, x.group(1), x.group(2)))
    d["rx"].sort()
    return d


def inside(x: float, wins: list[tuple[float, float]], starts: list[float]) -> bool:
    i = bisect.bisect_left(starts, x) - 1
    return i >= 0 and wins[i][0] < x < wins[i][1]


def q(v: list[float]) -> dict:
    if not v:
        return {"n": 0}
    s = sorted(v)
    if len(s) >= 2:
        q1, med, q3 = statistics.quantiles(s, n=4, method="inclusive")
    else:
        q1 = med = q3 = s[0]
    return {"n": len(s), "median": med, "q1": q1, "q3": q3, "mean": round(sum(s) / len(s), 2)}


def mwu_p(a: list[float], b: list[float]) -> float | None:
    """Two-sided Mann-Whitney U, normal approximation with tie correction and continuity correction."""
    n1, n2 = len(a), len(b)
    if n1 < 5 or n2 < 5:
        return None
    allv = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(allv); i = 0; tie = 0.0
    while i < len(allv):
        j = i
        while j + 1 < len(allv) and allv[j + 1][0] == allv[i][0]:
            j += 1
        r = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[k] = r
        c = j - i + 1; tie += c ** 3 - c
        i = j + 1
    r1 = sum(r for r, (_, g) in zip(ranks, allv) if g == 0)
    u1 = r1 - n1 * (n1 + 1) / 2
    mu = n1 * n2 / 2; n = n1 + n2
    sd = math.sqrt(n1 * n2 / 12 * ((n + 1) - tie / (n * (n - 1))))
    if sd == 0:
        return 1.0
    z = (abs(u1 - mu) - 0.5) / sd
    return round(math.erfc(max(z, 0) / math.sqrt(2)), 4)


def fisher_p(a: int, n1: int, b: int, n2: int) -> float:
    """Two-sided Fisher exact for a/n1 vs b/n2 (hypergeometric, p <= p_obs)."""
    k = a + b; N = n1 + n2
    lo, hi = max(0, k - n2), min(k, n1)
    pr = lambda x: math.comb(n1, x) * math.comb(n2, k - x) / math.comb(N, k)
    po = pr(a)
    return round(min(1.0, sum(pr(x) for x in range(lo, hi + 1) if pr(x) <= po * (1 + 1e-9))), 4)


def wilson(k: int, n: int) -> list[float]:
    if n == 0:
        return [0, 0]
    z = 1.96; p = k / n; den = 1 + z * z / n
    c = (p + z * z / (2 * n)) / den; h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return [round(100 * (c - h), 1), round(100 * (c + h), 1)]


def share(k: int, n: int) -> dict:
    return {"k": k, "n": n, "pct": round(100 * k / n, 1) if n else None, "wilson95_pct": wilson(k, n)}


def main() -> None:
    norm = Path(sys.argv[1]); out = Path(sys.argv[2])
    xia = load(norm / "oe3xia.log")
    oth = {n: load(norm / f"{n}.log") for n in OTH}
    rx = xia["rx"]
    wins = [(a, b) for a, b in zip(rx, rx[1:]) if b - a > GAP]
    starts = [a for a, _ in wins]
    res: dict = {"definition": "gaps > 300 s between consecutive ONRXDONE_TIME/RX_ERROR in oe3xia.log; logger-local time (CET)",
                 "silences": {"n": len(wins), "total_s": round(sum(b - a for a, b in wins)), "longest_s": round(max(b - a for a, b in wins)),
                              "first_start": fmt(wins[0][0]), "list": [{"start": fmt(a), "end": fmt(b), "dur_s": round(b - a, 1),
                                                                         "start_epoch_local": a, "end_epoch_local": b} for a, b in wins]},
                 "spans": {"oe3xia": [fmt(xia["first"]), fmt(xia["last"])], **{n: [fmt(oth[n]["first"]), fmt(oth[n]["last"])] for n in OTH}}}
    s_first = wins[0][0]
    tot_in = sum(b - a for a, b in wins)

    # (a) RSSI/SNR of frames with last hop = OE3XIA-12
    A: dict = {}
    pooled = {"in_rssi": [], "out_rssi": [], "in_snr": [], "out_snr": []}
    for n in OTH:
        o = oth[n]
        lo = max(xia["first"], o["first"]); hi = min(xia["last"], o["last"])
        sel = [f for f in o["frames"] if f[2] == ME]
        ins = [f for f in sel if inside(f[0], wins, starts)]
        out_all = [f for f in sel if lo <= f[0] <= hi and not inside(f[0], wins, starts)]
        out_late = [f for f in out_all if f[0] >= s_first]
        # air time of the comparison windows, to turn counts into rates
        in_s = sum(max(0.0, min(b, hi) - max(a, lo)) for a, b in wins)
        A[n] = {"span": [fmt(lo), fmt(hi)], "inside_window_s": round(in_s),
                "inside": {"rssi": q([f[3] for f in ins]), "snr": q([f[4] for f in ins]), "frames_per_h": round(len(ins) / in_s * 3600, 1) if in_s else None,
                           "rssi_hist": dict(sorted(collections.Counter(f[3] for f in ins).items())), "snr_hist": dict(sorted(collections.Counter(f[4] for f in ins).items()))},
                "outside_same_span": {"rssi": q([f[3] for f in out_all]), "snr": q([f[4] for f in out_all]),
                                      "frames_per_h": round(len(out_all) / ((hi - lo) - in_s) * 3600, 1),
                                      "rssi_hist": dict(sorted(collections.Counter(f[3] for f in out_all).items())), "snr_hist": dict(sorted(collections.Counter(f[4] for f in out_all).items())),
                                      "mwu_p_rssi": mwu_p([f[3] for f in ins], [f[3] for f in out_all]),
                                      "mwu_p_snr": mwu_p([f[4] for f in ins], [f[4] for f in out_all])},
                "outside_from_first_silence": {"rssi": q([f[3] for f in out_late]), "snr": q([f[4] for f in out_late]),
                                               "mwu_p_rssi": mwu_p([f[3] for f in ins], [f[3] for f in out_late]),
                                               "mwu_p_snr": mwu_p([f[4] for f in ins], [f[4] for f in out_late])}}
        pooled["in_rssi"] += [f[3] for f in ins]; pooled["out_rssi"] += [f[3] for f in out_all]
        pooled["in_snr"] += [f[4] for f in ins]; pooled["out_snr"] += [f[4] for f in out_all]
        # control: the three most frequent other last hops of this logger (receiver side stability)
        cnt = collections.Counter(f[2] for f in o["frames"] if f[2] != ME and lo <= f[0] <= hi)
        ctrl = {}
        for hop, _ in cnt.most_common(3):
            ci = [f for f in o["frames"] if f[2] == hop and inside(f[0], wins, starts)]
            co = [f for f in o["frames"] if f[2] == hop and lo <= f[0] <= hi and not inside(f[0], wins, starts)]
            ctrl[hop] = {"inside_rssi": q([f[3] for f in ci]), "outside_rssi": q([f[3] for f in co]),
                         "mwu_p_rssi": mwu_p([f[3] for f in ci], [f[3] for f in co]),
                         "inside_snr": q([f[4] for f in ci]), "outside_snr": q([f[4] for f in co]),
                         "mwu_p_snr": mwu_p([f[4] for f in ci], [f[4] for f in co])}
        A[n]["control_other_last_hops"] = ctrl
    A["pooled_all_loggers"] = {"note": "mixes receivers with different RSSI levels, not interpretable; per-logger rows are the result", "inside_rssi": q(pooled["in_rssi"]), "outside_rssi": q(pooled["out_rssi"]), "mwu_p_rssi": mwu_p(pooled["in_rssi"], pooled["out_rssi"]),
                               "inside_snr": q(pooled["in_snr"]), "outside_snr": q(pooled["out_snr"]), "mwu_p_snr": mwu_p(pooled["in_snr"], pooled["out_snr"])}
    # the hb9 / db0ais loggers: count frames with last hop OE3XIA-12 (expected 0)
    for n in ("hb9vqq4", "hb9vqq5"):
        c = 0
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                f = FRAME.search(ln)
                if f and f.group(3).split(",")[-1] == ME:
                    c += 1
        A[f"{n}_frames_last_hop_{ME}"] = c
    res["a_rssi_snr"] = A

    # (b) heard share of own TX
    idx: dict = {n: collections.defaultdict(list) for n in OTH}
    for n in OTH:
        for ts, mid, last, *_ in oth[n]["frames"]:
            if last == ME:
                idx[n][mid].append(ts)
    common_lo = max(xia["first"], *(oth[n]["first"] for n in OTH)); common_hi = min(xia["last"], *(oth[n]["last"] for n in OTH))

    def heard(ts: float, mid: str) -> bool:
        return any(abs(ts2 - ts) <= 30 for n in OTH for ts2 in idx[n].get(mid, ()))

    B: dict = {"matching": "same msg_id at >=1 of oe3xhl/xir/xoc/xwj, |dt| <= 30 s, last hop OE3XIA-12",
               "common_span_all_four_loggers": [fmt(common_lo), fmt(common_hi)]}
    groups = {"all_tx_full_capture": (xia["first"], xia["last"]), "common_span": (common_lo, common_hi), "from_first_silence_common_span": (max(common_lo, s_first), common_hi)}
    for gname, (glo, ghi) in groups.items():
        rows: dict = {}
        for where in ("inside", "outside"):
            tx = [x for x in xia["tx"] if glo <= x[0] <= ghi and (inside(x[0], wins, starts) == (where == "inside"))]
            ack = [x for x in tx if x[2] == "A"]; non = [x for x in tx if x[2] != "A"]
            hn = sum(heard(x[0], x[1]) for x in non); ha = sum(heard(x[0], x[1]) for x in ack)
            rows[where] = {"tx_all": len(tx), "heard_all": share(hn + ha, len(tx)), "tx_non_ack": len(non), "heard_non_ack": share(hn, len(non)),
                           "tx_ack_unmatchable": len(ack), "heard_ack_by_pathless_match": ha,
                           "by_type": {ty: share(sum(heard(x[0], x[1]) for x in non if x[2] == ty), sum(1 for x in non if x[2] == ty)) for ty in sorted({x[2] for x in non})}}
        i, o = rows["inside"]["heard_non_ack"], rows["outside"]["heard_non_ack"]
        rows["fisher_p_non_ack_inside_vs_outside"] = fisher_p(i["k"], i["n"], o["k"], o["n"]) if i["n"] and o["n"] else None
        i2, o2 = rows["inside"]["heard_all"], rows["outside"]["heard_all"]
        rows["fisher_p_all_inside_vs_outside"] = fisher_p(i2["k"], i2["n"], o2["k"], o2["n"]) if i2["n"] and o2["n"] else None
        B[gname] = rows
    # tx rate
    B["tx_per_h_inside"] = round(sum(1 for x in xia["tx"] if inside(x[0], wins, starts)) / tot_in * 3600, 1)
    out_s = (xia["last"] - xia["first"]) - tot_in
    B["tx_per_h_outside"] = round(sum(1 for x in xia["tx"] if not inside(x[0], wins, starts)) / out_s * 3600, 1)
    res["b_tx_heard"] = B

    # (c) CAD
    C: dict = {"busy": "CAD_SCAN result=-702"}
    def cad_split(cad: list, lo: float, hi: float) -> dict:
        i = [(ts, bz) for ts, bz in cad if lo <= ts <= hi and inside(ts, wins, starts)]
        o = [(ts, bz) for ts, bz in cad if lo <= ts <= hi and not inside(ts, wins, starts)]
        r = {"inside": share(sum(bz for _, bz in i), len(i)), "outside": share(sum(bz for _, bz in o), len(o))}
        r["fisher_p"] = fisher_p(r["inside"]["k"], r["inside"]["n"], r["outside"]["k"], r["outside"]["n"]) if i and o else None
        o2 = [(ts, bz) for ts, bz in o if ts >= s_first]
        r["outside_from_first_silence"] = share(sum(bz for _, bz in o2), len(o2))
        return r
    C["oe3xia"] = cad_split(xia["cad"], xia["first"], xia["last"])
    for n in OTH:
        C[n] = cad_split(oth[n]["cad"], max(xia["first"], oth[n]["first"]), min(xia["last"], oth[n]["last"]))
    res["c_cad"] = C
    out.write_text(json.dumps(res, indent=1))
    print(f"silences: {res['silences']['n']} total {res['silences']['total_s']} s longest {res['silences']['longest_s']} s first {res['silences']['first_start']}")


if __name__ == "__main__":
    main()
