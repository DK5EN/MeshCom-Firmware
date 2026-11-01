#!/usr/bin/env python3
"""HB9 mesh (HB9VQQ-4 / HB9VQQ-5) F5 time-signal and F6 manipulation checks.

Usage:  python3 -I hb9_net_checks.py NORM_DIR SECTION [SECTION ...]
        NORM_DIR holds hb9vqq4.log and hb9vqq5.log (normalised meshlogger captures).
        SECTION: parse | clock | cet | cetrad | relay | rxerr | mutual | unheard | redundancy | dedup | idreuse |
                 stale | load | hops | misc | idtrace=<MSGID> | all   (all = every section but idtrace; ~1 min)
        Example: python3 -I hb9_net_checks.py logs/20261101-24h-log/.logreport-work/norm all > out_all.json

Reads the logs once per call (stream, stdlib only), prints JSON-ish text to stdout.
Independent of tools/logreport/*: every number in data/hb9/q5|q6/findings.md that
cites "check:<section>" comes from here.

Line model (see src/lora_functions.cpp, src/txring_functions.cpp):
  MH-LoRa   every decoded LoRa frame whose last path token is not me (printed before dedup)
  RX-LoRa2  frame accepted as new (not own-echo, dedup NEW)
  RX_DEDUP_NEW/DUP/ADD msg_id  dedup verdict (4-byte msg_id only)
  RING_WRITE/RING_PRIO/RING_TX_READ/RING_SUPERSEDE  TX pool events
  TX-LoRa   own frame at TX start (RADIO_TX), path ends with my call
  [CET];gw_rx|rx|tx   time-signal telegram events with age_s at the node
"""
from __future__ import annotations

import bisect
import calendar
import json
import re
import sys
import time
from collections import Counter, defaultdict
from typing import Any, Dict, Iterator, List, Optional, Tuple

OWN = {"hb9vqq4": "HB9VQQ-4", "hb9vqq5": "HB9VQQ-5"}
LOGGER_UTC_OFF_S = 3600          # logger (Mac) = CET = UTC+1 on 2026-10-31/11-01; checked in section clock
CET_GATE_S = 150                 # src/cet_age.h CET_MAX_AGE_S

TS = re.compile(r"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$")
RE_RXPKT = re.compile(r"\[LoRa\]\.\.\.Received packet: RSSI:\s*(-?\d+) dBm / SNR:\s*(-?\d+) dB")
RE_FRAME = re.compile(
    r"^(\d\d:\d\d:\d\d) (MH-LoRa|RX-LoRa2|TX-LoRa|TX-UDP|RX-UDP)\s+(\d+) (\S) x([0-9A-F]{8}) "
    r"H([0-9A-F]{2}) S(\d) T(\d) M(\d\d) (.*)$"
)
RE_DEDUP = re.compile(r"\[MC-DBG\] RX_DEDUP_(NEW|DUP|ADD) msg_id=([0-9A-F]{8})(?: srv=(\d+))?(?: slot=(\d+))?")
RE_RW = re.compile(
    r"\[MC-DBG\] RING_WRITE slot=(\d+) type=([0-9A-F]+) status=([0-9A-F]+) len=(\d+) msg_id=([0-9A-F]{8}) "
    r"queued=(\d+)/20 src=(\w+)"
)
RE_RTR = re.compile(
    r"\[MC-DBG\] RING_TX_READ slot=(\d+) prio=(\d) type=([0-9A-F]+) status=([0-9A-F]+) len=(\d+) "
    r"msg_id=([0-9A-F]{8}) retry=(\d+) queued=(\d+)/20 lat=(\d+)ms"
)
RE_SUP = re.compile(r"\[MC-DBG\] RING_SUPERSEDE(_KEEP)? (?:slot=(\d+) )?prio=(\d) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})")
RE_PRIO = re.compile(r"\[MC-DBG\] RING_PRIO slot=(\d+) prio=(\d)")
RE_CET = re.compile(r"^\[CET\];(gw_rx|rx|tx|drop)(?:;(.*))?$")
RE_CONTENT = re.compile(r"\{CET\}(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)")
RE_TC = re.compile(r"TimeClient now \(UTC\): (\d+):(\d+):(\d+)")


def epoch(y: str, mo: str, d: str, h: str, mi: str, s: str, ms: str = "0") -> float:
    return calendar.timegm((int(y), int(mo), int(d), int(h), int(mi), int(s))) + int(ms) / 1000.0


def toa(length: int, sf: int = 11, bw: float = 250e3, cr: int = 2, pre: int = 8) -> float:
    """LoRa time on air in s; EU8 profile SF11 BW250 CR4/6 preamble 8 (src/country_profile.cpp:90).
    Cross-check: sum over all TX = 2259 s / 1876 s vs node CHANNEL_UTIL tx= sum 2270.5 s / 1884.9 s (-0.5 %)."""
    import math
    tsym = 2 ** sf / bw
    de = 1 if tsym > 16e-3 else 0
    npay = 8 + max(math.ceil((8 * length - 4 * sf + 28 + 16) / (4 * (sf - 2 * de))) * (cr + 4), 0)
    return (pre + 4.25 + npay) * tsym


def pct(a: float, b: float) -> float:
    return round(100.0 * a / b, 2) if b else 0.0


def quant(v: List[float], qs=(0.0, 0.05, 0.5, 0.9, 0.99, 1.0)) -> Dict[str, Any]:
    if not v:
        return {"n": 0}
    s = sorted(v)
    out: Dict[str, Any] = {"n": len(s)}
    for q in qs:
        out["p%g" % (q * 100)] = round(s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))], 2)
    return out


def parse_frame_body(kind: str, ptype: str, rest: str) -> Dict[str, Any]:
    """rest = 'PATH>DEST<ptype>PAYLOAD HW:.. MOD:.. FCS:.... FW:..:. LH:..'"""
    m = re.match(r"^(.*?)>(.*)$", rest)
    if not m:
        return {"path": [], "dest": "", "payload": rest}
    path = m.group(1).split(",")
    tail = m.group(2)
    i = tail.find(ptype)
    dest = tail[:i] if i >= 0 else tail
    payload = tail[i + 1:] if i >= 0 else ""
    k = payload.rfind(" HW:")
    meta = payload[k + 1:] if k >= 0 else ""
    payload = payload[:k] if k >= 0 else payload
    d: Dict[str, Any] = {"path": path, "dest": dest, "payload": payload}
    mf = re.search(r"FCS:([0-9A-F]{4}) FW:([0-9A-F:#a-z]+)", meta)
    if mf:
        d["fcs"] = mf.group(1)
        d["fw"] = mf.group(2)
    mh = re.search(r"HW:(\d+)", meta)
    if mh:
        d["hw"] = int(mh.group(1))
    return d


class Node:
    def __init__(self, key: str):
        self.key = key
        self.own = OWN[key]
        self.rx: List[Dict[str, Any]] = []       # MH-LoRa frames
        self.new2: List[Dict[str, Any]] = []      # RX-LoRa2
        self.tx: List[Dict[str, Any]] = []        # TX-LoRa
        self.rw: List[Dict[str, Any]] = []        # RING_WRITE
        self.rtr: List[Dict[str, Any]] = []       # RING_TX_READ
        self.sup: List[Dict[str, Any]] = []       # RING_SUPERSEDE(_KEEP)
        self.cet: List[Dict[str, Any]] = []       # [CET];* lines
        self.dedup: List[Tuple[float, str, str, Optional[int]]] = []  # t, verdict, id, slot
        self.udp_up: List[Dict[str, Any]] = []    # TX-UDP lines (uploads to the server)
        self.udp_dn: List[Dict[str, Any]] = []    # RX-UDP lines (server -> node)
        self.tc: List[Tuple[float, float]] = []   # TimeClient (logger_t, node_utc_secs_of_day)
        self.counts: Counter = Counter()
        self.crc_err: List[Tuple[float, int, int]] = []   # t, rssi, snr
        self.rxerr_t: List[float] = []
        self.channel_util: List[Tuple[float, int, int, int]] = []
        self.logger_gaps: List[Tuple[float, str]] = []
        self.t0 = self.t1 = 0.0
        self.lines = 0
        self.cad_busy: List[float] = []
        self.csma_aged: List[float] = []
        self.ack_events: List[Tuple[float, str]] = []
        self.ack_frames: List[Dict[str, Any]] = []
        self.txdone: List[float] = []
        self.stat: List[Dict[str, Any]] = []
        self.qmax: List[Tuple[float, int]] = []
        self.drops: List[Tuple[float, List[int]]] = []


