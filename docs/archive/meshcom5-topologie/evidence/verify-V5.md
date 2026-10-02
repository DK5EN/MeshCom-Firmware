# Verifier V5: finder E (concurrency) and finder H (acceptance criteria)

Tree: `feature-neighbour-matrix` @ 082c2412 (the paper cites d9fc5c5a; the nbr code did not change
between the two). Read-only. I ran no builds and no tests.

## Verdict table

| ID   | Verdict                                          | One-line reason                                                                                                                                                                                             |
| ---- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| E-1  | CONFIRMED                                        | The paper never names the task that runs the minute sweep. Today's only sweep is lazy and RX-driven, and it runs every 1024 min, not every minute.                                                          |
| E-2  | CONFIRMED, and stronger than stated              | nRF52 LORA and loop tasks both touch the matrix today. They also switch at every higher-priority task wake-up, not only at yields, so "no logging inside a mutation" is not enough.                         |
| E-3  | PARTLY                                           | The ABA risk is real, but it already exists today (`ringNeed` bits, MHeard BLE cursor). The topology does not introduce it.                                                                                 |
| E-4  | PARTLY (mechanism confirmed, "never" overstated) | The trigger is dead for relayed `!`/`@` frames if it is evaluated at the ME step. It still fires for text and for 1-token frames. The root cause is wider: MH DATE/TIME/AGE also read the row's `last_min`. |
| E-5  | CONFIRMED (timing is an estimate)                | The cross table has n² cells with one socket send each. On nRF52 radio re-arm is deferred for the whole render. The paper says nothing about the page layout.                                               |
| E-6  | CONFIRMED                                        | DK5EN-98 and DK5EN-1 are both Heltec V3 (S3). nRF52 first appears in step 5, after the switch-over, with no DIFF check.                                                                                     |
| E-7  | CONFIRMED (porting risk)                         | The row-sized locals are there and the arithmetic holds. The paper budgets stack only for the MH view.                                                                                                      |
| E-8  | PARTLY                                           | The wrap wipes the matrix. But the nRF52 millis wraps at 48.5 d, not 49.7 d, and MHeard is **not** wrap-safe on nRF52 today, so the "regression" holds on ESP32 only.                                       |
| E-9  | PARTLY                                           | (a), (c) and (d) confirmed; (d) also shows the paper's checkVia caller list is incomplete. (b) p90 206 s is correct, but "noticeable share late" is speculation. (e) depends on the implementation.         |
| E-10 | CONFIRMED (Low)                                  | Minutes against ms at the window edges. Quantisation is missing from the step-3 list of explained causes.                                                                                                   |
| H1   | CONFIRMED, and stronger than stated              | The capacity changes in the same step. On top of that, NEED/CANCEL/REFUSE print their masks as `%08X`, so wider masks change the bytes even at equal capacity. No replay harness exists.                    |
| H2   | PARTLY                                           | Right that the scan is vacuous on 30 of 32 envs. Wrong that it "cannot fail" on T-Deck: there it is a real check, and it would even fail a sensible migration step.                                         |
| H3   | CONFIRMED (one argument wrong)                   | `resource_watch.py` reads only the whole-image `RAM:`/`Flash:` lines. The confound is the other statics that step 4 changes, not build noise.                                                               |
| H4   | CONFIRMED                                        | `test_txring` assigns and asserts `ringNeed`/`ringAlone` as `uint32_t`. With `NbrMask` it no longer compiles.                                                                                               |
| H5   | CONFIRMED                                        | The two DIST paths already disagree today, and the new path quantises. DATE/TIME would also differ (see E-4).                                                                                               |
| H7   | CONFIRMED                                        | No tool computes an echo quote, and the paper defines no echo log line.                                                                                                                                     |
| H8   | PARTLY                                           | The "baseline not stable, several mechanisms change" argument is wrong: the 24 h before step 6 are Stufe 3 on the same nodes. The day-to-day confound and the unnamed metric remain.                        |
| H9   | CONFIRMED, and stronger than stated              | "plausibel" has no number. Also, after step 4 there is no shadow value left to compare against.                                                                                                             |

