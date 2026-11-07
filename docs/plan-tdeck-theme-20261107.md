# T-Deck light/dark theme: implementation plan

Status: planned 2026-11-07, operator decisions taken, no code written. Execute with `/orchestrate-waves`.
Target bench node: T-Deck Plus **DK5EN-14**. Repo: `MeshCom-Firmware-DEV-Main`, branch `fork-dev`.

## Before you start

- Done 2026-11-07: moved from `~/Desktop/tdeck-theme-plan.md` to `docs/plan-tdeck-theme-20261107.md`,
  update the TD-THEME-01 pointer in `docs/BACKLOG.md` (Status and Reading columns), and commit both before any
  wave is dispatched.
- TD-23 already answers part of open question 1: day/night is a sun-elevation switch at -6 deg with 1 deg
  hysteresis (`src/daylight.h`, test `test_daylight`); the T-Deck Plus has no light sensor.
- A parallel session was working on the tree when this plan was written. The working tree had uncommitted
  edits in `src/web_functions/web_functions.cpp`, `test/test_command_toggles/test_command_toggles.cpp` and
  `tools/webgui_settings_test.js`. Do not dispatch until `git status` is clean or those edits are committed;
  the web GUI and command-test parts of this plan touch the same files.
- Re-verify every `file:line` below with `grep -n` first. They were recorded at HEAD `90a55f52` and may have moved.
- Create a campaign doc `docs/campaign-tdeck-theme.md` from this plan (wave map, ownership table, decisions)
  and keep it and `docs/RESUME.md` current per wave. Add a backlog ID (suggestion: `TD-THEME-01`).

## Operator decisions (2026-11-07)

| Question        | Decision                                                                                                       |
| --------------- | -------------------------------------------------------------------------------------------------------------- |
| Palette         | **Web-match**: same red header in both modes, surfaces and buttons from the web GUI tokens, white status text  |
| Switching       | **Live + auto**: `--theme dark\|light\|auto`, applied immediately, no reboot; `auto` follows day/night         |
| Scope           | **Web GUI toggle**, **on-device toggle** (T-Deck setup tab), **boot logo** (MeshCom node logo on start screen) |
| Not in scope    | No upstream PR planned for now (it can still be cut later); T-Deck Pro (e-paper, 1-bit) excluded               |
| Default (unset) | **Auto** for every node that never set the option                                                              |

## Findings from the recon (HEAD 90a55f52)

- **LVGL 8.3.11**, vendored at `lib/lvgl`. The config in use is `variants/t_deck_plus/lv_conf.h`, the same as
  `variants/t_deck/lv_conf.h` (`LV_COLOR_DEPTH 16`, `LV_THEME_DEFAULT_DARK 1` at :545). `src/t-deck/lv_conf.h` is dead.
- **No explicit theme init.** LVGL sets up its default theme when the display is registered
  (`lib/lvgl/src/hal/lv_hal_disp.c:215-217`: primary BLUE, secondary RED, dark).
- **Hard-coded colours** are almost all in `src/t-deck/lv_obj_functions.cpp` (5167 lines): `lv_palette_*` 37,
  `lv_color_make`/`LV_COLOR_MAKE` 27, `lv_color_black/white` 28, `lv_color_hex` 2.
  - Header and tab bar: `LV_PALETTE_BLUE_GREY` (`:656-699`, `:2627`).
  - Header status text (time, battery, satellites): `LV_PALETTE_LIME` (`:713-732`).
  - Toggle buttons: `#d2d2d2` off, `#00a300` on (for example `:942-943`).
  - Message bubbles: `lv_color_hex` at `:2730`, `:2735`.
  - Map view dims tiles with black at 40% opacity (`:1693-1694`); keep that in both themes or tune it for light.
- A few colours also sit in `tdeck_main.cpp` and `tdeck_sdmap.cpp`.
- **Assets that cannot be recoloured:** `src/t-deck/mouse_cursor_icon.c` (TRUE_COLOR_ALPHA) and the SD map tiles.
  Fonts are 4-bpp alpha and the `LV_SYMBOL_*` glyphs follow the text colour, so they adapt.
- **Day/night today is backlight only** (TD-23): fields `node_bl_day`, `node_bl_night`, `node_bl_auto` at
  `src/meshcom_settings.h:263-266`; commands `--bl auto on|off`, `--bl day|night 1-16` at
  `src/command_functions.cpp:6392-6420`; logic in `src/t-deck/tdeck_helpers.cpp:27-141`;
  doc `docs/TD-23-tdeck-day-night-brightness.md`.
