# DM stage 2.1 — Fable Verdict (2026-09-14)

Subject: commit `44506d0b` (diff `fe3f0640..44506d0b`) against
`docs/dm-transport-impl-plan-20260913.md` §2.1 incl. the implementation notes, the §2.2 deferral,
decision D6 and traps T1/T5/T12 of `docs/dm-reliability-and-store-node-verdict-20260913.md`.
Stage 0 refuted claims (`docs/review/fable-dm-stage0-verdict-20260913.md`) were not re-raised.

Native run by the advisor: `pio test -e native -f test_dm_dedup -f test_reack_limiter` 19/19
(11 + 8). Board builds not re-run here (orchestrator gate).

## Verdict: APPROVED

No finding above low. The module matches its header contract, the three hooks pass the same
stripped payload, the NEW path is the pre-existing sequence, and the stage 0 branch stays
disjoint from the new layer. The six findings are test-strength, a bounded cross-task race on
nRF52 that the shared table newly introduces, one callsign-length corner, and two doc items.

## Question-by-question

1. **dm_dedup.cpp vs header** — CONFIRMED. Age-out `>= DM_DEDUP_AGE_MS` via unsigned
   subtraction (`src/dm_dedup.cpp:24-27`), aged slots skipped in the match scan (`:70-71`) and
   reused first in the write scan (`:100-105`); rollover covered by the same arithmetic. Same
   (call, NNN) with different len/CRC replaces in place with fresh age (`:83-85`); exact match
   returns DUP without touching `first_ms` (`:79-80`). Eviction picks the largest age (`:107-112`;
   `i == 0 ||` seeds the comparison). `src_call == NULL` returns NEW untouched (`:53-54`);
   `stripped_payload == NULL` forces `len = 0` (`:57-58`) and `crc32_buf(NULL, 0)` never enters
   its loop (`src/crc32_util.h:34-46`), so len 0 is a legal payload with CRC 0. Prefix callsigns:
   `strncmp(..., DM_DEDUP_CALL_MAX)` over the whole stored buffer (`:76`). Wrong suppression: only
   the accepted §3.3/T12 collision (same call, same NNN after a <1 h wrap, byte-identical text) —
   the plan accepts it, and the DUP is re-acked so the sender is not stalled. Double display via
   two routes: all three hooks pass `substring(0, indexOf("{", 1))` of the `decodeAPRS()` payload
   (`src/lora_functions.cpp:1123`, `src/udp_functions.cpp:440`, `src/nrf52/nrf_eth.cpp:610`);
   `decodeAPRS()` applies `charset_filter_apply(..., CHARSET_FILTER_PLAIN)` to every payload it
   returns (`src/aprs_functions.cpp:387-390`) regardless of ingress, and nothing else mutates
   `msg_payload` before the hooks (grep: the only `msg_payload =` sites are the three strips).
   Whether the MeshCom server itself rewrites text is UNVERIFIABLE-HERE.
2. **LoRa hook** — CONFIRMED. For a DUP: `updateMheard()` at `src/lora_functions.cpp:875` and
   `addLoraRxBuffer()` at `:992` precede the hook at `:1125`. Not relayed: the destination is
   `node_call`, so `rly_reason = "self"` and `rly_go` stays false (`:1480-1483`). GW upload
   `addNodeData()` at `:1469` and `queueExtern()` at `:963` still run for a DUP — that is the
   plan's server-side fold (`docs/dm-transport-impl-plan-20260913.md:405`, "mcmap and the server
   must fold on (source, NNN)"), so the upload is right; see F6 for the wording at `:252`. NEW
   path: `reackAllowed` seed → `SendAckMessage` → `msg_payload = strippedPayload` → `encodeAPRS`
   → `queueDisplayText` → `[MESHx]` print → `addBLEOutBuffer` (`:1149-1164`), same order and same
   bytes as `fe3f0640` (`substring(0, iEnqPos)` hoisted, value unchanged).
