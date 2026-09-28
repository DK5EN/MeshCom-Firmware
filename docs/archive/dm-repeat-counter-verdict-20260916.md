# DM repeat counter outside the msg_id: verdict and design

Date: 2026-09-16. Branch read: `dry-unification` (fork-main stage 1 code read via git).

## BLUF

The idea (keep the four msg_id bytes unchanged, carry a repeat counter that only new firmware
folds into its dedup key) is sound and wire-compatible. Old nodes drop the repeat exactly as
today, so there is zero regression. Its payoff, however, arrives only with fleet adoption:
a repeat travels solely over new-firmware relays and reaches only new-firmware destinations.

fork-main already ships the mirror image (stage 1: fresh msg_id per attempt, stable `{NNN`,
destination dedup on source plus NNN). The real decision is which nodes should relay the repeat:

- Counter outside the msg_id is the right **upstream proposal** (strict opt-in extension).
- Fresh msg_id is the right **fork tool** until the version mix shifts.

The two do not conflict. The retry ladder can choose the id policy per attempt.

## 1. What the code confirms

### 1.1 The repeat is dead everywhere, not only at relays

- Upstream re-sends with the identical msg_id, up to `MAX_RETRANSMIT 3`
  (`src/lora_functions.cpp:2070`, cap check at `:2104`).
- Every node that heard attempt 1 drops attempt 2 at the ring, including the destination itself.
- A repeat therefore helps only if the sender's first transmission was lost at all neighbours.
  Once one neighbour relayed it, all three repeats are wasted airtime.

### 1.2 Old firmware keys dedup on exactly the four msg_id bytes

- `src/dedup_functions.h`: ring entry is byte 0..3 msg_id (little endian) plus a server flag.
  `is_new_packet()` compares those four bytes and nothing else.
- Anything outside those bytes is invisible to an old ring. This is the property the idea
  needs, and it holds.

### 1.3 The header has no spare bit

- Byte 5 is fully used: hop count 0x0F, mesh 0x10, app_offline 0x20, track 0x40, server 0x80
  (`encodeStartAPRS()` / `decodeAPRS()` in `src/aprs_functions.cpp`).
- msg_id = `(MAC & 0x3FFFFF) << 10 | (node_msgid & 0x3FF)`. The 10-bit counter wraps at 999
  (`src/msgid_counter.h`, `kMsgIdMax = 999`), so codes 1000..1023 are unused but changing
  them would alter the msg_id itself.

### 1.4 The ack matches on the payload text, not on msg_id bits

- Receiver: `iAckId = payload.substring(iEnqPos+1).toInt()` from the `{NNN` tag, then
  `SendAckMessage(source, iAckId)` (`src/lora_functions.cpp` around `:1049-1098`).
- Sender: `msg_counter = (_GW_ID & 0x3FFFFF) << 10 | (ackNNN & 0x3FF)` and lookup in
  `own_msg_id[]`.
- Consequence: the upper msg_id bits and the NNN text are already decoupled. This is what lets
  stage 1 mint a fresh msg_id while keeping `{NNN` stable.

### 1.5 The trailer after the FCS is the only place old firmware provably ignores

Encoder order after the payload (`encodeAPRS()`, `src/aprs_functions.cpp:1045`):

```
0x00 | source_hw | source_mod | FCS hi | FCS lo | fw_version | last_hw | sub_version | 0x7e
```

- FCS covers everything up to and including `source_mod`. The four trailer bytes are outside
  the sum.
- Decoder (`src/aprs_functions.cpp:455-495`): reads fw_version, last_hw, sub_version, then
  consumes a 0x7e only if present. An unexpected byte at that position is neither consumed
  nor rejected; `msg_len` ends one byte short and the frame is accepted.
- The length cap `if((inext + 10) >= UDP_TX_BUF_SIZE)` reserves 10 trailer bytes and uses 9.
  One byte is free without touching the payload budget.

### 1.6 What fork-main already has (not on this branch)

- Stage 0: re-ACK on duplicate-for-me, failure status 0x03, no flash write in the ack path.
- Stage 1 (`731e0ebc`, `--dmretry off|3|9`, default off): outbox and retry ladder.
  Attempt 2 reuses the first id if no echo was seen, attempts 3+ mint a fresh id
  (`src/dm_outbox.cpp` around `:313-343`). `glueTransmit()` in `src/dm_outbox_glue.cpp`
  rebuilds the frame with the ladder's id and the entry's stable `{NNN`.
- Stage 2.1 (`44506d0b`): destination dedup on (source callsign, NNN); second copy is acked
  silently and shown once.
