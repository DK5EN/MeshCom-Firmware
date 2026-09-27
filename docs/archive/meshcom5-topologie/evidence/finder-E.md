# Finder E -- concurrency, tasking, timing

Paper: "MeshCom 5: Topologie als einzige Quelle" (paper.txt). Tree: feature-neighbour-matrix, read-only.

## 0. Which task runs what today (the basis for every finding)

| Context            | Stack / prio                      | What runs there                                                                                                                                                                                                                                        | Evidence                                                                                                              |
| ------------------ | --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| ESP32 loopTask     | 8 KB, prio 1, core 1              | `checkRX()` -> `OnRxDone()` -> `nbrNoteFrame`/`updateMheard`/relay/`checkVia`; web server (polled, synchronous); `sendMheard` list; console; LVGL `lv_task_handler`; T-Deck SD save (called from `updateMheard`/`updateHeyPath`, i.e. inside OnRxDone) | esp32_main.cpp:2520, :3965, :4072, :3922; mheard_functions.cpp:469, :667; docs/architecture/09-concurrency-map.md §2  |
| ESP32 NimBLE host  | core 0                            | only `xQueueSend(bleQueue)` -- never touches MHeard/matrix                                                                                                                                                                                             | 09-concurrency-map.md §2                                                                                              |
| nRF52 loop task    | **4 KB**, prio 1                  | web server (W5100S), `sendMheard`, `sendToPhone`, console/commandAction, own-frame senders + `checkVia`, `nbrLogSnapshot`, HN report                                                                                                                   | framework main.cpp `LOOP_STACK_SZ (256*4)`; nrf52_main.cpp:1865-1888, :2053-2061, :2490-2492; loop_functions.cpp:5149 |
| nRF52 LORA task    | 16 KB, **prio 1**                 | `Radio.BgIrqProcess()` -> `OnRxDone` (whole RX path incl. relay `checkVia`, `nbrRelayNeed`, live MH push)                                                                                                                                              | SX126x board.cpp:474-498 (`TASK_PRIO_NORMAL` redefined to 1); 09-concurrency-map.md §1.2 correction box; memory note  |
| nRF52 prio-2 tasks | Bluefruit cb 3 KB, timer svc 1 KB | only flags / `bleQueue` / staged settings; no topology access                                                                                                                                                                                          | nrf52_ble.cpp:224-276, :350-380                                                                                       |

nRF52 has `configUSE_TIME_SLICING=0`: loop and LORA task (equal prio) switch **only at yield points**. The
yield points that matter: `yield()` after every `loop()`; `printfdeb()` whenever the CDC FIFO is full
(printfdeb_functions.cpp:56-63, :133-141 -- up to 20 ms of `yield()` per line); **every** web
`print`/`printf` (W5100S `socketSend()` spins with `yield()` until SEND_OK, RAK13800-W5100S socket.cpp:432-461);
`delay()`; blocking semaphore takes.

Today's matrix has one writer (OnRxDone, comment nbr_matrix.cpp:1559-1562), a lazy sweep **inside the RX path**
(`nbrMaybeSweep()` called first thing in `nbrNoteFrame()`, nbr_matrix.cpp:509-510, :274-302), readers in the loop,
and no lock anywhere. `nbrReset()` from the console is the one foreign writer, and nbr_matrix.cpp:220-224 already
concedes "einen unvollstaendigen Reset aus einem anderen Task". The paper keeps the lock-free model implicitly
but adds loop-side writers and index-linked side structures.

## 1. Findings

### E-1 The minute sweep has no execution context -- both obvious choices break something (Medium-High)

- **Paper:** 4.3 Regeln (slot freed "im Minuten-Sweep"), 4.5 (`D60 ... vom Minuten-Sweep gepflegt`), 4.8 Regeln
  (via set "einmal je Minute im Sweep", echo table folded after 300 s, two misses -> 60 min flood), Stufe 2
  (`[TOPO]|DIFF je Minute`). The paper never says which task runs the sweep.
- **Today:** the only sweep is RX-driven (nbr_matrix.cpp:509-510). NCNT today is computed **on read** from
  millis (`getMheardCount()`, mheard_functions.cpp:691-707), so it decays without any RX.
