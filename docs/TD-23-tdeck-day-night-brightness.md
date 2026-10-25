# TD-23 T-Deck Plus: saved brightness, day/night auto switch, settings button

Handoff for a coding agent in a separate git worktree. Written 2026-10-25 against `fork-dev` at
`eb246050`. Defect ID proposal: **TD-23** (next free after TD-22 in `docs/BACKLOG.md`).

## BLUF

Make the T-Deck (Plus) backlight brightness persistent and let it switch automatically between a
**day level** and a **night level** based on the sun's position (GPS/stored position + clock).
SYM+B keeps working as today and sets the level of whichever period is currently active. A
checkable toggle on the setup (settings) tab turns the auto switch on and off. Both are saved.

Dark colour scheme: **deferred** (out of scope, see end).

## Operator decisions (2026-10-25)

| Item                            | Decision                                                                  |
| ------------------------------- | ------------------------------------------------------------------------- |
| Saved setting + settings button | Must                                                                      |
| Auto switch                     | Yes: night brightness vs. day brightness, both set via SYM+B for now      |
| Light sensor                    | None on T-Deck Plus (the LTR-553 is T-Deck Pro only); auto = sun position |
| SYM+B steps                     | Finer: 16, 12, 8, 4, 2, 1 (then wrap to 16), see "SYM+B step table"       |
| Dark theme                      | Deferred                                                                  |
| Upstream merge-conflict risk    | Accepted                                                                  |
| Flash/RAM cost                  | Accepted                                                                  |
| Bench test                      | Operator does it as UAT after implementation; agent does host gates only  |

## Ground rules for this job (read first)

- **A parallel session is working in the main checkout** `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`
  on `fork-dev`. Do not edit, stage, commit, checkout or stash anything there. Do not touch its
  `docs/BACKLOG.md`, `docs/RESUME.md` or campaign docs (they have uncommitted edits).
- Create your own worktree, pinned explicitly to current `fork-dev` (the Agent tool's automatic
  `isolation: worktree` has branched from stale upstream bases in this repo before):
  ```sh
  cd /Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main
  git worktree add ../MeshCom-TD23 -b feature/td23-brightness fork-dev
  ```
- The worktree has **no `.claude/`** (gitignored, private). Skills like `/full-regression`,
  `/build-firmware` are not present there; use `tools/regression.sh` directly. Never re-track
  `.claude/`.
- **One `pio` process at a time across both sessions** (shared build cache and global package
  store). Route every `pio` call through a mkdir lock, e.g. a wrapper that loops on
  `mkdir /tmp/meshcom-pio.lock` (pid file inside, free the lock when the pid is gone, `trap` to
  remove it on exit), then runs `"$@"`. If the parallel session uses a different lock path, ask the
  operator before running pio.
- **Never run bare `pio test`** (it walks board envs and flashes attached hardware). Only
  `pio test -e native_<env>` and `tools/regression.sh --stage 1,2`.
- No flashing, no serial ports, no bench. The operator flashes and tests (UAT).
- No git commit unless the operator asks; at the end report and offer to commit on
  `feature/td23-brightness`. Stage explicit paths only.
- English in code, comments and docs. Match the surrounding code style.

## Current state (facts, verified 2026-10-25)

- Display: 2.8" IPS LCD ST7789 (not OLED), backlight on `TDECK_TFT_BACKLIGHT` = GPIO 42
  (`variants/t_deck_plus/configuration.h:66`).
- Dimming exists: the backlight driver takes pulse counts, 16 hardware levels.
  `src/t-deck/tdeck_helpers.cpp`:
  - `setBrightness(value)` (line ~46): 0 = off (TFT sleep), 1..16 via pulse count, wake path
    re-inits the TFT; also syncs the keyboard backlight when `node_kbllightlock`.
  - `cycleBrightness()` (line 30): `+BRIGHTNESS_STEPS/BRIGHTNESS_LEVELS` = +4, wraps via
    `value % 16`, so it cycles 4 -> 8 -> 12 -> 16 -> 4. Levels 1..3 are unreachable today.
  - `current_brightness_level` (0 at boot), `pre_sleep_brightness_level` (16), `resetBrightness()`
    restores `pre_sleep_brightness_level`.
  - `BRIGHTNESS_STEPS 16`, `BRIGHTNESS_LEVELS 4` in `src/t-deck/tdeck_helpers.h`.
- SYM+B -> `cycleBrightness()` in `src/t-deck/tdeck_main.cpp:~1100` (gated by `!node_keyboardlock`).
- Wake: `tft_on()` in `src/t-deck/lv_obj_functions.cpp:~2195` calls `resetBrightness()`, i.e.
  every wake (keypress, incoming message) restores `pre_sleep_brightness_level`. `tft_off()` calls
  `setBrightness(0)`.
