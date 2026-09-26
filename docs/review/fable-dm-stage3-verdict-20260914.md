# DM stage 3 — Fable Verdict (2026-09-14)

Subject: commit `59f21e5b` (wave S3-1, store node), diff `3d63d2db..59f21e5b`. Read against
`docs/dm-stage3-wave-plan-20260914.md` (data model, state machine, hooks, recon, S3-1 gate notes),
`docs/dm-transport-impl-plan-20260913.md` stage 3 and D2-D8, concept §3.3-3.6, traps
T7/T8/T9/T12/T13/T14. Refuted claims from the three earlier verdicts were not re-raised.

Native run by the advisor: `pio test -e native -f test_msgstore` 28/28. No board build run here
(orchestrator owns `.pio/build`).

## Verdict: APPROVED (re-check of 96050d72; the original verdict on 59f21e5b was REWORK)

One critical, one high, two medium. The core is sound against D5/§3.5 in every arithmetic the
brief asked for, but on every normal ESP32-S3 image the loop task never calls it, and the hook gate
copied from the plan excludes exactly the DMs the role exists for.

## Finding 1: `msgstoreLoop()` is unreachable on every non-EXTERNAL_RADIO ESP32 build

- **File:** `src/esp32/esp32_main.cpp:4089-4115` (the call at `:4101`), `:2130`, `:2157-2165`
- **Severity:** Critical
- **Failure scenario:** The orchestrator's gate edit put `msgstoreLoop()` next to the
  `updateRetransmissionStatus()` at `:4099`, which is the EXTERNAL_RADIO re-creation of the tick
  (`#if defined(EXTERNAL_RADIO)` at `:4089`, `#endif` at `:4115`; only `platformio.ini:687,701`
  define it). The tick every shipping S3 image runs is the one inside `if(bRadio)` at `:2130`,
  `:2157-2165`, which got no hook. On Heltec V3 and T-Deck Plus the mailbox stores, arms on
  presence, and then nothing: no ladder, no cooldown release, no storetime expiry, no
  `dropped_*` counter ever moves, the page shows ARMED forever. The RAK4631 site
  (`src/nrf52/nrf52_main.cpp:1308`) is in the unconditional tick and is correct. Both platform
  builds compiled, so the build gate cannot see this; only T-3.1 on an ESP32 would.
- **Fix:** Add the guarded `msgstoreLoop()` after `updateRetransmissionStatus()` at `:2159`
  (inside the `bRadio` tick), keep the EXTERNAL_RADIO copy. The two ticks are mutually exclusive
  per iteration through `retransmit_timer`, so no double call.

## Finding 2: the `!msg_server` gate on the store and purge hooks excludes every gateway-touched DM

- **File:** `src/lora_functions.cpp:1200` (gate), `:1207-1228` (store/purge inside it);
  `src/udp_functions.cpp:356-359`, `src/nrf52/nrf_eth.cpp:484-487`, `src/lora_functions.cpp:1577`
- **Severity:** High
- **Failure scenario:** On the wire the 0x80 bit means "this copy passed a server-connected
  gateway": a gateway emitting a server frame sets it (`udp_functions.cpp:358`) and a
  gateway relaying an RF frame sets it (`lora_functions.cpp:1577`). A DM sent from the app via
  the server (the primary case for a mailbox: sender not on RF) reaches a store node as
  `SENDER,GW>DST` with 0x80 and is never stored; a DM relayed once by any gateway-relay is never
  stored; an `:ackNNN` that arrives via a gateway relay never purges. A store node that is
  itself a gateway sees the server copy on the UDP path, which has no hook at all. Net: the role
  works for pure-RF, non-gateway-relayed DMs only.
  The plan's T8 rationale ("server-injected traffic presents path length 1") does not hold in
  OnRxDone: the emitting gateway appends its own call (`udp_functions.cpp:356-357`), so such a
  frame has a comma and already fails the presence test at `:884`. The `msg_server` guard is
  redundant on the presence hook and harmful on store/purge. This is a plan error faithfully
  implemented, not an owner-A slip — but it has to be fixed before bench.
