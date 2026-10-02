# UDP-1990 golden, T-Beam-92 (test plan steps H6 and H7)

Captured 2026-09-11 on the classic ESP32 — the third platform on this surface,
and the board sitting 20 bytes from an IRAM link failure. Same procedure as
the other two: the stub replayed the 37-datagram corpus, the node was pointed
at it with `--srvip 192.168.68.58` and put into `--gateway on --udplog on`.

What the node made of the corpus: 24 `DATA`, 8 `BEAT`, 3 `CET`, 2 `OTHER`,
1 `CONF`.

`udp-tx.bin` holds the node's own uploads: three `KEEP` and two `DATA` (76 and
73 bytes) — the acks it generated for the corpus DM. The lengths differ from
RAK-90's 85/85 because the `DATA` header carries the node's own callsign and
version, which is the point of capturing per node rather than once.

Restored and verified back to `DK5EN-92` afterwards; the corpus `CONF`
provisions the node, see `../../heltec-93/udp/README.md`.
