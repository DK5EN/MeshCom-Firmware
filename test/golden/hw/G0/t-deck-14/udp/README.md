# UDP-1990 golden, T-Deck-14 (test plan step H6)

Captured 2026-09-11, completing this surface on all four bench nodes. Same
procedure as the other three: the stub replayed the 37-datagram corpus, the
node was pointed at it with `--srvip 192.168.68.58` and put into
`--gateway on --udplog on`.

What the node made of the corpus: 24 `DATA`, 5 `BEAT`, 3 `CET`, 2 `OTHER`,
1 `CONF` — the same classification as the other three, with only the `BEAT`
count varying by KEEP cadence.

`udp-tx.bin` holds four `KEEP` and **no `DATA`**. That is correct and worth
stating, because RAK-90 and T-Beam-92 each uploaded two: the corpus carries a
direct message addressed to `DK5EN-90`, so only a node that considers itself
the addressee generates an ack to upload. T-Deck-14 is `DK5EN-14` and rightly
stays quiet. A shared `D1-01` handler has to preserve that asymmetry, which is
only visible because the surface was captured per node rather than once.

Node restored afterwards; the corpus `CONF` provisions the node, see
`../../heltec-93/udp/README.md`.
