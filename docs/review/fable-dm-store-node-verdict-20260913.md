# Fable verdict: dm-reliability-and-store-node-verdict-20260913.md

Review of the design document, 2026-09-13. Eight independent finders, every load-bearing claim
re-verified against the tree at `7427f425` by the orchestrator before it was accepted here.
Review only; no code touched.

## Findings

### F1. The `via` concession in section 7 is wrong — critical

- **File:** `docs/dm-reliability-and-store-node-verdict-20260913.md` section 7
- **Severity:** critical
- **Failure scenario:** The document concedes the airtime argument on the grounds that `--via`
  turns a retry into a directed path. It does not. Every relay **overwrites**
  `msg_destination_path` with `msg_destination_call` and re-runs `checkVia()` from its **own**
  `bVIA`/`node_via` (`src/lora_functions.cpp:1460-1462`). The two branches that would auto-derive
  a next hop — the `HG,` gateway branch and the mheard/NCT branch — are block-commented out, dated
  2026-07-22 (`src/via_functions.cpp:109-146`). So sender-side `--via` constrains **hop 1 only**;
  from hop 2 the frame floods normally unless every intermediate node is separately via-configured.
  Worse: if the named via node is down, `checkMesh()` returns false at every other node
  (`src/via_functions.cpp:82`) and nothing relays at all — the ladder then fails silently and
  permanently, with no fallback to flood.
- **Fix:** Rewrite section 7. `via` is an infrastructure property of a fully configured corridor,
  not a sender-side setting, and it introduces a single point of failure. The airtime concession
  must stand or fall on measurement alone.

### F2. No total cap; the frame count for one DM is unbounded — critical

- **Severity:** critical
- **Failure scenario:** Section 3.2 asks for a cap but section 10 leaves it open. At the assumed
  3-minute group gap the ladder fires roughly 1,170 sender frames for a single DM over 24 h. With
  F1 removed, each is a full flood in the default configuration. The channel time demanded exceeds
  the calendar window it runs in. The 2026-09-09 proposal's "at most 6 blind re-floods per DM per
  day" also contradicts its own "+2, +10, +30, +60 min, then hourly" schedule; the verdict drops
  both without substituting a number.
- **Fix:** Name the cap before anything is built. The store-node side is bounded (max 3 deliveries
  x number of holders); the unbounded part is entirely sender-side.

### F3. Section 6 silently deletes the store-node concept's coordination caps — high

