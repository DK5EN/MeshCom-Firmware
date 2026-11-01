"""Ring-slot fate accounting for one hb9 node, with an overhear test for vanished relay slots.

usage: python3 -I ring_fate.py <norm_log> <out_json> <own_call> [partner_call]

For every RING_WRITE, follow the slot: aired (RING_TX_READ with same slot and msg_id),
superseded in place (RING_SUPERSEDE old_id), or vanished (slot rewritten without a read).
For relay writes, count foreign copies (RX_DEDUP_DUP of the same msg_id whose MH-LoRa line has
>= 2 path tokens and whose last hop is not us) heard while the entry was queued.
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from common import dist, iter_lines, kv, hour_key  # noqa: E402

PATH_RE = re.compile(r"MH-LoRa\s+\d{3} . ?: ?x([0-9A-F]{8}) H(\d\d) S\d T\d M\d\d (\S+?)(?:>|\s)")


def main(path: str, out: str, own: str, partner: str = "") -> None:
    writes = []          # dicts
    slot_live = {}       # slot -> index in writes
    prev_in_slot = {}    # slot -> index of the entry just overwritten (for in-place supersede)
    dups = defaultdict(list)  # msg_id -> [(t, last_hop, ntok)]
    pend_dup = None      # (t, msg_id) waiting for MH-LoRa line
    reads = 0
    sup_events = []
    for n, t, b in iter_lines(path):
        if b.startswith("[MC-DBG] RING_WRITE "):
            d = kv(b)
            w = {"line": n, "t": t, "slot": int(d["slot"]), "mid": d["msg_id"], "type": d["type"],
                 "src": d.get("src"), "status": d["status"], "queued": d["queued"], "len": int(d.get("len", 0)), "fate": None,
                 "end": None, "prio": None}
            s = w["slot"]
            if s in slot_live:
                old = writes[slot_live[s]]
                if old["fate"] is None:
                    old["fate"] = "vanished"
                    old["end"] = t
                    old["end_is_upper_bound"] = True
                prev_in_slot[s] = slot_live[s]
            slot_live[s] = len(writes)
            writes.append(w)
        elif b.startswith("[MC-DBG] RING_PRIO "):
            d = kv(b)
            s = int(d["slot"])
            if s in slot_live:
                writes[slot_live[s]]["prio"] = int(d["prio"])
        elif b.startswith("[MC-DBG] RING_TX_READ "):
            d = kv(b)
            s = int(d["slot"])
            reads += 1
            if s in slot_live:
                w = writes[slot_live[s]]
                if w["mid"] == d["msg_id"] and w["fate"] is None:
                    w["fate"] = "aired"
                    w["end"] = t
                    w["lat_ms"] = int(d["lat"].rstrip("ms")) if "lat" in d else None
                    w["retry"] = int(d.get("retry", 0))
                    del slot_live[s]
                else:
                    w2 = {"line": n, "mid": d["msg_id"], "note": "read without matching live write"}
                    sup_events.append(w2)
        elif b.startswith("[MC-DBG] RING_SUPERSEDE slot="):
            d = kv(b)
            s = int(d["slot"])
            pi = prev_in_slot.get(s)
            if pi is not None and writes[pi]["mid"] == d["old_id"] and writes[pi]["fate"] == "vanished":
                writes[pi]["fate"] = "superseded"
            elif s in slot_live:
                w = writes[slot_live[s]]
                if w["mid"] == d["old_id"] and w["fate"] is None:
                    w["fate"] = "superseded"
                    w["end"] = t
        elif b.startswith("[MC-DBG] RX_DEDUP_DUP "):
            d = kv(b)
            pend_dup = (t, d["msg_id"], n)
        elif pend_dup is not None and "MH-LoRa" in b:
            m = re.search(r"x([0-9A-F]{8}) H\d\d S\d T\d M\d\d (.*?)>", b)
            if m and m.group(1) == pend_dup[1]:
                toks = m.group(2).split(",")
                dups[pend_dup[1]].append((pend_dup[0], toks[-1], len(toks)))
            pend_dup = None
        elif pend_dup is not None and t - pend_dup[0] > 1.0:
            pend_dup = None
    # open entries at end of log
    for w in writes:
        if w["fate"] is None:
            w["fate"] = "open_at_end"

    def foreign_between(w, t0, t1):
        c = 0
        for (td, last, ntok) in dups.get(w["mid"], []):
            if t0 < td <= t1 and ntok >= 2 and last != own:
                c += 1
        return c

    res = {"n_writes": len(writes), "reads": reads, "by_fate": Counter(w["fate"] for w in writes)}
    by_src = defaultdict(Counter)
    by_prio = defaultdict(Counter)
    for w in writes:
        by_src[w["src"]][w["fate"]] += 1
        by_prio[str(w["prio"])][w["fate"]] += 1
    res["by_src"] = {k: dict(v) for k, v in by_src.items()}
    res["by_prio"] = {k: dict(v) for k, v in by_prio.items()}

    # overhear test on relay writes
    ov = {}
    for fate in ("aired", "vanished", "superseded"):
        sel = [w for w in writes if w["src"] == "rx_relay" and w["fate"] == fate and w["end"]]
        with_foreign = 0
        ncopies = []
        for w in sel:
            c = foreign_between(w, w["t"], w["end"])
            ncopies.append(c)
            if c:
                with_foreign += 1
        ov[fate] = {"n": len(sel), "with_foreign_copy_while_queued": with_foreign,
                    "share_pct": round(100.0 * with_foreign / len(sel), 1) if sel else None}
    res["overhear_test_relay_writes"] = ov
    from common import air_s
    ar = [w for w in writes if w["src"] == "rx_relay" and w["fate"] == "aired"]
    af = [w for w in ar if foreign_between(w, w["t"], w["end"])]
    res["aired_relays_after_foreign_copy_while_queued"] = {"n": len(af), "of": len(ar), "airtime_s_upper": round(sum(air_s(w["len"]) for w in af), 1)}
    if partner:
        def partner_between(w):
            return sum(1 for (td, last, ntok) in dups.get(w["mid"], []) if w["t"] < td <= w["end"] and last == partner)
        ap = [w for w in ar if partner_between(w)]
        res["aired_relays_after_partner_copy_while_queued"] = {"partner": partner, "n": len(ap), "of": len(ar), "airtime_s_upper": round(sum(air_s(w["len"]) for w in ap), 1)}
        vp = [w for w in writes if w["fate"] == "vanished" and partner_between(w)]
        res["vanished_with_partner_copy"] = {"n": len(vp), "of": len([w for w in writes if w["fate"] == "vanished"])}
    if partner:
        gw = [w for w in writes if w["src"] == "udp_rx" and w["fate"] == "aired"]
        gwp = [w for w in gw if partner_between(w)]
        res["server_inject_udp_rx"] = {"aired": len(gw), "airtime_s": round(sum(air_s(w["len"]) for w in gw), 1),
                                       "aired_after_partner_copy_while_queued": len(gwp),
                                       "aired_after_any_foreign_copy_while_queued": sum(1 for w in gw if foreign_between(w, w["t"], w["end"]))}
    res["vanished_airtime_saved_s_upper"] = round(sum(air_s(w["len"]) for w in writes if w["fate"] == "vanished"), 1)
    # by prio / type for vanished
    van = [w for w in writes if w["fate"] == "vanished"]
    res["vanished_by_prio_type"] = {"p%s_t%s" % k: v for k, v in Counter((str(w["prio"]), w["type"]) for w in van).most_common(10)}
    res["vanished_per_hour"] = dict(sorted(Counter(hour_key(w["t"]) for w in van).items()))
    # window in which vanished entries were last queued: time to first foreign copy
    first_copy = []
    for w in van:
        ts = [td for (td, last, ntok) in dups.get(w["mid"], []) if td > w["t"] and ntok >= 2 and last != own]
        if ts:
            first_copy.append(min(ts) - w["t"])
    res["vanished_first_foreign_copy_after_write_s"] = dist(first_copy)
    # aired relay: wait distribution
    res["aired_lat_s_by_prio"] = {}
    for p in sorted({w["prio"] for w in writes if w["fate"] == "aired" and w.get("lat_ms") is not None}, key=lambda x: (x is None, x)):
        res["aired_lat_s_by_prio"][str(p)] = dist([w["lat_ms"] / 1000.0 for w in writes if w["fate"] == "aired" and w["prio"] == p and w.get("lat_ms") is not None])
    res["aired_lat_s_by_src"] = {}
    for sname in sorted({w["src"] for w in writes}):
        res["aired_lat_s_by_src"][sname] = dist([w["lat_ms"] / 1000.0 for w in writes if w["fate"] == "aired" and w["src"] == sname and w.get("lat_ms") is not None])
    res["retry_gt0"] = sum(1 for w in writes if w.get("retry", 0) > 0)
    res["reads_without_live_write"] = len(sup_events)
    res["reads_without_live_write_examples"] = sup_events[:5]
    # dump a compact write list for other checks
    res["write_list_path"] = out.replace(".json", "_writes.jsonl")
    with open(res["write_list_path"], "w") as fh:
        for w in writes:
            fh.write(json.dumps({k: w.get(k) for k in ("line", "t", "slot", "mid", "type", "src", "prio", "status", "queued", "len", "fate", "end", "lat_ms", "retry")}) + "\n")
    res["by_fate"] = dict(res["by_fate"])
    json.dump(res, open(out, "w"), indent=1, default=str)
    print(json.dumps({k: res[k] for k in res if k not in ("write_list_path","vanished_per_hour","aired_lat_s_by_prio","aired_lat_s_by_src","reads_without_live_write_examples","by_src","by_prio")}, indent=1, default=str))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else "")