## Evidence

### E-1: where the sweep runs

- **Paper.** Three places mention the sweep, and none names a task or context:
  - Line 152: "im Minuten-Sweep frei".
  - Line 176: "D60 … // Kopf, vom Minuten-Sweep gepflegt".
  - Line 260: "Die Via-Menge wird einmal je Minute im Sweep neu gerechnet".

  The head keeps `last_sweep` (line 97), the same field today's lazy pattern uses.

- **Today.** The sweep is triggered from the frame feed:
  - `nbr_matrix.cpp:506-510`: "Der Sweep ist Wartung unabhaengig von diesem Frame und laeuft darum VOR jeder Pruefung" → `nbrMaybeSweep(m, now_min);`.
  - `nbr_matrix.cpp:701` in `nbrNotePos()`.
  - Both callers run only from OnRxDone (`lora_functions.cpp:788/793`, `:1001/1010`, `:1035`).
  - The interval is 1024 min (`nbr_matrix.cpp:276`). It is a ghost sweep against wrap-around, not a minute sweep.
- **NCNT today** is computed on read from `millis()` (`mheard_functions.cpp:691-707`), so it decays with no RX. The finder's two-horned argument holds:
  - An RX-driven sweep freezes D60 and NCNT on a quiet channel.
  - A loop-driven sweep is a second writer on nRF52.

### E-2 and E-6: who touches the matrix on nRF52 today

- **LORA task** (prio 1, 16 KB: `SX126x-Arduino/.../board.cpp:44-45`, `:498`): `nbrNoteFrame`, `nbrNotePos`, `nbrNoteReport`, `nbrRelayNeed`, `nbrCoverMask`, plus the `ringNeed` writes. They all log through `nbrLog` → `printfdeb` (`lora_functions.cpp:507-509`, `:518`).
- **Loop task** (prio 1, 4 KB: core `main.cpp:42,88`, `rtos.h:58`):
  - Readers: `sub_page_neighbours` (web), `--neighbours` (`command_functions.cpp:4880-4940`), `nbrLogSnapshot` (`nrf52_main.cpp:2057-2061`), `sendNbrReport` → `nbrBuildReport` (`loop_functions.cpp:5152`).
  - Writer: `nbrReset` (`command_functions.cpp:4950`, a `memset` of the whole matrix at `nbr_matrix.cpp:486-495`).
  - The single-writer comment at `nbr_matrix.cpp:1559-1561` already ignores `nbrReset`, and `:220-224` concedes "unvollstaendigen Reset aus einem anderen Task".
- **Explicit yield points:**
  - `printfdeb` spins with `yield()` while the CDC FIFO is full (`printfdeb_functions.cpp:56-63`, `:131-141`).
  - W5100S `socketSend` always yields at least once per print, then busy-waits for `SEND_OK` with `yield()` (`.pio/libdeps/wiscore_rak4631/RAK13800-W5100S/src/socket.cpp:435-461`).
- **New: the finder's model is incomplete.**
  - `configUSE_TIME_SLICING 0` only stops the rotation on the tick.
  - `taskSELECT_HIGHEST_PRIORITY_TASK()` in the core's FreeRTOS (`freertos/Source/tasks.c:150-165`) always calls `listGET_OWNER_OF_NEXT_ENTRY`, which rotates through the equal-priority ready list.
  - `prvAddTaskToReadyList` (`:235-239`) inserts a newly woken LORA task just before `pxIndex`, so it is the next one picked.

  So every time a higher-priority task wakes and blocks again, the scheduler switches from loop to LORA (or back) at an arbitrary instruction. Those tasks are:
  - `usbd` (TinyUSB, `TASK_PRIO_HIGH` = 3, `Adafruit_TinyUSB_nrf.cpp:87`), which runs on every CDC transfer, i.e. exactly during `--nbrdebug` logging with a host attached.
  - The Bluefruit BLE/SOC tasks (prio 3).
  - The timer service and the callback task (prio 2).