3. **Server twins** — CONFIRMED equivalent. `bDmDedupNew` gates `sendDisplayText`
   (`src/udp_functions.cpp:456`, `src/nrf52/nrf_eth.cpp:621`), the DM `addBLEOutBuffer` (`:472`,
   `:638`) and the ack branch (`:484-501`, `:650-667`); the seed-then-ack on NEW mirrors the LoRa
   path. nRF `!bGATEWAY` (`:623`) is pre-existing and only narrows the display further — no
   semantic change. `*` short-circuit sets `iEnqPos = 0` (`:402-406`, `:560-564`) so the dedup
   block (`iEnqPos > 0`) is never entered; groups fail `strcmp(destination_call, node_call)`
   (`:447`, `:612`). D6 holds. A payload with both `:ack` and `{` hits both `if` blocks in both
   twins (pre-existing, not else-if): the dedup registers the stripped text and the ack branch
   acks the `{NNN`; the display is already suppressed by `iAckPos > 0`. No new hazard. LoRa
   forward: both twins set `bUDPtoLoraSend = false` for `destination_call == node_call` before
   the dedup (`:343`, `:546`), so a DUP-to-self is never forwarded to LoRa on either platform;
   for groups/`*` nothing changed.
4. **reack_limiter split** — CONFIRMED. `reack_table`/`reack_oldest` are file-static in
   `src/reack_limiter.cpp:12-13`; the header carries declarations only (`src/reack_limiter.h:29-46`).
   `diff -w` of the old inline bodies (`fe3f0640:src/reack_limiter.h`) against the new
   definitions is empty. `reackLimiterReset()` is referenced only by
   `test/test_reack_limiter/test_main.cpp:20`.
5. **Stage 0 interaction** — CONFIRMED. `setlogCountDedup()` returns its argument
   (`src/lora_functions.cpp:297-305`); the new hook lives in the `rx_is_new == true` arm
   (`:935`), the stage 0 re-ack in the `else` arm (`:1641-1680`). A same-msg_id retry takes the
   `else` arm and never calls `dmDedupCheck()`; one frame reaches exactly one arm, so no double
   re-ack. A relay copy of a fresh-id attempt is caught by the msg_id ring, takes the `else` arm
   and does not touch `dm_dedup_table` (no call there); a DUP verdict does not refresh `first_ms`
   either (`:79-80`), so the 1 h window is anchored at the first sighting in every case.
6. **Tests** — see F1, F2. Per-case discriminating assertion: `first_ms` non-refresh (third
   assert, `test_main.cpp:95`), replace-in-place (fourth assert, `:108`), other call/prefix
   (`:127`, `:139`), boundary `>=` (`:147`), rollover (`:164`); `test_erste_sichtung` and
   `test_null_argumente` pass an always-NEW implementation and earn their place only as
   no-crash checks. The "start at index 2" accommodation is correct, not an off-by-one: the
   probe `nnn=0` re-registers and evicts the then-oldest `nnn=1` (`src/dm_dedup.cpp:107-112`);
   asserting 1..15 DUP before probing 0 would remove the need for it.
7. **Native** — 19/19, see header.

## Finding 1: eviction test cannot tell age-based from round-robin eviction

- **File:** `test/test_dm_dedup/test_main.cpp:113-141`
- **Severity:** low
- **Failure scenario:** the 16 entries are inserted in slot order with monotonically increasing
  `first_ms`, so slot 0 is both the round-robin victim and the oldest. A `reack_oldest`-style
  round-robin implementation passes the test verbatim; the comment "per Alter, nicht per
  Rundlauf" is not backed by an assertion.
- **Fix:** before the 17th insert, refresh slot 0 in place (`dmDedupCheck("DK5EN-1", 0, "q0",
2, 1900)` — same NNN, different payload, `src/dm_dedup.cpp:83-85`), then insert nnn 16: age-based
  evicts nnn 1, round-robin evicts nnn 0. Assert nnn 0 DUP and nnn 1 NEW.

## Finding 2: length is never tested independently of the CRC

- **File:** `test/test_dm_dedup/test_main.cpp:74-80`
- **Severity:** low
- **Failure scenario:** `"hallo"` vs `"hallo!"` differ in CRC16 as well as length; an
  implementation that drops the `len` compare passes. The length compare exists to cover CRC16
  collisions, which the suite never exercises.
- **Fix:** pick at test-writing time two strings of different length whose CRC32 low 16 bits
  collide (a few thousand brute-force tries on the host), or drop the claim from the comment.

