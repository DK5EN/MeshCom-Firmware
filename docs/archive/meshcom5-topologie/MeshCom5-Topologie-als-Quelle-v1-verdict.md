# MeshCom 5 Topology Concept — Fable Verdict

Reviewed: `~/Desktop/MeshCom5-Topologie-als-Quelle.html` (2026-09-24). Code base: feature-neighbour-matrix at 082c2412 (src identical to d9fc5c5a); upstream/dev at 472304e8. Field data: rpizero `~/meshlog/dk5en-98/2026-09-23.log` and `2026-09-24.log`.

Method: nine independent finders, one angle each (factual code claims, auto-via, NCNT, memory layout, concurrency, phone app/JSON, path/horizon, acceptance criteria, deductive consistency). Six verifiers then tried to refute every load-bearing candidate against code, git history, compiled test structs, ArduinoJson 7.4.3 and the field log. Only what survived is listed below. Evidence files: `scratchpad/fable/finder-*.md`, `verify-*.md`.

## Overall

Stages 1 to 3 hold in direction. The topology can replace MHeard and the path table, the RAM arithmetic is right (every "today" figure reproduces from nm), and the 40 B vs 36 B row correction is confirmed by compiling. What they need is specification: horizon rules, timestamps, task placement of the sweep, nRF52 locking and acceptance criteria that can fail.

Stage 4 (auto-via) does not hold as written:

- The worked example contradicts its own rule.
- The echo control would trip most of the time.
- Coverage silently assumes symmetric links.
- The premise "old nodes obey the via" is false for pre-June 4.35 builds, and the via leaks through the server.

A via on text frames would hide or block texts for users of those nodes and in other regions. Section 4.8 needs a rewrite. The minimum change is to restrict auto-via to position frames until the share of pre-June nodes is measured.

## Finding 1: pre-June 4.35 builds do not obey the via; the feeder in Abb. 6 is such a node

- **File:** paper 4.8 premise "gilt für 4.35 wie für den Fork", Abb. 6. Code: `git show v4.35p.06.08:src/aprs_functions.cpp` 296-331, `lora_functions.cpp` 703/1027/709; fix commits 2c96f11b (2026-06-09), a8dc23a2 (2026-06-13).
- **Severity:** high
- **Failure scenario:** Builds before 2c96f11b (4.35a to 4.35o, 4.35p up to v4.35p.06.08) take the first via token as the destination.
  - If the node named first is such a build, it never relays and receives a `*` text as a DM to itself.
  - Every other such node relays whether it is named or not, and keeps the via.
  - None of them displays or forwards to the app a `*` or group text that carries a via. It then discards the later clean copy as a duplicate.

  DL2UD-1 is such a node: behavioural proof, since it relayed `DB2GS-1,DK5EN-98>F1ZKT-12,*` unnamed at H04→H03. DL2JA-3 (4.35d) and DG3MNF-4 (4.35k) sit two hops out. The on-air version field cannot separate these builds from 4.35p after June.

- **Fix:**
  - Add a behavioural detector for such nodes: a neighbour that relays via frames not naming it, or keeps a foreign via, is pre-June.
  - Never name such a node first.
  - Keep auto-via off text frames; the position branch ignores the destination, so positions are safe.
  - Add a failure-scenario row.
  - Measure the fleet share with mcmap `fleet_firmware` before any default change. The finder could not run it: the permission classifier refused.

## Finding 2: the echo control uses the wrong base rate and would trip most of the time

- **File:** paper 4.8 "Prämissen der Echo-Kontrolle", Rollout step 6. Data: `scratchpad/v1b/b3.py`.
- **Severity:** high
- **Failure scenario:** Named-set echo rates within 300 s:

  | Set                                                           | Echo rate within 300 s | Two misses in a row   |
  | ------------------------------------------------------------- | ---------------------- | --------------------- |
  | Paper's set {DL2JA-2, DL2UD-1}, on paths that survive the via | 37 %                   | 27 of 64 pairs (42 %) |
  | Rule-correct set {DL2JA-2, DB0ED-99}                          | 37 %                   | 44 %                  |

  The paper's 86 % is the "any echo" rate. It includes DK5EN-1's 44 first-hand echoes, which disappear by design.

  The 300 s window cuts 6 of DL2JA-2's 25 frames. Its relay median from my TX is 122 s, not 109 s, so 300 s is 2.5 times the median, not "fast das Dreifache". SPERRE would fire every few frames, and the "≥ 86 %" acceptance in step 6 is unattainable.

