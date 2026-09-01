# F4 — Altitude / Architectural Judgement

Adversarial review of `docs/architecture/` (README, 01–07). Angle: attack the **reasoning**, not the
facts. Everything below is grounded in the tree at `v4.35p_prio` / `1ba101f4`.

Where I agree, I say so in one line and move on.

**Agreed without argument:** 02's B-07 (CI on tags only) is the correct #1 finding in the whole set.
03's "espressif32 version bumps are cosmetic because every release pins Arduino 2.0.17" is a genuinely
good piece of analysis. 06 Layer 1/Layer 2 (native codec tests + golden vectors from
`tools/meshcom_monitor/*.log`) is the right first engineering step and is feasible — `decodeAPRS()`
has exactly **one** config dependency (`meshcom_settings.node_call`, `src/aprs_functions.cpp:417`),
so it is genuinely near-pure. 04's "do not delete `batt_function_old.cpp`" is right and important.

---

## Findings

### F4-1: The "radio interface collapses ~3,200 duplicated lines" claim is a non sequitur — the measured duplication is in sensor polling, and a radio interface removes none of it

**Target:** `01-system-overview.md` §3 (lines 132–142) and the "three highest-leverage changes"
(lines 180–184); `04` remediation item 7 (line 212); `05` Phase 3 (lines 126–135).
**Severity: critical** — this is the doc's #1 recommendation and the most expensive phase in the plan.

**The argument.** 01 §3 makes a causal claim: _"Because the scheduler sits above the radio API rather
than above an interface, `esp32loop()` and `nrf52loop()` had to be written twice ... 46 cloned 12-line
windows between `esp32_main.cpp` and `nrf52_main.cpp` confirm it mechanically."_ The clone count is
offered as proof of the radio cause. It is not. I re-ran the clone detection restricted to the two
loop bodies and then **looked at what the clones contain**.

**Evidence.**

Clone measurement over `esp32loop()` (`src/esp32/esp32_main.cpp:1743–3689`, 1,497 non-blank lines)
vs `nrf52loop()` (`src/nrf52/nrf52_main.cpp:1102–2334`, 958 non-blank lines), whitespace/comment
normalised, 12-line window:

| Metric                                         | Value           |
| ---------------------------------------------- | --------------- |
| Shared 12-line windows                         | 100             |
| `esp32loop()` lines covered by a shared window | **221** (14.8%) |
| Identical distinct normalised lines            | 311 of 752      |

The doc claims a radio interface would collapse **~3,200 lines** (= 1,947 + 1,233, i.e. both function
bodies in full). The measured mechanical overlap is **~220–310 lines**. That is an overstatement of
roughly **10×**.

Worse, the shared windows are not radio code. The de-duplicated shared window starts are:

| esp32_main.cpp | nrf52_main.cpp | Content                                       |
| -------------- | -------------- | --------------------------------------------- |
| 1945           | 1257           | deferred display text (`bPendingDisplayText`) |
| 2524           | 1171           | NTP/RTC date-string parsing                   |
| 3049           | 1784           | `sendPosition(...)` position beacon           |
| 3065           | 1800           | `posinfo_distance` reset                      |
| 3347/3361/3375 | 2108/2122/2136 | sensor polling blocks                         |
| 3411           | 2164           | `if((bBMP3ON && bmp3_found))`                 |
| 3460           | 2216           | sensor polling                                |

**Not one** of the shared windows is in the radio state machine. Every one of them is already
radio-independent and could be extracted into a shared `common_loop.cpp` _today_, with no radio
abstraction, no RF risk, and no on-air verification. The doc's #1 highest-leverage item is therefore
proposed for the wrong reason, sized 10× too large, and its cheap 90%-of-the-value alternative is
never considered.

**Revised recommendation.** Delete "radio interface" from the highest-leverage list. Replace with:
_extract the ~220 lines of measured shared periodic work (deferred display, NTP/RTC parse, position
beacon, sensor polling) into `src/common_loop.cpp` called by both loops._ Small, mechanical, zero RF
risk, upstreamable as 3–4 PRs of the size upstream already accepts. Then re-measure before spending
20–30 sessions on a radio HAL.

---

### F4-2: Phase 3 is not incrementally achievable — RadioLib and SX126x-Arduino differ in _concurrency model_, not just API, and the doc's own mitigation does not mitigate it

**Target:** `05-rewrite-vs-refactor.md` Phase 3 (lines 126–135), esp. _"It is also the riskiest step,
which is exactly why Phase 0 has to come first."_
**Severity: critical.**

**The argument.** The doc proposes _"One interface with `begin/setChip/startRx/transmit/cad/onRxDone/
onTxDone`. RadioLib behind one implementation, SX126x-Arduino behind another. Then `esp32loop()` and
`nrf52loop()` collapse into a single scheduler."_ That framing assumes the difference is API shape.
It is not. The two stacks put the RX and CAD handlers in **different execution contexts**, and the
application code is already written to two different concurrency contracts.

**Evidence — RX context.**

- **ESP32:** the actual ISR is `setFlagReceive()` (`src/esp32/esp32_main.cpp:487–498`) — it sets two
  `bool`s and returns. `OnRxDone()` is then called from `checkRX()`
  (`src/esp32/esp32_main.cpp:3692`, call at **:3778**), which is called from `esp32loop()` at
  **:2217**. `OnRxDone()` runs in **main-loop context**.
- **nRF52:** `RadioEvents.RxDone = OnRxDone` (`src/nrf52/nrf52_main.cpp:940`). SX126x-Arduino's ISR
  (`RadioOnDioIrq`, `.pio/libdeps/wiscore_rak4631/SX126x-Arduino/src/radio/sx126x/radio.cpp:1346`)
  does `xSemaphoreGiveFromISR(_lora_sem, …)`, which wakes a dedicated FreeRTOS task created at
  `.../SX126x-Arduino/src/boards/mcu/board.cpp:498`
  (`xTaskCreate(_lora_task, "LORA", 4096, NULL, TASK_PRIO_NORMAL, …)`). `RadioBgIrqProcess()`
  (`radio.cpp:1358`) invokes `OnRxDone()` **on that separate task**, concurrently with `nrf52loop()`.

That is why `OnRxDone()` contains `taskENTER_CRITICAL()` around the double-buffer swap
(`src/lora_functions.cpp:315–323`) — and why that whole block is inside `#if defined BOARD_RAK4630`.

**Evidence — CAD context.**