- **Fix:** Drop `!aprsmsg.msg_server` from the block at `:1200` (keep it on the presence hook
  at `:884` if the orchestrator wants belt-and-braces; it costs nothing there). Update the wave
  plan's hook 1 and recon bullet accordingly. Peer-cancel is unaffected: `glueDeliver()` builds
  with `msg_server` false (`initAPRS`, `src/aprs_functions.cpp:102`).

## Finding 3: `next_ms` comparisons are not millis-wrap safe

- **File:** `src/msgstore.cpp:445`, `:458`, `:461`, `:594`, `:601`;
  `src/web_functions/web_functions.cpp` (mailbox row, `e->next_ms > now_ms`)
- **Severity:** Medium
- **Failure scenario:** Five comparisons use `<`/`>`/`>=` on raw `uint32_t` instead of the
  `(uint32_t)(now - x)` idiom the same file uses correctly at `:149`, `:383`, `:433`, `:473`.
  This is a 24/7 role, so the 49.7-day wrap is a certainty, not a corner. Two outcomes: (a) a
  COOLDOWN or LADDER entry whose `next_ms` was computed shortly before the wrap while `now` has
  wrapped is "not due" until `now` catches up in 49 days — i.e. until storetime drops it
  (`:433` is wrap-safe), so a live message dies silently; (b) an entry whose `next_ms` wrapped
  while `now` has not yet fires at the next tick — a 1 h cooldown collapses to 2 s. (b) stays
  inside the 30 s / 20 per h node caps, (a) is a lost delivery. `msgstoreNextActionInMs()` and
  the page's "next" column misreport across the same boundary.
- **Fix:** `if((int32_t)(now - s_entries[i].next_ms) < 0) continue;` at `:458`, the mirror at
  `:445` and `:601`, and compare candidates by `(int32_t)(a - b)` at `:461`/`:594`. One native
  case with `g_now = 0xFFFFF000` across a ladder step and a cooldown release.

## Finding 4: nRF52 — receive-path hooks mutate the table under a running `deliver()`

- **File:** `src/msgstore.cpp:492-515` vs `:345-414`; `src/lora_functions.cpp:1207-1244` (LORA
  task on nRF52 per MEMORY "nRF52 OnRxDone in LORA task")
- **Severity:** Medium (nRF52 only; ESP32 runs OnRxDone from `esp32loop()`)
- **Failure scenario:** `msgstoreLoop()` picks `candidate`, calls `s_env->deliver()` (String
  building + `encodeAPRS` + ring enqueue, hundreds of microseconds), then writes
  `state/attempt/next_ms` of that slot. In that window the LORA task can run
  `msgstoreOnAck()` (slot → FREE, `:361`), `msgstoreStore()` replace-in-place (`:303-311`, torn
  payload copied by `String(e->payload)` in the glue), or `msgstoreOnPeerDelivery()` (→ HELD).
  After `deliver()` returns the loop unconditionally does `attempt++` and, at `attempt >= 9`,
  `state = COOLDOWN` (`:501-507`) — on a slot the ack just freed. Result: a purged entry
  resurrected into COOLDOWN, delivered again after the next presence; or a HELD (peer-cancelled)
  entry silently promoted to LADDER-with-attempt-count. There is no critical section anywhere
  in the module and the concept's §3.5 last bullet ("on nRF52 nothing is done in the receive
  path") was consciously overridden by the wave plan's hooks, so the cross-task write has to be
  tolerated by design, not by luck.
- **Fix:** Cheapest: snapshot `(src, dst, nnn, state)` before `deliver()`, and after it returns
  advance the slot only if the four still match and `state` is still ARMED/LADDER; otherwise
  record the action (the frame is on the ring) and return. Also copy the entry to a stack
  `MsgStoreEntry` before handing it to `deliver()` so the glue never reads a slot the LORA task
  may be rewriting. Native case: a fake `deliver()` that calls `msgstoreOnAck()` re-entrantly and
  asserts the slot stays FREE.

