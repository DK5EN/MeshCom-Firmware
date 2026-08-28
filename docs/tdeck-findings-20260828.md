# T-Deck Plus — measured findings (session 2026-08-28/29)

Successor to [`tdeck-handover.md`](tdeck-handover.md). Everything here was measured on DK5EN-14
over USB serial with `tools/bench/tdeck_harness.py` plus the operator's eyes where the panel had to
be read; nothing is from reading code alone. Raw logs: `tools/bench/runs/tdeck_run_*.log`
(untracked). Research companion: [`tdeck-lvgl-agent-guide.md`](tdeck-lvgl-agent-guide.md).

## 0. Exec summary

| Topic                   | Status                                                                                                                                                                                                                                                                                                                        |
| ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| "Messages don't redraw" | **Root cause found and a mitigation verified (0/30 -> 30/30).** The first display transfer after an SD-card access on the shared SPI bus is lost by the panel. The message path does an SD lookup for the tone file and flushes the new bubble ~100 ms later, in the same loop pass. Not an LVGL invalidation problem at all. |
| Laggy UI (P1)           | Cause located: `update_header_batt_indicator()` rewrites two header labels every 500 ms unconditionally; with `full_refresh=1` each rewrite is a 57 ms full-screen SPI push (2.5/s idle).                                                                                                                                     |
| Partial refresh         | Works without band-aid invalidates once the SPI mitigation is in place. Idle pixel traffic 14x lower, mean refresh 7.7 ms.                                                                                                                                                                                                    |
| Sound blocks the UI     | Measured: loop stall 1.10 s per message tone (`play_cw('r')` runs synchronously on the loop task, `i2s_write` blocking) vs 93 ms idle.                                                                                                                                                                                        |
| Heap                    | 120 injected messages cost **40.8 KB internal heap, linear, no plateau** (~340 B/msg): `msg_list` children never trimmed (121 rendered vs 50 in the model), `persisted_msgs` = 120 dead entries. Internal heap gone after ~370 messages — a credible face for the maintainer's report.                                        |
| Display wake / sleep    | Refuted as a cause: the operator's failures happened with the display lit; the wake path was never involved.                                                                                                                                                                                                                  |
| Panel readback          | `tft.readRect()` returns a constant (MISO not driven); `--screencrc` is **not a valid instrument** on this hardware. Verdicts built on it are void.                                                                                                                                                                           |
| Boot                    | Fully mirrored to serial now; 8 boot messages block 2 s each (16 s), device ignores serial ~11 s after `CLIENT STARTED`.                                                                                                                                                                                                      |

## 1. The lost-flush defect

### Symptom

Incoming message: tone plays, bubble does not appear. Opening the drawer (full-screen invalidate)
or moving the cursor (small invalidates) reveals it. Same for the map: the tile "emerges" only where
the cursor passes. `full_refresh=1` hid it because the battery header repaints the whole screen
500 ms later.

### What was ruled out, with the measurement that ruled it out

| Candidate                            | Evidence against                                                                                                                  |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------- |
| Missing LVGL invalidation            | `[REDRAW]` trace: `msg_tabs_add_message()` invalidates the tab area, a 76 800 px refresh follows every message                    |
| Wrong render / stale scroll          | CRC of message rows in the flushed buffer identical to a forced re-render 4 s later (6/6); ASCII frame dump shows the new bubbles |
| Scroll position                      | `scroll_bottom = 0` after every message, last bubble inside the list box                                                          |
| Display sleep / `tft.init()` on wake | Operator: display lit throughout; `[TFT];sleeping;0` at every flush                                                               |
| Audio task starving the loop         | Muted messages: ~44/45 displayed                                                                                                  |
| Raw SPI transfer unreliable          | `--blink` probe: 20/20 alternating full frames, also under a WiFi ping flood                                                      |
| SPI clock margin (40 MHz)            | At 20 MHz the normal path still fails 0/30                                                                                        |
| WiFi association retry               | 4 of 5 early failures happened with WiFi connected and idle                                                                       |
| Concurrent SPI task                  | RadioLib has no task; audio task only runs when a file plays; scheduler-suspend experiment hung the device and was discarded      |