- **Pattern for a persisted setting:** `node_bleon` (`meshcom_settings.h:262`, persisted but not exported per
  `settings_schema.h:156`), handler `--ble on|off` at `command_functions.cpp:6256-6266`, `save_settings()` in
  `src/esp32/esp32_flash.cpp:698` (Preferences/NVS namespace `Credentials`).
- **Web GUI colours** live in `src/web_functions/web/mc.css` (embedded gzip through `web_assets.h`); light at
  `:4-6`, dark at `:7-10` under `prefers-color-scheme`. The header `#head_layer` uses a literal `#a2182f` (`:34`).
  Contrast test: `tools/webgui_theme_test.js`
  (`NODE_PATH=$HOME/.cache/meshcom-jsdom/node_modules node tools/webgui_theme_test.js`).
- **Logo:** none on the T-Deck. The web GUI logo is an inline SVG at `src/web_functions/web_functions.cpp:1579`
  (`id mc_logo`, `#ff0000` nodes on `#000`).
- **Screenshot tool:** `tools/bench/tdeck_screenshot.py <host> <out.png> [--tries 8] [--delay 1.0]`, over HTTP
  `GET /screen.rgb565` (server at `web_functions.cpp:1012`, `:1344-1346`, shadow buffer `src/t-deck/screen_shadow.h`).
  Needs an **INSTRUMENT** build; the first request returns 503 and the tool retries. Output is 320x240 PNG (via `magick`).

## Palette (tokens, mirrors mc.css)

| Token        | Dark      | Light     | Use                                         |
| ------------ | --------- | --------- | ------------------------------------------- |
| `header_bg`  | `#A2182F` | `#A2182F` | header bar, both modes (like `#head_layer`) |
| `header_fg`  | `#FFFFFF` | `#FFFFFF` | time, battery, satellites (replaces lime)   |
| `tab_bg`     | `#3A1D24` | `#FCEDF0` | tab bar, inactive tabs                      |
| `tab_sel`    | `#B3203A` | `#A2182F` | selected tab, focus outline                 |
| `bg`         | `#14161A` | `#FFFFFF` | screen background                           |
| `surface`    | `#1B1E23` | `#F9F9F9` | panels, lists, message area                 |
| `text`       | `#E8E8E8` | `#252323` | body text                                   |
| `muted`      | `#A8ADB5` | `#595959` | secondary text, hints                       |
| `border`     | `#8A8F98` | `#767676` | input and panel borders                     |
| `btn_on`     | `#B3203A` | `#A2182F` | active/primary button                       |
| `btn_off`    | `#3A1D24` | `#FCEDF0` | inactive button                             |
| `btn_fg_off` | `#E8E8E8` | `#252323` | text on inactive button                     |
| `ok`         | `#3FB950` | `#1A7F37` | on/ok states (replaces `#00a300`)           |
| `bubble_in`  | `#22262C` | `#F0F0F0` | received message bubble                     |
| `bubble_out` | `#3A1D24` | `#FCEDF0` | sent message bubble                         |
| `accent`     | `#FF8A9B` | `#A2182F` | red as text on background (`--mcred`)       |

Contrast requirement: at least 4.5:1 for every text-on-background pair, at least 3:1 for borders and state
indicators. The W1 host test enforces this. If a pair fails, adjust the T-Deck token and note it in the campaign
doc. Do not change `mc.css`.

## Architecture

- **`src/t-deck/tdeck_palette.h`** (new, header-only, no LVGL include): `enum TdeckTheme { THEME_AUTO=0,
THEME_DARK=1, THEME_LIGHT=2 }`, `struct TdeckPalette` holding `uint32_t` hex tokens, `const TdeckPalette&
tdeck_palette(bool dark)`, `bool tdeck_theme_resolve_dark(uint8_t setting, bool is_night)`. Pure, so the
  native Unity env can test it.
- **`src/t-deck/tdeck_theme.{h,cpp}`** (new, LVGL): owns shared `lv_style_t` objects (`style_header`,
  `style_tab`, `style_btn_on`, `style_btn_off`, `style_bubble_in`, ...). `tdeck_theme_init(lv_disp_t*)` calls
  `lv_theme_default_init(disp, primary=btn_on, secondary=ok, dark, font)` and fills the styles.
  `tdeck_theme_apply(bool dark)` rewrites the style properties in place, re-inits the default theme and calls
  `lv_obj_report_style_change(NULL)` so every object refreshes live. Widgets use `lv_obj_add_style(obj,
&style_x, ...)` instead of local `lv_obj_set_style_*_color` calls.
- **Setting:** `uint8_t node_tdeck_theme` in `meshcom_settings.h`, NVS key with default `0` (auto). That gives the
  "never set" nodes auto with no migration. Persisted but not exported (same as `node_bleon`) unless the web
  toggle needs it exported; decide in W1 and record the decision.
