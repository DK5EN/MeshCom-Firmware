#!/usr/bin/env python3
"""Offline analyzer for a gwrun.py bench run (RM + plain traffic, two gateway nodes, mock server).

    python3 gwanalyze.py <run dir>                 per-case table, negative phase, plain traffic, foreign frames, anomalies
    python3 gwanalyze.py <run dir> --trace rm:status   timeline of one case (substring of the case name, optional "@1>90")
    python3 gwanalyze.py --selftest                synthetic lines built from the real log formats

Run dir layout: events.json, DK5EN-1.log, DK5EN-90.log (lines prefixed with the host wall clock HH:MM:SS, local time),
mock.log (python logging, local time), mock/udp-log.txt (`t dir ip:port ind= len= hex`, t = seconds since mock start).

Never prints a password or a key: it reads none of them (pw files are not opened); RM tags are shortened to 6 hex.
Stdlib only.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import datetime as dt
import json
import os
import re
import statistics
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Any, Deque, Dict, Iterable, List, Optional, Tuple

NODE_FILES = {"DK5EN-1": "DK5EN-1.log", "DK5EN-90": "DK5EN-90.log"}
OWN_CALLS = ("DK5EN-1", "DK5EN-90")
SHORT = {"1": "DK5EN-1", "90": "DK5EN-90"}
MOCK_HDR = 36  # "DATA" + gw id (8) + call (9) + version/flags up to the frame type byte

# ---------------------------------------------------------------- line formats (see src/lora_functions.cpp, udp_frame_*.cpp)
PFX = re.compile(r"^(\d\d):(\d\d):(\d\d)\s(.*)$")
NODE_TS = re.compile(r"^\d\d:\d\d:\d\d(?:\s+|(?=\[))")  # the node's own (UTC) stamp, ignored
FRAME = re.compile(
    r"^(?P<tag>RX-UDP|TX-UDP|TX-Lo[Rr]a|NEW-TXT|NEW-POS|NEW-HEY|NEW-ACK)\s+(?P<len>\d+)\s+(?P<typ>\S)\s+x(?P<id>[0-9A-F]{8})\s+H(?P<hops>[0-9A-Fa-f]{2})\s+(?P<rest>.*)$"
)
LOGRX = re.compile(
    r"^\[LOG\]\s+(?P<len>\d+)\s+(?P<typ>\S)\s+x(?P<id>[0-9A-F]{8})\s+H(?P<hops>[0-9A-Fa-f]{2})\s+(?P<rest>.*)$"
)
BODY = re.compile(
    r"^S(?P<s>\d)\s+T\d+\s+M\d+\s+(?P<path>[^>\s]+)>(?P<dest>[^:!@\s]*)(?P<sep>[:!@])(?P<text>.*?)(?:\s+HW:[0-9A-Fa-f]+\s+MOD:.*)?$"
)
RFTAIL = re.compile(r"RSSI:(?P<rssi>-?\d+)\s+SNR:(?P<snr>-?\d+)\s+DUP:(?P<dup>[nd])\s+OWN:(?P<own>\S+)")
DEDUP = re.compile(r"^\[MC-DBG\]\s+RX_DEDUP_(?P<k>NEW|DUP|ADD)\s+msg_id=(?P<id>[0-9A-F]{8})")
DMDUP = re.compile(r"^\[DMDUP\]\s+from\s+(?P<c>\S+)\s+nnn:(?P<n>\d+)")
REACK = re.compile(r"^\[REACK-LIMIT\]\s+dup\s+from\s+(?P<c>\S+)\s+nnn:(?P<n>\d+)")
ACKMARK = re.compile(r"\[(?P<k>UDP-MSGID|ACK-MSGID)\]\s+ack_msg_id:(?P<id>[0-9A-F]{8})")
RETX = re.compile(r"^\[RETX\]\s+DM-ACK for retid:(?P<r>\d+)\s+stop retransmit msg-id:(?P<id>[0-9A-F]{8})")
RINGDROP = re.compile(r"^\[MC-DBG\]\s+RING_DROP_(?P<k>NEW|PRIO|STALE)\s+slot=\d+(?:\s+age_s=(?P<age>\d+))?.*?msg_id=(?P<id>[0-9A-F]{8})")
TXLOG = re.compile(r"^\[LOG\]\s+TX\s+x(?P<id>[0-9A-F]{8})\s+:?\s*H\w+\s+prio=(?P<prio>\d)\s+src=(?P<src>\S)\s+wait=(?P<wait>\d+)\s+q=(?P<q>\d+)")
GWU = re.compile(r"^\[LOG\]\s+(?P<k>GWU|RLY)\s+x(?P<id>[0-9A-F]{8})")
RM_OK = re.compile(r"^\[RM\];(?P<v>ok|fail|cached|sync);ctr;(?P<ctr>\d+)(?P<sfx>;suppressed)?")
RM_REJ = re.compile(r"^\[RM\];reject;(?P<why>\w+)")
RM_SEND = re.compile(r"^\[RM\];send;(?P<dst>[^;]+);ctr;(?P<ctr>\d+)")
RM_REPLY = re.compile(r"^\[RM\];reply;(?P<dst>[^;]+);ctr;(?P<ctr>\d+);verified;(?P<v>\d)")
RM_SENDFAIL = re.compile(r"^\[RM\];reply;send_failed;(?P<rc>-?\d+)")
BP = re.compile(r"^\[BP\];(?P<k>nack|refuse|notice);(?P<rest>.*)$")
RMTEXT = re.compile(r"^RM1 (?P<ctr>\d+) (?P<rest>.*)$")
JUNK = re.compile(r"RM1 (?P<ctr>\d+) status 0123456789abcde(?P<n>\d)")
ANOM = [
    ("boot", re.compile(r"\[BOOT\]")),
    ("dropped", re.compile(r"##\s*dropped|\bdropped\b")),
    ("watchdog", re.compile(r"watchdog|\bWDT\b|TASK_WDT", re.I)),
    ("eth_stall", re.compile(r"\[ETH\];stall")),
    ("crash", re.compile(r"Guru Meditation|Backtrace|panic", re.I)),
    ("rx_error", re.compile(r"^OnRxError")),
    ("onrxdone_slow", re.compile(r"ONRXDONE_SLOW")),
    ("ring_drop", re.compile(r"RING_DROP_(NEW|PRIO|STALE)")),
    ("ring_full", re.compile(r"^\[RING\] full")),
    ("bp", re.compile(r"^\[BP\];")),
    ("wifi_eth_link", re.compile(r"^\[(WIFI|ETH)\];link")),
]
TXFAIL = re.compile(r"tx_fail;(\d+)")


@dataclass
class Ev:
    t: float
    seq: int
    node: str
    kind: str  # rf_rx udp_rx rf_tx udp_tx new dedup dmdup reack ack retx txlog gwu rm rm_send rm_reply bp
    raw: str = ""
    msgid: str = ""
    typ: str = ""
    path: str = ""
    dest: str = ""
    text: str = ""
    sflag: int = -1
    rssi: Optional[int] = None
    dup: str = ""  # rf: 'n'|'d' from the LOG line
    k: str = ""  # sub kind: dedup NEW/DUP/ADD, rm verdict, ...
    ctr: int = 0
    extra: Dict[str, Any] = field(default_factory=dict)

    @property
    def origin(self) -> str:
        return self.path.split(",")[0]


@dataclass
class Copy:
    """One arrival of an RM frame at a node (over RF or over the server path)."""

    ev: Ev
    via: str  # 'rf' | 'srv'
    dup: bool  # dedup ring said duplicate (DUP:d / RX_DEDUP_DUP)
    reply: bool
    ctr: int
    verdict: str = ""  # receiver-side [RM] verdict paired with this copy (commands only)


# ---------------------------------------------------------------------------------------------------- parsing
def parse_log(path: str, node: str, base: dt.date) -> List[Ev]:
    evs: List[Ev] = []
    day = base
    prev_sod = -1
    seq = 0
    with open(path, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = PFX.match(line)
            if not m:
                continue
            hh, mm, ss, rest = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
            sod = hh * 3600 + mm * 60 + ss
            if prev_sod >= 0 and sod < prev_sod - 43200:
                day = day + dt.timedelta(days=1)
            prev_sod = sod
            t = dt.datetime.combine(day, dt.time(hh, mm, ss)).timestamp()
            seq += 1
            rest = NODE_TS.sub("", rest, count=1) if NODE_TS.match(rest) else rest
            ev = classify(rest, t, seq, node)
            if ev is not None:
                evs.append(ev)
    return evs


def classify(rest: str, t: float, seq: int, node: str) -> Optional[Ev]:
    m = FRAME.match(rest) or LOGRX.match(rest)
    if m:
        tag = m.groupdict().get("tag") or "[LOG]"
        body = m.group("rest")
        kind = {"RX-UDP": "udp_rx", "TX-UDP": "udp_tx", "[LOG]": "rf_rx"}.get(tag, "rf_tx" if tag.lower() == "tx-lora" else "new")
        ev = Ev(t, seq, node, kind, raw=rest, msgid=m.group("id"), typ=m.group("typ"))
        b = BODY.match(body)
        if b:
            ev.sflag, ev.path, ev.dest, ev.text = int(b.group("s")), b.group("path"), b.group("dest"), b.group("text")
            ev.extra["sep"] = b.group("sep")
        else:  # ack frame: "x55A8F198 01 00 ..."
            ev.text = body.split(" RSSI:")[0]
        if kind == "rf_rx":
            r = RFTAIL.search(body)
            if r:
                ev.rssi, ev.dup = int(r.group("rssi")), r.group("dup")
                ev.extra["own"] = r.group("own")
        return ev
    m = DEDUP.match(rest)
    if m:
        return Ev(t, seq, node, "dedup", raw=rest, msgid=m.group("id"), k=m.group("k"))
    m = DMDUP.match(rest)
    if m:
        return Ev(t, seq, node, "dmdup", raw=rest, path=m.group("c"), ctr=int(m.group("n")))
    m = REACK.match(rest)
    if m:
        return Ev(t, seq, node, "reack", raw=rest, path=m.group("c"), ctr=int(m.group("n")))
    m = ACKMARK.search(rest)
    if m:
        return Ev(t, seq, node, "ack", raw=rest, msgid=m.group("id"), k=m.group("k"))
    m = RETX.match(rest)
    if m:
        return Ev(t, seq, node, "retx", raw=rest, msgid=m.group("id"))
    m = RINGDROP.match(rest)
    if m:
        return Ev(t, seq, node, "ringdrop", raw=rest, msgid=m.group("id"), k=m.group("k"), extra={"age": int(m.group("age") or 0)})
    m = TXLOG.match(rest)
    if m:
        return Ev(t, seq, node, "txlog", raw=rest, msgid=m.group("id"), extra={"wait": int(m.group("wait")), "q": int(m.group("q")), "prio": int(m.group("prio"))})
    m = GWU.match(rest)
    if m:
        return Ev(t, seq, node, "gwu", raw=rest, msgid=m.group("id"), k=m.group("k"))
    m = RM_SEND.match(rest)
    if m:
        return Ev(t, seq, node, "rm_send", raw=rest, dest=m.group("dst"), ctr=int(m.group("ctr")))
    m = RM_REPLY.match(rest)
    if m:
        return Ev(t, seq, node, "rm_reply", raw=rest, dest=m.group("dst"), ctr=int(m.group("ctr")), k=m.group("v"))
    m = RM_SENDFAIL.match(rest)
    if m:
        return Ev(t, seq, node, "rm", raw=rest, k="reply_send_failed", extra={"rc": int(m.group("rc"))})
    m = RM_OK.match(rest)
    if m:
        v = m.group("v") + ("_suppressed" if m.group("sfx") else "")
        return Ev(t, seq, node, "rm", raw=rest, k=v, ctr=int(m.group("ctr")))
    m = RM_REJ.match(rest)
    if m:
        return Ev(t, seq, node, "rm", raw=rest, k="reject_" + m.group("why"))
    m = BP.match(rest)
    if m:
        return Ev(t, seq, node, "bp", raw=rest, k=m.group("k"), text=m.group("rest"))
    return None


def parse_frame_hex(h: str) -> Optional[Dict[str, str]]:
    """Frame inside a mock datagram: DATA = 36 byte header + frame, GATE = 'GATE' + frame.
    frame = type(1) msgid(4, little endian) flags(1) "path>dest:text" NUL ..."""
    try:
        b = bytes.fromhex(h)
    except ValueError:
        return None
    if b[:4] == b"DATA":
        fr = b[MOCK_HDR:]
    elif b[:4] == b"GATE":
        fr = b[4:]
    else:
        return None
    if len(fr) < 7:
        return None
    msgid = "%08X" % int.from_bytes(fr[1:5], "little")
    body = fr[6:].split(b"\0")[0].decode("latin1")
    origin = body.split(">")[0].split(",")[0]
    return {"msgid": msgid, "typ": chr(fr[0]), "body": body, "origin": origin}


@dataclass
class MockRow:
    t: float
    dir: str  # rx = node -> mock (DATA), tx = mock -> node (GATE)
    ip: str
    ind: str
    msgid: str = ""
    body: str = ""
    origin: str = ""


def parse_mock(d: str, fallback_t0: float) -> List[MockRow]:
    p = os.path.join(d, "mock", "udp-log.txt")
    if not os.path.exists(p):
        return []
    t0 = fallback_t0
    ml = os.path.join(d, "mock.log")
    if os.path.exists(ml):
        with open(ml, errors="replace") as f:
            for line in f:
                if "event=listen" in line:
                    try:
                        t0 = dt.datetime.strptime(line[:23], "%Y-%m-%d %H:%M:%S,%f").timestamp()
                    except ValueError:
                        pass
                    break
    rows: List[MockRow] = []
    with open(p, errors="replace") as f:
        for line in f:
            w = line.split()
            if len(w) < 6:
                continue
            try:
                t = t0 + float(w[0])
            except ValueError:
                continue
            ind = w[3].split("=", 1)[-1]
            r = MockRow(t, w[1], w[2].split(":")[0], ind)
            if ind in ("DATA", "GATE"):
                fr = parse_frame_hex(w[-1])
                if fr:
                    r.msgid, r.body, r.origin = fr["msgid"], fr["body"], fr["origin"]
            rows.append(r)
    return rows


# ---------------------------------------------------------------------------------------------------- run model
class Run:
    def __init__(self, d: str) -> None:
        self.d = d
        with open(os.path.join(d, "events.json")) as f:
            self.events: List[Dict[str, Any]] = json.load(f)
        t_first = next((e["t"] for e in self.events if "t" in e), None) or next((e["t_send"] for e in self.events if "t_send" in e), 0.0)
        base = dt.datetime.fromtimestamp(t_first).date()
        self.logs: Dict[str, List[Ev]] = {}
        for node, fn in NODE_FILES.items():
            p = os.path.join(d, fn)
            self.logs[node] = parse_log(p, node, base) if os.path.exists(p) else []
        self.mock = parse_mock(d, t_first)
        self.ip_to_node: Dict[str, str] = {}
        self._learn_ips()
        self.copies: Dict[str, List[Copy]] = {n: self._build_copies(n) for n in self.logs}
        for n in self.logs:
            self._pair_verdicts(n)

    def _learn_ips(self) -> None:
        for e in self.events:
            if e.get("host") and e.get("case") == "web_identity":
                self.ip_to_node[e["host"]] = e["dir"]
        ml = os.path.join(self.d, "mock.log")
        if os.path.exists(ml):
            with open(ml, errors="replace") as f:
                for line in f:
                    m = re.search(r"event=keep addr=([\d.]+):\d+ .*callsign=(\S+)", line)
                    if m:
                        self.ip_to_node.setdefault(m.group(1), m.group(2))

    def _dedup_kind(self, node: str, ev: Ev, dedups: Dict[str, List[Ev]]) -> Optional[str]:
        best: Optional[Ev] = None
        for d in dedups.get(ev.msgid, []):
            if abs(d.seq - ev.seq) <= 12 and (best is None or abs(d.seq - ev.seq) < abs(best.seq - ev.seq)):
                if d.k in ("NEW", "DUP"):
                    best = d
        return best.k if best else None

    def _build_copies(self, node: str) -> List[Copy]:
        evs = self.logs[node]
        dedups: Dict[str, List[Ev]] = collections.defaultdict(list)
        for e in evs:
            if e.kind == "dedup":
                dedups[e.msgid].append(e)
        out: List[Copy] = []
        for e in evs:
            if e.kind not in ("rf_rx", "udp_rx") or e.typ != ":":
                continue
            m = RMTEXT.match(e.text)
            if not m:
                continue
            via = "rf" if e.kind == "rf_rx" else "srv"
            if via == "rf":
                dup = e.dup == "d"
            else:
                dup = self._dedup_kind(node, e, dedups) == "DUP"
            rest = m.group("rest")
            out.append(Copy(e, via, dup, rest.startswith(("ok ", "err ")), int(m.group("ctr"))))
        return out

    PAIR_LAG = 10.0  # s: the RM queue is drained from the loop task, verdicts follow their frame within a few seconds

    def _pair_verdicts(self, node: str) -> None:
        """Pair each [RM] verdict line with the oldest unpaired command copy (not a ring duplicate) addressed to this node
        that arrived at most PAIR_LAG s before it. ok/fail/cached carry the counter; sync prints the mark, so it pairs with ctr 0."""
        q: List[Copy] = [c for c in self.copies[node] if not c.reply and not c.dup and c.ev.dest == node and c.ev.origin != node]
        used = set()
        for v in self.logs[node]:
            if v.kind != "rm" or v.k == "reply_send_failed":
                continue
            pick: Optional[Copy] = None
            for c in q:
                if id(c) in used or c.ev.seq > v.seq or v.t - c.ev.t > self.PAIR_LAG:
                    continue
                if v.k == "sync" and c.ctr != 0:
                    continue
                if v.k != "sync" and v.ctr and c.ctr != v.ctr:
                    continue
                pick = c
                break
            if pick is not None:
                used.add(id(pick))
                pick.verdict = v.k.replace("reject_", "")
                pick.ev.extra["verdict_t"] = v.t

    # --- accessors
    def sender_ctr(self, sender: str, target: str, t_send: float) -> Optional[Ev]:
        for e in self.logs[sender]:
            if e.kind == "rm_send" and e.dest == target and t_send - 2 <= e.t <= t_send + 6:
                return e
        return None

    def rm_cases(self) -> List[Tuple[int, Dict[str, Any]]]:
        return [(i, e) for i, e in enumerate(self.events) if str(e.get("case", "")).startswith("rm:") and "t_send" in e]


# ---------------------------------------------------------------------------------------------------- per-case analysis
def side(dirs: str) -> Tuple[str, str]:
    a, b = dirs.split(">")
    return SHORT[a], SHORT[b]


def fmt_via(cs: Iterable[Copy]) -> str:
    cs = list(cs)
    return "%d/%d" % (sum(c.via == "rf" for c in cs), sum(c.via == "srv" for c in cs))


@dataclass
class CaseRes:
    idx: int
    case: str
    dirs: str
    result: str
    lat: float
    ok_d: Any
    rej_d: Any
    ctr: int
    cmd_in: List[Copy]
    cmd_late: List[Copy]
    first: str
    dedup: int
    suppressed: int
    verdicts: str
    rep_in: List[Copy]
    rep_first: str
    verified: str
    real_lat: Optional[float]
    rf_delay: Optional[float]  # first rf copy minus first srv copy at the receiver (s)
    note: str = ""
    mid: str = ""
    rmid: str = ""
    rf_tx: Optional[float] = None  # sender's own TX-LoRa of the command, s after t_send
    rf_drop: str = ""  # RING_DROP_* seen for the command msgid at the sender
    mock_cmd: Optional[float] = None  # command DATA at the mock, s after t_send
    mock_rep: Optional[float] = None  # reply DATA at the mock, s after t_send
    rep_rf_tx: Optional[float] = None  # target's own TX-LoRa of the reply, s after t_send


def frame_msgid(run: Run, node: str, ctr: int, reply: bool, t0: float, t1: float, dst: str) -> str:
    """msgid of the RM frame this node created (NEW-*, else its first TX line) with this counter in [t0, t1]."""
    for kinds in (("new",), ("udp_tx", "rf_tx")):
        for e in run.logs[node]:
            if e.kind in kinds and t0 <= e.t <= t1 and e.typ == ":" and e.dest == dst:
                m = RMTEXT.match(e.text)
                if m and int(m.group("ctr")) == ctr and m.group("rest").startswith(("ok ", "err ")) == reply:
                    return e.msgid
    return ""


def analyze_case(run: Run, idx: int, e: Dict[str, Any], tail: float) -> Optional[CaseRes]:
    src, dst = side(e["dir"])
    base = dict(idx=idx, case=e["case"], dirs=e["dir"], result=e.get("result", "?"), lat=e.get("lat", 0), ok_d=e.get("ok_d"), rej_d=e.get("rej_d"))
    if e.get("result") == "refused":
        return CaseRes(ctr=-1, cmd_in=[], cmd_late=[], first="-", dedup=0, suppressed=0, verdicts="(never sent)", rep_in=[], rep_first="-", verified="-", real_lat=None, rf_delay=None, **base)
    s = run.sender_ctr(src, dst, e["t_send"])
    if s is None:
        return CaseRes(ctr=-1, cmd_in=[], cmd_late=[], first="-", dedup=0, suppressed=0, verdicts="no [RM];send line", rep_in=[], rep_first="-", verified="-", real_lat=None, rf_delay=None, **base)
    ctr = s.ctr
    t0, t1 = e["t_send"] - 2, e["t_send"] + e.get("lat", 0) + tail
    mid = frame_msgid(run, src, ctr, False, t0, t0 + 12, dst)
    allc = [c for c in run.copies[dst] if c.ev.msgid == mid and not c.reply and c.ev.t >= t0] if mid else []
    cin = [c for c in allc if c.ev.t <= t1]
    late = [c for c in allc if c.ev.t > t1]
    srt = sorted(allc, key=lambda c: (c.ev.t, c.ev.seq))
    first = srt[0].via if srt else "-"
    dedup = sum(c.dup for c in cin)
    sup = sum(1 for c in cin if c.dup or c.verdict in ("replay", "cached", "rate", "cached_suppressed"))
    verd = " ".join("%s:%s%s" % (c.via, "dup" if c.dup else (c.verdict or "?"), "" if c in cin else "@+%ds" % round(c.ev.t - e["t_send"])) for c in srt)
    rmid = frame_msgid(run, dst, ctr, True, t0, t1, src)
    reps = [c for c in run.copies[src] if c.ev.msgid == rmid and c.reply and c.ev.t >= t0] if rmid else []
    rep_in = [c for c in reps if c.ev.t <= t1]
    rsrt = sorted(reps, key=lambda c: (c.ev.t, c.ev.seq))
    rep_first = rsrt[0].via if rsrt else "-"
    ver = [v for v in run.logs[src] if v.kind == "rm_reply" and v.ctr == ctr and v.dest == dst and t0 <= v.t <= t1 + 90]
    verified = ("yes" if ver[0].k == "1" else "NO") if ver else "-"
    real = (ver[0].t - e["t_send"]) if ver else None
    rf = [c for c in srt if c.via == "rf"]
    sv = [c for c in srt if c.via == "srv"]
    rfd = (rf[0].ev.t - sv[0].ev.t) if (rf and sv) else None
    r = CaseRes(ctr=ctr, cmd_in=cin, cmd_late=late, first=first, dedup=dedup, suppressed=sup, verdicts=verd, rep_in=rep_in, rep_first=rep_first, verified=verified, real_lat=real, rf_delay=rfd, **base)
    r.mid, r.rmid = mid, rmid
    # sender RF side: when did its own LoRa TX of the command happen, was it dropped from the ring
    tx = [x for x in run.logs[src] if x.kind == "rf_tx" and x.msgid == mid and x.t >= t0]
    drop = [x for x in run.logs[src] if x.kind == "ringdrop" and x.msgid == mid and x.t >= t0]
    r.rf_tx = (tx[0].t - e["t_send"]) if tx else None
    r.rf_drop = drop[0].k if drop else ""
    mrow = [m for m in run.mock if m.ind == "DATA" and m.dir == "rx" and m.msgid == mid]
    mrep = [m for m in run.mock if m.ind == "DATA" and m.dir == "rx" and m.msgid == rmid]
    r.mock_cmd = (mrow[0].t - e["t_send"]) if mrow else None
    r.mock_rep = (mrep[0].t - e["t_send"]) if mrep else None
    rtx = [x for x in run.logs[dst] if x.kind == "rf_tx" and x.msgid == rmid and x.t >= t0]
    r.rep_rf_tx = (rtx[0].t - e["t_send"]) if rtx else None
    return r


def f1(x: Optional[float], w: int = 5) -> str:
    return ("%*.1f" % (w, x)) if x is not None else " " * (w - 1) + "-"


def table_rm(run: Run, tail: float) -> List[CaseRes]:
    res: List[CaseRes] = []
    for i, e in run.rm_cases():
        r = analyze_case(run, i, e, tail)
        if r:
            res.append(r)
    print("%3s %-24s %-5s %-9s %5s %-6s | %5s %5s | cmd rf/srv first dedup sup | late | %-44s | rf-tx(snd) | reply rf/srv first ver rf-tx(tgt)"
          % ("#", "case", "dir", "result", "lat", "ok/rej", "mockC", "mockR", "verdicts at receiver (via:verdict@+s late copy)"))
    for r in res:
        rftx = (("%.0f" % r.rf_tx) if r.rf_tx is not None else "-") + (" " + r.rf_drop if r.rf_drop else "")
        print(
            "%3d %-24s %-5s %-9s %5.1f %-6s | %s %s | %-7s %-5s %-5d %-3d | %-5s | %-44s | %-10s | %-7s %-5s %-3s %s"
            % (r.idx, r.case[:24], r.dirs, r.result, r.lat, "%s/%s" % (r.ok_d, r.rej_d), f1(r.mock_cmd), f1(r.mock_rep),
               fmt_via(r.cmd_in), r.first, r.dedup, r.suppressed, fmt_via(r.cmd_late), r.verdicts[:44], rftx,
               fmt_via(r.rep_in), r.rep_first, r.verified, ("%.0f" % r.rep_rf_tx) if r.rep_rf_tx is not None else "-")
        )
    return res


def med(xs0: List[Optional[float]]) -> str:
    xs = [x for x in xs0 if x is not None]
    return "n=%d median %.1f min %.1f max %.1f" % (len(xs), statistics.median(xs), min(xs), max(xs)) if xs else "n=0"


def summary_rm(run: Run, res: List[CaseRes]) -> None:
    print("\n== RM summary")
    sent = [r for r in res if r.ctr >= 0]
    for dirs in sorted({r.dirs for r in sent}):
        rs = [r for r in sent if r.dirs == dirs]
        sync = [r for r in rs if r.case == "rm:sync"]
        once = [r for r in rs if r.ok_d == 1 and r.rej_d == 0]
        dev = [r for r in rs if r not in once and r not in sync]
        print("%-5s sent %d  ok_d==1&rej_d==0: %d  sync (ok_d 0 by design): %d  deviating: %d  (refused before send: %d)"
              % (dirs, len(rs), len(once), len(sync), len(dev), sum(1 for r in res if r.dirs == dirs and r.ctr < 0)))
        for r in dev + [x for x in sync if x.rej_d]:
            print("      #%d %-26s %-9s ok_d=%s rej_d=%s  %s" % (r.idx, r.case[:26], r.result, r.ok_d, r.rej_d, r.verdicts[:70]))
    for node in OWN_CALLS:
        ex = collections.Counter(v.ctr for v in run.logs[node] if v.kind == "rm" and v.k in ("ok", "fail"))
        multi = {c: n for c, n in ex.items() if n > 1}
        ver = collections.Counter(v.k for v in run.logs[node] if v.kind == "rm_reply")
        rej = collections.Counter(v.k.replace("reject_", "") for v in run.logs[node] if v.kind == "rm" and v.k.startswith("reject_"))
        other = collections.Counter(v.k for v in run.logs[node] if v.kind == "rm" and not v.k.startswith("reject_"))
        print("%-9s executed (ok+fail) %d ctrs, any ctr executed >1x: %s ; replies verified=%d unverified=%d ; rejects %s ; other %s"
              % (node, len(ex), multi or "none", ver.get("1", 0), ver.get("0", 0), dict(rej), dict(other)))
    print("path timing (t_send = driver POST /rmsend returned):")
    print("  command DATA at mock, s after t_send:  %s" % med([r.mock_cmd for r in sent if r.mock_cmd is not None]))
    print("  reply   DATA at mock, s after t_send:  %s" % med([r.mock_rep for r in sent if r.mock_rep is not None]))
    print("  [RM];reply verified line, s after t_send (1 s log resolution): %s" % med([r.real_lat for r in sent if r.real_lat is not None and r.verified == "yes"]))
    print("  driver 'lat' (2 s poll): %s" % med([r.lat for r in sent if r.result in ("ok", "err")]))
    print("  sender TX-LoRa of the command, s after t_send: %s ; dropped from ring: %s"
          % (med([r.rf_tx for r in sent if r.rf_tx is not None]), dict(collections.Counter(r.rf_drop for r in sent if r.rf_drop)) or "none"))
    print("  target TX-LoRa of the reply,  s after t_send: %s" % med([r.rep_rf_tx for r in sent if r.rep_rf_tx is not None]))
    print("  first RF copy minus first server copy at the receiver, s: %s" % med([r.rf_delay for r in sent if r.rf_delay is not None]))
    print("  first command copy at receiver: %s ; first reply copy at sender: %s"
          % (dict(collections.Counter(r.first for r in sent)), dict(collections.Counter(r.rep_first for r in sent))))
    for dirs in sorted({r.dirs for r in sent}):
        rs = [r for r in sent if r.dirs == dirs]
        first_rf = []
        for r in rs:
            cs = [c for c in r.cmd_in + r.cmd_late if c.via == "rf"]
            if cs:
                first_rf.append(min(c.ev.t for c in cs) - run.events[r.idx]["t_send"])
        print("  %-5s sender TX-LoRa of command: %s | first RF copy at receiver: %s | target TX-LoRa of reply: %s | RF copy inside tail: %d of %d"
              % (dirs, med([r.rf_tx for r in rs]), med(first_rf), med([r.rep_rf_tx for r in rs]), sum(1 for r in rs if any(c.via == "rf" for c in r.cmd_in)), len(rs)))
    vc = collections.Counter()
    for r in sent:
        for c in r.cmd_in + r.cmd_late:
            vc[(c.via, "dup" if c.dup else (c.verdict or "?"))] += 1
    print("  verdict per command copy at the receivers (via, verdict): %s" % dict(sorted(vc.items())))


# ---------------------------------------------------------------------------------------------------- negative phase
def table_neg(run: Run) -> None:
    print("\n== negative phase (events + junk frames found in the logs)")
    for i, e in enumerate(run.events):
        if str(e.get("case", "")).startswith("neg:"):
            keys = {k: e[k] for k in ("result", "rej_d", "lock", "lockS", "ok_d", "reply") if k in e}
            print("  #%d %-30s %-8s %s" % (i, e["case"], e.get("dir", ""), keys))
    junk: Dict[Tuple[int, str], Dict[str, Any]] = {}
    for node in OWN_CALLS:
        for e in run.logs[node]:
            m = JUNK.search(e.text if e.kind != "bp" else e.text)
            if not m:
                continue
            j = junk.setdefault((int(m.group("ctr")), m.group("n")), {"tx": collections.Counter(), "nack": [], "rx": []})
            if e.kind == "bp" and e.k == "nack":
                j["nack"].append((node, e.t))
            elif e.kind in ("udp_tx", "rf_tx"):
                j["tx"][(node, "srv" if e.kind == "udp_tx" else "rf")] += 1
    for (ctr, n), j in sorted(junk.items(), key=lambda kv: kv[0][0]):
        print("  junk x%s ctr %d: sent %s  refused by sender queue (BP nack) %s" % (n, ctr, dict(j["tx"]) or "no TX line", [(a, dt.datetime.fromtimestamp(t).strftime("%H:%M:%S")) for a, t in j["nack"]] or "no"))
        for node in OWN_CALLS:
            cs = [c for c in run.copies[node] if c.ctr == ctr and not c.reply and "0123456789abcde" in c.ev.text]
            if cs:
                print("      at %-8s %s" % (node, "  ".join("%s@%s:%s%s" % (c.via, dt.datetime.fromtimestamp(c.ev.t).strftime("%H:%M:%S"), "dup" if c.dup else (c.verdict or "?"), "") for c in sorted(cs, key=lambda c: c.ev.t))))


# ---------------------------------------------------------------------------------------------------- plain traffic
def table_plain(run: Run, tail: float = 30.0) -> None:
    print("\n== plain traffic")
    for i, e in enumerate(run.events):
        if not str(e.get("case", "")).startswith("plain:"):
            continue
        tok = e["token"]
        t0, t1 = e["t_send"] - 2, e.get("t_end", e["t_send"] + 25) + tail
        src, to = e["src"], e["to"]
        rcv = None if to == "TEST" else to
        nn = re.search(re.escape(tok) + r"\{(\d+)", " ".join(x.raw for n in run.logs.values() for x in n if tok in x.raw) or "")
        nnn = int(nn.group(1)) if nn else None
        print("  #%d %s %s -> %s token %s nnn=%s" % (i, e["case"], src, to, tok, nnn))
        for node in OWN_CALLS:
            ev = [x for x in run.logs[node] if t0 <= x.t <= t1 and tok in x.raw]
            tx_udp = sum(x.kind == "udp_tx" for x in ev)
            tx_rf = sum(x.kind == "rf_tx" for x in ev)
            new = sum(x.kind == "new" for x in ev)
            nack = [x for x in run.logs[node] if x.kind == "bp" and x.k == "nack" and tok in x.text and t0 <= x.t <= t1]
            rf = [x for x in ev if x.kind == "rf_rx" and x.origin != node]
            sv = [x for x in ev if x.kind == "udp_rx" and x.origin != node]
            if node == src:
                print("     sender   %-8s NEW %d  TX-UDP %d  TX-LoRa %d  BP-nack %d %s" % (node, new, tx_udp, tx_rf, len(nack), ("(" + nack[0].text.split("txt;")[-1][:40] + ")") if nack else ""))
            else:
                srv_dups = 0
                dd = {x.msgid for x in ev}
                for x in sv:
                    srv_dups += any(d.kind == "dedup" and d.k == "DUP" and d.msgid == x.msgid and abs(d.seq - x.seq) <= 12 for d in run.logs[node])
                dmd = [x for x in run.logs[node] if x.kind == "dmdup" and nnn is not None and x.ctr == nnn and t0 <= x.t <= t1]
                rea = [x for x in run.logs[node] if x.kind == "reack" and nnn is not None and x.ctr == nnn and t0 <= x.t <= t1]
                fresh_rf = sum(x.dup == "n" for x in rf)
                fresh_sv = len(sv) - srv_dups
                print("     receiver %-8s RF copies %d (DUP:n %d, DUP:d %d)  server copies %d (dedup-dup %d)  DMDUP %d  REACK-LIMIT %d  -> copies passing the frame dedup: %d, accepted after DMDUP: %d"
                      % (node, len(rf), fresh_rf, len(rf) - fresh_rf, len(sv), srv_dups, len(dmd), len(rea), fresh_rf + fresh_sv, fresh_rf + fresh_sv - len(dmd) if e["case"] == "plain:dm" else fresh_rf + fresh_sv))
        if e["case"] == "plain:dm" and nnn is not None:
            sid = next((x.msgid for x in run.logs[src] if x.kind == "new" and tok in x.raw), "")
            acks = [x for x in run.logs[src] if x.kind in ("ack", "retx") and x.msgid == sid and t0 <= x.t <= t1]
            ackfr = [x for n in run.logs.values() for x in n if x.kind in ("new", "udp_tx", "rf_tx") and ":ack%d" % nnn in x.text and t0 <= x.t <= t1]
            print("     ACK for msg %s: markers at sender %s ; ack frames emitted %s" % (sid or "-", [(x.k or "RETX", dt.datetime.fromtimestamp(x.t).strftime("%H:%M:%S")) for x in acks] or "none", collections.Counter((x.node, x.kind) for x in ackfr) or "none"))


# ---------------------------------------------------------------------------------------------------- foreign frames
def table_foreign(run: Run) -> None:
    print("\n== foreign frames (origin = first path element not DK5EN-1 / DK5EN-90)")
    for node in OWN_CALLS:
        rf = [e for e in run.logs[node] if e.kind == "rf_rx" and e.typ != "A" and e.path]
        s1 = [e for e in rf if e.sflag == 1]
        f_s1 = [e for e in s1 if e.origin not in OWN_CALLS]
        f_all = [e for e in rf if e.origin not in OWN_CALLS]
        srv = [e for e in run.logs[node] if e.kind == "udp_rx" and e.path]
        f_srv = [e for e in srv if e.origin not in OWN_CALLS]
        tx = [e for e in run.logs[node] if e.kind == "rf_tx" and e.path]
        f_tx = [e for e in tx if e.origin not in OWN_CALLS]
        print("%-9s RF rx %d (S1 %d)  S1 & foreign origin: %d   foreign origin any S: %d   server rx %d, foreign %d   TX-LoRa %d, foreign origin %d"
              % (node, len(rf), len(s1), len(f_s1), len(f_all), len(srv), len(f_srv), len(tx), len(f_tx)))
        if f_s1:
            print("   example: %s" % (f_s1[0].raw[:150]))
    rows = [m for m in run.mock if m.ind == "DATA" and m.dir == "rx"]
    gate = [m for m in run.mock if m.ind == "GATE"]
    print("mock DATA in: %d  GATE out: %d  KEEP %d" % (len(rows), len(gate), sum(m.ind == "KEEP" for m in run.mock)))
    for ip, node in sorted(run.ip_to_node.items()):
        mine = [m for m in rows if m.ip == ip]
        own = [m for m in mine if m.origin in OWN_CALLS]
        print("   DATA from %-15s (%s): %d  (own-origin %d, foreign-origin %d)" % (ip, node, len(mine), len(own), len(mine) - len(own)))
        g = [m for m in gate if m.ip == ip]
        gf = [m for m in g if m.origin not in OWN_CALLS]
        print("   GATE to   %-15s (%s): %d  (foreign-origin %d)" % (ip, node, len(g), len(gf)))
    fo = [m for m in rows if m.origin not in OWN_CALLS]
    if fo:
        print("   example foreign DATA: %s" % (fo[0].body[:100]))


# ---------------------------------------------------------------------------------------------------- anomalies
def table_anom(run: Run) -> None:
    print("\n== anomalies per node (counts; first line of each)")
    for node in OWN_CALLS:
        evs = run.logs[node]
        print(" %s:" % node)
        # anomaly classes need the raw lines even for kinds that classify() ignored: re-read the file
        lines: List[str] = []
        with open(os.path.join(run.d, NODE_FILES[node]), errors="replace") as f:
            for line in f:
                m = PFX.match(line.rstrip("\n"))
                if m:
                    r = m.group(4)
                    lines.append(NODE_TS.sub("", r, count=1) if NODE_TS.match(r) else r)
        for name, rx in ANOM:
            hit = [ln for ln in lines if rx.search(ln)]
            if hit:
                print("   %-14s %4d  e.g. %s" % (name, len(hit), hit[0][:110]))
        tf = collections.Counter(m.group(1) for ln in lines for m in [TXFAIL.search(ln)] if m)
        if tf:
            print("   tx_fail values %s" % dict(tf))
        stat = [ln for ln in lines if ln.startswith("[LOG] STAT")]
        if stat:
            print("   STAT ringmax: %s ; drop: %s" % ([re.search(r"ringmax=(\S+)", s).group(1) for s in stat if re.search(r"ringmax=", s)], [re.search(r"drop=(\S+)", s).group(1) for s in stat if re.search(r"drop=", s)]))
        sl = [int(m.group(1)) for ln in lines for m in [re.search(r"ONRXDONE_SLOW ms=(\d+)", ln)] if m]
        if sl:
            print("   ONRXDONE_SLOW ms: min %d median %d max %d" % (min(sl), statistics.median(sl), max(sl)))
        rm_sf = [e for e in evs if e.kind == "rm" and e.k == "reply_send_failed"]
        if rm_sf:
            print("   RM reply send_failed: %d (rc %s) at %s" % (len(rm_sf), {e.extra["rc"] for e in rm_sf}, [dt.datetime.fromtimestamp(e.t).strftime("%H:%M:%S") for e in rm_sf]))


# ---------------------------------------------------------------------------------------------------- trace one case
def trace(run: Run, pat: str, tail: float) -> None:
    want_dir = None
    if "@" in pat:
        pat, want_dir = pat.split("@", 1)
    hits = [(i, e) for i, e in run.rm_cases() if pat in e["case"] and (want_dir is None or e["dir"] == want_dir)]
    for i, e in hits:
        src, dst = side(e["dir"])
        s = run.sender_ctr(src, dst, e["t_send"])
        if not s:
            print("#%d %s: no [RM];send line" % (i, e["case"]))
            continue
        ctr = s.ctr
        T = e["t_send"]
        print("\n(node lines: 1 s resolution, mock: ms; +rel = s after t_send)\n#%d %s %s  t_send=%s  driver lat %.1f  result %s  ok_d/rej_d %s/%s  ctr %d" % (i, e["case"], e["dir"], dt.datetime.fromtimestamp(T).strftime("%H:%M:%S.%f")[:-3], e.get("lat", 0), e["result"], e.get("ok_d"), e.get("rej_d"), ctr))
        rows: List[Tuple[float, int, str]] = []

        def add(t: float, tie: int, s_: str) -> None:
            prec = s_.startswith("mock")
            stamp = dt.datetime.fromtimestamp(t).strftime("%H:%M:%S.%f")[:12] if prec else dt.datetime.fromtimestamp(t).strftime("%H:%M:%S    ")
            rows.append((t, tie, "%s %+7.1f  %s" % (stamp, t - T, s_)))

        def short(txt: str) -> str:
            return re.sub(r"\b([0-9a-f]{6})[0-9a-f]{10}\b", r"\1..", txt)

        mids = set()
        for node in (src, dst):
            for x in run.logs[node]:
                if not (T - 2 <= x.t <= T + e.get("lat", 0) + tail + 25):
                    continue
                m = RMTEXT.match(x.text) if x.text else None
                if x.kind in ("udp_tx", "rf_tx", "udp_rx", "rf_rx", "new") and m and int(m.group("ctr")) == ctr:
                    mids.add(x.msgid)
                    lab = {"udp_tx": "TX-UDP", "rf_tx": "TX-LoRa", "udp_rx": "RX-UDP", "rf_rx": "RF rx", "new": "NEW"}[x.kind]
                    extra = ""
                    if x.kind == "rf_rx":
                        extra = " S%d RSSI %s DUP:%s" % (x.sflag, x.rssi, x.dup)
                    if x.kind == "udp_rx":
                        extra = " S%d" % x.sflag
                    add(x.t, x.seq, "%-9s %-7s x%s %s>%s %s%s" % (node, lab, x.msgid, x.path, x.dest, short(x.text), extra))
        for node in (src, dst):
            for x in run.logs[node]:
                if not (T - 2 <= x.t <= T + e.get("lat", 0) + tail + 25):
                    continue
                if x.kind in ("txlog", "dedup", "gwu") and x.msgid in mids:
                    if x.kind == "dedup" and x.k == "ADD":
                        continue
                    add(x.t, x.seq, "%-9s %s x%s %s" % (node, {"txlog": "TX-LOG wait=%dms q=%d" % (x.extra.get("wait", 0), x.extra.get("q", 0)) if x.kind == "txlog" else "", "dedup": "RX_DEDUP_" + x.k, "gwu": x.k}[x.kind], x.msgid, ""))
                if x.kind in ("rm_send", "rm_reply") and x.ctr == ctr:
                    add(x.t, x.seq, "%-9s %s" % (node, x.raw[:80]))
                if x.kind == "rm" and x.ctr == ctr:
                    add(x.t, x.seq, "%-9s %s" % (node, x.raw[:80]))
        for c in run.copies[dst] + run.copies[src]:
            if c.ctr == ctr and not c.reply and c.verdict and T - 2 <= c.ev.t <= T + e.get("lat", 0) + tail + 25:
                add(c.ev.extra.get("verdict_t", c.ev.t), c.ev.seq, "%-9s verdict '%s' belongs to the %s copy of x%s" % (c.ev.node, c.verdict, c.via, c.ev.msgid))
        for m in run.mock:
            if m.msgid in mids and T - 2 <= m.t <= T + e.get("lat", 0) + tail + 25 and m.ind in ("DATA", "GATE"):
                add(m.t, 0, "mock      %s %s %s x%s %s" % ("DATA in " if m.ind == "DATA" else "GATE out", "from" if m.ind == "DATA" else "to  ", m.ip, m.msgid, short(m.body)))
        for _, _, line in sorted(rows):
            print("  " + line)


# ---------------------------------------------------------------------------------------------------- main report
def report(d: str, tail: float) -> None:
    run = Run(d)
    print("run %s  (log times are local, 1 s resolution; mock times use mock.log start + udp-log offset)" % d)
    print("\n== RM cases (rf/srv = copies at the receiver in [t_send-2, t_send+lat+%.0f]; late = copies after the window)" % tail)
    res = table_rm(run, tail)
    summary_rm(run, res)
    table_neg(run)
    table_plain(run)
    table_foreign(run)
    table_anom(run)


# ---------------------------------------------------------------------------------------------------- selftest
def _ts(t: float) -> str:
    return dt.datetime.fromtimestamp(t).strftime("%H:%M:%S")


def selftest() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        base = dt.datetime(2026, 10, 7, 8, 36, 0).timestamp()
        os.makedirs(os.path.join(tmp, "mock"))
        T = base + 28.6  # t_send of the status command
        ev = [
            {"case": "web_identity", "dir": "DK5EN-1", "host": "192.168.68.71", "result": "ok", "t": base},
            {"case": "web_identity", "dir": "DK5EN-90", "host": "192.168.68.73", "result": "ok", "t": base},
            {"case": "rm:status", "dir": "1>90", "result": "ok", "lat": 2.2, "ver": 1, "reply": "ok v=4.40a", "t_send": T, "ok_d": 1, "rej_d": 0},
            {"case": "rm:name", "dir": "1>90", "result": "noanswer", "lat": 76.9, "ver": 0, "reply": "", "t_send": base + 41.0, "ok_d": 0, "rej_d": 2},
            {"case": "neg:junk x1", "dir": "1>90", "result": "UNEXPECTED", "rej_d": 0, "lock": 0, "t_send": base + 50},
            {"case": "plain:dm", "dir": "1>DK5EN-90", "result": "sent", "token": "gwb083700-dm1", "t_send": base + 60, "t_end": base + 85, "src": "DK5EN-1", "to": "DK5EN-90"},
        ]
        json.dump(ev, open(os.path.join(tmp, "events.json"), "w"))
        tag = "993e4f7e890e7b10"
        tag2 = "29612b6b8cad4f75"

        def L(t: float, s: str) -> str:
            return "%s %s\n" % (_ts(t), s)

        h1: List[str] = []
        h90: List[str] = []
        # status 1>90 (ctr 100): server copy first, RF copy 16 s later; reply over server
        h1.append(L(T, "[RM];send;DK5EN-90;ctr;100"))
        h1.append(L(T, "07:36:28 TX-UDP   070 : xEA25A006 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 100 status %s HW:43 MOD:8/8 FCS:10FA FW:40:a LH:AB" % tag))
        h90.append(L(T, "[UDP];rx;ip;192.168.68.64;port;1990;len;74;ms;1686947"))
        h90.append(L(T, "07:36:28 RX-UDP  070 : xEA25A006 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 100 status %s HW:43 MOD:8/8 FCS:10FA FW:40:a LH:AB" % tag))
        h90.append(L(T, "[MC-DBG] RX_DEDUP_NEW msg_id=EA25A006"))
        h90.append(L(T + 1, "[RM];ok;ctr;100"))
        h90.append(L(T + 1, "07:36:29 NEW-TXT 085 : x91A434CE H04 S0 T0 M00 DK5EN-90>DK5EN-1:RM1 100 ok v=4.40a %s HW:09 MOD:8/8 FCS:1553 FW:40:a LH:89" % tag2))
        h1.append(L(T + 1.4, "07:36:29 RX-UDP  085 : x91A434CE H04 S0 T0 M00 DK5EN-90>DK5EN-1:RM1 100 ok v=4.40a %s HW:09 MOD:8/8 FCS:1553 FW:40:a LH:89" % tag2))
        h1.append(L(T + 1.4, "[MC-DBG] RX_DEDUP_NEW msg_id=91A434CE"))
        h1.append(L(T + 1.5, "[RM];reply;DK5EN-90;ctr;100;verified;1"))
        h90.append(L(T + 16, "[MC-DBG] RX_DEDUP_NEW msg_id=EA25A006"))
        h90.append(L(T + 16, "07:36:44 [LOG] 070 : xEA25A006 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 100 status %s HW:43 MOD:8/8 FCS:10FA FW:40:a LH:AB RSSI:-45 SNR:6 DUP:n OWN:- t=1702084" % tag))
        h90.append(L(T + 16, "[RM];reject;replay"))
        # name 1>90 (ctr 101): rate reject of the server copy, RF copy dup
        t2 = base + 41
        h1.append(L(t2, "[RM];send;DK5EN-90;ctr;101"))
        h1.append(L(t2, "07:36:41 TX-UDP   068 : xEA25A015 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 101 name %s HW:43 MOD:8/8 FCS:1018 FW:40:a LH:AB" % tag))
        h90.append(L(t2 + 1, "07:36:42 RX-UDP  068 : xEA25A015 H04 S0 T0 M01 DK5EN-1>DK5EN-90:RM1 101 name %s HW:43 MOD:8/8 FCS:1018 FW:40:a LH:AB" % tag))
        h90.append(L(t2 + 1, "[MC-DBG] RX_DEDUP_NEW msg_id=EA25A015"))
        h90.append(L(t2 + 1, "[RM];reject;rate"))
        h90.append(L(t2 + 9, "[MC-DBG] RX_DEDUP_DUP msg_id=EA25A015 slot=1"))
        h90.append(L(t2 + 9, "07:36:50 [LOG] 068 : xEA25A015 H03 S1 T0 M01 DK5EN-1,DK5EN-98>DK5EN-90:RM1 101 name %s HW:43 MOD:8/8 FCS:1141 FW:40:a LH:AB RSSI:-66 SNR:7 DUP:d OWN:- t=1" % tag))
        # junk refused by the sender queue
        h1.append(L(base + 50, "[BP];nack;QRT;dst;DK5EN-90;ms;1;txt;QRT NOT SENT - RM1 555 status 0123456789abcde0"))
        # plain DM from Heltec, server copy + RF copy at the RAK, DMDUP, ack
        t3 = base + 60
        h1.append(L(t3, "07:37:00 NEW-TXT 050 : x91A43502 H04 S0 T0 M00 DK5EN-1>DK5EN-90:gwb083700-dm1{258 HW:43 MOD:8/8 FCS:0B4D FW:40:a LH:AB"))
        h1.append(L(t3, "07:37:00 TX-UDP   050 : x91A43502 H04 S0 T0 M00 DK5EN-1>DK5EN-90:gwb083700-dm1{258 HW:43 MOD:8/8 FCS:0B4D FW:40:a LH:AB"))
        h90.append(L(t3 + 1, "07:37:01 RX-UDP  050 : x91A43502 H04 S0 T0 M00 DK5EN-1>DK5EN-90:gwb083700-dm1{258 HW:43 MOD:8/8 FCS:0B4D FW:40:a LH:AB"))
        h90.append(L(t3 + 1, "[MC-DBG] RX_DEDUP_NEW msg_id=91A43502"))
        h90.append(L(t3 + 8, "[MC-DBG] RX_DEDUP_NEW msg_id=91A43502"))
        h90.append(L(t3 + 8, "07:37:08 [LOG] 050 : x91A43502 H04 S0 T0 M00 DK5EN-1>DK5EN-90:gwb083700-dm1{258 HW:43 MOD:8/8 FCS:0B4D FW:40:a LH:AB RSSI:-52 SNR:6 DUP:n OWN:- t=2"))
        h90.append(L(t3 + 8, "[DMDUP] from DK5EN-1 nnn:258"))
        h90.append(L(t3 + 9, "07:37:09 [LOG] 059 : x6AC5D656 H02 S1 T0 M01 OE1XAR-33,DK5EN-98>*:{CET}2026-10-07 06:50:09 HW:00 MOD:8/8 FCS:0E5D FW:00:# LH:AB RSSI:-44 SNR:6 DUP:n OWN:- t=3"))
        h90.append(L(t3 + 12, "[ETH];stall;udp_tx;ms;126;task;mcloop"))
        h1.append(L(t3 + 2, "[UDP-MSGID] ack_msg_id:91A43502 ACK...02"))
        h1.append(L(t3 + 3, "[RETX] DM-ACK for retid:19 stop retransmit msg-id:91A43502"))
        open(os.path.join(tmp, "DK5EN-1.log"), "w").write("".join(h1))
        open(os.path.join(tmp, "DK5EN-90.log"), "w").write("".join(h90))
        # mock: listen at base+1.000, DATA from .71 for the status command at T+0.2
        t0 = base + 1.0
        ml = "%s,%03d meshcom_mock.server event=listen host=0.0.0.0 port=1990 callsign=MOCK-SRV\n" % (dt.datetime.fromtimestamp(t0).strftime("%Y-%m-%d %H:%M:%S"), 0)
        ml += "%s,100 meshcom_mock.server event=keep addr=192.168.68.71:1990 gw=433A8968 callsign=DK5EN-1 ver=4.40a groups=[]\n" % dt.datetime.fromtimestamp(t0 + 1).strftime("%Y-%m-%d %H:%M:%S")
        ml += "%s,100 meshcom_mock.server event=keep addr=192.168.68.73:1337 gw=48A4690D callsign=DK5EN-90 ver=4.40a groups=[]\n" % dt.datetime.fromtimestamp(t0 + 1).strftime("%Y-%m-%d %H:%M:%S")
        open(os.path.join(tmp, "mock.log"), "w").write(ml)
        body = b"DK5EN-1>DK5EN-90:RM1 100 status " + tag.encode() + b"\0"
        fr = b":" + bytes.fromhex("06a025ea") + b"\x14" + body
        data = b"DATA433A8968DK5EN-1  4.40a   0   003" + fr
        gate = b"GATE" + fr
        assert len(b"DATA433A8968DK5EN-1  4.40a   0   003") == MOCK_HDR
        ul = "%9.3f rx 192.168.68.71:1990 ind=DATA len=%d %s\n" % (T - t0 + 0.2, len(data), data.hex())
        ul += "%9.3f tx 192.168.68.73:1337 ind=GATE len=%d %s\n" % (T - t0 + 0.21, len(gate), gate.hex())
        open(os.path.join(tmp, "mock", "udp-log.txt"), "w").write(ul)

        run = Run(tmp)
        # parsing
        assert [c.via for c in run.copies["DK5EN-90"] if c.ctr == 100] == ["srv", "rf"], run.copies["DK5EN-90"]
        c_srv, c_rf = [c for c in run.copies["DK5EN-90"] if c.ctr == 100]
        assert c_srv.verdict == "ok" and c_rf.verdict == "replay", (c_srv.verdict, c_rf.verdict)
        c101 = [c for c in run.copies["DK5EN-90"] if c.ctr == 101]
        assert [(c.via, c.dup, c.verdict) for c in c101] == [("srv", False, "rate"), ("rf", True, "")], c101
        m_ = [m for m in run.mock if m.ind == "DATA"][0]
        assert m_.msgid == "EA25A006" and m_.origin == "DK5EN-1", m_
        # per-case
        r = analyze_case(run, 2, ev[2], 12.0)
        assert r and r.ctr == 100 and r.first == "srv" and fmt_via(r.cmd_in) == "0/1" and fmt_via(r.cmd_late) == "1/0", r
        assert r.verified == "yes" and fmt_via(r.rep_in) == "0/1" and r.rep_first == "srv", r
        assert r.rf_delay is not None and round(r.rf_delay) == 16, r.rf_delay
        r2 = analyze_case(run, 3, ev[3], 12.0)
        assert r2 and r2.dedup == 1 and "srv:rate" in r2.verdicts and r2.verified == "-", r2
        print("--- selftest: report on the synthetic run")
        report(tmp, 12.0)
        trace(run, "rm:status", 12.0)
    print("selftest ok")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", nargs="?")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--tail", type=float, default=12.0, help="seconds after t_send+latency that count as in-window (default 12)")
    ap.add_argument("--trace", metavar="CASE[@DIR]", help="print the timeline of the matching RM case(s), e.g. rm:status@1>90")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.run_dir:
        ap.error("run dir required")
    if a.trace:
        trace(Run(a.run_dir), a.trace, a.tail)
        return 0
    report(a.run_dir, a.tail)
    return 0


if __name__ == "__main__":
    sys.exit(main())
