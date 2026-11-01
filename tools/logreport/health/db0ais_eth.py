#!/usr/bin/env python3
"""DB0AIS-12 ETH-04 focus: event timeline from node millis (db0ais.log has no wall-clock prefix).

Usage (via run.py): db0ais_eth.py <node>; reads <norm>/<node>.log, writes <out>/<node>_eth.json
Wall clock = --standalone-start (PC/PuTTY clock, CET) + node ms.  Single boot (RESETREAS=0x4) at ms=0.
"""
from __future__ import annotations
import collections
import datetime as dt
import json
import re
import sys

from nodeset import NORM, OUT, STANDALONE_START

BOOT0 = dt.datetime.fromisoformat(STANDALONE_START)


def wall(ms: int) -> str:
    return (BOOT0 + dt.timedelta(milliseconds=ms)).strftime("%m-%d %H:%M:%S")


def main() -> None:
    path, out = NORM / f"{sys.argv[1]}.log", OUT / f"{sys.argv[1]}_eth.json"
    cur = 0
    keeps: list[int] = []
    beats: list[int] = []
    cets: list[int] = []
    ext_tx: list[int] = []
    udp_nr: list[tuple[int, int]] = []
    flaps: list[tuple[int, str]] = []
    ntp_to: list[int] = []
    stalls: list[tuple[int, str, int]] = []
    eth_sum: list[dict[str, int]] = []
    gwu: list[int] = []
    gwi: list[int] = []
    boots: list[int] = []
    other_eth: list[tuple[int, str]] = []
    re_ms = re.compile(r";ms;(\d+)")
    re_t = re.compile(r"\bt=(\d+)")
    re_sum = re.compile(r"link_age_s;(\d+);ip;([\d.]+);dest;([\d.]+);hb_age_s;(\d+);got_ip_n;(\d+);downs;(\d+);renews;(\d+);renew_fail;(\d+);resets;(\d+);rx_n;(\d+);rx_max_ms;(\d+);tx_fail;(\d+);tx_max_ms;(\d+);ms;(\d+)")
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\r\n")
            if line.startswith("[") or " [LOG] " in line:
                m = re_ms.search(line) if line.startswith("[") else None
                if m:
                    v = int(m.group(1))
                    if v >= cur:
                        cur = v
                elif " [LOG] " in line:
                    m = re_t.search(line)
                    if m and int(m.group(1)) >= cur:
                        cur = int(m.group(1))
            if line.startswith("[GW];keep;tx;ok"):
                keeps.append(cur)
            elif line.startswith("[GW];rx;type;BEAT"):
                beats.append(cur)
            elif line.startswith("[GW];rx;type;CET"):
                cets.append(cur)
            elif line.startswith("[EXT];tx;len"):
                ext_tx.append(cur)
            elif line.startswith("[UDP] Server not responding"):
                udp_nr.append((cur, int(re.search(r"for (\d+)s", line).group(1))))
            elif line.startswith("[ETH];event;link;"):
                flaps.append((cur, "down" if ";down;" in line else "up"))
            elif line.startswith("[ETH];stall;"):
                p = line.split(";")
                stalls.append((cur, p[2], int(p[4])))
            elif line.startswith("[ETH];event;"):
                other_eth.append((cur, line[:90]))
            elif line.startswith("[ETH];link;up;link;"):
                m = re_sum.search(line)
                if m:
                    eth_sum.append({"ms": int(m.group(14)), "hb_age_s": int(m.group(4)), "downs": int(m.group(6)),
                                    "renews": int(m.group(7)), "rx_n": int(m.group(10)), "tx_fail": int(m.group(12)), "tx_max_ms": int(m.group(13))})
            elif line.startswith("[NTP];timeout"):
                ntp_to.append(cur)
            elif line.startswith("[BOOT];ready") or line.startswith("[INIT] START CLIENT"):
                boots.append(cur)
            elif " [LOG] GWU" in line:
                gwu.append(cur)
            elif " [LOG] GWI" in line:
                gwi.append(cur)

    # per keepalive: BEAT within 5 s?
    ev = sorted([(t, "k") for t in keeps] + [(t, "b") for t in beats])
    verdict: list[tuple[int, bool]] = []
    pend = None
    rep = False
    for t, k in ev:
        if k == "k":
            if pend is not None:
                verdict.append((pend, rep))
            pend, rep = t, False
        elif pend is not None and t - pend < 5000:
            rep = True
    if pend is not None:
        verdict.append((pend, rep))
    miss = [t for t, r in verdict if not r]

    # runs of consecutive misses
    runs: list[dict] = []
    prev = None
    for t, r in verdict:
        if not r:
            if prev is not None and not prev[1] and runs and t - runs[-1]["_last"] <= 45000:
                runs[-1]["_last"] = t
                runs[-1]["n"] += 1
            else:
                runs.append({"_first": t, "_last": t, "n": 1})
        prev = (t, r)
    run_out = []
    for r in runs:
        nxt = next((b for b in beats if b > r["_last"]), None)
        near_flap = [wall(ft) + " " + d for ft, d in flaps if r["_first"] - 120000 <= ft <= (nxt or r["_last"]) + 60000]
        run_out.append({"first_missed_keep": wall(r["_first"]), "keeps_without_beat": r["n"],
                        "next_beat_after": wall(nxt) if nxt else None,
                        "silence_s": round((nxt - r["_first"]) / 1000.0 + 0.0, 0) if nxt else None, "flap_near": near_flap})

    # window around each link-down
    windows = []
    for ft, d in flaps:
        if d != "down" or ft < 60000:
            continue
        w = [{"t": wall(t), "beat": r} for t, r in verdict if ft - 150000 <= t <= ft + 240000]
        last_b = max((b for b in beats if b <= ft), default=None)
        first_b = min((b for b in beats if b > ft), default=None)
        up = min((t for t, dd in flaps if dd == "up" and t > ft), default=None)
        sums = [s for s in eth_sum if ft - 130000 <= s["ms"] <= ft + 130000]
        windows.append({"link_down": wall(ft), "link_up": wall(up) if up else None, "down_ms": (up - ft) if up else None,
                        "last_beat_before": wall(last_b) if last_b else None, "age_s_at_down": round((ft - last_b) / 1000.0, 1) if last_b else None,
                        "first_beat_after_down": wall(first_b) if first_b else None,
                        "first_beat_after_up_s": round((first_b - up) / 1000.0, 1) if (first_b and up) else None,
                        "keepalive_verdicts": w, "eth_summaries_near": sums,
                        "udp_not_responding_near": [wall(t) for t, _ in udp_nr if ft - 150000 <= t <= ft + 240000],
                        "ext_tx_in_window": sum(1 for t in ext_tx if ft - 150000 <= t <= ft + 240000),
                        "gwu_in_window": sum(1 for t in gwu if ft - 150000 <= t <= ft + 240000)})

    # hourly miss histogram (CET logger clock)
    by_hour: collections.Counter[str] = collections.Counter(wall(t)[:8] for t in miss)
    keeps_hour: collections.Counter[str] = collections.Counter(wall(t)[:8] for t in keeps)
    hist = {h: {"keepalives": keeps_hour[h], "no_beat": by_hour.get(h, 0)} for h in sorted(keeps_hour)}
    # EXT gaps
    ext_gaps = sorted(((b - a) / 1000.0, wall(a)) for a, b in zip(ext_tx, ext_tx[1:]))[-5:]
    gwu_gaps = sorted(((b - a) / 1000.0, wall(a)) for a, b in zip(gwu, gwu[1:]))[-5:]
    # eth summary: hb_age and rx_n deltas
    stall_beat = [s for s in eth_sum if s["hb_age_s"] > 35]
    res = {
        "boots": [wall(t) for t in boots], "boot_count_ready_markers": len([b for b in boots]),
        "keepalives": len(keeps), "keepalive_gap_max_s": round(max((b - a) / 1000.0 for a, b in zip(keeps, keeps[1:])), 1),
        "beats": len(beats), "keepalives_without_beat": len(miss), "pct_without_beat": round(100.0 * len(miss) / len(keeps), 2),
        "miss_runs_n": len(run_out), "miss_runs_longest": sorted(run_out, key=lambda r: -(r["silence_s"] or 0))[:10],
        "miss_runs_all_n_by_len": dict(collections.Counter(r["keeps_without_beat"] for r in run_out)),
        "link_events": [(wall(t), d) for t, d in flaps],
        "link_windows": windows,
        "udp_not_responding_n": len(udp_nr),
        "hourly": hist,
        "ext_tx_n": len(ext_tx), "ext_tx_largest_gaps_s": ext_gaps,
        "gwu_n": len(gwu), "gwi_n": len(gwi), "gwu_largest_gaps_s": gwu_gaps,
        "ntp_timeouts": [wall(t) for t in ntp_to],
        "stalls": [(wall(t), n, ms) for t, n, ms in stalls],
        "other_eth_events": [(wall(t), l) for t, l in other_eth],
        "hb_age_gt35_summaries": len(stall_beat), "eth_summaries": len(eth_sum),
        "cet_rx": len(cets),
        "tx_fail_final": eth_sum[-1]["tx_fail"] if eth_sum else None,
        "tx_max_ms_final": eth_sum[-1]["tx_max_ms"] if eth_sum else None,
    }
    out.write_text(json.dumps(res, indent=1))


if __name__ == "__main__":
    main()