### What isolated it

Split of the unmuted message path with runtime switches (`--audiodbg`), 10 messages each, operator
counting bubbles:

| Variant                                               | Displayed  |
| ----------------------------------------------------- | ---------- |
| normal: `SD.exists()` for the tone file + `play_cw()` | 0 / 30     |
| tone only, no SD access (`--audiodbg 1`)              | ~5 / 10    |
| **SD lookup only, tone skipped (`--audiodbg 2`)**     | **0 / 10** |
| muted (neither)                                       | ~44 / 45   |
| `--sdtest` 400 ms _before_ the message                | 3 / 4      |

The `[BUS]` snapshot taken right before the lost flushes shows SPI2's clock register holding the
SD library's 800 kHz divider (`0x00249005`) instead of the display's (`0x00041001`): the frame is
the first display transaction after another bus user reconfigured the shared peripheral. The lost
frames were clocked at full speed (same 145-178 ms wire time as good ones), so the corruption is in
something TFT_eSPI programs only once, not the divider itself. The "3/4" row shows why it looks
random: the 500 ms header flush usually re-arms the bus in between; only a flush that follows the SD
access _immediately_ is lost — exactly what the message path (lookup, then render+flush in the same
loop pass) and the map path (tile read, then flush) do.

### Mitigation, verified

One throw-away display transaction (`startWrite; writecommand(NOP); endWrite`) before the real
transfer in `disp_flush()` (`--flushfix on`):

| Run                                     | Displayed |
| --------------------------------------- | --------- |
| SD-only path, fix on, 20 MHz            | 10 / 10   |
| SD-only path, fix on, replay            | 10 / 10   |
| normal path (SD + tone), fix on, 20 MHz | 10 / 10   |
| normal path, fix on, **40 MHz**         | 10 / 10   |

All in order, each bubble immediately after its tone. Cost: one 1-byte SPI transaction per flush.

### Open: the exact register

Which TFT_eSPI-initialised SPI2 field the Arduino SD path clobbers is not yet identified
(candidates: `misc`/CS setup-hold, dummy/addr bits, the S3 `SPI_UPDATE` requirement). Snapshot
`GPSPI2` fully (not only `clock/user/ctrl`) before a lost and a good flush to find it; then the
proper fix is to re-program that field in `disp_flush` instead of the NOP transaction, or to give
the SD card its own bus mutex discipline (paper §4).

## 2. Instrumentation in the firmware (all default off, no behaviour change)