## Finding 5: delivery is enqueued READY (priority CRITICAL) and flipped to DONE, against the plan

- **File:** `src/msgstore_glue.cpp:91-98`; `src/txring_functions.cpp:109-111`, `:148`
- **Severity:** Low
- **Failure scenario:** The plan (hooks item 5, gate notes) says "enqueues with
  `RING_STATUS_DONE`". The glue copies the `SendAckMessage()` idiom instead — enqueue 0x00, then
  `ringBuffer[slot][1] = 0xFF` — inheriting its documented N-14 race window. That idiom exists
  because an ACK must classify CRITICAL; a mailbox frame classified through the READY path is a
  "personal DM" (`txring_functions.cpp:148`) and also gets CRITICAL, i.e. it jumps ahead of
  relays and the node's own beacons. A store node holding several ARMED entries at a busy hour
  now front-runs the relay queue with third-party traffic. Enqueuing DONE directly is what the
  relay does (`lora_functions.cpp:1553` per the stage-0 verdict) and yields NORMAL with no race.
- **Fix:** `addTxRingEntry(buf, len, RING_STATUS_DONE, "mbox")`, drop the `0xFF` write and the
  comment.

## Finding 6: a peer's hop-0 delivery seeds a new entry on every listening store node

- **File:** `src/lora_functions.cpp:1207-1244`, `src/msgstore.cpp:317-338`
- **Severity:** Low
- **Failure scenario:** The store hook runs before the peer-cancel hook and does not look at
  `rly_hop`/path. Store node B hears A's delivery `SRC,A>DST` (hop 0, dst in B's heard set) and,
  holding nothing for it, stores a fresh entry (`:317`). With `heard` mode and several store
  nodes in range, one delivery fans out into N retained copies, each with its own ladder at the
  next presence; peer-cancel only reins in the jitter phase. T14 accepted a hidden-terminal
  duplicate, not N-fold retention seeded by the delivery itself. When B does hold it, the same
  frame counts once as `refreshed` (`:297`) and once as `cancelled_peer` (`:412`).
- **Fix:** In the hook, skip `msgstoreStore()` when `rly_hop == 0 && path has a comma` (that is
  the peer-delivery signature the next block already computes); let that frame only reach
  `msgstoreOnPeerDelivery()`.

## Finding 7: `blocked_bp` counts every 2 s tick and every gate, not "blocked by QRS/QRT"

- **File:** `src/msgstore.cpp:471-487`; page label `web_functions.cpp` "blocked QRS/QRT"
- **Severity:** Low
- **Failure scenario:** While a candidate is due and any of the four gates refuses (30 s gap,
  20/h, bp, util), the counter increments on every loop call — up to 1800/h under a held
  ceiling — and the page names it "blocked QRS/QRT". The same flag feeds `dropped_cap`, so a
  30 s-gap refusal followed by a later success is harmless, but a util-only block for the hold
  time is reported as "the 20-per-hour ceiling ate a hold time". Not wrong, mislabelled.
- **Fix:** Count once per transition (set the per-slot flag only when it was clear) and split
  the label, or rename to "blocked (gap/cap/bp/util)".

## Finding 8: `--storeslots` below the used range strands live entries

- **File:** `src/msgstore.cpp:428`, `:454`, `:556`, `:173-178`
- **Severity:** Low
- **Failure scenario:** Every loop iterates `< s_slots`. Lowering slots from 50 to 10 while
  slots 10..49 hold entries makes them invisible: no expiry, no delivery, no ack purge, not in
  `used`, but still not FREE. Raising slots later resurrects them (and storetime then drops the
  stale ones). `msgstorePurgeAll()` clears all 50, `msgstoreEntry()` still returns them.
