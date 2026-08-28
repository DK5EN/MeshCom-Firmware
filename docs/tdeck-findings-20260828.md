# T-Deck Plus — measured findings (2026-08-28, evening session)

Successor to [`tdeck-handover.md`](tdeck-handover.md). Everything here was measured on DK5EN-14
over USB serial with `tools/bench/tdeck_harness.py`; nothing is from reading code alone. Raw logs
are `tools/bench/tdeck_run_*.log` (untracked), JSON summaries in the session scratchpad.

## 0. Exec summary

| Topic                   | Status                                                                                                                                                                                                                                             |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Laggy UI (P1)           | **Cause located.** `update_header_batt_indicator()` rewrites two header labels every 500 ms from `esp32loop()`; with `full_refresh=1` each rewrite is a 57 ms full-screen SPI push. Idle: 2.5 repaints/s, 193 kpx/s.                             |
| Partial refresh         | **Works without band-aids** for tabs, drawer and incoming messages on an awake display. Idle pixel traffic 14x lower (13.9 kpx/s), mean refresh 7.7 ms.                                                                                            |
| "Messages don't redraw" | **Not reproduced** on an awake display: `msg_tabs_add_message()` invalidates the whole tab area itself. Leading hypothesis (H-R3): the display **wake path** (`setBrightness()` -> `tft.init()` = software reset) never invalidates the screen. |
| Heap                    | ~575 B internal + ~2.6 KB PSRAM per received message over the first 20 messages. 120-message run pending (see §4).                                                                                                                                |
| Audio                   | Built-in tones play and are logged; missing file is now an explicit `[AUDIO];err;missing`. Empty `node_audio_msg` produces a misleading `file / not found` line on every message before the CW fallback.                                         |
| Boot                    | Device ignores serial for ~11 s after `CLIENT STARTED` (map tile load + LoRa init). Boot messages block 2 s each in `addMessage()`.                                                                                                                |

## 1. Instrumentation now in the firmware (commit `6fa0c581`, `15ab3897`)

| Command                        | Output                                                                                          |
| ------------------------------ | ----------------------------------------------------------------------------------------------- |
| `--redrawlog on/off`           | `[REDRAW];ms;..;obj;..;cls;..;area;x1;y1;x2;y2;ra;..;bt;pc,pc,..[;name;..]` per invalidation, `[REFR]`/`[REFRSTART]` per refresh |
| `--uistat`                     | tab, drawer, object count, msg_list children, inv/refr totals, heap                             |
| `--tab list` / `--tab <n>`     | tab names; switch tab                                                                           |
| `--drawer on/off`              | open/close the tab drawer                                                                       |
| `--injectmsg <dst> <text>`     | queue a text message exactly like the LoRa RX path (same deferred slot, same tone)              |
| `--playtone start/msg/<file>`  | built-in tones or SD file, explicit errors                                                      |

The invalidation trace is a 6-line weak hook in `lib/lvgl/src/core/lv_obj_pos.c`
(`lv_obj_invalidate_area`); the backtrace uses `esp_backtrace_get_next_frame`. Host side
symbolizes with `xtensa-esp32s3-elf-addr2line`.

## 2. Baseline (`full_refresh=1`, commit `15ab3897`)

Run `run1.json`, idle 60 s, tab 0, display awake.

| Metric                      | Value                                              |
| --------------------------- | -------------------------------------------------- |
| Refreshes / s               | 2.52, every one 76 800 px (full screen)            |
| Mean / max refresh          | 56.9 ms / 61 ms (`monitor_cb`, render + flush)     |
| Flush (`INSTR-FLUSH`)       | avg 36.7 ms, max 36.8 ms                           |
| Invalidations / s           | 32                                                 |
| Top invalidator (100 %)     | `update_header_batt_indicator()` <- `tdeck_update_batt_label()` <- `esp32loop()` (`esp32_main.cpp:3386`): two `lv_label_set_text` + one `lv_obj_set_style_text_color` per tick, unconditionally, even when the text is unchanged |
| Loop (`INSTR-LOOP`)         | avg 6.6 ms, max 74 ms                              |
| Tabs 0-7                    | all repaint, 384-691 kpx per switch                |
| Drawer                      | 384 kpx per toggle                                 |
| Injected messages           | all displayed; 3-6 refreshes, 70-140 invalidations each |

## 3. Experiment: `full_refresh=0`, nothing else (branch `tdeck-partial-refresh-trace`)

Run `run_partial.json`, idle 30 s.

| Metric                 | full_refresh=1 | full_refresh=0 | Factor |
| ---------------------- | -------------- | -------------- | ------ |
| px / s idle            | 193 280        | 13 859         | 14x    |
| Mean refresh           | 56.9 ms        | 7.7 ms         | 7.4x   |
| Max refresh            | 61 ms          | 13 ms          |        |
| Tabs repainted         | 8/8            | 8/8 (19-269 kpx) |      |
| Drawer repainted       | 6/6            | 6/6 (85-95 kpx)  |      |
| Injected msgs displayed| 4/4            | 4/4            |        |

Trace after an injected message (partial build): `msg_tabs_add_message()`
(`lv_obj_functions.cpp:2354`) -> `lv_obj_clear_flag` invalidates area 5,40-314,193 (the whole tab
content), followed by a 76 800 px refresh and three partial ones. The message path announces
itself correctly. **§3.2 of the handover is not reproduced with the display awake.**

## 4. Open hypotheses and their tests

| ID   | Claim                                                                                                                                  | Test                                                                                       | Status  |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | ------- |
| H-R3 | After `tft_off()` (DISPOFF+SLPIN, 30 s timeout) the wake path `setBrightness()` -> `tft.init()` (SWRST) leaves the panel content undefined and nothing invalidates the LVGL screen; `full_refresh=1` masked it via the 500 ms battery tick | `sleep` scenario: `--tft off`, inject, `--tft on`, expect no full refresh; `--screencrc` before/after vs forced repaint | pending |
| H-P1a| Idle repaint rate is set by the battery header, not by widget count                                                                    | confirmed by backtrace (100 % of idle invalidations)                                       | confirmed |
| H-H1 | Internal-heap growth per message is unbounded (no trim) -> maintainer's heap report                                                    | 120-message injection, `--heap` before/after, look for plateau at the 50/60 trim          | running |
| H-A1 | I2S output path intact, only file decode fails                                                                                          | built-in tones audible and logged                                                          | confirmed |

## 5. Bench facts learned

- Opening the USB port resets the device; the harness holds one session and waits until
  `--uistat` answers (~11 s after `CLIENT STARTED`).
- The firmware echoes the command without a newline, so replies arrive glued
  (`--uistat[UISTAT];...`); the parser skips to the first `[`.
- `__builtin_return_address(0)` inside the invalidate hook always points at `lv_obj_invalidate`;
  the ESP-IDF backtrace walker is needed to reach user code.