## Finding 3: shared tables are now written from two tasks on nRF52 without a lock

- **File:** `src/dm_dedup.cpp:20`, `src/reack_limiter.cpp:12-13`; callers
  `src/lora_functions.cpp:1125` (`OnRxDone`, `_lora_task`, see `src/nrf52/nrf52_main.cpp:3040`)
  and `src/nrf52/nrf_eth.cpp:614` (`getUDP()`, called from `nrf52loop()` at
  `src/nrf52/nrf52_main.cpp:2084`)
- **Severity:** low
- **Failure scenario:** stage 0's header-static tables had one writer per translation unit; the
  split makes both tables shared between the LoRa task and the loop task on a RAK gateway. A
  preemption inside the match/write scans can tear a slot (partial `strncpy` of `call`, or
  `first_ms` from the old entry with `nnn` from the new). Bounded consequence: one wrong NEW
  verdict (one extra display/ack) or one wrong DUP against a torn entry; no pointers, no
  out-of-bounds. ESP32 is unaffected — `checkRX()` and `getMeshComUDP()` both run in `loop()`
  (`src/esp32/esp32_main.cpp:3877`).
- **Fix:** either record the race as accepted in the implementation notes, or wrap the two
  scans in `taskENTER_CRITICAL()/taskEXIT_CRITICAL()` on `BOARD_RAK4630` (16 + 8 slots, a few
  microseconds; the LoRa task already takes the same critical section at `:523`/`:579`).

## Finding 4: callsigns of 10+ characters never dedup

- **File:** `src/dm_dedup.cpp:33-34` (store 9 chars), `:76` (compare 10)
- **Severity:** low
- **Failure scenario:** `strncpy(..., DM_DEDUP_CALL_MAX - 1)` stores 9 characters; the match
  compares `DM_DEDUP_CALL_MAX` = 10, so a 10-character source call (stored NUL at index 9 vs a
  live character) never matches and every attempt is NEW. The limiter compares
  `REACK_CALL_MAX - 1` (`src/reack_limiter.cpp:26`) and does match the truncation. MeshCom
  callsigns are at most 9 characters (`XX0XXX-99`), so no field impact today; the two modules
  disagree on the same truncation.
- **Fix:** compare `DM_DEDUP_CALL_MAX - 1`, matching the limiter.

## Finding 5: server twins register and ack `{pong}{NNN}` addressed to the node (pre-existing)

- **File:** `src/udp_functions.cpp:438-453`, `:481`; `src/nrf52/nrf_eth.cpp:607-618`, `:648`
- **Severity:** low
- **Failure scenario:** `SendPong()` builds `{pong}{%03i}` (`src/loop_functions.cpp:3466`), so
  `indexOf("{", 1)` is 6 and the server twins treat a server-delivered pong as a DM: they always
  did (`SendAckMessage` was unconditional at `fe3f0640`), and now also occupy a dedup slot for
  it. The LoRa path excludes ping/pong before its DM block (`src/lora_functions.cpp:1026`,
  `:1043`) and the stage 0 branch was fixed for exactly this (stage 0 F2). Not introduced by
  this commit; a slot per pong is harmless.
- **Fix:** none required for 2.1. When the twins are next touched, add the same
  `startsWith("{ping}")`/`startsWith("{pong}")` exclusion the LoRa path has.

## Finding 6: plan wording "never forwarded twice" vs the unconditional GW upload

- **File:** `docs/dm-transport-impl-plan-20260913.md:252` vs `:405`;
  `src/lora_functions.cpp:1469`
- **Severity:** low (doc)
- **Failure scenario:** `:252` says a duplicate is "never forwarded twice"; the implementation
  uploads every DUP to the server (`addNodeData()` runs before the relay decision and is not
  gated on the verdict), which `:405` requires ("mcmap and the server must fold"). The
  implementation notes say "not forwarded to the app" and are silent on the server. A later
  reader will see a contradiction.
- **Fix:** one sentence in the 2.1 implementation notes: "forwarded to the app once; uploaded to
  the server on every attempt, the server folds on (source, NNN) (§7)".

## Refuted claims (do not re-investigate)

