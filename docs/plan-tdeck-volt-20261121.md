# Plan: T-Deck header battery label ignores `--volt` (P-0324, duplicate P-0326)

Date: 2026-11-21. Branch `fork-dev` at `b6b1db7b`. Problem files `P-0324` (herby, 2026-11-17) and `P-0326` (2026-11-18),
same text. Backlog id `TDECK-VOLT-01` (row exists, not edited here). Nothing implemented in this session.

## 1. Symptom

With the battery display set to voltage (`--volt on`, or the "Voltage" switch in the web setup), the T-Deck (Plus) main
screen header still shows the charge as a percentage. The info screen shows the voltage correctly (it always does).
Reported on T-Deck Plus, build 20261114.

## 2. Root cause at HEAD

- `bDisplayVolt` is defined at `src/loop_functions.cpp:412`, declared in `src/loop_functions_extern.h:125`, set by
  `--volt on` / `--proz off` at `src/command_functions.cpp:1500` (also `node_sset |= 0x0001`) and cleared by `--volt off` /
  `--proz on` at `:1518`; restored at boot at `src/esp32/esp32_main.cpp:1542`; web setup `src/web_functions/web_setup.cpp:116`.
- It is read only in the OLED/TFT/e-paper display code: `src/loop_functions.cpp:2278`/`:2285` (`HAS_TFT*`, `BOARD_T_ECHO`
  `%4.1fV`, else `%5.2fV`), `:2368`/`:2375` (same pair, second display function), `:2481` (e-paper), `:3169`. There is no
  read under `src/t-deck/`: `grep -n -i "volt\|proz" src/t-deck/*.cpp src/t-deck/*.h` matches only the function and
  parameter names below, never `bDisplayVolt`.
- The header label is written by exactly one function, `update_header_batt_indicator(float batt, int proz)`
  (`src/t-deck/lv_obj_functions.cpp:2672`): `:2693` clamps the percent, `:2695` `snprintf(percent_text, ..., "%d%%", ...)`,
  `:2696` `label_set_if_changed(header_batt_label, percent_text)`. The USB case (`:2683` `batt <= 0 || batt > 4.2`) returns
  earlier with `"USB"` (`:2687`).
- Callers: header build `:940`, theme change (`header_theme_cb`, `:2810`), once per displayed second from
  `tdeck_update_time_label()` (`:4365`; called from `src/loop_functions.cpp:2879`), and `tdeck_update_batt_label()` (`:4293`,
  `:4328`; called from `src/esp32/loop_actions_esp32.cpp:466` when the voltage in 10 mV steps or the percent changes).
  `tdeck_update_batt_label()` itself writes the info-screen text `"Batt: %.2fV (%i%%)"` (`:4298`/`:4302`), always with both
  numbers; that is why the info screen "correctly" shows volts.
- Units: `global_batt` is mV (`src/batt_functions.cpp:39`, `// in mV`). Every caller into the header passes
  `global_batt / 1000.0f`, i.e. volts, and `update_header_batt_indicator()` compares against `4.2f` V. So `batt` is V here
  (the `tdeck_update_batt_label(float batt, ...)` parameter is volts too, `loop_actions_esp32.cpp:466` divides by 1000).
- Refresh on toggle: `tdeck_update_time_label()` calls the indicator every second and `label_set_if_changed()` (`:2624`)
  skips unchanged text. Once the indicator reads `bDisplayVolt`, `--volt on/off` shows up within one clock tick. No extra
  trigger is needed (unlike the V1.1 e-paper, `loop_functions.cpp:2463-2472`).

## 3. Platforms (checked per platform)

