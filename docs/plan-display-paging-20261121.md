# Display paging and user button: implementation plan

Date: 2026-11-21. Branch: `fork-dev` at `067d29cd`. Problem files pin `96aaa67c72` (release v4.40a.11.21), HEAD is
later. Nothing in this plan is implemented; no `pio` command, no build and no hardware was used to write it.

Tickets (problem files in the operator's `telegram/problems/`):

| Ticket | Title                                                           | Backlog id   |
| ------ | --------------------------------------------------------------- | ------------ |
| P-0021 | OLED and web interface show different numbers of messages       | DISP-PAGE-01 |
| P-0384 | OLED: display off within 5 s after a browse click is ignored    | DISP-PAGE-02 |
| P-0059 | Wireless Paper: first button press redraws the newest message   | DISP-PAGE-03 |
| P-0345 | User button switched off in the web UI still works until reboot | BTN-OFF-01   |

## 0. One plan, four independent fixes

P-0021, P-0384 and P-0059 share one mechanism, the page ring and the button hold
(`pageLastPointer`, `pagePointer`, `pageHold`, `src/loop_functions.cpp:1187-1189`) and one file,
`src/onebutton_functions.cpp`. P-0345 shares only that file and `src/command_functions.cpp`: it is the button
handler wiring, not paging. The four fixes are logically independent (no fix needs another one to be merged, none
changes the behaviour another one tests), so they get four backlog rows. They collide on files, so the
implementation wave is sequenced by file, section 5.

ESP32 and nRF52 compile the same source for all four (`init_onebutton()` is called from `src/esp32/esp32_main.cpp:1988`
and `src/nrf52/nrf52_main.cpp:869`; `loop_functions.cpp`, `command_functions.cpp` and `onebutton_functions.cpp` are
common code). The per-board branches are chosen with `#if` inside `singleClick()`.

No commit since the pin touched the cited code.
`git log --oneline 96aaa67c72..HEAD -- src/onebutton_functions.cpp src/display_clock_gate.h src/esp32/udp_frame_esp32.cpp`
is empty. The commits that did touch `loop_functions.cpp` (`ce05f38c` AppLink, `9283e6a3` PONG-SIGN-01),
`command_functions.cpp` (`8b4d509b`, `ce05f38c`, `f4af55b0` CMD-DASH-01) and the two mains (`ce05f38c`) only moved
line numbers: `git diff 96aaa67c72 HEAD` of those files has no added or removed line that mentions `pageHold`,
`pagePointer`, `pageLastPointer`, `bDisplayIsOff`, `wpMsgIdx`, `PAGE_MAX`, `display off`, `button`, `btn` or
`init_onebutton`. The shift is +2 to +3 lines in `command_functions.cpp` and `loop_functions.cpp`. All commits cited in
this plan are ancestors of HEAD and none is reverted (`git merge-base --is-ancestor` rc 0 for each;
`git log -i --grep=revert` lists only unrelated aprswx and gateway reverts).

Upstream: `git show upstream/dev:<path>` at `5698913a` has the same code for all four (details per ticket). All four
are upstream PRs, not fork-only. `git diff upstream/dev HEAD -- src/onebutton_functions.cpp` is 49 lines, all of it the
fork-only T-Beam deep-sleep hold (`MC_TBEAM_VERYLONG_SLEEP`, `d0ecf82f`).

## 1. DISP-PAGE-01 (P-0021): OLED browse depth depends on the ring position

### Symptom

The reporter (OE3WAS, 2026-03-23, no board, no version) pages through only 3 messages on the OLED while the web page
lists at least 12, and once a UDP message was on the OLED but not in the browser. The second half (message missing in
the browser) has no code path and needs a reproduction; it is not part of this plan and stays open in P-0021.

### Root cause at HEAD

The click walk ends at the absolute ring slot 5, not at the end of the stored messages.

- Ring: `PAGE_MAX 6` (`src/loop_functions.cpp:1170`; T-Echo `:1164`; 10 only for `WP_DISP` and `BOARD_E290`, `:1167`).
  Every stored text page advances `pageLastPointer` and clears the next slot (`:1790-1796`,
  `pageLastLineAnz[pageLastPointer] = 0`), so at most `PAGE_MAX - 1 = 5` messages are browsable.
- Click: `singleClick()` (`src/onebutton_functions.cpp:35`) starts at `pagePointer`, which the hold timeout sets to the
  newest slot (`src/loop_functions.cpp:2197`, `pagePointer = pageLastPointer - 1`), shows it, and does
  `pagePointer--` with wrap (`:308-312`). The generic branch stops with
  `if(pagePointer == 5) { sendDisplayHead(true); return; }` at `src/onebutton_functions.cpp:236`; the T114 branch has the
  same stop at `:121` (`pagePointer == 5 || pageLastLineAnz[pagePointer] == 0`).
- Effect: with the newest message in slot `k` the walk shows slots `k, k-1, ..., 0`, then wraps to 5 and shows the status
  page. Depth is `k + 1` for `k = 0..4`, and none for `k = 5`: clicks show only the status page. The arrival
  (`src/loop_functions.cpp:1790`, `pagePointer = pageLastPointer` with the newest message in slot 5) and the hold
  timeout (`:2197`, `pagePointer = pageLastPointer - 1`, wrapped to 5 at `:2198-2199` when `pageLastPointer` is 0) both
  leave `pagePointer = 5`, and `src/onebutton_functions.cpp:236` returns at once, so every click shows the status page
  until the next message arrives (the comment at `:141-142` agrees). A message that sits in slot 5 can never be shown
  by a click. "3" is one position of the cycle. The `E290` block (`:211-233`) and the Wireless Paper branch (`:139-205`)
  already stop at an empty slot; the comment at `:141-142` says the slot-5 rule was the defect there.
- Not the cause (RCA correction, see problem file): position pages do not occupy ring slots (`iDisplayType == 0 && bNeu`
  gate, `src/loop_functions.cpp:1500`, `:1790`).
- Commits since the pin: none (section 0). The slot-5 stop is unchanged since `v4.35o` (`git show v4.35o:src/onebutton_functions.cpp`
  line 35, `v4.35p` line 35).

### Platforms

| Platform / board                                                      | Affected   | Basis                                                                                                                                                                                                              |
| --------------------------------------------------------------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| ESP32 u8g2 OLED boards (`MC_HAS_U8G2`: Heltec V2/V3/V4, T-Beam, TTGO) | yes        | generic branch `:236`, `PAGE_MAX 6`                                                                                                                                                                                |
| ESP32 TFT: Heltec Wireless Tracker, T-Connect-Pro                     | yes        | generic branch, `displayTFT()`                                                                                                                                                                                     |
| nRF52 Heltec T114 (`HAS_TFT_114`)                                     | yes        | own stop at `:121`; the generic stop at `:236` is not reached                                                                                                                                                      |
| nRF52 T-Echo (`PAGE_MAX 6`, `maxdisplines 11`)                        | yes        | generic branch                                                                                                                                                                                                     |
| RAK4631 with a display (`BUTTON_PIN` only with RAK13002)              | not traced | same function, depends on the add-on                                                                                                                                                                               |
| ESP32 Wireless Paper, Vision Master E213 (`WP_DISP`)                  | no         | own branch, section 4                                                                                                                                                                                              |
| ESP32 Vision Master E290 (`BOARD_E290`)                               | no         | already stops at the empty slot (`:219`)                                                                                                                                                                           |
| T-Deck Pro                                                            | no         | no `btn.tick()` (`esp32_main.cpp:3449`); `singleClick()` is reached only through the INSTRUMENT `--btn` hook (`command_functions.cpp:6969`)                                                                        |
| T-Deck, T-Deck Plus                                                   | no         | tick present (`esp32_main.cpp:3449`), but no `BUTTON_PIN` in `variants/t_deck*/configuration.h` (pin 99 unless set by the operator) and excluded from the `--btn` hook (`command_functions.cpp:6969`); own LVGL UI |
| T5 e-paper                                                            | no         | `singleClick()` returns first (`:40-42`)                                                                                                                                                                           |

### Fix design

Reuse the E290 algorithm, which is already correct, for every board that has the ring.

1. New header-only `src/page_ring.h` (no Arduino dependency, `static inline`), the E290 block moved verbatim:
   `pageRingNewest(last, pageMax)`, `pageRingCount(lineAnz, pageMax, last)` (contiguous non-empty slots from the newest;
   three copies of this loop exist today: `src/onebutton_functions.cpp:176-183` and `:224-230`,
   `src/loop_functions.cpp:2810-2815`), and
   `pageClickStep(lineAnz, pageMax, last, pointer) -> {kind: Status|Message, slot, rank, nextPointer}`:
   status (and `nextPointer = newest`) when no message is stored, when `pointer` is out of range or its slot is empty;
   otherwise `Message` with `rank = count - ((newest - pointer + pageMax) % pageMax)` and `nextPointer = pointer - 1`
   with wrap.
2. `singleClick()`: delete the `#if defined(BOARD_E290) ... #else if(pagePointer == 5) ... #endif` split; call
   `pageClickStep()` for all boards. Status: `pagePointer = step.nextPointer; sendDisplayHead(true); return;`. Message:
   `pageNo = step.rank`, show `step.slot`, then `pagePointer = step.nextPointer`. Remove `pagePointer == 5 ||` from the
   T114 test at `:121` (the empty-slot test stays, so the T114 status page and GPS page flow is unchanged).
   The E290 block is replaced by the same call (identical result by construction, pinned by the oracle test below).
3. Click order becomes newest, ..., oldest, status page, newest, on every board. Visible change on OLED boards: the
   number at the top right is the rank (newest = highest, like E290 and Wireless Paper) instead of `slot + 1`. With slot
   numbers a six-message ring shows sequences such as 3, 2, 1, 6, 5; the rank shows 5, 4, 3, 2, 1. See open question 1.
4. Side finding, a memory-safety defect that the helper also removes: at boot `pagePointer = 0`, `pageLastPointer = 0`,
   slot 0 is empty and `pageHold` starts at `PAGE_MAX - 1` (`src/loop_functions.cpp:1186-1189`). A click in the first
   seconds sets `pagePointer = -1` (`src/onebutton_functions.cpp:243-244`, `pageLastPointer - 1`) and then reads
   `pageLastLineAnz[-1]`, `pageLastLine[-1]`, `pageLastText[-1]` and `pageLastTextLong*[-1]` (out of bounds). The loop bound
   `pageLineAnz` (`:248`) comes from that garbage, so the loop can also WRITE past `pageLine[maxdisplines][3]` and
   `pageText[maxdisplines][25]` (`maxdisplines` is 7, 11 on T-Echo): memory corruption, not only a bad read. Generic
   branch only (OLED, TFT, T-Echo); E290, Wireless Paper and T114 test for the empty slot first (`:219`, `:170`, `:121`).
   The helper returns Status for an empty ring and an empty slot, so the index never goes below 0.
5. `PAGE_MAX` stays 6. A larger OLED ring costs about 221 B of DRAM per slot (`pageLastText` 175 B, `pageLastLine` 42 B,
   `pageLastLineAnz` 4 B; plus 225 B per slot where `HAS_LONG_PAGE_TEXT`). The web list is deeper by design
   (browser history `MC_HIST_MAX 200`, `src/web_functions/web/mc.js:42`; byte ring `RING_BYTES_PHONE`), so the answer to
   the reporter is: the OLED shows the last five, the web the last dozens.

### Test (fails before, passes after)

- New suite `test/test_page_ring/test_page_ring.cpp`, new env `[env:native_page_ring]` in `platformio.ini`
  (same stanza as `[env:native_display_clock]`: `test_framework = unity`, `test_filter = test_page_ring`,
  `-std=gnu++17 -D NATIVE_BUILD=1 -D UNIT_TEST=1 -I src -Wall -Wextra`, `test_build_src = no`).
  Run: `pio test -e native_page_ring -f test_page_ring`.
- `test_depth_is_stored_count_for_every_ring_position`: for `pageMax = 6`, newest slot `k = 0..5` and stored count
  `n = 1..5` (ring written the way `sendDisplay1306()` writes it: slot, advance, clear the next slot), click from
  `pointer = newest` until Status, assert the visited slots are newest to oldest and their number is `n`. Fails against
  the legacy rule (the verbatim old stop at slot 5, kept in the test as `legacyClick()` like the pre-W2 oracle in
  `test_display_clock`): for `k = 2, n = 5` the legacy copy visits 3 slots and never visits slot 5, the assertion
  `visited == 5` fails with 3; for `k = 5` it visits none (Status at once). The "3" and the "none" are properties of the
  legacy copy kept in the test, not measurements of the firmware.
- `test_status_page_after_oldest_and_loop`: the click after the oldest is Status with `nextPointer == newest`; the next
  click is the newest again.
- `test_empty_ring_and_boot_pointer` (regression test for the side finding in design step 4, which belongs to
  DISP-PAGE-01 because its helper removes the path): `lineAnz` all 0, `last = 0`, `pointer = 0` returns Status and never
  an index below 0 (the `-1` access that can read and write out of bounds). The legacy copy returns index `-1` here.
- `test_e290_equivalence`: for every `last`, fill and `pointer` with `pageMax = 10`, `pageClickStep()` equals the
  verbatim E290 block (kept in the test as the oracle).
- Source pin (in `test/test_display_clock/test_display_clock.cpp`, env `native_display_clock`, which already has
  `read_repo_file`, `func_body`, `code_only`; no fourth copy of these helpers): `code_only()` of
  `src/onebutton_functions.cpp` contains no `pagePointer == 5`, includes `page_ring.h`, and the `singleClick()` body
  contains `pageClickStep(` (deleting the `== 5` line and adding an unused include must not pass). The "no
  `BOARD_E290` split" check is limited to the range from `int pageNo` to `bDisplayIsOff=false` (`:209-246`), because
  `BOARD_E290` also appears legitimately at `:260`, `:282` and `:294`. Fails today (`:121`, `:236`).

### Bench

Heltec V3 (DK5EN-1), INSTRUMENT build. Rules: `tools/bench/identity_guard.py` first, `lsof` on the port, `--airgap on`,
TEST group only (`oled_harness.py` injects with `TEST_GROUP`), TX power at most 2 dBm, own call sign. Extend
`tools/bench/oled_harness.py` (its `PAGE_MAX = 6` and `scenario_pages` only require two distinct pages) with scenario
`depth`: for `i = 1..6` inject one more `--injectmsg TEST oled depth <i>`, wait until `--oledstat` shows `hold;0`, send
`--btn click` seven times, read `page` after each. Expected: `min(i, 5)` distinct message pages in newest-to-oldest
order, then the head page (`type;9`), then the newest again. Today the count is `slot_of_newest + 1` for newest slot
0..4 and none for slot 5 (clicks show only the head page).

### Upstream

Yes. `upstream/dev` has `pagePointer == 5` at `src/onebutton_functions.cpp:121` and `:236`, the E290 block, and
`PAGE_MAX` at `src/loop_functions.cpp:1152-1158` unchanged. Upstream PR with the same diff; the German PR body names the
ring layout.

### Risks

- E290 refactor: covered by the equivalence test; the E290 build is not on the central bench, so build it
  (`pio run -e vision-master-e290`) at the gate.
- The visible number changes from slot to rank on OLED boards (open question 1).
- `PAGE_MAX 6` keeps depth 5, which is still not "12"; the reporter has to be told.

## 2. DISP-PAGE-02 (P-0384): `--display off` ignored while the browse hold runs

### Symptom

A "display off" command (console, app, web) that arrives within about 5 s after a button browse click is stored but not
applied: the browsed page stays lit until the next message or click. Found by code reading; no report describes the
window (evidence 3096 is context only).

### Root cause at HEAD

- A browse click ends in `pageHold = 5` (`src/onebutton_functions.cpp:312`).
- The handler `commandCheck(msg_text+2, (char*)"display off")` (`src/command_functions.cpp:2033`) sets `bDisplayOff` and
  `bDisplayIsOff` (`:2035-2036`), saves, and calls `sendDisplayHead(false)` (`:2049`).
- `sendDisplayHead()` returns at once while `pageHold > 0` or `bSetDisplay` (`src/loop_functions.cpp:1828`:
  `if((bSetDisplay || pageHold > 0) && !bInit) return;`), before the `bDisplayIsOff` branch that sends `"#C"` (`:1844-1856`).
- `sendDisplayTime()` (`:2190`) counts `pageHold` down once a second (`:2193`) and then only calls
  `sendDisplayClockLine()`, whose gate skips with `SkipOff` while `bDisplayIsOff` (`src/display_clock_gate.h`
  `dispClkDecide`), so nothing clears the panel.
- Same pattern is already handled elsewhere: `PressLong()` sets `pageHold = 0` before it issues `--display off`
  (`src/onebutton_functions.cpp:414`), the T114 `tripleClick()` does it at `:362`, and the TFT block of the handler
  (`src/command_functions.cpp:2055-2059`, #1182) darkens the Wireless Tracker backlight by hand because of this very guard.
  The console and web path has no such reset.
- Commits since the pin: none touch this (section 0).

### Platforms

| Platform / board                                 | Affected   | Basis                                                                                                               |
| ------------------------------------------------ | ---------- | ------------------------------------------------------------------------------------------------------------------- |
| ESP32 and nRF52 u8g2 OLED boards (`MC_HAS_U8G2`) | yes        | common code; the "#C" blanking is skipped                                                                           |
| nRF52 Heltec T114                                | yes        | its backlight is switched inside the same branch, after the guard (`:1845`); `HAS_TFT_114` gets no `tftBacklight()` |
| nRF52 T-Echo, ESP32 T-Connect-Pro                | yes (code) | same function and guard; not on the bench                                                                           |
| Heltec Wireless Tracker (`HAS_TFT`)              | no         | `tftBacklight()` at `command_functions.cpp:2058` (#1182)                                                            |
| T-Deck, T-Deck Plus                              | no         | `tft_off()` at `:2052`                                                                                              |
| Wireless Paper, E213, E290, T5                   | not traced | e-paper keeps its image; the click branches differ                                                                  |

### Fix design

One line, in the `"display off"` handler before `sendDisplayHead(false)`:

```cpp
pageHold = 0;   // P-0384: a browse click holds the page for 5 s; --display off must not wait for it
```

`pageHold` is the global of `loop_functions.cpp:1189`, declared in `loop_functions_extern.h` (already used by
`onebutton_functions.cpp`; check the include in `command_functions.cpp` when implementing). The next `sendDisplayTime()`
tick runs `pageHold--` to -1, clamps to 0 and resets `pagePointer` to the newest slot (`:2193-2198`), which is the state
after a normal hold timeout. The `"display on"` handler needs nothing: the panel is lit and the guard only delays a
redraw. Not in scope: a draw in progress (`bSetDisplay`) can still swallow the command; the handler runs in the main
loop on the OLED boards, so this is not observed.

Rejected alternative: send `"#C"` from `sendDisplayTime()` on the hold transition. It adds state and a clock-gate input for
a case the handler can solve directly.

### Test (fails before, passes after)

Host compile of `command_functions.cpp` does not exist (the stance of `native_command_match`), so the proof is a source
pin plus a pure predicate:

- In `test/test_display_clock/test_display_clock.cpp` (env `native_display_clock`, `pio test -e native_display_clock -f test_display_clock`),
  `test_display_off_handler_cancels_browse_hold`: take the `"display off"` branch of `src/command_functions.cpp` (from
  `commandCheck(msg_text+2, (char*)"display off") == 0` to the next `commandCheck`), `code_only()` it, and assert that
  `pageHold = 0;` appears before the first `sendDisplayHead(false);`. Fails today: the branch has no `pageHold` in code
  (the only mention is in the #1182 comment, which `code_only()` drops).
- Seam for the behaviour: move the guard into `display_clock_gate.h` as
  `dispHeadBlocked(bool setDisplay, int pageHold, bool init)` and call it at `src/loop_functions.cpp:1828`. Test
  `test_head_guard_blocks_during_hold`: `(false, 5, false)` is blocked (the mechanism), `(false, 0, false)` is not,
  `(true, 0, false)` is blocked, any `init = true` is never blocked. Passes before and after the extraction; its job is to
  pin that the guard consults `pageHold`, so a future change cannot silently make the handler line unnecessary or
  insufficient. Optional; the source pin is the failing test.

### Bench

Heltec V3 (DK5EN-1), INSTRUMENT build, same rules as section 1. Sequence: `--oledlog on`, `--btn click`, within 1 s
`--display off`, wait 1.5 s, `--oledstat`. Expected after the fix: `isoff;1`, a `[OLED];frame` for the blank buffer and
`crc;efb5af2e` (the blank 128x64 CRC pinned in `scenario_display` of `oled_harness.py`). Today: `isoff;1` but the last frame
is the browsed page (CRC differs) for the rest of the hold and beyond. Add the sequence to `oled_harness.py` as scenario
`display_after_click`. T114 and T-Echo are not on the central bench; they stay "by code reading" until a node is available.

### Upstream

Yes. `upstream/dev` has the handler without a hold reset (`src/command_functions.cpp:1848-1875`, including the #1182 TFT
block), the guard at `src/loop_functions.cpp:1816`, and `pageHold=5` at `src/onebutton_functions.cpp:312`.

### Risks

Very low: the line only runs on an explicit display-off. `pageHold = 0` also stops the T114 GPS page hold (60 s,
`src/nrf52/t114_gps_page.cpp:191`) when the display is switched off; that is intended (the display is off).

## 3. BTN-OFF-01 (P-0345): `--button off` has no effect until reboot

### Symptom

Switching "User Button" off in the web UI (Heltec V3, v4.35p, uid 3130) changes nothing: clicks, double clicks and the long
press still act until the next reboot.

### Root cause at HEAD

- The off row has no hook: `{ "--button off", &bButtonCheck, ..., 0x00007FEF, 0x00000000, nullptr, TG_DIRTY_NODE, TG_SAVE | TG_BRETURN }`
  at `src/command_functions.cpp:645`. The on row (`:644`) has `tg_post_button_on` (`:484`:
  `gpioFnWins(PIN_FN_BUTTON); init_onebutton();`).
- `init_onebutton()` (`src/onebutton_functions.cpp:493`) attaches the handlers only when `bButtonCheck` is true (`:495-513`,
  plus `attachDuringLongPress` under `MC_TBEAM_VERYLONG_SLEEP`). Nothing detaches them. `bButtonCheck` is read nowhere
  else in `onebutton_functions.cpp` (`git grep -n bButtonCheck -- src/onebutton_functions.cpp` finds `:495` only), and
  `singleClick()` (`:35`), `doubleClick()` (`:316`), `tripleClick()` (`:346`), `PressLong()` (`:398`) do not test it.
- `btn.tick()` is unguarded: `src/esp32/esp32_main.cpp:3450` (inside `#if not defined(BOARD_T_DECK_PRO)`),
  `src/nrf52/nrf52_main.cpp:2068` and `:3009` (the 10 ms delay loop on the T114).
- OneButton 2.6.2 (`mathertel/OneButton@^2.6.1`, `platformio.ini:81`) calls a handler only when its pointer is non-null
  (`OneButton.cpp` `if (_clickFunc) _clickFunc();` and the same for double, multi, long press start, during long press), and
  `attachClick(nullptr)` simply stores the null. `reset()` only resets the state machine.
- Commits since the pin: none touch the off row, `init_onebutton()` or the tick sites. The GPIO-01 series (`283e3d56`,
  `29112ca7`, `189a3c5b`, `dc2decdc`) only changed the pin setters and `tg_post_button_on` and is older than the pin.
- Not verified on a device: uid 3130 does not say whether the node was rebooted after switching off.

### Platforms

ESP32 (Heltec V3, the reporter's board, and every other ESP32 board that has `btn.tick()`) and nRF52 (T114, T-Echo,
RAK4631 with the RAK13002) are both affected: same hook table, same attach function, one `tick()` per main loop. T-Deck Pro
has no tick (`BOARD_T_DECK_PRO` guard) and is not affected; T-Deck and T-Deck Plus compile the file but their button path
was not traced.

### Fix design

Detach in a post hook; leave the tick sites alone.

1. `src/onebutton_functions.cpp`: new `void deinit_onebutton()` next to `init_onebutton()`:
   `btn.attachClick(nullptr)`, `attachDoubleClick`, `attachMultiClick`, `attachLongPressStart` and, under
   `MC_TBEAM_VERYLONG_SLEEP`, `attachDuringLongPress`, then `btn.reset()` and
   `Serial.printf("[OBUT]...One Button GPIO(%i) detached\n", iButtonPin)`. Declare it in `src/onebutton_functions.h`.
2. `src/command_functions.cpp`: `static void tg_post_button_off() { deinit_onebutton(); }` beside `tg_post_button_on`
   (`:484`) and use it as the hook of the `--button off` row (`:645`). The hook runs after `save_settings()` (see
   `post_after` in `src/command_toggles.h`), so the setting is stored first.
3. Why not guard `btn.tick()` or the handlers: three tick sites (and the T114 loop) instead of one hook; and the INSTRUMENT
   hook `--btn click|double|triple` (`src/command_functions.cpp:6968`) calls the handlers directly, so a guard inside them
   would break the bench hook while the button is off. Detaching leaves the hook working.
4. The deep-sleep wake source already follows `bButtonCheck` at run time (`src/esp32/esp32_sleep.cpp:68`,
   `src/nrf52/nrf52_sleep.cpp:145`), so no change there.

### Test (fails before, passes after)

OneButton needs the Arduino core, so the proof is a source scrape in the style the repo already uses for this file
(`test_tbeam_very_long_press_deepsleep`, "OneButton needs the firmware"). In `test/test_command_setters/test_command_setters.cpp`
(env `native_command_setters`, `pio test -e native_command_setters -f test_command_setters`; helpers `read_repo_file` and
`pin_find` exist there):

- `test_button_off_row_detaches`: the `{ "--button off",` row of `src/command_functions.cpp` contains `tg_post_button_off`
  and not `nullptr`; the body of `tg_post_button_off` calls `deinit_onebutton()`. Fails today: the row has `nullptr`,
  the function does not exist.
- `test_deinit_detaches_every_attached_handler`: for each `btn.attach<Name>(` call in the `init_onebutton()` body of
  `src/onebutton_functions.cpp`, the `deinit_onebutton()` body contains `btn.attach<Name>(nullptr)`. Fails today:
  there is no `deinit_onebutton()`. This keeps the two functions in step when a handler is added later (the T-Beam
  `attachDuringLongPress` is the example).

### Bench

Heltec V3 (DK5EN-1), normal build, an operator at the board. `--btn click` cannot prove this (it calls the handler
directly). Sequence: `--setcont on` (a press prints `SingleClick`), press the button: line appears; `--button off`:
`[OBUT]...detached` line; press: no line and no page change (`--oledstat` `frames` unchanged if INSTRUMENT); `--button on`:
`[OBUT]...started`, press: line again; reboot with the button off, press: no line. Rules as in section 1; this sequence
transmits nothing.

### Upstream

Yes. `upstream/dev` has the same row with `nullptr` (`src/command_functions.cpp:629`), `tg_post_button_on` (`:460`),
`init_onebutton()` with the only `bButtonCheck` test (`src/onebutton_functions.cpp:449`), and the unguarded ticks
(`esp32_main.cpp:3253`, `nrf52_main.cpp:1958` and `:2891`). Upstream PR; the T-Beam `#if` in `deinit_onebutton()` is
fork-only and is left out of the upstream diff.

### Risks

- Detached handlers while a gesture is in progress: `btn.reset()` clears the half-finished click state.
- Nothing else owns the pin: with the button off the pin stays configured with the pull-up (as after a boot with the button
  on); at a boot with it off the pin is never configured (`docs/CHANGELOG-stability.md:160`). That difference is old and
  harmless.
- `--button off` now also disables the T-Beam 3 s deep-sleep press; that is the meaning of the setting.

## 4. DISP-PAGE-03 (P-0059): Wireless Paper, the first press redraws the newest message

### Symptom

On a Wireless Paper V1.2 (build 2026-08-06, OE3WAS) a new message is shown without the counter at the top right; the first
press seems to do nothing (it redraws the same message, now with the counter); three stored messages need three presses.
The ghosting reports are P-0162 and the reboot with vanished messages is P-0156; neither is covered here.

### Root cause at HEAD

- Arrival: `sendDisplay1306()` stores the page in slot `pageLastPointer`, then `pagePointer = pageLastPointer;`
  `pageLastPointer++` (`src/loop_functions.cpp:1790-1792`), so `pagePointer` is the newest slot. `sendDisplayText()` clears
  the browse index, `wpMsgIdx = 0` (`:3188`), and the live render draws no counter (`wpDrawMsgIdx()` is called only from
  `wpShowStoredMessage()` `:2573` and `wpRefreshClock()` `:2518`, `:2531`).
- Click: the `WP_DISP` branch (`src/onebutton_functions.cpp:139-205`) computes
  `wpSteps = (wpNewest - pagePointer + PAGE_MAX) % PAGE_MAX` (`:187`), which is 0 after an arrival, and shows
  `wpShowStoredMessage(pagePointer, wpRank)` (`:194`): the message already on the panel, now with the counter
  (`wpRank = wpShow - wpSteps`, `:191`) and a full refresh. Only then it steps `pagePointer` to the next older slot
  (`:198-203`).
- Commits since the pin: none (section 0). The WP click block is byte-identical to the old pin (problem file re-check).
  `ff60d1ee` (E290, 2026-10-25) changed the browse window, not this order.

### Platforms

| Platform / board              | Affected | Basis                                                                                                           |
| ----------------------------- | -------- | --------------------------------------------------------------------------------------------------------------- |
| ESP32-S3 Wireless Paper       | yes      | `BOARD_WIRELESS_PAPER` defines `WP_DISP` (`src/configuration_global.h:230`)                                     |
| ESP32-S3 Vision Master E213   | yes      | same `WP_DISP` path (`env:vision-master-e213`)                                                                  |
| E290, OLED, TFT, T-Echo, T114 | no       | other click branches (section 1); same first-press-redraws pattern exists on E290 and OLED, see open question 2 |
| nRF52                         | no       | no WP board; `WP_DISP` is ESP32 only                                                                            |

### Fix design

Two parts, one implementer, same header as section 1.

1. First press moves on. Add a global `bool wpLiveNewest` (beside `wpMsgIdx`, `src/loop_functions.cpp:609`): set where the
   slot is stored (`iDisplayType == 0 && bNeu`, `src/loop_functions.cpp:1788`), not at the render (`:3188`), cleared in `wpShowStoredMessage()` and in the `wpMsgIdx = 0` of `sendDisplayHead()`
   (`:1834`), so it is true exactly while the panel shows the newest message that no click has stepped from. In
   `src/page_ring.h` add the pure
   `wpBrowseClick(lineAnz, pageMax, last, browseMax, state{statusStep, liveNewest, pointer}) -> {status, slot, rank, nextState}`:
   the existing logic of `:149-203` moved verbatim, plus one rule after `wpSteps` is known: if `liveNewest && wpSteps == 0`, the
   newest is already on the panel, so with `wpShow <= 1` return Status (`nextState = {false, false, newest}`), else take
   `wpSteps = 1` and the next older slot. `wpStatusStep` (the static at `:147`) becomes part of the state struct, kept as a
   static in `singleClick()`. The branch calls `wpBrowseClick()`, `wpShowStoredMessage(slot, rank)` or
   `sendDisplayHead(true)`, and stores `nextState`.
2. Counter on arrival. Draw the rank on the live render: in the `HAS_EPAPER` transfer of `sendDisplay1306()`
   (`src/loop_functions.cpp:1559-1580`, the `update()` for non-`#N`, non-`#S/#F` pages), under `WP_DISP` and
   `iDisplayType == 0 && bNeu`, set `wpMsgIdx = min(pageRingCount(..., pageLastPointer), WP_MSG_BROWSE_MAX)` (the slot was
   written a few lines above, `:1502`) and call `wpDrawMsgIdx()` before `update()`; it needs a forward declaration because it
   is `static` at `:2431`. `wpRefreshClock()` then keeps the digit. The `wpMsgIdx = 0` at `:3188` stays; it clears the digit of
   an old browse page until the new one is drawn. This is a visible change, open question 2.
3. Evidence line under `bDisplayCont`: `[WPBROWSE];slot;%d;rank;%d;live;%d` from the click branch, so the bench can read the
   step without a camera.

### Test (fails before, passes after)

In the same suite as section 1: `test/test_page_ring/test_page_ring.cpp`, env `native_page_ring`
(`pio test -e native_page_ring -f test_page_ring`), `pageMax = 10`, `browseMax = 4`.

- `test_first_click_after_arrival_shows_second_newest`: three messages stored, state `{statusStep false, liveNewest true,
pointer = newest}`, one click returns the second-newest slot with rank 2. Fails against the legacy logic (kept verbatim as
  `legacyWpClick()`): it returns the newest slot with rank 3. Then click 2 returns rank 1, click 3 returns Status, click 4
  returns the newest with rank 3 (not live any more).
- `test_single_message_arrival_click_goes_to_status`: one stored message, live, click returns Status (legacy: redraws the
  message).
- `test_not_live_equals_legacy`: with `liveNewest = false`, `wpBrowseClick()` equals `legacyWpClick()` for every `last`
  0..9, fill 0..9, `pointer` 0..9 and `statusStep` 0/1 (the browse-from-status path must not change).
- `test_rank_on_arrival`: `wpBrowseShow(count, browseMax)` is `min(count, browseMax)` for 1..9 (the live counter and the
  click counter agree).
- Source pin in `test_display_clock.cpp`: `code_only()` of the `WP_DISP` block of `singleClick()` calls `wpBrowseClick(` and
  the `iDisplayType == 0 && bNeu` block of `sendDisplay1306()` sets `wpLiveNewest = true`.

### Bench

Needs a Wireless Paper V1.2 or Vision Master E213. The central bench table (DK5EN-1, -14, -90, -92,
`campaign-release-train-20261115.md`) has none, so ask the operator for the board; until then the host test is the proof.
Sequence on the board (INSTRUMENT build, TEST group, `--airgap on`, max 2 dBm): `--setcont on`; `--injectmsg TEST wp 1`,
`--injectmsg TEST wp 2`, `--injectmsg TEST wp 3`; expect the counter 3 on the panel after the last; `--btn click` (the
INSTRUMENT hook runs the same handler): `[WPBROWSE];slot;<second newest>;rank;2`, panel shows message 2 and the digit 2;
click: rank 1; click: status page; click: the newest with rank 3. The e-paper refresh behaviour (one full refresh per
click) is an eye test and stays the operator's.

### Upstream

Yes. `upstream/dev` has `WP_MSG_BROWSE_MAX 4` (`src/onebutton_functions.cpp:32`), `wpSteps` (`:187-188`), `wpMsgIdx = 0` on
arrival (`src/loop_functions.cpp:3113`) and in `sendDisplayHead()` (`:1822`). Upstream PR; it is the same diff as the fork's.

### Risks

- The live render now draws a digit: one more draw call per arrival on the e-paper path; it is inside the framebuffer
  before the existing `update()`, so no extra refresh.
- `wpLiveNewest` must be cleared on every path that changes the panel (head page, track page, stored message): a missed
  path makes one click skip a message. The track revert (`src/loop_functions.cpp:2804-2817`) already goes through
  `wpShowStoredMessage()`, which clears it. Cover with the not-live equivalence test and the bench sequence.
- E213 shares the path, so it needs the same build check (`pio run -e vision-master-e213`) at the gate.

## 5. Implementation order and file ownership

| Row          | Files                                                                                                                                                         | Host test                                  |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| DISP-PAGE-01 | `src/page_ring.h` (new), `src/onebutton_functions.cpp`, `platformio.ini`, `test/test_page_ring/`, `test/test_display_clock/`, `tools/bench/oled_harness.py`   | `native_page_ring`, `native_display_clock` |
| DISP-PAGE-03 | `src/page_ring.h`, `src/onebutton_functions.cpp`, `src/loop_functions.cpp`, `test/test_page_ring/`, `test/test_display_clock/`                                | `native_page_ring`, `native_display_clock` |
| DISP-PAGE-02 | `src/command_functions.cpp`, optional `src/display_clock_gate.h` and `src/loop_functions.cpp:1828`, `test/test_display_clock/`, `tools/bench/oled_harness.py` | `native_display_clock`                     |
| BTN-OFF-01   | `src/onebutton_functions.cpp`, `src/onebutton_functions.h`, `src/command_functions.cpp`, `test/test_command_setters/`                                         | `native_command_setters`                   |

Collisions: `onebutton_functions.cpp` (01, 03, BTN-OFF-01), `command_functions.cpp` (02, BTN-OFF-01), `loop_functions.cpp`
(03, optional 02), `test_display_clock.cpp` (01, 02, 03). Run DISP-PAGE-01 and DISP-PAGE-03 as one brief (shared header and
test suite), DISP-PAGE-02 and BTN-OFF-01 as two small briefs after it, one writer per file at a time. Gate: the suites above,
`tools/regression.sh --stage 1,2`, and builds of `heltec_v3`, `heltec_t114` (nRF52, needs Rosetta, see `CLAUDE.md`),
`vision-master-e290`, `wireless-paper`, `vision-master-e213`, `t_echo`, `wiscore_rak4631`. After implementation add the new
suite to `docs/test-suite-map.md` and move the rows to the bench table.

## 6. Backlog rows (Implement table, `docs/BACKLOG.md` section 3.2)

Columns Id, Area, Item, Status, Next step. Not yet added to `BACKLOG.md`; the orchestrator inserts them.

| Id           | Area                  | Item                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | Status                                          | Next step                                                                                                                                                                                                                                        |
| ------------ | --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| DISP-PAGE-01 | OLED / button paging  | The OLED click walk stops at ring slot 5 instead of the end of the stored messages (`onebutton_functions.cpp:121`, `:236`), so the browse depth is k + 1 for newest slot k = 0..4 and none for k = 5 (clicks show only the status page), and slot 5 is never shown; the boot click also indexes `pageLastLineAnz[-1]` and can write out of bounds (generic branch); reuse the E290 empty-slot stop for all boards via `src/page_ring.h`. Plan [plan-display-paging-20261121.md](plan-display-paging-20261121.md). Problem file P-0021. | Open 2026-11-21: plan written, not implemented. | Implement `page_ring.h` and `native_page_ring` (the legacy copy kept in the test gives depth 3 for newest slot 2, none for slot 5; includes `test_empty_ring_and_boot_pointer`); one brief with DISP-PAGE-03; bench `depth` scenario on DK5EN-1. |
| DISP-PAGE-02 | OLED / display off    | `--display off` within about 5 s after a browse click is not applied: `sendDisplayHead(false)` returns while `pageHold > 0` (`loop_functions.cpp:1828`) and the panel stays lit; the handler needs `pageHold = 0` like `PressLong()`. Plan [plan-display-paging-20261121.md](plan-display-paging-20261121.md). Problem file P-0384.                                                                                                                                                                                                    | Open 2026-11-21: plan written, not implemented. | One line in the handler (`command_functions.cpp:2049`) plus a source-pin test in `test_display_clock`; bench click then `--display off`, expect blank CRC.                                                                                       |
| DISP-PAGE-03 | Wireless Paper / E213 | The first button press after an arrival redraws the newest message and the live page shows no counter (`onebutton_functions.cpp:187-194`, `loop_functions.cpp:3188`); the first press must go to the next older message and the counter must show on arrival. Plan [plan-display-paging-20261121.md](plan-display-paging-20261121.md). Problem file P-0059.                                                                                                                                                                            | Open 2026-11-21: plan written, not implemented. | Implement `wpBrowseClick()` in `page_ring.h` and the `wpLiveNewest` flag; test `native_page_ring`; bench needs a Wireless Paper or E213 (none on the central bench).                                                                             |
| BTN-OFF-01   | User button           | `--button off` (web switch) keeps the OneButton handlers attached until reboot (`command_functions.cpp:645` has no post hook; `init_onebutton()` only attaches); add `deinit_onebutton()` and `tg_post_button_off`. Plan [plan-display-paging-20261121.md](plan-display-paging-20261121.md). Problem file P-0345.                                                                                                                                                                                                                      | Open 2026-11-21: plan written, not implemented. | Implement the hook and source-scrape tests in `test_command_setters`; bench with a physical press on DK5EN-1 (`--btn click` bypasses the handler wiring).                                                                                        |

## 7. Evidence checklist

- Every path:line in sections 1 to 4 was read at HEAD `067d29cd` (`sed -n` on the lines, `grep -n` for the symbols). Lines
  that differ from the problem files' pin numbers: `command_functions.cpp` button rows `:643`/`:642` to `:645`/`:644`,
  `tg_post_button_on` `:482` to `:484`; `loop_functions.cpp` arrival `:1787`/`:1789` to `:1790`/`:1792`, `wpMsgIdx = 0` `:3188`
  unchanged, `sendDisplayHead` guard `:1825-1826` to `:1828`, `bDisplayIsOff` in the handler `:2022-2023` to `:2035-2036`,
  `sendDisplayHead(false)` `:2036` to `:2049`; `esp32_main.cpp` tick `:3442` to `:3450`; `nrf52_main.cpp` tick `:2050` to `:2068`,
  `init_onebutton` `:851` to `:869`; `esp32_main.cpp` `init_onebutton` `:1980` to `:1988`.
- Commits cited are in `fork-dev` (`merge-base --is-ancestor` rc 0): `ff60d1ee`, `d0ecf82f`, `33b0cd5a`, `a10d7756`, `9785c019`,
  `0b36ea58`, `283e3d56`, `29112ca7`, `189a3c5b`, `dc2decdc`, `ce05f38c`, `9283e6a3`, `f4af55b0`, `8b4d509b`, `96aaa67c72`. None
  is reverted.
- Platforms checked per platform by `#if` reading in each section; not checked: RAK4631 with display (T-Deck, T-Deck Pro and T5 are
  resolved in the section 1 platform table). No hardware, no test run, no build was done.
- Upstream: `git show upstream/dev:<path>` at `5698913a` for `src/onebutton_functions.cpp`, `src/command_functions.cpp`,
  `src/loop_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`. Open upstream PRs were not queried
  (no network use for this plan).

## 8. Open questions for the operator

1. DISP-PAGE-01: show the rank (newest = highest, as on E290 and Wireless Paper) at the top right of OLED pages instead of the
   slot number plus one? Recommended: yes; the slot number cycles 3, 2, 1, 6, 5 once the ring wraps. The alternative keeps the
   old digit and only moves the stop, a smaller diff with the odd sequence.
2. DISP-PAGE-03: draw the counter on a live message on the Wireless Paper (the reporter asks for it), and should the same
   "first click moves on" rule apply to E290 and the OLED boards, where the first click also re-shows the newest page (with a
   number)? Recommended: counter yes; OLED and E290 no, because their live page has no number and the click adds it, and
   nobody reported it.
3. DISP-PAGE-01: keep `PAGE_MAX 6` (5 browsable) or raise the OLED ring? Recommended: keep; a larger ring costs about 221 B of
   DRAM per slot and does not make the OLED match the web history.
