#!/usr/bin/env python3
"""Raw-log measurements for the HB9VQQ-5 report (24-h log analysis, 2026-11-01).

Usage:
    python3 -I hb9vqq5_measure.py --norm <norm-dir> --out <data/extra/hb9vqq5_measure.json>

--norm  directory with the normalised logs hb9vqq4.log and hb9vqq5.log
--out   JSON file read by generators/hb9vqq5.py (the generator itself never reads a raw log)

HB9VQQ-5 was captured with --loradebug only, so there are no [LOG] lines. Everything here is rebuilt
from the [MC-DBG] / RING_* / [CET] markers and from the RX lines (Received packet, MH-LoRa, RX-LoRa2,
TX-LoRa). HB9VQQ-4 is read only for the numbers that compare the two gateways. Stdlib only.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import statistics as st
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path
from typing import Any

OWN = {"hb9vqq4": "HB9VQQ-4", "hb9vqq5": "HB9VQQ-5"}
TSF = "%Y-%m-%d %H:%M:%S.%f"

RE_MH = re.compile(r"^(\S+ \S+)\s+\S+ (MH-LoRa|TX-LoRa|RX-LoRa2)\s+(\d+) . x([0-9A-F]{8}) H(\d\d) \S+ \S+ \S+ ([^>]+)>(\S*?)([:!@])(.*)$")
RE_PKT = re.compile(r"Received packet: RSSI:\s+(-?\d+) dBm / SNR:\s+(-?\d+) dB / Frequency error:\s+(-?[\d.]+)")
RE_CRC = re.compile(r"CRC_ERROR rssi=(-?\d+) snr=(-?\d+) freq_err=(-?[\d.]+)")
RE_WRITE = re.compile(r"RING_WRITE slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=([0-9A-F]{8}) .*src=(\S+)")
RE_READ = re.compile(r"RING_TX_READ slot=(\d+) prio=(\d) type=(\w+) status=\w+ len=(\d+) msg_id=([0-9A-F]{8}) .*lat=(\d+)ms")
RE_SUP = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]{8})")
RE_STATUS = re.compile(r"RING_STATUS queued=(\d+)")
RE_UTIL = re.compile(r"CHANNEL_UTIL rx=(\d+)ms tx=(\d+)ms util=(\d+)%")
RE_CETTX = re.compile(r"\[CET\];tx;age_s;(-?\d+);trusted;(\d);wait_ms;(\d+);prio;(\d)")
RE_CETGW = re.compile(r"\[CET\];gw_rx;src;(\S+?);age_s;(-?\d+)")
RE_CETRX = re.compile(r"\[CET\];rx;src;(\S+?);age_s;(-?\d+)")
RE_CSMA = re.compile(r"CSMA_AGED slot=(\d+) age=(\d+) stage=(\d) hold=(\d+)")
RE_HWM = re.compile(r"\[MC-HWM\] uptime=(\d+)s queue_hwm=(\d+)/20 csma_hwm=\d+ trickle=\d+ms aged1=(\d+) aged2=(\d+) wait_hwm=(\d+)")
RE_HEAP = re.compile(r"\[HEAP\] \S+ (\d+) (\d+) (\d+) \(mon\)")
RE_CET_PAY = re.compile(r"\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")

# TTL per priority class (src/txring_ttl.h): HEY 180 s, position 120 s, text 300 s, ACK 90 s
TTL = {"5": 180.0, "4": 120.0, "3": 300.0, "1": 90.0}


def t_of(s: str) -> float:
    return datetime.strptime(s[:23], TSF).timestamp()


def q(v: list[float], p: float) -> float:
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))]


def dist(v: list[float], nd: int = 1) -> dict[str, Any]:
    if not v:
        return {"n": 0}
    return {"n": len(v), "p50": round(st.median(v), nd), "p90": round(q(v, 0.9), nd), "p99": round(q(v, 0.99), nd),
            "max": round(max(v), nd), "min": round(min(v), nd)}


def toa(n: int) -> float:
    """Time on air in s, SF11 / BW250 / CR4:6, preamble 8, explicit header, CRC on."""
    sf, bw, cr = 11, 250000.0, 2
    ts = (2 ** sf) / bw
    pl = 8 + max(math.ceil((8 * n - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
    return (8 + 4.25) * ts + pl * ts


def cet_age(logger_ts: str, payload: str) -> float:
    """{CET} payload is UTC, the logger runs on CET (UTC+1)."""
    a = datetime.strptime(logger_ts[:19], "%Y-%m-%d %H:%M:%S")
    b = datetime.strptime(payload, "%Y-%m-%d %H:%M:%S")
    return (a - b).total_seconds() - 3600.0


def scan(p: Path, own: str) -> dict[str, Any]:
    """One pass over a normalised log; returns the raw event lists the measures are built from."""
    s: dict[str, Any] = defaultdict(list)
    last_pkt: tuple[int, int, float] | None = None
    occ: dict[str, dict[str, Any]] = {}
    done: list[dict[str, Any]] = []
    lines = p.open(errors="replace").read().splitlines()
    s["lines"] = len(lines)
    s["first"] = lines[0][:19]
    s["last"] = lines[-1][:19]
    s["t_first"] = t_of(lines[0])
    s["t_last"] = t_of(lines[-1])
    nl = len(lines)
    for i, line in enumerate(lines):
        if "OE1XAR-62" in line:
            s["oe1xar62_lines"] = s.get("oe1xar62_lines", 0) + 1
        if " TX-UDP " in line:
            s["tx_udp"] = s.get("tx_udp", 0) + 1
            continue
        if "MH-LoRa" in line or "TX-LoRa" in line or "RX-LoRa2" in line:
            m = RE_MH.match(line)
            if not m:
                continue
            ts, kind, ln, mid, hh, path_s, dest, sep, body = m.groups()
            t = t_of(ts)
            toks = path_s.split(",")
            if kind == "MH-LoRa":
                pk = last_pkt
                last_pkt = None
                s["mh"].append((t, mid, toks, sep, pk, int(ln)))
            elif kind == "TX-LoRa":
                s["txl"].append((t, mid, toks, sep, body, int(ln)))
            else:
                s["acc"].append((t, mid))
            continue
        if "Received packet" in line:
            m = RE_PKT.search(line)
            if m:
                last_pkt = (int(m.group(1)), int(m.group(2)), float(m.group(3)))
            continue
        if "CRC_ERROR" in line:
            m = RE_CRC.search(line)
            if m:
                s["crc"].append((int(m.group(1)), int(m.group(2)), float(m.group(3))))
            continue
        if "ONRXDONE_TIME" in line:
            s["good"].append(t_of(line))
            continue
        if line.endswith(" OnRxDone"):
            s["n_onrxdone"] = s.get("n_onrxdone", 0) + 1
            continue
        if "RX_IRQ_STALE rssi=" in line:
            s["irq_stale"] = s.get("irq_stale", 0) + 1
            continue
        if "RING_" in line:
            m = RE_WRITE.search(line)
            if m:
                slot, typ, stt, ln, mid, src = m.groups()
                t = t_of(line)
                old = occ.get(slot)
                if old is not None:
                    sup = False
                    for x in lines[i + 1:i + 4]:
                        ms = RE_SUP.search(x)
                        if ms and ms.groups() == (slot, old["id"]):
                            sup = True
                    old["fate"] = "superseded" if sup else "vanished"
                    old["end"] = t
                    done.append(old)
                occ[slot] = {"id": mid, "t": t, "src": src, "type": typ, "len": int(ln), "slot": slot}
                s["writes"].append((t, mid, src, typ, int(ln)))
                continue
            m = RE_READ.search(line)
            if m:
                slot, prio, typ, ln, mid, lat = m.groups()
                t = t_of(line)
                s["reads"].append((t, slot, int(prio), mid, int(lat) / 1000.0, int(ln)))
                o = occ.get(slot)
                if o is not None and o["id"] == mid:
                    o["fate"] = "aired"
                    o["end"] = t
                    o["prio"] = int(prio)
                    o["lat"] = int(lat) / 1000.0
                    done.append(o)
                    del occ[slot]
                continue
            m = RE_STATUS.search(line)
            if m:
                s["status"].append(int(m.group(1)))
                continue
            if "RING_DROP" in line or "RING_OVERFLOW" in line:
                s["drops"].append(line[:120])
                continue
            if "RING_SUPERSEDE_KEEP" in line:
                s["sup_keep"] = s.get("sup_keep", 0) + 1
                continue
            if "RING_SUPERSEDE" in line:
                s["sup"] = s.get("sup", 0) + 1
                continue
            continue
        if "CHANNEL_UTIL" in line:
            m = RE_UTIL.search(line)
            if m:
                s["util"].append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
            continue
        if "[CET];" in line:
            ts = line[:23]
            m = RE_CETTX.search(line)
            if m:
                s["cet_tx"].append((t_of(ts), int(m.group(1)), int(m.group(3)) / 1000.0, int(m.group(2))))
                continue
            m = RE_CETGW.search(line)
            if m:
                s["cet_gw"].append((t_of(ts), m.group(1), int(m.group(2))))
                continue
            m = RE_CETRX.search(line)
            if m:
                s["cet_rx"].append((t_of(ts), m.group(1), int(m.group(2))))
                continue
            if "[CET];drop" in line:
                s["cet_drop"] = s.get("cet_drop", 0) + 1
            continue
        if "CSMA_AGED" in line:
            m = RE_CSMA.search(line)
            if m:
                s["csma"].append((t_of(line), m.group(1), int(m.group(2)) / 1000.0, int(m.group(3)), int(m.group(4))))
            continue
        if "[MC-HWM]" in line:
            m = RE_HWM.search(line)
            if m:
                s["hwm"].append(tuple(int(x) for x in m.groups()))
            continue
        if "[HEAP]" in line and "(mon)" in line:
            m = RE_HEAP.search(line)
            if m:
                s["heap"].append(tuple(int(x) for x in m.groups()))
            continue
        if "[MC-TRICKLE]" in line:
            for k in ("SEND", "SUPPRESS", "TOPO_CHANGE"):
                if f"] {k} " in line:
                    s["trickle_" + k] = s.get("trickle_" + k, 0) + 1
            mt = re.search(r"SEND consistent=(\d+)<k=(\d+)", line)
            if mt and int(mt.group(1)) >= int(mt.group(2)):
                s["trickle_send_forced"] = s.get("trickle_send_forced", 0) + 1
            continue
        if "[LOGGER] connection lost" in line:
            s["lost"].append((t_of(line), line[:23]))
            continue
        if "[LOGGER] connected" in line:
            s["connected"].append(t_of(line))
            continue
        if "[CON ]" in line and "s_password" in line:
            s["con_login"].append(t_of(line))
            continue
        if "RX_DEDUP_DUP" in line:
            s["dup_n"] = s.get("dup_n", 0) + 1
            continue
        if "[KEEP]" in line:
            s["keep_n"] = s.get("keep_n", 0) + 1
            continue
        if "[BEAT]" in line:
            s["beat_n"] = s.get("beat_n", 0) + 1
            continue
        if "Guru Meditation" in line or "Backtrace" in line or "Brownout" in line or "rst:0x" in line:
            s["crash_hits"] = s.get("crash_hits", 0) + 1
    for o in occ.values():
        o["fate"] = "vanished" if s["t_last"] - o["t"] > 400 else "pending"
        o["end"] = s["t_last"]
        done.append(o)
    s["done"] = done
    s["nl"] = nl
    return s


def fate_block(sc: dict[str, Any], own: str) -> dict[str, Any]:
    """Slot fate per ring write, with the foreign-copy test of the overhear-cancel."""
    rx: dict[str, list[tuple[float, list[str]]]] = defaultdict(list)
    for t, mid, toks, _sep, _pk, _ln in sc["mh"]:
        rx[mid].append((t, toks))

    def foreign(o: dict[str, Any]) -> float | None:
        for t, toks in rx.get(o["id"], []):
            if o["t"] < t <= o["end"] and len(toks) >= 2 and toks[-1] != own:
                return t - o["t"]
        return None

    done = sc["done"]
    c = Counter(o["fate"] for o in done)
    van = [o for o in done if o["fate"] == "vanished"]
    relay = [o for o in done if o["src"] == "rx_relay"]
    aired_relay = [o for o in relay if o["fate"] == "aired"]
    vf = [foreign(o) for o in van]
    af = [foreign(o) for o in aired_relay]
    by_src: dict[str, dict[str, int]] = defaultdict(lambda: Counter())  # type: ignore[assignment]
    for o in done:
        by_src[o["src"]][o["fate"]] += 1
    van_prio_type = Counter(o["type"] for o in van)
    return {
        "writes": len(done),
        "fates": dict(c),
        "by_src": {k: dict(v) for k, v in by_src.items()},
        "vanished_by_src": dict(Counter(o["src"] for o in van)),
        "vanished_by_type": dict(van_prio_type),
        "vanished_with_foreign_copy": sum(1 for x in vf if x is not None),
        "vanished_air_s": round(sum(toa(o["len"]) for o in van), 1),
        "aired_relays": len(aired_relay),
        "aired_relays_with_foreign_copy": sum(1 for x in af if x is not None),
        "aired_relays_with_foreign_copy_air_s": round(sum(toa(o["len"]) for o, x in zip(aired_relay, af) if x is not None), 1),
        "first_foreign_after_write_s": dist([x for x in vf if x is not None]),
        "aired_total_air_s": round(sum(toa(o["len"]) for o in done if o["fate"] == "aired"), 1),
        "udp_aired": sum(1 for o in done if o["src"] == "udp_rx" and o["fate"] == "aired"),
        "udp_aired_air_s": round(sum(toa(o["len"]) for o in done if o["src"] == "udp_rx" and o["fate"] == "aired"), 1),
        "udp_aired_after_foreign": sum(1 for o in done if o["src"] == "udp_rx" and o["fate"] == "aired" and foreign(o) is not None),
    }


def latency_block(sc: dict[str, Any]) -> dict[str, Any]:
    by_prio: dict[str, list[float]] = defaultdict(list)
    classes: dict[str, list[float]] = defaultdict(list)
    for _t, _slot, prio, _mid, lat, _ln in sc["reads"]:
        by_prio[str(prio)].append(lat)
    # join reads to the write source: done list carries prio and lat for aired entries
    for o in sc["done"]:
        if o["fate"] != "aired":
            continue
        pr = str(o["prio"])
        src = o["src"]
        key = {("5", "auto_pos"): "hey_own", ("5", "rx_relay"): "hey_relay", ("4", "user_wx"): "pos_own", ("4", "rx_relay"): "pos_relay",
               ("3", "rx_relay"): "text_relay", ("3", "udp_rx"): "text_server"}.get((pr, src))
        if pr == "1":
            key = "ack"
        if key:
            classes[key].append(o["lat"])
    ttl_of = {"hey_own": 180.0, "hey_relay": 180.0, "pos_own": 120.0, "pos_relay": 120.0, "text_relay": 300.0, "text_server": 300.0, "ack": 90.0}
    cls: dict[str, Any] = {}
    for k, v in classes.items():
        d = dist(v)
        d["ttl_s"] = ttl_of[k]
        d["max_over_ttl_pct"] = round(100 * max(v) / ttl_of[k], 1)
        d["ge50"] = sum(1 for x in v if x >= 0.5 * ttl_of[k])
        d["ge80"] = sum(1 for x in v if x >= 0.8 * ttl_of[k])
        cls[k] = d
    hey_own = classes["hey_own"]
    hey_rel = classes["hey_relay"]
    hey_all = hey_own + hey_rel
    sens = {str(x): {"own": sum(1 for v in hey_own if v > x), "all": sum(1 for v in hey_all if v > x)} for x in (150, 120, 90)}
    return {
        "by_prio": {k: dist(v) for k, v in sorted(by_prio.items())},
        "classes": cls,
        "hey_own_vs_relay_p50_ratio": round(st.median(hey_own) / st.median(hey_rel), 2) if hey_own and hey_rel else None,
        "hey_ttl_sensitivity_aired_older_than_s": sens,
        "longest_wait_s": round(max(r[4] for r in sc["reads"]), 1),
    }


def csma_block(sc: dict[str, Any]) -> dict[str, Any]:
    ev = sc["csma"]
    # episodes: one per ring slot occupant, approximated by (slot, gap > 120 s between events)
    eps = 0
    last: dict[str, float] = {}
    for t, slot, _age, _stage, _hold in sorted(ev):
        if slot not in last or t - last[slot] > 120:
            eps += 1
        last[slot] = t
    holds = [e[4] for e in ev]
    return {
        "events": len(ev),
        "episodes": eps,
        "stage1": sum(1 for e in ev if e[3] == 1),
        "stage2": sum(1 for e in ev if e[3] == 2),
        "age_s": dist([e[2] for e in ev]),
        "hold_ms_min": min(holds) if holds else None,
        "hold_ms_max": max(holds) if holds else None,
        "hwm_aged1_first": sc["hwm"][0][2] if sc["hwm"] else None,
        "hwm_aged1_last": sc["hwm"][-1][2] if sc["hwm"] else None,
    }


def rx_block(sc: dict[str, Any]) -> dict[str, Any]:
    mh = sc["mh"]
    types = Counter({"@": 0, "!": 0, ":": 0})
    for _t, _mid, _toks, sep, _pk, _ln in mh:
        types[sep] += 1
    lasthop = Counter(toks[-1] for _t, _mid, toks, _s, _pk, _ln in mh)
    ids = {mid for _t, mid, _toks, _s, _pk, _ln in mh}
    direct = sum(1 for _t, _mid, toks, _s, _pk, _ln in mh if len(toks) == 1)
    crc = sc["crc"]
    good = len(sc["good"])
    dec_rssi = [pk[0] for _t, _mid, _toks, _s, pk, _ln in mh if pk]
    dec_snr = [pk[1] for _t, _mid, _toks, _s, pk, _ln in mh if pk]
    per_hop: dict[str, list[tuple[int, int, float]]] = defaultdict(list)
    for _t, _mid, toks, _s, pk, _ln in mh:
        if pk:
            per_hop[toks[-1]].append(pk)
    neigh = []
    for call, n in lasthop.most_common():
        v = per_hop.get(call, [])
        neigh.append({"call": call, "n": n,
                      "rssi_p50": round(st.median([x[0] for x in v])) if v else None,
                      "snr_p50": round(st.median([x[1] for x in v])) if v else None,
                      "ferr_abs_p50": round(st.median([abs(x[2]) for x in v])) if v else None})
    fe = [abs(c[2]) for c in crc]
    ack_n = good - sc.get("n_onrxdone", 0)
    # airtime booked on RX: decoded frames by length, CRC-error frames by their size is not logged per frame here
    dec_air = sum(toa(ln) for _t, _mid, _toks, _s, _pk, ln in mh)
    util = sc["util"]
    n_util = len(util)
    return {
        "decoded": len(mh),
        "distinct_ids": len(ids),
        "types": {"hey": types["@"], "pos": types["!"], "text": types[":"]},
        "direct_zero_relay": direct,
        "last_hops": len(lasthop),
        "accepted_new": len(sc["acc"]),
        "dup": sc.get("dup_n", 0),
        "crc_n": len(crc),
        "good_n": good,
        "crc_share_pct": round(100 * len(crc) / (len(crc) + good), 1),
        "crc_rssi_p50": st.median([c[0] for c in crc]),
        "crc_rssi_max": max(c[0] for c in crc),
        "crc_snr_p50": st.median([c[1] for c in crc]),
        "crc_ferr_lt1000_pct": round(100 * sum(1 for x in fe if x < 1000) / len(fe), 1),
        "crc_ferr_gt3000_pct": round(100 * sum(1 for x in fe if x > 3000) / len(fe), 1),
        "decoded_rssi_p50": st.median(dec_rssi),
        "decoded_rssi_max": max(dec_rssi),
        "decoded_snr_p50": st.median(dec_snr),
        "ack_receptions": ack_n,
        "irq_stale": sc.get("irq_stale"),
        "decoded_air_s_model": round(dec_air, 0),
        "util_samples": n_util,
        "rx_ms_sum": sum(u[0] for u in util),
        "tx_ms_sum": sum(u[1] for u in util),
        "util_mean_pct": round(st.mean([u[2] for u in util]), 1),
        "tx_duty_pct": round(100 * sum(u[1] for u in util) / (sc["t_last"] - sc["t_first"]) / 1000, 2),
        "neighbours": neigh,
    }


def cet_block(sc: dict[str, Any], own: str) -> dict[str, Any]:
    tx = sc["cet_tx"]
    ages = [x[1] for x in tx]
    waits = [x[2] for x in tx]
    gw = sc["cet_gw"]
    gw33 = sorted(x[0] for x in gw if x[1] == "OE1XAR-33")
    gaps = [b - a for a, b in zip(gw33, gw33[1:])]
    return {
        "tx_n": len(tx),
        "tx_age_s": dist([float(a) for a in ages], 0),
        "tx_wait_s": dist(waits),
        "tx_trusted_all": all(x[3] == 1 for x in tx),
        "tx_gt150": sum(1 for a in ages if a > 150),
        "drop_lines": sc.get("cet_drop", 0),
        "gw_rx_by_src": dict(Counter(x[1] for x in gw)),
        "gw_rx_gap_s": dist(gaps),
        "rf_rx_by_src": dict(Counter(x[1] for x in sc["cet_rx"])),
        "rf_rx_age_max": max((x[2] for x in sc["cet_rx"]), default=None),
        "rf_rx_gt150": sum(1 for x in sc["cet_rx"] if x[2] > 150),
    }


def stale_block(p: Path) -> dict[str, Any]:
    """{CET} frames over the air older than 150 s (payload time against logger time)."""
    fr = re.compile(r"^(\S+ \S+)\s+\S+ (MH-LoRa|TX-LoRa|RX-LoRa2)\s+\d+ . x([0-9A-F]{8}) H\d\d \S+ \S+ \S+ ([^>]+)>\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")
    rx: list[tuple[str, str, list[str], float]] = []
    new_ids: set[str] = set()
    tx: list[float] = []
    origins: dict[str, set[str]] = defaultdict(set)
    with p.open(errors="replace") as fh:
        for line in fh:
            if "{CET}" not in line or "-LoRa" not in line and "RX-UDP" not in line:
                continue
            m = fr.match(line)
            if not m:
                continue
            ts, kind, mid, path_s, pay = m.groups()
            toks = path_s.split(",")
            a = cet_age(ts, pay)
            if kind == "MH-LoRa":
                rx.append((ts, mid, toks, a))
                origins[toks[0]].add(mid)
            elif kind == "TX-LoRa":
                tx.append(a)
            else:
                new_ids.add(mid + "@" + ts[:16])
    stale = [r for r in rx if r[3] > 150]
    ages = sorted(r[3] for r in stale)
    acc = [r for r in stale if (r[1] + "@" + r[0][:16]) in new_ids]
    return {
        "decoded": len(rx),
        "stale": len(stale),
        "stale_distinct_ids": len({r[1] for r in stale}),
        "stale_last_hops": dict(Counter(r[2][-1] for r in stale)),
        "stale_age_min": round(ages[0]) if ages else None,
        "stale_age_p50": round(st.median(ages)) if ages else None,
        "stale_age_max": round(ages[-1]) if ages else None,
        "stale_accepted_new": len(acc),
        "decoded_by_origin_distinct_ids": {k: len(v) for k, v in origins.items()},
        "own_tx_n": len(tx),
        "own_tx_age_max_logger_based": round(max(tx)) if tx else None,
    }


def hno_block(sc5: dict[str, Any], sc4: dict[str, Any]) -> dict[str, Any]:
    """HB9HNO-49: late copies, accepted as new at -5 (id seen at -4 300..3600 s earlier), relayed by -5."""
    first5: dict[str, float] = {}
    dup_late = 0
    dup_n = 0
    n_frames = 0
    for t, mid, toks, sep, _pk, _ln in sc5["mh"]:
        if mid in first5:
            if toks[-1] == "HB9HNO-49":
                dup_n += 1
                if t - first5[mid] > 300:
                    dup_late += 1
        else:
            first5[mid] = t
        if toks[-1] == "HB9HNO-49":
            n_frames += 1
    first_b: dict[str, float] = {}
    for t, mid, _toks, _sep, _pk, _ln in sc4["mh"]:
        first_b.setdefault(mid, t)
    for t, mid, _toks, _sep, _b, _ln in sc4["txl"]:
        first_b.setdefault(mid, t)
    acc_t: dict[str, list[float]] = defaultdict(list)
    for t, mid in sc5["acc"]:
        acc_t[mid].append(t)
    tx_t: dict[str, list[float]] = defaultdict(list)
    for t, mid, _toks, _sep, _b, _ln in sc5["txl"]:
        tx_t[mid].append(t)
    seen: set[str] = set()
    acc: Counter[str] = Counter()
    rel: Counter[str] = Counter()
    names = {"@": "hey", "!": "pos", ":": "text"}
    for t, mid, toks, sep, _pk, _ln in sc5["mh"]:
        if mid in seen:
            continue
        seen.add(mid)
        if toks[-1] != "HB9HNO-49" or mid not in first_b:
            continue
        if not 300 <= t - first_b[mid] <= 3600:
            continue
        if not any(abs(a - t) < 1.0 for a in acc_t.get(mid, [])):
            continue
        acc[names[sep]] += 1
        if any(0 < x - t < 400 for x in tx_t.get(mid, [])):
            rel[names[sep]] += 1
    return {
        "frames_last_hop": n_frames,
        "dup_copies": dup_n,
        "dup_copies_gt300s_after_first": dup_late,
        "late_accepted_new": dict(acc),
        "late_accepted_new_total": sum(acc.values()),
        "late_relayed": dict(rel),
        "late_relayed_total": sum(rel.values()),
    }


def mutual(a: dict[str, Any], b: dict[str, Any], a_call: str, w: float, t0: float, t1: float) -> tuple[int, int]:
    """Share of A's TX-LoRa frames that B decodes (same id, last hop = A) within -1..w s."""
    rx: dict[str, list[tuple[float, str]]] = defaultdict(list)
    for t, mid, toks, _s, _pk, _ln in b["mh"]:
        rx[mid].append((t, toks[-1]))
    n = hit = 0
    for t, mid, _toks, _sep, _b, _ln in a["txl"]:
        if not t0 <= t <= t1:
            continue
        n += 1
        if any(lh == a_call and -1.0 <= r - t <= w for r, lh in rx.get(mid, [])):
            hit += 1
    return hit, n