- **Fix:** In `msgstoreConfigure()`, FREE every slot `>= new s_slots` and count them as
  `dropped_storetime` (or refuse the shrink while those slots are live and say so in `[ERR]`).

## Item-by-item answers to the brief

1. **Core vs D5:** ladder 0/40/80/180/220/260/360/400/440 s CONFIRMED (`:511-514`, test
   `kGaps`); 1 h cooldown then HELD CONFIRMED (`:505-507`, `:445` modulo Finding 3); one action
   per call CONFIRMED (single candidate, `:449-466`); 30 s gap and 20/h ring CONFIRMED
   (`:144-164`, `:473-475`; the ring of 20 makes a 21st in any trailing hour impossible by
   construction); bp `!= 0` and util `> 25` CONFIRMED (`:477-480`, QRS included — stricter
   than `bp_state.refusing()`, matches the plan text); presence only from HELD and only after
   60 s CONFIRMED (`:377`, `:383`); peer cancel from ARMED and LADDER CONFIRMED (`:403`);
   refresh/replace/drop-when-full CONFIRMED (`:290-342`); `dropped_cap` vs
   `dropped_storetime` CONFIRMED with the caveat of Finding 7; 12 h edge `age < window`
   CONFIRMED (`:254`, test at exactly the window); OWN = base call, LIST = SSID-exact or
   base-any CONFIRMED (`:91-142`); NULL safety CONFIRMED (`:265`, `:347`, `:370`, `:398`,
   `:520-552`). More than 20 frames/h: REFUTED (only `:492` transmits; web deliver-now sets
   ARMED and still passes the gate). TX while QRT latched: REFUTED (`:477`). Leak: REFUTED
   (`:433` frees every state; `hold_ms` max 604,800,000 fits) — but Finding 8 strands entries
   and Finding 3 lets a live entry ride to storetime. Rollover in every comparison: REFUTED
   (Finding 3).
2. **Glue `deliver()`:** msg_id from `millis()` CONFIRMED (`:71`); source call = original
   sender CONFIRMED (`:72`, path `SRC,OWN` `:73`); raw `max_hop` 0 after `initAPRS` CONFIRMED
   (`:69`, `:81`). Byte 5 = `0x00 | (bMESH ? 0x10 : 0)` (`aprs_functions.cpp:1262-1274`); no
   receiver rejects on hop 0 or on flags — the only `max_hop` consumer on RX is the relay
   decision (`lora_functions.cpp:1669` reason `hop0`), so no flag bits are needed for the
   destination. Relay precedent sets `msg_server` when it is a gateway; the mailbox does not,
   which is right (another gateway may upload it). Payload `text{NNN` CONFIRMED (`:77-79`).
   READY-then-DONE: CONFIRMED same race as `SendAckMessage()`, but see Finding 5 — the plan
   asked for DONE and the CRITICAL classification is unwanted. No `insertOwnTx`, no
   `addNodeData`, no `0x41` in the diff: CONFIRMED (grep of `+` lines finds only comments).
   Destination handling: fresh msg_id → NEW arm; (src, NNN, payload) unseen at an absent
   destination → display + `SendAckMessage(original sender)`; already seen → DUP → limited
   re-ack (`lora_functions.cpp:1118-1160`). Either way the ack is
   `DST>SENDER:` `SENDER   :ackNNN` (`loop_functions.cpp:4970`); the store node hears it in the
   non-own branch, `indexOf(":ack")` = 9 > 0, `msgstoreOnAck(source_call = acker,
destination_call = sender, nnn)` (`lora_functions.cpp:1209`) vs core `dst == acker && src ==
sender` (`msgstore.cpp:356-358`): argument order CONFIRMED correct, test
   `test_ack_correct_acker_purges` covers it. Caveat: only if the ack copy that arrives first
   is not gateway-flagged (Finding 2).
