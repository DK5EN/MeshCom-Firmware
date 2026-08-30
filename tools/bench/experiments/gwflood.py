#!/usr/bin/env python3
"""TM-31 (upstream #568): a LoRa gateway fed UDP messages faster than it can
radiate -- does it drop, and at which inter-arrival?

Setup (all on the bench LAN, run from tools/bench/runs/):
  * this script hosts the mock MeshCom server (tools/mock/meshcom_server.py)
    on --server-port and points the gateway node at it with the fork-only
    `--srvip <this host>` hook (RAM only; UDP is re-initialised at once);
  * the gateway (default Heltec V3 DK5EN-93, USB) gets --loradebug on so every
    LoRa transmission prints `TX-LoRa ... x<msg_id>`;
  * the observer (default T-Beam DK5EN-92, USB) gets --loradebug on so every
    reception prints `RX-LoRa2 ... x<msg_id>`;
  * for every inter-arrival in --gaps the server sends --per-gap GATE frames
    (corpus frame f001, a foreign position beacon, with a fresh msg_id and a
    recomputed FCS per frame) to the gateway and records the send time.

Per gap the report gives ingress / gateway TX / observer RX counts and the
ingress-to-TX latency -- the number #568 never had.

  python3 ../experiments/gwflood.py --gaps 8,4,2,1,0.5 --per-gap 6
"""
import argparse
import json
import random
import re
import socket
import statistics
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))            # tools/bench
sys.path.insert(0, str(HERE.parent.parent / "mock"))  # tools/mock
from tdeck_harness import TDeckSession  # noqa: E402
from meshcom_server import MockMeshComServer  # noqa: E402

F001 = bytes.fromhex(
    "21AB13F1E991444C324A412D312C444C324A412D323E2A21343832352E33354E5C30313134372E3139452D"
    "4D61727A6C696E67235765726E65722F523D393B002B88132F23AB707E"
)
FCS_AT = 66   # sum of bytes [0:66] big-endian at [66:68], trailer 23 AB 70 7E follows


def frame_with_id(msg_id: int) -> bytes:
    b = bytearray(F001)
    b[1:5] = msg_id.to_bytes(4, "little")
    fcs = sum(b[:FCS_AT]) & 0xFFFF
    b[FCS_AT:FCS_AT + 2] = fcs.to_bytes(2, "big")
    return bytes(b)


def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("192.168.68.1", 1))
    ip = s.getsockname()[0]
    s.close()
    return ip


def _new_session(port: str, tag: str) -> TDeckSession:
    s = TDeckSession(port=port, boot_timeout=45.0, ready_timeout=60.0,
                     log_path=Path(f"gwflood_{tag}_{time.strftime('%Y%m%d-%H%M%S')}.log"))
    s.probe_cmd = "--oledstat"
    s.probe_pattern = r"\[OLEDSTAT\]"
    s.wake_cmd = None
    return s


def _nudge_reboot(port: str) -> None:
    """Opening the port resets most ESP32 bench nodes -- but not all of them
    (the T-Beam v1.2 kept running through it on 2026-08-30, so CLIENT STARTED
    never came and the session timed out). Send --reboot over a throwaway
    connection so the retry sees a real boot."""
    import serial  # noqa: PLC0415  -- optional dependency, same as tdeck_harness
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        ser.write(b"--reboot\r\n")
        ser.flush()
        time.sleep(1.0)
    finally:
        ser.close()
    time.sleep(2.0)


def open_node(port: str, tag: str) -> TDeckSession:
    s = _new_session(port, tag)
    try:
        s.open()
        return s
    except TimeoutError:
        print(f"[{tag}] no boot marker after opening {port}; forcing --reboot", file=sys.stderr)
        s.close()
    _nudge_reboot(port)
    s = _new_session(port, tag)
    s.open()
    return s