def pair_block(s4: dict[str, Any], s5: dict[str, Any]) -> dict[str, Any]:
    """Ids aired by both gateways within 30 min (RING_TX_READ times, one-to-one) and the later-copy airtime."""
    def tx_times(sc: dict[str, Any]) -> tuple[dict[str, list[float]], dict[str, int], dict[tuple[str, float], str]]:
        tt: dict[str, list[float]] = defaultdict(list)
        ll: dict[str, int] = {}
        for t, _slot, _prio, mid, _lat, ln in sc["reads"]:
            tt[mid].append(t)
            ll[mid] = ln
        for o in sc["done"]:
            pass
        srcmap = {(o["id"], round(o["end"], 3)): o["src"] for o in sc["done"] if o["fate"] == "aired"}
        return tt, ll, srcmap

    t4, l4, m4 = tx_times(s4)
    t5, l5, m5 = tx_times(s5)
    both = []
    for mid, ts4 in t4.items():
        free = list(t5.get(mid, []))
        for a in ts4:
            near = [b for b in free if abs(b - a) <= 1800]
            if near:
                b = min(near, key=lambda x: abs(x - a))
                free.remove(b)
                both.append((mid, a, b))
    later = sum(toa(l4[m] if a > b else l5[m]) for m, a, b in both if m in l4 and m in l5)
    pair_air = sum(toa(n) * len(t4[m]) for m, n in l4.items()) + sum(toa(n) * len(t5[m]) for m, n in l5.items())
    cls = Counter((m4.get((m, round(a, 3)), "?"), m5.get((m, round(b, 3)), "?")) for m, a, b in both)
    t0 = max(s4["txl"][0][0], s5["txl"][0][0])
    t1 = min(s4["txl"][-1][0], s5["txl"][-1][0])
    h54, n4 = mutual(s4, s5, "HB9VQQ-4", 3.5, t0, t1)
    h45, n5 = mutual(s5, s4, "HB9VQQ-5", 3.5, t0, t1)
    gaps = [abs(a - b) for _m, a, b in both]
    return {
        "ids_aired_by_any": len(set(t4) | set(t5)),
        "pairs": len(both),
        "distinct_ids_in_pairs": len({m for m, _, _ in both}),
        "relay_relay_pairs": cls.get(("rx_relay", "rx_relay"), 0),
        "udp_udp_pairs": cls.get(("udp_rx", "udp_rx"), 0),
        "later_copy_air_s": round(later, 0),
        "pair_air_s": round(pair_air, 0),
        "later_copy_air_pct": round(100 * later / pair_air, 1),
        "gap_s": dist(gaps),
        "mutual_common_window": {"hb9vqq5_hears_4": {"hit": h54, "n": n4, "pct": round(100 * h54 / n4, 1)},
                                 "hb9vqq4_hears_5": {"hit": h45, "n": n5, "pct": round(100 * h45 / n5, 1)}},
    }