- **ESP32:** `radio.scanChannel()` (`src/esp32/esp32_main.cpp:2380`, double-check at `:2399`) is
  **synchronous and blocking**; the result is a return value consumed inline.
- **nRF52:** `Radio.StartCad()` (`src/nrf52/nrf52_main.cpp:1364`, `:1417`) is **asynchronous**; the
  result arrives at `OnCadDone(bool)` (`src/nrf52/nrf52_main.cpp:388`) on the LoRa task. The loop
  therefore maintains explicit CAD state (`// CSMA/CA async CAD state`, `src/nrf52/nrf52_main.cpp:234`),
  a critical section (`RACE-05 fix`, `:1336`), and a **`CAD_SAFETY_TIMEOUT`** (`:1443`) that has no
  ESP32 counterpart at all.

A `cad()` method cannot hide sync-return vs async-callback-on-another-task. Unifying means either
blocking the nRF52 LoRa task on a semaphore waiting for `OnCadDone` — reintroducing the RX blind
window that `// FIX BUG #2` (`src/lora_functions.cpp:305`, `src/esp32/esp32_main.cpp:3718`) exists to
close — or converting ESP32 to interrupt-driven CAD, a different RadioLib code path than
`scanChannel()`. **Either direction is a behavioural change to RF timing on the entire deployed
fleet.**

**Evidence — interface width.** The proposed interface has 7 methods. The actual ESP32 radio surface
is 91 `radio.` call sites in `esp32_main.cpp` plus 9 in `lora_setchip.cpp`, across ~30 distinct
methods, including `getFrequencyError`, `getIrqFlags`, `finishTransmit`,
`clearPacketReceivedAction`, `setRxBoostedGainMode`, `setRfSwitchPins`, `setTCXO`, `setCurrentLimit`.
SX126x-Arduino exposes no direct equivalent for several of those. A 7-method interface cannot cover
91 call sites; the honest interface is ~25–30 methods, at which point it is a RadioLib-shaped
interface the nRF52 side cannot implement — or a lowest-common-denominator interface that silently
drops ESP32 capability.

