# Shared context for all research tracks

Read this first. Do not duplicate its content into your own findings file; reference it.

## What the final deliverable is

The orchestrator will merge all track files into ONE comprehensive markdown whose sole reader is an
**AI coding agent** that will then change firmware in this repo. Therefore:

- Write for a machine that must produce correct code, not for a human browsing a blog.
- Every claim must be actionable: "do X because Y, symptom if you don't: Z".
- Prefer concrete API names, config macros, exact numbers, code snippets that compile against
  LVGL 8.3.
- Every non-obvious claim carries a source URL. Mark anything you could not confirm as
  `UNVERIFIED`. Do not present forum speculation as fact.
- Explicitly distinguish LVGL **8.3** behaviour from LVGL **9.x** behaviour. Most fresh web material
  is about v9 and does not apply here. If a v9 answer exists but v8.3 differs, say both.
- English. No emojis.

## The hardware and software under discussion

- Board: LilyGO **T-Deck Plus**, ESP32-S3 (dual core, 240 MHz), 8 MB PSRAM (`BOARD_HAS_PSRAM=1`),
  16 MB flash.
- Display: 320x240 ST7789 over **SPI**, driven by **TFT_eSPI** (vendored at `lib/TFT_eSPI`).
- Touch: GT911 capacitive, I2C. Plus a trackball (mouse indev) and an I2C keyboard (ESP32-C3 at
  0x55, keypad indev).
- **The SD card shares the SPI bus with the TFT.** A single FreeRTOS semaphore (`xSemaphore`)
  serialises TFT and SD access.
- Audio: I2S DAC, driven by the vendored **ESP32-audioI2S** library (`lib/ESP32-audioI2S`,
  `Audio.cpp`). MP3/AAC/FLAC decode in software on the CPU, source files read from the SD card.
- LVGL: **8.3.11**, vendored at `lib/lvgl`. Config at `src/t-deck/lv_conf.h`
  (`-D LV_CONF_INCLUDE_SIMPLE`).
- PlatformIO env `t_deck_plus`, `platform = espressif32 @ 6.6.0` (Arduino core 2.x, IDF 4.4.x).

## The current LVGL configuration (facts, already verified by the orchestrator)

From `src/t-deck/lv_conf.h`:

- `LV_COLOR_DEPTH 16`
- `LV_MEM_CUSTOM 1` with `LV_MEM_CUSTOM_ALLOC ps_malloc` / `free` / `ps_realloc`
  (i.e. LVGL's object heap lives in **PSRAM**, and `LV_USE_MEM_MONITOR` is therefore unavailable)
- `LV_DISP_DEF_REFR_PERIOD 10` ms, `LV_INDEV_DEF_READ_PERIOD 30` ms
- `LV_DRAW_COMPLEX 1`, all GPU backends off
- `LV_USE_PERF_MONITOR 0`, `LV_USE_MEM_MONITOR 0`
- Only `LV_FONT_MONTSERRAT_28` is enabled; all other Montserrat sizes are 0
- `LV_SPRINTF_CUSTOM 1`

From `src/t-deck/tdeck_main.cpp`:

- **Single draw buffer**, screen-sized: `lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_WIDTH * TFT_HEIGHT)`.
  `buf2` is `NULL`, so there is no double buffering.
- `disp_drv.full_refresh` was `1` for a long time and is currently being trialled at `0`
  (branch `tdeck-partial-refresh-trace`).
- `disp_drv.hor_res = TFT_HEIGHT; disp_drv.ver_res = TFT_WIDTH;` (landscape swap).
- `flush_cb` = `disp_flush()`: takes `xSemaphore`, then
  `tft.startWrite(); tft.setAddrWindow(...); tft.pushColors(..., w*h, false); tft.endWrite();`
  then `lv_disp_flush_ready(disp)` — **fully blocking, no DMA**.
- `monitor_cb` and `render_start_cb` are wired to debug hooks in `src/t-deck/tdeck_debug.cpp`.
- `lv_task_handler()` is called from the Arduino main loop, and additionally from busy-wait loops
  such as `addMessage()` in `tdeck_main.cpp` (`while (...) { lv_task_handler(); delay(5); }`) and
  around `src/t-deck/lv_obj_functions.cpp:4307`.

From `src/esp32/esp32_audio.cpp`:

- Playback is done in two ways. One path blocks the calling task outright:
  `while (audio.isRunning()) { if (node_mute) break; audio.loop(); }` with no yield at all.
  The other runs `play_function()` as a pinned FreeRTOS task doing `audio.loop(); vTaskDelay(1);`
  and `vTaskSuspend(NULL)` when done.
- The I2S driver is torn down and re-installed at runtime in the mute path.
- An earlier internal review flagged: audio task priority above the intended ceiling, the audio
  semaphore guarding only setup and not playback, and the I2S teardown happening under the audio
  task itself.

## The symptoms the operator reports

1. A graphics object is created/updated but **no redraw is triggered** — the screen does not show it.
2. Redraws in general are unreliable; incoming messages do not repaint the list.
3. Full-screen repaints are slow; the UI feels laggy.
4. **Playing audio blocks the whole device** — the UI freezes for the duration of playback.
5. General wish: fast touch feedback, then render the expensive part afterwards.

## Existing in-repo analysis you may cite but must not repeat wholesale

- `docs/tdeck-findings-20260828.md` — measured flush timings, partial-refresh experiment
- `docs/tdeck-handover.md` — problem list as handed to the fix team
- `docs/tdeck-gui-verdict.md` — adversarial review verdict (findings H1, C1, C2, C3, P1, P2, F1)
- `docs/tdeck-status-20260828.md` — campaign status

## Hard rules for every agent in this wave

- **AUDIT ONLY on the repository.** Do not edit, create or delete any file under
  `/Users/martinwerner/WebDev/MeshCom-Firmware-DEV-Main`. Reading and grepping is encouraged.
- **No git commands at all** (no add/commit/push/checkout/stash/restore/reset/worktree).
- Your only write target is your own exclusive output file, named in your brief. Never write to
  another track's file.
- Do not run builds, do not flash hardware, do not open serial ports.
- Use `WebSearch` and `WebFetch` freely. Prefer primary sources in this order: LVGL docs for the
  **v8.3** release, the LVGL GitHub repo source and issues, the LVGL forum, library source on
  GitHub, then blogs. The LVGL 8.3 documentation lives under `https://docs.lvgl.io/8.3/`.
- You may read the vendored source under `lib/lvgl` and `lib/ESP32-audioI2S` directly — that is the
  exact code that ships, and beats any doc.
- Budget roughly 12-25 web operations. Depth beats breadth: a wrong or vague answer is worse than a
  short one.

## Output file format

Write your file as markdown with this skeleton:

```markdown
# Track N — <title>

## TL;DR for the coding agent

<5-12 bullets, each one actionable rule>

## Findings

### <numbered finding>

**Claim** / **Why** / **Symptom if violated** / **Fix (code or config)** / **Source**

## Rules to hand the coding agent

<numbered, imperative, checkable>

## Open questions / UNVERIFIED

<what you could not confirm>

## Sources

<url list with one-line description each>
```

## Your report back to the orchestrator

Terse and structured, no narrative:

- Output file path
- 5-10 line summary of the most important conclusions
- Anything that contradicts the shared context above (this matters — say it loudly)
- What you could not verify