| Platform                                                   | Header code                                                                                                                                             | Affected                                     |
| ---------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| T-Deck (`variants/t_deck`, `BOARD_T_DECK`)                 | `src/t-deck/*` (`+<t-deck/*>`)                                                                                                                          | yes                                          |
| T-Deck Plus (`variants/t_deck_plus`, `BOARD_T_DECK_PLUS`)  | same `src/t-deck/*` (its `build_src_filter` has `+<t-deck/*> -<t-deck-pro/*>`)                                                                          | yes (the reported board)                     |
| T-Deck Pro (`variants/t_deck_pro`, `BOARD_T_DECK_PRO`)     | does NOT use it: `build_src_filter` has `-<t-deck/*> +<t-deck-pro/*>`; its UI is `src/t-deck-pro/ui_deckpro.cpp` (taskbar `:362`/`:364`, `:554`/`:556`) | same defect class, different code, see below |
| Other TFT boards (`HAS_TFT*`, `HAS_TFT_CONNECT`, `T_ECHO`) | `src/loop_functions.cpp:2278`/`:2368` honour `bDisplayVolt`                                                                                             | no                                           |
| OLED boards, e-paper                                       | `:2285`/`:2375`/`:2481` honour it                                                                                                                       | no                                           |
| nRF52                                                      | no T-Deck code (`nrf52_base` excludes `t-deck/*`, `t-deck-pro/*`)                                                                                       | no                                           |

T-Deck Pro: the taskbar battery label (`menu_taskbar_battery_percent`) is `lv_label_set_text_fmt(..., "%d",
ui_battery_27220_get_percent())`, the fuel-gauge percent straight from the BQ27220 (`ui_deckpro_port.cpp:324`), and never
reads `bDisplayVolt`. It is the same symptom on another board, but a different file set, a different data source
(`ui_battery_27220_get_voltage()` returns mV from the gauge, `:318`) and no reporter. Out of scope here; listed under 8.

## 4. Fix design (minimal)

One new pure header, `src/t-deck/tdeck_batt_label.h` (header-only, LVGL-free, same pattern as `tdeck_pattern.h`,
`tdeck_palette.h`, `dm_prefill.h`):

```cpp
struct TdeckBattHdr { bool usb; char text[8]; };
// battV in volts (global_batt / 1000.0f; 0 = no reading). USB rule exactly as today: battV <= 0 || battV > 4.2.
inline TdeckBattHdr tdeckBattHeader(bool showVolt, float battV, int proz);
```