- Brightness is **not persisted**: every reboot comes up at 16.
- T-Deck UI settings live in `MESHCOM_SETTINGS_MEMBERS_TDECK` (`src/meshcom_settings.h:~229`) and
  are persisted via `SETTINGS_PERSIST_ONLY_LIST_PLATFORM` (`src/settings_schema.h:~131`, NVS key in
  the first column; ESP32 NVS keys max 15 chars). They are dumped on the console in the
  `[PERSIST];ui;...` line (`src/command_functions.cpp:~5746`).
- Golden lints that list every persisted field and will fail on a new one until updated:
  `test/golden/settings_persist_lint.py` (`_TDECK` rows ~line 216),
  `test/golden/settings_schema_lint.py` (`PERSIST` map ~line 268),
  `test/golden/persist_readback_lint.py` (readback map ~line 52; check whether it needs a row).
  NVS round-trip test: `test/test_esp32_settings_nvs/test_esp32_settings_nvs.cpp` (uses
  `node_mute` as T-Deck example).
- Setup tab toggles: `btn_wifi`, `btn_gps`, `btn_soundon` are checkable buttons on
  `setup_line_callsign` (`src/t-deck/lv_obj_functions.cpp:~840-880`), grey default / green
  checked, shared handler `btn_event_handler_setup_btn` in `src/t-deck/event_functions.cpp`
  (calls `save_settings()`).
- Clock: `cetClockTrusted(bRTCON, posinfo_fix, bNTPDateTimeValid)` and `cetNowUtc(&now_utc)` in
  `src/cet_age.h` (clock side needs `#define CET_AGE_WITH_CLOCK` before the include; pure part is
  host-testable, see `test/test_cet_age`).
- Position: `meshcom_settings.node_lat/node_lon` (unsigned magnitude) + `node_lat_c/node_lon_c`
  hemisphere chars; `nodeSignedLat()/nodeSignedLon()` in `src/node_position.h`. Validity pattern:
  `compute_locator_from_settings()` in `lv_obj_functions.cpp:~2558` (hemisphere char must be
  N/S and E/W, and not 0/0).

## Target behaviour

1. **Two saved levels**: `day` and `night`, each 1..16.
2. **Auto switch on**: once a minute (and at boot after settings load) compute whether it is day or
   night at the node position. On a period change, set the backlight to that period's level
   (only if the panel is on) and always update `pre_sleep_brightness_level`, so the next wake uses
   the right level.
