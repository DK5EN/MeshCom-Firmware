#!/usr/bin/env python3
"""Extract all {CET} time-signal events from one node log into JSON lines.

Usage (via run.py): extract_cet.py <node>; reads <norm>/<node>.log, writes <out>/data/events_<node>.jsonl

Time base: every event gets `t` = UTC epoch seconds (float).
  cluster logs: logger wall clock minus LOGGER_UTC_OFF_S (3600 = CET), ms resolution.
  standalone logs (no wall clock): node clock HH:MM:SS of the last timestamped line, converted
            with the node's UTC offset and start date (--standalone-utc-off-h, --standalone-start;
            the offset steps by -1 h when the node clock jumps back by ~1 h), day rolled over at midnight wrap. Second resolution.
Event kinds: udp, rf, tx, txmeta (from [LOG] TX), cet (the [CET];* lines),
             ring (RING_* / RX_DEDUP_* / RLY / NBR with a CET msg_id), txdur (TX_START->OnTXDone).
"""
import json
import re
import sys
from datetime import datetime, timezone

from nodeset import LOGGER_UTC_OFF_S, NORM, OUT, STANDALONE, STANDALONE_START, STANDALONE_UTC_OFF_H

LOG_PREFIX = re.compile(rb"^(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$", re.S)
NODE_TS = re.compile(rb"^(\d\d):(\d\d):(\d\d) (.*)$", re.S)

RE_FRAME = re.compile(
    rb"^(?P<tag>\[LOG\]|RX-UDP|TX-LoRa|TX-Lora|MH-LoRa|RX-LoRa2?)\s+(?P<len>\d+) : x(?P<id>[0-9A-F]{8}) H(?P<h>[0-9A-F]{2}) S(?P<s>\d) T(?P<t>\d) M(?P<m>[0-9A-F]{2}) "
    rb"(?P<path>[^>]*)>(?P<dst>[^:]*):\{CET\}(?P<cont>\S+ \S+|<\S*)(?P<rest>.*)$")
RE_RFTAIL = re.compile(rb"FCS:(?P<fcs>[0-9A-F]{4}).*? RSSI:(?P<rssi>-?\d+) SNR:(?P<snr>-?\d+) DUP:(?P<dup>\w) OWN:(?P<own>\S) t=(?P<ms>\d+)")
RE_FCS = re.compile(rb"FCS:(?P<fcs>[0-9A-F]{4})")
RE_TX = re.compile(rb"^\[LOG\] TX x(?P<id>[0-9A-F]{8}) \S+ H(?P<h>[0-9A-F]{2}) prio=(?P<prio>\d) src=(?P<src>\w) wait=(?P<wait>\d+) q=(?P<q>\d+) cad=(?P<cad>\d+) len=(?P<len>\d+) t=(?P<ms>\d+)")
RE_CET = re.compile(rb"^\[CET\];(?P<kind>\w+);(?P<rest>.*)$")
RE_ID = re.compile(rb"(?<![0-9A-Fa-f])([0-9A-F]{8})(?![0-9A-Fa-f])")
RE_RINGKIND = re.compile(rb"(RING_[A-Z_]+|RX_DEDUP_[A-Z]+|OWN-TX-CHECK-UDP|checkOwnTx|\[NBR\]\|[A-Z]+|\[LOG\] RLY|\[LOG\] GW[IU]|Insert own_msg_id)")


def to_epoch(date: str, h: int, m: int, s: int) -> float:
    return datetime.strptime(date, "%Y-%m-%d").replace(tzinfo=timezone.utc).timestamp() + h * 3600 + m * 60 + s