- **Fix:** Base the rule on the named-set rate. Use a statistical window, for example no echo in N consecutive frames with N chosen from p ≈ 0.4. At N = 7 to 9 that is 3.5 to 4.5 h at one position per 30.5 min, and observed runs of 9 to 12 misses still exist. Alternatively, detect a failed via through D60 and HM freshness instead of echoes. Add an echo/SPERRE log line so the quote is measurable.

## Finding 3: Abb. 6 picks the wrong feeder; the rule selects DB0ED-99

- **File:** paper 4.8 Abb. 6, caption, via string `DL2JA-2,DL2UD-1,*`. Fixture: Anhang B of the Kantenpool paper.
- **Severity:** high (worked example contradicts its own rule)
- **Failure scenario:** The rule is "m hört F, meiste Treffer auf Kante (F, m)". For m = DL2JA-2 it gives DB0ED-99 66, DL2UD-1 15 and DK5EN-1 0.
  - The paper's "DL2JA-2 hat ihn 159-mal gehört" is edge 2,4,159, which means DL2UD-1 heard DL2JA-2: the reversed direction.
  - The field log agrees with the rule: DL2JA-2's 13 second-hand copies came via DB0ED-99 seven times and via DL2UD-1 six times.
  - DL2UD-1 is also a pre-June build (Finding 1).
- **Fix:** Feeder DB0ED-99, via string `DL2JA-2,DB0ED-99,*`, silent nodes DL2UD-1 (which relays anyway as a pre-June build) and DK5EN-1. Redraw Abb. 6.

## Finding 4: coverage uses the wrong edge direction, a silent symmetry assumption

- **File:** paper 4.8 pseudo-code (N2, Pflicht, "offen" built from `hears(m)`).
- **Severity:** high
- **Failure scenario:** `hears(m)` holds the stations m heard. Delivery needs the opposite, x hears m. In the fixture only 4 of 15 N2 stations show any evidence of hearing a K member, and only 1 of DL2JA-2's 8 exclusive stations does. The via set can name a node that the "covered" stations cannot hear, which contradicts "Auto-Via nutzt nur Beweise" and the paper's own reason for rejecting plain OLSR.
- **Fix:** Build coverage from `heardBy` evidence where it exists. Otherwise state the symmetry assumption explicitly, gated like SYM (for example m heard x at ≥ −16 dB).

## Finding 5: the via leaks through the server into other regions

- **File:** paper 4.8 (no mention); code `udp_frame_esp32.cpp:252`, `udp_frame_nrf52.cpp:197` (fork); upstream `udp_functions.cpp:381`, `nrf52/nrf_eth.cpp:497`; RF-heard texts uploaded raw at `lora_functions.cpp:1754` before the rewrite at :1825.
- **Severity:** high for text frames
- **Failure scenario:**
  1. A text carrying DK5EN-98's local via reaches the server unchanged.
  2. Every 4.35 gateway then injects it with the foreign via intact.
  3. There, nodes from June onward do not relay it (not named) and pre-June nodes do not display it. The text reaches only the injecting gateway's direct neighbours.

  DK5EN-98 itself radiated `DB2GS-1,DK5EN-98>F5JFA-12,*`. The server carried only texts in 18 days of logs, which is exactly the frame type at risk. DB0ED-99 relayed 75 % of DK5EN-98's injections but 0 of the 2 via frames; that matches the code, but n = 2.

- **Fix:** On v5, reset the destination path to the destination call before upload and before `checkVia` at injection. 4.35 gateways keep passing the via through, which is a further reason to keep auto-via off text frames. Add the injection callers to the paper's `checkVia` caller list (also `test_inject.cpp:182`).