3. **SYM+B** steps through the new table 16 -> 12 -> 8 -> 4 -> 2 -> 1 -> 16 (operator decision,
   replaces today's 4 -> 8 -> 12 -> 16) and stores the resulting level into the **active
   period's** slot (auto on) or the day slot (auto off). Saved to flash.
4. **Auto switch off**: the day slot is the one saved brightness; reboot restores it. Behaviour is
   otherwise exactly today's.
5. **Fallback**: no trusted clock or no valid position -> treat as day (no switching). Log once
   when the fallback starts and ends.
6. **Settings button**: checkable toggle on the setup tab, checked = auto on. State loaded from the
   setting when the tab is built; toggling saves and applies immediately (re-evaluate the period
   and set the level).
7. Wake-on-message, keyboard lock, standby (`node_backlightlock`) and the keyboard-backlight sync
   keep working unchanged.

## Implementation plan

### 1. Settings (3 new T-Deck fields)

In `MESHCOM_SETTINGS_MEMBERS_TDECK` (append at the end to keep the persisted layout of older
fields stable):

| Member          | Type | Default | NVS key        | Meaning                         |
| --------------- | ---- | ------- | -------------- | ------------------------------- |
| `node_bl_day`   | int  | 16      | `node_blday`   | day level 1..16                 |
| `node_bl_night` | int  | 4       | `node_blnight` | night level 1..16               |
| `node_bl_auto`  | bool | false   | `node_blauto`  | auto day/night switch on or off |

- Default `auto=false` keeps today's behaviour for everyone after the update (upstream-friendly).
- Add the three `X(...)` rows to `SETTINGS_PERSIST_ONLY_LIST_PLATFORM`.
- Clamp in `src/settings_sanitize.cpp` (`sanitize_loaded_settings()`): levels outside 1..16 -> default.
  Level 0 must never be stored (0 = panel off).
- Update the three golden lints, extend `test_esp32_settings_nvs` with one of the new fields if it
  covers T-Deck fields generically, and extend the `[PERSIST];ui` console line with
  `blday;%d;blnight;%d;blauto;%d` (the operator's UAT reads this over the 2323 console).

### 2. Pure day/night logic (new header, host-tested)

New `src/daylight.h` (pure, no Arduino includes, `static inline`, style like `src/cet_age.h`):

- `double sun_elevation_deg(uint32_t unix_utc, double lat_deg, double lon_deg)`: NOAA / Meeus
  low-precision solar position (declination + equation of time + hour angle). +-0.5 deg is plenty.
- `bool daylight_is_day(uint32_t unix_utc, double lat, double lon, bool was_day)`: day if
  elevation > threshold, with hysteresis (e.g. +-1 deg around the threshold based on `was_day`) so
  it does not flap at dusk.
- Threshold: civil twilight **-6 deg** (screen stays at day level until it is actually getting
  dark). Make it a named constant; the operator may want -0.833 (geometric sunset) instead, so
  mention the choice in the report.
- `uint8_t brightness_slot_target(...)`/small policy helper if it keeps `tdeck_helpers.cpp` thin:
  given auto flag, clock-trusted flag, position-valid flag, is_day -> which slot (day/night) is
  active. This is what SYM+B writes into and what the 60 s tick applies.

### 3. Firmware wiring (`src/t-deck/`)

- `tdeck_helpers.cpp/.h`:
  - `brightnessApplySaved()` at boot (after settings load): set `pre_sleep_brightness_level` to
    the active slot's level. Find where the first `setBrightness/tft_on` happens after boot in
    `tdeck_main.cpp` and make sure it uses it.
  - `brightnessTick()` called once per 60 s from the T-Deck loop (look at how existing periodic
    UI work is scheduled in `tdeck_main.cpp`, use a `millis()` interval, not a new task). On a
    period change: update `pre_sleep_brightness_level`; if the panel is on
    (`current_brightness_level > 0 && !tft_is_sleeping`) call `setBrightness(level)`.
  - `cycleBrightness()`: replace the `+ BRIGHTNESS_STEPS / BRIGHTNESS_LEVELS` arithmetic with the
    step table (see "SYM+B step table" below); after setting the level, write it into the active
    slot and request a save. `BRIGHTNESS_LEVELS` becomes unused; remove it if nothing else uses it.
- **SYM+B step table** (operator decision 2026-10-25): `16, 12, 8, 4, 2, 1`, descending, wrap
  1 -> 16. Put it as a pure helper next to the day/night logic, e.g.
  `uint8_t brightness_next_step(uint8_t cur)` in `src/daylight.h` (or a small
  `src/t-deck/brightness_steps.h` if that reads cleaner). A current level that is not in the
  table (e.g. 10, set via `--bl` or an old value) steps to the next lower table entry (10 -> 8);
  0 (panel off) steps to 16. The pulse-count driver in `setBrightness()` already handles any
  1..16 value, so no driver change is needed; check that the pulse arithmetic is correct for the
  1 and 2 levels (it counts `(16 + to - from) % 16` pulses, verify downward and 1 -> 16 wrap).
- **Flash wear**: SYM+B is pressed several times in a row. Debounce the save (e.g. save 5 s after
  the last press, from the same tick or a one-shot `lv_timer`), do not `save_settings()` per press.
- Clock + position: include `cet_age.h` with `CET_AGE_WITH_CLOCK`, use `cetDeviceTrusted()` and
  `cetNowUtc()`; position via `nodeSignedLat/Lon` with the validity check above.
- Console log line on every applied change (bench observability, keep the existing `[TFT];...`
  format family):
  `[TFT];bl;period;day|night|fallback;level;%d;auto;%d;elev;%.1f`
- Setup tab: add a checkable `btn_blauto` right of `btn_soundon` on `setup_line_callsign`, same
  grey/green styling and the same handler `btn_event_handler_setup_btn`. Label: there is no moon
  glyph in the built-in LVGL symbols; use text such as `"A"` / `"DN"` or check
  `src/Fonts` for an available icon. Check the row still fits in 320 px with the existing buttons
  (look at the widths used); if not, put it on the next setup line and say so in the report. In the
  handler: set `node_bl_auto`, `save_settings()`, call `brightnessTick()` forced.
- The T-Deck Pro shares `MESHCOM_SETTINGS_MEMBERS_TDECK` but has its own UI (`src/t-deck-pro/`,
  `cycleBrightness` there is a TODO). Fields exist there too; no Pro UI work in this job. Make sure
  the Pro build still compiles.

### 4. Optional console command (recommended, small)

For UAT without touching the screen: `--bl` (show), `--bl auto on|off`, `--bl day N`,
`--bl night N` (N 1..16). Follow the pattern in `src/command_toggles.h` /
`src/command_setters.h` and their native tests (`native_command_toggles`, `test_command_setters`).
T-Deck-only (`#if defined(BOARD_T_DECK) || ...`). If it turns out bigger than ~1 h, skip it and say
so.

## Tests (host only)

- New `[env:native_daylight]` in `platformio.ini` (copy a small existing native env such as
  `native_rtc_offset`) + `test/test_daylight/test_daylight.cpp`:
  - Sun elevation / sunrise-sunset for at least: Munich (48.14 N, 11.58 E) at both solstices and
    an equinox; a southern-hemisphere site (e.g. Sydney); a site west of Greenwich (negative lon);
    reference values from the NOAA solar calculator, cite them in a comment, tolerance +-3 min.
  - Polar day / polar night (e.g. Tromsoe 69.65 N in June and December): no crash, constant period.
  - Hysteresis: elevation oscillating around the threshold does not flip every call.
  - Policy: auto off -> day slot; untrusted clock -> day slot (fallback); invalid position ->
    fallback; auto on + night -> night slot; SYM+B writes the active slot.
  - Clamp: stored 0 or 17 is sanitised to the default.
  - Step table: 16 -> 12 -> 8 -> 4 -> 2 -> 1 -> 16; off-table values (10 -> 8, 3 -> 2, 0 -> 16).
- Add the env to whatever list `tools/regression.sh` / `docs/test-suite-map.md` use to enumerate
  native envs (check how `native_*` envs are discovered; update `docs/test-suite-map.md`).
- Gates (through the pio lock):
  1. `pio test -e native_daylight`
  2. `tools/regression.sh --stage 1,2` (all native envs, golden selftest incl. the settings lints,
     pytest and tool suites); passed count must equal collected count.
  3. Board builds: T-Deck Plus env, T-Deck env and T-Deck Pro env (find exact names in
     `platformio.ini`), plus one non-T-Deck ESP32 board (e.g. Heltec V3) to prove the `#if` guards.
     Report RAM/flash delta for the T-Deck Plus build (ESP32 images are not byte-reproducible;
     compare regions, not hashes).
- Logs go to `tools/bench/runs/harness/` (raw `*.log` is gitignored), never the repo root.

## Docs (in the worktree branch only)

- `docs/BACKLOG.md`: new row TD-23 (note: the main checkout has uncommitted edits to this file;
  the row will be merged later, keep the change small and at the end of the TD block).
- `docs/test-suite-map.md`: new env.
- Run `npx --yes prettier@3 --write <file>` on every edited `.md`.

## Done criteria

- Host gates green, board builds green (T-Deck Plus, T-Deck, T-Deck Pro, one non-T-Deck ESP32).
- Diff limited to: `src/meshcom_settings.h`, `src/settings_schema.h`, `src/settings_sanitize.cpp`,
  `src/daylight.h`, `src/t-deck/tdeck_helpers.*`, `src/t-deck/tdeck_main.cpp`,
  `src/t-deck/lv_obj_functions.cpp`, `src/t-deck/event_functions.cpp`,
  `src/command_functions.cpp` (PERSIST line, optional `--bl`), golden lints, new test + env, docs.
- Report BLUF to the operator: what is in place, deviations (button placement, threshold choice,
  `--bl` done or skipped), RAM/flash delta, and the UAT checklist below. Offer to commit.

## UAT checklist for the operator (bench, after flashing)

1. Fresh boot with auto off: brightness = last SYM+B level (not 16). `[PERSIST];ui` shows the
   saved values.
2. SYM+B steps 16 -> 12 -> 8 -> 4 -> 2 -> 1 -> 16; levels 2 and 1 are visibly dimmer than 4.
   SYM+B several times, reboot: level restored; only one flash save per burst.
3. Toggle auto on in the setup tab: `[TFT];bl;period;...` line, level follows the current period.
4. Night test: set a night level with SYM+B after dark (or fake the clock/position via `--bl`
   / settings), reboot at night -> night level; day -> day level.
5. Panel off / standby / wake on incoming message: wakes at the active period's level.
6. Keyboard lock and keyboard-backlight sync behave as before.
7. No GPS fix and no NTP: fallback line, day level, no switching.

## Out of scope / deferred

- **Dark colour scheme** (deferred by the operator). For later: LVGL's dark default theme is already
  on (`LV_THEME_DEFAULT_DARK 1`, `variants/t_deck_plus/lv_conf.h:545`), but about 90 hard-coded
  colours in `src/t-deck/lv_obj_functions.cpp` (white text areas and tables, light message bubbles,
  grey toggles) override it. Estimate 1.5-2 days for a light/dark palette table plus runtime
  re-apply; map tiles stay bright unless recoloured.
- T-Deck Pro UI (has an LTR-553 light sensor; real ambient-light auto would fit there later).
- Upstream PR (German body, `/submit-pr`) after UAT.
