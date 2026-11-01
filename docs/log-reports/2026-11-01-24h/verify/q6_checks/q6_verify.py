"""q6 adversarial checks on the event dump from q6_parse.py.

Usage: python3 -I q6_verify.py <ev.json>
Prints one block per claim (C1..C9). Airtime: LoRa formula SF11/BW250/CR4:6, preamble 8 (EU8 row,
src/country_profile.cpp:90), explicit header, CRC on, LDRO off (Tsym 8.19 ms < 16 ms).
"""
from __future__ import annotations

import json
import math
import statistics
import sys
from collections import Counter, defaultdict

NODES = ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj"]
CALL = {"oe3xhl": "OE3XHL-12", "oe3xia": "OE3XIA-12", "oe3xir": "OE3XIR-12", "oe3xoc": "OE3XOC-12", "oe3xwj": "OE3XWJ-12"}


def air_ms(plen: int, sf: int = 11, bw: float = 250e3, cr: int = 2, pre: int = 8) -> float:
    ts = (2 ** sf) / bw
    npay = 8 + max(math.ceil((8 * plen - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
    return ((pre + 4.25) + npay) * ts * 1000


def pct(a: float, b: float) -> str:
    return f"{100 * a / b:.1f} %" if b else "n/a"


def main() -> None:
    ev = json.load(open(sys.argv[1]))
    # ---------- C0 census + airtime instrument check
    print("== C0 census")
    tot_tx = 0
    tot_air = 0.0
    stat_sum = 0
    for n in NODES:
        tx = ev[n]["tx"]
        a = sum(air_ms(e["len"]) for e in tx)
        tot_tx += len(tx)
        tot_air += a
        stat_sum += ev[n]["stat_tx_ms"]
        src = Counter((e["typ"], e["src"]) for e in tx)
        print(n, "tx", len(tx), "air_s", round(a / 1000), "stat_tx_s", round(ev[n]["stat_tx_ms"] / 1000),
              "boot(first STAT up s)", ev[n]["stat_up_first"][1], dict(src))
    print("cluster tx", tot_tx, "air_s %.0f" % (tot_air / 1000), "STAT sum s %.0f" % (stat_sum / 1000))

    # ---------- C1 ACK attribution
    print("== C1 ACK / ping")
    ping_ids: set[str] = set()
    ping_ts: dict[str, list[float]] = defaultdict(list)
    for e in ev["oe3xwj"]["tx"]:
        if e["src"] == "o" and e["typ"] == ":":
            ping_ids.add(e["id"])
            ping_ts[e["id"]].append(e["ts"])
    reuse_gaps = sorted(round((v[-1] - v[0]) / 3600, 1) for v in ping_ts.values() if len(v) > 1)
    print("WJ ping TX events", sum(len(v) for v in ping_ts.values()), "distinct ids", len(ping_ts),
          "ids TX twice", len(reuse_gaps), "reuse gap h min/median/max", reuse_gaps[0], statistics.median(reuse_gaps), reuse_gaps[-1])

    def is_ping(acked: str | None, t: float) -> bool:
        return acked is not None and any(-5 <= t - x <= 1800 for x in ping_ts.get(acked, []))
    heard_ping_ids = {r["id"] for n in NODES for r in ev[n]["rx"] if r.get("ping")}
    print("WJ own ':' TX", len(ping_ids), "distinct; ping ids heard at others not in WJ TX set:",
          len(heard_ping_ids - ping_ids))
    ack_tx = [(n, e) for n in NODES for e in ev[n]["tx"] if e["typ"] == "A"]
    no_acked = sum(1 for _, e in ack_tx if e["acked"] is None)
    ack_ping = [(n, e) for n, e in ack_tx if is_ping(e["acked"], e["ts"])]
    ack_air = sum(air_ms(e["len"]) for _, e in ack_tx)
    ackp_air = sum(air_ms(e["len"]) for _, e in ack_ping)
    ping_tx = [e for e in ev["oe3xwj"]["tx"] if e["src"] == "o" and e["typ"] == ":" and e["id"] in ping_ids]
    ping_air = sum(air_ms(e["len"]) for e in ping_tx)
    # relays of the ping text itself by any of the five
    ping_rly = [(n, e) for n in NODES for e in ev[n]["tx"] if e["typ"] == ":" and e["src"] == "r" and e["id"] in ping_ids]
    print("ACK TX", len(ack_tx), "src", Counter(e["src"] for _, e in ack_tx), "acked-id missing", no_acked)
    print("ACK TX acking WJ ping", len(ack_ping), pct(len(ack_ping), len(ack_tx)))
    print("  share of cluster TX count: ACK-ping relays only", pct(len(ack_ping), tot_tx),
          "; + WJ pings", pct(len(ack_ping) + len(ping_tx), tot_tx),
          "; + ping text relays", len(ping_rly), pct(len(ack_ping) + len(ping_tx) + len(ping_rly), tot_tx))
    print("  share of cluster airtime: ACK-ping relays only", pct(ackp_air, tot_air), "(%.0f s)" % (ackp_air / 1000),
          "; + WJ pings", pct(ackp_air + ping_air, tot_air), "(%.0f s)" % ((ackp_air + ping_air) / 1000),
          "; all ACK", pct(ack_air, tot_air))
    print("  per node ACK-ping TX:", {n: sum(1 for m, _ in ack_ping if m == n) for n in NODES})
    # received ACK frames
    for n in NODES:
        racks = [r for r in ev[n]["rx"] if r["typ"] == "A"]
        rp = sum(1 for r in racks if is_ping(r["acked"], r["ts"]))
        print("  rx ACK frames", n, len(racks), "acking WJ ping", rp, pct(rp, len(racks)))
    # who originates: ACK ids per ping, hop byte, GW byte
    ids_per_ping: dict[str, set[str]] = defaultdict(set)
    maxhop: dict[str, int] = {}
    gwb = Counter()
    for n in NODES:
        for r in ev[n]["rx"]:
            if r["typ"] == "A" and is_ping(r["acked"], r["ts"]):
                ids_per_ping[r["acked"]].add(r["id"])
                h = int(r["h"], 16)
                maxhop[r["id"]] = max(maxhop.get(r["id"], 0), h & 0x7F)
                gwb[(r["gwb"], "srvbit" if h & 0x80 else "nosrv")] += 1
    k = [len(v) for v in ids_per_ping.values()]
    print("  pings with >=1 ACK heard", len(ids_per_ping), "of", len(ping_ids), "; distinct ACK ids per acked ping mean %.2f median %s max %s" % (
        statistics.mean(k), statistics.median(k), max(k)))
    print("  max hop seen per ACK id:", sorted(Counter(maxhop.values()).items()), "; byte10/server-bit:", dict(gwb))
    own_orig = sum(1 for _, e in ack_tx if e["src"] != "r")
    print("  ACK originated by the five (src!=r):", own_orig)

    # ---------- C2 NEED / CANCEL
    print("== C2 NEED/CANCEL")
    for n in NODES:
        nd = ev[n]["need"]
        c = Counter(x["case"] for x in nd)
        ck = Counter(x["kind"] for x in ev[n]["cancel"])
        print(n, "NEED", len(nd), dict(c), "CANCEL", dict(ck), "REFUSE", ev[n]["refuse"], "RLY q=", ev[n]["rly"], "RPTTX", ev[n]["rptx"])
    hl_relay_tx = sum(1 for e in ev["oe3xhl"]["tx"] if e["src"] == "r" and e["typ"] != "A")
    hl_q = ev["oe3xhl"]["rly"].get("tx", 0)
    ncan = sum(1 for x in ev["oe3xhl"]["cancel"] if x["kind"] == "CANCEL")
    ucan = len({x["id"] for x in ev["oe3xhl"]["cancel"] if x["kind"] == "CANCEL"})
    print("HL cancel", ncan, "distinct ids", ucan, "of RLY q=tx", hl_q, pct(ncan, hl_q), "; HL non-ACK relay TX", hl_relay_tx)

    # ---------- C7 sole provider: membership of OE3MIF-12 / OE3NRA-1 in alone sets, case A from boot
    print("== C7 sole-provider")
    for n in NODES:
        snaps = ev[n]["snaps"]
        si = 0
        hits = Counter()
        cnt = 0
        early = [x for x in ev[n]["need"] if x["min"] <= 60]
        early_a = sum(1 for x in early if x["case"] == "A")
        sizes = []
        for x in ev[n]["need"]:
            while si + 1 < len(snaps) and snaps[si + 1]["ts"] <= x["ts"]:
                si += 1
            if not snaps or snaps[si]["ts"] > x["ts"]:
                continue
            rows = {int(b): c for b, c in snaps[si]["rows"].items()}
            alone = int(x["alone"], 16)
            sizes.append(bin(alone).count("1"))
            cnt += 1
            for bit, call in rows.items():
                if alone >> bit & 1 and call in ("OE3MIF-12", "OE3NRA-1"):
                    hits[call] += 1
        print(n, "NEEDs mapped", cnt, "alone contains MIF", pct(hits["OE3MIF-12"], cnt), "NRA", pct(hits["OE3NRA-1"], cnt),
              "median alone size", statistics.median(sizes) if sizes else None,
              "| NEED in first 60 uptime-min", len(early), "case A", early_a,
              "| ME lines MIF", ev[n]["me"].get("OE3MIF-12", 0), "NRA", ev[n]["me"].get("OE3NRA-1", 0))

    # ---------- C3 relay TX after a foreign copy was heard while queued
    print("== C3 heard-while-queued")
    tot_hq_air = 0.0
    tot_hq_air_ack = 0.0
    for n in NODES:
        rxby: dict[tuple[str, str], list[int]] = defaultdict(list)
        for r in ev[n]["rx"]:
            if r["typ"] != "A" and r.get("last") == CALL[n]:
                continue  # own echo
            rxby[(r["typ"], r["id"])].append(r["t"])
        nrel = nhq = 0
        a_hq = a_ack = 0.0
        per_typ = Counter()
        for e in ev[n]["tx"]:
            if e["src"] != "r":
                continue
            nrel += 1
            tq = e["t"] - e["wait"]
            if any(tq + 300 < t < e["t"] for t in rxby.get((e["typ"], e["id"]), [])):
                nhq += 1
                a = air_ms(e["len"])
                a_hq += a
                per_typ[e["typ"]] += 1
                if e["typ"] == "A":
                    a_ack += a
        tot_hq_air += a_hq
        tot_hq_air_ack += a_ack
        print(n, "relay TX", nrel, "heard copy while queued", nhq, pct(nhq, nrel), dict(per_typ), "air_s %.0f" % (a_hq / 1000))
    print("cluster: %.0f s = %s of TX airtime (ACK %.0f s, other %.0f s)" % (
        tot_hq_air / 1000, pct(tot_hq_air, tot_air), tot_hq_air_ack / 1000, (tot_hq_air - tot_hq_air_ack) / 1000))

    # ---------- C5 audibility of HL
    print("== C5 is OE3XHL heard by the others")
    for n in NODES:
        if n == "oe3xhl":
            continue
        last_hl = sum(1 for r in ev[n]["rx"] if r.get("last") == "OE3XHL-12")
        src_hl = sum(1 for r in ev[n]["rx"] if r.get("src") == "OE3XHL-12")
        print(n, "rx lines with last hop OE3XHL-12:", last_hl, "; originated by HL:", src_hl, "; ME lines HL:", ev[n]["me"].get("OE3XHL-12", 0))
    hl_tx = [e for e in ev["oe3xhl"]["tx"] if e["typ"] != "A"]
    for n in NODES:
        if n == "oe3xhl":
            continue
        idx: dict[str, list[float]] = defaultdict(list)
        for r in ev[n]["rx"]:
            if r.get("last") == "OE3XHL-12":
                idx[r["id"]].append(r["ts"])
        hit = sum(1 for e in hl_tx if any(0 <= t - e["ts"] <= 3 for t in idx.get(e["id"], [])))
        print("  HL non-ACK TX", len(hl_tx), "logged at", n, "with last hop HL within 3 s:", hit)
    # reverse: what does HL hear from the others as last hop
    for m in NODES:
        if m == "oe3xhl":
            continue
        print("  HL rx lines with last hop", CALL[m], sum(1 for r in ev["oe3xhl"]["rx"] if r.get("last") == CALL[m]))

    # ---------- C6 re-relays and dedup window
    print("== C6 re-relays / dedup window")
    for n in NODES:
        seen: dict[str, list[float]] = defaultdict(list)
        for e in ev[n]["tx"]:
            if e["src"] == "r":
                seen[e["id"]].append(e["ts"])
        gaps = sorted(round(v[i + 1] - v[i]) for v in seen.values() for i in range(len(v) - 1))
        dur_min = (ev[n]["last_ts"] - ev[n]["first_ts"]) / 60
        rate = ev[n]["dedup_add"] / dur_min
        print(n, "same id relayed twice: gaps s", gaps, "| dedup adds/min %.2f -> 100-slot window %.1f min" % (rate, 100 / rate))
    # path loop: own call appears in path of a received frame earlier than last position
    print("loop guard RLY q=loop per node:", {n: ev[n]["rly"].get("loop", 0) for n in NODES})

    # ---------- C8 gateway inject redundancy
    print("== C8 inject redundancy")
    inj: dict[str, list[tuple[float, str, int]]] = defaultdict(list)
    for n in NODES:
        for e in ev[n]["tx"]:
            if e["src"] == "g":
                inj[e["id"]].append((e["ts"], n, e["len"]))
    hist = Counter(len({x[1] for x in v}) for v in inj.values())
    red = sum(sum(air_ms(x[2]) for x in v) - air_ms(v[0][2]) for v in inj.values())
    allinj = sum(air_ms(x[2]) for v in inj.values() for x in v)
    print("distinct inject ids", len(inj), "nodes-per-id", sorted(hist.items()), "4-5 nodes", hist[4] + hist[5],
          "airtime %.0f s, redundant %.0f s = %s" % (allinj / 1000, red / 1000, pct(red, tot_air)))


if __name__ == "__main__":
    main()
