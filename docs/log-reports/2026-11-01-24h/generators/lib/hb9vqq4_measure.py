#!/usr/bin/env python3
"""Helper for the HB9VQQ-4 report page: numbers that need the normalised raw logs of the HB9 pair.

Usage: python3 -I hb9vqq4_measure.py --norm <dir with hb9vqq4.log + hb9vqq5.log> \
           --out <data/extra/hb9vqq4_measure.json>

Measures (stdlib only; each log is read once into memory, 32 MB / 19 MB):
  * Slot fate per ring write (aired / superseded in place / vanished), exact join on slot + msg_id;
    foreign-copy test for vanished and aired relays; per-capture-hour profile (overhear-cancel hole).
    A write is "vanished" when its slot is written again (or the capture ends > 400 s later)
    without a RING_TX_READ or RING_SUPERSEDE for it.
  * Wait (RING_TX_READ lat=) of prio-5 HEY by source: own (auto_pos) against relayed (rx_relay).
  * Pair redundancy: ids aired by both nodes (one-to-one pairs within 30 min), classes, later-copy airtime.
  * Mutual audibility in the common capture window (same id, last hop = sender, -1 .. +3.5 s).
  * {CET} frames older than 150 s (logger clock - 3600 s - payload, payload is UTC) and who radiated
    them; late HB9HNO-49 copies accepted as new / relayed.
  * Frequency error per last hop (HB9LEK-12) and strong stale-IRQ events at HB9VQQ-4.
Airtime model: SF11 / BW250 / CR4:6, preamble 8, explicit header, CRC (reproduces CHANNEL_UTIL tx within 0.5 %).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

OWN = {"hb9vqq4": "HB9VQQ-4", "hb9vqq5": "HB9VQQ-5"}
EPOCH = datetime(1970, 1, 1)

W = re.compile(
    r"RING_WRITE slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=([0-9A-F]{8}) .*src=(\S+)"
)
R = re.compile(
    r"RING_TX_READ slot=(\d+) prio=(\d) type=(\w+) status=\w+ len=(\d+) msg_id=([0-9A-F]{8}) .*lat=(\d+)ms"
)
S = re.compile(r"RING_SUPERSEDE slot=(\d+) prio=\d old_id=([0-9A-F]{8})")
FRAME = re.compile(
    r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\s+\S+ (MH-LoRa|TX-LoRa|RX-LoRa2)\s+\d+ (.) "
    r"x([0-9A-F]{8}) (H\d\d) \S+ \S+ \S+ ([^>]+)>(.*)"
)
CET_BODY = re.compile(r"^\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")
CETTX = re.compile(r"\[CET\];tx;age_s;(-?\d+)")
PKT = re.compile(r"Received packet: RSSI:\s+(-?\d+) dBm / SNR:\s+(-?\d+) dB / Frequency error:\s+(-?[\d.]+)")
CRC = re.compile(r"CRC_ERROR rssi=(-?\d+) snr=(-?\d+) freq_err=(-?[\d.]+)")
STALE = re.compile(r"RX_IRQ_STALE rssi=(-?\d+) flags=(\S+)")


def tsec(s: str) -> float:
    """Logger time stamp 'YYYY-MM-DD HH:MM:SS(.mmm)' as seconds (naive, no time-zone shift)."""
    fmt = "%Y-%m-%d %H:%M:%S.%f" if "." in s[:23] else "%Y-%m-%d %H:%M:%S"
    return (datetime.strptime(s[:23] if "." in s[:23] else s[:19], fmt) - EPOCH).total_seconds()


def toa(n: int) -> float:
    sf, bw, cr = 11, 250000.0, 2
    ts = (2**sf) / bw
    pl = 8 + max(math.ceil((8 * n - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
    return (8 + 4.25) * ts + pl * ts


def quant(v: list[float], p: float) -> float:
    s = sorted(v)
    return s[min(len(s) - 1, int(p * len(s)))]


def dist(v: list[float]) -> dict[str, float | int]:
    return {
        "n": len(v),
        "p50": round(statistics.median(v), 1),
        "p90": round(quant(v, 0.9), 1),
        "max": round(max(v), 1),
    }


class Log:
    """One normalised log: all lines plus the frame events used by several measurements."""

    def __init__(self, path: Path, own: str) -> None:
        self.name = path.stem
        self.own = own
        self.lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        self.t0 = tsec(self.lines[0])
        self.t1 = tsec(self.lines[-1])
        self.first = self.lines[0][:19]
        self.last = self.lines[-1][:19]
        # (t, kind, msg_id, hop, path, sep, cet_payload|None)
        self.frames: list[tuple[float, str, str, str, str, str, str | None]] = []
        self.rx: dict[str, list[tuple[float, str]]] = defaultdict(list)
        for ln in self.lines:
            if "-LoRa" not in ln:
                continue
            m = FRAME.match(ln)
            if not m:
                continue
            t = tsec(m.group(1))
            kind, sep, mid, hop, path_s, rest = m.group(2), m.group(3), m.group(4), m.group(5), m.group(6), m.group(7)
            cm = CET_BODY.match(rest) if sep == ":" else None
            self.frames.append((t, kind, mid, hop, path_s, sep, cm.group(1) if cm else None))
            if kind == "MH-LoRa":
                self.rx[mid].append((t, path_s))


# ----------------------------------------------------------------------------------------------
# slot fate
# ----------------------------------------------------------------------------------------------
def slot_fate(lg: Log) -> dict[str, Any]:
    occ: dict[str, dict[str, Any]] = {}
    done: list[dict[str, Any]] = []
    tx_times: dict[str, list[float]] = defaultdict(list)
    tx_len: dict[str, int] = {}
    lat_by_src: dict[str, list[float]] = defaultdict(list)
    lines = lg.lines
    for i, line in enumerate(lines):
        if "RING_" not in line:
            continue
        m = W.search(line)
        if m:
            slot, typ, _st, ln, mid, src = m.groups()
            t = tsec(line)
            old = occ.get(slot)
            if old is not None:
                sup = False
                for x in lines[i + 1:i + 4]:
                    sm = S.search(x)
                    if sm and sm.groups() == (slot, old["id"]):
                        sup = True
                        break
                old["fate"] = "superseded" if sup else "vanished"
                old["end"] = t
                done.append(old)
            occ[slot] = {"id": mid, "t": t, "src": src, "type": typ, "len": int(ln), "slot": slot}
            continue
        m = R.search(line)
        if m:
            slot, prio, _typ, ln, mid, lat = m.groups()
            t = tsec(line)
            tx_times[mid].append(t)
            tx_len[mid] = int(ln)
            o = occ.get(slot)
            if o is not None and o["id"] == mid:
                o["fate"] = "aired"
                o["end"] = t
                o["prio"] = int(prio)
                done.append(o)
                del occ[slot]
                if prio == "5" and o["type"] == "40":
                    lat_by_src[o["src"]].append(int(lat) / 1000.0)
            elif prio == "5":
                lat_by_src["unjoined"].append(int(lat) / 1000.0)
    for o in occ.values():
        o["fate"] = "vanished" if lg.t1 - o["t"] > 400 else "pending"
        o["end"] = lg.t1
        done.append(o)

    def foreign(o: dict[str, Any]) -> float | None:
        for t, path_s in lg.rx.get(o["id"], []):
            toks = path_s.split(",")
            if o["t"] < t <= o["end"] and len(toks) >= 2 and toks[-1] != lg.own:
                return t - o["t"]
        return None

    relay = [o for o in done if o["src"] == "rx_relay"]
    van = [o for o in done if o["fate"] == "vanished"]
    aired_relay = [o for o in relay if o["fate"] == "aired"]
    vf = [foreign(o) for o in van]
    af = [foreign(o) for o in aired_relay]
    by_src: dict[str, dict[str, int]] = defaultdict(lambda: {"aired": 0, "superseded": 0, "vanished": 0, "pending": 0})
    for o in done:
        by_src[o["src"]][o["fate"]] += 1
    nh = 24
    hv = Counter(int((o["t"] - lg.t0) // 3600) for o in van)
    hf = Counter(int((o["t"] - lg.t0) // 3600) for o, x in zip(aired_relay, af) if x is not None)
    hw = Counter(int((o["t"] - lg.t0) // 3600) for o in relay)
    first_copy = [x for x in vf if x is not None]
    inj = [o for o in done if o["src"] == "udp_rx" and o["fate"] == "aired"]
    inj_f = [foreign(o) for o in inj]
    van_by_prio = Counter(str({"40": 5, "21": 4, "3A": 3}.get(o["type"], 0)) for o in van)
    fates = Counter(o["fate"] for o in done)
    own_hey = lat_by_src.get("auto_pos", [])
    rel_hey = lat_by_src.get("rx_relay", [])
    out: dict[str, Any] = {
        "writes": len(done),
        "fates": dict(fates),
        "by_src": {k: dict(v) for k, v in sorted(by_src.items())},
        "relay_writes": len(relay),
        "relay_fates": dict(Counter(o["fate"] for o in relay)),
        "vanished_by_prio": dict(van_by_prio),
        "vanished_with_foreign_copy": sum(1 for x in vf if x is not None),
        "vanished_total": len(van),
        "aired_relays": len(aired_relay),
        "aired_relays_with_foreign_copy": sum(1 for x in af if x is not None),
        "first_foreign_copy_s": dist(first_copy) if first_copy else None,
        "inject_aired": len(inj),
        "inject_aired_with_foreign_copy": sum(1 for x in inj_f if x is not None),
        "inject_aired_airtime_s": round(sum(toa(o["len"]) for o in inj), 1),
        "inject_vanished": sum(1 for o in van if o["src"] == "udp_rx"),
        "vanished_airtime_s": round(sum(toa(o["len"]) for o in van), 1),
        "aired_airtime_foreign_s": round(
            sum(toa(o["len"]) for o, x in zip(aired_relay, af) if x is not None), 1
        ),
        "tx_airtime_s_distinct_ids": round(sum(toa(n) for n in tx_len.values()), 1),
        "hourly_vanished": [hv.get(k, 0) for k in range(nh)],
        "hourly_aired_with_foreign_copy": [hf.get(k, 0) for k in range(nh)],
        "hourly_relay_writes": [hw.get(k, 0) for k in range(nh)],
        "hey_wait_s": {
            "own": dist(own_hey) if own_hey else None,
            "relay": dist(rel_hey) if rel_hey else None,
            "ratio_p50": round(
                round(statistics.median(own_hey), 1) / round(statistics.median(rel_hey), 1), 2
            )
            if own_hey and rel_hey
            else None,
        },
    }
    out["_internal"] = {"done": done, "tx_times": tx_times, "tx_len": tx_len}
    return out


# ----------------------------------------------------------------------------------------------
# pair redundancy and mutual audibility
# ----------------------------------------------------------------------------------------------
def pair_redundancy(f4: dict[str, Any], f5: dict[str, Any]) -> dict[str, Any]:
    t4, t5 = f4["_internal"]["tx_times"], f5["_internal"]["tx_times"]
    l4, l5 = f4["_internal"]["tx_len"], f5["_internal"]["tx_len"]
    src4 = {(o["id"], round(o["end"], 3)): o["src"] for o in f4["_internal"]["done"] if o["fate"] == "aired"}
    src5 = {(o["id"], round(o["end"], 3)): o["src"] for o in f5["_internal"]["done"] if o["fate"] == "aired"}
    both: list[tuple[str, float, float]] = []
    for mid, ts4 in t4.items():
        free = list(t5.get(mid, []))
        for a in ts4:
            near = [b for b in free if abs(b - a) <= 1800]
            if near:
                b = min(near, key=lambda x: abs(x - a))
                free.remove(b)
                both.append((mid, a, b))
    later_air = sum(toa(l4[m] if a > b else l5[m]) for m, a, b in both)
    pair_air = sum(toa(n) * len(t4[m]) for m, n in l4.items()) + sum(toa(n) * len(t5[m]) for m, n in l5.items())
    cls = Counter((src4.get((m, round(a, 3)), "?"), src5.get((m, round(b, 3)), "?")) for m, a, b in both)
    gaps = [abs(a - b) for _, a, b in both]
    first4 = sum(1 for _, a, b in both if a < b)
    return {
        "ids_aired_by_at_least_one": len(set(t4) | set(t5)),
        "pairs": len(both),
        "distinct_ids_in_pairs": len({m for m, _, _ in both}),
        "relay_relay_pairs": cls.get(("rx_relay", "rx_relay"), 0),
        "inject_inject_pairs": cls.get(("udp_rx", "udp_rx"), 0),
        "classes": {f"{a}/{b}": n for (a, b), n in cls.most_common(8)},
        "later_copy_airtime_s": round(later_air, 1),
        "both_nodes_tx_airtime_s": round(pair_air, 1),
        "later_copy_share_pct": round(100 * later_air / pair_air, 1),
        "gap_p50_s": round(statistics.median(gaps), 1),
        "first_is_hb9vqq4_pairs": first4,
    }


def mutual(l4: Log, l5: Log, w: float = 3.5) -> dict[str, Any]:
    tx4 = [(t, mid) for t, k, mid, *_ in l4.frames if k == "TX-LoRa"]
    tx5 = [(t, mid) for t, k, mid, *_ in l5.frames if k == "TX-LoRa"]
    t0 = max(tx4[0][0], tx5[0][0])
    t1 = min(tx4[-1][0], tx5[-1][0])

    def share(tx: list[tuple[float, str]], rx: dict[str, list[tuple[float, str]]], a_call: str) -> tuple[int, int]:
        n = hit = 0
        for t, mid in tx:
            if not (t0 <= t <= t1):
                continue
            n += 1
            if any(p.split(",")[-1] == a_call and -1.0 <= r - t <= w for r, p in rx.get(mid, [])):
                hit += 1
        return hit, n

    h5, n4 = share(tx4, l5.rx, "HB9VQQ-4")
    h4, n5 = share(tx5, l4.rx, "HB9VQQ-5")
    return {
        "window_s": w,
        "hb9vqq5_decodes_hb9vqq4": {"hit": h5, "of": n4, "pct": round(100 * h5 / n4, 1)},
        "hb9vqq4_decodes_hb9vqq5": {"hit": h4, "of": n5, "pct": round(100 * h4 / n5, 1)},
    }


# ----------------------------------------------------------------------------------------------
# stale {CET} and HB9HNO-49
# ----------------------------------------------------------------------------------------------
def stale_cet(lg: Log) -> dict[str, Any]:
    rx_stale: list[tuple[float, str, str, float, str]] = []
    n_cet = 0
    tx_age: list[float] = []
    new_keys: set[tuple[str, int]] = set()
    for t, kind, mid, hop, path_s, _sep, pay in lg.frames:
        if pay is None:
            continue
        age = t - 3600.0 - tsec(pay)
        if kind == "MH-LoRa":
            n_cet += 1
            if age > 150:
                rx_stale.append((t, mid, path_s, age, hop))
        elif kind == "TX-LoRa":
            tx_age.append(age)
        else:
            new_keys.add((mid, int(t)))
    node_tx_age = []
    for ln in lg.lines:
        if "[CET];tx;age_s;" in ln:
            m = CETTX.search(ln)
            if m:
                node_tx_age.append(int(m.group(1)))
    accepted = [r for r in rx_stale if (r[1], int(r[0])) in new_keys]
    ages = sorted(r[3] for r in rx_stale)
    hops = Counter(r[2].split(",")[-1] for r in rx_stale)
    return {
        "cet_frames_decoded": n_cet,
        "stale_frames": len(rx_stale),
        "stale_distinct_ids": len({r[1] for r in rx_stale}),
        "stale_ids": sorted({r[1] for r in rx_stale}),
        "stale_last_hop": dict(hops),
        "stale_age_s": {"min": int(ages[0]), "p50": int(ages[len(ages) // 2]), "max": int(ages[-1])}
        if ages
        else None,
        "stale_accepted_new": len(accepted),
        "stale_accepted_new_detail": [
            {"time": (EPOCH + timedelta(seconds=r[0])).strftime("%Y-%m-%d %H:%M:%S"),
             "id": r[1], "path": r[2], "age_s": round(r[3]), "hop": r[4]}
            for r in accepted
        ],
        "own_cet_tx": len(tx_age),
        "own_cet_tx_age_max_logger_s": round(max(tx_age)) if tx_age else None,
        "own_cet_tx_over_150": sum(1 for a in tx_age if a > 150),
        "own_cet_tx_node_age_max_s": max(node_tx_age) if node_tx_age else None,
        "own_cet_tx_node_lines": len(node_tx_age),
    }


def cet_pairs(l4: Log, l5: Log, f4: dict[str, Any], f5: dict[str, Any]) -> dict[str, Any]:
    """{CET} frames aired by both nodes (same id, within 30 min): did the later node hear the earlier copy?"""
    def tx(lg: Log) -> dict[str, list[float]]:
        out: dict[str, list[float]] = defaultdict(list)
        for t, k, mid, _h, _p, _s, pay in lg.frames:
            if k == "TX-LoRa" and pay is not None:
                out[mid].append(t)
        return out

    t4, t5 = tx(l4), tx(l5)
    len4, len5 = f4["_internal"]["tx_len"], f5["_internal"]["tx_len"]
    pairs = heard = 0
    air_later = air_all = 0.0
    for mid, ts4 in t4.items():
        free = list(t5.get(mid, []))
        for a in ts4:
            near = [b for b in free if abs(b - a) <= 1800]
            if not near:
                continue
            b = min(near, key=lambda x: abs(x - a))
            free.remove(b)
            pairs += 1
            later_is_4 = a > b
            lg, other, t_later, t_early = (l4, l5.own, a, b) if later_is_4 else (l5, l4.own, b, a)
            if any(
                p.split(",")[-1] == other and t_early - 1.0 <= r <= t_later for r, p in lg.rx.get(mid, [])
            ):
                heard += 1
            air_later += toa(len4.get(mid, 0) if later_is_4 else len5.get(mid, 0))
    for mid, n in len4.items():
        air_all += toa(n) * len(t4.get(mid, []))
    for mid, n in len5.items():
        air_all += toa(n) * len(t5.get(mid, []))
    return {
        "pairs": pairs,
        "later_node_had_heard_earlier_copy": heard,
        "later_copy_airtime_s": round(air_later, 1),
    }


def log_markers(lg: Log) -> dict[str, Any]:
    send = send_wrong = 0
    keep = qfull = qfull_after_keep = 0
    tx_udp = 0
    prev_keep_id = ""
    for ln in lg.lines:
        if "MC-TRICKLE] SEND" in ln:
            send += 1
            m = re.search(r"consistent=(\d+)<k=(\d+)", ln)
            if m and int(m.group(1)) >= int(m.group(2)):
                send_wrong += 1
        elif "RING_SUPERSEDE_KEEP" in ln:
            keep += 1
            m = re.search(r"new_id=([0-9A-F]{8})", ln)
            prev_keep_id = m.group(1) if m else ""
        elif "[LOG] RLY" in ln and "q=full" in ln:
            qfull += 1
            m = re.search(r"RLY x([0-9A-F]{8})", ln)
            if m and m.group(1) == prev_keep_id:
                qfull_after_keep += 1
        elif " TX-UDP " in ln:
            tx_udp += 1
    return {
        "trickle_send": send,
        "trickle_send_consistent_ge_k": send_wrong,
        "supersede_keep": keep,
        "rly_q_full": qfull,
        "rly_q_full_directly_after_supersede_keep_same_id": qfull_after_keep,
        "tx_udp_lines": tx_udp,
    }


def hno_late(lg: Log, other: Log) -> dict[str, Any]:
    """HB9HNO-49 first copies accepted as NEW here whose id the other node had seen 300..3600 s earlier."""
    first_b: dict[str, float] = {}
    for t, k, mid, *_ in other.frames:
        if k in ("MH-LoRa", "TX-LoRa"):
            first_b.setdefault(mid, t)
    acc_t: dict[str, list[float]] = defaultdict(list)
    tx_t: dict[str, list[float]] = defaultdict(list)
    for t, k, mid, *_ in lg.frames:
        if k == "RX-LoRa2":
            acc_t[mid].append(t)
        elif k == "TX-LoRa":
            tx_t[mid].append(t)
    seen: set[str] = set()
    acc: Counter[str] = Counter()
    rel: Counter[str] = Counter()
    names = {"@": "HEY", "!": "Position", ":": "Text"}
    for t, k, mid, _hop, path_s, sep, _p in lg.frames:
        if k != "MH-LoRa" or mid in seen:
            continue
        seen.add(mid)
        if path_s.split(",")[-1] != "HB9HNO-49" or mid not in first_b:
            continue
        if not (300 <= t - first_b[mid] <= 3600):
            continue
        if not any(abs(a - t) < 1.0 for a in acc_t.get(mid, [])):
            continue
        acc[names[sep]] += 1
        if any(0 < x - t < 400 for x in tx_t.get(mid, [])):
            rel[names[sep]] += 1
    return {
        "accepted_new_by_type": dict(acc),
        "accepted_new_total": sum(acc.values()),
        "relayed_by_type": dict(rel),
        "relayed_total": sum(rel.values()),
    }


def hno_frames(lg: Log) -> dict[str, Any]:
    """All frames whose last hop is HB9HNO-49: count, share, and delay of copies against the first copy."""
    first: dict[str, float] = {}
    n = n_dup_late = 0
    for t, k, mid, _h, path_s, _s, _p in lg.frames:
        if k != "MH-LoRa":
            continue
        first.setdefault(mid, t)
        if path_s.split(",")[-1] == "HB9HNO-49":
            n += 1
            if t - first[mid] > 300:
                n_dup_late += 1
    return {"frames": n, "frames_over_300s_after_first_copy": n_dup_late}


# ----------------------------------------------------------------------------------------------
# radio environment (HB9VQQ-4)
# ----------------------------------------------------------------------------------------------
def radio(lg: Log) -> dict[str, Any]:
    last: tuple[int, int, float] | None = None
    per: dict[str, list[float]] = defaultdict(list)
    crc_ferr: list[float] = []
    stale_hi: list[tuple[int, str]] = []
    n_stale = 0
    for ln in lg.lines:
        if "CRC_ERROR" in ln:
            m = CRC.search(ln)
            if m:
                crc_ferr.append(abs(float(m.group(3))))
        elif "Received packet" in ln:
            m = PKT.search(ln)
            if m:
                last = (int(m.group(1)), int(m.group(2)), float(m.group(3)))
        elif "MH-LoRa" in ln and last is not None:
            m2 = FRAME.match(ln)
            if m2:
                per[m2.group(6).split(",")[-1]].append(abs(last[2]))
                last = None
        elif "RX_IRQ_STALE rssi=" in ln:
            n_stale += 1
            m = STALE.search(ln)
            if m and int(m.group(1)) > -80:
                stale_hi.append((int(m.group(1)), m.group(2)))
    lek = per.get("HB9LEK-12", [])
    others = {
        k: round(statistics.median(v))
        for k, v in per.items()
        if k != "HB9LEK-12" and len(v) >= 40
    }
    hist = Counter(r for r, _ in stale_hi)
    return {
        "lek_frames": len(lek),
        "lek_median_abs_ferr_hz": round(statistics.median(lek)) if lek else None,
        "lek_share_over_3000hz_pct": round(100 * sum(1 for x in lek if x > 3000) / len(lek), 1) if lek else None,
        "other_neighbours_ge40_frames": len(others),
        "other_neighbours_max_median_ferr_hz": max(others.values()) if others else None,
        "other_neighbours_max_median_ferr_call": max(others, key=lambda k: others[k]) if others else None,
        "crc_errors": len(crc_ferr),
        "crc_ferr_4000_5000": sum(1 for x in crc_ferr if 4000 <= x < 5000),
        "crc_ferr_over_3000": sum(1 for x in crc_ferr if x > 3000),
        "stale_irq_total": n_stale,
        "stale_irq_over_m80": len(stale_hi),
        "stale_irq_m72_m71": hist.get(-72, 0) + hist.get(-71, 0),
        "stale_irq_over_m80_pre": sum(1 for _, f in stale_hi if f == "PRE"),
    }


def strip(f: dict[str, Any]) -> dict[str, Any]:
    return {k: v for k, v in f.items() if k != "_internal"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--norm", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    l4 = Log(a.norm / "hb9vqq4.log", OWN["hb9vqq4"])
    l5 = Log(a.norm / "hb9vqq5.log", OWN["hb9vqq5"])
    f4, f5 = slot_fate(l4), slot_fate(l5)
    res: dict[str, Any] = {
        "generated_by": "generators/lib/hb9vqq4_measure.py",
        "capture": {"hb9vqq4": [l4.first, l4.last], "hb9vqq5": [l5.first, l5.last]},
        "nodes": {
            "hb9vqq4": {
                "fate": strip(f4),
                "stale_cet": stale_cet(l4),
                "hno_late_at_node": hno_late(l4, l5),
                "hno_frames": hno_frames(l4),
                "markers": log_markers(l4),
                "radio": radio(l4),
            },
            "hb9vqq5": {
                "fate": strip(f5),
                "stale_cet": stale_cet(l5),
                "hno_late_at_node": hno_late(l5, l4),
                "hno_frames": hno_frames(l5),
            },
        },
        "stale_union_distinct_ids": len(
            set(stale_cet(l4)["stale_ids"]) | set(stale_cet(l5)["stale_ids"])
        ),
        "pair": pair_redundancy(f4, f5),
        "cet_pair": cet_pairs(l4, l5, f4, f5),
        "mutual": mutual(l4, l5),
    }
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(res, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