- **Command:** `--theme dark|light|auto` (T-Deck and T-Deck Plus only; other boards answer "not supported"). It
  saves the setting and calls `tdeck_theme_apply()`. One line of help text.
- **Auto:** re-evaluated where TD-23 already decides day/night (`tdeck_helpers.cpp`). Theme changes only on a
  day/night transition, never every loop iteration.
- **Boot logo:** convert the web SVG (`web_functions.cpp:1579`) to an LVGL `TRUE_COLOR_ALPHA` image
  (`src/t-deck/mc_logo_img.c`, about 64x64, generated by a script under `tools/` that is checked in) and show it
  on the start screen. Red nodes on a transparent background read well on both themes. A scout first finds where
  the start screen is drawn.

## Open questions (resolve during wave prep, record the answers in the campaign doc)

1. **Auto when backlight auto is off:** where does TD-23 take "night" from (clock schedule, sunrise, light
   sensor)? Proposal: theme auto uses the same day/night decision whether or not `--bl auto` is on. If that
   decision is not available without `bl auto`, fall back to dark.
2. **Settings struct on non-T-Deck boards:** the field is shared. Check the flash/NVS layout impact on nRF52
   (RAK4631), and guard only the command and apply code with the T-Deck board macro (verify the macro name).
3. **Web GUI toggle placement:** settings page section for T-Deck nodes only. The JSON source (config/info
   endpoint) must expose the board type and current theme; check for an existing board-capability flag
   (the GPIO switch on the Pins page, commit `90a55f52`, is the closest pattern).
4. **On-device toggle:** where in the setup tab (scout `lv_obj_functions.cpp` for the setup tab builder). Three
   states (Auto/Dark/Light) as a button matrix or dropdown.
5. **Live restyle coverage:** objects created with local style setters but not migrated to shared styles will
   not switch live. The W3 screenshot pass on every screen in both themes is the check.

## Waves

### Wave 0: prep (read-only, Haiku scouts, parallel)

- Scout A: list every colour call in `lv_obj_functions.cpp` grouped by screen/builder function with line
  ranges, plus the setup tab builder and start screen location (questions 4 and boot logo).
- Scout B: TD-23 day/night source (question 1), board macro name, settings persistence path for a new `uint8_t`
  (question 2), web GUI board-capability flag and the GPIO switch pattern (question 3).
- Orchestrator records the answers in `docs/campaign-tdeck-theme.md`. No writes to source.

### Wave 1: theme core, setting, command (1 implementer)

| Agent | Model / effort | Exclusive files                                                                                                                                                                                                             |
| ----- | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W1    | Sonnet medium  | new `src/t-deck/tdeck_palette.h`, new `src/t-deck/tdeck_theme.{h,cpp}`, `src/meshcom_settings.h`, `src/settings_schema.h`, `src/esp32/esp32_flash.cpp` (NVS key), `src/command_functions.cpp`, new `test/test_tdeck_theme/` |

- Acceptance: palette contrast test, `tdeck_theme_resolve_dark()` truth table, and `--theme` parsing
  (dark/light/auto/invalid) pass in the native env. Compile check:
  `pio test -e <native_env> --without-testing` (never a bare `pio test`). T-Deck Plus env builds.
- `tdeck_theme.cpp` compiles but is not yet called (W2 wires it in). The brief must say so, and the
  artifact check is deferred to W2.
- Advisor pass (Opus medium) before commit: it changes behaviour (new setting and command).

### Wave 2: apply the theme (2 implementers, parallel, disjoint)

| Agent | Model / effort | Exclusive files                                                                                                                                                                                                                                                                                    |
| ----- | -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W2a   | Sonnet medium  | `src/t-deck/lv_obj_functions.cpp` only: migrate all colour sites to the shared styles/tokens, add the on-device Auto/Dark/Light control in the setup tab, show the boot logo on the start screen                                                                                                   |
| W2b   | Sonnet medium  | `src/t-deck/tdeck_main.cpp` (call `tdeck_theme_init` after display registration), `src/t-deck/tdeck_helpers.cpp` (auto hook on day/night transition), `src/t-deck/tdeck_sdmap.cpp`, `src/t-deck/event_functions.cpp`, new `src/t-deck/mc_logo_img.c`, new `tools/svg_to_lvgl_logo.py` (or similar) |