- **If the new sweep copies today's pattern (RX-driven):** on a quiet channel D60 is never recomputed. NCNT
  (`popcount(D60 & (HM|SYM))`, sent in own HEY/position from the loop) freezes at its last value instead of
  falling to 0 after an hour. That is a regression against `getMheardCount()`. The echo fold, the "two misses"
  test and the end of the 60-min SPERRE are evaluated only when some foreign frame arrives. Scenario: the super
  node dies at night; my positions name only it, so nobody repeats them; the fallback waits for the next
  received frame, in practice my own flooded HEY echo, i.e. up to one HEY interval (15 min) per evaluation, x2
  misses.
- **If the sweep runs in the loop:** on nRF52 it becomes a second writer that interleaves with the LORA task at
  every yield inside it (see E-2). ESP32 is unaffected (same task).
- **Needed in the paper:** name the context. Loop-driven, with the nRF52 mutation bracketed
  (`vTaskSuspendAll()`/`xTaskResumeAll()`, precedent lora_functions.cpp:2274-2283 N-16) and no logging inside
  the bracket.

### E-2 nRF52: multi-step mutations with log lines inside them expose half-applied state (Medium)

- **Paper:** 4.1-4.4. A row eviction now cascades: row core + call word, the bit in all `nbrHears`/`nbrHeardBy`
  masks, its edges in the pool, its ext slot, the bit in every horizon entry mask ("löscht der Durchlauf ihr Bit
  auch in allen Eintrittsmasken"), plus D60/via/echo masks and ringNeed/ringAlone. Each step has a named log line
  (`[NBR]|EVICT`, `EVICT-X`, `EVICT-H`, EVICT-E from the edge-pool paper). The ME step allocates slots (RX
  context), and the sweep frees them (E-1, likely the loop).
- **Code:** `nbrLog` = `printfdeb` (lora_functions.cpp:507-509, :518). `printfdeb` yields on a full CDC FIFO
  (above). Today's `nbrCommitRow()` already logs in the middle of its sequence (nbr_matrix.cpp:237-246, log,
  then zero, then init), and `nbrNoteFrame()` logs between edge hits (:670) and before the ME step.
- **Failure scenario (the field-test configuration: `--nbrdebug on`, meshlogger/USB host attached, so the FIFO
  fills):** the loop-side sweep marks slot s of silent neighbour x as free and emits its log line. The FIFO is
  full, so it yields, and the LORA task runs OnRxDone for a new direct neighbour y. The ME step takes the first
  free slot, which is s, while `x.ext` still points to s because the sweep has not cleared it yet. Now x and y
  share slot s. MH list and live push show y's RSSI, position and altitude under x's call. The f/s echo counters
  of both neighbours merge, so `schwach(m)` and the choice of Zubringer in 4.8 work on the wrong evidence. The
  mirror case: the RX-side eviction cascade yields mid-way, and the loop (web page, BLE list, own-frame
  `checkVia`) reads a row that has its new call word but still carries the old occupant's mask bits.
- **Needed:** a rule: "no `nbrLog`/`printfdeb` inside a mutation. Collect log lines, emit them after commit.
  Mutations from the loop are bracketed on nRF52." The Arduino-free `nbr_matrix`/`nbr_views` need a lock hook
  like the existing `nbrLog` function pointer, and it must be a no-op on ESP32/native.

### E-3 Index-keyed derived state outlives row eviction (ABA on row indices) (Low-Medium)

- **Paper:** only the horizon entry masks are said to be cleared on eviction (4.4 Regeln). Other state also
  holds row indices across time and is not mentioned: the via mask and D60 in the head (recomputed only once
  per minute), the echo table's first- and second-hand masks (live 300-360 s), `ringNeed[]`/`ringAlone[]`
  (become `NbrMask`, 4.2), and the frozen order of the BLE list at connect (4.6: "läuft wie heute in Portionen").
- **Code:** ringNeed/ringAlone are written at relay enqueue and consumed by overhear-cancel seconds to minutes
  later (txring_functions.cpp:60-61, :696-697; lora_functions.cpp:862-894). They hold 2-hop rows, which are
  exactly the pass-0 eviction victims (nbr_matrix.cpp:189-212). The BLE list order is walked across loop
  iterations on **both** platforms, with RX in between (mheard_functions.cpp:779-804, :873-890). Memory note
  ble-com-ring-connect-flood names two traps for porting this cursor (freeze order not instant; occupancy
  guard). Row reuse is a third trap, which the index-based topology introduces.