def load(path: str, key: str) -> Node:
    n = Node(key)
    ctx_rssi: Optional[int] = None
    ctx_snr: Optional[int] = None
    ctx_dedup: Dict[str, str] = {}
    ctx_t = 0.0
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m = TS.match(line.rstrip("\n"))
            if not m:
                continue
            n.lines += 1
            t = epoch(*m.groups()[:7])
            body = m.group(8)
            if n.t0 == 0.0:
                n.t0 = t
            n.t1 = t
            c0 = body[:1]
            if c0 == "[":
                if body.startswith("[LoRa]...Received packet"):
                    mm = RE_RXPKT.search(body)
                    if mm:
                        ctx_rssi, ctx_snr, ctx_t = int(mm.group(1)), int(mm.group(2)), t
                        ctx_dedup = {}
                    continue
                if body.startswith("[MC-DBG] RX_DEDUP_"):
                    mm = RE_DEDUP.search(body)
                    if mm:
                        v, mid, srv, slot = mm.groups()
                        n.dedup.append((t, v, mid, int(slot) if slot else None))
                        if v in ("NEW", "DUP"):
                            ctx_dedup[mid] = v
                    continue
                if body.startswith("[MC-DBG] RING_WRITE"):
                    mm = RE_RW.search(body)
                    if mm:
                        n.rw.append({"t": t, "slot": int(mm.group(1)), "type": mm.group(2), "status": mm.group(3),
                                     "len": int(mm.group(4)), "id": mm.group(5), "q": int(mm.group(6)), "src": mm.group(7),
                                     "prio": None})
                    continue
                if body.startswith("[MC-DBG] RING_PRIO"):
                    mm = RE_PRIO.search(body)
                    if mm and n.rw:
                        n.rw[-1]["prio"] = int(mm.group(2))
                    continue
                if body.startswith("[MC-DBG] RING_TX_READ"):
                    mm = RE_RTR.search(body)
                    if mm:
                        n.rtr.append({"t": t, "slot": int(mm.group(1)), "prio": int(mm.group(2)), "type": mm.group(3),
                                      "len": int(mm.group(5)), "id": mm.group(6), "retry": int(mm.group(7)),
                                      "q": int(mm.group(8)), "lat": int(mm.group(9)) / 1000.0})
                    continue
                if body.startswith("[MC-DBG] RING_SUPERSEDE"):
                    mm = RE_SUP.search(body)
                    if mm:
                        n.sup.append({"t": t, "keep": bool(mm.group(1)), "prio": int(mm.group(3)),
                                      "old": mm.group(4), "new": mm.group(5)})
                    continue
                if body.startswith("[CET];"):
                    mm = RE_CET.match(body)
                    if mm:
                        f = (mm.group(2) or "").split(";")
                        kv = {f[i]: f[i + 1] for i in range(0, len(f) - 1, 2)}
                        kv["kind"] = mm.group(1)
                        kv["t"] = t
                        n.cet.append(kv)
                    continue
                if body.startswith("[MC-DBG] CHANNEL_UTIL"):
                    mm = re.search(r"rx=(\d+)ms tx=(\d+)ms util=(\d+)%", body)
                    if mm:
                        n.channel_util.append((t, int(mm.group(1)), int(mm.group(2)), int(mm.group(3))))
                    continue
                if body.startswith("[MC-DBG] TX_DONE"):
                    n.txdone.append(t)
                    continue
                if body.startswith("[MC-STAT]"):
                    mm = re.search(r"qmax=(\d+)/20", body)
                    if mm:
                        n.qmax.append((t, int(mm.group(1))))
                    continue
                if body.startswith("[MC-DBG] CRC_ERROR"):
                    mm = re.search(r"rssi=(-?\d+) snr=(-?\d+)", body)
                    if mm:
                        n.crc_err.append((t, int(mm.group(1)), int(mm.group(2))))
                    continue
                if body.startswith("[MC-DBG] CAD_BUSY "):
                    n.cad_busy.append(t)
                    continue
                if body.startswith("[MC-DBG] CSMA_AGED"):
                    n.csma_aged.append(t)
                    continue
                if body.startswith("[MC-DBG] ACK_"):
                    n.ack_events.append((t, body[len("[MC-DBG] "):]))
                    continue
                if body.startswith("[LOGGER]") and "connection lost" in body:
                    n.logger_gaps.append((t, body))
                    continue
                continue
            if body.startswith(" drop: p1="):
                mm = re.findall(r"p(\d)=(\d+)", body)
                n.drops.append((t, [int(v) for _k, v in mm]))
                continue
            if body.startswith("TimeClient now"):
                mm = RE_TC.search(body)
                if mm:
                    n.tc.append((t, int(mm.group(1)) * 3600 + int(mm.group(2)) * 60 + int(mm.group(3))))
                continue
            mm = RE_FRAME.match(body)
            if mm:
                hms, kind, ln, ptype, mid, hh, ss, tt, mmode, rest = mm.groups()
                d = parse_frame_body(kind, ptype, rest)
                d.update({"t": t, "hms": hms, "len": int(ln), "ptype": ptype, "id": mid, "H": int(hh, 16),
                          "S": int(ss), "mode": int(mmode)})
                if kind == "MH-LoRa":
                    d["rssi"], d["snr"] = ctx_rssi, ctx_snr
                    d["dedup"] = ctx_dedup.get(mid)
                    n.rx.append(d)
                elif kind == "RX-LoRa2":
                    d["rssi"], d["snr"] = ctx_rssi, ctx_snr
                    n.new2.append(d)
                elif kind == "TX-LoRa":
                    n.tx.append(d)
                elif kind == "TX-UDP":
                    n.udp_up.append(d)
                elif kind == "RX-UDP":
                    n.udp_dn.append(d)
                continue
            if "[LOG] 0" in body and " A x" in body:
                mm = re.search(r"(\d\d:\d\d:\d\d) \[LOG\] 0(07|12) A x([0-9A-F]{8}) H([0-9A-F]{2}) (.*?) RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w)", body)
                if mm:
                    n.ack_frames.append({"t": t, "id": mm.group(3), "H": int(mm.group(4), 16), "rest": mm.group(5),
                                         "rssi": int(mm.group(6)), "snr": int(mm.group(7)), "dup": mm.group(8)})
    return n


def content_epoch(payload: str) -> Optional[float]:
    m = RE_CONTENT.search(payload)
    if not m:
        return None
    return epoch(*m.groups())


def src_of(f: Dict[str, Any]) -> str:
    return f["path"][0] if f.get("path") else ""


def last_of(f: Dict[str, Any]) -> str:
    return f["path"][-1] if f.get("path") else ""


REGISTRY: Dict[str, Any] = {}


