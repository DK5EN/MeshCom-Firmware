# DM stage 0 — Fable Verdict (2026-09-13)

Subject: commit `7aeb2ac5` (diff `116053d8..7aeb2ac5`) against
`docs/dm-transport-impl-plan-20260913.md` §0.1-0.5, the stage 0 implementation notes, the stage 0
test table, traps T1-T14 and §3.4 of `docs/dm-reliability-and-store-node-verdict-20260913.md`.
Refuted claims from `docs/review/fable-dm-store-node-verdict-20260913.md` were not re-raised.

Native run by the advisor: `pio test -e native -f test_reack_limiter -f test_dm_stats` 14/14,
`pio test -e native_aprs -f test_txring` 26/26 (the txring suite lives in `native_aprs`, not
`native`; a `-e native` run errors, which is env selection, not a defect).

## Verdict: REWORK

Two findings block: F1 breaks the 0.5 acceptance criterion on nRF52 ("`--airgap off` restores
normal behaviour without a reboot"), F2 puts spurious `:ack` frames on air from the new 0.2 branch.
Everything else is medium or lower and can ride the same rework wave. 0.1, 0.3 and 0.4 are sound as
written.

## Finding 1: `--airgap` early return skips the RX-complete bookkeeping on nRF52

- **File:** `src/lora_functions.cpp:464-473` (new), against `:495`, `:548-556`, `:1672-1685`
- **Severity:** high
- **Failure scenario:** On RAK4630 `OnHeaderDetect()` is registered as the preamble callback
  (`src/nrf52/nrf52_main.cpp:995`) and sets `is_receiving = true` and `ch_util_rx_start`
  (`src/lora_functions.cpp:2637-2638`). Those are undone only at the end of a completed
  `OnRxDone()` (`:1685`, `:495`), in `OnRxTimeout()`/`OnRxError()`, or in the `handleACK` early
  exit (`:594-612`). The airgap `return` at `:472` does none of it. After the first frame under
  `--airgap on`:
  - `is_receiving` stays `true`. The nRF52 LoRa state machine at `src/nrf52/nrf52_main.cpp:1453`
    (`iReceiveTimeOutTime == 0 && is_receiving == false && tx_is_active == false`) and the battery
    read at `:2281` are gated on it. After `--airgap off` the node remains TX-mute until the next
    _completed_ RX, timeout or error — it does not "restore normal behaviour" on its own. The
    RX-timeout deferral at `:1428-1441` restarts the radio after three deferrals but never clears
    `is_receiving`.
  - `ch_util_rx_start` keeps the header-detect timestamp of the first airgapped frame; the first
    completed `OnRxDone()` after `--airgap off` books `millis() - _rx_s` — the whole airgap span —
    into `ch_util_rx_accum` (`:495-497`). One STAT window shows a bogus channel-utilisation spike.
  - The RACE-05 CAD abort (`:548-556`) is skipped: a frame arriving mid-CAD leaves
    `cad_in_progress` stale.
  - `iReceiveTimeOutTime`/`csma_timeout` (`:1672-1673`) are not refreshed.
  - ESP32 is unaffected: `checkRX()` sets and clears `is_receiving` around the call
    (`src/esp32/esp32_main.cpp:4135`, `:4334`) and re-arms at `:4181` before `OnRxDone()` at
    `:4225`.
- **Fix:** Mirror the `handleACK` early-exit block at `:594-612` rather than inventing a second
  variant: on the airgap path do `ch_util_rx_start.exchange(0)`, the CAD abort, `is_receiving =
false`, `iReceiveTimeOutTime = millis()`, `csma_timeout = csma_compute_timeout(cad_attempt)`.
  Simplest structurally: move the airgap test to just after the RAK re-arm/CAD block (`:560`), and
  on that path release `rxBufInUse[rxBufIndex]` plus the tail bookkeeping. Bench check on RAK-90:
  `--airgap on`, receive one frame, `--airgap off`, confirm the next beacon transmits without any
  intervening RX.

## Finding 2: the re-ACK branch acks duplicate `{pong}` frames

- **File:** `src/lora_functions.cpp:1618-1646` (new), against `:1024`, `:1041`,
  `src/loop_functions.cpp:3466`
- **Severity:** high
- **Failure scenario:** The original path tests `startsWith("{ping}")` / `startsWith("{pong}")`
  before the `{NNN` ack logic (`:1024`, `:1041`). The new duplicate branch does not. A pong is
  `{pong}{%03i}` with the 32-bit msg_id printed as decimal (`src/loop_functions.cpp:3466`), so
  `indexOf("{", 1)` is 6 and `toInt()` yields the msg_id, truncated to `uint16_t`. The pong's
  original arrival never seeds the limiter (the pong path at `:1041` does not call
  `reackAllowed()`), so every relayed copy of a pong to this node — the common case, seconds
  after the original — emits `SendAckMessage(pong_sender, garbage_nnn)`: an on-air `:ackNNNN`
  frame with a 4-5 digit NNN, `dmstat_reack` counts it, and on the pong sender `iAckId & 0x3FF`
  (`:1069`) can collide with an in-flight own DM and mark it ACKed. This is the T14 class ("a
  frame that parses as a DM").
- **Fix:** Add `!aprsmsg.msg_payload.startsWith("{")` (covers ping, pong, `{mcp}`) to the branch
  condition, or factor the ping/pong/ack/rej/`{NNN` classification into one predicate used by
  both paths so the two gates cannot drift again. Add a native or `--injectraw` bench case:
  duplicate `{pong}{...}` to self must produce no ack.

## Finding 3: `dmstat_echo` counts own-ACK echoes

- **File:** `src/lora_functions.cpp:919-925` (new), `src/loop_functions.cpp:4966`
- **Severity:** medium
- **Failure scenario:** `SendAckMessage()` calls `insertOwnTx(aprsmsg.msg_id)` (`:4966`), so a
  relayed copy of this node's own `:ackNNN` hits `checkOwnTx() >= 0` and, being `MSG_TYPE_TEXT`
  with status `0x00`, increments `dmstat_echo`. The implementation notes admit broadcast and
  group echoes; they do not mention ACK echoes. On any node that receives DMs, `echo` exceeds
  `sent` and the echo/sent ratio the counters exist to produce is meaningless.
- **Fix:** Count only when `ackMsgIdFromNode(aprsmsg.msg_id, _GW_ID)` holds — after 0.1, ACK ids
  are `millis()` and fail that test, so this discriminates ACKs for free. Record the remaining
  broadcast/group residue in the notes.

## Finding 4: tests do not pin the exclusion set of the parked-overwrite criterion

- **File:** `test/test_txring/test_txring.cpp:1008-1033`, `test/test_reack_limiter/test_main.cpp`
- **Severity:** medium
- **Failure scenario:** `test_ringstat_parked_overwrite_bei_pending_slot` uses status `0x05`;
  the sibling test uses an empty slot. An implementation reduced to `ringBuffer[w][0] != 0` passes
  both. The cases that matter for M0-1 — DONE with non-zero length (the normal post-transmit state
  of a non-text frame, `src/lora_functions.cpp:1908` sets SENT only for READY slots, and every ACK
  slot after `:4999`), READY with length (queued, not parked) and `EXT_PENDING` — are untested, so
  a false positive on ordinary ring wrap would not be caught. In `test_reack_limiter`, the
  distinct-callsign case pairs `DK5EN-1` with `OE1XYZ`; a comparator on the first character passes.
  The field discriminator is the SSID (`DK5EN-1` vs `DK5EN-2`), which is not exercised.
  `test_neunter_eintrag` comment still says "60s-Fenster".
- **Fix:** Add three txring cases (DONE+len, READY+len, EXT_PENDING+len, each expecting 0) and a
  shared-prefix callsign case to the limiter suite; fix the comment.

## Finding 5: `dmStatNoteSent` keeps a stale never-acked entry across an NNN wrap

- **File:** `src/dm_stats.cpp:41-47`, `test/test_dm_stats/test_main.cpp:113-129`
- **Severity:** low
- **Failure scenario:** A DM that is never acked keeps its slot `used` until seven more fresh DMs
  push it out. `node_msgid` advances at seven other sites (`src/loop_functions.cpp:3368`,
  `:3470`, `:4818`, `:4895`, `:5045`, `:5383` plus `sendMessage`), so NNN wraps after ~1000
  frames of any kind — about ten days on a quiet beaconing node. A new DM reusing that NNN keeps
  the stale time ("erste Zeit behalten", encoded by the test as intended) and its ack lands in
  bucket 5. Bench-invisible, field-visible over weeks.
- **Fix:** Treat an entry older than the bucket-5 edge (30 min) as free in `dmStatNoteSent()`.

## Finding 6: a late echo overwrites the 0x03 failed mark

- **File:** `src/lora_functions.cpp:928-929`, `:2247-2249`
- **Severity:** low
- **Failure scenario:** Give-up sets `own_msg_id[idx][4] = 0x03` unless already `0x02`. The echo
  path writes `0x01` for anything `!= 0x02` (`:929`), so an echo of the last attempt heard after
  give-up (a slow relay, a store node later) turns the web GUI's ballot X back into a tick. The
  BLE frame already went out, so app and GUI then disagree.
- **Fix:** `if(own_msg_id[icheck][4] != 0x02 && own_msg_id[icheck][4] != 0x03)` at `:928`.

## Finding 7: re-ACK refuses `msg_server` frames the original path acks

- **File:** `src/lora_functions.cpp:1620`, against `:1020` (no `msg_server` test)
- **Severity:** low
- **Failure scenario:** The original ack at `:1113` fires for any text addressed to this node,
  server-injected or not. The duplicate branch adds `!aprsmsg.msg_server`, so a DM that reached
  this node via two gateways (same msg_id, second copy a duplicate) is acked once and never
  repaired. Conservative, and server-originated retries carry a fresh id anyway, so no field harm —
  but the two gates now differ in a second place and the notes do not say why.
- **Fix:** Drop the extra clause or record the reason in the implementation notes.

## Finding 8: documentation drift on the limiter window

- **File:** `src/dm_stats.h:26`, `test/test_reack_limiter/test_main.cpp:66`
- **Severity:** low
- **Fix:** Both say 60 s; `REACK_LIMITER_WINDOW_MS` is 30000. Correct the two comments.

## Refuted claims (do not re-investigate)

- **"The re-ACK `else` can fire for an own frame."** Refuted: the chain is
  `if(icheck >= 0) {...} else if(setlogCountDedup(rx_is_new)) {...} else {...}` at
  `src/lora_functions.cpp:898`, `:933`, `:1607`; an own msg_id takes the first arm. Group
  messages fail `msg_destination_call == node_call` (`:1619`); `:ack`/`:rej` payloads are
  excluded at `:1626`; foreign destinations fail `:1619`.
- **"A relay's copy is not a duplicate."** Refuted: `is_new_packet()` compares the four msg_id
  bytes only (`src/dedup_functions.cpp:24-30`); relays keep the msg_id, so the copy reaches the
  duplicate arm. Seeding at `:1110` (before `SendAckMessage` at `:1113`) suppresses it.
- **"The 30 s window swallows the sender's retry."** Refuted: `doTX()` sets SENT=`0x01` at
  `:1908`, `updateRetransmissionStatus()` adds 1 per 2 s tick (`src/nrf52/nrf52_main.cpp:1294`,
  `src/esp32/esp32_main.cpp:2147`), threshold `0x15` (`:2210`) — retry at 40 s ±2 s plus queue
  latency, never under 30 s. `upstream/dev` carries the same `0x15`. Each allowed retry refreshes
  the window (`reack_limiter.h`, in-place refresh), so retries 2 and 3 pass too.
- **"`SendAckMessage()` is unsafe from the duplicate branch."** Refuted: same function, same
  task (nRF52 `_lora_task`), no critical section open at `:1607` (the ones at `:517` and `:548`
  close before the decode), shallower call depth than the original ack site.
- **"`size` is read after the length clear in the give-up path."** Refuted: `int size =
ringBuffer[ircheck][0]` at `:2202`, clear at `:2219`.
- **"The give-up decode uses a different offset than `doTX()`."** Refuted: `doTX()` copies from
  `ringBuffer[txSlot] + 2` (`:1860`); give-up decodes `&ringBuffer[ircheck][2]` (`:2235`).
- **"`updateRetransmissionStatus()` runs on the nRF52 timer task."** Refuted: called from
  `loop()` on both platforms (`src/nrf52/nrf52_main.cpp:1296`, `src/esp32/esp32_main.cpp:2149`,
  `:4091`); the `aprsMessage` on that stack is what `doTX()` already does at `:2077`.
- **"`CheckGroup()` misclassifies callsigns."** Refuted: returns 0 for any non-digit or length
  outside 1..6, else the number for 1..99999 or 100001 (`src/aprs_functions.cpp:28-51`);
  `sendMessage()` derives `bDM` with the same call (`src/loop_functions.cpp:4013`), so give-up
  and origin agree. `WLNK-1`/`APRS2SOTA` are not DMs at origin and are gateway-acked at
  `:1284`.
- **"An ACK slot can reach give-up."** Refuted: `SendAckMessage()` forces `0xFF` after enqueue
  (`src/loop_functions.cpp:4999`); gateway ACKs enqueue `RING_STATUS_DONE` (`:1304`, `:1328`);
  `doTX()` restores the length only for `!= DONE` text (`:2144`); give-up requires `size > 0`
  (`:2204`). Relays enqueue DONE too (`:1553`).
- **"`millis()` ACK ids collide across nodes and get deduped."** Refuted as material: collision
  needs two unrelated uptime counters equal to the millisecond at ack time; the receiver matches
  `:ackNNN` by parsing NNN (`:1068-1069`), never by the ACK's msg_id; the gateway ACK path
  (`:1287`, `:1321`) has used `millis()` all along. Own-echo matching via `insertOwnTx()` still
  works; `ackMsgIdFromNode()` (`src/ack_attribution.h:84-87`) now rejects ACK echoes for the
  phone frame, which is an improvement (a bogus HEARD used to go to the phone).
- **"The eviction path invalidates the parked-overwrite read."** Refuted: the read is taken at
  `w` before any write (`src/txring_functions.cpp:504-507`); eviction scans `r..w` exclusive of
  `w` (`:549`) and writes `worst_slot` and `r`, neither equal to `w`; the criterion excludes DONE,
  so a normal wrap onto a transmitted-and-finished slot is not counted. Modulo the TEXT-type check
  (which `updateRetransmissionStatus()` forces to DONE at `:2197-2200` and which cannot hold a
  non-zero length after `:2144`), the set matches what the ladder would still retry.
- **"ESP32 `--airgap` leaves the radio un-armed."** Refuted: `checkRX()` calls `startReceive()`
  at `src/esp32/esp32_main.cpp:4181` before `OnRxDone()` at `:4225`.
- **"`{` rewrite breaks ping/pong."** Refuted: `SendPing()`/`SendPong()` build their own frames
  (`src/loop_functions.cpp:3364`, `:3466`) and do not pass through `sendMessage()`.

## Acceptance-criteria matrix (0.1-0.5: native-verified / bench-only)

| Item | Criterion                                                                         | Native-verified here                                                                                                                                                                               | Bench-only                                                                                                   |
| ---- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| 0.1  | 20 DMs, zero settings writes from the ACK path                                    | By inspection: `SendAckMessage()` has no `node_msgid++` and no `save_settings()` (diff, `src/loop_functions.cpp:4941-4962`); seven `node_msgid++` sites remain, none in the ACK path               | LittleFS/NVS write count over 20 DMs on RAK-90 and Heltec-93                                                 |
| 0.2  | Replayed one-hop DM: exactly one extra `:ackNNN`, no second display; <30 s: none  | Limiter semantics (window, refresh, eviction, rollover) 7/7; branch reachability and exclusions by inspection; **F2 shows the exclusion set is incomplete**                                        | T-0.2 with `--injectraw`: replay at 5 s and 40 s; add a `{pong}` replay after F2                             |
| 0.3  | DM to unreachable call: exactly one `0x03`; broadcast and ACK: none               | Scoping by inspection (`:2237-2247`, refuted ACK-slot claim above); no native test of the give-up path exists                                                                                      | T-0.3 on BLE (`mc-chat`/MCProxy) and web GUI; F6 case (late echo) optional                                   |
| 0.4  | Counters in `--setlog`, survive an hour, overwrite counter non-zero only on load  | `dmStatFormat` string and reset 7/7; `ringstat_*` 2/2 (weak, F4); wiring of every counter by inspection; **F3: `echo` includes ACK echoes**, so the sent/echo pair is not yet a usable measurement | One-hour live run on Heltec-93; a loaded relay for `ovw > 0`                                                 |
| 0.5  | `--airgap on`: no emission, mheard/dedup unchanged; `off` restores without reboot | Hook placement before dedup/mheard by inspection (`:464` precedes `:683`, `:880`); TX refusal at `:1938`; **F1: `off` does not restore on nRF52 until the next completed RX**                      | T-0.4 on RAK-90 and Heltec-93; string scan of the release image is the orchestrator's claim, not re-run here |

Noted, not a finding: a DM transmitted under `--airgap on` is consumed on the non-rollback path
(`:2150-2168`, slot length stays 0), so it loses its retransmit ladder. Acceptable for a bench
instrument; the bench plan should not expect a retry after `--airgap off`.