- **Scenario:** 2-hop row r is in a pending relay's need mask. A burst of new callsigns evicts r and reuses it
  for station Z. The overheard relayer happens to cover Z, so the relay is cancelled although the original
  2-hop station still depends on me. Or: the list cursor holds row r, which is reused by a new direct neighbour.
  It is sent (harmless), and the old station is silently skipped. Via members are fresh direct rows and are
  protected in pass 0, so a wrongly named via needs a pass-1 eviction (all 64/128 rows fresh-direct), which is
  rare.
- **Needed:** either clear every index-keyed mask in the same eviction step, or store the 64-bit call word next
  to each held index (cheap now that a call is one word) and re-validate it on use.

### E-4 Live BLE push trigger "wenn sich last_min der Zeile ändert" is defeated by the same frame (Medium)

- **Paper 4.6:** live MH frame at most once per neighbour per minute, triggered when the row's `last_min`
  changes.
- **Who pushes, from where:** it must be the RX path (OnRxDone after the ME step, as `updateMheard()` does today,
  mheard_functions.cpp:417-439). On nRF52 that is the LORA task. The target ring locks itself
  (loop_functions.cpp:678-682), so the context itself is fine.
- **The bug:** `nbrNoteFrame()` bumps `rows[x].last_min` and `rows[y].last_min` for every in-window edge
  (nbr_matrix.cpp:664-668) **before** the ME step (:676-684), and y is the last hop, i.e. the ME row. For every
  relayed frame (>= 2 tokens) the last hop's row already holds `now_min` when the ME step runs. Implemented
  literally, the rule never pushes a neighbour that is heard relaying. A neighbour named as a relay token in
  someone else's frame earlier in the minute also suppresses its own direct push in that minute.
- **Needed:** trigger on the minute of the ME edge (x, me) or of the slot, captured **before** the frame is
  applied.

### E-5 Page render time is an RX-loss window, and it grows with rows² (Medium)

- **Paper:** 128 rows on S3/nRF52, 64 on classic. "Die Nachbarschaft über 2 Hops zeigt die Web-Seite Nachbarn".
  Path view: "alle Wege". Page layout and size are not discussed.
- **nRF52:** `loopWebserver()` runs with `bSPI_ETH_Active = true` (nrf52_main.cpp:2490-2492). OnRxDone then
  **defers the radio re-arm** (lora_functions.cpp:597-601), so the radio is deaf from the first packet until
  the page is finished. Each `print` is one `socketSend()` busy-wait.
- **ESP32:** the radio is re-armed only in `checkRX()` from the loop, and the page renders synchronously in the
  loop. Every packet after the first during the render is lost (BACKLOG.md:2672-2680).
- **Scale:** the cross table prints one call per cell (web_functions.cpp:1826-1843): 21² = 441 cells today;
  p90 48 rows = 2,304 cells (5x); 128 rows = 16,384 cells (37x), which is about 150 kB of `<td>` even when most
  cells are empty. On nRF52 that is several seconds of deafness per page view (estimate, not measured).
- **Needed:** a page design that does not scale with R² (list per row with hearer names, or paging), and
  buffered writes.

### E-6 The rollout never exercises the one platform where the races exist (Medium, process)

- Step 3 (24 h shadow) and step 6 (Auto-Via) run on DK5EN-98 and DK5EN-1, **both Heltec V3 (ESP32-S3)**. There,
  RX and every reader share loopTask, so E-2/E-3 cannot occur.
- nRF52 is the only concurrent family, and it also gets the largest structure (128 rows). It first appears in
  step 5 (DK5EN-90). Its acceptance ("kein Neustart, Heap, NCNT plausibel") has no DIFF or consistency check.
- A shadow run on an nRF52 node with `--nbrdebug on`, a host attached and the web page polled would be the
  first test that can hit E-2.

### E-7 nRF52 4 KB loop stack: row-sized locals grow 6x (Medium)

- **Paper 4.7** budgets stack only for the MH view ("Sicht von 48 Byte").
- **Code:** every other reader sizes locals by `NBR_MAX_ROWS`, justified by "<= 21 ... klein genug fuer den
  4-KB-Loop-Stack" (web_functions.cpp:1611-1612). `sub_page_neighbours()` holds 9 arrays
  (:1613, :1625, :1649-1651, :1863, :1867, :1892, :1937): 189 B today, 1,152 B at 128 rows.
