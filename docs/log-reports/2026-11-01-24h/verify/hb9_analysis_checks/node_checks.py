"""One-pass health / TX-pool / decay measurements for one hb9 node.

usage: python3 -I node_checks.py <norm_log> <out_json> <own_call>

Everything here is derived from markers that exist WITHOUT --setlog (so it works for HB9VQQ-5 too):
RING_STATUS, TX_GATE_ENTER, CAD_*, CSMA_AGED, [MC-STAT]/[MC-PRIO]/[MC-HWM], CRC_ERROR,
RX_IRQ_STALE (anchored), RX_TIMEOUT_DEFERRED, CHANNEL_UTIL, [HEAP], [WIFI], [GW], [CET];tx,
MH-LoRa + the preceding "[LoRa]...Received packet" RSSI line.
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import air_s, dist, hour_key, iter_lines, kv, pct  # noqa: E402

RXP = re.compile(r"Received packet: RSSI:\s*(-?\d+) dBm / SNR:\s*(-?\d+) dB / Frequency error:\s*(-?[\d.]+)")
MH = re.compile(r"MH-LoRa\s+(\d{3}) (\S) x([0-9A-F]{8}) H(\d\d) S(\d) T(\d) M(\d\d) (.*?)>")


def slope(xs, ys):
    n = len(xs)
    if n < 2:
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else None


def main(path: str, out: str, own: str) -> None:
    R = defaultdict(dict)
    ring_q = []          # (t, queued, done, pending, retrying)
    gate_q = []
    gate_att = Counter()
    cad_free_att = Counter()
    cad = Counter()
    csma_ev = []         # (t, slot, age, stage, hold)
    stat_blocks = []     # dict per MC-STAT
    cur = None
    hwm = []
    crc = []             # (t, rssi, snr, ferr, size)
    stale = []           # (t, rssi, flags)
    stale_forced = []
    stale_early = []
    defer = Counter()
    onrx_ms = []
    good_n = 0
    util = []            # (t, rx_ms, tx_ms, util)
    heap = []
    wifi = []
    keep_t = []
    beat_t = []
    gw_types = Counter()
    cet_tx = []          # (t, age_s, wait_ms, prio)
    cet_rx = []          # (t, src, age_s, hop, path)
    rxp_last = None      # (t, rssi, snr, ferr)
    heard = []           # (t, rssi, snr, ferr, lasthop, ntok, size, typ, mid, dupflag)
    dedup_new = Counter()
    dedup_state = {}
    txlora = []
    rx_dedup_last = None
    tx_read = []         # (t, slot, prio, type, mid, lat_ms, queued)
    reboot_hits = []
    for n, t, b in iter_lines(path):
        if b.startswith("[MC-DBG] "):
            c = b[9:]
            if c.startswith("RING_STATUS "):
                d = kv(c)
                ring_q.append((t, int(d["queued"]), int(d["done"]), int(d["pending"]), int(d["retrying"])))
            elif c.startswith("TX_GATE_ENTER "):
                d = kv(c)
                gate_q.append(int(d["qlen"]))
                gate_att[int(d["cad_attempt"])] += 1
            elif c.startswith("CAD_FREE"):
                d = kv(c)
                cad["free"] += 1
                if "attempt" in d:
                    cad_free_att[int(d["attempt"])] += 1
            elif c.startswith("CAD_BUSY_1"):
                cad["busy1"] += 1
            elif c.startswith("CAD_BUSY"):
                cad["busy_confirmed"] += 1
            elif c.startswith("CAD_FALSE_POSITIVE"):
                cad["false_positive"] += 1
            elif c.startswith("CAD_SAFETY_TIMEOUT"):
                cad["safety_timeout"] += 1
            elif c.startswith("CSMA_AGED "):
                d = kv(c)
                csma_ev.append((t, int(d["slot"]), int(d["age"]), int(d["stage"]), int(d["hold"])))
            elif c.startswith("CRC_ERROR "):
                d = kv(c)
                crc.append((t, float(d["rssi"]), float(d["snr"]), float(d["freq_err"]), int(d["size"])))
            elif c.startswith("RX_IRQ_STALE_EARLY "):
                stale_early.append(t)
            elif c.startswith("RX_IRQ_STALE forced restart"):
                stale_forced.append(t)
            elif c.startswith("RX_IRQ_STALE "):
                d = kv(c)
                stale.append((t, float(d["rssi"]), d.get("flags")))
            elif c.startswith("RX_TIMEOUT_DEFERRED "):
                d = kv(c)
                defer[d["src"]] += 1
            elif c.startswith("ONRXDONE_TIME "):
                good_n += 1
                onrx_ms.append(int(kv(c)["ms"]))
            elif c.startswith("CHANNEL_UTIL "):
                m = re.search(r"rx=(\d+)ms tx=(\d+)ms util=(\d+)%", c)
                if m:
                    util.append((t, int(m.group(1)), int(m.group(2)), int(m.group(3))))
            elif c.startswith("RING_TX_READ "):
                d = kv(c)
                tx_read.append((t, int(d["slot"]), int(d["prio"]), d["type"], d["msg_id"], int(d["lat"].rstrip("ms")), d["queued"]))
            elif c.startswith("RX_DEDUP_ADD "):
                m = re.search(r"slot=(\d+)/(\d+)", c)
                if m:
                    dedup_state[t] = int(m.group(1))
            elif c.startswith("RX_DEDUP_NEW") or c.startswith("RX_DEDUP_DUP"):
                rx_dedup_last = ("N" if c.startswith("RX_DEDUP_NEW") else "D", t)
        elif b.startswith("[MC-STAT]"):
            cur = {"t": t, "qmax": kv(b)["qmax"]}
            stat_blocks.append(cur)
        elif cur is not None and b.lstrip().startswith("tx: "):
            cur["tx"] = {k: int(v) for k, v in kv(b).items()}
        elif cur is not None and b.lstrip().startswith("drop: "):
            cur["drop"] = {k: int(v) for k, v in kv(b).items()}
        elif b.startswith("[MC-PRIO] "):
            m = re.match(r"\[MC-PRIO\] p(\d)_lat_avg=(\d+)ms p\d_lat_max=(\d+)ms p\d_cnt=(\d+)", b)
            if m and cur is not None:
                cur.setdefault("prio", {})[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
        elif b.startswith("[MC-HWM] "):
            d = kv(b)
            hwm.append({"t": t, **{k: (v if k == "queue_hwm" else int(v.rstrip("ms"))) for k, v in d.items()}})
        elif b.startswith("[HEAP] "):
            p = b.split()
            if len(p) >= 6:
                heap.append((t, int(p[2]), int(p[3]), int(p[4])))
        elif b.startswith("[WIFI];"):
            p = b.split(";")
            if p[1] == "link":
                wifi.append((t, p[2], int(p[4]) if p[3] == "rssi" and p[4].lstrip("-").isdigit() else None, p[6] if len(p) > 6 else None, p[8] if len(p) > 8 else None))
            else:
                wifi.append((t, ";".join(p[1:4]), None, None, None))
        elif b.startswith("[GW];"):
            p = b.split(";")
            if p[1] == "keep" and p[2] == "tx":
                keep_t.append(t)
            elif p[1] == "rx":
                gw_types[p[3]] += 1
                if p[3] == "BEAT":
                    beat_t.append(t)
            else:
                gw_types["other:" + ";".join(p[1:3])] += 1
        elif b.startswith("[CET];tx;"):
            p = b.split(";")
            d = {p[i]: p[i + 1] for i in range(2, len(p) - 1, 2)}
            cet_tx.append((t, int(d["age_s"]), int(d["wait_ms"]), int(d["prio"])))
        elif b.startswith("[CET];rx;") or b.startswith("[CET];gw_rx;"):
            p = b.split(";")
            d = {p[i]: p[i + 1] for i in range(2, len(p) - 1, 2)}
            cet_rx.append((t, p[1], d.get("src"), int(d.get("age_s", -1))))
        else:
            m = RXP.search(b)
            if m:
                rxp_last = (t, float(m.group(1)), float(m.group(2)), float(m.group(3)))
                continue
            m = MH.search(b)
            if m and " MH-LoRa " in " " + b:
                toks = m.group(8).split(",")
                size = int(m.group(1))
                rec = (t, rxp_last[1] if rxp_last and t - rxp_last[0] < 1.0 else None,
                       rxp_last[2] if rxp_last and t - rxp_last[0] < 1.0 else None,
                       rxp_last[3] if rxp_last and t - rxp_last[0] < 1.0 else None,
                       toks[-1], len(toks), size, m.group(2), m.group(3),
                       rx_dedup_last[0] if rx_dedup_last and t - rx_dedup_last[1] < 1.0 else None)
                heard.append(rec)
            elif b.startswith(("Guru Meditation", "rst:0x", "Backtrace:", "abort()", "ESP-ROM:", "***ERROR***", "Brownout")):
                reboot_hits.append((n, b[:80]))
            elif " TX-LoRa " in b and "x" in b:
                m2 = re.search(r"TX-LoRa\s+(\d{3}) (\S) x([0-9A-F]{8})", b)
                if m2:
                    txlora.append((t, int(m2.group(1)), m2.group(2), m2.group(3)))

    span = (iter_lines(path).__next__()[1])
    t0 = ring_q[0][0]
    t1 = ring_q[-1][0]
    hours = (t1 - t0) / 3600.0
    # ---- occupancy
    qv = [r[1] for r in ring_q]
    hr = defaultdict(list)
    for r in ring_q:
        hr[hour_key(r[0])].append(r[1])
    hmeans = [sum(v) / len(v) for k, v in sorted(hr.items()) if len(v) >= 100]
    R["occupancy"] = {"samples": len(qv), "dist": dist(qv), "share_ge3_pct": round(100 * sum(1 for q in qv if q >= 3) / len(qv), 2),
                      "share_ge5_pct": round(100 * sum(1 for q in qv if q >= 5) / len(qv), 2),
                      "share_ge10_pct": round(100 * sum(1 for q in qv if q >= 10) / len(qv), 2),
                      "hourly_mean_slope_per_h": round(slope(list(range(len(hmeans))), hmeans), 5),
                      "hourly_means_first_last": [round(hmeans[0], 2), round(hmeans[-1], 2)] if hmeans else None,
                      "retrying_gt0_samples": sum(1 for r in ring_q if r[4] > 0), "pending_gt0_samples": sum(1 for r in ring_q if r[3] > 0),
                      "done_max": max(r[2] for r in ring_q)}
    R["tx_gate"] = {"enter": len(gate_q), "qlen": dist(gate_q), "attempt_dist": dict(sorted(gate_att.items())),
                    "attempt0_pct": round(100 * gate_att[0] / max(1, sum(gate_att.values())), 1),
                    "attempt_ge3_pct": round(100 * sum(v for k, v in gate_att.items() if k >= 3) / max(1, sum(gate_att.values())), 1),
                    "attempt_max": max(gate_att) if gate_att else None, "cad": dict(cad),
                    "cad_busy1_pct_of_scans": None}
    # ---- MC-STAT
    tot_tx = Counter()
    tot_drop = Counter()
    pre = 0
    lat_sum = Counter()
    lat_cnt = Counter()
    lat_max = Counter()
    qmaxs = []
    for s in stat_blocks:
        for k, v in s.get("tx", {}).items():
            if k == "preempt":
                pre += v
            else:
                tot_tx[k] += v
        for k, v in s.get("drop", {}).items():
            tot_drop[k] += v
        for p, (avg, mx, cnt) in s.get("prio", {}).items():
            lat_sum[p] += avg * cnt
            lat_cnt[p] += cnt
            lat_max[p] = max(lat_max[p], mx)
        qmaxs.append(int(s["qmax"].split("/")[0]))
    R["mc_stat"] = {"blocks": len(stat_blocks), "tx_sum_by_prio": dict(tot_tx), "tx_sum": sum(tot_tx.values()),
                    "drop_sum_by_prio": dict(tot_drop), "preempt_sum": pre,
                    "lat_avg_ms_by_prio": {p: round(lat_sum[p] / lat_cnt[p]) for p in lat_cnt},
                    "lat_max_ms_by_prio": dict(lat_max), "qmax_first": qmaxs[0] if qmaxs else None, "qmax_last": qmaxs[-1] if qmaxs else None,
                    "qmax_increases": sum(1 for a, b in zip(qmaxs, qmaxs[1:]) if b > a),
                    "windows_with_tx_sum_gt0": sum(1 for s in stat_blocks if sum(v for k, v in s.get("tx", {}).items() if k != "preempt") > 0)}
    R["mc_stat"]["tx_vs_ring_tx_read"] = {"ring_tx_read": len(tx_read),
                                            "ring_tx_read_by_prio": dict(Counter(str(r[2]) for r in tx_read))}
    if hwm:
        R["mc_hwm"] = {"n": len(hwm), "first": hwm[0], "last": hwm[-1],
                       "queue_hwm_set": sorted({h["queue_hwm"] for h in hwm}),
                       "csma_hwm_last": hwm[-1].get("csma_hwm"), "wait_hwm_ms_last": hwm[-1].get("wait_hwm"),
                       "aged1_last": hwm[-1].get("aged1"), "aged2_last": hwm[-1].get("aged2"),
                       "trickle_ms_dist": dict(Counter(h.get("trickle") for h in hwm)),
                       "uptime_monotonic": all(b["uptime"] > a["uptime"] for a, b in zip(hwm, hwm[1:])),
                       "wait_hwm_series_ms": [h.get("wait_hwm") for h in hwm if True][::6]}
    # ---- RX health
    cr = defaultdict(list)
    for (t, rs, sn, fe, sz) in crc:
        cr[hour_key(t)].append(rs)
    fe_abs = [abs(c[3]) for c in crc]
    R["crc"] = {"n": len(crc), "per_hour_mean": round(len(crc) / hours, 1),
                "per_hour_max": max(len(v) for v in cr.values()), "per_hour_min": min(len(v) for v in cr.values()),
                "rssi": dist([c[1] for c in crc]), "snr": dist([c[2] for c in crc]), "size": dist([c[4] for c in crc]),
                "ferr_abs": dist(fe_abs), "ferr_gt3000_pct": round(100 * sum(1 for f in fe_abs if f > 3000) / max(1, len(crc)), 1),
                "ferr_lt1000_pct": round(100 * sum(1 for f in fe_abs if f < 1000) / max(1, len(crc)), 1),
                "rssi_gt_m85_n": sum(1 for c in crc if c[1] > -85), "rssi_gt_m60_n": sum(1 for c in crc if c[1] > -60),
                "per_hour": {k: len(v) for k, v in sorted(cr.items())}}
    R["rx_good"] = {"onrxdone_time_n": good_n, "onrxdone_ms": dist(onrx_ms), "mh_lora_lines": len(heard),
                    "err_share_pct_of_good_plus_err": round(100 * len(crc) / (len(crc) + good_n), 1)}
    sh = defaultdict(int)
    for s in stale:
        sh[hour_key(s[0])] += 1
    R["rx_irq_stale"] = {"rssi_lines": len(stale), "early": len(stale_early), "forced_restart": len(stale_forced),
                         "flags": dict(Counter(s[2] for s in stale)), "rssi": dist([s[1] for s in stale]),
                         "per_hour_mean": round(len(stale) / hours, 1), "per_hour_max": max(sh.values()) if sh else 0,
                         "per_hour": dict(sorted(sh.items())),
                         "stale_gt_m80_n": sum(1 for s in stale if s[1] > -80)}
    R["rx_timeout_deferred"] = dict(defer)
    # channel util
    rx_s = sum(u[1] for u in util) / 1000.0
    tx_s = sum(u[2] for u in util) / 1000.0
    uh = defaultdict(list)
    for u in util:
        uh[hour_key(u[0])].append(u[3])
    R["channel_util"] = {"n": len(util), "mean": round(sum(u[3] for u in util) / len(util), 2), "p95": pct([u[3] for u in util], 95),
                         "max": max(u[3] for u in util), "ge70_n": sum(1 for u in util if u[3] >= 70),
                         "rx_s_total": round(rx_s), "tx_s_total": round(tx_s),
                         "rx_multiple_of_2476ms_n": sum(1 for u in util if u[1] > 0 and u[1] % 2476 == 0), "rx_nonzero_n": sum(1 for u in util if u[1] > 0),
                         "hourly_mean": {k: round(sum(v) / len(v), 1) for k, v in sorted(uh.items())}}
    # decoded events: last hop / rssi
    d_rssi = [h[1] for h in heard if h[1] is not None]
    lh = defaultdict(list)
    for h in heard:
        if h[5] >= 2 and h[1] is not None:
            lh[h[4]].append(h)
    top = sorted(lh.items(), key=lambda kv_: -len(kv_[1]))[:14]
    R["heard"] = {"mh_lora_events": len(heard), "rssi": dist(d_rssi), "snr": dist([h[2] for h in heard if h[2] is not None]),
                  "dup_flag": dict(Counter(h[9] for h in heard)), "types": dict(Counter(h[7] for h in heard)),
                  "path_tokens": dict(sorted(Counter(h[5] for h in heard).items())),
                  "direct_zero_relay_pct": round(100 * sum(1 for h in heard if h[5] == 1) / max(1, len(heard)), 1),
                  "unique_msgids": len({h[8] for h in heard}), "airtime_s_decoded": round(sum(air_s(h[6]) for h in heard)),
                  "top_last_hops": [{"call": c, "n": len(v), "rssi_p50": pct([x[1] for x in v], 50), "snr_p50": pct([x[2] for x in v], 50)} for c, v in top],
                  "unique_last_hops": len(lh)}
    # ferr of decoded
    R["heard"]["ferr_decoded_abs_p50"] = pct([abs(h[3]) for h in heard if h[3] is not None], 50)
    R["heard"]["ferr_decoded_p50_signed"] = pct([h[3] for h in heard if h[3] is not None], 50)
    # tx
    R["tx"] = {"tx_lora_lines": len(txlora), "ring_tx_read": len(tx_read), "airtime_s_from_tx_lora_len": round(sum(air_s(x[1]) for x in txlora)),
               "tx_per_hour": round(len(tx_read) / hours, 1), "tx_duty_pct_from_util": round(100 * tx_s / (hours * 3600), 2),
               "type_split": dict(Counter(x[2] for x in txlora)),
               "airtime_s_by_type": {k: round(sum(air_s(x[1]) for x in txlora if x[2] == k)) for k in sorted({x[2] for x in txlora})}}
    # heap
    if heap:
        hv = [h[1] for h in heap]
        hs = slope([(h[0] - heap[0][0]) / 3600 for h in heap], hv)
        R["heap"] = {"n": len(heap), "free_min": min(hv), "free_max": max(hv), "first": hv[0], "last": hv[-1], "slope_bytes_per_h": hs,
                     "min_free_ever": sorted({h[2] for h in heap}), "max_alloc": sorted({h[3] for h in heap})}
    # wifi
    wr = [w[2] for w in wifi if w[2] is not None]
    R["wifi"] = {"lines": len(wifi), "link_up": sum(1 for w in wifi if w[1] == "up"), "non_up": [w for w in wifi if w[1] != "up"][:5],
                 "rssi": dist(wr), "bssids": dict(Counter(w[3] for w in wifi)), "chan": dict(Counter(w[4] for w in wifi)),
                 "rssi_lt_m75_n": sum(1 for r in wr if r < -75)}
    # gateway keep / beat
    miss = []
    j = 0
    for kt in keep_t:
        while j < len(beat_t) and beat_t[j] < kt - 0.01:
            j += 1
        if not (j < len(beat_t) and beat_t[j] - kt < 2.0):
            miss.append(kt)
    mh = Counter(hour_key(m) for m in miss)
    delays = []
    jj = 0
    for kt in keep_t:
        while jj < len(beat_t) and beat_t[jj] < kt - 0.01:
            jj += 1
        if jj < len(beat_t):
            delays.append(beat_t[jj] - kt)
    R["gateway"] = {"keepalives": len(keep_t), "beats": len(beat_t), "keep_without_beat_within_2s": len(miss),
                    "miss_pct": round(100 * len(miss) / max(1, len(keep_t)), 2), "miss_per_hour": dict(sorted(mh.items())),
                    "rx_types": dict(gw_types), "beat_delay_s": dist(delays),
                    "keep_gap_max_s": round(max(b - a for a, b in zip(keep_t, keep_t[1:])), 1) if len(keep_t) > 1 else None,
                    "keep_gaps_gt45s": sum(1 for a, b in zip(keep_t, keep_t[1:]) if b - a > 45)}
    # CET
    ca = [c[1] for c in cet_tx]
    R["cet_tx"] = {"n": len(cet_tx), "age_s": dist(ca), "wait_s": dist([c[2] / 1000 for c in cet_tx]), "prio": dict(Counter(c[3] for c in cet_tx)),
                   "age_plus_wait_max_s": max((c[1] for c in cet_tx), default=None), "gt120_n": sum(1 for a in ca if a > 120), "gt150_n": sum(1 for a in ca if a > 150),
                   "rx_by_src": dict(Counter((c[1], c[2]) .__repr__() for c in cet_rx)), "rx_age_s_nonzero": sum(1 for c in cet_rx if c[3] > 0)}
    # CSMA aged
    ev = csma_ev
    eps = []
    for e in ev:
        if eps and e[1] == eps[-1][-1][1] and e[0] - eps[-1][-1][0] < 60:
            eps[-1].append(e)
        else:
            eps.append([e])
    R["csma_aged"] = {"events": len(ev), "episodes": len(eps), "stage": dict(Counter(e[3] for e in ev)),
                      "age_s": dist([e[2] / 1000 for e in ev]), "hold_ms": dist([e[4] for e in ev]),
                      "events_per_episode": dist([len(e) for e in eps]),
                      "per_hour": dict(sorted(Counter(hour_key(e[0]) for e in ev).items()))}
    # dedup window
    # tx reads: double
    byid = defaultdict(list)
    for r in tx_read:
        byid[r[4]].append(r)
    dd = {"ids_gt1": 0, "gap_le_30min": 0, "gap_gt_30min": 0, "gaps_le30_s": []}
    for mid, rs in byid.items():
        if len(rs) > 1:
            dd["ids_gt1"] += 1
            g = rs[1][0] - rs[0][0]
            if g <= 1800:
                dd["gap_le_30min"] += 1
                dd["gaps_le30_s"].append(round(g, 1))
            else:
                dd["gap_gt_30min"] += 1
    dd["types_le30"] = dict(Counter(rs[0][3] for mid, rs in byid.items() if len(rs) > 1 and rs[1][0] - rs[0][0] <= 1800))
    R["double_tx"] = dd
    R["reboot_hits"] = reboot_hits[:5]
    R["span"] = {"first_ring_status": t0, "last_ring_status": t1, "hours": round(hours, 3)}
    json.dump(R, open(out, "w"), indent=1, default=str)
    print("ok", out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
