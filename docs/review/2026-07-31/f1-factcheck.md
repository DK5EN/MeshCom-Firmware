# F1 — Fact-check / claim verification of `docs/architecture/`

Baseline used: branch `v4.35p_prio`, HEAD `1ba101f4` (confirmed), 2026-07-31.
Every number below was re-derived from the repo / installed toolchain / upstream registries.
Commands shown inline.

**Score: 25 findings. 4 critical, 3 high, 6 medium, 12 low.**

---

## F1-1: Arduino-ESP32 2.0.17 is NOT what every official espressif32 release pins — 4 boards build against 2.0.14

- Doc + location: `03-dependencies.md` §"The headline" (lines 9–26), and `02-build-and-variants.md` B-04 last bullet
- Claim as written:
  > "Every release of PlatformIO's official `espressif32` platform — 6.5.0, 6.6.0, 6.13.0 **and 7.0.1** — declares `framework-arduinoespressif32: ~3.20017.0`."
  > "**So 'update the espressif32 platform from 6.6.0 to 7.0.1' buys nothing for this project.**"
  > and (02, B-04) "The caret pins are largely cosmetic: `^6.6.0` and `^6.13.0` both resolve to 6.13.0 today, and _every_ official `espressif32` release — including 7.0.1 — pins Arduino-ESP32 to `~3.20017.0` (= 2.0.17)."
- Verified reality: **false for 6.5.0 and 6.6.0.**
  ```
  $ for v in 6.5.0 6.6.0 6.13.0 7.0.1; do curl -s .../platform-espressif32/v$v/platform.json | ... ; done
  6.5.0  -> framework-arduinoespressif32 ~3.20014.0   framework-espidf ~3.50102.0
  6.6.0  -> framework-arduinoespressif32 ~3.20014.0   framework-espidf ~3.50201.0
  6.13.0 -> framework-arduinoespressif32 ~3.20017.0   framework-espidf ~3.50503.0
  7.0.1  -> framework-arduinoespressif32 ~3.20017.0   framework-espidf ~4.60001.0
  ```
  Cross-checked against the locally installed copy: `~/.platformio/platforms/espressif32@6.6.0/platform.json` → `arduino=~3.20014.0`.
  `~3.20014.0` = Arduino-ESP32 **2.0.14** (IDF 4.4.5), not 2.0.17 (IDF 4.4.7 — confirmed via
  `~/.platformio/packages/framework-arduinoespressif32/tools/sdk/versions.txt` → `esp-idf: v4.4.7`).

  The doc's "Verified locally" block only sampled 6.13.0 and 7.0.1 and generalised from two data points.

  Consequence the doc actively denies: the fleet **already runs two different Arduino cores**.
  `variants/t_deck/platformio.ini:4` and `variants/t_deck_plus/platformio.ini:4` pin `espressif32 @ 6.6.0`
  (exact, not caret) and `variants/t_deck_pro/platformio.ini:2`, `variants/t5_epaper/platformio.ini:2`
  pin `espressif32@6.5.0` → all four build on **Arduino 2.0.14 / IDF 4.4.5**, while every
  `^6.6.0` / `^6.13.0` board resolves to 6.13.0 → **Arduino 2.0.17 / IDF 4.4.7**.
- Severity: **critical** — the doc tells the reader that platform version bumps are cosmetic and that the
  application is uniformly on 2.0.17. Both are wrong for 4 boards, and one of them (`t_deck_pro`) is
  simultaneously the board pinned to the *older* RadioLib. A reader would skip an easy, real convergence win
  and would mis-attribute any T-Deck-family bug.
- Correction: "Official `espressif32` 6.13.0 and 7.0.1 pin Arduino-ESP32 `~3.20017.0` (2.0.17 / IDF 4.4.7);
  6.5.0 and 6.6.0 pin `~3.20014.0` (2.0.14 / IDF 4.4.5). Boards on the caret pins `^6.6.0`/`^6.13.0` resolve to
  6.13.0 and therefore run 2.0.17; `t_deck`, `t_deck_plus` (exact `6.6.0`) and `t_deck_pro`, `t5_epaper`
  (`6.5.0`) run 2.0.14. Bumping those four to 6.13.0 is a real, non-cosmetic Arduino-core update and should be
  step 1 of the dependency sequence."

---

## F1-2: "~3,200 lines of duplicated scheduling" is not what the clone data shows — it is ~270 lines

- Doc + location: `01-system-overview.md` §3 and Verdict item 1; `04-complexity-and-duplication.md` remediation #7; `05-rewrite-vs-refactor.md` Phase 3
- Claim as written:
  > "46 cloned 12-line windows between `esp32_main.cpp` and `nrf52_main.cpp` confirm it mechanically."
  > "one interface would collapse ~3,200 lines of duplicated scheduling into one implementation"
  > (04) "Extract a radio interface; unify `esp32loop`/`nrf52loop` — Deletes/unifies **~3,200 LOC**"
- Verified reality: 3,200 is simply `1947 + 1233` — the *sum of both function lengths*, not duplicated lines.
  Re-running the tool's own window index and merging the overlapping windows:
  ```
  esp32_main <-> nrf52_main: 46 shared window hashes
     src/esp32/esp32_main.cpp : 9 merged blocks, 268 source lines covered
     src/nrf52/nrf52_main.cpp : 9 merged blocks, 264 source lines covered
  ```
  and only 6 of those 9 blocks fall inside `esp32loop()` (1743–3689) / `nrf52loop()` (1102–2334);
  two blocks (`esp32_main:768–783`, `3907–3945`) are outside the loop entirely.
  A looser line-level measure (multiset intersection of whitespace/comment-normalised lines):
  ```
  esp32loop normalized lines: 909   nrf52loop: 597
  identical lines shared: 346  (58.0% of nrf52loop, 38.1% of esp32loop)
  ```
  Upper bound on deletable code from unifying the two schedulers is `min(1947, 1233) = 1,233` lines, since a
  unified scheduler still has to exist. Realistic mechanically-confirmed duplication: ~270–350 lines.
- Severity: **critical** — this is the doc's #1-ranked structural change in three separate documents, and its
  headline payoff is overstated by ~10×. The effort estimate (20–30 sessions, "biggest single win") is being
  justified by a number that does not mean what it is presented to mean.
- Correction: "Mechanically, the two schedulers share 46 cloned 12-line windows covering ~270 lines, and 58%
  of `nrf52loop()`'s normalised lines also appear in `esp32loop()`. Unifying them behind a radio interface
  can remove at most the smaller of the two (1,233 lines) and would realistically consolidate ~350–600 lines
  of verbatim-duplicated body. The stronger argument for the interface is not LOC deleted but that every
  feature must currently be ported by hand."

---

## F1-3: The "golden-vector corpus you already own" does not contain a single successfully-received frame

- Doc + location: `06-test-strategy.md` §"What already exists" item 1 and §Layer 2; `07-verification-infrastructure.md` §1.5 table row and §10 step 4; scenario catalogue rows 1–2
- Claim as written:
  > "**`tools/meshcom_monitor/*.log` — 17 captured sessions with raw frame hex.** Real on-air traffic,
  > timestamped, with the decoded interpretation alongside … **This is a golden-vector corpus you already
  > own.** Input bytes plus the expected decode, captured from the live network, including the awkward cases."
  > "Write `tools/extract_vectors.py`: parse `tools/meshcom_monitor/*.log`, pair each raw hex dump with the
  > `MH-LoRa:` decode line that follows it"
