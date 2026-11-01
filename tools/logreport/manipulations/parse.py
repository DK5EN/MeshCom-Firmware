#!/usr/bin/env python3
"""Parser: stream every <norm>/<node>.log into a compact per-node event file.

Output: <out>/work/<node>.jsonl  (one JSON object per event, key 'k' = kind, 't' = epoch seconds
from the logger prefix; standalone logs have no prefix -> 't' = node-clock seconds + 86400 * day wraps, or None).
Run via run.py.
"""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime

from nodeset import ALL, NORM, STANDALONE
from nodeset import OUT as OUT_ROOT

OUT = OUT_ROOT / "work"
OUT.mkdir(parents=True, exist_ok=True)

RE_TS = re.compile(rb"^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(\d{3})  (.*)$", re.S)
RE_RX = re.compile(
    r"(\d\d:\d\d:\d\d) \[LOG\] (\d{3}) ([:!@]) x([0-9A-F]{8}) H([0-9A-F]{2}) S(\d) T(\d) M(\d\d) (.*?)>(.*?) HW:(\w+) MOD:(\S+) FCS:([0-9A-F]{4}) FW:(\S+) LH:(\w+) RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w) OWN:(\S) t=(\d+)"
)
RE_ACK = re.compile(
    r"(\d\d:\d\d:\d\d) \[LOG\] (007|012) A x([0-9A-F]{8}) H([0-9A-F]{2}) (.*?) ?RSSI:(-?\d+) SNR:(-?\d+) DUP:(\w) OWN:(\S) t=(\d+)"
)
RE_RLY = re.compile(r"\[LOG\] RLY x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) q=(\S+) prio=(\d+) slot=(-?\d+)")
RE_TX = re.compile(
    r"\[LOG\] TX x([0-9A-F]{8}) (\S) H([0-9A-F]{2}) prio=(\d) src=(\w) wait=(\d+) q=(\d+) cad=(\d+) len=(\d+) t=(\d+)"
)
RE_GW = re.compile(r"\[LOG\] (GWI|GWU) x([0-9A-F]{8}) (\S) H([0-9A-F]{2})(?: from=(\S+))? t=(\d+)")
RE_STAT = re.compile(r"\[LOG\] STAT (.*)")
RE_ERR = re.compile(r"\[LOG\] ERR rssi=(-?\d+) snr=(-?\d+)")
RE_NODECLK = re.compile(r"^(\d\d:\d\d:\d\d) \[LOG\]")


def kv(s: str) -> dict:
    d = {}
    for m in re.finditer(r"(\w+)=(\S+)", s):
        d[m.group(1)] = m.group(2)
    return d


