#!/usr/bin/env python3
"""Independent q5 ({CET} time signal) checks over the five OE3 logs.

Usage: python3 -I v_cet_oe3.py <norm-dir>
Prints one JSON object with all measured numbers.
"""
from __future__ import annotations

import json
import re
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

NODES: dict[str, str] = {
    "oe3xhl": "OE3XHL-12",
    "oe3xia": "OE3XIA-12",
    "oe3xir": "OE3XIR-12",
    "oe3xoc": "OE3XOC-12",
    "oe3xwj": "OE3XWJ-12",
}
PFX = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})  (.*)$")
FRAME = re.compile(
    r"^(?:(\d\d:\d\d:\d\d) )?(\[LOG\]|RX-UDP|TX-LoRa|MH-LoRa)\s+\d+ : x([0-9A-F]{8}) H(\d\d) .*? "
    r"([A-Z0-9-]+(?:,[A-Z0-9-]+)*)>\*:\{CET\}(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)"
)
LOGTX = re.compile(r"^(\d\d:\d\d:\d\d) \[LOG\] TX x([0-9A-F]{8}) ")
CETLINE = re.compile(r"^\[CET\];(gw_rx|tx|rx|drop);.*?age_s;(-?\d+|na)")
NTP = re.compile(r"^\[NTP\];ok;epoch;(\d+)")
NEED = re.compile(r"^\[NBR\]\|NEED\|\d+\|([0-9A-F]{8})\|(\w)\|(\w)\|([0-9a-fA-F]+)\|([0-9a-fA-F]+)\|")
CANCEL = re.compile(r"^\[NBR\]\|(CANCEL\??|REFUSE)\|\d+\|([0-9A-F]{8})\|")
DROPSTALE = re.compile(r"RING_DROP_STALE.*reason=(\w+)")


def ts_utc(s: str) -> float:
    """Logger wall clock -> epoch, assuming logger = CET (UTC+1); validated via NTP."""
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=timezone.utc).timestamp() - 3600.0


def content_utc(s: str) -> float:
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc).timestamp()


def pct(v: list[float], q: float) -> float:
    if not v:
        return float("nan")
    v = sorted(v)
    return v[min(len(v) - 1, int(round(q * (len(v) - 1))))]


