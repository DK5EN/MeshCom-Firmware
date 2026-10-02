# UDP-1990 golden, RAK-90 (test plan steps H6 and H7)

Captured 2026-09-11 on the nRF52/Ethernet path — the one the `--srvip` hook
(`ee545088`) was written to make reachable at all. Same procedure as
`heltec-93/udp/`: the stub replayed the 37-datagram corpus, the node was
pointed at it with `--srvip 192.168.68.58` and put into
`--gateway on --udplog on`.

What the node made of the corpus: 26 `DATA`, 4 `BEAT`, 3 `CET`, 2 `OTHER`,
1 `CONF`. The `DATA`/`BEAT` split differs from Heltec-93's 24/7 only because
the KEEP cadence fell differently; the classification of every corpus entry
agrees.

## H7 is here too, unplanned

`udp-tx.bin` holds four datagrams the node sent on its own:

```
KEEP len=46
DATA len=85     <- ack for the corpus DM addressed to DK5EN-90
DATA len=85
KEEP len=46
```

The corpus contains a direct message to `DK5EN-90`; the node acked it and
uploaded the ack to the server as `DATA`. That is genuine node -> server
gateway traffic, recorded byte-exact, without any LoRa injection. A full H7
still needs `--injectraw` so the upload path is exercised for frames the node
heard on the air rather than ones it generated itself.

## EXTUDP came along for free

The node forwards every received frame to the EXTUDP peer, so
`udp-rx-log.txt` also carries the `[EXT] Out:` JSON for each one —
`"src_type":"udp"` rather than `"lora"`. That is a first sample of the EXTUDP
surface of test plan step H8, though not a substitute for driving it properly
with `tools/bench/extudp_peer.py`.

## The CONF entry provisions the node

As on Heltec-93, the corpus CONF renamed this node to `DK5EN-1` / `BNCH`. The
nRF52 CONF path is the one doc 11 §2.2 always described correctly; what the
Heltec run disproved was the claim that the ESP32 has no such branch. Restored
and verified back to `DK5EN-90` / `5EN40`.

## USB note

The RAK4631's CDC port drops mid-capture and comes back about a second later.
The first attempt at this capture lost its entire node-side log to that;
`serial_drive.py` now reopens the port and reports how many drops it
recovered. This run: one. The node itself never stopped — the web server
answered throughout — so this is a host-side USB artefact, not the frozen-loop
symptom that `rak4631-serial-testing-pitfalls` warns about.
