# Store-and-forward changelog

Branch `feature-snf`, the DM transport stages from `fork-main` ported onto
`feature-neighbour-matrix` (`0d4b914c`). Engineering record, decisions and gate results:
`docs/snf-port-campaign.md`. Feature design and stage table: `docs/dm-transport-impl-plan-20260913.md`.

Released as `v4.35t.09.27-neo` together with the neighbour matrix. Bench state: the store node's
basic case (absent destination, held DM, `:sto` notice, one-hop delivery, ack back to the sender)
passed twice on 2026-09-26 with a RAK4631 store node and a T-Beam receiver; the rest of the bench
plan (`docs/dm-bench-session-plan-20260914.md`) is open, see the coverage table at the end of
`docs/snf-port-campaign.md`.

## Upgrade note

No settings wipe. `FLASH_STRUCT_VERSION` is unchanged; the new settings live in their own storage
(ESP32: `Credentials` NVS keys, nRF52: `/dm.cfg` and `/msgstore.cfg`). Everything new is off by
default: `--dmretry off`, `--store off`.

## What a node does differently

1. **Duplicate DMs are acknowledged, not shown twice.** A DM addressed to this node that arrives
   again (relayed copy, sender retry, server copy) with the same sender and `{NNN}` is acked again,
   rate limited to once per 30 s, and neither displayed nor forwarded to the phone a second time.
   This holds on LoRa and on both server paths.
2. **A failed DM says so.** When the TX ring gives up on a DM the user sent, the app gets status
   `0x03` (failed) and the web messages page marks it.
3. **A `{` in DM text no longer breaks the ack.** It is sent as `(`; a leading `{ping}` or `{SET}`
   tag stays as it is.
4. **Enhanced message transport protection** (`--dmretry off|3|9`, web setup): a DM is kept in an
   outbox (five slots on S3 and RAK4631, three on classic ESP32) and resent on a ladder until the
   destination acks it, at most 3 or 9 sends. A full
   outbox refuses a new DM with `OUTBOX FULL NOT SENT - ...`. Ping messages are never retried.
5. **Store node** (ESP32-S3 and RAK4631 only, `--store own|list|heard`, web setup and the new
   Mailbox page): keeps DMs for destinations that are not answering and delivers them one hop when
   the destination is heard again. Which destinations qualify: the node's own base callsign (any
   SSID), a list, or every station heard directly in the last 12 hours (read from the
   neighbour-matrix topology).
6. **Custody notice** (`--storenotice on|off`): a store node tells the sender it holds the DM
   (`:stoNNN`); the sender's app shows status `0x04` (held) with the holder's callsign until the
   destination's own ack arrives.
7. **Counters.** A `DM` setlog line (sent, echo, gateway ack, ack, gave up, gave up while held,
   attempts, outbox full, re-acks, ack round-trip histogram, ring overwrites), plus `OUTBOX` and `MBOX`
   lines when those roles are on. `--airgap` (instrumented builds only) makes a bench node deaf and
   mute without switching the radio off.

## Differences from fork-main

- ACK ids keep this branch's `msgid_counter` design; fork-main's millis()-based ACK id (stage 0.1)
  is not ported.
- The store node's "heard" test reads the neighbour-matrix topology instead of the removed MHeard
  table, minute resolution, same 12 h window.
- Store-node commands have `--help` lines (fork-main had none), and the Mailbox table follows this
  branch's table styling.
- `--storecall`, `--storetime`, `--storeslots`, `--storenotice` on classic ESP32 answer "unknown
  command" instead of "unavailable" (exact command matching).

## Since v4.35t.09.27-neo (not released yet)

1. **Personal-message retries use the XOR format** (`src/pn_retry.h`, `docs/pn-retry-xor-impl-plan.md`).
   Retry k (1..3) of a DM this node sent carries the original msg_id with bits 10-11 XOR k and a
   recomputed FCS, so relays with msg_id-only dedup forward it. The first send is byte-identical to
   before. Applies to both send paths: the ring retry (`--dmretry off`, 40 s steps) and the outbox
   ladder (`--dmretry 3`).
2. **An echo no longer ends the retry, only the destination's `:ackNNN` does** -- over LoRa or over
   the server; a server-side ack now also stops the waiting ring slot. A DM sent on behalf of a
   KISS client keeps the old behaviour (byte-identical retries, released on the first echo).
3. **Repeat copies are recognised** by their three other bit variants: not uploaded to the server,
   EXTUDP or KISS again, not stored again by a store node; a DM addressed to this node is re-acked
   but not shown twice (`dm_dedup`).
4. **`--dmretry` is now `off|3`.** Mode 3 sends at most four times (original plus retries at
   40/80/120 s) with XOR ids; mode 9 is gone because nine attempts do not fit three bit variants. A
   stored `9` is read back as `3`.