def main() -> None:
    node = sys.argv[1]
    log = str(NORM / f"{node}.log")
    out = str(OUT / "data" / f"events_{node}.jsonl")
    is_db = node in STANDALONE
    db_date, DB_OFF_H = STANDALONE_START[:10], STANDALONE_UTC_OFF_H
    cet_ids = set()
    # pass 1: ids of all {CET} frames
    with open(log, "rb") as f:
        for line in f:
            if b"{CET}" in line and b" : x" in line:
                m = re.search(rb" : x([0-9A-F]{8}) H", line)
                if m:
                    cet_ids.add(m.group(1))
    ev = []
    # db0ais day tracking
    db_last_sec = None
    last_t = None
    pending_tx = None   # state machine for TX duration
    txstart_t = None
    with open(log, "rb") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip(b"\r\n")
            t = None
            node_clock = None
            body = line
            if not is_db:
                m = LOG_PREFIX.match(line)
                if not m:
                    continue
                d, H, M, S, ms, body = m.groups()
                t = to_epoch(d.decode(), int(H), int(M), int(S)) + int(ms) / 1000.0 - LOGGER_UTC_OFF_S
                m2 = NODE_TS.match(body)
                if m2:
                    node_clock = "%s:%s:%s" % tuple(x.decode() for x in m2.groups()[:3])
                    body = m2.group(4)
                last_t = t
            else:
                m2 = NODE_TS.match(line)
                if m2:
                    h_, mi_, s_ = int(m2.group(1)), int(m2.group(2)), int(m2.group(3))
                    sec = h_ * 3600 + mi_ * 60 + s_
                    if db_last_sec is not None and 3000 <= db_last_sec - sec <= 4200:
                        # node clock stepped back by 1 h (operator changed utcoff 2 -> 1 at ~17:20 UTC)
                        DB_OFF_H -= 1
                    elif db_last_sec is not None and sec < db_last_sec - 4200:
                        # midnight wrap
                        dt = datetime.strptime(db_date, "%Y-%m-%d")
                        db_date = datetime.fromtimestamp(dt.replace(tzinfo=timezone.utc).timestamp() + 86400, timezone.utc).strftime("%Y-%m-%d")
                    db_last_sec = sec
                    last_t = to_epoch(db_date, h_, mi_, s_) - DB_OFF_H * 3600
                    node_clock = "%02d:%02d:%02d" % (h_, mi_, s_)
                    body = m2.group(4)
                t = last_t
                if t is None:
                    continue
            # --- frames
            if b"{CET}" in body:
                mf = RE_FRAME.match(body)
                if mf:
                    g = mf.groupdict()
                    tag = g["tag"].decode()
                    path = g["path"].decode().split(",")
                    e = {"k": {"[LOG]": "rf", "RX-UDP": "udp", "TX-LoRa": "tx", "TX-Lora": "tx", "MH-LoRa": "mh", "RX-LoRa": "rxl", "RX-LoRa2": "rxl"}[tag],
                         "t": t, "ln": lineno, "nc": node_clock, "id": g["id"].decode(), "h": int(g["h"], 16), "path": path,
                         "cont": g["cont"].decode(), "len": int(g["len"])}
                    rest = g["rest"]
                    if e["k"] == "rf":
                        mt = RE_RFTAIL.search(rest)
                        if mt:
                            e.update(fcs=mt.group("fcs").decode(), rssi=int(mt.group("rssi")), snr=int(mt.group("snr")),
                                     dup=mt.group("dup").decode(), own=mt.group("own").decode(), ms=int(mt.group("ms")))
                    else:
                        mf2 = RE_FCS.search(rest)
                        if mf2:
                            e["fcs"] = mf2.group("fcs").decode()
                    if e["k"] != "mh":
                        ev.append(e)
                    continue
                else:
                    ev.append({"k": "unparsed", "t": t, "ln": lineno, "line": body[:200].decode(errors="replace")})
                    continue
            if body.startswith(b"[CET];"):
                mc = RE_CET.match(body)
                if mc:
                    fields = mc.group("rest").decode().split(";")
                    kv = {fields[i]: fields[i + 1] for i in range(0, len(fields) - 1, 2)}
                    ev.append({"k": "cet", "t": t, "ln": lineno, "kind": mc.group("kind").decode(), **kv})
                continue
            if body.startswith(b"[LOG] TX x"):
                mx = RE_TX.match(body)
                if mx and mx.group("id") in cet_ids:
                    g = mx.groupdict()
                    ev.append({"k": "txmeta", "t": t, "ln": lineno, "id": g["id"].decode(), "h": int(g["h"], 16), "prio": int(g["prio"]),
                               "src": g["src"].decode(), "wait": int(g["wait"]), "q": int(g["q"]), "cad": int(g["cad"]), "len": int(g["len"]), "ms": int(g["ms"])})
                    pending_tx = g["id"].decode()
                    txstart_t = None
                    continue
            if pending_tx is not None and not is_db:
                if b"[MC-DBG] TX_START" in body:
                    txstart_t = t
                elif body.startswith(b"OnTXDone"):
                    if txstart_t is not None:
                        ev.append({"k": "txdur", "t": t, "ln": lineno, "id": pending_tx, "dur_ms": round((t - txstart_t) * 1000)})
                    pending_tx = None
                    txstart_t = None
                elif body.startswith(b"[LOG] TX x"):
                    pending_tx = None
            # --- other lines mentioning a CET id
            if b"RING_" in body or b"RX_DEDUP" in body or b"[NBR]|" in body or b"RLY x" in body or b"GWI x" in body or b"GWU x" in body or b"checkOwnTx" in body or b"OWN-TX-CHECK" in body or b"Insert own" in body:
                ids = [i for i in RE_ID.findall(body) if i in cet_ids]
                if ids:
                    mk = RE_RINGKIND.search(body)
                    ev.append({"k": "ring", "t": t, "ln": lineno, "ids": [i.decode() for i in ids], "mk": (mk.group(1).decode() if mk else "?"), "line": body[:240].decode(errors="replace")})
                elif b"RING_DROP_STALE" in body or b"RING_SUPERSEDE" in body:
                    ev.append({"k": "ringany", "t": t, "ln": lineno, "line": body[:200].decode(errors="replace")})
    with open(out, "w") as fo:
        for e in ev:
            fo.write(json.dumps(e) + "\n")
    from collections import Counter
    print(node, len(cet_ids), "ids;", Counter(e["k"] for e in ev))


if __name__ == "__main__":
    main()