- **Consequences:**
  1. The "fine" check in finder-E §2 ("a two-word store cannot be observed half-written unless the writer yields between the words") is **refuted**.
  2. The proposed rule "no logging inside a mutation" is necessary but not sufficient. Any multi-step mutation reachable from both tasks needs a `vTaskSuspendAll()`/`xTaskResumeAll()` bracket (precedent N-16 at `lora_functions.cpp:2278-2284`), or a strict single-writer design.
  3. `docs/architecture/09-concurrency-map.md:63` and `:101-102` ("runs only when the loop blocks or yields") are wrong in the same way.
- **The paper adds loop-side writers anyway.** Echo-table insertion for own frames starts in the loop (`sendMessage` → `checkVia`, `loop_functions.cpp:3446` and others), and echo matching happens in the LORA task. So a two-task writer exists even if the sweep is placed in RX.
- **E-6.** DK5EN-98 and DK5EN-1 are both Heltec V3 (memory: webflash default, and "DK5EN-93 is configured as callsign DK5EN-1"). ESP32 RX runs in loopTask (`esp32_main.cpp:2520` → `checkRX` → `OnRxDone` at `:4072`), and the web server runs in the same loop (`:3860`). The only nRF52 appearance is step 5 (paper line 294), and its acceptance has no DIFF or consistency item.

### E-3

- `ringNeed`/`ringAlone` are `uint32_t` bitmasks over row indices (`txring_functions.cpp:60-61`), written at enqueue (`:696-697`) and consumed by overhear-cancel (`lora_functions.cpp:855-900`). That is index-over-time state **today**, with 21 rows. A larger table evicts less, so the risk goes down, not up.
- The MHeard BLE cursor already has the same slot-reuse ABA. Its guard catches cleared slots only, not reused ones (`mheard_functions.cpp:873-883`).
- The spec gap is valid: the paper only clears horizon masks on eviction (line 172). "Introduced by the topology" is wrong. Severity: Low.

### E-4: the live-push trigger

- **Paper wording** (line 205): "Live-Rahmen höchstens einmal je Nachbar und Minute, nämlich wenn sich last_min der Zeile ändert." Line 135 ties MHeard to the ME step.
- **Ordering in `nbrNoteFrame`:**
  - The in-window edge loop sets `m.rows[x].last_min` and `m.rows[y].last_min = now_min` (`nbr_matrix.cpp:666-667`).
  - Only after that does the ME step set `m.rows[last].last_min` (`:681`).
  - For `!`/`@` with ntok ≥ 2, the pair (ntok-2, ntok-1) is always in-window (`:577`), and y = last hop ≠ 0.

  So a check at the ME step never sees a change for relayed POS/HEY frames.

- **Where it still fires:** text frames (ME only, `:552-570`) and 1-token direct frames (no edge loop). "Never fires" is therefore overstated. Neighbours that mostly relay get pushed only on their own originations.
- **Wider root cause** (not in the finder): the row's `last_min` is also bumped
  - when the neighbour is merely named as the second-to-last token in someone else's frame (`:666`), and
  - by `nbrNotePos` for relayed positions of that source (`:716`, called at `lora_functions.cpp:1035`).

  The paper derives MH DATE/TIME ("Minute im Kern", line 138) and AGE ("last_min", line 194) from that field. MHeard today stamps only direct receptions (`msg_source_last`). The correct clock is the edge (x, me) minute, which the paper already uses for slot freeing and for D60.

### E-5

- The cross table prints one cell per (X, Y) (`web_functions.cpp:1822-1842`), each at least `<td></td>` (9 B):
  - 21 rows: 441 cells.
  - 48 rows: 2,304 cells.
  - 128 rows: 16,384 cells, at least 147 kB.
- **nRF52:** `bSPI_ETH_Active` is held for the whole `loopWebserver()` (`nrf52_main.cpp:2490-2493`), and OnRxDone defers the re-arm while it is held (`lora_functions.cpp:597-601`).
- **ESP32:** the radio is re-armed only in `checkRX` (`docs/BACKLOG.md:2672-2680`).
- The duration is not measured; the finder correctly labels it an estimate.

### E-7