- Size check: W2a is the heavy brief (about 90 colour sites, 5k-line file). If Scout A shows more than about
  25 minutes of work, split the boot logo and setup control out of W2a into a follow-up serial wave on the same
  file. Never run two writers on `lv_obj_functions.cpp`.
- Hotspot carve-out: if both agents need a declaration in `tdeck_theme.h`, the orchestrator adds it at the gate.
  `tdeck_theme.h` is owned by nobody in W2.
- Shared resource: only one agent builds the T-Deck Plus env (W2a). W2b uses a compile check of its own files
  or the plain T-Deck env. Every `pio` call goes through the scratchpad lock wrapper (`piolock.sh`); one `pio`
  process at a time.
- Artifact proof: `strings` on the built `firmware.elf`/`.bin` for the `--theme` help text and a symbol check
  (`nm`) for `tdeck_theme_apply` and the logo image descriptor.
- Advisor pass before commit.

### Wave 3: web GUI toggle (1 implementer, serial after W2, only if the tree is free of the parallel session's edits)

| Agent | Model / effort | Exclusive files                                                                                                                                             |
| ----- | -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W3    | Sonnet medium  | `src/web_functions/web_functions.cpp`, the web asset sources it serves (settings page JS/HTML), regenerated `web_assets.h`, `tools/webgui_settings_test.js` |

- Shown only for T-Deck boards; three-state control that sends `--theme ...`; reflects the current value.
- Acceptance: `tools/webgui_settings_test.js` and `tools/webgui_theme_test.js` green; web assets regenerated by the
  project's own script, never hand-edited gzip.
- Advisor pass before commit.

### Gate (orchestrator, after each wave)

1. `ls` every file the wave should have produced.
2. Apply hotspot edits.
3. `tools/regression.sh --stage 1,2`.
4. Build every env that compiles `src/t-deck/` (T-Deck, T-Deck Plus) plus one nRF52 (RAK4631) and one other
   ESP32 env for the shared settings field, sequentially. Compare RAM/flash regions with the previous commit
   (ESP32 images are not byte-reproducible).
5. Read the diff, spot-check the sharpest claim, commit with explicit paths on `fork-dev`.

### Wave 4: bench verification on DK5EN-14 (orchestrator, serialized)

- Load `/bench` first. Run `tools/bench/identity_guard.py`. `lsof <port>` before opening a serial port. No radio
  traffic is needed: take the node off the air with `--airgap on` (INSTRUMENT build). Never touch DK5EN-98.
- Build INSTRUMENT T-Deck Plus, flash DK5EN-14.
- For each theme (`--theme dark`, `--theme light`, `--theme auto` at day and simulated night): capture every
  screen and tab with `tools/bench/tdeck_screenshot.py DK5EN-14 <out>.png`. Save to
  `tools/bench/runs/harness/tdeck-theme-<date>/`.
- Check: the switch is live with no reboot, nothing stays in the old colours, the boot logo is visible on both
  themes, the header matches `#A2182F`, the map view stays readable in light mode.
- Build a side-by-side contact sheet (dark versus light per screen) and show it to the operator for sign-off.
- Commit the summaries the docs cite, delete raw logs, keep `git status` clean.

## Estimate

| Wave         | Wall-clock | Cost (rough) |
| ------------ | ---------- | ------------ |
| W0 scouts    | 5 min      | < $1         |
| W1 + advisor | 30 min     | $4-6         |
| W2 + advisor | 35 min     | $7-10        |
| W3 + advisor | 25 min     | $4-6         |
| Gates        | 30 min     | $2-3         |
| W4 bench     | 30 min     | $2-4         |
| **Total**    | **~2.5 h** | **$20-30**   |

## Bans for every brief

- No git commands, no worktrees, no whole-repo formatters, no edits outside the exclusive file set.
- Never a bare `pio test`; native envs only for tests; one `pio` process at a time through the lock wrapper.
- No flashing, no serial port, no radio transmission inside writer waves (bench is W4, orchestrator only).
- Never change `mc.css`, `REVIEW.md`, lint/CI config.
- Report in at most 15 lines: files touched, verification commands and their pass/fail line, deviations,
  open questions.
