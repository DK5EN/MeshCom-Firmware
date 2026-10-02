# DM retries

Every direct message (DM) on this branch is sent once and, if needed, retried automatically by the
LoRa TX ring: up to 3 retries, 40 s after the previous send. An echo of the DM (heard back on air)
restarts that 40 s wait; only the destination's own `:ackNNN` — over LoRa or over the server —
stops it.

Each retry carries a different msg_id: bits 10-11 XOR the retry number (the XOR retry format,
`src/pn_retry.h`). Relays that dedup on the full msg_id still forward it; the destination folds the
XOR variants back onto the original and acks once.

After the 4th send's wait has passed with no ack, the sending phone gets a "failed" status
(`0x03`) — unless a store node has taken custody of the DM in the meantime, in which case it gets
"held" (`0x04`) instead.

There is no user-facing switch for this. Every DM behaves this way; an earlier build of this branch
had an `--dmretry off|3` setting with a separate outbox path, removed 2026-09-27 (see
`docs/CHANGELOG-snf.md`).

**Compatibility**: older nodes forward the retries correctly but don't recognise them as copies of
the same DM. A receiving node on older firmware may show the same DM up to 4 times, and an older
store node may push its delivery back by up to ~2 minutes per copy it sees. Servers must dedup on
the mask `0xFFFFF3FF` (`docs/pn-retry-server.md`) before this goes to the field.
