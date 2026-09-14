# DM stage 4 — Fable Verdict (2026-09-14)

Subject: commit `66dea241` (wave S4-1), diff `bf9e689d..66dea241`, against
`docs/dm-stage4-plan-20260914.md` (sections 3-5, 9, S4-1 notes), T9/T10 in
`docs/dm-reliability-and-store-node-verdict-20260913.md`, and the stage 0/2.1/3 verdicts.
Review only; `pio test -e native -f test_sto_notice -f test_msgstore -f test_dm_stats` run here:
3/3 suites PASSED (24 + 51 + all dm_stats cases).

## Verdict: REWORK

One Medium finding (F1) refutes the headline sender-side claim for every gateway sender; the fix
is small and local. Everything else is Low/Info and can ride the same wave.

## Finding 1: the notice is displayed as text on NEW firmware when it arrives via the server

- **File:** `src/udp_functions.cpp:397-475`, `src/nrf52/nrf_eth.cpp:556-639`
- **Severity:** Medium
- **Failure scenario:** The `:sto` parse exists only in the LoRa ingress
  (`src/lora_functions.cpp:1130-1161`). Any gateway that hears the notice uploads it (the frame
  has `msg_server` false, `initAPRS`), the server fans it out, and a sender that is itself a
  gateway (ESP32 UDP path) or an Ethernet RAK (nRF52 path) takes it through the server for-me
  branch: `iAckPos <= 0 && bDmDedupNew` is true (no `:ack`, no `{` so `dmDedupCheck` never runs)
  → `sendDisplayText()` at `:458` / `:624` and `addBLEOutBuffer()` at `:473` / `:639`. The
  notice reaches the display and the phone as a plain DM "DK5EN-93 :sto017 DK5EN-14" from the
  store node. `is_new_packet()` gates only the LoRa forward at `:512`, not the display. Two
  sub-cases: (a) sender also hears the LoRa copy → held mark **and** a stray text; (b) sender is
  out of RF range of the store node and reachable only through a gateway → **never** marked held,
  only the text — which is exactly the old-firmware behaviour the plan says new firmware replaces.
  Gateway senders are the common home setup, so this is not an edge case.
- **Fix:** Mirror the LoRa branch in both server ingress paths: after the `:ack/:rej` block, if
  `stoNoticeParse()` is true on a frame whose `destination_call == node_call`, run
  `checkOwnTx` → state 0x00/0x01/0x04 → `stoHolderNote()` → `own_msg_id[][4] = 0x04` →
  `buildAckPhoneFrame(..., ACK_STATUS_HELD, source)`, set `bDmDedupNew = false` (so neither
  `sendDisplayText` nor the BLE text goes out) and keep `bUDPtoLoraSend = false` (already forced
  for `node_call`). Add the same `stoHolderClear()` on the UDP/Ethernet `:ack` writes
  (`udp_functions.cpp:421`, `nrf_eth.cpp:585`). No native test can cover the ingress glue; add
  bench item T-4.7 below.

## Finding 2: a held give-up still increments `giveup=`