def cet_pair_block(s4: dict[str, Any], s5: dict[str, Any]) -> dict[str, Any]:
    """Same {CET} telegram aired by both gateways, and whether the later one had decoded the earlier copy."""
    def txs(sc: dict[str, Any]) -> dict[str, list[tuple[float, str]]]:
        out: dict[str, list[tuple[float, str]]] = defaultdict(list)
        for t, mid, toks, _sep, body, _ln in sc["txl"]:
            if body.startswith("{CET}"):
                out[mid].append((t, toks[0]))
        return out

    c4, c5 = txs(s4), txs(s5)
    rx: dict[str, dict[str, list[float]]] = {"HB9VQQ-4": defaultdict(list), "HB9VQQ-5": defaultdict(list)}
    for who, sc in (("HB9VQQ-4", s4), ("HB9VQQ-5", s5)):
        for t, mid, toks, _s, _pk, _ln in sc["mh"]:
            rx[who][mid].append((t, toks[-1]))  # type: ignore[arg-type]
    pairs = []
    for mid, l4 in c4.items():
        free = list(c5.get(mid, []))
        for a, org in l4:
            near = [b for b in free if abs(b[0] - a) <= 1800]
            if near:
                b = min(near, key=lambda x: abs(x[0] - a))
                free.remove(b)
                pairs.append((mid, a, b[0], org))
    heard = 0
    for mid, a, b, _org in pairs:
        if a < b:
            ok = any(lh == "HB9VQQ-4" and t < b for t, lh in rx["HB9VQQ-5"].get(mid, []))  # type: ignore[union-attr]
        else:
            ok = any(lh == "HB9VQQ-5" and t < a for t, lh in rx["HB9VQQ-4"].get(mid, []))  # type: ignore[union-attr]
        heard += 1 if ok else 0
    tel33 = {m for m, v in list(c4.items()) + list(c5.items()) if any(o == "OE1XAR-33" for _t, o in v)}
    both33 = {m for m in tel33 if m in c4 and m in c5}
    return {"telegrams_33_aired_by_any": len(tel33), "telegrams_33_aired_by_both": len(both33),
            "pairs": len(pairs), "later_had_decoded_earlier": heard,
            "later_had_decoded_earlier_pct": round(100 * heard / len(pairs), 0) if pairs else None}


