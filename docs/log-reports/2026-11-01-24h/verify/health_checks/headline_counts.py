#!/usr/bin/env python3
"""Headline counts per node: TX_GATE_ENTER cad_attempt>=3 share, ACK receptions acknowledging
F5CE7xxx (OE3XWJ ping ids), '255.:{ping}' events, keepalives without a BEAT before the next keepalive,
RX_ERROR share of (RX_ERROR + ONRXDONE_TIME non-ACK OnRxDone lines).
Usage: python3 -I headline_counts.py <norm-dir>
"""
from __future__ import annotations
import re, sys
from pathlib import Path


def main() -> None:
    norm = Path(sys.argv[1])
    for n in ["oe3xhl", "oe3xia", "oe3xir", "oe3xoc", "oe3xwj", "db0ais"]:
        gate = ge3 = ack = ackp = p255 = keep = miss = err = done = 0; got = True
        with open(norm / f"{n}.log", encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                if "TX_GATE_ENTER" in ln and (m := re.search(r"cad_attempt=(\d+)", ln)):
                    gate += 1; ge3 += int(m.group(1)) >= 3
                elif "[LOG] 012 A x" in ln and "RSSI:" in ln:
                    ack += 1; ackp += bool(re.search(r"\[LOG\] 012 A x[0-9A-F]{8} H\w\w xF5CE7", ln))
                elif "255.:{ping}" in ln and "RING_TX_READ" not in ln:
                    p255 += 1 if "[LOG]" not in ln else 0
                elif "[GW];keep;tx;ok" in ln:
                    keep += 1; miss += 0 if got else 1; got = False
                elif "[GW];rx;type;BEAT" in ln:
                    got = True
                elif "[MC-DBG] RX_ERROR rssi=" in ln:
                    err += 1
                elif ln.rstrip().endswith("OnRxDone"):
                    done += 1
        print(f"{n}: TX_GATE_ENTER {gate}, cad>=3 {100*ge3/max(gate,1):.1f}%; ACK rx {ack}, F5CE7 {100*ackp/max(ack,1):.1f}%; "
              f"'255.:{{ping}}' lines {p255}; keepalives {keep}, no BEAT before next {miss} ({100*miss/max(keep,1):.2f}%); "
              f"RX_ERROR share of (err+OnRxDone) {100*err/max(err+done,1):.1f}%")


if __name__ == "__main__":
    main()