- **File:** `src/lora_functions.cpp:2414-2434`
- **Severity:** Low
- **Failure scenario:** `dmstat_giveup.fetch_add(1)` at `:2414` runs before the 0x04 test at
  `:2423`, so one held give-up prints `giveup=1 giveuph=1`. The plan (§4 "count
  `dmstat_giveup_held` instead") and the commit message ("counts giveuph= instead") describe
  disjoint counters; the header comment in `dm_stats.h:24` says "suppressed". An operator reading
  `giveup=N` as "N failures reported to the phone" is now wrong by `giveuph`.
- **Fix:** Move `dmstat_giveup.fetch_add(1)` into the `else` arm (or document `giveuph ⊆ giveup`
  in `dm_stats.h` and the plan). Add a test only if the arm split is made testable; the format
  test is unaffected.

## Finding 3: `--mbox` line buffer truncates the new trailing `sto=` field first

- **File:** `src/command_functions.cpp:4806-4807`
- **Severity:** Low
- **Failure scenario:** `msgstoreFormatLine()` now carries 15 numbers behind 94 bytes of
  literals; the `--mbox` command formats into `char line[128]`, leaving 34 bytes for the numbers.
  `s_cnt` counters are cumulative since boot, so a store node a few weeks up (`stored=` /
  `refr=` / `deliv=` at 4 digits) crosses 128 and `snprintf` clamps — the field that vanishes is
  the one this wave added. The STAT path uses `dmbuf[200]` (`loop_functions.cpp:3240-3245`) and
  is fine; `printfdeb`'s 300 B format cap is irrelevant because `setlogPrint()` passes the body
  through `%s` (`:3174`).
- **Fix:** `char line[200]` at `:4806` (match the STAT buffer).

## Finding 4: docs name the payload suffix `<holder-call>`; it is the held destination

- **File:** `docs/commands-store-node.md`, section "The `:sto` notice on old firmware"
- **Severity:** Low
- **Failure scenario:** The section writes the payload as
  `"<sender-call> :sto<nnn> <holder-call>"` and then says the sender shows "held by
  `<holder-call>`". The code builds `"%-9.9s:sto%03u %s"` with `e->dst`
  (`msgstore_glue.cpp:144`, `sto_notice.cpp:24`); the holder is the frame's **source** call,
  which is what `stoHolderNote()` records (`lora_functions.cpp:1143`). Anyone writing an app-side
  parser from this doc extracts the wrong callsign.
- **Fix:** `"<sender-call> :sto<nnn> <destination-call>"`, holder = frame source.

## Finding 5: `stoNoticeParse()` accepts the tag anywhere in the text (hardening, optional)

- **File:** `src/sto_notice.cpp:55-67`
- **Severity:** Low
- **Failure scenario:** The tag is matched by `strstr` at any offset, so a for-me DM **without**
  a `{NNN` tag whose text contains `:sto` + three digits is consumed as a notice and never
  displayed. Every firmware-originated DM carries `{NNN` (rejected at `:55`), so the residual
  window is a `{`-less DM — the same permissiveness the `:ack` path has always had
  (`indexOf(":ack") > 0`), hence not blocking.
- **Fix (cheap):** the builder always puts the tag at byte 9 (`%-9.9s` pads or truncates to
  exactly 9), so requiring `tag == payload + 9` closes the window without touching the wire
  format; one test line in `test_parse_lehnt_fehlenden_tag_ab`.

## Finding 6: holder table is never cleared on the server-side ack or on own-table eviction (Info)

- **File:** `src/sto_notice.h:36`, `src/udp_functions.cpp:421`, `src/nrf52/nrf_eth.cpp:585`
- **Severity:** Info
- The header promises a clear "when the own-message table drops it"; no caller exists
  (`insertOwnTx` is untouched) and the UDP/Ethernet `:ack` paths do not call `stoHolderClear()`
  either (folded into F1's fix). Harmless: state 0x02 is never downgraded, the table is 16
  entries with oldest-first eviction. Fix the comment or add the calls.

## Finding 7: replace-in-place never re-arms a notice (Info, spec-conformant)

- **File:** `src/msgstore.cpp:345-361`
- **Severity:** Info
- The wrapped-NNN replace path (`s_cnt.stored++`, fresh cycle) leaves `notice` as it was: if the
  old entry's notice already went out (`notice == 2`), the unrelated new message is never
  announced. Plan §5 carves replace out explicitly, so this is recorded, not raised. Worth one
  line in the plan's accepted limitations.

## Refuted claims (do not re-investigate)

- **"Old firmware acks, stores or mis-parses the notice."** Refuted on `upstream/oe1kbc_4.35t`:
  the for-me branch (`lora_functions.cpp:983-1099` there) tests `startsWith("{ping}")`,
  `startsWith("{pong}")`, `indexOf(":ack") > 0 || indexOf(":rej") > 0`, then
  `indexOf("{", 1) > 0`; the payload `"DK5EN-93 :sto017 DK5EN-14"` fails all four and lands in
  the plain-display arm (`queueDisplayText` + `addBLEOutBuffer(RcvBuffer)`), displayed once, no
  `SendAckMessage`. `{SET}`/`{CET}`/`{MCP}` are `memcmp` on the payload start and live in the
  non-me branch anyway. The upstream UDP for-me branch (`udp_functions.cpp:384-458` there) has
  the same three tests; no `{` → no `SendAckMessage`, displayed once. A relaying old gateway
  emits its usual gateway `0x41` to the notice's source (store node) with the millis() id;
  `checkOwnTx` misses (never `insertOwnTx`), no effect — identical to the stage 3 delivery
  frame.
- **"The charset filter or the 9-char padding breaks the payload."** Refuted: `decodeAPRS()`
  applies `charset_filter_apply(..., CHARSET_FILTER_PLAIN)` (`aprs_functions.cpp:387-390`),
  which strips controls, invalid UTF-8 and bidi/zero-width only (`charset_filter.cpp:19-23`);
  ASCII spaces, `:` and digits pass untouched. The `:ack` frames have used the same `%-9.9s`
  padding for years.
- **"A real DM with `:sto` in its text is swallowed on new firmware."** Refuted for every
  firmware-originated DM: `stoNoticeParse()` rejects any `{` (`sto_notice.cpp:784`) before
  looking for the tag, and `sendMessage()` appends `{NNN` to every DM; the `{`-tagged frame then
  takes the `iEnqPos > 0` arm unchanged. The residual `{`-less case is F5 (hardening).
- **"The reconstructed msg_id differs from the `:ack` rule."** Refuted: both compute
  `((_GW_ID & 0x3FFFFF) << 10) | (nnn & 0x3FF)` (`lora_functions.cpp:1094` vs `:1138`) and
  look it up with `checkOwnTx()`; the messages page uses the same id from the own buffer
  (`web_functions.cpp:2138`), so `stoHolder(aprsmsg.msg_id)` keys consistently.
- **"0x02/0x03 can be downgraded to held" / "an echo downgrades held to heard."** Refuted: the
  `:sto` arm requires state 0x00/0x01/0x04 (`:1142`); the only HEARD write in the tree is
  `:953`, guarded by `!= 0x02 && != 0x03 && != 0x04` (`:949`); the UDP/Ethernet paths write
  only 0x02 (`udp_functions.cpp:421`, `nrf_eth.cpp:585`). The unconditional 0x02 at `:417` is
  the pre-existing `0x41` handler (T9), not a stage 4 regression.
- **"The rate limit is per msg_id, not per (holder, NNN), or breaks at the millis() wrap."**
  Refuted: the key is (msg_id, nnn, holder) with msg_id a function of nnn
  (`sto_notice.cpp:144-146`); the window test is `(uint32_t)(now - noted) < WINDOW` (`:150`),
  wrap-safe, covered by `test_holder_note_millis_rollover`.
- **"The give-up path still sends 0x03 for a held message."** Refuted: `:2423-2434` — the 0x03
  write and the `ACK_STATUS_FAILED` frame sit in the `else` arm. Only the counter is wrong (F2).
- **"A notice can loop between two store nodes" / "a notice gets stored or peer-cancelled."**
  Refuted: the notice's destination is the DM sender, so on the sender it takes the for-me branch
  (`rly_reason = "self"`, never relayed, never the store hook); on any third store node the
  store hook needs `indexOf("{", 1) > 0` (`lora_functions.cpp:1295-1297`) and the peer-cancel
  needs `iMboxTagPos > 0` (`:1271-1275`) — the notice has no `{`. `msgstoreStore()` also
  refuses `src == own` and `dst == own` (`msgstore.cpp:315-317`).
- **"The notice bypasses the node caps or is not counted."** Refuted: `notice_slot` is chosen
  before the gate but the gate (`actionsInLastHour`, 30 s gap, bp, util) runs on the same path
  (`msgstore.cpp:508-580`); on pass `recordAction(now)` and `s_cnt.notified++` (`:597-598`), on
  refuse `notice_blocked++` once per episode (`:574`); `test_loop_sends_notice_before_ladder_step`
  and `test_notice_blocked_episode_counted_once` fail if either moves.
- **"`max_hop` is 0 on the notice."** Refuted: `glueNotify()` never assigns `max_hop`; `initAPRS(m,
':')` seeds `meshcom_settings.max_hop_text` (`aprs_functions.cpp:99-101`, stage 3 verdict).
  Source/path = own call, destination = `e->src`, `msg_id = millis()`, READY then
  `ringBuffer[slot][1] = RING_STATUS_DONE` (`msgstore_glue.cpp:151-174`), no `insertOwnTx`, no
  upload from the store node itself (only `sendMessage()` uploads; `addTxRingEntry` does not).
- **"The notice is not deduped on relays."** Refuted: relays keep the msg_id; the receiver's
  `is_new_packet()` ring rejects the second copy before the for-me branch (stage 0 verdict), and
  `stoHolderNote()`'s one-hour window absorbs a copy that slips through.
- **"A refresh re-notifies."** Refuted: `notice` is written only in the new-slot path
  (`msgstore.cpp:380`); refresh (`:335-341`) does not touch it; `test_refresh_does_not_rearm_notice`
  pins it. Every FREE transition clears it (`:208`, `:408`, `:491`, `:666`, `:677`).
- **"`--storenotice` is swallowed by `--store` / breaks on T-Beam."** Refuted: `commandCheck()`
  truncates at the command length and compares (`command_functions.cpp:161-172`); "store heard"
  (11 chars) vs "storenotice" differs; the two `storenotice` tests sit before the bare `store`
  catch-all (`:4765`, `:4790`, `:4797`). On ineligible boards the `#else` `store` prefix answers
  `[STORE];unavailable` (`:4834`) and the web setparam lives inside `ENABLE_MSGSTORE`
  (`web_setup.cpp:507-546`). String scan of the T-Beam image built at the commit's minute
  (`.pio/build/ttgo_tbeam/firmware.bin`): `storenotice`, `notices sent`, `MBOX`,
  `store_notice` absent; `:sto`, `[HELD]`, `giveuph` present ("Notified" hit is FreeRTOS's
  `ulNotifiedValue`).
- **"MBX1 is misread as MBX2 or the memcmp guard skips the upgrade."** Refuted: `sizeof(V2)` ≠
  `sizeof(V1)` (one appended byte plus alignment), the load branches on the stored size
  (`msgstore_settings.cpp:130-148`), a v1 file yields `notice = 1`; save reads `current` only
  when the size equals `sizeof(V2)` (`:199`), so a v1 file compares unequal to the zeroed
  struct and is rewritten as v2. ESP32 key `store_notice` is 12 chars (< 15 NVS limit), default
  1 on read (`:51`). `(void)v1` at `:166` is a dead variable, cosmetic.
- **"The web switch posts a value `compareTo("on")` cannot match."** Refuted: the switch
  element emits `this.checked?'on':'off'` (`web_functions.cpp:2696`), lowercase, and
  `commandAction` uses `casecmp` anyway.
- **"`htmlEscape(holder)` is needed / unsafe."** Refuted both ways: the holder is the frame
  source call (`[A-Z0-9-]` after `ackAttrCallLen`), so escaping is defensive; `htmlEscape` is
  `static String htmlEscape(const String&)` at `:1300`, the `String holder` conversion is fine
  on nRF52.

## Tests (question 7)

- Every one of the 24 `test_sto_notice` cases and the 10 new `test_msgstore` cases has a
  plausible failing counterpart; I tried the following mental mutations and each is caught:
  swapped pad/precision (`%-9s`), `nnn` unclamped, tag matched inside `{`-text, holder
  rate-limit keyed on msg_id only, `>` instead of `>=` in the wrap test, notice armed on
  refresh, notice sent when gate refuses, episode counter per tick, NULL `notify`
  dereference, `notified` counter missed.
- Weak spot: `test_notify_receives_entry_fields` and `test_format_line_shows_sto` would still pass
  if the notice were sent for an entry that has since gone FREE (the F4-style `gen`/FREE guard at
  `msgstore.cpp:600-601` is untested: `fakeNotify` has no re-entrancy hook, as the fixture
  comment admits). One test with a `g_notify_reentrant_purge` flag would close it.
- `setUp()` defaulting notice **off**: it does hide one production interaction, on purpose and
  documented at `test_main.cpp:165-170` — with the default on, every stage 3 ladder test's first
  due delivery is pushed behind a notice plus the 30 s node gap. That is the plan's intended
  cost (§5 "competes with deliveries on purpose"), and `test_loop_sends_notice_before_ladder_step`
  asserts the shift once. Missing: an assertion that the ladder step is _refused_ at
  `GAP - 1 ms` after the notice (the test jumps straight to `GAP`). Low.
- No test covers the replace path's notice (F7) or the two-pending-notices ordering (one per
  tick, second on the next tick after the gap). Low.

## Bench-only items (map to T-4.1..T-4.6)

| ID    | Maps to                         | What only the bench can show                                                                                                                                        |
| ----- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-4.1 | plan T-4.1                      | LoRa `:sto` on Heltec/RAK sender: `[HELD] by` line, 0x41 status 0x04 with holder in the app, ladder continues, ack flips                                            |
| T-4.2 | plan T-4.2                      | Upstream 4.35t sender: one short DM shown, no `:ack` on air, no `0x41`, no crash — the trace above predicts it                                                      |
| T-4.3 | plan T-4.3                      | Re-flood with fresh id refreshes only (`refr=` up, `sto=` unchanged)                                                                                                |
| T-4.4 | plan T-4.4                      | Held give-up: no 0x03 frame, `giveuph=` up (and `giveup=` too until F2)                                                                                             |
| T-4.5 | plan T-4.5                      | Two holders: two 0x04 frames, different calls, `act=` counts both under 20/h                                                                                        |
| T-4.6 | plan T-4.6                      | Notice relayed at `max_hop_text` (read hop byte on a sniffer), sender two hops away marks held                                                                      |
| T-4.7 | **new, F1**                     | Gateway sender (Heltec V3 with `--gateway on`, RAK Ethernet) receiving the notice only via the server: after the fix, held mark and no text; before, text displayed |
| T-4.8 | commit claim, unverifiable here | "native 410/410 (13 envs)" and the four board builds — no board build was run for this verdict; string scan of the existing T-Beam image only                       |