- Nothing of this has been bench-tested yet (see `docs/dm-bench-session-plan-20260914.md`).

## 2. Comparison

|                              | Counter outside msg_id (this idea)       | Fresh msg_id, stable NNN (stage 1 + 2.1)      |
| ---------------------------- | ---------------------------------------- | --------------------------------------------- |
| Old relays                   | drop the repeat, as today                | relay it as a new message                     |
| Old destination              | never sees it, no gain, no harm          | gets it, acks it, shows a duplicate line      |
| New destination              | delivered if a path of new nodes exists  | delivered over any path                       |
| Channel cost on old nodes    | zero                                     | one full re-flood per attempt                 |
| Benefit in the current fleet | near zero until adoption                 | immediate                                     |
| Upstream story               | strict opt-in extension, zero regression | needs stage 2.1 everywhere to hide duplicates |
| 1-hop lost-ack case          | same as stage 0 re-ACK                   | same as stage 0 re-ACK                        |

## 3. Caveats before proposing it

1. **Closed server parser.** The MeshCom server's frame parser is not ours. Gateways must strip
   the trailer byte before the UDP upload so the server never sees it. The same applies to
   EXTUDP consumers unless they are updated.
2. **Server-side dedup.** If the server keys on msg_id (likely), attempt 2 is swallowed on the
   server path the same way old relays swallow it. Either accept that or fold the counter into
   the server's key too.
3. **Old relays re-encode from the struct.** They would strip the byte, which is irrelevant
   because they drop the repeat anyway. New relays must carry the field through.
4. **No gain on the 1-hop lost-ack case** beyond stage 0's re-ACK on duplicate-for-me.
5. **Adoption gate.** The mcmap `fleet_firmware` tool gives the share of nodes on a version
   that could carry the byte. Below that share the scheme is dead weight until upstream ships.

## 4. Design

1. **Wire.** Add `uint8_t msg_repeat` to `struct aprsMessage` (0 = original). The encoder
   emits one tagged byte before the closing 0x7e only when `msg_repeat > 0`, so attempt 1
   stays byte-identical to today. Tag it (for example `0xC0 | (counter & 0x0F)`) so the decoder
   can tell it from garbage; accept it only if the byte after it is 0x7e.
2. **Dedup key.** Keep the ring layout. Fold the counter into the lookup key before
   `is_new_packet()` and `addLoraRxBuffer()`, for instance into bits 30 and 31 of the 22-bit
   MAC field. The wire msg_id is untouched; only the ring sees a distinct key per attempt.
   Pin it with `test/test_dedup_replay`.
3. **Relay.** The relay path re-encodes from the struct, so carrying `msg_repeat` through
   `decodeAPRS()` and `encodeAPRS()` is enough.
4. **Sender.** In the retransmit loop (`src/lora_functions.cpp` around `:2070-2110`), or in
   the stage 1 ladder as a third id policy, patch the stored ring frame: insert the byte
   before 0x7e and bump the length. The FCS is unaffected. `{NNN` stays constant, so ack
   matching works unchanged on old and new firmware.
5. **Destination.** Stage 2.1's dedup on source plus NNN already shows the message once and
   re-acks silently. Nothing new.
6. **Gateway.** Strip the byte before the server upload and before EXTUDP. Decide explicitly
   whether the server-side dedup key should include the counter.
7. **Ladder policy (fork).** Attempt 2 same-id, echo-gated (as now). Attempt 3 with counter
   (zero cost on old nodes, reaches upgraded paths). Later attempts fresh id (reaches everyone,
   costs a re-flood and a duplicate line on old destinations).
8. **Tests.**
   - Native encode/decode round trip with `msg_repeat` 0 and > 0.
   - Decoder test feeding a repeat frame through the unchanged tolerance path: no discard,
     `msg_len` as expected.
   - Replay test: same msg_id with counter 1 passes the ring, counter 0 still does not.
   - Ack matching test: `{NNN` unchanged across attempts resolves to `first_id`.

## 5. Files touched (estimate)

- `src/aprs_structures.h` (field), `src/aprs_functions.cpp` (encoder, decoder)
- `src/dedup_functions.cpp` / `.h` (key fold)
- `src/lora_functions.cpp` (retransmit patch, relay carry-through, gateway strip)
- `src/esp32/udp_frame_esp32.cpp`, `src/nrf52/udp_frame_nrf52.cpp` (server ingress key)
- fork-main only: `src/dm_outbox.cpp`, `src/dm_outbox_glue.cpp` (third id policy)
- `test/test_dedup_replay`, new native test for the trailer byte
