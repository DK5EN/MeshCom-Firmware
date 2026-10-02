# EXTUDP golden, T-Deck-14 (test plan step H8)

Captured 2026-09-11, completing this surface on all four bench nodes.

Originated by this node: 7 messages to `DK5EN-1`, 1 to group `9999`, **zero to
`*`** — identical to Heltec-93, RAK-90 and T-Beam-92 from the same 23-object
corpus.

Like T-Beam-92, this node had `node_extern = none` and no EXTUDP peer, so the
feature could not enable until one was set. Set to the Mac for the capture and
restored to `none` afterwards, verified by re-reading the configuration. Two of
the four bench nodes were in that state; the setting is not uniform across the
fleet and `--extudp on` does not say why it fails when the peer is unset.

Only datagrams whose source is `192.168.68.71` are kept: other nodes emit
EXTUDP into the same listener.
