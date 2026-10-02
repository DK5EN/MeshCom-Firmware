# EXTUDP golden, RAK-90 (test plan step H8)

Captured 2026-09-11 — the nRF52 half, which is the point of this surface:
`extudp_functions.cpp` is shared code and both `R1-06` (entry buffer 500 → 264 B)
and `R3-15` (`JsonDocument` reserve) are aimed at it.

All 23 inbound corpus objects sent on UDP 1799 at 3.5 s intervals;
`tools/bench/extudp_peer.py` recorded everything the node emitted.

Originated by this node: 7 messages to `DK5EN-1`, 1 to group `9999`, **zero to
`*`**. The nRF52 takes its destination from the JSON `dst` field exactly as the
ESP32 does — no prefix-parsing step, so none of the trap that made the BLE
corpus broadcast.

The capture also contains inbound mesh traffic the node forwarded with
`"src_type":"lora"`, including a `{CET}` control payload from `OE1XAR-33` and a
group-20 message relayed through `DK5EN-98,DK5EN-92`. Those are other stations'
frames, not ours, and they are why the source IP is recorded per datagram.
