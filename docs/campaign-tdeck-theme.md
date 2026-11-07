# Campaign TD-THEME-01: T-Deck light/dark theme

Plan: [plan-tdeck-theme-20261107.md](plan-tdeck-theme-20261107.md). Started 2026-11-07 on `fork-dev`. Bench node
DK5EN-14 (T-Deck Plus).

## Current state

- W1 done 2026-11-07, committed. Next: **W2a** (static colour sites in `lv_obj_functions.cpp`) in parallel with
  **W2b** (`tdeck_main.cpp` init, `tdeck_helpers` night hook and brightness API, logo and lock images).
- Prep commits: `a3cd6db5` (plan moved), `4f253308` (`[env:native_tdeck_theme]` and its ini baseline).

## W1 outcome

- New `tdeck_palette.h` (pure, host-tested), `tdeck_theme.{h,cpp}` (LVGL styles, live apply), setting
  `node_tdeck_theme` (NVS key `node_tdtheme`, U8, default 0 = auto, persisted but not exported like `node_bleon`; the
  web toggle in W3 reads it back), `--theme auto|dark|light` (bare `--theme` shows it, T-Deck and T-Deck Plus only, exact
  lower case), `;theme;N` in `--persiststat`.
- **API for W2** (`tdeck_theme.h`): `tdeck_theme_init(lv_disp_t*)`, `tdeck_theme_set_night(bool)`,
  `tdeck_theme_reapply()`, `tdeck_theme_is_dark()`, `tdeck_theme_pal()`, `bool tdeck_theme_on_change(void (*)(void))`
  (4 slots), shared styles `tdeck_style_*`.
- **Thread safety:** checked by the advisor. `commandAction` (serial, BLE queue, polled web server) and `brightnessTick`
  run on the loop task, which also runs `lv_task_handler`, so no deferred apply is needed.
- **Token deviations from the plan:**
  - Dark `tab_sel` `#D03A55` (plan `#B3203A` had 2.31:1 against `tab_bg`).
  - New `primary` (LVGL theme primary for focus, switches and the slider): `#D03A55` dark, `#A2182F` light. `btn_on`
    stays the button fill.
  - New `warn` and `warn_bg` (no single colour reaches 4.5:1 on both the red header and white).
  - New `info`, `bubble_sys`, and header icon states `hdr_on` `#FFFFFF`, `hdr_off` `#E8A0AC`, `hdr_err` `#FFD166`.
  - `muted` serves hints.
- Advisor: one rework round (primary contrast, header tokens, double tree walk, callback slots), applied by a fresh
  implementer and checked by the orchestrator.

## Scope additions (operator, 2026-11-07)

- **Keyboard-lock glyph** in the header while the keyboard lock is on.
- **Brightness slider** in the T-Deck setup tab, in sync with SYM+B and the TD-23 day/night slots.

Recon for both:

- **Keyboard lock** is `node_keyboardlock` (persisted as `node_kblock`). SYM+K (`tdeck_main.cpp:1082`) and
  `--keylock on|off` (`command_functions.cpp:3134-3145`) toggle it. The header keyboard button toggles something else,
  the keyboard backlight lock (`node_kbllightlock`). The fonts and `lv_symbol_def.h` have no lock glyph, so W2b
  generates a small alpha image `mc_lock_img.c` with the logo script. W2c places it in the header and refreshes it
  from the compare-and-cache in `tdeck_update_header_standby` (runs every loop).
- **Brightness:** the active slot is static in `tdeck_helpers.cpp`. W2b exports
  `uint8_t brightnessActiveLevel()`, `void brightnessSetActiveLevel(uint8_t)` (clamp 1-16, store into the active slot,
  debounced save; never 0, which turns the panel off) and `void brightnessSetChangeCallback(void (*)(uint8_t))`,
  which SYM+B fires. W2d adds the `lv_slider` row between `setup_line_backlight` (`:1439`) and `setup_line_starttone`
  (`:1464`), syncs it in `tdeck_refresh_SET_view` and registers the callback. Drag ticks never go through
  `commandAction` (each call would write to flash).

## Wave map (revised after W0)

Scout A counted 98 colour sites in `lv_obj_functions.cpp`, too many for one brief. W2 is split into serial waves on
that file; a parallel writer handles the other T-Deck files.

| Wave | Agent         | Exclusive files                                                                                                                               | Content                                                               |
| ---- | ------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| W1   | Sonnet medium | new `tdeck_palette.h`, new `tdeck_theme.{h,cpp}`, `meshcom_settings.h`, `settings_schema.h`, `esp32_flash.cpp`, `command_functions.cpp`, test | palette, resolve, parser, styles, `--theme`, setting                  |
| W2a  | Sonnet medium | `lv_obj_functions.cpp`                                                                                                                        | static sites in `setDisplayLayout`, `apply_tab_bar_styles`, bubbles   |
| W2b  | Sonnet medium | `tdeck_main.cpp`, `tdeck_helpers.{h,cpp}`, new `mc_logo_img.c`, new `tools/svg_to_lvgl_logo.py`                                               | theme init, night hook, boot logo asset (parallel with W2a)           |
| W2c  | Sonnet medium | `lv_obj_functions.cpp`                                                                                                                        | runtime sites (header indicators, map points, bubbles), lock glyph    |
| W2d  | Sonnet medium | `lv_obj_functions.cpp`, `event_functions.cpp`                                                                                                 | setup Auto/Dark/Light control, brightness slider, boot logo on screen |
| W3   | Sonnet medium | `web_functions.cpp`, `mc.js`, `web_assets.h` (regenerated), `tools/webgui_settings_test.js`                                                   | web GUI theme toggle                                                  |
| W4   | orchestrator  | bench                                                                                                                                         | DK5EN-14 screenshots per theme, contact sheet                         |

## W0 answers

- **Day/night source:** `brightnessEvalPeriod()` (`tdeck_helpers.cpp:45`) returns DAY early when `--bl auto` is off, so
  the sun is never computed. The transition point is `brightnessTick()` `:120-122`. W2b computes the period regardless
  of `bl auto` (backlight still uses the day level when auto is off) and calls `tdeck_theme_set_night()` on a change.
  No clock or position gives NIGHT_UNKNOWN, and theme auto falls back to dark.
- **Board guard:** `BOARD_T_DECK || BOARD_T_DECK_PLUS`; the T-Deck Pro defines neither.
- **Persistence:** schema-driven NVS (namespace `Credentials`); a missing key keeps the struct default (0 = auto), so no
  migration is needed. nRF52 has no T-Deck members. The new field is persist-only, like `node_bleon`.
- **Lints touched by a new field:** `settings_schema_lint.py`, `settings_persist_lint.py`, `persist_readback_lint.py`,
  `help_parity_lint.py`.
- **Setup tab:** inline in `setDisplayLayout` (`:874-1641`), no toggle helper; dropdown precedent at `:982`.
- **Boot screen:** none exists; version lines go into the "booting..." tab. The theme init goes after
  `lv_disp_drv_register` (`tdeck_main.cpp:507`).
- **Web GUI:** no board-capability endpoint; capability is per endpoint (`mc.js:301-351`). Assets are regenerated by
  `python3 tools/gen_web_assets.py`.
- `tdeck_sdmap.cpp` colours are map data and stay as they are.