`uint8_t[NBR_MAX_ROWS]` locals, 9 in total: `web_functions.cpp:1613`, `:1625`, `:1649-1651`, `:1863`, `:1867`, `:1892`, `:1937`.

- `nbrFormatRow`: `hearer_idx[NBR_MAX_ROWS]` + `hearers[NBR_MAX_ROWS*10]` (`nbr_matrix.cpp:1492-1494`) = 1,408 B at 128 rows.
- It is reached from `commandAction` (`command_functions.cpp:4938`). The measured base is `nrf52loop 792 + commandAction 1120` (`mheard_functions.cpp:946`).
- Note: `printfdeb` (≈900 B of locals) runs **after** `nbrFormatRow` returns, so the two frames are not nested. The finder's 3.6-3.9 KB estimate stands, and it has little margin.

### E-8

- ESP32: `millis()` = `esp_timer_get_time()/1000` as `unsigned long` (`esp32-hal-misc.c:171-174`). It wraps at 2^32 ms, and `now_min` jumps from 6046 to 0. Confirmed.
- nRF52: `millis()` = `tick2ms(xTaskGetTickCount())` (`delay.c:28-31`, `rtos.h:65`) at 1024 Hz. It wraps after 48.5 d, from 4,194,303,999 ms to 0. `now_min` jumps from 4369 to 0.
- Across that wrap, every nRF52 `uint32_t` millis difference is off by +100,663,296 ms (about 28 h). MHeard's 12 h and 1 h windows (`mheard_functions.cpp:702`, `:882`) therefore also empty on the RAK **today**.
- So "Stage 3 turns a latent matrix-only bug into a MHeard regression" is true for ESP32 only.
- The DATE comment is correct: `Bootepoche + last_min` breaks after 65536 min (45.5 d) unless it is rebased or computed as now − age.

### E-9 and E-10

- (b) `docs/nbr-wichtigkeit-konzept.md:137`: DL2JA-2 has a median of 109.0 s and a p90 of 206.3 s, measured from the first heard copy. The paper cites only the median (line 264).
- (d) The paper's caller list (line 10) omits `test_inject.cpp:182`, `udp_frame_esp32.cpp:252`, `:349` and `udp_frame_nrf52.cpp:197`, `:353`. These are gateway server→LoRa frames, so under Auto-Via a gateway would put its via set on server-injected frames. The paper does not address this.
- E-10: MHeard ages in ms (`MHEARD_*_WINDOW_MS`), `nbrFresh` in minutes. Quantisation is missing from the explanation list in step 3 (line 292).

### H1: replay byte-identical

- Step 1 content: "NBR_MAX_ROWS 64 und 128". Step 1 acceptance: "SNAP, ROW, NEED, CANCEL, REFUSE byte-gleich zum dichten Build" (line 290). ROW prints the row index (`nbr_matrix.cpp:1550`), so with more rows there are fewer EVICTs and the indices differ.
- **New:** NEED, CANCEL and REFUSE print their masks as 32-bit `%08X` (`lora_functions.cpp:874`, `:922`, `:940`, `:1926`). A 64- or 128-bit `NbrMask` needs a new format. nRF52 nano printf has no `%llX` (memory note), so this means two or four words. Byte identity then fails even at equal capacity unless the replay compares the low 32 bits on both sides.
- **No harness:**
  - `tools/nbr*.py` only consume logs.
  - `berglog_sim.py` models relay policy, not `nbrNoteFrame`.
  - `test_dedup_replay` and `test_txprio_replay` do not touch the matrix.
  - The native envs pin `NBR_MAX_ROWS=5` and `12` (`platformio.ini:1303-1334`).

### H2: string scan

- Every `mheard.dat`/`mhpath.dat` literal is in `src/mheard_functions.cpp` under `BOARD_T_DECK || BOARD_T_DECK_PLUS` (`:232`/`:249-250`, `:267`/`:284-285`, `:1254`/`:1262-1263`, and the load of `mhpath`). The only other `BOARD_T_DECK` definitions are native test envs (`platformio.ini:604`, `:721`, `:761`). So the scan is vacuous on 30 envs. Confirmed.
- It is not vacuous on T-Deck and T-Deck Plus. There it detects persistence code that was kept or moved.
- It would also flag a sensible cleanup (`SD.remove("/mheard.dat")` of stale files, which paper 4.9 does not mention) as a failure.
- Fix: scope the scan to the two T-Deck images, and decide whether old files are deleted.