## Finding 6: nRF52 concurrency, task switches happen at every higher-priority wake-up

- **File:** paper 3 and 4.x (no locking model). Code: FreeRTOS `cores/nRF5/freertos/Source/tasks.c:150-165` (`taskSELECT_HIGHEST_PRIORITY_TASK` rotates through the equal-priority list), `FreeRTOSConfig.h` preemption 1 / time slicing 0; LORA task and loop task both prio 1.
- **Severity:** medium-high
- **Failure scenario:**
  - Both tasks touch the matrix today: the LORA task through `nbrNoteFrame` and relay need, the loop task through the web page, `--neighbours`, reports and `nbrReset`.
  - They swap whenever usbd (prio 3), a BLE task (prio 3) or the timer task (prio 2) wakes and blocks, not only at yields.
  - The paper adds a loop-side writer, echo-table inserts from own frames through `checkVia`, next to LORA-side echo matching. A 128-bit mask or a 12-byte slot can therefore be read half-written, and evictions can interleave with a web iteration.
  - The shadow and via runs use only Heltec V3 (S3) nodes, where RX and web share one loop, so no test would see it.
- **Fix:**
  - Specify a single-writer design, or bracket every multi-step mutation and multi-word read with `vTaskSuspendAll()`/`xTaskResumeAll()`; precedent N-16 at `lora_functions.cpp:2278-2284`.
  - Add a RAK4631 to the shadow run with a DIFF check.
  - `docs/architecture/09-concurrency-map.md:63,101-102` states the "only at yield" model and is wrong the same way.

## Finding 7: the minute sweep has no task, and today's sweep is RX-driven every 1024 min

- **File:** paper lines on "Minuten-Sweep" (4.3 slot freeing, 4.5 D60, 4.8 via set). Code: `nbr_matrix.cpp:506-510, 701, 276`.
- **Severity:** medium
- **Failure scenario:** If the sweep follows today's pattern, it runs only when a frame arrives. On a quiet channel D60, NCNT, slot freeing and the via set then freeze, whereas today's NCNT decays on read from `millis()`. If it runs in the loop, it is a second writer on nRF52 (Finding 6).
- **Fix:** Name the task and cadence. Keep NCNT and D60 computed on read from edge minutes so a stalled sweep cannot freeze them.

## Finding 8: MH DATE/TIME/AGE and the live push are keyed to the wrong minute

- **File:** paper 4.3 ("Minute im Kern"), 4.6 (AGE = last_min, push "wenn sich last_min der Zeile ändert"). Code: `nbr_matrix.cpp:666-667` (window edges bump both rows), `:681` (ME step after), `:716` (`nbrNotePos` bumps relayed-position sources).
- **Severity:** medium
- **Failure scenario:**
  - A row's `last_min` is also bumped when the station is merely named in someone else's path, and for relayed positions. The app would show "heard 08:27" for a neighbour last heard directly hours ago; today's MHeard stamps direct receptions only.
  - For relayed position and HEY frames, the ME step never sees a `last_min` change, so the live push fires only on text and 1-token frames.
- **Fix:** Use the minute of edge (x, me) for DATE/TIME/AGE and as the push trigger, as the paper already does for D60 and slot freeing. Store a sub-minute second for that edge, not for the row.

## Finding 9: horizon entries duplicate rows, include own-relay echoes, and the sizing undercounts

- **File:** paper 4.4 rules and sizing premise. Data: `scratchpad/fable/v3_hz12.py`.
- **Severity:** medium
- **Failure scenario:** The rule creates an entry from any position or HEY with ≥ 3 tokens and never checks for an existing row.
  - At DK5EN-98, 68 to 73 senders per 12 h would get entries. 26 to 28 of them are direct or 2-hop stations, which then appear twice in the path view as "3 Hops über X".
  - Far-only senders number 42 to 45 per 12 h, so the 40 classic slots are already short.
  - 987 of 3,832 horizon-creating frames (26 %) are echoes of my own relays. There the entry token is my own row, and the derived route loops back through me.