- **Severity:** high
- **Failure scenario:** Concept §3.5 makes four caps mandatory ("without the per-node ceiling the
  role is a runaway-node amplifier"): one delivery per (source, NNN) per 10 min, max 3 per entry,
  one mailbox action per 30 s, max 20 per hour. Verdict section 6 replaces the schedule with "the
  ladder from 3.2 applies here too" — the sender's uncapped 3x40 s ladder — and section 11 does
  not list the swap as a deliberate correction.
- **Fix:** Either keep §3.5's caps explicitly or state the replacement and its ceiling in §11.

### F4. T2's stated mechanism was refuted, then re-confirmed by a different route — high

- **Severity:** high
- **Failure scenario:** A finder refuted T2 by grepping `insertOwnTx()` call sites and concluding
  beacons consume no own-ring slots. That is wrong: `sendPosition()` — the periodic beacon —
  writes the ring **inline** at `src/loop_functions.cpp:4829-4831`, and `sendAPPPosition()` at
  `:4902`, bypassing the helper. Beacons do consume slots, unconditionally. T2 stands as written.
- **Fix:** Keep T2. Add the inline-write citation so the next reader does not re-refute it.
  Recorded below under refuted claims.

### F5. The nRF52 timer-task premise is false — high

- **Severity:** high
- **Failure scenario:** Section 1 point 3, T4 and stage 0 inherit advisor M5's claim that the ACK
  path runs in the 1 kB FreeRTOS timer-service task. It does not: the DIO IRQ gives a semaphore
  that wakes a dedicated 16 kB `_lora_task` (`src/boards/mcu/board.cpp:498`), which calls
  `Radio.BgIrqProcess()` -> `OnRxDone()` -> `SendAckMessage()`. M5's stack-overflow hazard does not
  apply, and this is inherent to the vendored SX126x-Arduino library, not a recent fix. A stale
  comment at `src/nrf52/nrf52_main.cpp:3040` still asserts the old belief.
- **Fix:** Drop the task-context justification. The flash argument stands on wear alone — and is
  stronger than stated: all eight `node_msgid++` sites are followed by `save_settings()`; on nRF52
  that re-reads the whole struct from LittleFS and, because `node_msgid` always differs, performs a
  full remove+rewrite every call; ESP32 does ~20 NVS `put*()` per send with no change detection.
  Neither platform batches or rate-limits.

### F6. Fresh msg_ids shorten the dedup ring's safe window fleet-wide — high

- **Severity:** high
- **Failure scenario:** `is_new_packet()` (`src/dedup_functions.cpp:24-52`) is a linear scan of a
  count-based ring with no per-entry timestamp. The measured 39.6-48.5 min safe corridor is
  therefore a function of arrival rate at today's traffic. A ladder that mints a fresh msg_id per
  attempt raises the distinct-id arrival rate at **every relay and gateway on the path**, not only
  at the destination, pushing busy nodes' real rotation window below the measured corridor. That
  degrades dedup for unrelated traffic — HEY, positions — on nodes that are not party to the DM.
  Neither document budgets or measures this.
- **Fix:** Add it to the measurement plan in section 7 and to the risks. It is the strongest
  technical argument for spacing attempts, independent of round-trip time.

### F7. New settings must not touch the settings struct — high

- **Severity:** high
- **Failure scenario:** Section 7 of the concept introduces `--store`, `--storecall`, `--storetime`
  and `--storeslots`. Adding fields to `struct s_meshcom_settings` forces a
  `FLASH_STRUCT_VERSION` bump, which runs `clear_flash()` on every updating node — callsign, WLAN,
  sensors — the exact incident recorded at `src/configuration_global.h:102-108`. This is advisor
  B1, which the verdict drops.
- **Fix:** Prescribe the established pattern, already documented at
  `src/configuration_global.h:113-118`: own NVS keys or spare bits in existing fields, layout
  unchanged, as `max_hop_text` did.

### F8. D2's "minimal hop count" is too vague to implement correctly — medium

- **Severity:** medium
- **Failure scenario:** `--maxhop` is clamped to 1..6 (`src/maxhop.h:17-18`). Implemented through
  the standard setter, a "minimal" delivery would floor at 1 and permit one relay hop, defeating
  the direct-neighbour intent and letting several store nodes' deliveries propagate. The raw wire
  field of 0 is valid and relays skip it explicitly (`src/lora_functions.cpp:1431`, reason `hop0`
  at `:1526`).
- **Fix:** Say `max_hop 0` on the raw field, as the original concept did.

### F9. D3 and T9 describe different products — medium

- **Severity:** medium
- **Failure scenario:** D3 records the custody ACK as "display only", implying the sender's app
  shows a third state. T9's adopted option A emits nothing on air, so the sender sees nothing at
  all; the notice exists only on the store node's own mailbox page. The decisions table and the
  body disagree about what the operator gets.
- **Fix:** Restate D3 as "no on-air custody notice in v1; the store node logs it locally", and move
  the display-only variant to stage 4 where it belongs.

### F10. D1 and section 7 describe different ladders — medium

- **Severity:** medium
- **Failure scenario:** D1 records "keep 3x40 s as specified" as settled; section 7 then proposes
  gating the fast rate on evidence and stretching the gap otherwise. That is a different,
  conditional ladder under the same name. Section 10 still lists the gap and the cap as open.
- **Fix:** Present the evidence gate as a proposal requiring a decision, not as the recorded
  decision. Keep the two visibly separate.

### F11. The store node has no payload check on its own dedup — medium

- **Severity:** medium
- **Failure scenario:** Section 3.3 mandates a payload-length or CRC comparison for the
  destination's (source, NNN) cache because `node_msgid` wraps at 1000. The store node's own dedup
  on (source, NNN) — concept §3.3 step 1 — has no such check, and it holds entries for up to 24 h,
  a far longer window. A chatty sender that wraps NNN inside the hold window can have two unrelated
  messages merged into one mailbox slot, and the wrong one delivered.
- **Fix:** Apply the same payload comparison to the mailbox, with the longer window stated.

### F12. Missing failure modes — medium

- **Severity:** medium
- **Failure scenario:** None of the documents address: a store node that is also a gateway,
  replaying a stale DM to the server hours later; a store node with a wrong clock computing
  mailbox age and hold time (the known NC-01 bad-clock class); a destination that changes SSID,
  which stalls delivery permanently with no operator signal; two store nodes in a hidden-terminal
  geometry, where jitter cancellation cannot work by physics rather than by implementation —
  contradicting bench expectation 5 in both documents; QRT latched for hours, which stalls both
  delivery paths at once and drops silently at `storetime`, precisely during the congestion EMCOMM
  cares about; and a group message that parses as a DM, the bug class of the 2026-09-11 broadcast
  incident.
- **Fix:** Add as risks; the hidden-terminal case also needs the bench expectation softened.

### F13. `heard` mode in v1 is a privacy regression without a stronger GUI gate — medium

- **Severity:** medium
- **Failure scenario:** The concept scoped the mailbox to `own`/`list` partly because §6 flags
  that `heard` "reopens" DM metadata exposure. D4 promotes `heard` to v1 without strengthening the
  owner-only gate. The page then shows source, destination and NNN for third-party DMs to an
  operator who is not a party to them.
- **Fix:** Keep the owner-only auth requirement explicit and state what the page must not show.
  Also: no manual purge or force-deliver action is specified, and "dropped by cap" conflates the
  delivery cap with the storage-slot cap — two counters.

### F14. Citation and capacity errors — low

- **Severity:** low
- **Fix:** Row 10 of the section 4 table cites `loop_functions.cpp:4979-4982`, which is unrelated
  UDP-out code; the enqueue is at `:4972-4975`. The mheard capacity row lists four tiers; only two
  ship — 80 (ESP32-S3 / nRF52) and 30 (everything else). `ENABLE_XML` is set only by the native
  unit-test env (`platformio.ini:415`); `ENABLE_SBUFFER` and `ENABLE_TBEAM` are defined nowhere, so
  the `MAX_RING` 10 and mheard 50/10 branches are dead. Several rows are 1-2 lines off; harmless.

### F15. Stage dependencies are understated — low

- **Severity:** low
- **Fix:** Stage 1 cannot be completed until stage 0's measurement has run, because two of its
  defining parameters (group gap, total cap) are open. Stage 3 has an undeclared dependency on a
  `mheard_functions.cpp` change for the gateway-injection guard (see the T8 note below). Stage 0
  itself verifies as genuinely self-contained and shippable alone.

## Resolved open questions

- **T7, "does mheard record under the relaying or the originating callsign?"** Resolved:
  `mheardLine.mh_callsign = aprsmsg.msg_source_last` (`src/lora_functions.cpp:729`), the last hop.
  mheard is therefore already a directly-heard table by construction — `heard` mode needs no new
  state and no mheard change, and the role is intrinsically limited to RF neighbours, which agrees
  with D2. Remove the open question; keep the eviction caveat.
- **T8 needs one guard the document omits.** Path fields are populated only for payload types
  `:`, `!`, `@` (`src/aprs_functions.cpp:155`), and relays always append before retransmit, so
  path length 1 does mean "not yet relayed" for those types. But nothing checks `msg_server` /
  gateway injection first, so server-injected traffic presents path length 1 without ever having
  been heard over RF — the OE1XAR-62 gwflood confound. A store node that is also a gateway would
  learn phantom neighbours.

## Refuted claims (do not re-investigate)

- **"T1 is refuted because retries reuse the frame byte-for-byte and never mint a fresh msg_id."**
  Refuted as a refutation: that is current behaviour, which the document states in 3.1 and which
  the whole change replaces. T1 is a claim about the proposed design and stands. The finder did
  confirm the matching rule — `findAndStopRingSlot()` compares the full 32-bit msg_id read from
  bytes 3-6 of the stored frame, and the text-ACK and binary-`0x41` paths share it — which is
  exactly why the outbox must be keyed on NNN.
- **"T2 is refuted because `sendPosition()` never calls `insertOwnTx()`."** Refuted by
  `src/loop_functions.cpp:4829-4831`: the beacon writes `own_msg_id` inline via `memcpy`, not
  through the helper. Same at `:4902` for `sendAPPPosition()`. A call-site grep of the helper
  misses both.
- **"`MAX_RING` may be 10, so 'own_msg_id holds 20 entries' is overstated."** Refuted: the
  `ENABLE_TBEAM` branch is never compiled (not defined in `platformio.ini`). 20 on every shipping
  environment.
- **"The nRF52 ACK path runs in the timer-service task" (advisor M5).** Refuted, see F5.

## Sound as written

T5 (the dedup gate must be split), T9 and its option table including the `:stoNNN` fall-through,
T10, section 3.3's time-ageing requirement, the section 11 correction that no gateway emits `0x41`
for a DM, and the stage 0 contents.