### H3: resource_watch

- `tools/resource_watch.py:57-60`, `parse_usage()` at `:95`: it parses only the `RAM:`/`Flash:` summary. `regions` reads linker segments. No mode reads `nm` symbols.
- Paper 4.7 (line 209) measured "Summe der Datensymbole aus nm".
- The finder's noise argument is wrong. The "not reproducible" memory note concerns flash **bytes** under an address shift, not section sizes, and the baseline-drift note concerns different bases.
- The real confound: step 4 also removes and adds statics outside the 4.7 group, for example `mheard_send_idx[MAX_MHEARD]` (`mheard_functions.cpp:802`), the nbr_views cursor and order arrays, and alignment padding. A ±64 B whole-image gate will almost certainly miss for reasons that are not defects.

### H4

- `test/test_txring/test_txring.cpp:474-475`: `ringNeed[3] = 0xAAAAAAAAUL;`
- `:485-538`: `TEST_ASSERT_EQUAL_UINT32(…, ringNeed[slot])`
- `:1190`, `:1216`: `ringAlone[0] = 0;`
- `txring_functions.h:36-37` declares both as `uint32_t`.

With `struct NbrMask` none of this compiles. Step 1 does not list the test rewrite as a deliverable.

### H5

- `mheard_record.h:60-72` says the live and buffered DIST paths disagree today ("widersprechen sich also schon heute").
- The live path assigns `DIST` raw (`mheard_functions.cpp:430`). The buffered path is rounded (`:916` via the record).
- The paper's 0.0001° quantisation (line 144) makes a third value.
- Per E-4, DATE and TIME differ too whenever the row minute came from a path appearance.

### H7

- No tool contains an echo computation: `grep -i echo tools/nbr*.py` finds only a comment in `nbrsnap.py:121`.
- The paper defines no ECHO or SPERRE log line in 4.8 or §5.
- The 86 % was computed by hand (line 229).

### H8

- Step 6 compares against "den 24 h davor" (line 295). Those hours are step 5, i.e. Stufe 3 on the same nodes, so `--via on` is the only firmware change. The finder's "multiple mechanisms change" is refuted.
- Still valid: RF and traffic change from day to day, and no mcmap query or metric is named.

### H9

- Line 294: "NCNT gegen den Schattenwert plausibel". Step 4 removes `getMheardCount()` and the shadow compare. Step 5 therefore has no live reference, only the step-3 history.

## New findings from this pass (not in either finder)

1. **N-1 (Medium-High, nRF52):** equal-priority tasks rotate on every higher-priority wake-up (evidence under E-2).
   - This invalidates "switches only at yield points" in finder-E and in `09-concurrency-map.md`.
   - Any multi-step topology mutation or multi-word mask read shared between the LORA and loop tasks needs a scheduler bracket, not only "no logging".
2. **N-2 (Medium):** MH DATE/TIME/AGE and the live-push trigger are keyed to the row's `last_min`, which path appearances and relayed positions also bump (`nbr_matrix.cpp:666-667`, `:716`). They should use the edge (x, me) minute. This also breaks "feldgleich" for DATE/TIME.
3. **N-3 (Low-Medium):** on nRF52 `millis()` wraps at 48.5 d to a non-2^32 value. Every `uint32_t` millis difference is then about 28 h off, so MHeard and NCNT empty on the RAK today. This is a pre-existing bug and outside the paper, but it changes E-8's regression claim.
4. **N-4 (Medium, Stufe 4):** the paper's checkVia caller list misses the gateway UDP→LoRa paths and test inject. Auto-Via would apply the via set to server-injected frames, and the paper says nothing about echo control for them.
5. **N-5 (strengthens H1):** the 32-bit `%08X` mask fields in NEED/CANCEL/REFUSE make byte identity impossible once masks widen, independent of capacity.