- **Fix:** Create or refresh an entry only when S has no fresh row and the entry token is not the own call; free the entry when S gets a row. Re-derive the slot count from 12-h far-only counts; the 2.5-h path-table sample is too short.

## Finding 10: one hop count per horizon entry is shared by all entry rows and never rises

- **File:** paper 4.4 ("Hop-Zahl bis zur Eintrittszeile A aus hzEntry(S)").
- **Severity:** medium-low (display)
- **Failure scenario:** 28 of 49 far senders arrive through different entry rows at different hop counts. DL2RN-13, for example, comes in at 3 via DB0HOB-12 and at 4 via DB0ED-99, and the path view would print 3 for both. `last_min` moves with every sighting, so the minimum cannot age out while S is heard. There were no stale cases in 32.5 h, but the construction allows them.
- **Fix:** Store the hop count per entry bit, which costs a few bits per entry, or print the minimum as a label on the entry and not per route.

## Finding 11: rollout acceptance criteria that cannot pass or cannot fail

- **File:** paper section 6.
- **Severity:** medium
- **Failure scenario:**
  - **Step 1, replay "byte-gleich zum dichten Build":** impossible. Capacity changes in the same step (row indices and EVICT lines differ). NEED/CANCEL/REFUSE print masks as 32-bit `%08X` (`lora_functions.cpp:874, 922, 940, 1926`), so wider masks change the bytes even at equal capacity. No harness feeds a capture into `nbrNoteFrame`.
  - **Step 1, test list:** `test_txring` assigns and asserts `ringNeed`/`ringAlone` as `uint32_t` (`test_txring.cpp:474-538, 1190, 1216`) and will not compile with `NbrMask`. The rewrite is not a listed deliverable.
  - **Step 4, resource_watch ±64 B:** the tool reads whole-image `RAM:` lines, not nm symbols, and step 4 also moves other statics. The gate will fail for non-defects.
  - **Step 4, string scan:** vacuous on 30 of 32 envs (literals already under `BOARD_T_DECK`); meaningful on the two T-Deck images only.
  - **Step 2 and 3, JSON "feldgleich":** the two DIST paths already disagree today (`mheard_record.h:60-72`), the new path quantises, and DATE differs per Finding 8.
  - **Step 5, NCNT "plausibel":** no number, and after step 4 there is no shadow value left.
  - **Step 6, echo quote:** no tool or log line computes it.
- **Fix:**
  - Replay at equal capacity with masks compared in their low 32 bits, then a separate capacity step judged on decisions, not bytes.
  - Write a replay harness.
  - List the `test_txring` rewrite as a deliverable.
  - Gate RAM on nm symbol sums.
  - Scope the string scan to T-Deck.
  - Give numeric tolerances.
  - Add echo and SPERRE log lines.

## Finding 12: MH frame lengths are 5 B low, and phone MTU is unverified

- **File:** paper 4.6, Mengengerüst. Code: `lora_functions.cpp:1166` (live DIST unrounded double), `loop_functions.cpp:674-676, 716-719`, `phone_commands.cpp:69,104`, `nrf52_ble.cpp:97`, `ble_json_frame.h`.
- **Severity:** medium
- **Failure scenario:** ArduinoJson 7.4.3 prints the live DIST as an 11-character double, so the realistic worst case is 167 / 230 B, not 162 / 225. Encoding maxima reach 237 B, which still fits.

  | Path                          | Effective limit | What happens above it                                                                    |
  | ----------------------------- | --------------- | ---------------------------------------------------------------------------------------- |
  | List (`addBLEComToOutBuffer`) | 245 B           | Bytes are truncated into invalid JSON, which the app drops (`MessageHandler.ts:795-799`) |
  | Live, ESP32                   | 252 B           | `uint8_t` wrap and MTU−3                                                                 |
  | Live, nRF52                   | 247 B           | Notify split at MTU−3                                                                    |

  Both call sites use plain `bleJsonFrame()`, not FailSoft. Every limit assumes the phone negotiates the node's maximum MTU. A phone at MTU 185 would lose every frame over 182 B, which includes the 20-field frame but not today's 13-field frame.