- Verified reality: the **only** hex dumps in all 17 logs are `[MC-DBG] CRC_PAYLOAD[255]:` lines, and they exist
  in only **3** of the 17 files:
  ```
  $ grep -rhoE "\[MC-DBG\] [A-Z_]+\[[0-9]+\]" tools/meshcom_monitor/*.log | sort -u
  [MC-DBG] CRC_PAYLOAD[255]
  $ grep -rc CRC_PAYLOAD tools/meshcom_monitor/*.log | grep -v ':0$'
  meshcom_2026-03-22_172422.log:670
  meshcom_2026-03-23_105629.log:109
  meshcom_2026-03-23_155228.log:1042
  ```
  `src/esp32/esp32_main.cpp:3811-3821` shows the dump is emitted **only inside the CRC-error branch**, gated by
  `bLORADEBUG`. By construction these are frames whose CRC *failed* — corrupted bytes plus 255-byte stale
  buffer tail. They never produce an `MH-LoRa:` decode line, so the proposed pairing has nothing to pair.

  Worse, the doc's own example is a splice of two unrelated records from **two different files**:
  - the `MH-LoRa: 062 @ x91A4354E …` line is `meshcom_2026-03-22_172424.log:9`, and the line immediately
    above it is `checkOwnTx:91A4354E own_msg_id:91A4354E` — i.e. it is the node's **own transmission**, not a
    received frame;
  - the hex `21 50 35 A4 91 91 44 4B 35 45 4E 2D 39 38 2C 44 4C 37 4F 53 58 2D 31 3E 2A 21 …` is the
    `CRC_PAYLOAD` dump at `meshcom_2026-03-22_172422.log:363` — a different frame from a different session.

  The sample `rx_<n>.json` in §Layer 2 mixes fields from both: `msg_fcs "0D21"` and `max_hop 2` come from the
  MH-LoRa line; `lat 48.4072 / lon 11.74 / alt 1657` decode from the CRC-failed hex (`4824.43N/01144.40E`,
  `/A=001657`); `msg_source_call "DL7OSX-1"` comes from the hex, `msg_source_path "DK5EN-98"` from neither
  cleanly.
- Severity: **critical** — Layer 2 is described as "the before/after oracle", the single deliverable that makes
  the whole rewrite-vs-refactor argument work, costed at 3–5 sessions and listed as needing no new work
  ("Layer 2's extractor runs against logs that are already in the repo"). As specified it cannot be built.
- Correction: "The 17 logs contain the decoded `MH-LoRa:` stream and the full `[MC-DBG]` event stream, but the
  only raw hex present is `CRC_PAYLOAD[255]` — dumps of CRC-failed frames, in 3 of 17 files. They are a good
  corpus for *malformed*-frame tests (scenario 3) and for event-sequence replay, but they contain no
  input-bytes/expected-decode pair for a valid frame. Building the golden-vector corpus requires first adding a
  raw-hex dump of *accepted* frames (a one-line `printfdeb` next to the existing `CRC_PAYLOAD` dump, behind
  `bLORADEBUG`) and then capturing a new session. Budget that capture step before Layer 2."

---

## F1-4: B-01 describes the wrong mechanism, understates the impact, and its proposed fix silently changes two boards

- Doc + location: `02-build-and-variants.md` B-01
- Claim as written:
  > "Because `extra_configs = variants/*/platformio.ini` merges all fragments into one namespace, the three
  > definitions collide and the effective content depends on glob order. … Whichever loses, two of the three
  > nRF52 boards inherit something other than what their file says."
  > "**Fix:** move `[nrf52]` into the root `platformio.ini` next to `[esp32]`, and drop the board-specific
  > `+<../variants/wiscore_rak4631/*>` line down into `[env:wiscore_rak4631]`."
- Verified reality: the three `[nrf52]` sections do exist (`variants/{wiscore_rak4631,heltec_t114,t_echo}/platformio.ini:1`),
  but ConfigParser **merges options across files** — it does not pick a winner. Since all three set identical
  `platform`/`framework`, glob order is irrelevant and the result is deterministic. Verified with
  `pio project config --json-output` (PlatformIO Core 6.1.18):
  ```
  ==== nrf52 (effective)
     platform = nordicnrf52 ; framework = arduino ; extends = upload_settings
     build_src_filter = +<*> … -<tinyxml_function.cpp> +<../variants/wiscore_rak4631/*>
     build_flags = …26 RADIOLIB_EXCLUDE… -D BOARD_RAK4630="RAK4630" -D MONITOR_SPEED=115200
                   -D SERIAL_BUFFER_SIZE=250 -Isrc/nrf52
  ```
  and the two "innocent" boards really do inherit all of it, because both use `${nrf52.build_flags}`:
  ```
  ==== env:heltec_t114  build_flags = [… '-D BOARD_RAK4630="RAK4630"', '-Isrc/nrf52', …, '-D BOARD_HELTEC_T114="T114"', …]
  ==== env:t_echo       build_flags = [… '-D BOARD_RAK4630="RAK4630"', '-Isrc/nrf52', …, '-D BOARD_T_ECHO="T_ECHO"', …]
  both: build_src_filter = […, '+<../variants/wiscore_rak4631/*>']
  ```
  So: (a) `heltec_t114` and `t_echo` compile with `BOARD_RAK4630` defined — a macro referenced 66 times in
  `src/` (`grep -c` on joined continuation lines) and used in `#elif` chains, e.g.
  `src/lora_functions.cpp:130,139,149,158` (`taskENTER_CRITICAL` selection); (b) both compile
  `variants/wiscore_rak4631/variant.cpp` (the directory contains `variant.cpp`, `variant.h`, `WVariant.h`);
  (c) all 26 `RADIOLIB_EXCLUDE_*` flags are applied twice.
  This also refutes 02's opening statement "Each environment sets exactly one `-D BOARD_<X>="<name>"` flag" —
  two environments set two.

  The proposed fix is therefore not a no-op cleanup: dropping `+<../variants/wiscore_rak4631/*>` into
  `[env:wiscore_rak4631]` removes `variant.cpp` from the `heltec_t114` and `t_echo` builds, and consolidating
  `[nrf52]` without preserving `-D BOARD_RAK4630` changes which `#if` arms those two boards take.
- Severity: **critical** — the doc presents a config-hygiene fix that would change the compiled output of two
  shipping boards, and it does not tell the reader that `BOARD_RAK4630` currently leaks into them.
- Correction: "`[nrf52]` is declared in all three nRF52 variant files. PlatformIO merges duplicate sections
  option-by-option, so the effective `[nrf52]` deterministically carries `wiscore_rak4631`'s
  `build_src_filter` and `build_flags`. Verified with `pio project config`: `heltec_t114` and `t_echo` are
  built with `-D BOARD_RAK4630="RAK4630"` in addition to their own board macro, with `-Isrc/nrf52`, and with
  `variants/wiscore_rak4631/variant.cpp` in their source filter. Consolidating `[nrf52]` into the root file is
  correct, but it is a **behaviour-changing** refactor for those two boards, not a cleanup: audit every
  `#if defined(BOARD_RAK4630)` site and RAM/flash-diff all three nRF52 targets before and after."