3. **Hooks:** presence — for `:`/`!`/`@` frames `msg_source_call` is the first path token and
   equals the whole path when there is no comma (`aprs_functions.cpp:201-241`); other types
   never reach this block (`msg_type_b_lora == 0`), and the core rejects an empty call. Store
   hook order (ack → else store → peer) and the `startsWith("{")` guard CONFIRMED
   (`:1207-1218`), placed before the relay decision CONFIRMED (`:1553+`). Peer cancel: a
   sender's own hop-0 DM has no comma → REFUTED as a false trigger; a normally relayed DM
   arriving with hops exhausted (`rly_hop == 0`, comma) DOES fire it — but the store hook
   refreshed the same entry to HELD one line earlier, so the state is right and only the
   counter is off (Finding 6 second half). nRF52 heap churn: three `substring()` temporaries
   per matching frame plus one `String` for the stripped payload — same order as the existing
   own-destination branch; the table scan is 50 `strcmp`s. Acceptable, but it is receive-path
   work (Finding 4 is the consequence). T8 "all four skipped for msg_server": CONFIRMED as
   implemented — and that is Finding 2.
4. **Loop sites:** nRF52 `:1308` inside the unconditional 2 s tick CONFIRMED. ESP32: REFUTED,
   Finding 1. Cadence: a 2 s tick against 40 s steps and a 30 s gap is fine; worst case a step
   lands 2 s late and the next step is re-based on the actual send (`:514`).
5. **Settings:** ESP32 uses a second `Preferences` object on the same namespace, opened and
   closed per call (`msgstore_settings.cpp:46-67`); `esp32_flash.cpp` closes its handle at
   `:334` before `init_flash()` returns, `msgstoreSettingsLoad()` runs after that
   (`esp32_main.cpp:812-818`), and NVS allows concurrent handles anyway — CONFIRMED safe. Keys
   ≤ 15 chars. nRF52 `/msgstore.cfg`: size check before read, magic check, terminator forced,
   read-compare-write, own `File` — CONFIRMED (`:100-174`). Save only from `commandAction()`
   CONFIRMED (grep: no other caller). Struct untouched: CONFIRMED (`git diff` on
   `WisBlock-API.h`, `esp32_flash.h`, `configuration_global.h` is empty). Load after
   `msgstoreGlueInit()` and before the radio starts on both platforms CONFIRMED
   (`esp32_main.cpp:815-817`, `nrf52_main.cpp:523-525`).
6. **Commands:** `storecall `/`storecall`/`storetime `/`storetime`/`storeslots `/`storeslots`
   before `store off|own|list|heard` before `store` CONFIRMED (`command_functions.cpp:4614-4770`);
   no earlier `commandCheck` prefix collides (`s…` prefixes listed: settime, setinfo, setlog,
   shortpath, spectrum, save, sht21, setname, softser…). Clamps 1..168 and 1..50 CONFIRMED,
   offsets `+12`/`+12`/`+13` CONFIRMED. Warning + heap line CONFIRMED (`:229-246`; the nRF52
   `dbgHeapTotal/Used` externs are declared in `nrf52_main.cpp:75-76`, orchestrator's RAK build
   is the link proof — UNVERIFIABLE-HERE). `--mbox` prints dst/src/nnn/state/cycles.attempt/age,
   never payload CONFIRMED. Ineligible `[STORE];unavailable` CONFIRMED. Web: per-row fixed
   buffers and `printf`, no `String` per row CONFIRMED (one `String(int)` per page in the setup
   card only); all pages and `/callfunction/`, `/setparam/` behind the single password session
   (`web_functions.cpp:613-660`) CONFIRMED; payload never rendered (only `plen`) CONFIRMED;
   `confirm()` on purge, purge-all, deliver, and on leaving `off` CONFIRMED (switching TO off
   has no confirm — acceptable); guards: every `mailbox`/`MBOX` string is inside
   `ENABLE_MSGSTORE`, the unguarded block is the `.mbx-*` CSS only — CONFIRMED by grep
   (orchestrator's T-Beam string scan is the image-level proof). setparam mapping for
   store/storecall/storetime/storeslots through `commandAction()` CONFIRMED
   (`web_setup.cpp:376-406`). Nit: callsigns from the air land in `onclick="…confirm('…%s…')"`
   unescaped; `decodeAPRS` rejects non-callsign characters (`aprs_functions.cpp:263-276`), so
   the vector is closed at ingress — same exposure the messages page already has.
7. **Tests:** `test_format_line_content`'s `strchr(buf, ';') == NULL` cannot fail (the format
   string has no `;`). `g_deliver_last_src/dst/nnn` are captured and never asserted — nothing
   proves `deliver()` gets the right entry. No case for: presence ignored while ARMED/LADDER/
   COOLDOWN, ack purge from LADDER/COOLDOWN, two entries due in one call (one-per-call is only
   covered indirectly through the 30 s gap), slot shrink (Finding 8), wrap (Finding 3),
   re-entrant hook during `deliver()` (Finding 4). `fakeRandomBetween` returning `lo` makes
   timing exact but hides a swapped `(lo, hi)` or an off-by-one in the real
   `glueRandomBetween()` (`random(lo, hi + 1)`, Arduino exclusive upper bound — reads correct);
   one assertion with a fake returning `hi` would pin the upper edge.