- **Fix:** Round DIST to 0.1 km before the JSON. Require `bleJsonFrameFailSoft(..., 244)` at both call sites. Verify the negotiated MTU on iOS and Android before adding fields.

## Finding 13: ME step claim overstated; HN reports would enter MHeard

- **File:** paper 4.3. Code: `nbr_matrix.cpp:519-535, 672-682`; `lora_functions.cpp:773-795`.
- **Severity:** low-medium
- **Failure scenario:**
  - The ME step "läuft heute schon bei jedem Rahmen" is false for DROP TOK and DROP LOOP frames. There were 0 and 2 of them in 32.5 h. The planned decoupling is therefore a code change, and no rollout step names it.
  - HN reports reach the ME step but not today's MHeard, so the derived MHeard would list neighbours heard only through a report.
- **Fix:** Name the decoupling as a deliverable. Exclude HN reports from the ME step's MHeard effect, or accept and document the difference.

## Finding 14: nRF52 porting risks at 128 rows (web page, stack)

- **File:** paper 4.2 and 4.7 (stack only budgeted for the MH view). Code: `web_functions.cpp:1613-1937` (nine `uint8_t[NBR_MAX_ROWS]` locals, n² cross table), `nbr_matrix.cpp:1492-1494` (`nbrFormatRow` 1,408 B at 128 rows), `nrf52_main.cpp:2490-2493`.
- **Severity:** medium
- **Failure scenario:**
  - At 128 rows the neighbours cross table is 16,384 cells, at least 147 kB of HTML.
  - On nRF52 the radio re-arm is deferred for the whole render.
  - `--neighbours` reaches about 3.6 to 3.9 KB of the 4 KB loop stack.
- **Fix:** Paginate or drop the cross table above 64 rows, and move row-sized buffers to static or streamed output. Add a stack budget line for the nRF52 loop.

## Finding 15: NCNT details (veto, flapping, age)

- **File:** paper 4.5. Code: `nbr_matrix.cpp:1184-1219` (`nbrHearsSym` veto), `esp32_main.cpp:3480-3491`, `nrf52_main.cpp:2018-2025`.
- **Severity:** low to low-medium
- **Failure scenario:**
  - **Veto omitted:** the formula drops the HN-report veto that `nbrHearsSym` applies. The paper accepts the overcount, and 0 reports were received in the capture.
  - **No hysteresis:** SYM uses the last frame's SNR, so a neighbour without HM evidence sitting at −16 dB flips the count. That costs 0.7 to 3.4 trickle resets per hour at 2 to 3 extra HEYs each; replay of the capture gives 0.
  - **No NCNT age:** the new row has no timestamp for NCNT, so the NCNT paper's B3 age display is lost.
  - **Fifth reader:** the dead B6 trigger reads `getMheardCount()` and is missing from the paper's reader list.
- **Fix:**
  - Add the veto term, or state why negative evidence is ignored.
  - Use hysteresis or an EMA for SYM.
  - Keep a minute for `ncnt`, or state that the age display is dropped.
  - Add B6 to the reader list.

## Finding 16: numbers and citations