---

## F1-5: The `mheard` ↔ `ui_deckpro` "layering violation" points the wrong way, and is 54 lines not ~200

- Doc + location: `01-system-overview.md` §5 bullet 3; `04-complexity-and-duplication.md` duplication table + remediation item #4
- Claim as written:
  > "`mheard_functions.cpp` shares 19 cloned windows with `t-deck-pro/ui_deckpro.cpp` — neighbour-table logic
  > reimplemented inside a UI file."
  > (remediation #4) "Move mheard logic out of `ui_deckpro.cpp` back into `mheard_functions.cpp` — ~200 LOC"
- Verified reality: the shared region is **one** block, and it is LVGL rendering code:
  ```
  mheard <-> ui_deckpro: 19 shared window hashes
     src/mheard_functions.cpp   : 1 merged block, 54 lines  [819, 872]
     src/t-deck-pro/ui_deckpro.cpp: 1 merged block, 55 lines [1308, 1362]
  ```
  `src/mheard_functions.cpp:819-872` is inside `showMHeardTDECK()` (declared at `:801`) and is a sequence of
  `lv_table_set_cell_value(mheard_ta, …)` calls; the peer in `ui_deckpro.cpp` is inside `ui_mheard_disp()`
  (declared at `:1275`). `src/mheard_functions.h:16-19` guards `showMHeardTDECK()` / `showPathTDECK()` with
  `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`.
  So the violation is **T-Deck LVGL UI code living inside `mheard_functions.cpp`**, not mheard logic living
  inside a UI file. Remediation #4 as written would move code deeper into the wrong layer.
- Severity: **high** — an "S effort, low risk, ship upstream today" item that is described backwards and is
  ~4× smaller than costed.
- Correction: "`mheard_functions.cpp:819-872` (`showMHeardTDECK`) and `t-deck-pro/ui_deckpro.cpp:1308-1362`
  (`ui_mheard_disp`) share one ~54-line block of LVGL table construction. The layering violation is that
  `mheard_functions.cpp` contains T-Deck UI rendering (`showMHeardTDECK` / `showPathTDECK`, guarded by
  `BOARD_T_DECK`/`BOARD_T_DECK_PLUS`). Fix: move that rendering out of `mheard_functions.cpp` into the
  T-Deck UI layer and have both UIs call one shared table-builder. ~55 LOC unified."

---

## F1-6: `-Wall -Wextra` and the NimBLE buffer tuning are NOT applied to every board

- Doc + location: `03-dependencies.md` §NimBLE "Risks" bullet 1; `06-test-strategy.md` §Layer 0 last paragraph
- Claim as written:
  > (03) "`[esp32]` sets `CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=4` and `MAX_CONNECTIONS=1` — tight buffer tuning
  > that interacts directly with the 2.4.0 mbuf changes. Re-measure."
  > (06) "Add `-Werror` for a curated warning subset later, not immediately — `-Wall -Wextra` is already on and
  > the existing warning volume should be measured before it becomes blocking."
- Verified reality: those flags live in `[esp32].build_flags` (`platformio.ini:148-160`), and several
  environments override `build_flags` **without** interpolating `${esp32.build_flags}`. From the
  `pio project config --json-output` dump, per-env effective flags:
  ```
  envs WITHOUT -Wall -Wextra (9 of 34):
    heltec_t114, t5_epaper, t_deck_pro, t_echo, vision-master-e213,
    vision-master-e213-preview, vision-master-e290, wireless-paper, wiscore_rak4631
  envs WITHOUT CONFIG_BT_NIMBLE_* tuning (11 of 34):
    the 9 above + esp32-safeboot, esp32-S3-safeboot
  ```
  i.e. six *ESP32 firmware* environments (`t5_epaper`, `t_deck_pro`, `vision-master-e213`,
  `vision-master-e213-preview`, `vision-master-e290`, `wireless-paper`) run NimBLE with stock buffer
  configuration, not the tuned one, and compile without `-Wall -Wextra`.
- Severity: **high** — 03's NimBLE upgrade risk assessment and 06's Layer 0 advice are both derived from an
  assumption that is false for 6–9 environments. It also means an upgrade could behave differently on exactly
  the display-heavy boards flagged as highest risk.
- Correction: "`[esp32]` sets the NimBLE tuning and `-Wall -Wextra`, but `t5_epaper`, `t_deck_pro`,
  `vision-master-e213(-preview)`, `vision-master-e290` and `wireless-paper` replace `build_flags` without
  `${esp32.build_flags}` and therefore get neither; the three nRF52 envs get neither either. Before the NimBLE
  bump, either fold those envs back onto `${esp32.build_flags}` or measure them separately."

---

## F1-7: "218 commands" is a grep artifact; the cited ordering example does not exist; two arms really are dead

- Doc + location: `01-system-overview.md` layer table + Verdict item 3; `04-complexity-and-duplication.md` §`commandAction()` points 1–2; `05-rewrite-vs-refactor.md` §1 and Phase 2; `07-verification-infrastructure.md` §Guiding principle and §1.2
- Claim as written:
  > "218 `--command` arms" / "218 verbs" / "218 dispatch arms" / "218 small functions"
  > "Ordering is load-bearing: `--setinfo off` must be tested before `--setinfo`, and nothing enforces that."
- Verified reality:
  - `grep -c "commandCheck(" src/command_functions.cpp` → 218, but that counts **lines**, and one of them is
    the function definition at `:113`. Inside `commandAction()` (lines 194–5109):
    `230` `commandCheck(` calls, `217` top-level `if(commandCheck` arms, `227` distinct command literals.
  - There is **no bare `--setinfo` arm**. Only `"setinfo off"` (`:447`) and `"setinfo on"` (`:456`). The
    example is invented.
  - `commandCheck()` (`:113-124`) truncates the input to `strlen(command)` and compares — so it *is* a prefix
    match and ordering *is* load-bearing. The real cases the doc missed:
    ```
    2878: if(commandCheck(msg_text+2, (char*)"softser app")  == 0)
    2887: if(commandCheck(msg_text+2, (char*)"softser app0") == 0)   <-- unreachable
    3490: if(commandCheck(msg_text+2, (char*)"setowndns ")   == 0)
    3558: if(commandCheck(msg_text+2, (char*)"setowndns ")   == 0)   <-- unreachable duplicate
    ```
    (`operatorname ` and `aprscomment ` also appear twice but as legitimate inner re-checks at `:2225/:2227`
    and `:3203/:3205`.)
- Severity: **high** — the number is the headline of the single largest refactor proposal and appears in four
  documents; and the doc claims a hazard class exists while missing the two live instances of it.
- Correction: "`commandAction()` contains **217** top-level `if(commandCheck(...))` arms matching **227**
  distinct command literals. `commandCheck()` is a prefix match, so declaration order is load-bearing — and two
  arms are already unreachable because of it: `--softser app0` (`:2887`, shadowed by `--softser app` at
  `:2878`) and the second `--setowndns` (`:3558`, duplicate of `:3490`). A dispatch table would make both a
  compile-time or startup-assert error."

---

## F1-8: "423 externs" is wrong by ~25%

- Doc + location: `01-system-overview.md` §1 and §4/§"Practical consequences"; `04-complexity-and-duplication.md` §`OnRxDone()`
- Claim as written: "declares **260 externs**; 423 across all of `src/`" · "Only 12 of 423 globals are `std::atomic`" · "only 12 of the 423 globals are atomic"
- Verified reality:
  ```
  $ grep -c "^[[:space:]]*extern " src/loop_functions_extern.h            -> 260   (255 variables + 5 functions)
  $ grep -rh "^[[:space:]]*extern " --include=*.h --include=*.hpp --include=*.cpp --include=*.c src/ | grep -v "(" | wc -l
                                                                          -> 530   (variable declarations)
  $ ... | sed -E 's/;.*//;s/\[.*//' | awk '{print $NF}' | sort -u | wc -l -> 441   (unique symbol names)
  ```
  Second-largest header: `src/t-deck/lv_obj_functions_extern.h` (63), then `src/nrf52/WisBlock-API.h` (48).
  The type table is otherwise accurate (bool 87, int 37, float 18, String 12, char 11, uint8_t 10, double 10,
  `std::atomic<*>` 12 ✓); `unsigned` is 42 not 43, "others" is 16 not ~13, and the column sums to 255
  (variables), not 260.
- Severity: medium — 423 is quoted three times as the denominator of the atomicity argument.
- Correction: "`src/loop_functions_extern.h` declares 260 `extern`s (255 variables, 5 functions). Across all of
  `src/` there are 530 extern variable declarations naming 441 distinct symbols. 12 of them are `std::atomic`."

---

## F1-9: The `CC` metric does not count `&&` or `||`, contrary to its stated definition

- Doc + location: `04-complexity-and-duplication.md` preamble (line 8)
- Claim as written: "`CC` below is a **heuristic decision count** (`if`/`for`/`while`/`case`/`catch`/`&&`/`||`/`?` plus one)"
- Verified reality: `tools/arch_metrics.py:18` is
  `DECISION = re.compile(r"\b(if|for|while|case|catch|&&|\|\||\?)\b|\?")`.
  The `\b…\b` wrapper can never match `&&` or `||` (no word characters adjacent). Empirically:
  ```
  'a && b || c;' -> []            count = 0
  'if(x) y;'     -> ['if']        count = 1
  'x = a ? b : c;' -> ['']        count = 1   (matched by the trailing |\? alternative)
  ```
  So `CC = 1 + count(if|for|while|case|catch) + count(?)`. Boolean-operator complexity is invisible, which
  systematically under-ranks condition-heavy parsers (`decodeAPRS`, `webSetup_setParam`) relative to
  `switch`-heavy ones.
- Severity: medium — the doc explicitly instructs the reader to "Rank by `LOC × CC × NEST`", and the CC input
  to that ranking is not what the doc says it is.
- Correction: "`CC` is `1 + count of if/for/while/case/catch + count of ?`. The regex in `arch_metrics.py`
  cannot match `&&`/`||`, so short-circuit operators are **not** counted; treat CC as a branch-statement count,
  not a boolean-complexity count." (Or fix the regex to `r"\b(if|for|while|case|catch)\b|&&|\|\||\?"`.)

---

## F1-10: The board-macro table's "Conditional sites" column is not conditional sites, and the ranking is wrong

- Doc + location: `02-build-and-variants.md` §"Board macro inventory" table
- Claim as written: column header "Conditional sites"; `BOARD_T_DECK_PRO` 61 (ranked 5th, above `BOARD_T5_EPAPER` 52); `BOARD_TLORA_OLV216` 15; `BOARD_RAK4630` 64; `BOARD_E22_S3` 17; `BOARD_TBEAM_V3` 19
- Verified reality: counting occurrences **inside preprocessor conditionals** (joining `\`-continuations, all
  of `src/` minus asset dirs):
  ```
  102 BOARD_T_ECHO    83 BOARD_T_DECK       83 BOARD_T_DECK_PLUS   66 BOARD_RAK4630
   52 BOARD_T5_EPAPER 51 BOARD_T_DECK_PRO   47 BOARD_HELTEC_T114   45 BOARD_T_CONNECT_PRO
   31 BOARD_WIRELESS_PAPER 30 BOARD_TRACKER 28 BOARD_E290          23 BOARD_E213
   21 BOARD_TBEAM_1W  21 BOARD_TBEAM_V3     20 BOARD_HELTEC_V4     19 BOARD_E22_S3
   14 BOARD_STICK_V3  12 BOARD_TLORA_OLV216 12 BOARD_HELTEC_V3     11 BOARD_T3S3_V13 / BOARD_T_ETH_ELITE
  ```
  The doc's numbers are reproducible only as a **mix of two different greps**: `BOARD_T_DECK_PRO 61`,
  `BOARD_E290 29`, `BOARD_TBEAM_V3 19`, `BOARD_E22_S3 17` are `grep -rho "BOARD_X" src/ | wc -l` (total
  occurrences anywhere, including `#define`s and comments), while `BOARD_RAK4630 64` and
  `BOARD_TLORA_OLV216 15` are `grep -rn … | grep -c '#'` (lines containing `#`). The `1,131` total is likewise
  not reproducible; the actual figure is **1,148** occurrences across `.cpp/.c/.h/.hpp` with asset dirs excluded.
  Net effect on the "ranked by how often application code has to branch" claim: `BOARD_T_DECK_PRO` is 6th
  (51), not 5th (61) — it sits *below* `BOARD_T5_EPAPER`.
- Severity: medium — the table is explicitly sold as a ranking of where `#ifdef` pressure lives.
- Correction: replace the column values with the conditional-site counts above, label the total "1,148
  `BOARD_*` references across `src/`", and state the exact command used.

---

## F1-11: "24 contributors" does not match any measurement

- Doc + location: `02-build-and-variants.md` B-07; `05-rewrite-vs-refactor.md` §4; `06-test-strategy.md` §Current state
- Claim as written: "~24 contributors" / "935 commits in the last 12 months, 24 contributors"
- Verified reality:
  ```
  $ git log --since="2025-07-30" --oneline | wc -l                 -> 935      (commits ✓)
  $ git log --since="2025-07-30" --format='%an' | sort -u | wc -l  -> 16
  $ git log --since="2025-07-30" --format='%ae' | sort -u | wc -l  -> 15
  $ git log --format='%an' | sort -u | wc -l                       -> 22  (all time)
  $ git log --format='%ae' | sort -u | wc -l                       -> 20  (all time)
  $ gh api "repos/icssw-org/MeshCom-Firmware/contributors?per_page=100" --jq length -> 14
  ```
- Severity: medium — used to size the "who can break what" argument in three documents.
- Correction: "935 commits in the last 12 months from 16 distinct authors (22 all-time; GitHub lists 14
  contributors on `icssw-org/MeshCom-Firmware`)."

---

## F1-12: Several size-at-a-glance numbers are off; `variants/` has 31 dirs, MCU layers are ~14k not 12.6k

- Doc + location: `README.md` §"Size at a glance"; `05-rewrite-vs-refactor.md` §2 ("12.6k lines of MCU bring-up")
- Claim as written: own firmware code ~71,100; MCU layers (`src/esp32/`, `src/nrf52/`) ~12,600; board variants 30 dirs
- Verified reality (all files under `src/`, excluding `Fonts/`, `GFX_Root/`, `*/maps/`, `Font_*`, `img_*`, `firasans*`):
  ```
  TOTAL         73,875   (73,215 excluding the 660 lines of src/code_review/*.md)
  <root>        27,258   (doc 27,200 ✓)
  nrf52          7,608 + esp32 6,381 = 13,989   (doc 12,600 ✗ — 11% low)
  t-deck 7,458 + t-deck-pro 7,528 + t5-epaper 5,667 + Displays 3,680 = 24,333  (doc 24,100 ✓)
  web_functions  3,258   (doc 3,250 ✓)
  safeboot       3,220   (doc 3,220 ✓ exact)
  Platforms      1,157   (doc 1,130 ✓)
  lib/       1,446,276   (doc ~1.4M ✓)
  $ ls -d variants/*/ | wc -l  -> 31           (doc "30 dirs" ✗)
  ```
  The doc's own buckets sum to 71,500, which already exceeds its stated 71,100 total.
- Severity: medium — 12.6k is reused in 05's cost argument; the 30/31 variant-dir count feeds the "30 boards"
  framing everywhere.
- Correction: own firmware code ~73,200; MCU layers ~14,000; `variants/` 31 directories (30 board
  `configuration.h` files — `variants/t5_epaper/` has none, see F1-24).

---

## F1-13: B-03 undercounts dead `build_src_filter` exclusions — there are 7 distinct patterns, not 3

- Doc + location: `02-build-and-variants.md` B-03
- Claim as written: "three `build_src_filter` exclusions target files that do not exist" (`-<tinyxml_function.cpp>`, `-<esp32/esp32_gps.cpp>`, `-<gps_l76k.cpp>`)
- Verified reality: expanding every `-<…>` token in the effective `pio project config` against the filesystem
  (`src/` is the filter root):
  ```
  -<lvgl/*>              esp32 base + 21 envs   (src/lvgl does not exist)
  -<lib/lvgl/*>          vision-master-e213, -preview, vision-master-e290, wireless-paper
  -<SDWrapper/*>         t5_epaper              (src/SDWrapper does not exist)
  -<U8g2/*>              t_deck_pro
  -<tinyxml_function.cpp> nrf52 base + 3 envs   (actual: src/tinyxml_functions.cpp)
  -<gps_l76k.cpp>        t_deck_pro
  -<esp32/esp32_gps.cpp> t_deck_pro
  ```
  Also: the guard in `src/tinyxml_functions.cpp` is lines **7–244** (`#endif` is the last line of a 244-line
  file), not "7–245"; and `ENABLE_XML` is defined only in `variants/E22_XML-DevKitC/configuration.h:9` ✓.
- Severity: medium — B-03 is presented as an exhaustive audit of dead filter config.
- Correction: "Seven distinct `build_src_filter` exclusion patterns match nothing: `-<lvgl/*>`,
  `-<lib/lvgl/*>`, `-<SDWrapper/*>`, `-<U8g2/*>`, `-<tinyxml_function.cpp>`, `-<gps_l76k.cpp>`,
  `-<esp32/esp32_gps.cpp>`. All are harmless today; all read as protection that does not exist."

---

## F1-14: `[esp32]` uses the deprecated `src_filter` key, not `build_src_filter`

- Doc + location: `02-build-and-variants.md` §"Source filtering"
- Claim as written: "`build_src_filter` per platform excludes whole subtrees: `[esp32] -<nrf52/*> …`"
- Verified reality: `platformio.ini:136` is `src_filter = `, not `build_src_filter`. Confirmed in the effective
  config: `env:heltec_wifi_lora_32_V3` has `src_filter = [...]` and `build_src_filter = None`. PlatformIO 6
  still honours the old name (it is a registered `oldname`), so behaviour is correct today, but the key is
  deprecated and a future core could drop it — silently compiling `nrf52/`, `t-deck/`, `safeboot/` into every
  ESP32 image. The exclusion **list** quoted in the doc is accurate.
- Severity: low (behaviourally), but worth stating because it is a latent break.
- Correction: "`[esp32]` still uses the deprecated `src_filter` key (`platformio.ini:136`); `[nrf52]` and the
  per-board overrides use the current `build_src_filter`. Rename `[esp32]`'s for consistency and future-proofing."

---

## F1-15: `[common]` has 26 `RADIOLIB_EXCLUDE_*` flags (24 unique), not 22

- Doc + location: `02-build-and-variants.md` §"Shared sections" table
- Claim as written: "`[common]` | root | 22 `RADIOLIB_EXCLUDE_*` build flags"
- Verified reality: `platformio.ini:91-116` = 26 flags; `AFSK` (lines 98, 111) and `RFM2X` (97, 115) are each
  listed twice → 24 unique.
- Severity: low
- Correction: "26 `RADIOLIB_EXCLUDE_*` flags (24 unique — `AFSK` and `RFM2X` are duplicated)."

---

## F1-16: The numeric hardware-type registry has 34 entries, not 38

- Doc + location: `02-build-and-variants.md` §"Board macro inventory", last paragraph
- Claim as written: "`configuration_global.h` … (`TLORA_V2 1` … `ESP32_LORAPRS_RA01 60`, 38 entries)"
- Verified reality: `src/configuration_global.h:8-41` — 34 `#define`s. IDs run 1–12 then jump to 39–60 (13–38
  are unused/retired), which is probably where "38" came from.
- Severity: low
- Correction: "34 entries with IDs 1–12 and 39–60."

---

## F1-17: `lib_ignore` spelling split is 19/7, not 20/6

- Doc + location: `02-build-and-variants.md` B-02
- Claim as written: "The variants spell it `SensorLibTDECKpro` (20 files) or `SensorLibTDECKPro` (6 files)"
- Verified reality:
  ```
  $ grep -rln "SensorLibTDECKpro" variants/*/platformio.ini | wc -l -> 19
  $ grep -rln "SensorLibTDECKPro" variants/*/platformio.ini | wc -l ->  7
  $ grep -rln "SensorLibTDECK"    variants/*/platformio.ini | wc -l -> 26   (total ✓, no overlap)
  ```
  Everything else in B-02 checks out: `lib/SensorLibTDECkpro/library.json` declares `"name": "SensorLib"` ✓,
  the directory is `SensorLibTDECkpro` (lowercase `k`) ✓, `XPowerLib` typo at
  `variants/ttgo-lora32-v21/platformio.ini:16` ✓, `lewisxhe/SensorLib` name collision ✓.
- Severity: low
- Correction: "(19 files) or `SensorLibTDECKPro` (7 files)".

---

## F1-18: V-01 — 11 other variants have the exception decoder, not 12

- Doc + location: `07-verification-infrastructure.md` §9, V-01
- Claim as written: "`heltec_wifi_lora_32_V3` has **no `monitor_filters = esp32_exception_decoder`**, while 12 other variants do."
- Verified reality: `grep -rn "monitor_filters" variants/*/platformio.ini` → 11 files
  (E22_1262_S3, E22_1268_S3, LilyGo_T3_S3_V1_3, LilyGo_T-Beam-1W, t_deck_pro, t5_epaper, T-ETH-ELITE_1262,
  ttgo_tbeam_supreme, vision-master-e213, wireless-paper, vision-master-e290). The root `platformio.ini:169,207`
  adds it for the two safeboot envs → 13 environments total. Neither count is 12. The finding itself is valid.
- Severity: low
- Correction: "while 11 other variants (13 environments including the two safeboot images) do."

---

## F1-19: §8 coverage arithmetic — 31 of 32 environments are uncovered, not 29

- Doc + location: `07-verification-infrastructure.md` §8 table, last "Not covered" row
- Claim as written: "SF/BW/CR/power parameter handling | 29 of 32 build environments"
- Verified reality: 32 environments are in `default_envs`; a 2 × Heltec V3 bench exercises exactly one of them
  (`heltec_wifi_lora_32_V3`), so 31 are uncovered. (29 is the count of *ESP32 firmware* environments —
  34 declared − 3 nRF52 − 2 safeboot — which is the correct figure for `01`'s "29 build environments" claim
  about `esp32setup()`, but not for this row.)
- Severity: low
- Correction: "31 of 32 build environments."

---

## F1-20: Three stale line/length references

- Doc + location: `07-verification-infrastructure.md` §4.2; `02-build-and-variants.md` B-03; `01-system-overview.md` layer table
- Claims as written: "(`src/command_functions.cpp:3994`)" · "wraps its entire body (lines 7–245)" · "`src/main.cpp` (70 lines)"
- Verified reality:
  - `grep -n "430.0 + dec_bandwith" src/command_functions.cpp` → **4000** (the `txfreq ` arm starts at 3993);
    the formula itself (`430.0 + BW/200 … 439.000 − BW/200`, `869.4 … 869.65`) is exactly right.
  - `wc -l src/tinyxml_functions.cpp` → **244**; guard is `#if defined(ENABLE_XML)` at :7, `#endif` at :244.
  - `wc -l src/main.cpp` → **69**.
- Severity: low
- Correction: `:4000`; "lines 7–244"; "69 lines".

---

## F1-21: "one mutex" undercounts the synchronisation primitives in the tree

- Doc + location: `01-system-overview.md` §1, "Practical consequences" bullet 3
- Claim as written: "there is one `portMUX_TYPE` (`displayMux`), one mutex (`net_console.cpp`), one queue (`bleQueue`, 5 slots)"
- Verified reality: `portMUX_TYPE` ✓ exactly one (`src/lora_functions.cpp:122`). `bleQueue` ✓
  (`src/esp32/esp32_main.cpp:277`, `xQueueCreate(5, …)` at `:1578`). But semaphores:
  ```
  src/net_console.cpp:275,284   xSemaphoreCreateMutex()      (two call sites, one handle)
  src/t-deck/tdeck_main.cpp:111 xSemaphoreCreateBinary()     (TFT access)
  src/esp32/esp32_audio.cpp:40  xSemaphoreCreateBinary()     (audio state)
  src/nrf52/nrf52_main.cpp:407  xSemaphoreCreateBinary()     (g_task_sem)
  ```
- Severity: low — the argument survives, the inventory does not.
- Correction: "one `portMUX_TYPE` (`displayMux`), one true mutex (`net_console.cpp`), three binary semaphores
  (T-Deck TFT, ESP32 audio, nRF52 task signal), one queue (`bleQueue`, 5 slots)."

---

## F1-22: `Platforms/*/power_controls.cpp` — "three near-copies" overstates E290

- Doc + location: `04-complexity-and-duplication.md` duplication table, row 4; remediation #3
- Claim as written: "`Platforms/VisionMasterE213/power_controls.cpp` ↔ `WirelessPaper/…` | 93 / 108 | 49 | **Three near-copies (E213, E290, WirelessPaper).**" / remediation "Merge the three … ~140 LOC"
- Verified reality: `wc -l` → E213 93 ✓, WirelessPaper 108 ✓, **E290 40**. Clone-window counts:
  E213↔WirelessPaper 30, E213↔E290 7, E290↔WirelessPaper 7. `diff -w -B` E213↔WirelessPaper = 47 differing
  lines (doc says 49 — within method noise); E213↔E290 = 59 differing out of 93/40.
  So it is one strong near-copy pair plus a much smaller third file.
- Severity: low
- Correction: "E213 (93) and WirelessPaper (108) are near-copies (30 cloned windows, 47 differing lines); E290
  (40 lines) shares only a 7-window head. Merging saves ~90 LOC, not ~140."

---

## F1-23: "Boards that need a subtree re-add it" — three of the five named boards do not

- Doc + location: `02-build-and-variants.md` §"Source filtering", last paragraph
- Claim as written: "Boards that need a subtree re-add it (`t_deck`, `t_deck_pro`, `t5_epaper`, `vision-master-*`, `wireless-paper`)."
- Verified reality: only four `+<subtree/*>` re-adds exist:
  ```
  t_deck_pro:15  +<t-deck-pro/*>    t_deck:11  +<t-deck/*>
  t_deck_plus:18 +<t-deck/*>        t5_epaper:36 +<t5-epaper/*>
  ```
  `vision-master-e213/e290` and `wireless-paper` instead **replace** the whole `build_src_filter` with one that
  never excludes `Displays/`, `Fonts/`, `GFX_Root/`, `Platforms/` in the first place (see their
  `;use Displays, Fonts, GFX_Root, Platforms` comments). `t_deck_plus` — which does re-add — is not in the doc's list.
- Severity: low
- Correction: "`t_deck`, `t_deck_plus`, `t_deck_pro` and `t5_epaper` re-add their subtree with `+<…>`;
  `vision-master-e213/e290` and `wireless-paper` replace the inherited filter wholesale."

---

## F1-24: `variants/t5_epaper/` has no `configuration.h`, and t5_epaper is absent from the `USE_NEW_BATT` split

- Doc + location: `02-build-and-variants.md` §Mechanism diagram; `04-complexity-and-duplication.md` §`batt_function_old.cpp` table
- Claim as written: "`variants/<board>/configuration.h` — pins, `BOARD_*` macro, hardware flags" (presented as universal); and the 13-new / 17-old table
- Verified reality: `find . -name configuration.h -not -path './lib/*'` → 30 files for 31 variant directories;
  the missing one is `variants/t5_epaper/`, whose `platformio.ini:38` still sets `-I variants/${this.__env__}`.
  Correspondingly the `USE_NEW_BATT` table covers 13 + 17 = 30 boards and omits `t5_epaper` entirely (and
  `vision-master-e213-preview`).
  **The 13/17 split itself is correct** — verified exhaustively:
  ```
  $ grep -rn "USE_NEW_BATT" variants/
  active #define  : E22-DevKitC, E22_XML-DevKitC, E22_1262-DevKitC, E22_1262_S3, E22_1268_S3,
                    esp32-loraprs-e22, LilyGo_T-Beam-1W, LilyGo_T3_S3_V1_3, t_deck, t_deck_plus,
                    ttgo-lora32-v21, vision-master-e213, wireless-paper                 = 13 ✓
  commented out   : vision-master-e290, esp32-loraprs-ra01                              =  2 ✓
  ```
  and `heltec_wifi_lora_32_V3/configuration.h` has no `USE_NEW_BATT` ✓ (so 07 §8's "V3 is one of the 17" holds).
  Guards ✓: `src/batt_function_old.cpp:5` `#ifndef USE_NEW_BATT`, `src/batt_functions.cpp:13` `#if defined(USE_NEW_BATT)`.
- Severity: low (the decision-relevant 13/17 split is right; the note is that t5_epaper is unaccounted and
  probably cannot build as configured, which is consistent with it being commented out of `default_envs`)
- Correction: add a footnote: "`variants/t5_epaper/` ships no `configuration.h` and is therefore not in either
  column; it is also commented out of `default_envs`."

---

## F1-25: NimBLE "Fourteen releases" → thirteen

- Doc + location: `03-dependencies.md` §NimBLE-Arduino 2.2.3 → 2.5.1
- Claim as written: "3 minors, ~14 releases" / "Fourteen releases."
- Verified reality: `gh`/GitHub releases between 2.2.3 (2025-02-28, exclusive) and 2.5.1 (2026-07-30,
  inclusive): 2.3.0, 2.3.1, 2.3.2, 2.3.3, 2.3.4, 2.3.5, 2.3.6, 2.3.7, 2.3.8, 2.3.9, 2.4.0, 2.5.0, 2.5.1 = **13**.
- Severity: low
- Correction: "3 minors, 13 releases."

---

# Claims verified as CORRECT

Do not re-check these.

**Baseline / repo facts**
- HEAD `1ba101f4` on `v4.35p_prio`; `SOURCE_VERSION "4.35"` + `SOURCE_VERSION_SUB "p"`, `FLASH_VERSION 20260712` (`src/configuration_global.h:1-5`).
- 935 commits in the last 12 months.
- 34 environments declared, 32 in `default_envs` (= 30 firmware + 2 safeboot); `t5_epaper` commented out at `platformio.ini:20`; `vision-master-e213-preview` declared (`variants/vision-master-e213/platformio.ini:54`) but not in `default_envs`.
- 29 ESP32 firmware environments (34 − 3 nRF52 − 2 safeboot) — 01's "29 build environments / 29 different programs" is right.
- `lib/` ≈ 1.45 M lines. `src/<root>` 27,258; display/UI 24,333; `web_functions` 3,258; `safeboot` 3,220; `Platforms` 1,157.

**`arch_metrics.py` output (re-ran; parser audited)**
- 1,032 functions parsed; >100 LOC 81 (7.8%), >200 36 (3.5%), >400 11 (1.1%), >800 6 (0.6%).
- All six headline functions verified against the source, start **and** end line:
  `commandAction` 194–5109 (4,916) · `esp32loop` 1743–3689 (1,947) · `nrf52loop` 1102–2334 (1,233) ·
  `setDisplayLayout` 448–1628 (1,181) · `esp32setup` 601–1729 (1,129) · `OnRxDone` 288–1255 (968).
  Runners-up also correct: `nrf52setup` 698, `webSetup_setParam` 597, `decodeAPRSPOS` 466, `sendDisplayText` 466,
  `getUDP` 419, `readPhoneCommand` 395, `decodeAPRS` 378, `sendDisplay1306` 321, `sendDisplayPosition` 312,
  `create9` 364/CC 8, `create0` 166/CC 8.
- Parser sanity audit: 0 overlapping function extents, 0 false positives in headers, 16 entries ≤2 LOC. The
  `#`-line-skipping brace tracker did **not** mis-parse anything in this tree. Only the CC *definition* is wrong (F1-9).
- `esp32setup()` PP = 107 ✓. Preprocessor-density table (counting `#if/#ifdef/#ifndef/#else/#elif/#endif`):
  esp32_main 404, loop_functions 294, command_functions 206, nrf52_main 114, lora_functions 110,
  batt_function_old 104, gps_functions 103 — all ✓.
- 32 distinct `BOARD_*` macros appear inside preprocessor conditionals ✓.

**`arch_duplication.py` output**
- 393 duplicated 12-line windows, 119 files ✓. Top pairs reproduce exactly: peri_gps 94, ui_scr_mrg/scr_mrg 89,
  esp32_main/nrf52_main 46, E213/WirelessPaper power_controls 30, nrf_eth/udp 19, mheard/ui_deckpro 19,
  ui_deckpro/t5 ui 18, loop_functions self 14.
- File sizes in the duplication table ✓: 417/379, 267/275, 3,980/2,697, 93/108, 1,019/1,110.
- `loop_functions.cpp` self-duplication at 1291–1317 / 1400–1426 / 2549–2575 ✓ (3× ~27 lines).
- `scr_mrg` differing lines 29 ✓ ("~90% identical"). `peri_gps` 94 by `diff -w -B` (doc says 96 — method noise).
- Caveat the doc already states correctly: the pair counts are occurrence-pair counts over *overlapping*
  windows and must be treated as a ranking signal.

**Dependencies (all re-queried live)**
- Latest versions, PlatformIO registry: RadioLib **7.7.1**, NimBLE-Arduino **2.5.1**, ArduinoJson **7.4.3**,
  SX126x-Arduino **2.0.32**, AsyncTCP **3.3.2**, ESPAsyncWebServer **3.6.0**, OneButton **2.6.2**,
  TinyGPSPlus **1.1.0**, U8g2 **2.36.18**, SensorLib **0.4.1**, ESP32Ping **1.7**, XPowersLib **0.3.3**,
  GxEPD2 **1.6.9**, TFT_eSPI **2.5.43**, LVGL **9.5.0** — every "Latest" cell in 03 is correct.
- Pins in the repo ✓: `[esp32libs]` RadioLib 7.6.0 + NimBLE 2.2.3; `t_deck_pro`/`t5_epaper` RadioLib 7.1.2 and
  ArduinoJson ^7.4.1; root ArduinoJson ^7.4.3; safeboot AsyncTCP 3.2.14 + ESPAsyncWebServer 3.3.23.
- Vendored `lib/` versions ✓ **all exact**: lvgl 8.3.11, TFT_eSPI 2.5.22, GxEPD2 1.5.5, XPowersLib 0.2.4,
  SensorLibTDECkpro 0.2.1 (`"name": "SensorLib"`), epdiy 2.0.0, ESP32-audioI2S 2.1.0, TinyGSM 0.11.7,
  AceButton 1.3.3, Adafruit TCA8418 1.0.2.
- `nordicnrf52` is unpinned in all three nRF52 fragments ✓; registry latest **10.12.0** (2026-06-25) with
  Adafruit core `~1.10700.0` = 1.7.0 ✓. The "developer with an older local install" hazard is real on this very
  machine: `~/.platformio/platforms/nordicnrf52@10.3.0` (Adafruit core 1.6.0) is installed alongside 10.12.0.
- tasmota `platform-espressif32` latest **2026.05.50** ✓; the pinned 2026.02.30 resolves to
  arduino-esp32 **v3.1.10** + esp-idf **v5.3.4** ✓ (from the installed `espressif32@src-663ed35…/platform.json`).
- pioarduino latest **55.03.311**, released **2026-07-24** ✓.
- Arduino core 2.0.17 → IDF **4.4.7** ✓ (`framework-arduinoespressif32/tools/sdk/versions.txt`); installed
  package version `3.20017.241212+sha.dcc1105b` ✓.
- RadioLib changelog claims ✓ all four: 7.6.0 removed `getDataRate()`; 7.7.0 introduced `ConfigLoRa_t`/`ConfigFSK_t`
  `begin()` with the old form deprecated and removal announced for 8.0.0; 7.7.0 SX126x skip-reset-on-startup +
  simplified `rxBw`; 7.7.1 LoRaWAN `parseDownlink` OOB read + leak. `RADIOLIB_EXCLUDE_LORAWAN=1` is set ✓.
- NimBLE changelog claims ✓ all: 2.3.2 secure connections disabled by default + legacy FreeRTOS port + IDF 4.x
  build fixes; 2.4.0 GATT handle rework, multi-mbuf >255-byte notification truncation fix, re-pair after bond
  deletion, whitelist bounds; 2.5.0 client connection-state tracking, `setValue` char length, connection retry;
  2.5.1 `whiteListRemove` use-after-free, scan-response timer crash on reinit, tick→ms infinite recursion.
- Platform pin inventory in B-04 ✓ exact (`^6.13.0` base; `^6.6.0` for the three e-paper boards; `@ 6.6.0` for
  t_deck/t_deck_plus with the TFT_eSPI#3332 comment; `6.5.0` for t_deck_pro/t5_epaper).

**Firmware constants and code references**
- CSMA table ✓ **entirely correct**: `CSMA_SLOT_SIZE 35` with the "28ms CAD + 2ms TX-Switch + 5ms Safety"
  comment, `CSMA_MAX_ATTEMPTS 3`, `CSMA_RAPID_RX_MS 100`, bases 3000/3000/4500/5500/5500, slots 10 each
  (350 ms jitter), priorities 1–5 with the stated semantics, retry reduction `*5/6` (−17%) and `*2/3` (−33%)
  (`src/configuration_global.h:136-179`, `src/lora_functions.cpp:2073-2076`).
- `random()` determinism claim ✓ **fully verified against the installed framework source**:
  `framework-arduinoespressif32/cores/esp32/WMath.cpp` has `static bool s_useRandomHW = true;`,
  `randomSeed()` calling `srand()` + clearing the flag, and `random()` selecting `esp_random()` vs `rand()`.
  `grep -rn randomSeed src/` → **no hits**. The backoff call site is `src/lora_functions.cpp:2076`. ✓
- `LORA_ISR_DEBUG` guards exactly 5 sites (`src/lora_functions.cpp:330,336,357,363,371`) and is defined
  nowhere in `src/`, `variants/` or `platformio.ini` ✓.
- `--loradebug on` sets `bLORADEBUG`, `bDisplayInfo`, `bDisplayRetx` and persists via `node_sset |= 0x0200` +
  `save_settings()` (`src/command_functions.cpp:2454-2470`) ✓.
- `printfdeb` terminates in a blocking `Serial.printf(temp)` (`src/printfdeb_functions.cpp:118`) ✓;
  `;`→space rewriting when `!bDEBUGCSV` (`:62-70`) ✓; `.`→`,` under `--debug de` (`:113-114`) ✓;
  `bDEBUGCSV = meshcom_settings.node_sset4 & 0x0001` (`src/esp32/esp32_main.cpp:811`, `src/nrf52/nrf52_main.cpp:568`) ✓.
- 51 distinct `[MC-DBG]` marker names in `src/` ✓ ("50+"); every marker listed in 07 §1.1 exists.
- Every command cited in 07 §1.2 exists as a `commandCheck` literal (only `--inject`, `--dump`, `--faketime`
  are absent — correctly, they are the proposals) ✓.
- `--txfreq` validation formula ✓ (`430.0 + BW/200 … 439.000 − BW/200`, `869.4 … 869.65`).
- Spectral scan 430.0–440.2 MHz (`src/spectral_scan.cpp:141-142`) ✓.
- Net console ✓: port 2323 (`src/net_console.h:32`), HMAC-SHA256 challenge-response with the password never
  transmitted, open when `node_passwd` is empty, ESP32-only (`#if defined(ESP32) && !defined(DISABLE_NET_CONSOLE)`),
  and the header states it replaced a TLS console to free the 36 KB mbedTLS I/O buffers ✓.
- `MAX_MSG_LEN_PHONE 300` ✓, `PAIRING_PIN "000000"` ✓, `BLEtoPhoneBuff[MAX_RING][MAX_MSG_LEN_PHONE+5]` = `[MAX_RING][305]` ✓.
- Per-board ring sizing ✓ exact, including the tight-board block quoted in 03
  (`MAX_MHEARD 10 // was 20, limited by DRAM`, `MAX_MHPATH 10 // was 30`, `MAX_DEDUP_RING 10 // was 60`,
  `src/configuration_global.h:102-105`) and `MAX_MHEARD 80` / `MAX_DEDUP_RING 100 // wraparounds observed` at `:95-98`.
- `struct aprsMessage` has exactly 7 `String` members with the names listed ✓; `struct mheardLine`
  (`src/aprs_structures.h:67`) has exactly 7 ✓. `docs/codequality-rules.md:14` says
  "String handling: fixed `char[]` arrays -- NEVER Arduino `String` in hot paths." ✓
- 15 distinct `msg_text+N` argument offsets, max `+20`, and the explanatory comment near `:280` ✓.
- `commandCheck()` is a prefix match ✓ (truncate to `strlen(command)`, `casecmp`).
- Board `hwids` for Heltec V3 ✓: `platform-espressif32/boards/heltec_wifi_lora_32_V3.json` →
  `[["0x303A","0x1001"]]` in every installed platform version; no local `boards/heltec_wifi_lora_32_V3.json`
  overrides it. `-DARDUINO_USB_MODE=1` with `;-DARDUINO_USB_CDC_ON_BOOT=1` commented out at
  `variants/heltec_wifi_lora_32_V3/platformio.ini:27-28` ✓ — so `Serial` is UART0 and native USB-Serial/JTAG is available.

**Test-infrastructure state**
- Zero automated tests ✓. No `platform = native` and no `test_framework`/Unity/GoogleTest/Catch2 reference
  anywhere in `platformio.ini` or the variant fragments ✓.
- `test/` contains `compress_functions.cpp/.h` and `test_invariant_TinyGsmClientSequansMonarch.h` ✓
  (plus the stock PlatformIO `README`, not mentioned).
- `.github/workflows/meshcom-ci.yml` triggers on `push: tags: '*'` only — no `pull_request`, no branch push ✓.
- `.gitignore:10` is `.*` with only `!/.gitignore` negated ✓ (V-06 valid).
- `tools/meshcom_monitor/` holds exactly 17 `.log` files ✓ (but see F1-3 for what is in them).
- `tools/serial_monitor.py` has `--replay` (`:1035`) and `--no-dtr` (`:1029`) ✓.
- `docs/loradebug-serial-output.md` and `docs/report-ble-tx-latency.md` exist ✓; 9 `docs/code-audit-*.md` files ✓.