8. **Native:** 28/28 PASSED, 0.81 s.

## Refuted claims (do not re-investigate) — with evidence

- **"The 20/h ring can undercount and let a 21st frame through."** Refuted: the ring holds
  exactly 20 timestamps; a 21st action in any trailing hour requires all 20 to be < 1 h old,
  which `actionsInLastHour()` counts as 20 and blocks (`msgstore.cpp:144-153`, `:475`).
- **"Web deliver-now bypasses the node caps."** Refuted: `msgstoreDeliverNow()` only sets
  ARMED + `next_ms = now` (`:541-552`); the only transmit is in `msgstoreLoop()` after the gate.
- **"A hop-0 T-Deck DM from the sender falsely peer-cancels."** Refuted: direct frame has no
  comma; the hook requires one (`lora_functions.cpp:1233`).
- **"`msgstoreOnAck` argument order is swapped at the hook."** Refuted: hook passes (ack source
  = acker, ack destination = original sender) (`:1209`), core matches `dst == acker`,
  `src == sender` (`msgstore.cpp:356-358`).
- **"The destination needs mesh/server flag bits in byte 5 to accept the delivery."** Refuted:
  no RX path tests byte 5 except the relay decision; DM handling keys on
  `destination_call == node_call` (`lora_functions.cpp:1031`).
- **"`initAPRS` leaves `max_hop` at the text default."** Refuted: `initAPRS` seeds
  `max_hop_text` (`aprs_functions.cpp:99-101`), the glue overwrites with 0 (`msgstore_glue.cpp:81`),
  `encodeAPRS` masks `& 0x0F` (`:1262`).
- **"Two `Preferences` handles on `Credentials` collide."** Refuted: begin/end per call in both
  modules, never overlapping (`esp32_flash.cpp:65,334,339,348,353,612`;
  `msgstore_settings.cpp:46-67`), and NVS supports multiple handles per namespace.
- **"`--store` prefix swallows `--storecall`."** Refuted: chain order at
  `command_functions.cpp:4614-4770` tests the longer prefixes first.
- **"The ESP32 hooks race the loop task like nRF52."** Refuted: `OnRxDone()` runs from
  `esp32loop()` via `checkRX()` (stage-0 verdict, `esp32_main.cpp:4181/4225`); Finding 4 is
  nRF52-only.
- **"`hold_ms` overflows at 168 h."** Refuted: 604,800,000 < 2^32.

## Bench-only items (map to T-3.1..T-3.9)

- T-3.1 (one delivery within a beacon interval, ack reaches sender): the only proof that
  Finding 1 is fixed on ESP32 and that Finding 2's gate removal lets a gateway-relayed DM in.
  Run once with the Heltec as store node, once with the RAK.