- **"The LoRa and server hooks pass different stripped payloads."** Refuted: all three use
  `msg_payload.substring(0, indexOf("{", 1))` on a `decodeAPRS()` result
  (`src/lora_functions.cpp:1123`, `src/udp_functions.cpp:440`, `src/nrf52/nrf_eth.cpp:610`);
  the charset filter sits inside `decodeAPRS()` (`src/aprs_functions.cpp:387-390`) and is
  applied on every ingress; the path/via fields are not part of the payload.
- **"A DUP frame is still relayed."** Refuted: `destination_call == node_call` yields
  `rly_reason = "self"`, `rly_go` false (`src/lora_functions.cpp:1480-1483`).
- **"The NEW path changed order."** Refuted: `:1149-1164` is the `fe3f0640` sequence with the
  `substring()` hoisted to `:1123`; same value assigned at `:1152`.
- **"`reackAllowed()` semantics changed in the split."** Refuted: `diff -w` of the old inline
  bodies against `src/reack_limiter.cpp` is empty; only the doc comment moved.
- **"There is still one limiter table per translation unit."** Refuted: `static` definitions at
  `src/reack_limiter.cpp:12-13`, header holds prototypes only; `reack_limiter.cpp` is in the
  board build via `+<*>` (`platformio.ini:108`) and in the native filter (`:239-243`).
- **"A same-msg_id retry reaches `dmDedupCheck()`."** Refuted: it takes the `else` arm at
  `:1641`, which does not call it; the two arms are exclusive (`:935` / `:1641`).
- **"One frame can be re-acked twice."** Refuted: exclusive arms, plus the limiter is one table
  now — a LoRa NEW seed at `:1149` blocks the server copy's re-ack at
  `src/udp_functions.cpp:493` within 30 s, and vice versa.
- **"A relay copy refreshes the dm_dedup age."** Refuted: the `else` arm never calls the module;
  a DUP verdict returns before any write (`src/dm_dedup.cpp:79-80`).
- **"Groups or `*` reach the dedup."** Refuted: `*` forces `iEnqPos = 0`
  (`src/udp_functions.cpp:402-406`, `src/nrf52/nrf_eth.cpp:560-564`); groups fail the
  `node_call` strcmp (`:447`, `:612`); the LoRa DM block is inside
  `strcmp(destination_call, node_call) == 0` (`src/lora_functions.cpp:1022`).
- **"A DUP on the RAK server path is still forwarded to LoRa."** Refuted:
  `bUDPtoLoraSend = false` for `node_call` at `src/nrf52/nrf_eth.cpp:546`, before the dedup;
  same on ESP32 at `src/udp_functions.cpp:343`.
- **"`NULL` payload with `len > 0` is dereferenced."** Refuted: `len` forced to 0 at
  `src/dm_dedup.cpp:57-58`; `crc32_update()` loops zero times.
- **"`reackLimiterReset()` is reachable in production."** Refuted: single reference,
  `test/test_reack_limiter/test_main.cpp:20`.
- **"The 'start at index 2' accommodation hides an off-by-one."** Refuted: the probe re-registers
  `nnn=0` and evicts `nnn=1` by age (`src/dm_dedup.cpp:107-112`); same shape as the limiter
  precedent (`test/test_reack_limiter/test_main.cpp:66-77`), which is round-robin by design.

## Bench-only items

- RAK-90 as gateway: one DM to `node_call` heard on LoRa and delivered by the server within
  seconds — expect one display, one `:ackNNN`, one `[DMDUP]` and `reack_limited` +1 in `--setlog`.
- Heltec-93 as gateway: same, with the server copy first (`--airgap on` for the LoRa copy is not
  enough — needs the LoRa frame to arrive second; `--injectraw` after the server delivery).
- Fresh-id attempt: `--injectraw` a crafted DM with the same source/NNN/text and a new msg_id
  40 s after the original — expect `[DMDUP]`, a re-ack, no second display or BLE frame.
- mcmap: confirm the server-side fold on (source, NNN) once stage 1 emits fresh-id attempts
  (plan `:405`); otherwise DM counts inflate per attempt.
- F3 race is not bench-provable at field rates; accept or lock.
