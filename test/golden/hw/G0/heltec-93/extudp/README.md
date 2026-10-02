# EXTUDP golden, Heltec-93 (test plan step H8)

Captured 2026-09-11. All 23 inbound corpus objects from
`test/golden/corpus/extudp/` were sent to the node on UDP 1799 at 3.5 s
intervals; `tools/bench/extudp_peer.py` recorded everything the node emitted.

| File                  | Content                                     |
| --------------------- | ------------------------------------------- |
| `extudp-sent.txt`     | the 23 corpus objects, in send order        |
| `extudp-received.txt` | every datagram the node emitted, normalized |

## Addressing was piloted before the full run

After the broadcast incident of the same day, two corpus entries were sent
first and what went on the air was inspected before the remaining 21. The
EXTUDP path takes its destination from the JSON `dst` field directly and has
no prefix-parsing step, so it carries none of the trap that made the BLE
corpus broadcast. Measured over the full run, the node emitted 13 messages to
`DK5EN-1`, 2 to group `9999`, 1 to `123456789` — and **zero to `*`**.

## What the capture happens to contain

- **Mesh relay, proven live:** one frame comes back as
  `"src":"DK5EN-93,DK5EN-92"` — T-Beam-92 relayed it.
- **Backpressure fired:** `QRS - slow down, TX buffer is filling` appears as a
  `src_type":"lora"` message. The corpus drives 23 sends in 80 s, which is
  enough to fill the TX ring, so the notice path is exercised without being
  asked for.
- **Both source types:** `"src_type":"node"` for what this node originated and
  `"src_type":"lora"` for the same frame heard back off the air, often from
  RAK-90 at -39 to -41 dBm.

Node restored afterwards; `--extudp` switched back off.

## Owed

The same capture on RAK-90 (nRF52) — the platform pair is the point of this
surface, and `extudp_functions.cpp` is shared code with `R1-06` and `R3-15`
against it.