- T-3.2 (relay logs `hop0`): frame shape verified here; the relay's skip is `:1669`.
- T-3.3 (9 sends, 1 h cooldown, 9 more, drop at storetime): native covers the arithmetic; the
  bench proves the 2 s tick cadence and the DONE-slot single transmission.
- T-3.4 (two store nodes, peer cancel; mutually airgapped): also observe Finding 6 — count
  `stored` on the second node after the first's delivery.
- T-3.5 (store node as gateway, server-injected unknown callsign): after Finding 2, confirm the
  UDP path still has no store hook and that no phantom presence arises.
- T-3.6 (reboot with pending entries, RAM warning shown): `[STORE];warning` and `heap` lines.
- T-3.7 (destination on official 4.35t): the delivery's acceptance and plain `:ackNNN` are
  reasoned from code here; bench is the proof.
- T-3.8 (string scan): T-Beam image has no `mailbox`/`MBOX`; also grep the S3 image for
  `0x41` emission paths with a mailbox source label.
- T-3.9 (25 DMs to an absent destination): ring occupancy flat, and after Finding 5 the
  deliveries should appear at NORMAL priority in the setlog `RLY`/`TX` lines.
- Finding 4's window is not bench-observable at useful rates; the native re-entrancy case is
  the evidence.

## Re-check (96050d72)

Diff `59f21e5b..96050d72`. Native `pio test -e native -f test_msgstore` 41/41 (0.53 s). No board
build run here.

- **F1 — FIXED.** `msgstoreLoop()` now sits in the `if(bRadio)` tick at
  `src/esp32/esp32_main.cpp:2160-2162`, guarded; the EXTERNAL_RADIO copy at `:4104` stays. Both
  ticks key on the same `retransmit_timer`, so at most one fires per iteration.
- **F2 — FIXED.** The `!msg_server` gate is gone from presence (`src/lora_functions.cpp:891`)
  and from the store/purge/peer block (`:1207-1265`). New false triggers checked: a gateway's
  server→LoRa emission keeps the server frame's hop count (`udp_functions.cpp` never assigns
  `max_hop` except the 0x20 flag at `:461`; `nrf_eth.cpp:627` same) and carries a comma
  (`SENDER,GW`), so it neither passes the presence test nor matches the hop-0 peer signature;
  a gateway's own originated frames never set 0x80 (only `:1598`, `udp_functions.cpp:358`,
  `nrf_eth.cpp:487` set it, all on forwarded copies). No new trigger found.
- **F3 — FIXED.** Every `next_ms` compare is signed-difference now: `msgstore.cpp:493`, `:507`,
  `:511`, `:680`, `:688-690`; page `web_functions.cpp:1694`. The `stored_ms` compares were
  already unsigned-difference (`:426`, `:478`, `command_functions.cpp:4781`,
  `web_functions.cpp:1686`). Grep over `src/` finds no raw `<`/`>` on either field.
- **F4 — FIXED, one residual.** `gen` (`msgstore_api.h:55`) is bumped on every hook mutation
  (refresh `:337`, new `:354`, ack `:405`, presence `:431`, peer `:456`, expiry `:487`, purge
  `:612`, purge-all `:622`, shrink `:206`); the loop snapshots `gen` and the entry
  (`:563-571`), hands `deliver()` the stack copy, and skips the ladder stamp when
  `state == FREE || gen != gen_before` (`:579-580`) while still recording the action. Walked
  every hook: `msgstoreDeliverNow()` does not touch `gen`, but it only acts on HELD/COOLDOWN and
  the candidate is ARMED/LADDER, so it cannot hit the in-flight slot. Residual: the few
  instructions between the `gen` check at `:579` and the stamps at `:582-597` remain
  unguarded — irreducible without a lock, orders of magnitude narrower than the `deliver()`
  window, accepted.