- **Severity:** low
- **Items:**
  - "128 rows everywhere = +6.2 kB" should be +7.9 kB with S3 sizes, as the sentence says (a literal in `.body.html:317`, not computed).
  - "55 B" per MHeard entry is correct, but the field list omits `mheard_send_idx` (1 B per entry).
  - The citation `lora_functions.cpp:1076` should be :1078.
  - The `checkMesh` caller at :1777 is uncited.
  - `hzMeta` has no type; a naive struct is 4 B, while `uint8_t[3]` is 3 B.
  - DL2JA-2's relay timing: 122 s median from my TX, or 109 s median / 206 s p90 from the first heard copy.
  - The server-injected position/HEY residual risk (today's 2-hop window, `nbr_matrix.h:229-236`) is not restated for the horizon. Low, with 0 cases in 749 server frames.
  - The hop budget: with `max_hop_pos` 2, frames through the feeder reach the super node at H00 and stop at N2. That is the same limit as in the flood, but "jede Station bleibt gedeckt" holds only for N2.
  - A v5 feeder applies its own via set and may not name the super node; first-copy dedup then locks the super node out. This is a design gap, not active today.
  - Detection takes about 35 to 65 min (own position every 30.5 min), not 600 s. One named echo masks a silent member.

## Refuted claims (do not re-investigate)

- **"Nur zweite Hand" does not prove DL2JA-2 missed the direct copy:** refuted. DL2JA-2 runs 4.35t, which records the msg_id before the relay decision (`lora_functions.cpp:962` before :1429). Duplicates never reach the relay block (:897). Relay slots carry `RING_STATUS_DONE` and are excluded from overhear-cancel (:596). The frame is fixed at enqueue. The paper's lower bound "mindestens 13 von 65" stands.
- **The field measurement is wrong:** refuted. It was reproduced exactly by two independent scripts: 65 own positions, 56/9, 44/0, 25/1, 18/1, 12/13.
- **The OLSR analogy fails because of a missing duplicate-set rule:** refuted. Under RFC 3626 §3.4.1 step 2, a single-interface node also acts on the first copy only.
- **No clock gate on the MH send path:** refuted. `mheard_functions.cpp:303` returns before any MH JSON is built.
- **The NCNT worked example is unsourced or wrong:** refuted. The capture reproduces it; DL2UD-1 is −13 instead of −12 dB, a nit.
- **CALL up to 20 characters breaks the new frame:** refuted. The proposed CALL comes from the 6-bit word, 3 to 9 characters.
- **SNR −128 and PL 255 push the frame to 235-245 B:** refuted. RadioLib SNR is −32 to +31.75, PL is 4 bits, and the realistic maximum is 230 B.
- **The live path has 10 B more headroom than the list path:** refuted. Its effective limit is 252 B on ESP32 and 247 B on nRF52.
- **The MHeard entry is 54 B, not 55 B:** refuted. The 55th byte is `mheard_send_idx`.
- **Citation `nbr_matrix.cpp:1109` is wrong:** refuted. 1109 is the `return 0` line for idx ≥ 32, exactly the claim.
- **4.3 contradicts the DROP TOK row in section 5:** refuted. 4.3 describes target behaviour; section 5 is about a station whose own call breaks the character rule.
- **The topology introduces row-index reuse (ABA):** refuted as new. The same risk exists today in `ringNeed` bits and the MHeard BLE cursor.
- **Stage 3 turns the 49.7-day millis wrap into a MHeard regression on all platforms:** only ESP32. nRF52 wraps at 48.5 d, and its MHeard is already broken there today, a pre-existing bug outside this paper.
- **resource_watch fails because of build noise:** refuted. The real confound is other statics that change in step 4.
- **The step-6 mcmap baseline is confounded by several changes:** refuted. The 24 h before step 6 are stage 3 on the same nodes.
- **Finder E's "a two-word store cannot be seen half-written unless the writer yields":** refuted (Finding 6).

## Confirmed as stated (for the record)

- **RAM:** every "today" RAM figure reproduces from nm across the four envs (5,829 / 9,463 / 14,663 / 16,415 B).
- **Struct sizes:** the 40 B AoS row, the 12 B core and the 6 B edge hold (compiled on xtensa and arm).
- **Bit and callsign widths:** the 96-bit slot widths fit the value ranges, and the 6-bit callsign alphabet matches `nbrValidToken()`.
- **Fixture figures:** Abb. 4 edge numbers 358, 53, 750 and 610 hold. DL2JA-2 is the sole hearer of 8 and covers all 15; DB0ED-99 hears 7.
- **Factual code claims:** about 30 were checked and confirmed, among them the MHeard key, the windows, the caps, `/N` 99, the relay path reset and own via, the upstream substring match, auto-via dead since 22.07 in both trees, `shortVERSION()`, and the T-Deck 30 s throttle.
- **App:** it ignores unknown fields, deduplicates by callsign and takes "last heard" from DATE/TIME, so the per-minute throttle is safe.