def console_block(sc: dict[str, Any]) -> dict[str, Any]:
    lost = sc["lost"]
    conn = sc["connected"]
    costs = []
    for t, _ts in lost:
        nxt = [c for c in conn if c > t]
        if nxt:
            costs.append(nxt[0] - t)
    logins = sc["con_login"]
    pre = sum(1 for t, _ts in lost if any(0 <= t - lg <= 3 for lg in logins))
    full_hour = sum(1 for _t, ts in lost if ts[14:16] == "00" and ts[17:19] in ("01", "02"))
    return {
        "lost": len(lost),
        "at_full_hour": full_hour,
        "other": [ts[11:19] for _t, ts in lost if not (ts[14:16] == "00" and ts[17:19] in ("01", "02"))],
        "preceded_by_login": pre,
        "reconnect_s": dist(costs, 2),
        "reconnect_sum_s": round(sum(costs), 1),
        "reconnect_share_pct": round(100 * sum(costs) / (sc["t_last"] - sc["t_first"]), 2),
    }


def build(norm: Path) -> dict[str, Any]:
    s5 = scan(norm / "hb9vqq5.log", "HB9VQQ-5")
    s4 = scan(norm / "hb9vqq4.log", "HB9VQQ-4")
    up = [h[0] for h in s5["hwm"]]
    heap = [h[0] for h in s5["heap"]]
    d: dict[str, Any] = {
        "meta": {"node": "HB9VQQ-5", "first": s5["first"], "last": s5["last"], "lines": s5["lines"],
                 "span_h": round((s5["t_last"] - s5["t_first"]) / 3600, 2),
                 "partner": {"first": s4["first"], "last": s4["last"], "lines": s4["lines"]}},
        "health": {
            "uptime_first_s": up[0], "uptime_last_s": up[-1], "uptime_samples": len(up),
            "uptime_monotonic": all(b > a for a, b in zip(up, up[1:])),
            "heap_samples": len(heap), "heap_first": heap[0], "heap_last": heap[-1], "heap_min": min(heap), "heap_max": max(heap),
            "heap_min_free_ever_const": len({h[1] for h in s5["heap"]}) == 1, "heap_max_alloc_const": len({h[2] for h in s5["heap"]}) == 1,
            "crash_hits": s5.get("crash_hits", 0),
            "keep_n": s5.get("keep_n"), "beat_n": s5.get("beat_n"),
            "status_queued": dist([float(x) for x in s5["status"]], 2),
            "status_mean": round(st.mean(s5["status"]), 2),
            "status_ge3_pct": round(100 * sum(1 for x in s5["status"] if x >= 3) / len(s5["status"]), 2),
            "status_ge5_pct": round(100 * sum(1 for x in s5["status"] if x >= 5) / len(s5["status"]), 2),
            "drops_ring": len(s5["drops"]),
            "trickle": {k: s5.get("trickle_" + k, 0) for k in ("SEND", "SUPPRESS", "TOPO_CHANGE")},
            "trickle_send_forced": s5.get("trickle_send_forced", 0),
            "tx_udp": s5.get("tx_udp", 0),
            "oe1xar62_lines": s5.get("oe1xar62_lines", 0),
        },
        "rx": rx_block(s5),
        "console": console_block(s5),
        "ring": {
            "writes_seen": len(s5["writes"]),
            "reads": len(s5["reads"]),
            "supersede": s5.get("sup", 0), "supersede_keep": s5.get("sup_keep", 0),
            "fate": fate_block(s5, "HB9VQQ-5"),
            "latency": latency_block(s5),
            "csma": csma_block(s5),
            "hwm_last": list(s5["hwm"][-1]),
        },
        "cet": {**cet_block(s5, "HB9VQQ-5"), "stale": stale_block(norm / "hb9vqq5.log")},
        "hno": hno_block(s5, s4),
        "pair": pair_block(s4, s5),
        "cet_pair": cet_pair_block(s4, s5),
    }
    return d


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    data = build(a.norm)
    a.out.write_text(json.dumps(data, indent=1, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