- **Worse:** `nbrFormatRow()` has `char hearers[NBR_MAX_ROWS * NBR_CALL_LEN]` plus `hearer_idx[NBR_MAX_ROWS]`
  (nbr_matrix.cpp:1492-1494): 231 B today, **1,408 B at 128 rows**. It is called from `--neighbours` inside
  `commandAction()` (command_functions.cpp:4901, :4911, :4938). The measured chain is nrf52loop 792 +
  commandAction 1120 (mheard_functions.cpp:946), plus +214 B for the grown `nbr_rows`/`ex_idx`, plus ~1.4 kB
  in the formatter plus snprintf: about 3.6-3.9 KB of 4 KB. N-22 has already measured zero reserve on this
  stack once (08-defect-catalogue.md:1028-1045).
- The own-message path (checkSerialCommand -> sendMessage -> checkVia) has about 1 KB reserve after the N-22
  fix. A `printfdeb` there for a via or echo log line costs about 960 B plus newlib.
- **Needed:** local sets as `NbrMask` (16 B) instead of `uint8_t[R]` lists; the hearer-name string in BSS or
  streamed; no log line on the own-send path.

### E-8 The minute clock jumps at the 32-bit millis wrap (49.7 d); stage 3 extends this to MHeard/NCNT/Via (Low-Medium)

- Every `now_min` is `(uint16_t)(millis() / 60000UL)` (lora_functions.cpp:777, :830, :988, :1843;
  web_functions.cpp:1597; command_functions.cpp:4884; loop_functions.cpp:5149).
- millis wraps at 2^32 ms = 71,582 min, which is not a multiple of 65,536. So `now_min` jumps from 6,046 to 0,
  and every stored minute then looks about 59,490 min old. The header comment covers only the 16-bit wrap
  (nbr_matrix.h:59-70).
- **Effect after the jump:** nothing is fresh; the ghost sweep (>= 32768, nbr_matrix.cpp:274-302) zeroes the
  rows. HM (12 h of evidence, one proof per 2.7 h for DL2JA-2) and the f/s counters are gone, so K is empty,
  the via set is invalid and the node floods for hours. NCNT dips and the MH list empties.
- Today MHeard is wrap-safe: uint32 millis differences, NC-01 (mheard_functions.cpp:702, :882). Stage 3 turns a
  latent matrix-only bug into a MHeard, NCNT and Auto-Via regression.
- Also, `DATE = Bootepoche + last_min` (4.3) is wrong after 45.5 days unless it is rebased at each wrap.
- **Fix:** derive minutes from a 64-bit ms base, or keep a loop-maintained minute counter.

### E-9 Echo control timing is underspecified (Low-Medium)

- **(a) Window start is undefined.** The start could be at enqueue (`checkVia`) or at TX-done. Own frames wait
  in the TX ring, where every heard frame re-arms CSMA (docs/nbr-wichtigkeit-konzept.md:139-156).
- **(b) The 300 s margin is judged against the wrong statistic.** "Fast das Dreifache" compares 300 s with
  DL2JA-2's **median** of 109 s. Its **p90 is 206 s** (docs/nbr-wichtigkeit-konzept.md:137), and that is
  measured from the first heard copy, without my own queue wait. A via set whose only member is the TX-starved
  super node (no Zubringer) will see a noticeable share of late echoes. The paper's 2 % false-pair estimate
  assumes the window never cuts.
- **(c) The echo table can overflow.** It has 4 entries, and folding happens only in the minute sweep, so an
  entry lives 300-360 s. Five own frames in 5-6 min (a chat burst from the app) overwrite an unfolded entry.
  The paper does not say whether an overwrite counts as a miss. If it does, a false SPERRE follows.
- **(d) checkVia is not only called for own on-air frames.** Callers include relays (lora_functions.cpp:1827),
  the BLE command-back to the phone (loop_functions.cpp:750), gateway UDP frames (udp_frame_esp32.cpp:252, :349;
  udp_frame_nrf52.cpp:197, :353) and test inject. So the echo insertion must not live in checkVia.