- **F5 — NOT CHANGED, accepted as operator decision.** `msgstore_glue.cpp:91-115` documents
  the choice: CRITICAL classification wanted, READY-then-DONE inherits the SendAckMessage N-14
  window (`loop_functions.cpp:4990-5009`). The trade-off is stated correctly; the residual race
  is the same one the ACK path has carried since N-14. Bench T-3.9 should confirm mailbox
  frames do not starve relays at a busy hour.
- **F6 — FIXED, with an over-reach I proposed and now flag.** `bMboxPeerDelivery` is computed
  once at `:1219`, the store hook is gated on `!bMboxPeerDelivery` (`:1235`), the peer-cancel
  runs after (`:1252`). The signature (`rly_hop == 0` and a comma) is also what a sender's DM
  looks like after its hop count is exhausted in the mesh: `MAX_HOP_TEXT_DEFAULT` is 4
  (`configuration_global.h:288`), so a DM heard from the fourth relay, or any DM whose sender
  set `--maxhop` low, is now never stored and cancels a running ladder for that (src, NNN)
  instead of refreshing it. Cheap discriminator available: every sender-originated text id is
  `(_GW_ID << 10) | NNN` (`loop_functions.cpp:4112`, kept by relays), a mailbox id is `millis()`
  (`msgstore_glue.cpp:71`) — `(aprsmsg.msg_id & 0x3FF) == nnn` identifies the sender's frame
  with a 1/1024 miss. Stage 1's fresh ids must keep the low-10-bit NNN for this to hold; note
  it in the stage-1 brief either way. Low; decide before S3-2 closes, not a blocker.
- **F7 — FIXED.** `s_blocked_episode_active` (`:54`) counts once per due-but-refused run
  (`:536-544`), cleared on pass (`:549`) or nothing-due (`:521`); label "blocked by caps",
  setlog key `blk` (`:703`). Test `test_blocked_episode_counted_once_across_five_ticks` fails
  on the old code (5 vs 1).
- **F8 — FIXED.** `msgstoreConfigure()` frees live slots `>= new_slots` and counts them as
  `dropped_slots` (`:200-211`). Test fails on the old code (used 10, entries visible).

### New tests (13): sharpness

Fail-before verified by reading the old code for: `test_wrap_due_check_across_wrap` (old
`next_ms > now` skips), `test_wrap_candidate_pick_across_wrap` (old picks b),
`test_wrap_cooldown_release_across_wrap` (old never releases),
`test_wrap_next_action_in_ms_across_wrap` (old returns ~2^32), blocked-episode, shrink,
`test_deliver_call_receives_entry_fields`, `test_presence_jitter_uses_hi_bound` (a swapped
`(lo, hi)` in the core returns MIN). Coverage-only (pass on old code too, acknowledged in the
file): `test_wrap_storetime_drop_across_wrap`, the three presence-outside-HELD cases.

**Not sharp: `test_reentrant_ack_during_deliver_keeps_slot_free`.** On the old `59f21e5b`
loop the re-entrant ack frees the slot, then `attempt++` makes it 1, which is `< 9`, so the
old code writes `next_ms` on a FREE slot and leaves `state` FREE — `msgstoreEntry()` returns
NULL and all three assertions pass on the unfixed code. The resurrection F4 described needs
the ninth attempt (`attempt >= 9` → COOLDOWN). Fix: drive the entry to `attempt == 8` first
(eight `msgstoreLoop()` calls with the step gaps), then set `g_deliver_reentrant_ack` for the
ninth; or add a peer-cancel variant asserting `state == HELD && attempt == 0` afterwards (old
code leaves HELD with `attempt == 1`). One of the two is owed before S3-2 closes — the code
fix itself is correct by reading.

### Still open

1. Sharpen the F4 regression test as above (test-only change).
2. Decide the F6 over-reach: keep (miss hop-exhausted DMs, simpler) or add the
   `(msg_id & 0x3FF) == nnn` discriminator (one condition in the hook, one native case if the
   core is given the id — or bench-only).
3. Bench T-3.1..T-3.9 as listed, T-3.1 on the Heltec specifically for F1.