- usb: `text` = `"USB"`.
- else `showVolt`: `"%.2fV"` of `battV` (e.g. `3.87V`, max `4.20V`, 5 chars plus NUL, fits 8).
- else: `"%d%%"` of `proz` clamped to 0..100 (today's output, byte for byte).

`update_header_batt_indicator()` (`lv_obj_functions.cpp:2672`) calls `tdeckBattHeader(bDisplayVolt, batt, proz)`, sets the
label from `.text`, and keeps the USB early return keyed on `.usb`. The icon and its colour stay percent-driven
(`clamped_proz` thresholds `:2701-2719`): the icon is the charge indicator in both modes, only the number changes.
`bDisplayVolt` is already visible there (`loop_functions_extern.h` is included at `:23`). No change to
`tdeck_update_batt_label()`, to `bDisplayVolt` semantics, to the settings bit or to `command_functions.cpp`.

Format choice: `%.2fV`, matching the info screen (`%.2fV`, `:4298`) and the generic OLED rule (`%5.2fV`). The TFT rule
prints one decimal (`%4.1fV`); that is too coarse to read a LiPo discharge curve (3.7 to 4.2 V) and the T-Deck header
has the room (section 7). Open question 1.

Why no helper shared with the OLED rule (the backlog row suggests one): the OLED/TFT code pads to a fixed column width
(`%5.2fV`, `%4d%%`, `"   USB"`), differs per display type (1 vs 2 decimals), and decides USB only on `global_batt == 0.0`
(no `> 4.2 V` rule). A common helper would need a width/decimals/USB-rule parameter set that only the T-Deck uses, and
touching `loop_functions.cpp` (four call sites in the same file other sessions edit) widens the blast radius for no
gain. `grep` for an existing helper found only `battInfoDecide()` (`src/batt_info.h`, unrelated, `--info` text) and
`mv_to_percent()` (`src/batt_functions.h`, mV to percent); nothing formats the label. Open question 2 if the operator
wants the shared version anyway.

## 5. Test (fails before, passes after)

New suite `test/test_tdeck_batt_label/test_main.cpp`, new env `[env:native_tdeck_batt_label]` in `platformio.ini`
(copy of `[env:native_tdeck_pattern]`, `test_filter = test_tdeck_batt_label`, `-I src`, `test_build_src = no`). The stage 1
gate picks it up automatically (`tools/regression.sh:170` greps `[env:native*]`). Update `docs/test-suite-map.md`
counts (suites, cases, environments) in the same commit.

Cases:

1. percent mode: `tdeckBattHeader(false, 3.87f, 55)` text `"55%"`.
2. volt mode: `tdeckBattHeader(true, 3.87f, 55)` text `"3.87V"`, not `"55%"`.
3. USB, both modes: `battV` 0 and 4.25 give `usb` true, text `"USB"`.
4. boundary: `4.2f` is not USB, volt mode `"4.20V"`.
5. percent clamp: 120 gives `"100%"`, -5 gives `"0%"` (today's behaviour, pinned).
6. buffer: longest text (`"4.20V"`, `"100%"`) shorter than `sizeof(text)`.
7. source pin (why a pin: `lv_obj_functions.cpp` needs LVGL, TFT_eSPI and the board toolchain and is compiled by no
   native env, so the wiring cannot run on the host; same pattern as the source pins in `test_command_setters`, using its
   `read_repo_file()` walk-up): read `src/t-deck/lv_obj_functions.cpp`, cut the body of
   `static void update_header_batt_indicator(` up to the next top-level `}` and assert
   `TEST_ASSERT_NOT_NULL(strstr(body, "tdeckBattHeader(bDisplayVolt"))` and
   `TEST_ASSERT_NULL(strstr(body, "\"%d%%\""))`.

Red run, in this order (the unit cases cannot fail by assertion before the header exists; they would only fail to
compile): the implementer first adds the header and the suite, runs
`tools/piolock.sh pio test -e native_tdeck_batt_label` and records the pin as RED
(`test_header_wiring_reads_bdisplayvolt`: `Expected non-NULL`, because `update_header_batt_indicator()` at HEAD
contains `"%d%%"` and no `tdeckBattHeader`), then wires the function and records GREEN. A before-run without that red
line counts as unverified. Mutation check: with the wiring in, replacing the `showVolt` branch by the percent branch in
the header turns cases 2 and 7 red.

Scoped gate: `tools/piolock.sh pio test -e native_tdeck_batt_label`, then board builds
`tools/piolock.sh pio run -e t_deck` and `-e t_deck_plus` (the changed `.cpp` is compiled only there; one `pio` at a time),
`tools/regression.sh --stage 1,2` at the wave gate. Never bare `pio test`.

## 6. Bench step

T-Deck (Plus) on the bench, INSTRUMENT build so `tools/bench/tdeck_screenshot.py` works (`/bench` skill for serial,
flashing and the console login; run `tools/bench/identity_guard.py` first; `lsof <port>` before opening a port; DK5EN-98
is not touched).

1. Flash the fix to the bench T-Deck on battery (not USB; with USB power the label correctly reads `USB` and the check is
   void). Bench TX power at most 2 dBm, own call signs only, test traffic only to group `TEST` or a DM to an own node;
   no transmission is needed for this check at all.
2. `--proz on`, wait 2 s: header shows `NN%` and the percent battery icon. Screenshot:
   `tools/bench/tdeck_screenshot.py <node>.local /path/off.png`.
3. `--volt on`, wait 2 s: header shows `N.NNV` (3.30 to 4.20) and the same icon. Screenshot. Info screen still shows
   `Batt: N.NNV (NN%)`.
4. `--volt off`: back to `NN%` within 2 s. Reboot with `--volt on` persisted: header shows volts after boot.
5. Layout check on the screenshots: label, icon and time do not overlap, icon is not clipped (section 7).
6. Plug USB: header reads `USB` in both modes.

Pass: texts as above in steps 2-6.

## 7. Risks

- Header width. `header_batt_label` (`:888-893`) is created with `"0%"`, `LV_LABEL_LONG_CLIP`, and positioned once with
  `lv_obj_align_to(..., header_time_label, LV_ALIGN_OUT_LEFT_MID, -35, 0)`; the icon is aligned to the label once
  (`:898`). `"4.12V"` is one character wider than `"100%"` and about three wider than the creation text. Whether the
  label grows into the 35 px gap to the clock or shifts the icon is a render question; the screenshot in bench step 5
  decides. If it overlaps, widen the gap or fix the label width (`lv_obj_set_width`) in the same function; no other file.
- `%.2fV` on a battery that reads 4.19 V while charging flips to `USB` above 4.2 V (existing rule, unchanged). In volt
  mode the user sees the number jump to `USB` instead of a percent pinned at 100; same threshold as today.
- Per-second `snprintf` plus string compare: the call already runs every second with the same pattern, no new cost.
- `bDisplayVolt` is written from the command/BLE path and read on the LVGL path of the same Arduino loop; a bool, no new
  threading concern (same as the OLED readers).
- Persistence is untouched (`node_sset & 0x0001`); a node updated from an older build keeps its stored mode, so users who
  already set voltage get it on the T-Deck after the update. That is the intended fix but a visible change for them.
- Other sessions: `src/loop_functions.cpp` is hot (merge in flight on 2026-11-21); this fix does not touch it.

## 8. Upstream impact

Yes, upstream has the same defect. `git show upstream/dev:src/t-deck/lv_obj_functions.cpp`: `update_header_batt_indicator()`
at `:2553`, `snprintf(percent_text, sizeof(percent_text), "%d%%", clamped_proz)` at `:2575-2576`, called at `:848`,
`:2701`, `:4220`, `:4257`; `upstream/dev:src/loop_functions.cpp:403` defines `bDisplayVolt`, read at `:2260`/`:2267`
only (upstream head `4164f180`), `upstream/dev:src/loop_functions_extern.h:122` declares it. The fix ports as is: one new
header, one function edit, one test; `lv_obj_functions.cpp` differs in line numbers only around the function. Per the PR
workflow: land on `fork-dev` first, later cut a PR from `upstream/dev` with a German description (new header plus a
five-line change in `update_header_batt_indicator()`).

## 9. Other findings (not fixed)

1. T-Deck Pro taskbar ignores `--volt` too (`src/t-deck-pro/ui_deckpro.cpp:362-364`, `:554-556`: percent from the BQ27220
   only; `ui_battery_27220_get_voltage()` exists, `ui_deckpro_port.cpp:318`). Same symptom, no ticket. Suggest a backlog
   row if the operator wants it; fix needs the same helper (volts from mV) in a different UI file and bench on a T-Deck Pro.
2. `loop_actions_esp32.cpp:460-467` throttles `tdeck_update_batt_label()` on `labelCv`/percent, not on `bDisplayVolt`; harmless
   for the header (the clock tick refreshes it), noted because it looks like the place to hook a toggle and is not.

## 10. Evidence checklist

- [x] `src/t-deck/lv_obj_functions.cpp:2672`, `:2693-2696`, `:2683-2689`, `:4293-4302`, `:4328`, `:2624` read at HEAD `b6b1db7b`.
- [x] `bDisplayVolt` set sites `command_functions.cpp:1500`/`:1518`, read sites `loop_functions.cpp:2278`/`:2285`/`:2368`/`:2375`/`:2481`/`:3169`
      confirmed with `git show HEAD:src/loop_functions.cpp | grep -n bDisplayVolt`; zero matches under `src/t-deck/`.
- [x] Units: `global_batt` mV (`batt_functions.cpp:39`), header gets V (`/ 1000.0f` at `:940`, `:2810`, `:4365`,
      `loop_actions_esp32.cpp:466`).
- [x] T-Deck / Plus: `variants/t_deck*/platformio.ini` `build_src_filter` `+<t-deck/*>`. T-Deck Pro: `variants/t_deck_pro/platformio.ini`
      `-<t-deck/*> +<t-deck-pro/*>`, own UI in `src/t-deck-pro/ui_deckpro.cpp`. Other TFT/OLED: `loop_functions.cpp` rules.
- [x] Upstream: `git show upstream/dev:src/t-deck/lv_obj_functions.cpp`, `:src/loop_functions.cpp`, `:src/loop_functions_extern.h`.
- [ ] Not verified: header width on the real panel (bench step 5); that no `.cpp` in the T-Deck envs overrides `bDisplayVolt`
      (grep found none); nothing was built or run in this session.

## Operator decisions 2026-11-21

- Open question 1: the T-Deck header shows the voltage with one decimal, `%.1fV` (e.g. `3.9V`), against the
  recommendation.
- Open question 2: one formatting helper shared with the OLED volt display, against the recommendation. The helper takes
  the precision; the OLED keeps two decimals (`%5.2fV`, `loop_functions.cpp` three sites), so OLED boards show no
  visible change.