# --------------------------------------------------------------------------------------------
def sec_parse(nodes: Dict[str, Node]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        out[k] = {
            "lines": n.lines, "span_h": round((n.t1 - n.t0) / 3600, 2), "mh_lora": len(n.rx), "rx_lora2": len(n.new2),
            "tx_lora": len(n.tx), "ring_write": len(n.rw), "ring_tx_read": len(n.rtr), "supersede": len(n.sup),
            "dedup_new": sum(1 for d in n.dedup if d[1] == "NEW"), "dedup_dup": sum(1 for d in n.dedup if d[1] == "DUP"),
            "crc_error": len(n.crc_err), "tx_udp_uploads": len(n.udp_up), "rx_udp_downloads": len(n.udp_dn),
            "cet_lines": dict(Counter(c["kind"] for c in n.cet)),
            "console_drops": len(n.logger_gaps),
            "ring_write_by_src": dict(Counter(w["src"] for w in n.rw)),
        }
    return out


def sec_clock(nodes: Dict[str, Node]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        offs = []
        for t, sod in n.tc:
            utc_sod = (t - LOGGER_UTC_OFF_S) % 86400
            d = (utc_sod - sod)
            if d < -43200:
                d += 86400
            if d > 43200:
                d -= 86400
            offs.append(d)
        # node [LOG]/MH-LoRa prefix hh:mm:ss vs logger clock (logger - node_local), local = logger tz
        dd = []
        for f in n.rx[::7]:
            h, mi, s = map(int, f["hms"].split(":"))
            sod_log = (f["t"] % 86400)
            d = sod_log - (h * 3600 + mi * 60 + s)
            if d < -43200:
                d += 86400
            if d > 43200:
                d -= 86400
            dd.append(d)
        out[k] = {"timeclient_logger_minus_node_utc_s": quant(offs), "mh_prefix_vs_logger_s": quant(dd),
                  "note": "node 'now (UTC)' lines carry floor seconds: true offset is value-0.5 +- 0.5"}
    return out


def telegram_table(nodes: Dict[str, Node]) -> Dict[str, Dict[str, Any]]:
    """Per node: msg_id -> {'content': epoch, 'src': server ssid, ...} for {CET} frames."""
    return {}


def cet_frames(n: Node, rx: bool = True) -> Iterator[Dict[str, Any]]:
    for f in (n.rx if rx else n.tx):
        if f["ptype"] == ":" and f["payload"].startswith("{CET}"):
            yield f


def sec_cet(nodes: Dict[str, Node]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    allcontent: Dict[str, Dict[str, float]] = {}      # server -> content epoch -> msg_id (union over both nodes)
    for k, n in nodes.items():
        r: Dict[str, Any] = {}
        gw = [c for c in n.cet if c["kind"] == "gw_rx"]
        rxl = [c for c in n.cet if c["kind"] == "rx"]
        txl = [c for c in n.cet if c["kind"] == "tx"]
        r["cet_line_counts"] = {"gw_rx": len(gw), "rx": len(rxl), "tx": len(txl),
                                "drop": sum(1 for c in n.cet if c["kind"] == "drop")}
        r["gw_rx_src"] = dict(Counter(c.get("src") for c in gw))
        r["rx_src"] = dict(Counter(c.get("src") for c in rxl))
        r["gw_rx_age_s"] = dict(sorted(Counter(int(c["age_s"]) for c in gw).items()))
        r["rx_age_s"] = quant([float(c["age_s"]) for c in rxl])
        r["rx_age_gt150"] = sum(1 for c in rxl if int(c["age_s"]) > CET_GATE_S)
        r["rx_lines_trusted"] = dict(Counter(c.get("trusted") for c in rxl))
        # own TX of {CET}
        r["tx_age_s"] = quant([float(c["age_s"]) for c in txl])
        r["tx_age_gt150"] = sum(1 for c in txl if int(c["age_s"]) > CET_GATE_S)
        r["tx_wait_ms"] = quant([float(c["wait_ms"]) for c in txl])
        r["tx_prio"] = dict(Counter(c.get("prio") for c in txl))
        # UDP downloads of {CET}
        udp_cet = [f for f in n.udp_dn if f["payload"].startswith("{CET}")]
        r["rx_udp_cet"] = len(udp_cet)
        r["rx_udp_cet_src"] = dict(Counter(src_of(f) for f in udp_cet))
        # cadence of the gw_rx (server push) per server
        by_src: Dict[str, List[float]] = defaultdict(list)
        for f in udp_cet:
            by_src[src_of(f)].append(f["t"])
        r["udp_cadence_s"] = {s: quant([b - a for a, b in zip(v, v[1:])]) for s, v in by_src.items()}
        # RF {CET}: frames by server ssid, by last hop; copy statistics
        rf = list(cet_frames(n))
        r["rf_frames"] = len(rf)
        r["rf_by_server"] = dict(Counter(src_of(f) for f in rf))
        ids = defaultdict(list)
        for f in rf:
            ids[f["id"]].append(f)
        r["rf_distinct_ids"] = len(ids)
        copies = [len(v) for v in ids.values()]
        r["rf_copies_per_id"] = quant([float(c) for c in copies])
        # age on air of every heard RF copy from content time vs logger utc
        ages = []
        by_last: Dict[str, List[float]] = defaultdict(list)
        for f in rf:
            ce = content_epoch(f["payload"])
            if ce is None:
                continue
            a = (f["t"] - LOGGER_UTC_OFF_S) - ce
            ages.append(a)
            by_last[last_of(f)].append(a)
        r["rf_age_s"] = quant(ages)
        r["rf_age_gt150_n"] = sum(1 for a in ages if a > CET_GATE_S)
        r["rf_age_by_lasthop_gt150"] = {c: [sum(1 for a in v if a > CET_GATE_S), len(v)]
                                         for c, v in by_last.items() if any(a > CET_GATE_S for a in v)}
        r["rf_age_neg"] = sum(1 for a in ages if a < -1)
        # stale: first-heard vs duplicate
        stale_new = [f for f in rf if f.get("dedup") == "NEW" and ((f["t"] - LOGGER_UTC_OFF_S) - (content_epoch(f["payload"]) or 0)) > CET_GATE_S]
        r["stale_copies_that_were_dedup_NEW"] = len(stale_new)
        # own TX of {CET} frames (TX-LoRa)
        txf = list(cet_frames(n, rx=False))
        r["tx_lora_cet"] = len(txf)
        r["tx_by_server"] = dict(Counter(src_of(f) for f in txf))
        # per-hour tx
        # collect content table
        for f in rf + udp_cet:
            ce = content_epoch(f["payload"])
            if ce is not None:
                allcontent.setdefault(src_of(f), {})[f["id"]] = ce
        out[k] = r
    # cadence per server over the union of both nodes (content time)
    cad: Dict[str, Any] = {}
    for s, d in allcontent.items():
        cts = sorted(set(d.values()))
        gaps = [b - a for a, b in zip(cts, cts[1:])]
        cad[s] = {"telegrams": len(cts), "gap_s": quant(gaps), "first": cts[0] if cts else None, "last": cts[-1] if cts else None}
    out["_union_by_server"] = cad
    return out


def relay_ledger(n: Node) -> List[Dict[str, Any]]:
    """One record per RING_WRITE; fate by msg_id: sent (RING_TX_READ after write), superseded, unaccounted."""
    reads: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
    for r in n.rtr:
        reads[r["id"]].append(r)
    sup_old: Dict[str, List[float]] = defaultdict(list)
    for s_ in n.sup:
        if not s_["keep"]:
            sup_old[s_["old"]].append(s_["t"])
    heard: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
    for f in n.rx:
        heard[f["id"]].append(f)
    led = []
    for w in n.rw:
        rd = [r for r in reads.get(w["id"], []) if w["t"] - 0.001 <= r["t"] <= w["t"] + 400.0 and r["slot"] == w["slot"]]
        first = rd[0] if rd else None
        fate = "sent" if first else None
        if fate is None:
            sp = [t for t in sup_old.get(w["id"], []) if w["t"] <= t <= w["t"] + 400.0]
            fate = "superseded" if sp else "unaccounted"
        end = first["t"] if first else (min(sp) if fate == "superseded" else None)
        # foreign copies heard while queued: same id, MH-LoRa after the write (before end or +60 s for unsent)
        lim = end if end is not None else w["t"] + 60.0
        hs = [f for f in heard.get(w["id"], []) if w["t"] + 0.002 < f["t"] <= lim and last_of(f) != n.own
              and len(f["path"]) >= 2]
        led.append({"w": w, "fate": fate, "tx": first, "foreign_while_queued": len(hs), "heard": hs,
                    "wait": (first["t"] - w["t"]) if first else None})
    return led


def sec_relay(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Relay fate ledger. 'unaccounted' = ring write never read for TX within 400 s and not superseded:
    freed silently (no marker without --nbrdebug). Test: share with a foreign copy heard while queued."""
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        led = relay_ledger(n)
        r: Dict[str, Any] = {}
        by_src: Dict[str, Counter] = defaultdict(Counter)
        for e in led:
            by_src[e["w"]["src"]][e["fate"]] += 1
        r["fate_by_src"] = {s: dict(c) for s, c in by_src.items()}
        rel = [e for e in led if e["w"]["src"] == "rx_relay"]
        c = Counter((e["fate"], e["foreign_while_queued"] > 0) for e in rel)
        r["relay_fate_x_foreign_copy_while_queued"] = {"%s|foreign=%s" % k_: v for k_, v in c.items()}
        gaps = [g[0] for g in n.logger_gaps]
        un = [e for e in rel if e["fate"] == "unaccounted"]
        r["unaccounted_near_console_gap"] = sum(1 for e in un if any(e["w"]["t"] - 1 <= g <= e["w"]["t"] + 60 for g in gaps))
        r["unaccounted_by_type"] = dict(Counter(TXTYPE.get(e["w"]["type"]) for e in un))
        nrel, nun = len(rel), len(un)
        r["relay_writes"] = nrel
        r["unaccounted_pct_of_relay_writes"] = pct(nun, nrel)
        pf = pct(sum(1 for e in rel if e["fate"] == "sent" and e["foreign_while_queued"]), sum(1 for e in rel if e["fate"] == "sent"))
        r["sent_with_foreign_copy_pct"] = pf
        hr: Dict[int, List[int]] = defaultdict(lambda: [0, 0, 0])
        for e in rel:
            i = int((e["w"]["t"] - n.t0) // 3600)
            hr[i][0] += 1
            if e["fate"] == "unaccounted":
                hr[i][1] += 1
            if e["fate"] == "sent" and e["foreign_while_queued"]:
                hr[i][2] += 1
        r["hourly_[writes,unaccounted,sent_with_foreign]"] = [hr[i] for i in range(24)]
        trig, sent_c = Counter(), Counter()
        for e in rel:
            if e["heard"]:
                (trig if e["fate"] == "unaccounted" else sent_c)[e["heard"][0]["path"][-1]] += 1
        r["first_foreign_copy_sender_cancelled"] = dict(trig.most_common(6))
        r["first_foreign_copy_sender_sent_anyway"] = dict(sent_c.most_common(6))
        r["cancel_rate_by_first_copy_sender"] = {c: [trig[c], trig[c] + sent_c[c], pct(trig[c], trig[c] + sent_c[c])]
                                                  for c in sorted(set(trig) | set(sent_c)) if trig[c] + sent_c[c] >= 30}
        r["unaccounted_airtime_s_model"] = round(sum(toa(e["w"]["len"]) for e in un), 0)
        r["sent_airtime_s_model_relays"] = round(sum(toa(e["w"]["len"]) for e in rel if e["fate"] == "sent"), 0)
        r["sent_wait_s"] = quant([e["wait"] for e in rel if e["fate"] == "sent"])
        r["unaccounted_first_copy_after_write_s"] = quant([e["heard"][0]["t"] - e["w"]["t"] for e in un if e["heard"]])
        out[k] = r
    return out


REGISTRY["relay"] = sec_relay


def sec_rxerr(nodes: Dict[str, Node]) -> Dict[str, Any]:
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        dec = len(n.rx) + 0
        err = len(n.crc_err)
        out[k] = {"crc_error": err, "decoded_mh_lora": dec, "err_share_pct": pct(err, err + dec),
                  "crc_rssi": quant([float(x[1]) for x in n.crc_err]), "crc_snr": quant([float(x[2]) for x in n.crc_err]),
                  "rssi_decoded_last_hop": quant([float(f["rssi"]) for f in n.rx if f["rssi"] is not None and len(f["path"]) == 1]),
                  "note": "MH-LoRa is not printed for frames whose last hop is this node and for 12-byte ACK frames; share is indicative"}
    return out


REGISTRY["rxerr"] = sec_rxerr


def other(n: Node) -> str:
    return "HB9VQQ-5" if n.key == "hb9vqq4" else "HB9VQQ-4"


def sec_mutual(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Direct audibility: frames whose LAST path token is the other node (the other node transmitted it)."""
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        oc = other(n)
        d = [f for f in n.rx if last_of(f) == oc]
        r: Dict[str, Any] = {"heard_from_other_n": len(d)}
        r["heard_from_other_rssi"] = quant([float(f["rssi"]) for f in d if f["rssi"] is not None])
        r["heard_from_other_snr"] = quant([float(f["snr"]) for f in d if f["snr"] is not None])
        r["heard_from_other_by_type"] = dict(Counter(f["ptype"] for f in d))
        r["heard_from_other_cet"] = sum(1 for f in d if f["payload"].startswith("{CET}"))
        # per hour
        hh = Counter(int((f["t"] - n.t0) // 3600) for f in d)
        r["per_hour"] = [hh.get(i, 0) for i in range(24)]
        # any frame with the other call anywhere in the path (heard via third node) 
        r["other_call_anywhere_in_path"] = sum(1 for f in n.rx if oc in f["path"][:-1])
        # vs. the other node's TX count
        out[k] = r
    # reciprocal: of the other's TX-LoRa ids, how many heard at this node within 2 s
    for k, n in nodes.items():
        m = nodes["hb9vqq5" if k == "hb9vqq4" else "hb9vqq4"]
        idx: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
        for f in n.rx:
            if last_of(f) == m.own:
                idx[f["id"]].append(f)
        tx_ids = [f for f in m.tx]
        hit = 0
        for f in tx_ids:
            if any(abs(g["t"] - f["t"]) < 3.0 for g in idx.get(f["id"], [])):
                hit += 1
        out[k]["other_tx_lora_n"] = len(tx_ids)
        out[k]["other_tx_heard_here_within_3s"] = hit
        out[k]["other_tx_heard_pct"] = pct(hit, len(tx_ids))
        # same, excluding the other's gateway injects (fine either way)
        # by-type conditional hit
        by_t: Dict[str, List[int]] = defaultdict(lambda: [0, 0])
        for f in tx_ids:
            by_t[f["ptype"]][1] += 1
            if any(abs(g["t"] - f["t"]) < 3.0 for g in idx.get(f["id"], [])):
                by_t[f["ptype"]][0] += 1
        out[k]["other_tx_heard_by_type"] = {t: v for t, v in by_t.items()}
    return out


REGISTRY["mutual"] = sec_mutual


def sec_unheard(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Why is a TX of node A not decoded at node B (both directions)? classify by B's own activity."""
    import bisect
    out: Dict[str, Any] = {}
    for k, b in nodes.items():
        a = nodes["hb9vqq5" if k == "hb9vqq4" else "hb9vqq4"]
        idx: Dict[str, List[float]] = defaultdict(list)
        for f in b.rx:
            if last_of(f) == a.own:
                idx[f["id"]].append(f["t"])
        btx = sorted(f["t"] for f in b.tx)
        crc = sorted(t for t, _r, _s in b.crc_err)
        heardn = unheard = 0
        cls = Counter()
        gaps = sorted(t for t, _ in [(g[0], 0) for g in a.logger_gaps] + [(g[0], 0) for g in b.logger_gaps])
        rssi_crc = []
        for f in a.tx:
            if any(-0.5 < x - f["t"] < 3.5 for x in idx.get(f["id"], [])):
                heardn += 1
                continue
            unheard += 1
            # logger gap within +-6 s?
            if any(-6 < g - f["t"] < 1 for g in gaps):
                cls["logger_gap_near"] += 1
                continue
            i = bisect.bisect_left(btx, f["t"] - 1.5)
            if i < len(btx) and btx[i] < f["t"] + 1.2:
                cls["b_transmitting_overlap"] += 1
                continue
            j = bisect.bisect_left(crc, f["t"] - 0.5)
            if j < len(crc) and crc[j] < f["t"] + 3.0:
                cls["b_crc_error_in_window"] += 1
                continue
            cls["silent"] += 1
        out[k + "_hears_" + a.key] = {"a_tx": len(a.tx), "heard": heardn, "unheard": unheard, "classes": dict(cls)}
    return out


REGISTRY["unheard"] = sec_unheard


TXTYPE = {"40": "@", "21": "!", "3A": ":", "41": "A"}


def tx_records(n: Node) -> List[Dict[str, Any]]:
    """RING_TX_READ joined with the preceding RING_WRITE of the same id (src class) and TX-LoRa line."""
    last_w: Dict[str, Dict[str, Any]] = {}
    ws = sorted(n.rw, key=lambda w: w["t"])
    wi = 0
    out = []
    for r in n.rtr:
        while wi < len(ws) and ws[wi]["t"] <= r["t"] + 0.001:
            last_w[ws[wi]["id"]] = ws[wi]
            wi += 1
        w = last_w.get(r["id"])
        cls = "?"
        if w:
            sname = w["src"]
            cls = {"rx_relay": "relay", "udp_rx": "gateway", "auto_pos": "own", "user_wx": "own", "user_msg": "own"}.get(sname, "ack")
        air = toa(r["len"])
        out.append({"t": r["t"], "id": r["id"], "type": TXTYPE.get(r["type"], r["type"]), "prio": r["prio"], "cls": cls, "air": air,
                    "lat": r["lat"], "q": r["q"], "retry": r["retry"], "w_t": w["t"] if w else None})
    return out


def sec_redundancy(nodes: Dict[str, Node]) -> Dict[str, Any]:
    n4, n5 = nodes["hb9vqq4"], nodes["hb9vqq5"]
    t4, t5 = tx_records(n4), tx_records(n5)
    out: Dict[str, Any] = {}
    out["tx_total"] = {"hb9vqq4": len(t4), "hb9vqq5": len(t5)}
    out["tx_by_class"] = {"hb9vqq4": dict(Counter((r["cls"], r["type"]) .__str__() for r in t4)),
                          "hb9vqq5": dict(Counter((r["cls"], r["type"]).__str__() for r in t5))}
    i4: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
    i5: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
    for r in t4:
        i4[r["id"]].append(r)
    for r in t5:
        i5[r["id"]].append(r)
    common = [m for m in i4 if m in i5]
    # restrict to pairs within 30 min (ids are unique ~4 h)
    pairs = []
    for m in common:
        for a in i4[m]:
            near = [b for b in i5[m] if abs(b["t"] - a["t"]) <= 1800]
            for b in near:
                pairs.append((m, a, b))
    out["ids_tx_by_both"] = len({m for m, _a, _b in pairs})
    out["pairs"] = len(pairs)
    heard4 = defaultdict(list)   # at node 4: frames with last hop == -5
    for f in n4.rx:
        if last_of(f) == n5.own:
            heard4[f["id"]].append(f["t"])
    heard5 = defaultdict(list)
    for f in n5.rx:
        if last_of(f) == n4.own:
            heard5[f["id"]].append(f["t"])
    rows = []
    cet_ids = {f["id"] for n_ in (n4, n5) for f in n_.rx + n_.tx if f["payload"].startswith("{CET}")}
    for m, a, b in pairs:
        first = "4" if a["t"] <= b["t"] else "5"
        if first == "4":
            # did 5 hear 4's copy before 5's own TX?
            h = [x for x in heard5.get(m, []) if a["t"] - 0.5 <= x < b["t"]]
        else:
            h = [x for x in heard4.get(m, []) if b["t"] - 0.5 <= x < a["t"]]
        cls = (a["cls"], b["cls"])
        sec_air = (b["air"] if first == "4" else a["air"])
        rows.append({"id": m, "cet": m in cet_ids, "type": a["type"], "second_air": sec_air, "cls4": a["cls"], "cls5": b["cls"], "first": first,
                     "gap": abs(a["t"] - b["t"]), "second_heard_first_before_tx": bool(h)})
    out["pair_classes"] = dict(Counter("%s/%s" % (r["cls4"], r["cls5"]) for r in rows))
    out["pair_types"] = dict(Counter(r["type"] for r in rows))
    out["pair_first"] = dict(Counter(r["first"] for r in rows))
    out["pair_gap_s"] = quant([r["gap"] for r in rows])
    out["pair_second_heard_first_before_tx"] = sum(1 for r in rows if r["second_heard_first_before_tx"])
    byc: Dict[str, List[int]] = defaultdict(lambda: [0, 0])
    for r in rows:
        key = "%s/%s" % (r["cls4"], r["cls5"])
        byc[key][1] += 1
        if r["second_heard_first_before_tx"]:
            byc[key][0] += 1
    out["pair_heard_before_by_class"] = dict(byc)
    cp = [r for r in rows if r["cet"]]
    out["cet_pairs"] = {"n": len(cp), "classes": dict(Counter("%s/%s" % (r["cls4"], r["cls5"]) for r in cp)),
                        "second_heard_first": sum(1 for r in cp if r["second_heard_first_before_tx"]),
                        "gap_s": quant([r["gap"] for r in cp]),
                        "second_air_s": round(sum(r["second_air"] or 0 for r in cp), 1)}
    ncp = [r for r in rows if not r["cet"]]
    for key_ in ("relay/relay", "gateway/gateway"):
        sub = [r for r in ncp if "%s/%s" % (r["cls4"], r["cls5"]) == key_]
        out["noncet_" + key_.replace("/", "_")] = {"n": len(sub), "second_heard_first": sum(1 for r in sub if r["second_heard_first_before_tx"]),
                                                     "by_type": dict(Counter(r["type"] for r in sub)),
                                                     "second_air_s": round(sum(r["second_air"] or 0 for r in sub), 1),
                                                     "second_air_s_heard_first": round(sum(r["second_air"] or 0 for r in sub if r["second_heard_first_before_tx"]), 1)}
    out["second_tx_airtime_s_total"] = round(sum(r["second_air"] or 0 for r in rows), 1)
    out["second_tx_airtime_s_when_heard_first"] = round(sum(r["second_air"] or 0 for r in rows if r["second_heard_first_before_tx"]), 1)
    out["all_tx_airtime_s"] = {"hb9vqq4": round(sum(r["air"] or 0 for r in t4), 1), "hb9vqq5": round(sum(r["air"] or 0 for r in t5), 1)}
    out["pair_gap_le5s"] = sum(1 for r in rows if r["gap"] <= 5)
    # airtime estimate: measured airtime: TX_START->TX_DONE is not parsed here; use 0.32 s p50 median per frame
    # frames per node transmitted = total ids; unique ids on air from the pair
    allids = set(i4) | set(i5)
    out["distinct_ids_tx"] = len(allids)
    out["ids_tx_only4"] = len([m for m in i4 if m not in i5])
    out["ids_tx_only5"] = len([m for m in i5 if m not in i4])
    # gateway-class pairs: CET injects
    cet4 = {r["id"] for r in t4 if r["cls"] == "gateway"}
    cet5 = {r["id"] for r in t5 if r["cls"] == "gateway"}
    out["gateway_ids"] = {"hb9vqq4": len(cet4), "hb9vqq5": len(cet5), "both": len(cet4 & cet5), "union": len(cet4 | cet5)}
    gp = [r for r in rows if r["cls4"] == "gateway" and r["cls5"] == "gateway"]
    out["gateway_pairs"] = len(gp)
    out["gateway_pair_first"] = dict(Counter(r["first"] for r in gp))
    out["gateway_pair_gap_s"] = quant([r["gap"] for r in gp])
    out["gateway_pair_second_heard_first"] = sum(1 for r in gp if r["second_heard_first_before_tx"])
    return out


REGISTRY["redundancy"] = sec_redundancy


def sec_cetrad(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Radiations per {CET} telegram in the HB9 mesh as visible from the two logs (lower bound)."""
    n4, n5 = nodes["hb9vqq4"], nodes["hb9vqq5"]
    out: Dict[str, Any] = {}
    # one record per (id, transmitter); first sighting time (merge both logs; dedupe window 30 s)
    rad: Dict[str, Dict[str, Dict[str, Any]]] = defaultdict(dict)
    ctime: Dict[str, float] = {}
    srv: Dict[str, str] = {}
    for n in (n4, n5):
        for f in n.rx:
            if f["ptype"] != ":" or not f["payload"].startswith("{CET}"):
                continue
            ce = content_epoch(f["payload"])
            tx = last_of(f)
            if ce is None:
                continue
            key = f["id"]
            ctime.setdefault(key, ce)
            srv.setdefault(key, src_of(f))
            r = rad[key].get(tx)
            age = (f["t"] - LOGGER_UTC_OFF_S) - ce
            if r is None:
                rad[key][tx] = {"t": f["t"], "age": age, "len": f["len"], "pathlen": len(f["path"]), "H": f["H"]}
        for f in n.tx:
            if f["ptype"] != ":" or not f["payload"].startswith("{CET}"):
                continue
            ce = content_epoch(f["payload"])
            key = f["id"]
            ctime.setdefault(key, ce or 0)
            srv.setdefault(key, src_of(f))
            rad[key].setdefault(n.own, {"t": f["t"], "age": (f["t"] - LOGGER_UTC_OFF_S) - (ce or 0), "len": f["len"],
                                        "pathlen": len(f["path"]), "H": f["H"]})
    per: List[Dict[str, Any]] = []
    for key, d in rad.items():
        gws = [c for c, r in d.items() if r["pathlen"] == 2]
        rel = [c for c, r in d.items() if r["pathlen"] > 2]
        per.append({"id": key, "server": srv[key], "radiations": len(d), "gateways": len(gws), "relays": len(rel),
                    "air": sum(toa(r["len"]) for r in d.values()), "stale": sum(1 for r in d.values() if r["age"] > CET_GATE_S),
                    "gw_calls": gws, "rel_calls": rel, "ce": ctime[key]})
    for sv in ("OE1XAR-33", "OE1XAR-63", "OE1XAR-28"):
        sub = [p for p in per if p["server"] == sv]
        if not sub:
            continue
        gwc = Counter(c for p in sub for c in p["gw_calls"])
        rlc = Counter(c for p in sub for c in p["rel_calls"])
        out[sv] = {
            "telegrams": len(sub),
            "radiations_per_telegram": quant([float(p["radiations"]) for p in sub]),
            "gateways_per_telegram": quant([float(p["gateways"]) for p in sub]),
            "relays_per_telegram": quant([float(p["relays"]) for p in sub]),
            "airtime_s_per_telegram": quant([p["air"] for p in sub]),
            "airtime_s_total": round(sum(p["air"] for p in sub), 1),
            "stale_radiations": sum(p["stale"] for p in sub),
            "gateway_presence_pct": {c: pct(v, len(sub)) for c, v in gwc.most_common(12)},
            "relay_presence_pct": {c: pct(v, len(sub)) for c, v in rlc.most_common(12)},
        }
    # who radiates first: -4 vs -5 for -33 telegrams where both are present
    first = Counter()
    gap = []
    for key, d in rad.items():
        if srv[key] == "OE1XAR-33" and "HB9VQQ-4" in d and "HB9VQQ-5" in d:
            a, b = d["HB9VQQ-4"]["t"], d["HB9VQQ-5"]["t"]
            first["4" if a < b else "5"] += 1
            gap.append(abs(a - b))
    out["_both_nodes_radiate_33"] = {"telegrams": sum(first.values()), "first": dict(first), "gap_s": quant(gap)}
    # RF-listener view: first radiation of each telegram (any server) -> gaps between consecutive telegrams on air
    firsts = sorted(min(r["t"] for r in d.values()) for d in rad.values())
    # restrict to the common capture window (both logs running)
    lo = max(n4.t0, n5.t0) + 60
    hi = min(n4.t1, n5.t1) - 60
    fw = [t for t in firsts if lo <= t <= hi]
    out["_on_air_first_radiation_gap_s"] = quant([b - a for a, b in zip(fw, fw[1:])])
    out["_on_air_first_radiation_gap_gt600"] = sum(1 for a, b in zip(fw, fw[1:]) if b - a > 600)
    out["_on_air_window_telegrams"] = len(fw)
    # UDP push arrival difference between the two gateways, same telegram id
    ua = {c["ms"]: c for c in []}
    d4 = {f["id"]: f["t"] for f in n4.udp_dn if f["payload"].startswith("{CET}")}
    d5 = {f["id"]: f["t"] for f in n5.udp_dn if f["payload"].startswith("{CET}")}
    both_ = [i for i in d4 if i in d5]
    out["_udp_push_arrival_4_minus_5_s"] = quant([d4[i] - d5[i] for i in both_])
    out["_udp_push_telegrams_both"] = len(both_)
    out["_udp_push_only_one"] = {"only4": len([i for i in d4 if i not in d5]), "only5": len([i for i in d5 if i not in d4])}
    out["_all_telegrams_total_radiations_known"] = sum(p["radiations"] for p in per)
    out["_all_telegram_airtime_s"] = round(sum(p["air"] for p in per), 1)
    out["_distinct_telegrams"] = len(per)
    # share of node TX that is {CET}
    for n in (n4, n5):
        recs = tx_records(n)
        ct = [r for r in recs if r["type"] == ":"]
        cets = {f["id"] for f in n.tx if f["payload"].startswith("{CET}")}
        cet_tx = [r for r in recs if r["id"] in cets]
        out["share_" + n.key] = {"tx_total": len(recs), "cet_tx": len(cet_tx), "cet_tx_pct": pct(len(cet_tx), len(recs)),
                                 "cet_air_s": round(sum(r["air"] for r in cet_tx), 1),
                                 "all_air_s": round(sum(r["air"] for r in recs), 1),
                                 "cet_air_pct": pct(sum(r["air"] for r in cet_tx), sum(r["air"] for r in recs)),
                                 "cet_tx_by_class": dict(Counter(r["cls"] for r in cet_tx))}
    # supersede details
    idsrc = {}
    for n in (n4, n5):
        for f in n.rx + n.tx:
            idsrc.setdefault(f["id"], f)
    for n in (n4, n5):
        det = Counter()
        dts = []
        for sp in n.sup:
            if sp["prio"] != 3:
                continue
            o, w = idsrc.get(sp["old"]), idsrc.get(sp["new"])
            if not o or not w:
                det["unmapped"] += 1
                continue
            det[("keep" if sp["keep"] else "sup", src_of(o), src_of(w))] += 1
            co, cw = content_epoch(o["payload"]), content_epoch(w["payload"])
            if co and cw:
                dts.append(cw - co)
        out["cet_supersede_" + n.key] = {"n_prio3": sum(det.values()), "by": {str(k): v for k, v in det.items()},
                                         "new_minus_old_content_s": quant(dts)}
    return out


REGISTRY["cetrad"] = sec_cetrad


def sec_dedup(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Dedup ring (100 slots, src/dedup_functions.cpp): window length, re-accepted ids, re-relays."""
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        adds = [(t, mid, slot) for t, v, mid, slot in n.dedup if v == "ADD"]
        # window: time for 100 consecutive ADDs (one full ring turn)
        win = [adds[i + 100][0] - adds[i][0] for i in range(0, len(adds) - 100, 5)]
        r: Dict[str, Any] = {"adds": len(adds), "ring_turn_min": quant([w / 60 for w in win])}
        seen: Dict[str, float] = {}
        re_acc = []
        for t, v, mid, slot in n.dedup:
            if v == "ADD":
                if mid in seen:
                    re_acc.append((t, mid, t - seen[mid]))
                seen[mid] = t
        r["ids_added_again"] = len(re_acc)
        r["re_add_gap_min"] = quant([g / 60 for _t, _m, g in re_acc if g < 4 * 3600])
        # classify re-added ids: what are they
        byid = {f["id"]: f for f in n.rx}
        cls = Counter()
        for t, mid, g in re_acc:
            f = byid.get(mid)
            if f is None:
                cls["no_frame"] += 1
            else:
                key = ("CET" if f["payload"].startswith("{CET}") else f["ptype"])
                cls[key] += 1
        r["re_added_by_type"] = dict(cls)
        # relay writes for an id that was already relayed (written to ring) earlier by this node, gap < 4 h
        lastw: Dict[str, float] = {}
        rr = []
        for w in n.rw:
            if w["src"] != "rx_relay":
                continue
            if w["id"] in lastw and w["t"] - lastw[w["id"]] < 4 * 3600:
                rr.append((w["t"], w["id"], w["t"] - lastw[w["id"]]))
            lastw[w["id"]] = w["t"]
        r["relay_writes_of_already_relayed_id"] = len(rr)
        r["relay_rewrite_gap_min"] = quant([g / 60 for _t, _m, g in rr])
        r["relay_rewrite_gap_lt5min"] = sum(1 for _t, _m, g in rr if g < 300)
        r["relay_rewrite_examples"] = [(round(g, 1), m) for _t, m, g in sorted(rr, key=lambda x: x[2])[:5]]
        # dup flood: DUP verdicts per NEW id
        nn = sum(1 for d in n.dedup if d[1] == "NEW")
        dd = sum(1 for d in n.dedup if d[1] == "DUP")
        r["dup_per_new"] = round(dd / max(nn, 1), 2)
        # copies per id (all MH-LoRa): distribution
        cp = Counter(f["id"] for f in n.rx)
        r["copies_per_id"] = quant([float(v) for v in cp.values()])
        r["ids_with_ge5_copies"] = sum(1 for v in cp.values() if v >= 5)
        out[k] = r
    return out


REGISTRY["dedup"] = sec_dedup


def sec_idreuse(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Same (origin, msg_id) heard in separate bursts (>10 min apart). Split by who carried the later burst:
    HB9HNO-49 (late echoes of old frames) vs everyone else (= genuine id reuse by the origin)."""
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        bursts: Dict[Tuple[str, str], List[Dict[str, Any]]] = defaultdict(list)
        for f in n.rx:
            key = (src_of(f), f["id"])
            b = bursts[key]
            if not b or f["t"] - b[-1]["t_last"] > 600:
                b.append({"t": f["t"], "t_last": f["t"], "dedup": f.get("dedup"), "lh": last_of(f)})
            else:
                b[-1]["t_last"] = f["t"]
        cls: Dict[str, List[float]] = defaultdict(list)
        for (src, mid), v in bursts.items():
            for a, b in zip(v, v[1:]):
                g = (b["t"] - a["t"]) / 60
                who = "HB9HNO-49_echo" if b["lh"] == "HB9HNO-49" else "other_carrier"
                cls["%s|%s|first_copy_%s" % ("HB9JAT" if src == "HB9JAT" else "others", who, b["dedup"])].append(g)
        r: Dict[str, Any] = {kk: {"n": len(v), "gap_min": quant(v)} for kk, v in sorted(cls.items())}
        ids = sorted({m for (src, m) in bursts if src == "HB9JAT"})
        r["HB9JAT_origin"] = {"distinct_ids": len(ids), "bursts": sum(len(v) for (src, _m), v in bursts.items() if src == "HB9JAT"),
                              "id_min": ids[0] if ids else None, "id_max": ids[-1] if ids else None,
                              "fw": dict(Counter(f.get("fw") for f in n.rx if src_of(f) == "HB9JAT" and len(f["path"]) == 1))}
        out[k] = r
    return out


REGISTRY["idreuse"] = sec_idreuse


def sec_stale(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Late copies of any frame type: DUP frames arriving long after the dedup ADD of the same id (per last-hop transmitter)."""
    out: Dict[str, Any] = {}
    for k, n in nodes.items():
        added: Dict[str, float] = {}
        ai = 0
        dd = sorted(n.dedup, key=lambda d: d[0])
        # map frames to the ADD time: iterate frames in time order, advance ADD pointer
        late: Dict[str, List[float]] = defaultdict(list)
        tot: Counter = Counter()
        delay_all = []
        for f in n.rx:
            while ai < len(dd) and dd[ai][0] <= f["t"] + 0.002:
                if dd[ai][1] == "ADD":
                    added[dd[ai][2]] = dd[ai][0]
                ai += 1
            if f.get("dedup") != "DUP":
                continue
            ta = added.get(f["id"])
            if ta is None:
                continue
            d = f["t"] - ta
            tot[last_of(f)] += 1
            delay_all.append(d)
            if d > 300:
                late[last_of(f)].append(d)
        r: Dict[str, Any] = {
            "dup_frames_with_add": sum(tot.values()),
            "dup_delay_s": quant(delay_all),
            "late_gt300s_total": sum(len(v) for v in late.values()),
            "late_gt300s_by_lasthop": {c: [len(v), tot[c], round(sorted(v)[len(v) // 2])] for c, v in
                                       sorted(late.items(), key=lambda x: -len(x[1]))[:8]},
        }
        # how many of HB9HNO-49's DUP copies are late
        h = [d for f in n.rx if last_of(f) == "HB9HNO-49" for d in [0]]
        r["frames_from_HB9HNO-49"] = sum(1 for f in n.rx if last_of(f) == "HB9HNO-49")
        hn = [f for f in n.rx if last_of(f) == "HB9HNO-49"]
        r["HB9HNO-49_rx_air_s"] = round(sum(toa(f["len"]) for f in hn), 0)
        r["HB9HNO-49_share_of_decoded_frames_pct"] = pct(len(hn), len(n.rx))
        r["HB9HNO-49_share_of_decoded_rx_air_pct"] = pct(sum(toa(f["len"]) for f in hn), sum(toa(f["len"]) for f in n.rx))
        r["HB9HNO-49_by_type"] = dict(Counter(f["ptype"] for f in hn))
        r["HB9HNO-49_dedup_verdict"] = dict(Counter(f.get("dedup") for f in hn))
        r["fw_of_HB9HNO-49"] = dict(Counter(f.get("fw") for f in n.rx if src_of(f) == "HB9HNO-49" and len(f["path"]) == 1))
        out[k] = r
    return out


REGISTRY["stale"] = sec_stale


def sec_load(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Channel load, queue behaviour (no build-up), TX waits, CSMA, uploads, loops, own traffic."""
    out: Dict[str, Any] = {}
    uploads: Dict[str, set] = {}
    for k, n in nodes.items():
        r: Dict[str, Any] = {}
        cu = n.channel_util
        r["util_pct"] = quant([float(x[3]) for x in cu])
        r["rx_air_s"] = round(sum(x[1] for x in cu) / 1000, 0)
        r["tx_air_s"] = round(sum(x[2] for x in cu) / 1000, 0)
        r["tx_duty_pct"] = pct(r["tx_air_s"], 86400)
        r["rx_duty_pct"] = pct(r["rx_air_s"], 86400)
        # queue depth: RING_WRITE queued= (depth before write)
        qs = [w["q"] for w in n.rw]
        r["queue_depth_at_write"] = quant([float(q) for q in qs])
        hq = defaultdict(list)
        for w in n.rw:
            hq[int((w["t"] - n.t0) // 3600)].append(w["q"])
        means = [sum(v) / len(v) for _h, v in sorted(hq.items())]
        # slope of hourly mean depth (least squares, slots per hour)
        xs = list(range(len(means)))
        mx, my = sum(xs) / len(xs), sum(means) / len(means)
        r["queue_depth_hourly_mean_slope_per_h"] = round(sum((x - mx) * (y - my) for x, y in zip(xs, means)) / sum((x - mx) ** 2 for x in xs), 4)
        r["qmax_5min"] = quant([float(q) for _t, q in n.qmax])
        r["qmax_windows_ge15"] = sum(1 for _t, q in n.qmax if q >= 15)
        r["ring_drops_p1_p5_sum"] = [sum(d[1][i] for d in n.drops) for i in range(5)] if n.drops else None
        # TX wait by class/type
        recs = tx_records(n)
        byc: Dict[str, List[float]] = defaultdict(list)
        for x in recs:
            byc["%s/%s/p%d" % (x["cls"], x["type"], x["prio"])].append(x["lat"])
        r["tx_wait_s"] = {c: {"n": len(v), "p50": quant(v)["p50"], "p90": quant(v)["p90"], "max": quant(v)["p100"]}
                          for c, v in sorted(byc.items()) if len(v) >= 5}
        r["tx_wait_gt150s"] = sum(1 for x in recs if x["lat"] > 150)
        # CSMA
        r["cad_busy_events"] = len(n.cad_busy)
        r["csma_aged_events"] = len(n.csma_aged)
        # TX per hour spread
        hh = Counter(int((x["t"] - n.t0) // 3600) for x in recs)
        r["tx_per_hour"] = quant([float(hh.get(i, 0)) for i in range(24)])
        # loops: new frames with own call in path (not last) that got a ring write
        wt = sorted(w["t"] for w in n.rw if w["src"] == "rx_relay")
        loopw = 0
        own_in_path_new = 0
        for f in n.new2:
            if n.own in f["path"][:-1]:
                own_in_path_new += 1
                i = bisect.bisect_left(wt, f["t"] - 0.01)
                if i < len(wt) and wt[i] < f["t"] + 0.2:
                    loopw += 1
        r["accepted_new_with_own_call_in_path"] = own_in_path_new
        r["of_which_relayed"] = loopw
        r["rx_with_own_call_in_path_total"] = sum(1 for f in n.rx if n.own in f["path"][:-1])
        # uploads
        up = [f["id"] for f in n.udp_up]
        uploads[k] = set(up)
        r["tx_udp_uploads"] = len(up)
        r["tx_udp_distinct_ids"] = len(set(up))
        r["tx_udp_by_type"] = dict(Counter(f["ptype"] for f in n.udp_up))
        r["tx_udp_by_S"] = dict(Counter(f["S"] for f in n.udp_up))
        # frames accepted as new that were NOT uploaded (server flag)
        newids = {f["id"] for f in n.new2}
        r["rx_lora2_ids"] = len(newids)
        r["rx_lora2_ids_not_uploaded"] = len(newids - set(up))
        sflag = Counter(f["S"] for f in n.new2)
        r["rx_lora2_by_S"] = dict(sflag)
        # own user messages
        um = [w for w in n.rw if w["src"] == "user_msg"]
        r["user_msg_writes"] = [{"t": time.strftime("%d %H:%M:%S", time.gmtime(w["t"])), "id": w["id"], "type": w["type"]} for w in um]
        out[k] = r
    both = uploads["hb9vqq4"] & uploads["hb9vqq5"]
    out["_uploads_ids_both"] = len(both)
    out["_uploads_ids_union"] = len(uploads["hb9vqq4"] | uploads["hb9vqq5"])
    return out


REGISTRY["load"] = sec_load


def sec_hops(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Hop/path invariants and direct-neighbour sets."""
    out: Dict[str, Any] = {}
    nb: Dict[str, Counter] = {}
    for k, n in nodes.items():
        sums: Dict[str, Counter] = defaultdict(Counter)
        dup_path = 0
        hi = 0
        maxlen = 0
        for f in n.rx:
            sums[f["ptype"]][(f["H"] & 0x0F) + len(f["path"]) - 1] += 1
            if len(set(f["path"])) != len(f["path"]):
                dup_path += 1
            if f["H"] >> 4:
                hi += 1
            maxlen = max(maxlen, len(f["path"]))
        out[k] = {"hop_sum_by_type": {t: dict(sorted(c.items())) for t, c in sums.items()},
                  "frames_with_repeated_call_in_path": dup_path, "frames_with_H_high_nibble": hi, "max_path_len": maxlen}
        nb[k] = Counter(last_of(f) for f in n.rx)
    a = {c: v for c, v in nb["hb9vqq4"].items() if v >= 5 and c != "HB9VQQ-4"}
    b = {c: v for c, v in nb["hb9vqq5"].items() if v >= 5 and c != "HB9VQQ-5"}
    n4, n5 = nodes["hb9vqq4"], nodes["hb9vqq5"]
    i4 = {f["id"] for f in n4.rx}
    i5 = {f["id"] for f in n5.rx}
    new4 = {f["id"] for f in n4.rx if f.get("dedup") == "NEW"}
    new5 = {f["id"] for f in n5.rx if f.get("dedup") == "NEW"}
    out["_neighbours"] = {
        "n4": len(a), "n5": len(b), "common": sorted(set(a) & set(b)), "only4": sorted(set(a) - set(b)),
        "only5": sorted(set(b) - set(a)),
        "share_of_rx_from_common_pct": {"hb9vqq4": pct(sum(a[c] for c in set(a) & set(b)), sum(a.values())),
                                         "hb9vqq5": pct(sum(b[c] for c in set(a) & set(b)), sum(b.values()))},
        "ids_heard": [len(i4), len(i5), len(i4 & i5)],
        "ids_heard_at_5_also_at_4_pct": pct(len(i4 & i5), len(i5)),
        "NEW_at_4_never_heard_at_5": [len(new4 - i5), len(new4)], "NEW_at_5_never_heard_at_4": [len(new5 - i4), len(new5)],
    }
    return out


REGISTRY["hops"] = sec_hops


def sec_misc(nodes: Dict[str, Node]) -> Dict[str, Any]:
    """Trickle, junk 12-byte frames, ACK markers, own user texts (no payload printed)."""
    out: Dict[str, Any] = {}
    import re as _re
    for k, n in nodes.items():
        r: Dict[str, Any] = {}
        send = supp = topo = send_ge2 = 0
        junk = []
        ackm: Counter = Counter()
        with open(PATHS[k], "r", errors="replace") as fh:
            for line in fh:
                if "[MC-TRICKLE]" in line:
                    if "] SEND" in line:
                        send += 1
                        m = _re.search(r"consistent=(\d+)<k=2", line)
                        if m and int(m.group(1)) >= 2:
                            send_ge2 += 1
                    elif "] SUPPRESS" in line:
                        supp += 1
                    elif "TOPO_CHANGE" in line:
                        topo += 1
                elif "ACK_REJECT" in line:
                    junk.append(line[:23])
                elif "ACK_SKIP_READY" in line or "ACK_RECEIVED" in line:
                    ackm["ACK_SKIP_READY" if "SKIP" in line else "ACK_RECEIVED"] += 1
        r["trickle"] = {"SEND": send, "SEND_with_consistent_ge_k": send_ge2, "SUPPRESS": supp, "TOPO_CHANGE": topo}
        r["ack_reject_times"] = junk
        r["ack_markers"] = dict(ackm)
        # own user texts
        ut = []
        for w in [w for w in n.rw if w["src"] == "user_msg"]:
            tx = [f for f in n.tx if f["id"] == w["id"]]
            heard_here = [f for f in n.rx if f["id"] == w["id"] and f["t"] >= w["t"] - 1]
            other = nodes["hb9vqq5" if k == "hb9vqq4" else "hb9vqq4"]
            heard_other = [f for f in other.rx if f["id"] == w["id"] and f["t"] >= w["t"] - 1]
            ut.append({"id": w["id"], "t": time.strftime("%d %H:%M:%S", time.gmtime(w["t"])),
                       "tx_delay_s": round(tx[0]["t"] - w["t"], 1) if tx else None, "dest": tx[0]["dest"] if tx else None,
                       "copies_heard_here": len(heard_here), "distinct_relays_here": len({last_of(f) for f in heard_here}),
                       "copies_heard_at_other": len(heard_other),
                       "other_first_lasthop": last_of(heard_other[0]) if heard_other else None,
                       "other_first_after_s": round(heard_other[0]["t"] - w["t"], 1) if heard_other else None})
        r["user_texts"] = ut
        out[k] = r
    return out


REGISTRY["misc"] = sec_misc


def sec_idtrace(nodes: Dict[str, Node], mid: str) -> Dict[str, Any]:
    """All events of one msg_id at both nodes (path/H/RSSI only, no payload)."""
    rows = []
    for k, n in nodes.items():
        for f in n.udp_dn:
            if f["id"] == mid:
                rows.append((f["t"], k, "RX-UDP", ",".join(f["path"]), f["H"], None, None))
        for w in n.rw:
            if w["id"] == mid:
                rows.append((w["t"], k, "RING_WRITE(%s)" % w["src"], "", None, None, None))
        for f in n.tx:
            if f["id"] == mid:
                rows.append((f["t"], k, "TX-LoRa", ",".join(f["path"]), f["H"], None, None))
        for f in n.rx:
            if f["id"] == mid:
                rows.append((f["t"], k, "MH-LoRa(%s)" % f.get("dedup"), ",".join(f["path"]), f["H"], f["rssi"], f["snr"]))
    rows.sort()
    t0 = rows[0][0] if rows else 0
    return {"id": mid, "events": [{"t": time.strftime("%H:%M:%S", time.gmtime(r[0])) + (".%03d" % int(round((r[0] % 1) * 1000))),
                                   "dt": round(r[0] - t0, 2), "node": r[1], "ev": r[2], "path": r[3], "H": r[4], "rssi": r[5], "snr": r[6]}
                                  for r in rows]}


PATHS: Dict[str, str] = {}


def main(argv: List[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    norm = argv[1].rstrip("/")
    secs = argv[2:]
    for k_ in OWN:
        PATHS[k_] = "%s/%s.log" % (norm, k_)
    nodes = {k: load(PATHS[k], k) for k in OWN}
    table = {"parse": sec_parse, "clock": sec_clock, "cet": sec_cet}
    # sections registered below via REGISTRY
    table.update(REGISTRY)
    if secs == ["all"]:
        secs = list(table)
    res = {}
    for s in secs:
        if s.startswith("idtrace="):
            res[s] = sec_idtrace(nodes, s.split("=", 1)[1])
        else:
            res[s] = table[s](nodes)
    print(json.dumps(res, indent=1, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