def parse_line(body: str) -> dict | None:
    """Return an event dict (without time) or None."""
    if "[LOG]" in body:
        m = RE_RX.search(body)
        if m:
            (nt, ln, typ, mid, hop, s, tr, me, path, rest, hw, mod, fcs, fw, lh, rssi, snr, dup, own, tms) = m.groups()
            if fcs == "0000":
                return {"k": "badfcs", "id": mid}
            # format is path>dest<typ>payload; 'rest' = dest + typ + payload
            dd = rest
            payload = ""
            idx = dd.find(typ)
            if idx >= 0:
                dst, payload = dd[:idx], dd[idx + 1:]
            else:
                dst = dd
            pl = path.split(",")
            return {"k": "rx", "id": mid, "typ": typ, "hop": int(hop, 16), "s": int(s), "path": pl, "dst": dst,
                    "dup": dup == "d", "own": own == "e", "rssi": int(rssi), "snr": int(snr), "len": int(ln),
                    "fcs": fcs, "lh": lh, "tms": int(tms), "nt": nt, "pl": payload[:60], "hw": hw, "fw": fw, "srv": int(s)}
        m = RE_ACK.search(body)
        if m:
            nt, ln, mid, hop, mid2, rssi, snr, dup, own, tms = m.groups()
            return {"k": "ack", "id": mid, "hop": int(hop, 16), "dup": dup == "d", "rssi": int(rssi), "snr": int(snr),
                    "tms": int(tms), "nt": nt, "ex": mid2}
        m = RE_RLY.search(body)
        if m:
            mid, typ, hop, q, prio, slot = m.groups()
            return {"k": "rly", "id": mid, "typ": typ, "hop": int(hop, 16), "q": q, "prio": int(prio), "slot": int(slot)}
        m = RE_TX.search(body)
        if m:
            mid, typ, hop, prio, src, wait, q, cad, ln, tms = m.groups()
            return {"k": "tx", "id": mid, "typ": typ, "hop": int(hop, 16), "prio": int(prio), "src": src,
                    "wait": int(wait), "q": int(q), "cad": int(cad), "len": int(ln), "tms": int(tms)}
        m = RE_GW.search(body)
        if m:
            kind, mid, typ, hop, frm, tms = m.groups()
            return {"k": kind.lower(), "id": mid, "typ": typ, "hop": int(hop, 16), "from": frm, "tms": int(tms)}
        m = RE_STAT.search(body)
        if m:
            d = kv(m.group(1))
            d["k"] = "stat"
            return d
        if RE_ERR.search(body):
            return {"k": "err"}
        return None
    if body.startswith("[NBR]|"):
        f = body.rstrip().split("|")
        kind = f[1]
        if kind == "NEED" and len(f) >= 9:
            return {"k": "need", "up": int(f[2]), "id": f[3], "typ": f[4], "case": f[5], "need": f[6], "alone": f[7],
                    "slot": int(f[8]), "inf": f[9] if len(f) > 9 else ""}
        if kind in ("CANCEL", "CANCEL?") and len(f) >= 8:
            return {"k": "cancel" if kind == "CANCEL" else "cancelq", "up": int(f[2]), "id": f[3], "typ": f[4],
                    "relayer": f[5], "before": f[6], "after": f[7], "inf": f[8] if len(f) > 8 else ""}
        if kind == "REFUSE" and len(f) >= 7:
            return {"k": "refuse", "up": int(f[2]), "id": f[3], "typ": f[4], "relayer": f[5], "alone": f[6]}
        if kind == "ECHO" and len(f) >= 6:
            return {"k": "echo", "up": int(f[2]), "id": f[3], "first": f[4], "second": f[5]}
        if kind == "SYM" and len(f) >= 8:
            return {"k": "sym", "up": int(f[2]), "id": f[3], "role": f[4], "x": f[5], "m": f[6], "snr": f[7]}
        if kind == "GW" and len(f) >= 6:
            return {"k": "nbrgw", "up": int(f[2]), "call": f[3], "v": f[4], "cause": f[5]}
        if kind == "ME" and len(f) >= 8:
            return {"k": "me", "up": int(f[2]), "from": f[3], "typ": f[4], "rssi": int(f[5]), "cnt": int(f[6]), "snr": f[7]}
        if kind == "EDGE" and len(f) >= 9:
            return {"k": "edge", "up": int(f[2]), "from": f[3], "to": f[4], "typ": f[5], "cnt": int(f[7]), "snr": f[8]}
        if kind == "SNAP":
            return {"k": "snap", "up": int(f[2]), "own": f[3], "rows": int(f[4])}
        if kind == "ROW" and len(f) >= 10:
            return {"k": "row", "up": int(f[2]), "idx": int(f[3]), "call": f[4], "flags": f[5], "age": f[6], "hearers": f[7],
                    "verdict": f[8], "meshneed": f[9]}
        if kind == "CHECK":
            return {"k": "check", "up": int(f[2]), "f": f[3:]}
        if kind == "DROP":
            return {"k": "nbrdrop", "up": int(f[2]), "reason": f[3], "path": f[4] if len(f) > 4 else ""}
        if kind == "CUT":
            return {"k": "cut", "up": int(f[2]), "ntok": f[3], "kept": f[4]}
        return None
    if body.startswith("[MC-DBG] "):
        if "RELAY_QUEUED" in body:
            m = re.search(r"msg_id=([0-9A-F]{8}) type=(\w+) len=(\d+)", body)
            if m:
                return {"k": "relayq", "id": m.group(1), "typ": m.group(2), "len": int(m.group(3))}
        elif "RELAY_LOOP_BLOCKED" in body:
            return {"k": "loopblk"}
        elif "RING_SUPERSEDE_KEEP" in body:
            return {"k": "supkeep"}
        elif "RING_SUPERSEDE" in body:
            m = re.search(r"slot=(\d+) prio=(\d) old_id=([0-9A-F]{8}) new_id=([0-9A-F]{8})", body)
            if m:
                return {"k": "supersede", "slot": int(m.group(1)), "prio": int(m.group(2)), "old": m.group(3), "new": m.group(4)}
        elif "RING_DROP_STALE" in body:
            m = re.search(r"slot=(\d+) prio=(\d) age_s=(\d+) msg_id=([0-9A-F]{8}) reason=(\w+)", body)
            if m:
                return {"k": "dropstale", "slot": int(m.group(1)), "prio": int(m.group(2)), "age": int(m.group(3)), "id": m.group(4), "reason": m.group(5)}
        elif "RING_DROP_PRIO" in body or "RING_DROP_NEW" in body:
            return {"k": "ringdrop", "raw": body[:100]}
        elif "RING_WRITE" in body:
            m = re.search(r"slot=(\d+) type=(\w+) status=(\w+) len=(\d+) msg_id=([0-9A-F]{8}) queued=(\d+)/(\d+) src=(\S+)", body)
            if m:
                return {"k": "ringwrite", "slot": int(m.group(1)), "typ": m.group(2), "id": m.group(5), "q": int(m.group(6)), "src": m.group(8)}
        elif "RING_TX_READ" in body:
            m = re.search(r"slot=(\d+) prio=(\d) type=(\w+) status=(\w+) len=(\d+) msg_id=([0-9A-F]{8}) retry=(\d+) queued=(\d+)/(\d+) lat=(\d+)ms", body)
            if m:
                return {"k": "txread", "slot": int(m.group(1)), "prio": int(m.group(2)), "id": m.group(6), "retry": int(m.group(7)), "lat": int(m.group(10))}
        elif "CSMA_AGED" in body:
            return {"k": "csmaaged"}
        elif "ACK_REJECT" in body:
            return {"k": "ackreject"}
        elif "RX_DEDUP_DUP" in body:
            m = re.search(r"msg_id=([0-9A-F]{8}) slot=(-?\d+)", body)
            if m:
                return {"k": "dedupdup", "id": m.group(1)}
        elif "RX_DEDUP_ADD" in body:
            m = re.search(r"msg_id=([0-9A-F]{8}) srv=(\d) slot=(\d+)/(\d+)", body)
            if m:
                return {"k": "dedupadd", "id": m.group(1), "srv": int(m.group(2)), "slot": int(m.group(3)), "size": int(m.group(4))}
        elif "CAD_BUSY" in body or "CAD_SAFETY" in body:
            return {"k": "cadbusy"} if "CAD_BUSY" in body else {"k": "cadsafety"}
        return None
    if body.startswith("[CET];"):
        f = body.split(";")
        return {"k": "cet", "f": f[1:6]}
    return None