def ids_with_times(session: TDeckSession, since: int, tag: str) -> Dict[int, float]:
    """msg_id -> monotonic timestamp of the first line carrying it.

    Records are (t_wall, t_mono, line); the latency below is measured against
    time.monotonic(), so it must use the second field. Using t_wall printed the
    absolute epoch instead of a latency (seen 2026-08-30)."""
    out: Dict[int, float] = {}
    for _, t, l in session.records_since(since):
        if tag in l:
            m = re.search(r"\bx([0-9A-Fa-f]{8})\b", l)
            if m:
                out.setdefault(int(m.group(1), 16), t)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gateway-port", default="/dev/cu.usbserial-0001")
    ap.add_argument("--observer-port", default="/dev/cu.usbserial-573C0005841")
    ap.add_argument("--server-port", type=int, default=1990)
    ap.add_argument("--gaps", default="10,8,6,4,3,2,1.5,1,0.5", help="inter-arrival seconds, comma list")
    ap.add_argument("--per-gap", type=int, default=6, help="frames per inter-arrival group")
    ap.add_argument("--settle", type=float, default=180.0,
                    help="seconds to wait after the last frame -- the TX queue drains slowly, "
                         "a short settle counts still-queued frames as lost")
    ap.add_argument("--out", default="gwflood.json")
    args = ap.parse_args()
    gaps = [float(x) for x in args.gaps.split(",") if x.strip()]

    host = local_ip()
    import logging
    logging.basicConfig(level=logging.DEBUG, format="%(asctime)s %(name)s %(message)s", stream=sys.stderr)
    server = MockMeshComServer(host="0.0.0.0", port=args.server_port, callsign="MOCK-SRV", verbose=True)
    server.start()
    print(f"mock server on {host}:{args.server_port}", file=sys.stderr)

    obs = open_node(args.observer_port, "observer")
    obs.send("--loradebug on")
    time.sleep(0.3)

    gw = open_node(args.gateway_port, "gateway")
    idx_info = gw.send("--info")
    gw.collect(2.0)
    info = [l for _, _, l in gw.records_since(idx_info)]
    gw_was_on = any(re.search(r"Gateway on", l) for l in info)
    if not gw_was_on:
        # bGATEWAY is read from the settings at boot: enable, save, reboot
        gw.send("--gateway on")
        time.sleep(1.5)
        gw.close()
        time.sleep(1.0)
        gw = open_node(args.gateway_port, "gateway")
    # TD-01: the first join fails on roughly half of the boots; wait for an IP
    if not any("got_ip" in l or "now listening" in l for _, _, l in gw.records_since(0)):
        t_ip = time.monotonic() + 150.0
        while time.monotonic() < t_ip:
            gw.collect(2.0)
            if any("got_ip" in l for _, _, l in gw.records_since(0)):
                break
    gw.send("--loradebug on")
    time.sleep(0.3)
    # TM-31: without this the UDP receive path prints nothing at all (DEBUG_MSG
    # is compiled away, DO_DEBUG 0), so "0 frames radiated" could not be told
    # apart from "0 frames arrived".
    gw.send("--udplog on")
    time.sleep(0.3)
    gw.send("--instreset")
    time.sleep(0.3)
    gw.send(f"--srvip {host}")
    # the gateway registers with a KEEP; wait for it
    t_wait = time.monotonic() + 90.0
    client_addr: Optional[Tuple[str, int]] = None
    while time.monotonic() < t_wait:
        gw.collect(1.0)
        if server.clients:
            client_addr = next(iter(server.clients.keys()))
            break
    if client_addr is None:
        print("gateway never sent KEEP to the mock server -- is WiFi up and the gateway enabled?", file=sys.stderr)
        server.stop()
        gw.close()
        obs.close()
        return 2
    print(f"gateway registered from {client_addr}", file=sys.stderr)

    base = random.getrandbits(24) << 8
    sent: List[Dict[str, Any]] = []
    idx_gw = gw.length()
    idx_obs = obs.length()
    n = 0
    for gap in gaps:
        for k in range(args.per_gap):
            msg_id = (base + n) & 0xFFFFFFFF
            n += 1
            server.send_gate(frame_with_id(msg_id), client_addr)
            sent.append({"gap": gap, "k": k, "msg_id": msg_id, "t_send": time.monotonic()})
            t_end = time.monotonic() + gap
            while time.monotonic() < t_end:
                gw.collect(min(0.2, max(0.01, t_end - time.monotonic())))
                obs.collect(0.05)
        # let the queue drain between groups
        t_end = time.monotonic() + 8.0
        while time.monotonic() < t_end:
            gw.collect(0.2)
            obs.collect(0.05)
    t_end = time.monotonic() + args.settle
    while time.monotonic() < t_end:
        gw.collect(0.5)
        obs.collect(0.1)
    idx_i = gw.send("--instr")
    gw.collect(1.5)
    instr = [l.strip()[:120] for _, _, l in gw.records_since(idx_i) if "INSTR" in l]
    gaps_seen = [l.strip()[:120] for _, _, l in gw.records_since(idx_gw) if "[INSTR-LOOP]" in l and "gap" in l]

    # TM-31 arrival evidence: [UDP];rx counts what the socket actually saw,
    # RING_WRITE src=udp_rx what the firmware queued for LoRa. ingress > rx_seen
    # means the datagram was lost before the firmware; rx_seen > queued means the
    # UDP->LoRa path dropped it; queued > tx means the TX queue dropped it.
    udp_rx_seen = [l.strip() for _, _, l in gw.records_since(idx_gw) if "[UDP];rx" in l]
    queued_udp = [l.strip() for _, _, l in gw.records_since(idx_gw)
                  if "RING_WRITE" in l and "src=udp_rx" in l]
    # the stage #568 is actually about: the TX ring is 20 slots, and once it
    # saturates the firmware discards the NEW frame (RING_DROP_NEW) or evicts a
    # lower-priority one already queued (RING_DROP_PRIO, e.g. the node's own HEY)
    ring_drops = [l.strip() for _, _, l in gw.records_since(idx_gw)
                  if "RING_DROP_NEW" in l or "RING_DROP_PRIO" in l]
    idx_u = gw.send("--udpstat")
    gw.collect(1.5)
    udpstat = [l.strip() for _, _, l in gw.records_since(idx_u) if "[UDPSTAT]" in l]

    tx = ids_with_times(gw, idx_gw, "TX-LoRa")
    rx = ids_with_times(obs, idx_obs, "RX-LoRa2")
    groups: Dict[float, Dict[str, Any]] = {}
    for row in sent:
        g = groups.setdefault(row["gap"], {"gap_s": row["gap"], "ingress": 0, "tx": 0, "rx": 0, "tx_lat_s": []})
        g["ingress"] += 1
        if row["msg_id"] in tx:
            g["tx"] += 1
            g["tx_lat_s"].append(round(tx[row["msg_id"]] - row["t_send"], 2))
        if row["msg_id"] in rx:
            g["rx"] += 1
    table = []
    for gap in gaps:
        g = groups[gap]
        lat = g["tx_lat_s"]
        table.append({"gap_s": gap, "ingress": g["ingress"], "tx": g["tx"], "rx": g["rx"],
                      "tx_lat_med_s": round(statistics.median(lat), 2) if lat else None,
                      "tx_lat_max_s": max(lat) if lat else None})
    gw.send("--loradebug off")
    obs.send("--loradebug off")
    if not gw_was_on:
        gw.send("--gateway off")
    gw.send("--srvip 0.0.0.0")
    time.sleep(0.5)
    gw.close()
    obs.close()
    server.stop()

    print(f"\n{'gap_s':>6} {'in':>3} {'tx':>3} {'rx':>3} {'tx_lat_med':>10} {'tx_lat_max':>10}")
    for r in table:
        print(f"{r['gap_s']:6.1f} {r['ingress']:3d} {r['tx']:3d} {r['rx']:3d} {str(r['tx_lat_med_s']):>10} {str(r['tx_lat_max_s']):>10}")
    print(f"total ingress {len(sent)}  udp_rx_seen {len(udp_rx_seen)}  queued {len(queued_udp)}"
          f"  ring_drops {len(ring_drops)}"
          f"  tx {len([r for r in sent if r['msg_id'] in tx])}  rx {len([r for r in sent if r['msg_id'] in rx])}")
    for l in ring_drops[:12]:
        print("  " + l)
    for l in udpstat:
        print("  " + l)
    for l in instr:
        print("  " + l)
    for l in gaps_seen[:10]:
        print("  " + l)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump({"host": host, "client": client_addr, "table": table, "sent": sent,
                   "tx_ids": sorted(tx), "rx_ids": sorted(rx), "instr": instr, "loop_gaps": gaps_seen,
                   "udp_rx_seen": udp_rx_seen, "queued_udp": queued_udp, "udpstat": udpstat,
                   "ring_drops": ring_drops}, f, indent=2, default=str)
    print(f"written {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