| Command                       | Output / effect                                                                                                                                                             |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--redrawlog on/off`          | `[REDRAW]` per invalidation with 8-frame backtrace, `[REFRSTART]`, `[REFR]`, `[FLUSH]` with area, sleep state and CRC32 (whole + message rows), `[BUS]` before full flushes |
| `--uistat`                    | tab, drawer, objects, msg_list children, inv/refr totals, heap, tft state, scroll_y/bottom, list geometry                                                                   |
| `--tab list/<n>`, `--drawer`  | navigation                                                                                                                                                                  |
| `--injectmsg <dst> <text>`    | message as if received via LoRa                                                                                                                                             |
| `--playtone start/msg/<file>` | tones with explicit errors (ignores mute)                                                                                                                                   |
| `--mute on/off`               | `node_mute` + I2S driver install/uninstall                                                                                                                                  |
| `--audiodbg 0/1/2`            | normal / skip SD lookup / skip tone in the message path                                                                                                                     |
| `--sdtest`                    | one `SD.exists()` on the shared bus                                                                                                                                         |
| `--flushfix on/off`           | the NOP-transaction mitigation                                                                                                                                              |
| `--invalidate`, `--reflush`   | force a re-render / push the current buffer again                                                                                                                           |
| `--framedump`                 | ASCII dump of the next full frame (every 4th px)                                                                                                                            |
| `--blink <n>`                 | alternate normal/inverted full frames (transfer probe)                                                                                                                      |
| `--tft on/off/state`          | display sleep/wake                                                                                                                                                          |
| `--screencrc`                 | **void on this hardware** (constant readback)                                                                                                                               |

Harness: `tools/bench/tdeck_harness.py --scenario all` (boot, idle, tabs, drawer, inject, audio,
audio_stall, sleep, screen, heap; `--heap-count`, `--inject-count`, `--inject-spacing`). The
`sleep`/`screen` verdicts depend on `--screencrc` and must be treated as void until a readback
exists.

## 3. Baseline numbers (`full_refresh=1`, 40 MHz)

| Metric               | Value                                                 |
| -------------------- | ----------------------------------------------------- |
| Idle refreshes       | 2.52/s, every one 76 800 px, mean 56.9 ms             |
| Idle invalidations   | 32/s, 100 % from `update_header_batt_indicator()`     |
| Loop                 | avg 6.6 ms, max 74 ms; 1.10 s during the message tone |
| Partial refresh idle | 13 859 px/s, mean refresh 7.7 ms                      |
| Heap per message     | ~340 B internal + ~2.7 KB PSRAM, unbounded            |

## 4. Recommended order of work

1. Make the flush mitigation permanent in `disp_flush` (or identify the clobbered register and fix it
   properly); keep `--flushfix` as the switch to demonstrate the defect.
2. Switch to `full_refresh=0` (branch `tdeck-partial-refresh-trace`) — verified to render tabs, drawer
   and messages correctly with the mitigation.
3. Battery header: only `lv_label_set_text` when the text changes (kills the idle repaint).
4. Route `play_cw()` off the loop task, fix the audio task priority (paper §6).
5. Trim the rendered message list with the model; drop `persisted_msgs`.
6. Tone-file lookup: resolve once at boot, not per message (removes the SD access from the message
   path altogether).
7. Boot: replace the 2 s busy-wait per boot message.

Measure every step with the harness before and after; do not stack changes.

## 5. Map (2026-08-29, early morning)

| Item                                | Result                                                                                                                                                                                                                                                                                                                                                                                                 |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Own position not centred, "jumps"   | Cause: one 256 px tile in a 294x182 tab that scrolled. Fix: `sdmap_refresh()` composes a viewport-sized image from the 1-4 tiles that intersect it, own position at the centre by construction; `sdmap_project_view()` places every station in viewport pixels. `[MAP]` line reports `center_err`; 0/0 at zoom 3-9 in both directions (harness `--mapzoom`). Tab scrolling disabled, scroll bars gone. |
| Zoom keys                           | `g` / `h` on the map tab (SYM+I / SYM+O and touch buttons unchanged); all five entry points now call one `tdeck_map_zoom(dir)` (was 4 copies, handover A1).                                                                                                                                                                                                                                            |
| Reboot on zoom-out with other nodes | G01 confirmed: `add_map_point()` deleted a station's objects, then returned early for an off-screen station without NULLing the slot; the next refresh deleted the freed object (`Guru Meditation LoadProhibited` in `_lv_obj_get_ext_draw_size`). Fix: NULL the slots. Reproducer `--injectpos` x4 + 36 zoom steps: crashes at step 7 without the fix, clean with it.                                 |
| Tile load time                      | SD card was mounted at 800 kHz: 1.6-4.5 s per zoom. At 20 MHz (`SD.begin(..., 20000000)`, experiment): 0.33-0.79 s; the rest is PNG decode (~170 ms/tile). A decoded-tile cache would make zoom-back instant.                                                                                                                                                                                          |

Still open on the map: neighbour tiles beyond the 2x2 window are not needed (viewport < tile), but
a station further away than the viewport is simply not drawn; map panning (handover §4.5) is not
implemented.