- **(e) nRF52 ordering race.** If insertion happens at TX-done in the loop, a fast echo processed by the LORA
  task while the loop is stalled (web page, W5100S N-20) finds no entry, and a healthy via counts as failed.
  Insertion at enqueue together with the own-TX msg_id (as `insertOwnTx` does, loop_functions.cpp:3435) avoids
  this.

### E-10 Shadow DIFF: the minute clock against the millisecond clock at window edges (Low)

- MHeard ages in ms; the topology ages in whole minutes (a frame at xx:00:59.9 is "1 min old" 0.2 s later).
  Each neighbour that falls silent produces up to 1-2 boundary minutes of DIFF at the 1 h (NCNT) and 12 h (MH
  set) edges.
- The step-3 acceptance ("jede Abweichung durch DROP TOK, Verdrängung oder B2 erklärt") does not list
  quantisation, so these DIFFs show up as unexplained. The same holds if the DIFF compare uses a D60 that is
  one sweep old.

## 2. Checks that came out fine

- **ESP32 (all families incl. T-Deck/T-Deck Pro):** RX, relay, `checkVia`, web, console, BLE list, LVGL and SD
  persistence all run on loopTask, and NimBLE only enqueues. Torn 64- and 128-bit mask reads are impossible.
  The `topo.dat` snapshot is consistent: the save runs in loopTask, and the load runs in T-Deck setup
  (tdeck_main.cpp:263) before `checkRX()` ever runs. The 13.3 kB write every 10 min is no worse than today's
  30-s double write from inside OnRxDone (mheard_functions.cpp:469, :667). It should be triggered from the
  sweep, not from the RX path.
- **nRF52, plain 128-bit mask stores:** with equal priority and time-slicing off, a two-word store cannot be
  observed half-written unless the writer yields between the words. The risk exists only for multi-step
  sequences that contain a yield (E-2).
- **nRF52 prio-2 contexts:** they never touch the topology. The RX-timeout timer path is never armed
  (memory note).
- **Live-push context and cost:** the push runs in RX context into a self-locking ring. ROLE/EX/VIA per push is
  O(D x R x W), about 16k word ops, which is negligible. Once per minute is also less heap churn than today's
  per-frame JSON.
- **Per-minute via set cost at 128 rows x 512 edges:**
  - N2, Pflicht and the greedy cover are mask operations, O(|K| x W) per round, at most 3 rounds.
  - The Zubringer search is weak x non-weak x a linear 512-edge scan: at most 32 x 32 x 512 ≈ 0.5 M compares,
    about 10-20 ms on nRF52 at 64 MHz and a few ms on S3.
  - D60 is one pass over the edges.
  - This is fine from either context. OnRxDone would exceed `ONRXDONE_WARN_MS` 50 (configuration_global.h:404)
    only if the Zubringer search degenerates.
- **MPR stack:** about 10 NbrMask locals (≤ 160 B) plus one per-x counter array. That fits on the 16 KB LORA
  task and at nrf52loop depth. The relay `checkVia` runs on the 16 KB LORA task (nRF52) or the 8 KB loopTask
  (ESP32).
- **Route enumeration** is bounded (S -> A -> B -> me, no recursion), so its stack depth is not an issue.
- **60 min SPERRE, 10 min save, 300 s fold at minute granularity:** the effective windows of 300-360 s and
  60-61 min are lenient. Measured loop stalls of seconds (BACKLOG TM-35/TM-44) do not matter at these scales,
  except for E-8.
- **T5-ePaper note:** it would be the only ESP32 build with a genuinely parallel RX task (peri_lora.cpp:179,
  :223, unpinned, prio 23, 3 KB). The env is disabled (platformio.ini:20 `;t5_epaper`), so it is out of scope.

## 3. One paragraph the paper is missing ("Nebenläufigkeit")

- **Ownership:** all topology mutation happens in the RX path, or in a named loop-side sweep that is bracketed
  on nRF52.
- **No logging inside a mutation:** log lines are emitted after commit.
- **Index-keyed state:** every structure holding a row index is cleared in the eviction step or carries the
  call word for validation.
- **Yielding readers:** readers that yield (web, console, BLE list) copy one view per entry without yielding,
  and re-validate the call word.
- **Echo table:** the insertion point and the window start are defined.
- **Rollout:** one nRF52 node joins the shadow step.
