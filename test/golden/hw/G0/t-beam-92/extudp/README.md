# EXTUDP golden, T-Beam-92 (test plan step H8)

Captured 2026-09-11 — the classic-ESP32 third platform, completing this surface
on three of four nodes.

Originated by this node: 7 messages to `DK5EN-1`, 1 to group `9999`, **zero to
`*`** — the same shape as Heltec-93 and RAK-90.

## This node had no EXTUDP peer configured

The first attempt captured nothing from it. `--extudp on` reported `EXTUDP off`
afterwards, and `--info` showed `EXT IP none`: unlike the other three nodes,
T-Beam-92's `node_extern` was never set, so the feature cannot enable. It was
set to the Mac for the capture (`--extudpip 192.168.68.58`) and restored to
`none` afterwards — verified by re-reading the configuration.

Worth knowing for any later EXTUDP work: the bench nodes are not uniformly
configured for this surface, and `--extudp on` fails quietly when the peer is
unset rather than reporting why.

## Cross-talk is filtered by source IP

RAK-90 was still emitting EXTUDP during this window, so its datagrams arrived
at the same listener. The stored capture keeps only datagrams whose source is
`192.168.68.72`. That is also why `extudp_peer.py` records the source per
datagram rather than assuming one node.