def main() -> None:
    d = Path(sys.argv[1])
    out: dict = {"nodes": {}}
    id2content: dict[str, float] = {}
    id_contents: dict[str, set[float]] = defaultdict(set)
    srcs: Counter[str] = Counter()
    # radiation key (id, path) -> list of (node, rx_utc)
    rad: dict[tuple[str, str], list[tuple[str, float]]] = defaultdict(list)
    own_tx: list[tuple[str, str, float, int | None]] = []  # node, id, utc, fw_age
    node_rx_ev: dict[str, list[tuple[str, str, float]]] = defaultdict(list)  # node -> (id,path,utc)
    windows: dict[str, tuple[float, float]] = {}
    need_cases: dict[str, list[tuple[str, str, str, str]]] = defaultdict(list)
    cancels: dict[str, list[tuple[str, str]]] = defaultdict(list)
    for node, call in NODES.items():
        nd: dict = {}
        ntp_off: list[float] = []
        nodeclk_off: list[float] = []
        fw: dict[str, list[int]] = defaultdict(list)
        drops: Counter[str] = Counter()
        udp_ids: set[str] = set()
        first = last = None
        pending_fw_tx: int | None = None
        with open(d / f"{node}.log", "rb") as fh:
            for raw in fh:
                line = raw.decode("latin-1").rstrip("\r\n")
                m = PFX.match(line)
                if not m:
                    continue
                t = ts_utc(m.group(1))
                if first is None:
                    first = t
                last = t
                body = m.group(2)
                if mm := NTP.match(body):
                    ntp_off.append(t - int(mm.group(1)))
                    continue
                if mm := CETLINE.match(body):
                    if mm.group(2) != "na":
                        fw[mm.group(1)].append(int(mm.group(2)))
                        if mm.group(1) == "tx":
                            pending_fw_tx = int(mm.group(2))
                    continue
                if mm := DROPSTALE.search(body):
                    drops[mm.group(1)] += 1
                    continue
                if mm := NEED.match(body):
                    need_cases[node].append((mm.group(1), mm.group(2), mm.group(3), mm.group(4) + "/" + mm.group(5)))
                    continue
                if mm := CANCEL.match(body):
                    cancels[node].append((mm.group(1), mm.group(2)))
                    continue
                if mm := LOGTX.match(body):
                    own_tx.append((node, mm.group(2), t, pending_fw_tx))
                    pending_fw_tx = None
                    continue
                if "{CET}" not in body:
                    continue
                if "FCS:0000" in body:
                    continue
                mm = FRAME.match(body)
                if not mm:
                    continue
                nclk, kind, mid, _h, path, cont = mm.groups()
                c = content_utc(cont)
                src = path.split(",")[0]
                srcs[src] += 1
                id_contents[mid].add(c)
                id2content[mid] = c
                if kind == "[LOG]":
                    if nclk:
                        # node clock (time of day) minus logger time of day, folded to +-12 h
                        lt = datetime.strptime(m.group(1)[11:19], "%H:%M:%S")
                        nt = datetime.strptime(nclk, "%H:%M:%S")
                        dd = (nt - lt).total_seconds()
                        dd = (dd + 43200) % 86400 - 43200
                        nodeclk_off.append(dd)
                    rad[(mid, path)].append((node, t))
                    node_rx_ev[node].append((mid, path, t))
                elif kind == "RX-UDP":
                    udp_ids.add(mid)
        windows[node] = (first or 0.0, last or 0.0)
        nd["ntp_logger_minus_epoch_s"] = {
            "n": len(ntp_off), "min": min(ntp_off) if ntp_off else None,
            "p50": statistics.median(ntp_off) if ntp_off else None, "max": max(ntp_off) if ntp_off else None,
        }
        nd["nodeclock_minus_logger_s"] = {
            "n": len(nodeclk_off), "p50": statistics.median(nodeclk_off) if nodeclk_off else None,
            "min": min(nodeclk_off) if nodeclk_off else None, "max": max(nodeclk_off) if nodeclk_off else None,
        }
        nd["fw_age"] = {k: {"n": len(v), "p50": statistics.median(v), "max": max(v), "min": min(v),
                            "gt150": sum(1 for x in v if x > 150)} for k, v in fw.items()}
        nd["ring_drop_stale_reasons"] = dict(drops)
        nd["rx_udp_ids"] = len(udp_ids)
        out["nodes"][node] = nd

    out["sources"] = dict(srcs)
    out["ids_with_multiple_contents"] = sum(1 for v in id_contents.values() if len(v) > 1)
    # telegrams of OE1XAR-33 by content time
    conts = sorted({c for mid, c in id2content.items()})
    gaps = [b - a for a, b in zip(conts, conts[1:])]
    out["telegrams"] = {
        "distinct_ids": len(id2content), "distinct_contents": len(conts),
        "first_utc": datetime.fromtimestamp(conts[0], timezone.utc).isoformat(),
        "last_utc": datetime.fromtimestamp(conts[-1], timezone.utc).isoformat(),
        "gap_p50": statistics.median(gaps), "gap_min": min(gaps), "gap_max": max(gaps),
        "gap_p90": pct(gaps, 0.9),
        "gaps_gt_700": [(datetime.fromtimestamp(conts[i], timezone.utc).isoformat(), gaps[i])
                        for i in range(len(gaps)) if gaps[i] > 700],
        "expected_from_span": round((conts[-1] - conts[0]) / statistics.median(gaps)) + 1,
    }
    # own TX ages (logger based)
    ages_by_node: dict[str, list[float]] = defaultdict(list)
    fw_by_node: dict[str, list[int]] = defaultdict(list)
    unmatched = 0
    for node, mid, t, fwa in own_tx:
        if mid not in id2content:
            continue  # not a CET frame
        ages_by_node[node].append(t - id2content[mid])
        if fwa is not None:
            fw_by_node[node].append(fwa)
    out["own_tx"] = {n: {"n": len(v), "p50": round(statistics.median(v), 1), "max": round(max(v), 1),
                         "gt150": sum(1 for x in v if x > 150), "gt100": sum(1 for x in v if x > 100)}
                     for n, v in ages_by_node.items()}
    out["own_tx_total"] = sum(len(v) for v in ages_by_node.values())
    # own TX that followed an already heard gateway radiation (1 via, not own) at the same node
    follow = Counter()
    tot = Counter()
    rx_by_node_id: dict[tuple[str, str], list[tuple[float, str]]] = defaultdict(list)
    for node, evs in node_rx_ev.items():
        for mid, path, t in evs:
            rx_by_node_id[(node, mid)].append((t, path))
    for node, mid, t, _ in own_tx:
        if mid not in id2content:
            continue
        tot[node] += 1
        prior = [p for (tt, p) in rx_by_node_id.get((node, mid), []) if tt < t and len(p.split(",")) == 2]
        if prior:
            follow[node] += 1
    out["own_tx_after_heard_gw"] = {n: [follow[n], tot[n]] for n in tot}
    out["own_tx_after_heard_gw_total"] = [sum(follow.values()), sum(tot.values())]
    # add own TX as radiations
    for node, mid, t, _ in own_tx:
        if mid in id2content:
            rad[(mid, f"OE1XAR-33,{NODES[node]}")].append((node, t))
    # radiation ages and per-relayer stale shares
    per_relayer: dict[str, list[float]] = defaultdict(list)
    cls_age: dict[str, list[float]] = defaultdict(list)
    own_calls = set(NODES.values())
    for (mid, path), evs in rad.items():
        t0 = min(t for _, t in evs)
        age = t0 - id2content[mid]
        hops = path.split(",")
        relayer = hops[-1]
        per_relayer[relayer].append(age)
        if len(hops) == 2:
            cls_age["own_gw" if relayer in own_calls else "foreign_gw"].append(age)
        else:
            cls_age["relay"].append(age)
    out["radiation_classes"] = {k: {"n": len(v), "p50": round(statistics.median(v), 1),
                                    "p90": round(pct(v, 0.9), 1), "gt150": sum(1 for x in v if x > 150)}
                                for k, v in cls_age.items()}
    rel = {}
    for r, v in per_relayer.items():
        s = sum(1 for x in v if x > 150)
        if s >= 5:
            rel[r] = {"n": len(v), "stale": s, "p50": round(statistics.median(v), 1), "max": round(max(v), 1)}
    out["stale_relayers"] = dict(sorted(rel.items(), key=lambda kv: -kv[1]["stale"]))
    # the same per relayer but from the node clock of the receiving node instead of the logger:
    # done separately via nodeclock offsets; reported above.
    # funnel in the window where all five log
    w0 = max(w[0] for w in windows.values())
    w1 = min(w[1] for w in windows.values())
    per_tel: dict[str, dict[str, int]] = defaultdict(lambda: Counter())
    for (mid, path), evs in rad.items():
        c = id2content[mid]
        if not (w0 + 60 <= c <= w1 - 1800):
            continue
        hops = path.split(",")
        k = ("own_gw" if hops[-1] in own_calls else "foreign_gw") if len(hops) == 2 else "relay"
        per_tel[mid][k] += 1
        per_tel[mid]["all"] += 1
    fun = {}
    for k in ("own_gw", "foreign_gw", "relay", "all"):
        v = [per_tel[m][k] for m in per_tel]
        fun[k] = {"min": min(v), "p50": statistics.median(v), "max": max(v)}
    out["funnel"] = {"window_utc": [datetime.fromtimestamp(w0, timezone.utc).isoformat(),
                                    datetime.fromtimestamp(w1, timezone.utc).isoformat()],
                     "telegrams": len(per_tel), **fun}
    # NEED lines for CET ids
    cet_ids = set(id2content)
    nd_out = {}
    for node, lst in need_cases.items():
        c = [x for x in lst if x[0] in cet_ids]
        cases = Counter((x[1], x[2]) for x in c)
        eq = sum(1 for x in c if x[3].split("/")[0] == x[3].split("/")[1])
        nd_out[node] = {"cet_need": len(c), "type_case": {f"{a}{b}": n for (a, b), n in cases.items()},
                        "need_eq_alone": eq}
    out["need_cet"] = nd_out
    out["cancel_refuse_cet"] = {n: dict(Counter(k for k, mid in lst if mid in cet_ids)) for n, lst in cancels.items()}
    out["cancel_refuse_all"] = {n: dict(Counter(k for k, _ in lst)) for n, lst in cancels.items()}
    print(json.dumps(out, indent=1, default=str))


if __name__ == "__main__":
    main()