**The reasoning failure.** The doc says Phase 0 (the harness) is the mitigation for Phase 3's risk.
But Phase 0 as specified is **native unit tests** (06 Layer 1–2) — and the doc's own scenario
catalogue (07 §7) marks every CAD and CSMA scenario (#11–#16) as **"2-node bench"**, i.e. Layer 4,
the last and most expensive layer. Phase 0 does not cover the thing Phase 3 breaks. The stated
mitigation is not a mitigation.

**Revised recommendation.** Strike Phase 3 from the plan as written. If the goal is "nRF52 stops
lagging ESP32 on features", the achievable version is F4-1: extract the shared _non-radio_ periodic
work. If a radio abstraction is ever attempted, it must be gated on the two-node bench (07 §4) plus a
seeded-CSMA baseline captured **before** the change — not on native tests. Re-cost it as 60–100+
sessions, not 20–30.

---

### F4-3: The command-table refactor is not "mechanical" — 218 arms are not 218 independent handlers, they are ~230 prefix tests across 7 chain segments feeding an 11-flag shared continuation

**Target:** `04-complexity-and-duplication.md` §`commandAction()` (lines 48–98, esp. line 50 _"It is
one flat `if / else if` chain"_ and line 84 _"The fix is mechanical and high-value"_); `05` Phase 2
(lines 119–124, _"This is mechanical, reviewable in slices"_).
**Severity: critical** — this refactor is ranked "do early, low–medium risk" in three documents.

**The argument.** The doc's model is: 218 arms → 218 handlers + a table. That model is wrong in three
independent ways, each of which alone would break a naive conversion.

**Evidence 1 — it is not one chain; it is seven.** Brace/`else` analysis over
`commandAction()` (`src/command_functions.cpp:194–5112`) finds **230 `commandCheck(msg_text+2, …)`
test sites** and **8 chain breaks** — points where a `commandCheck` `if(` begins with no preceding
`else`:

| Break at line | First verb of the new segment                    |
| ------------- | ------------------------------------------------ |
| 244 / 257     | (commented-out `compress ` block, then `utcoff`) |
| **1478**      | `gps autosymbol`                                 |
| **2227**      | `operatorname `                                  |
| **2749**      | `setl76k `                                       |
| **2981**      | `softser xml`                                    |
| **3205**      | `aprscomment `                                   |
| **3532**      | `setownms `                                      |

An arm in segment _n_ that does **not** `return` continues to be tested against every arm in segments
_n+1 … 7_. This is load-bearing, not incidental — see Evidence 2.

**Evidence 2 — an 11-flag shared continuation, with ~190 set-sites.** The function opens by declaring
11 local booleans (`src/command_functions.cpp:211–221`): `bInfo`, `bPos`, `bShowPos`, `bWeather`,
`bTelemetry`, `bIO`, `bReturn`, `bSensSetting`, `bWifiSetting`, `bNodeSetting`, `bAnalogSetting`.
Arms set them and fall through; the epilogue consumes them as further links of the chain:

| Flag                              | set-sites | consumed at                 |
| --------------------------------- | --------: | --------------------------- |
| `bReturn`                         |    **83** | `:5095`                     |
| `bSensSetting`                    |        28 | `:4935`                     |
| `bNodeSetting`                    |        26 | `:5081`                     |
| `bWifiSetting`                    |        14 | `:5011`                     |
| `bAnalogSetting`                  |        11 | `:5088`                     |
| `bInfo`                           |         8 | `:4694`                     |
| `bPos`                            |         5 | `:4912`                     |
| `bWeather`                        |         3 | `:4503`                     |
| `bIO` / `bTelemetry` / `bShowPos` |    1 each | `:4593` / `:4463` / `:4921` |

Concretely: `--info` matches at `src/command_functions.cpp:719`, sets `bInfo=true`, and **does not
return**. It reaches its actual behaviour 3,975 lines later at `if(bInfo)` (`:4694`) — and it can only
get there because of the chain breaks in Evidence 1. That is not a dispatch table; it is a
continuation-passing state machine implemented in `goto`-by-fallthrough.

Converting to `struct Command { name, argc, handler }` requires, for each of the ~190 flag set-sites,
proving that no arm in any later segment can also match the same input — and the intervening tests are
`#ifdef`-gated (206 preprocessor branches in this file, per 04's own table), so the intervening set is
**board-dependent**. That is the opposite of mechanical.

**Evidence 3 — `commandCheck` is a case-insensitive _prefix_ match, so the doc's own alternative
implementation is wrong.** `src/command_functions.cpp:113–124` copies the input, truncates it to
`strlen(command)`, and compares case-insensitively. 04 line 96 offers _"Dispatch becomes a loop (**or
a sorted binary search**)"_. A sorted binary search over prefix-matching keys is not well-defined —
`--setinfo off` would not reliably locate the `"setinfo off"` entry. That suggestion would silently
change behaviour.

**Evidence 4 — a live latent bug the doc's model cannot see.** `"setowndns "` is tested **twice**:

- `src/command_functions.cpp:3490` — `snprintf(..., msg_text+12)` — correct offset, ends with `return;`
  at `:3504`.
- `src/command_functions.cpp:3558` — `snprintf(..., msg_text+11)` — **off by one** (would store a
  leading space), in a later chain segment, and therefore **dead code**.

Similarly `"aprscomment "` at `:3203`/`:3205` and `"operatorname "` at `:2225`/`:2227` are duplicated
across segment boundaries. A "218 arms → 218 handlers" conversion has no way to notice that ~30 lines
are unreachable and buggy, because its model says every arm is reachable.

**Evidence 5 — the ordering hazard the doc names is nearly absent; the real one is different.** 04
line 79 says _"`--setinfo off` must be tested before `--setinfo`, and nothing enforces that."_ In
fact there is no bare `--setinfo` arm at all (only `"setinfo off"` at `:447` and `"setinfo on"` at
`:456`). Exhaustive prefix-collision analysis across all 230 test sites finds exactly **one**
badly-ordered pair: `"softser app"` (`:2878`) before `"softser app0"` (`:2887`). The doc identified a
mostly-hypothetical risk and missed the structural one (segments + flags) entirely.

**Revised recommendation.** Do not convert to a table yet. Ordered, each independently shippable:

1. Delete the dead duplicate arms (`setowndns` at `:3558`, and audit `aprscomment`/`operatorname`).
2. Add `tools/check_commands.py` to CI: extract all `commandCheck` literals, fail on duplicates and on
   any shorter verb tested before a longer verb that starts with it. This is the actual guardrail the
   doc wants, costs one afternoon, and needs no source change.
3. **Collapse the 7 chain segments into 1** and make each flag-setting arm's continuation explicit
   (`goto epilogue` or a small `enum Post { NONE, INFO, POS, … }` returned by the arm). This is the
   mechanical, reviewable, behaviour-preserving step, and it is the one that makes step 4 possible.
4. _Only then_ consider a table, and only with a linear scan preserving declaration order.

---

### F4-4: 01's pipeline diagram inverts the actual order and mislabels the IRQ boundary — a test author following it will write wrong assertions

**Target:** `01-system-overview.md` mermaid diagram (lines 28–52); `04` §`OnRxDone()` (lines 115–123).
**Severity: high** — 06 and 07 build their scenario catalogues on this picture.

**The argument.** The diagram draws `RF --|IRQ|--> OnRxDone --> DEDUP --> DECODE --> MHEARD/ROUTE`.
Both the boundary and the order are wrong.

**Evidence — the IRQ arrow.** See F4-2: on ESP32 the ISR is `setFlagReceive()`
(`src/esp32/esp32_main.cpp:487`) and `OnRxDone()` runs in the main loop via `checkRX()`. On nRF52 it
runs on the `"LORA"` FreeRTOS task. It runs in **IRQ context on neither platform**. 04 lines 118–120
compound this: _"Because it runs off the radio callback, everything it touches is shared with the main
loop — and only 12 of the 423 globals are atomic."_ On ESP32 that is exactly backwards — `OnRxDone`
**is** the main loop, and the real unsynchronised sharing is with the NimBLE task and the async web
server (`src/web_functions/web_setup.cpp` calls `commandAction()` from ~60 sites). The doc points the
concurrency alarm at the wrong boundary.

**Evidence — the order.** Inside `OnRxDone()` (`src/lora_functions.cpp:288`) the actual sequence is:

| Line   | Call                                     |
| ------ | ---------------------------------------- |
| `:455` | `decodeAPRS(RcvBuffer, size, aprsmsg)`   |
| `:459` | `checkOwnTx(aprsmsg.msg_id)`             |
| `:626` | `updateMheard(mheardLine, isPhoneReady)` |
| `:637` | `updateHeyPath(mheardLine)`              |
| `:673` | `is_new_packet(RcvBuffer+1)` ← **dedup** |

Decode and the neighbour-table update happen **before** dedup, not after. That is almost certainly
deliberate (you want to record that you heard a neighbour again, even on a duplicate), but it is the
opposite of what the diagram shows. Anyone writing 07's scenario #5 ("duplicate `msg_id` recognised,
not relayed") from the diagram will also expect the duplicate not to touch mheard — and will file a
bug against correct code.

**Evidence — "pipeline stages".** They are not stages. `sendDisplay*`, `addBLEOutBuffer` (7 sites),
`queueExtern`, and direct `ringBuffer[iWrite]` writes (`:882–899`) are all inline inside one 968-line
function at nesting depth 13. The diagram's four parallel `ROUTE -->` edges suggest a dispatcher that
does not exist.

**Revised recommendation.** Redraw with the ISR/flag/poll seam explicit and the real ordering:
`RF --IRQ--> setFlagReceive (flag only) --poll--> checkRX --> OnRxDone{decode → mheard → dedup →
route → {display, BLE, UDP, TX ring}}`, with a note that nRF52 substitutes `_lora_task`. The seam is
not cosmetic — it is where `--inject` belongs (F4-6).

---

### F4-5: 04's remediation items 1–3 propose trading measured duplication for exactly the `#ifdef` HAL that 01 calls the second-worst defect — in files that never coexist in a binary, two of which are vendored third-party code

**Target:** `04-complexity-and-duplication.md` ranked remediation, items 1–3 (lines 206–208), and
line 216 _"Items 1–4 are safe, mechanical, and can go upstream as small PRs today."_
**Severity: high.**

**The argument.** The brief asks whether forking-per-board has a legitimate rationale. For these three
items specifically it does, and the doc's own framework says so — 05 §5 (lines 79–85) argues _"The
complexity that looks accidental from the outside is often a fix."_ 04 then ignores that for the three
cheapest-looking wins.

**Evidence — the files can never coexist, so there is no flash or binary benefit.**
`variants/t_deck_pro/platformio.ini:13–20` sets `build_src_filter = … +<t-deck-pro/*> -<t5-epaper/*>`;
`variants/t5_epaper/platformio.ini:34–46` sets `+<t5-epaper/*> -<t-deck-pro/*>`. `peri_gps.cpp` and
`ui_scr_mrg.c`/`scr_mrg.cpp` are mutually exclusive by construction. Merging them saves **zero** bytes
in every shipped image; it only reduces line count in a repository listing.

**Evidence — the merge cost is paid in the currency the doc says is most expensive.** The only way to
merge two mutually-exclusive files is `#if defined(BOARD_T_DECK_PRO) / #elif defined(BOARD_T5_EPAPER)`
inside the merged file. 01 §2 counts 1,131 `BOARD_*` references as the second-worst structural defect
and 04's own preprocessor-density table is a complaint about exactly this. Items 1–3 **increase** that
number to reduce a line count that costs nothing.

**Evidence — the coupling is asymmetric and CI-invisible.** `t5_epaper` is declared but **not in
`default_envs`** (02 line 22). Merging couples a shipped board (`t_deck_pro`) to a board that is not
built by anyone, on a repo whose CI runs on tags only (02 B-07). The first `t_deck_pro` GPS fix after
the merge is an unreviewed, unbuilt change to `t5_epaper`.

**Evidence — item 3 targets vendored third-party code.** `src/Platforms/*/power_controls.cpp` lives
inside a vendored copy of the `heltec-eink-modules` library: `src/Platforms/platforms.h` also ships
`M328P`, `M2560`, `M1280`, `SAMD21G18A`, `ESP8266` ports, and `src/heltec-eink-modules.h` is its
umbrella header pulling in `src/Displays/` and `src/GFX_Root/`. The README's scope note (lines 39–43)
excludes `lib/` as vendored but counts `src/Platforms/` (~1,130 lines) and `src/Displays/` as _own
code_. Refactoring inside a vendored tree forfeits the ability to re-sync it. And the diff is not
boilerplate: `diff VisionMasterE213/power_controls.cpp WirelessPaper/power_controls.cpp` shows the
WirelessPaper variant carries a distinct, documented deep-sleep rationale (separating the SX1262 sleep
from `prepareToSleep()` so the e-ink OTP refresh gets the full energy budget on a near-empty battery)
that E213 does not. That is precisely 05 §5's _"three days of someone's life compressed into one
line."_

**What I do agree with:** item 4 (move mheard logic out of `t-deck-pro/ui_deckpro.cpp` back into
`mheard_functions.cpp`) is a real layering fix and should stay — it removes a genuine second
implementation of shared logic, not a per-board fork of board-specific logic.

**Revised recommendation.** Drop items 1–3. Replace with: _leave per-board display/GPS/power forks
alone; they are correct under the current build model._ If the duplication genuinely hurts, the fix is
a shared driver in a **new** file both boards include (additive, no `#ifdef`, no vendored-tree edit),
not a merge — and it should wait until CI builds `t5_epaper`.

---

### F4-6: 07's `--inject` hook is the best idea in the set, justified with the wrong reasoning, specified at the wrong seam, and it silently breaks the seeded-CSMA scenario it sits next to

**Target:** `07-verification-infrastructure.md` H-01 (lines 251–276), §2.1 (lines 158–206),
scenario table §7 (#5–#12).
**Severity: high.** (The hook should be built. The spec as written should not.)

**The argument.** The brief asks whether `--inject` would bypass IRQ context, buffer switching and
RadioLib state. Per F4-2/F4-4, the answer is better than the doc thinks on ESP32 and worse than it
thinks on nRF52 — and the doc, believing `OnRxDone` is an IRQ callback, gets the seam wrong.

**Evidence — fidelity is high on ESP32, and the doc doesn't know why.** Because `OnRxDone()` already
runs in main-loop context on ESP32 (`checkRX` at `src/esp32/esp32_main.cpp:3778`), and `commandAction`
also runs in the loop for the serial path, an injected call is in **the same execution context as a
real packet**. That is a strong argument for H-01 that the doc never makes.

**Evidence — but the specified entry point skips the parts that matter.** `checkRX()`
(`src/esp32/esp32_main.cpp:3692–3866`) does, before and after calling `OnRxDone`:
the `is_receiving` re-entrancy guard (`:3706`, `:3709`, cleared `:3866`); the
save-RSSI/SNR/FreqError-before-restart sequence (`:3718–3721`); the immediate RX restart with
`clearPacketReceivedAction`/`startReceive`/`setPacketReceivedAction` and the missed-DIO1-edge recovery
(`:3730–3746`); and `ch_util_rx_accum.fetch_add(radio.getTimeOnAir(ibytes)/1000)` (`:3776`).
Injecting at `OnRxDone` skips all of it — including the channel-utilisation accounting that scenario
#17/#23 would want. Inject at the `checkRX` seam (a `MC_TEST_HOOKS` branch that substitutes the
`radio.readData()` result) and you get the whole path.

**Evidence — `--inject` perturbs CSMA, so scenarios #5–#10 and #12 are mutually exclusive.**
`OnRxDone` itself calls `csma_timeout = csma_compute_timeout(cad_attempt)` on the ACK early-return
path (`src/lora_functions.cpp:402`) and `OnRxTimeout` does the same at `:1242`; `esp32loop()` calls it
at `:2224` (after **every** received packet), `:2361` and `:2465`. `csma_compute_timeout_prio()` ends
in `random(0, slots+1)` (`src/lora_functions.cpp:2076`) — the _only_ `random()` call site in `src/`.
So every injected frame consumes one PRNG draw. §2.1 claims that with `-D MC_TEST_SEED=12345` _"the
backoff sequence becomes byte-for-byte replayable, and you can assert exact slot selection and exact
retry timing"_ (lines 202–204), and scenario #12 asserts exactly that. But `randomSeed()` makes the
**stream** reproducible, not the **values at a point in time**: the value you observe depends on how
many draws preceded it, which depends on how many packets arrived, how many CAD attempts ran, how many
RX timeouts fired — i.e. on `millis()` jitter, on WiFi/NimBLE task preemption, and on any foreign
frame the bench happens to hear. One extra CAD attempt shifts the whole sequence. **Scenario #12 is
flaky by construction**, and cannot be combined with `--inject` at all.

**Evidence — nRF52 fidelity is low and the doc doesn't flag it.** On nRF52, `OnRxDone` runs on the
`"LORA"` task; injecting from `commandAction` runs it on the main task, skipping the double-buffer
swap and `taskENTER_CRITICAL()` block (`src/lora_functions.cpp:296–374`) that exists precisely because
of that task boundary. `--inject` on nRF52 tests a code path that never executes in production.

**Evidence — the ISR-debug hook the doc promotes is a no-op on the stated bench hardware.** 07 §1.1
lists `RX_BUF_SWITCH`, `RX_BUF_RELEASE`, `RX_BUF_OVERWRITE`, `CAD_ABORT_BY_RX`, `RX_RESTART_EARLY` as
assertable markers, V-03 (line 527) calls `LORA_ISR_DEBUG` an _"undocumented dormant hook"_ whose fix
is _"document + test env"_, and build-out step 2 (line 560) adds `-D LORA_ISR_DEBUG` to the test
environment. All five `LORA_ISR_DEBUG` sites in `src/lora_functions.cpp` (`:331`, `:337`, `:358`,
`:364`, `:371`) are **also inside `#if defined BOARD_RAK4630`**. The document's own header (line 10)
says it is _"Written against the available hardware: 2 × Heltec WiFi LoRa 32 V3"_. On that hardware,
adding `-D LORA_ISR_DEBUG` unlocks **nothing**, and four of the §1.1 marker groups are unreachable.

**Revised recommendation.** Build `--inject`, but: (a) place the hook at the `checkRX` seam, not at
`OnRxDone`; (b) document it as ESP32-fidelity-only; (c) delete scenario #12 (exact seeded CSMA
sequence) and keep only #11 (bounds/distribution) — or make CSMA deterministic by injecting the slot
value directly under `MC_TEST_HOOKS` rather than by seeding the PRNG; (d) mark the `RX_BUF_*` /
`CAD_ABORT_BY_RX` / `RX_RESTART_EARLY` rows in §1.1 as nRF52-only and drop `-D LORA_ISR_DEBUG` from
the Heltec test env.

---

### F4-7: 07 §5's on-device `pio test` is structurally blocked, not merely "under-used"

**Target:** `07-verification-infrastructure.md` §5 (lines 378–408), _"The most under-used capability
available."_
**Severity: medium-high** — it is step 7 of 10 in the build-out order and would consume the
maintainer's scarce board time before failing.

**The argument.** PlatformIO's Unity runner requires the test translation unit to define `setup()` and
`loop()`. `src/main.cpp:22` and `:52` define `setup()`/`loop()` unconditionally — there is **no
`#ifndef PIO_UNIT_TESTING` guard anywhere in the file**. So:

- With the PlatformIO default (`test_build_src = no`), `src/` is not compiled into the test image, and
  none of §5's listed targets — flash/NVS persistence, ADC/battery conversion, `scanI2C`, RadioLib init
  - `--lora` round-trip, `ONRXDONE_TIME` under load — are reachable. The environment builds and tests
    nothing.
- With `test_build_src = yes`, `src/main.cpp` and the test file both define `setup()`/`loop()` →
  duplicate symbol at link.
- Guarding `main.cpp` with `#ifndef PIO_UNIT_TESTING` fixes the link but removes the only call to
  `esp32setup()` (`src/main.cpp:48`). Every §5 target requires the board to be brought up. Calling
  `esp32setup()` from the test's `setup()` means running all 1,129 lines of board bring-up, which ends
  with the radio in RX, BLE advertising and WiFi up — at which point it is not a unit test, it is the
  firmware with an extra assertion printer, running concurrently with the very state machine under
  test.

None of this is mentioned. It is also a `src/main.cpp` change that would need upstreaming, which the
document's own framing (05 §4) treats as the binding constraint.

**Revised recommendation.** Demote `pio test` from "most under-used capability" to "requires a
`#ifndef PIO_UNIT_TESTING` guard in `src/main.cpp` plus an explicit decision about whether tests run
before or after `esp32setup()`". Until that decision is made, everything in §5 is better served by the
`--inject` + `bench_runner.py` path (07 §6), which needs no change to the firmware's entry point.

---

### F4-8: Phase 4 (board descriptor) would trade compile-time dead-code elimination for runtime dispatch on boards with 28 bytes of IRAM free — directly contradicting 03's own DRAM analysis

**Target:** `05-rewrite-vs-refactor.md` Phase 4 (lines 137–142), _"Application code reads fields
instead of branching at compile time"_; `01` highest-leverage item #2; `04` remediation item 9.
**Severity: high.**

**The argument.** 03 devotes a section to _"The DRAM problem is the one to worry about"_ (lines
160–174) and correctly identifies static memory as the binding constraint for the Arduino 3.x
migration. 05 then proposes converting compile-time board selection to runtime field reads without
once mentioning that this **removes the dead-code elimination the tight boards depend on**.

**Evidence — the headroom.** `docs/ram-comparison-20260517.md`:

- line 102–103: `ttgo_tbeam` `iram0_0_seg` at **99.98 %** — _"nur **28 Byte frei**"_.
- line 104: `E22-DevKitC` `dram0_0_seg` at **99.09 %** — _"nur **1.128 Byte frei**"_.

`src/configuration_global.h:102–105` already cuts `MAX_MHEARD` to 10 and `MAX_DEDUP_RING` to 10 on
those boards _"limited by DRAM"_, down from 80/100 on the roomy ones (`:95`, `:98`).

**Evidence — the descriptor already exists; what's missing is not a struct.** Classifying the 98
preprocessor conditions inside `esp32setup()` (`src/esp32/esp32_main.cpp:601–1730`): **51** branch on
`BOARD_*`, but **35** already branch on capability macros — `ENABLE_BMX280`, `ENABLE_BMP390`,
`ENABLE_AHT20`, `ENABLE_SHT21`, `ENABLE_MC811`, `ENABLE_BMX680`, `ENABLE_MCP23017`, `ENABLE_INA226`,
`ENABLE_RTC`, `ENABLE_SOFTSER`, `ENABLE_GPS`, `HAS_SDCARD`, `VEXT_CTRL`, `ADC_CTRL`, `BUTTON_PIN`,
`OneWire_GPIO`, `PMU_USE_WIRE1`, `FAN_CTRL`, `GPS_PPS_PIN`, `BAT_MAX_VOLTAGE`. The board descriptor
_is_ `variants/<board>/configuration.h`. The delta the doc proposes is not "add a descriptor" but
"move selection from compile time to run time" — and that is the part that costs flash and DRAM,
because every driver must then link into every image.

**Evidence — the maintainer's own hardware would hide the regression.** Same file, lines 111–121:
Heltec V3 has _"~213 kB DRAM frei, ~1,9 MB Flash frei"_. The one board available for testing has
comfortable headroom; the boards that would fail are `ttgo_tbeam` and `E22-DevKitC`, which cannot be
flashed and — with CI on tags only — are not even built on PR.

**Revised recommendation.** Reframe Phase 4. The achievable, cheap version is _"convert the remaining
51 `BOARD_*` conditions in `esp32setup()` into capability macros in `variants/<board>/configuration.h`,
matching the 35 that already are."_ That drains `BOARD_*` out of application code, keeps compile-time
elimination, is reviewable per-board, and can go upstream in slices. Runtime dispatch on a `const`
struct should be explicitly ruled **out** for anything that gates a driver, and any Phase-4 PR must
carry a `tools/ram_snapshot.py` diff for `ttgo_tbeam` and `E22-DevKitC`.

---

### F4-9: "A rewrite is structurally unmergeable" treats a self-imposed policy as a law of physics — and the same rule, applied honestly, kills Phases 3–5 of the doc's own plan

**Target:** `05-rewrite-vs-refactor.md` §4 (lines 68–77) and the "Upstream-able" column of the cost
table (lines 154–162).
**Severity: high** — this is the argument doing most of the work in the verdict.

**The argument.** The doc says: _"This repository's own rule (`CLAUDE.md`) is: sync upstream first,
cherry-pick the absolute minimum, no large refactors. A rewrite is structurally unmergeable under that
rule."_ Note the construction — the constraint cited is **this repository's own `CLAUDE.md`**, i.e. a
policy the maintainer wrote and can change. It is then used as though it were an upstream requirement.
No alternative is considered: not a vendor branch, not a long-lived `dev-harness` branch, not
upstreaming the harness alone, not simply **asking the upstream maintainer whether tests and CI would
be welcome**. That is a social question being answered by architectural fiat.

**Evidence — upstream demonstrably merges from this fork, and has 23 times.** `git log upstream/dev
--grep='from DK5EN'` → **23 merged PRs** (#789, #790, #791, #793, #802, #805, #806, #809, #816, …).
Sampled diffstats: 73/14, 7/14, 40/12 lines. So the "small PR" channel is real and open — which
_supports_ the strangle thesis on the mergeability axis. But upstream also merges its own commits of
1,706 and 1,150 insertions (largest in the last 200 on `upstream/dev`), so upstream is not allergic to
size; the ≤73-line ceiling is this fork's self-restraint, not upstream's rule.

**Evidence — the rule, applied honestly, also kills Phases 3–5.** `git rev-list --left-right --count
upstream/dev...v4.35p_prio` → **0 behind, 30 ahead**; `git diff --shortstat upstream/dev...v4.35p_prio`
→ **42 files changed, 8,027 insertions, 821 deletions**. The current local delta already exceeds every
individual phase the doc proposes, and the substantive fixes in it are **not upstream**: searching
`upstream/dev` for `atomic iWrite`, `plaintext auth bypass`, `SPSC memory ordering`, `NimBLE
server-only` returns **0 hits each**. Meanwhile the cost table marks Phase 3 (radio HAL, ~3,200 lines
touched) and Phase 4 (board descriptor, 30 boards) as _"Upstream-able: yes"_ and _"yes, per board
group"_. If an 8k-line delta of targeted bug fixes has not been upstreamed, a radio HAL will not be
either. The table's "upstream-able" column is aspiration presented as analysis.

**Revised recommendation.** Split the column into "would upstream plausibly take this?" (harness, CI,
capability-macro conversion, dead-arm deletion: yes — precedent exists at ≤100 lines/PR) and "requires
a negotiated agreement first" (radio HAL, board descriptor, global-bus retirement). Add a step 0 to
the plan: **open an issue upstream proposing CI-on-PR + a native test env, and see what comes back.**
It costs an hour and it determines whether Phases 3–5 are engineering or fan fiction.

---

### F4-10: The five documents end in five different "do this first" lists, and they contradict on the single most important item

**Target:** `02` §Suggested cleanups (lines 172–183); `03` §Recommended sequence (lines 197–214);
`04` §Ranked remediation (lines 202–218); `06` §Effort + First concrete step (lines 217–239);
`07` §10 Build-out order (lines 555–571).
**Severity: high** — with one maintainer and two boards, ordering _is_ the deliverable.

**Evidence — where CI-on-PR ranks in each document:**

| Document | Rank of "CI builds all envs on PR"                         |
| -------- | ---------------------------------------------------------- |
| 02       | **#1** of 7 (line 176)                                     |
| 03       | **#0**, before everything (line 203)                       |
| 06       | **Layer 0**, "do first, costs nothing" (l.65)              |
| 07       | **#9 of 10** (line 567) — _after_ the two-node bench at #8 |

07 puts the one mechanism that protects the 28 boards the maintainer cannot flash **behind** the
hardware bench. That inverts 02, 03 and 06 simultaneously.

**Further conflicts.**

- 04 says items 1–4 _"are safe, mechanical, and can go upstream as small PRs today"_ (line 216). Two of
  them (`peri_gps`, `scr_mrg`) touch `t5_epaper`, which is not in `default_envs` (02 line 22) and is
  not built by CI (02 B-07). "Safe today" is only true _after_ 02's item #1, not before it. See also
  F4-5, where I argue they should not be done at all.
- 03's step 0b ("RAM baseline across all 32 envs") presumes the ability to build 32 environments — which
  is 02's item #1 wearing a different hat. They are the same task, costed twice, in two documents,
  under two names.
- 07's step 7 (`pio test`, "1 board") is blocked (F4-7) and would consume the scarce board before step
  8 needs both.
- 06's "first five test targets" opens with `decodeAPRS`; 07's build-out puts the vector extractor at
  step 4, behind `randomSeed` and `--inject`. The vector corpus is the one asset that needs no
  hardware, no design decision, and no firmware change — it should be ahead of both.

**Revised recommendation.** Replace all five lists with the single path below, and make each document
link to it rather than restating its own.

---

## Steelman: the case for a rewrite

The strongest honest version, made as hard as I can:

**1. "The code is the spec" is not a reason not to rewrite — it is the definition of an unmaintainable
codebase, and repeating it forever guarantees the state persists.** 05 §1 lists five things that exist
only as code: the 218 command arms, the wire format, the CSMA timing, 30 boards' pin maps, and a
half-finished battery migration. Every one of those is an argument that _nobody currently knows what
the firmware does_. A codebase whose behaviour cannot be described independently of its implementation
cannot be safely modified either — which is exactly the situation 04 documents (four `#ifdef`-laden
functions over 1,000 lines each). "We cannot rewrite it because we do not know what it does" is a
strictly worse position than "we do not know what it does", and the strangler plan does not fix it: it
preserves undocumented behaviour by construction. The rewrite forces the specification to be written.

**2. The interop argument protects less than it appears to.** The frames that must stay bit-compatible
are produced by `encodeAPRS`/`PositionToAPRS` and consumed by `decodeAPRS`/`decodeAPRSPOS` — ~850
lines in `src/aprs_functions.cpp` plus the position encoder in `loop_functions.cpp`. That is ~1.2 % of
71k lines. Everything else — the scheduler, the command surface, the ring buffers, the display stacks —
has **no** on-air contract at all. The doc uses a 1.2 % constraint to veto changes to the other 98.8 %.

**3. A partial greenfield is not the same as a 1:1 port, and the doc never evaluates it.** 05 answers
"should we rewrite everything?" with "no", and treats that as answering "should we rewrite anything?".
The interesting proposal — _greenfield the packet engine (RX pipeline, dedup, routing, TX ring, CSMA)
as a platform-independent library with no Arduino dependency and no globals; keep drivers, displays,
sensors, board bring-up and the command surface exactly as they are_ — is never considered. It is
~4,000 lines (`lora_functions.cpp` 2,085 + the routing half of `OnRxDone` + `mheard_functions.cpp`
1,011 + `via_functions.cpp`), it is exactly the code that is 100 % testable on a desktop, it is exactly
the code that currently has nesting depth 13 and no test path, and it is exactly the code where the
audit findings in `src/code_review/code-audit-*.md` cluster. Crucially it is _additive_: the new engine
can be compiled alongside the old one and driven by the same injected frames until they agree on the
17 captured log sessions. That is a real before/after oracle, and it does **not** require touching the
24k lines of display code or the 12.6k lines of MCU bring-up.

**4. The board count is a choice, not a constraint.** 05 lists "the board matrix is cut to 3–5 boards"
as a hypothetical that would change the verdict (line 174) — and then never asks whether cutting it is
a good idea. One maintainer, two boards, no CI, 30 variants, and 17 boards on a battery implementation
that was supposed to be deleted. The essential-complexity claim (01's verdict table: _"~30 real boards
is essential"_) is asserted, not argued.

**5. The effort table is not evidence.** 05 lines 154–162 quote "~5–10 sessions", "~20–30 sessions"
with no basis, no unit definition, and no calibration against anything the project has actually done.
Against it sits a measured fact: 30 commits and 8,027 insertions of _targeted bug fixes_ over roughly
six months. A "session" that produces a radio HAL across two concurrency models is not the same unit as
a session that adds a CI trigger. Presenting them in one table with one unit makes a 3× difference look
like a 100× difference.

### My judgement: the steelman wins on points 1, 3 and 5; it loses overall

Points 2 and 4 are real but do not carry the conclusion. Point 3 — the partial greenfield of the packet
engine — is the strongest thing in this review that the concept does not contain, and **it should be
adopted**, but as the _destination_ of Phase 1, not as a replacement for it. Concretely: 05's Phase 1
("extract the portable core behind a thin seam") and my point 3 ("greenfield the packet engine as a
free-standing library") converge on the same first three PRs — get `aprs_functions`, the CSMA math and
`mheard_functions` compiling natively with no globals. They diverge only at the point where you decide
whether the new engine _replaces_ `OnRxDone` or merely _shadows_ it. That decision can be deferred
until the shadow implementation exists and the vectors agree, which is the honest way to make it.

The full rewrite still loses, but for one reason only, and it is not the one the doc leads with. It is
not "the code is the spec" (point 1 defeats that) and not "1:1 is unverifiable without 30 boards"
(point 3 routes around that). It is **F4-2**: the two supported radio stacks have different concurrency
models, the deployed nRF52 fleet's RF timing is a function of that model, and no rewrite — partial or
total — has an oracle for CAD/CSMA behaviour that does not require a two-node bench and months of
soak. Anything that changes when CAD runs is unverifiable at the maintainer's current scale. That
argument is available to the doc and the doc does not make it; it makes the weaker "no spec" argument
instead, which the steelman defeats.

---

## The avoided decision

**The concept never decides whether this repository is a downstream contributor or the maintained
line — and it silently assumes whichever answer is convenient to the paragraph it is in.**

When killing the rewrite, it is a contributor: _"A rewrite is structurally unmergeable under that
rule"_ (05:74). When proposing the plan, it is the maintained line: Phases 3–5 are a multi-hundred-
session structural programme that the same rule forbids, yet the cost table marks them _"Upstream-able:
yes"_ (05:160–162). Both cannot be true.

The evidence says the repository has already made the decision in practice and has not admitted it:
**0 commits behind upstream, 30 ahead, 8,027 insertions across 42 files**, containing atomic ring
indices, a plaintext-auth-bypass fix, an SPSC memory-ordering fix and NimBLE DRAM tuning — **none of
which appear in `upstream/dev`**. Meanwhile the 23 PRs that _were_ merged upstream are all ≤73 lines.
The fork is the maintained line for everything that matters to its own maintainer, and a contributor
only for small fixes.

This is the decision every other question depends on:

- If **contributor**: the plan is 02's B-07, 03's steps 0–3, and 06's Layers 0–2. Everything from
  Phase 3 onward is out of scope, permanently, and should be deleted from the document rather than
  costed.
- If **maintained line**: the minimal-diff rule in `CLAUDE.md` is obsolete and should be rewritten;
  the board matrix should be cut to what one person with two boards and CI can actually defend; and
  the partial greenfield of the packet engine becomes the correct centre of the plan.

The second-order avoided decision is the same one wearing a different hat: **nobody has decided which
of the 30 boards this repository is actually responsible for.** 07 §8 draws the coverage table honestly
— 2 × Heltec V3 covers "29 of 32 build environments: not covered" — and then declines to draw the
conclusion. A maintainer who cannot build, flash or test 28 boards is not maintaining 30 boards; he is
hoping about 28 of them. That is a scope decision, not an architecture problem, and no amount of test
infrastructure substitutes for making it.

---

## Proposed single critical path

One list. Replaces the five in 02, 03, 04, 06 and 07. Assumes: one maintainer, 2 × Heltec V3, limited
time. Each step is independently shippable and independently revertible. Steps 1–7 need **no hardware**.

**Step 0 — Decide, in writing, fork vs contributor, and which boards you are responsible for.**
No code. One paragraph in `CLAUDE.md`. Everything below is cheaper and clearer once it exists; steps
11+ are undefined without it. _(F4-9, "The avoided decision".)_

**Step 1 — Open an upstream issue proposing CI-on-PR and a native test environment.** One hour.
Precedent exists (23 merged DK5EN PRs). The answer determines whether steps 3–8 are contributions or
fork maintenance. Do not wait for the reply to proceed.

**Step 2 — CI: build all 32 environments on `pull_request` and `push`.** (02 B-07, 03 step 0, 06 Layer
0.) This is the only mechanism that protects the 28 boards you cannot flash, and it is a precondition
for every source change below. It is currently ranked #9 in 07 — that ranking is wrong.

**Step 3 — RAM/flash gate in CI.** `tools/ram_snapshot.py` as a CI artifact, with a hard threshold on
`iram0_0_seg` and `dram0_0_seg`. `ttgo_tbeam` has **28 bytes** of IRAM free and `E22-DevKitC` has
1,128 bytes of DRAM free (`docs/ram-comparison-20260517.md:102–104`); Heltec V3 has ~213 kB. Without
this gate, every refactor validated on your hardware is unvalidated on theirs. (Subsumes 03 step 0b.)

**Step 4 — Free wins, minutes each.** Pin `nordicnrf52 = 10.12.0` (02 B-04); `monitor_filters =
esp32_exception_decoder` for `heltec_wifi_lora_32_V3` (07 V-01); fix `-<tinyxml_function.cpp>` (02
B-03); single `[nrf52]` section in root `platformio.ini` (02 B-01).

**Step 5 — Native test environment + golden vectors.** (06 Layers 1–2, the best idea in the set.)
`tools/extract_vectors.py` over `tools/meshcom_monitor/*.log`; Unity assertions on `decodeAPRS` /
`decodeAPRSPOS` / round-trip. **Pin `meshcom_settings.node_call` per vector** — it is the one config
value `decodeAPRS` reads (`src/aprs_functions.cpp:417`), so a vector captured on DK5EN-98 will not
reproduce under another callsign. This is the oracle; nothing structural should precede it.

**Step 6 — Command-surface guardrail, no refactor.** `tools/check_commands.py` in CI: extract every
`commandCheck` literal, fail on duplicates and on shorter-verb-before-longer-verb. Then delete the
dead duplicate arm at `src/command_functions.cpp:3558` and audit `aprscomment`/`operatorname`. This is
the real protection the command-table refactor was supposed to buy, at ~1 % of the cost and 0 % of the
risk. _(F4-3.)_

**Step 7 — Extract the measured duplication.** Move the ~220 lines the clone detector actually finds
between the two loops — deferred display text, NTP/RTC date parse, position beacon, sensor polling
blocks — into `src/common_loop.cpp`, called by both. No radio abstraction, no RF risk, upstreamable as
3–4 PRs the size upstream already accepts. _(F4-1.)_

**Step 8 — `--inject <hex>` at the `checkRX` seam.** (07 H-01, respecified.) Substitute the
`radio.readData()` result under `MC_TEST_HOOKS` rather than calling `OnRxDone` directly, so the
`is_receiving` guard, RSSI/SNR capture, RX restart and channel-utilisation accounting all run.
Document it as ESP32-fidelity-only. Unlocks scenarios 3–10 on one board with no radio. _(F4-6.)_

**Step 9 — `bench_runner.py` with an exit code, plus H-03 `--dump ring/dedup` and H-05 counters.**
(07 §6, steps 5–6.) Turns steps 5 and 8 into pass/fail.

**Step 10 — Two-node wired bench,** for CAD/CSMA/retransmit only (07 §4, scenarios 11, 13–18). Drop
scenario 12 (exact seeded-CSMA sequence) — it is flaky by construction; keep bounds and distributions.
The wired-with-attenuator design is correct and I would not change it. Use the same session to
characterise the `USE_NEW_BATT` migration for `heltec_wifi_lora_32_V3` — one of the 17 old-path boards
you can actually measure (07 §8, 04 item 6).

**Step 11 — `commandAction` structural cleanup:** collapse the 7 chain segments into 1 and make the
11-flag continuation explicit. Only after step 6's guardrail and step 5's oracle exist. A dispatch
table, if ever, comes after this — not instead of it. _(F4-3.)_

**Step 12 — Drain `BOARD_*` from `esp32setup()` into capability macros** in
`variants/<board>/configuration.h`, matching the 35 that already are. **Not** runtime dispatch on a
`const` struct — that removes the dead-code elimination the tight boards need. Every PR carries a
`ram_snapshot` diff for `ttgo_tbeam` and `E22-DevKitC`. _(F4-8.)_

**Explicitly deferred, with a stated trigger:**

- **Radio interface / scheduler unification (05 Phase 3).** Trigger: a stable two-node bench with a
  recorded CAD/CSMA baseline, plus an upstream agreement. Not before. _(F4-2.)_
- **Merging `peri_gps`, `scr_mrg`, `power_controls` (04 items 1–3).** Recommend **never** as specified;
  they cost `#ifdef` density for zero binary benefit, and `power_controls` is vendored third-party
  code. _(F4-5.)_
- **Global-bus retirement (05 Phase 5).** Only as a by-product of step 7 and the packet-engine work.
- **On-device `pio test` (07 §5).** Trigger: a decision about `#ifndef PIO_UNIT_TESTING` in
  `src/main.cpp` and about whether tests run before or after `esp32setup()`. _(F4-7.)_

**One addition the concept does not contain, from the steelman:** as step 5 matures, build the packet
engine as a **shadow** implementation — a free-standing, Arduino-free library that consumes the same
injected frames and is asserted to agree with `OnRxDone` on all 17 captured sessions. It is additive,
it needs no upstream permission, it costs nothing if abandoned, and it is the only construct in this
whole review that would let a rewrite decision be made on evidence rather than on rhetoric.