bad: dict = {}


def main() -> None:
    for n in ALL:
        src = NORM / f"{n}.log"
        out = OUT / f"{n}.jsonl"
        cnt = 0
        day = 0
        prev_nt = None
        with open(src, "rb") as fh, open(out, "w") as fo:
            for raw in fh:
                t = None
                if n not in STANDALONE:
                    m = RE_TS.match(raw.rstrip(b"\r\n"))
                    if not m:
                        continue
                    y, mo, d, h, mi, s, ms, rest = m.groups()
                    t = datetime(int(y), int(mo), int(d), int(h), int(mi), int(s)).timestamp() + int(ms) / 1000.0
                    body = rest.decode("latin-1")
                else:
                    body = raw.rstrip(b"\r\n").decode("latin-1")
                try:
                    ev = parse_line(body)
                except (ValueError, IndexError):
                    ev = None
                    bad[n] = bad.get(n, 0) + 1
                if ev is None:
                    continue
                if n in STANDALONE:
                    mm = RE_NODECLK.match(body)
                    if mm:
                        hh, mi, ss = (int(x) for x in mm.group(1).split(":"))
                        sec = hh * 3600 + mi * 60 + ss
                        if prev_nt is not None and sec < prev_nt - 600:
                            day += 1
                        prev_nt = sec
                        ev["t"] = day * 86400 + sec
                    else:
                        ev["t"] = None
                else:
                    ev["t"] = t
                fo.write(json.dumps(ev, separators=(",", ":")) + "\n")
                cnt += 1
        print(n, cnt, "malformed", bad.get(n, 0), file=sys.stderr)


if __name__ == "__main__":
    main()
