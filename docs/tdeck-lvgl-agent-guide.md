# T-Deck Plus LVGL — engineering guide for a coding agent

Target reader: an AI coding agent that is about to change LVGL/display/audio code in this
repository. Every claim below was checked against the vendored source in this tree (LVGL 8.3.11 in
`lib/lvgl`, TFT_eSPI in `lib/TFT_eSPI`, ESP32-audioI2S 2.1.0 in `lib/ESP32-audioI2S`) or against
the PlatformIO build metadata for `env:t_deck_plus`. Where something could not be confirmed it is
marked `UNVERIFIED` and must not be treated as fact.

Scope: LVGL 8.3 only. LVGL 9 renamed or removed most of the API used here (`lv_display_t`,
`lv_display_set_buffers` with byte sizes, `LV_EVENT_RENDER_READY` instead of `monitor_cb`,
`lv_binfont_create` instead of `lv_font_load`, a `level` parameter on the log callback). Any v9
snippet — from a blog, a forum, or your own memory — will not compile here. Check the vendored
header before believing an API exists.

Supporting material: [`lvgl-research/`](lvgl-research/) holds the raw per-topic research this guide
was built from, with full source URLs and per-track `UNVERIFIED` sections. Read
[`lvgl-research/README.md`](lvgl-research/README.md) first — those files predate the configuration
correction in §0 and contain claims this guide supersedes.

---

## 0. Read this first: the config trap that invalidates most prior analysis

**`src/t-deck/lv_conf.h` is dead. It is never compiled.**

The build passes `-D LV_CONF_INCLUDE_SIMPLE`, so LVGL resolves `#include "lv_conf.h"` through the
`-I` search path. Verified from `pio project metadata -e t_deck_plus`:

- `src/t-deck` is **not** on the include path.
- `variants/t_deck_plus` **is** on the include path.
- There is no `src/lv_conf.h`, no `include/lv_conf.h`, no `lib/lvgl/lv_conf.h`.

**`variants/t_deck_plus/lv_conf.h` is the effective configuration.** It is byte-identical to
`variants/t_deck/lv_conf.h`.

Independent confirmation: the code references `&lv_font_montserrat_{12,14,16,18}` at 16 call sites
and never references `_28`. Those symbols only exist when the matching `LV_FONT_MONTSERRAT_N` macro
is 1. The dead file enables only `_28`; the effective file enables 12/14/16/18/20. The firmware
builds, so the effective file is the one in force.

**Consequence: delete or clearly mark `src/t-deck/lv_conf.h`.** It sits next to the T-Deck sources,
looks authoritative, and has already caused wasted analysis. Leaving it in place guarantees the
next agent reads the wrong values.

### The real configuration

| Setting                          | Value                                          | Why it matters                                                    |
| -------------------------------- | ---------------------------------------------- | ----------------------------------------------------------------- |
| `LV_COLOR_DEPTH`                 | 16                                             | 2 bytes/px                                                        |
| `LV_COLOR_16_SWAP`               | **1**                                          | Byte order; wrong value shows as inverted/wrong colours           |
| `LV_MEM_CUSTOM`                  | 1, `ps_malloc` / `ps_realloc`                  | LVGL object heap in PSRAM. Disables `LV_USE_MEM_MONITOR` entirely |
| `LV_DISP_DEF_REFR_PERIOD`        | **16 ms**                                      | Not 10 ms. Also the animation step rate                           |
| `LV_INDEV_DEF_READ_PERIOD`       | 30 ms                                          | Input poll interval                                               |
| `LV_TICK_CUSTOM`                 | 1, `millis()`                                  | Hardware-backed; cannot stall. Rule out tick bugs                 |
| `LV_USE_LOG`                     | **0**                                          | Every LVGL warning and error is compiled out                      |
| `LV_USE_ASSERT_NULL` / `_MALLOC` | 1                                              | Active                                                            |
| `LV_USE_ASSERT_OBJ` / `_STYLE`   | 0                                              | No object-validity checks                                         |
| `LV_ASSERT_HANDLER`              | `while(1);`                                    | A failed assert is a **silent hang**, no message                  |
| `LV_USE_THEME_DEFAULT`           | **1**, `GROW 1`, `TRANSITION_TIME 80`          | Default theme IS active; presses animate                          |
| `LV_FONT_DEFAULT`                | `&lv_font_montserrat_12`                       | Not 28                                                            |
| Montserrat enabled               | 12, 14, 16, 18, 20 (+ `12_SUBPX`, `UNSCII_16`) | 28 is not compiled                                                |
| `LV_USE_FONT_SUBPX`              | 0                                              | But `_12_SUBPX` font data still compiles — dead flash             |
| `LV_IMG_CACHE_DEF_SIZE`          | 0                                              | Decoder open/close per redraw                                     |
| `LV_LAYER_SIMPLE_BUF_SIZE`       | 24 KB (internal default)                       | Translucent-overlay compositing chunk size                        |
| `LV_INV_BUF_SIZE`                | 32 (internal default)                          | Invalid-area ring depth                                           |
| `LV_SPRINTF_CUSTOM`              | 0                                              | LVGL's own printf                                                 |
| `LV_TXT_ENC`                     | UTF-8                                          | Multi-byte decoding is on                                         |
| `LV_TXT_LINE_BREAK_LONG_LEN`     | 0                                              | No forced break inside long words                                 |
| `LV_DPI_DEF`                     | 130                                            | Drives `LV_DPX()`, scrollbar width, default object size           |

### Display driver state (`src/t-deck/tdeck_main.cpp`)

- Draw buffer: **one** buffer, screen-sized, allocated with `ps_malloc(320*240*2)` = 153,600 B in
  **PSRAM** (`:351`). `buf2 = NULL`. No double buffering.
- `lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_WIDTH * TFT_HEIGHT)` (`:387`) — the 4th argument
  is a **pixel count** (`lv_hal_disp.h:223`). Correct today; the allocation is in bytes, so the
  buffer is 2x larger than declared. Harmless over-allocation, not a bug. Keep one pixel-count
  constant as the single source of truth if you ever resize it.
- `full_refresh = 0` on this branch (`:398`); it was `1` for a long time.
- `hor_res = TFT_HEIGHT (320)`, `ver_res = TFT_WIDTH (240)` with `tft.setRotation(1)`. `sw_rotate`
  and `rotated` stay 0. **This is correct** — the ST7789 rotates in hardware via MADCTL. Do not
  enable LVGL-side rotation; it would double-rotate and burn CPU.
- `flush_cb` = `disp_flush()` (`:458`): `xSemaphoreTake` → `startWrite` → `setAddrWindow` →
  `pushColors(..., false)` → `endWrite` → `lv_disp_flush_ready` → `xSemaphoreGive`. **Fully
  blocking, no DMA.**
- TFT_eSPI config is `lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h`, hard-included (not gated
  by a build flag) at `lib/TFT_eSPI/User_Setup_Select.h:139`, so it applies to every board in this
  repo. `SPI_FREQUENCY 40000000`, ST7789. It also enables `LOAD_FONT2/4/6/7/8`, `LOAD_GFXFF` and
  `SMOOTH_FONT` — TFT_eSPI's own font engine, which LVGL never uses. Pure flash cost, safe to strip.

### Toolchain

`variants/t_deck_plus/platformio.ini:4` pins `platform = espressif32 @ 6.6.0` at env level, which
overrides the `^6.13.0` in the root `platformio.ini:332`. Confirmed by the resolved framework:
`framework-arduinoespressif32@3.20014.231204` (Arduino core 2.0.14, ESP-IDF 4.4.x).

From that package: `configMAX_PRIORITIES = 25` (valid priorities 0-24), `CONFIG_FREERTOS_HZ = 1000`
(so `vTaskDelay(1)` is 1 ms, not 10 ms). `loopTask` runs at priority **1** on core 1.

---

## 1. Non-negotiable rules

1. **One task owns LVGL.** Today every `lv_*` call runs on the Arduino loop task — verified by
   auditing every `xTaskCreate*` body in `src/`. LVGL 8.3 has no internal locking. If any fix moves
   a `lv_*` call to another task, it must either take a shared mutex around every `lv_*` call
   _including_ `lv_task_handler()`, or post through a queue drained on the LVGL task. The only two
   functions safe to call without the mutex are `lv_tick_inc()` and `lv_disp_flush_ready()`.
2. **Never block inside `read_cb`, an `LV_EVENT_*` callback, or `flush_cb`.** LVGL timers are
   non-preemptive: while any of these runs, no input is read, nothing renders, and `loop()` does not
   advance.
3. **Never call `lv_task_handler()` re-entrantly.** It is guarded by a function-local
   `static bool already_running` (`lv_timer.c:72-76`) and a nested call returns immediately without
   running a single timer. It is a no-op, not a deferral.
4. **Never invalidate from inside a draw event.** `_lv_inv_area()` hard-returns when
   `disp->rendering_in_progress` is true (`lv_refr.c:212-215`). It logs an error — which is compiled
   out here. Completely silent.
5. **Go through the widget API.** Nothing in LVGL polls widget state. Writing a widget's private
   struct fields is a guaranteed redraw bug.
6. **Extend the existing instrumentation, do not add a third layer.** `src/t-deck/tdeck_debug.cpp`
   and `src/instrument.{h,cpp}` already exist and `tools/bench/tdeck_parse.py` parses both.
7. **Do not change buffering mode to fix a missing redraw.** Rule out a missing invalidation first.

---

## 2. Why an object does not repaint

### The pipeline (LVGL 8.3.11, from `lv_refr.c` / `lv_obj_pos.c`)

```
lv_obj_invalidate(obj)                 // lv_obj_pos.c:856, expands coords by ext_draw_size
  -> lv_obj_invalidate_area(obj, area) // :840
       -> lv_obj_get_disp(obj); bail if !lv_disp_is_invalidation_enabled(disp)
       -> lv_obj_area_is_visible()     // :873  <-- THE DROP GATE
       -> lv_obj_invalidate_hook()     // :852  <-- MeshCom patch, feeds [REDRAW]
       -> _lv_inv_area(disp, area)     // lv_refr.c:206
            intersect with screen; apply full_refresh; rounder_cb; dedupe;
            append to disp->inv_areas[inv_p++]; lv_timer_resume(disp->refr_timer)
  -> _lv_disp_refr_timer()             // every LV_DISP_DEF_REFR_PERIOD (16 ms)
       lv_obj_update_layout() on act/prev screen + top/sys layers
       lv_refr_join_area() -> refr_invalid_areas() -> refr_area() -> refr_area_part()
         DRAW_MAIN_BEGIN/MAIN/MAIN_END, recurse children, DRAW_POST_*
       draw_buf_flush() -> flush_cb -> YOU must call lv_disp_flush_ready()
       monitor_cb(elapsed, px_count)   // only if inv_p != 0
```

### The silent-drop catalogue

Confirmed against source. An invalidation is dropped, with no output whatsoever in this build, when:

| #   | Condition                                                                                 | Where                      | Live risk here                                   |
| --- | ----------------------------------------------------------------------------------------- | -------------------------- | ------------------------------------------------ |
| a   | Object or any ancestor has `LV_OBJ_FLAG_HIDDEN`                                           | `lv_obj_pos.c:875`, `:908` | High — the UI toggles HIDDEN constantly          |
| b   | Object's screen is not act/prev screen or top/sys layer                                   | `lv_obj_pos.c:878-885`     | Low — single persistent screen, no `lv_scr_load` |
| c   | Area does not survive intersection with the ancestor clip chain                           | `lv_obj_pos.c:888-899`     | Medium                                           |
| d   | Area does not intersect the screen rect, or the object is zero-sized                      | `lv_refr.c:232-233`        | Medium                                           |
| e   | `disp->inv_en_cnt <= 0` — `lv_disp_enable_invalidation` is a **refcount**, not a flag     | `lv_disp.c:433`            | None — never called in this repo                 |
| f   | `_lv_inv_area()` called while `disp->rendering_in_progress`                               | `lv_refr.c:212-215`        | **High** — silent, classic trap                  |
| g   | Style changed on a shared `lv_style_t` without `lv_obj_report_style_change()`             | `lv_style.c:283`           | None found in `src/t-deck/`                      |
| h   | Widget-private state written directly instead of via the setter                           | logical                    | Audit any `((lv_label_t*)obj)->...` cast         |
| i   | `lv_obj_set_pos/size/x/y/width/height` called with the value it already has               | `lv_obj_pos.c:61,75,166`   | Medium — silent by design                        |
| j   | Object read back via `obj->coords` before `lv_obj_update_layout()` flushed pending layout | `lv_refr.c:309-313`        | Medium                                           |
| k   | `lv_task_handler()` re-entered                                                            | `lv_timer.c:72-76`         | Latent — see below                               |

Not a drop but worth knowing: overflowing `LV_INV_BUF_SIZE` (32 distinct rects between refresh
ticks) does **not** lose the newest area — it collapses to one full-screen redraw for that pass
(`lv_refr.c:252-259`). Correct output, unexplained slow frame. Look for a burst of many small
non-adjacent invalidations (e.g. redrawing every list row individually) rather than a flush bug.

### Ruled out — do not chase these

- **Tick stall.** `LV_TICK_CUSTOM` is bound to `millis()`, hardware-timer backed. It cannot stall
  from task starvation.
- **Wrong display.** Exactly one `lv_disp_drv_register()` call exists.
- **Paused refresh timer.** `_lv_inv_area` resumes it on every accepted invalidation. The repo
  pauses `msg_flush_timer` and `track_clear_timer`, never `disp->refr_timer`.
- **Cross-task LVGL race.** No `xTaskCreate*` body in `src/` calls any `lv_*` function. The audio
  task (`play_function`) and the net-console task (`con_auth`) are both clean. This is a _current_
  fact, not a guarantee — it is exactly what a careless audio fix would break.
- **Re-entrant `lv_task_handler()` today.** All three call sites — `esp32_main.cpp:3847` (`loop()`),
  `tdeck_main.cpp:284` (`addMessage()` boot busy-wait), `lv_obj_functions.cpp:4307`
  (`tdeck_reset_msg_tabs()`, whose only caller `tdeck_clear_text_ta()` appears itself to be dead
  code) — resolve to non-nested paths. Still fix the pattern: the moment any of these becomes
  reachable from a button handler, the nested call becomes a silent multi-second no-op that also
  holds the outer refresh pass hostage.

### `full_refresh` — understand the trade before touching it

`full_refresh = 1` makes `refr_area()` redraw the entire screen regardless of what was invalidated
(`lv_refr.c:638-648`). It **masks every missing-invalidation bug**: some unrelated periodic repaint
(the battery/clock tick) repaints everything anyway. Flipping it to 0 does not create redraw bugs,
it reveals them. Do not flip it back to 1 as a "fix" — that hides the defect. Confirm the area
computation with the `[SCREEN]` CRC readback first.

### `lv_refr_now()`

`lv_refr_now(disp)` (`lv_refr.c:113-128`) synchronously runs a whole refresh pass on the calling
stack. Correct use: forcing one acknowledgement frame out before deliberately blocking. Never call
it from inside a `DRAW_*`/`COVER_CHECK` event, or from `flush_cb`/`monitor_cb`/`render_start_cb` —
it re-enters `refr_invalid_areas()` against buffer state the outer call has not unwound. Not called
anywhere in this repo today.

---

## 3. Buffers, partial refresh, flush and SPI

### The four legal regimes in 8.3

| Mode                              | buf1  | buf2 | `full_refresh` | `direct_mode` | Behaviour                                                                                                                                    |
| --------------------------------- | ----- | ---- | -------------- | ------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| A. One small partial buffer       | small | NULL | 0              | 0             | Default. Large areas chunked by `get_max_row()`. Renderer blocks on previous flush.                                                          |
| B. Two small partial buffers      | small | same | 0              | 0             | **The only mode with real render/flush overlap.** Wait moves into `draw_buf_flush()`.                                                        |
| C. Two full-screen + full_refresh | full  | full | 1              | 0             | Classic double buffering. No partial-update savings.                                                                                         |
| D. `direct_mode`                  | full  | opt  | —              | 1             | Draw at absolute screen coords into a persistent buffer. With two buffers LVGL runs a sync pass (`lv_refr.c:518-560`); with one it skips it. |

**The current combination — one screen-sized buffer, `buf2 = NULL`, `full_refresh = 0` — is legal.**
It is not the cause of any redraw bug. It simply forfeits all overlap: `refr_area_part()` waits on
`buf1 && !buf2` (`lv_refr.c:708-712`), which costs nothing today only because `disp_flush()` is
already synchronous.

### Buffer sizing for 320x240x16bpp

| Fraction | Pixels | Bytes   | Flush calls per full-screen invalidation |
| -------- | ------ | ------- | ---------------------------------------- |
| Full     | 76,800 | 153,600 | 1                                        |
| 1/4      | 19,200 | 38,400  | 4                                        |
| 1/8      | 9,600  | 19,200  | 8                                        |
| 1/10     | 7,680  | 15,360  | 10                                       |

LVGL's documented floor is 1/10 screen; below that the fixed per-flush overhead (`setAddrWindow`,
CS toggling, semaphore pair) starts to dominate. Hard invariant: the buffer must hold at least one
full row of the widest expected area — if `get_max_row()` returns 0, `refr_area()`'s row loop never
advances and **hangs**. There is no guard against this in 8.3.

### PSRAM vs internal DRAM — the decisive constraint

On this toolchain (ESP-IDF 4.4.x), an SPI DMA source buffer must be internal DMA-capable memory:
`heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`. `MALLOC_CAP_DMA` excludes
`MALLOC_CAP_SPIRAM`. If you hand a PSRAM pointer to the SPI driver it silently allocates a bounce
buffer and `memcpy`s per transfer — reintroducing a blocking copy into the "non-blocking" path, plus
per-flush heap churn on a hot path. The IDF 5.x `SPI_TRANS_DMA_USE_PSRAM` opt-in is not available
here, and TFT_eSPI's vendored DMA code sets `trans.flags = 0` regardless.

**Placement rules:**

- **Draw buffers → internal DRAM.** Two buffers of ~1/8 screen is ~38 KB total. Affordable.
- **LVGL object heap → keep in PSRAM.** `LV_MEM_CUSTOM_ALLOC = ps_malloc` is correct. Objects,
  styles, string copies are not DMA'd and not latency-critical. Internal RAM is the scarce resource
  to protect for DMA buffers and stacks.
- Today's full-screen PSRAM draw buffer is the worst placement: LVGL renders into PSRAM and
  `pushColors` reads pixels back out of PSRAM in a software loop.

### The non-blocking flush, and the one unresolved risk

Read from the vendored `lib/TFT_eSPI/Processors/TFT_eSPI_ESP32_S3.c`:

- `initDMA(bool ctrl_cs = false)` calls `spi_bus_initialize()` **and** `spi_bus_add_device()`
  (`:861-864`) with `queue_size = 1`. It claims the bus at IDF driver level.
- `pushPixelsDMA()` / `pushImageDMA()` call `dmaWait()` as their **first** action (`:637`, `:681`,
  `:736`). That is what makes it safe to call `lv_disp_flush_ready()` right after queuing.
- Both chunk transfers above 32,768 px into blocking `pushPixels()` first. Irrelevant at ≤1/4-screen
  buffers.

```c
static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, w, h);
        tft.pushPixelsDMA((uint16_t *)&color_p->full, w * h);
        tft.endWrite();
        xSemaphoreGive(xSemaphore);
    }
    lv_disp_flush_ready(disp);
}
```

`UNVERIFIED, highest-risk item in this document:` with `initDMA(false)`, `endWrite()` deasserts CS.
`pushPixelsDMA()` only _queues_ the transfer. Calling `endWrite()` immediately after may deassert CS
while the DMA engine is still clocking data out, producing a torn write. Options: use
`initDMA(true)` so the DMA transaction controls CS in hardware; or defer the CS release to
`lv_disp_flush_is_last(disp)`; or call `dmaWait()` before `endWrite()` (which makes it blocking
again). **Resolve this on hardware with the `[SCREEN]` CRC harness before shipping DMA.**

### Throughput arithmetic

320x240x16bpp = 1,228,800 bits.

- At 40 MHz: 30.7 ms data phase. Repo-measured full flush 36.7 ms — the 6 ms delta is
  `setAddrWindow`, the software FIFO-refill loop in blocking `pushColors`, and the semaphore pair.
  The model and the measurement agree.
- Full render+flush cycle measured 56.9 ms mean / 61 ms max → ~17.6 Hz ceiling full-screen.
- Partial refresh measured: mean refresh 7.7 ms, idle pixel rate down 14x (193,280 → 13,859 px/s).
- At 80 MHz: ~15.4 ms data phase, ~20-21 ms total, ~45-50 Hz ceiling. `UNVERIFIED` on this board.
  Treat as an isolated, measured experiment, not a setting to flip alongside other work.

### Partial-refresh artifact catalogue

| Artifact                           | Root cause                                                                                                                  | Fix                                                              |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Stale rectangles left on screen    | Area invalidated and drawn in the buffer but the flush was skipped or targeted wrong coords; or invalidation never happened | Follow the `[REDRAW]`→`[REFRSTART]`→`[REFR]`→`[FLUSH]` chain     |
| "Half the screen updates"          | A chunk flush was dropped. Cannot happen today (`portMAX_DELAY`), but a future timed semaphore take reintroduces it         | Keep flush waits unbounded, or retry on timeout — never drop     |
| Tearing                            | DMA buffer reused before the transfer drained; the `endWrite`/CS question above                                             | Resolve the CS timing; never signal flush-ready early            |
| Ghosting                           | Moved/resized object invalidated only its new rect, not its old one                                                         | Invalidate both old and new bounding boxes                       |
| Wrong colours after 1→0 flip       | A widget relied on the background being freshly repainted every frame                                                       | Audit custom drawing for "the area under me is always fresh"     |
| Tooling that assumes a whole frame | Under partial refresh `draw_buf` only ever holds the last flushed rectangle                                                 | Read the panel (`tft.readRect`), as `tdeck_dbg_screencrc()` does |

---

## 4. The shared SPI bus — an unguarded correctness gap

The TFT and the SD card sit on the same physical SPI pins. `xSemaphore` exists to serialise them.
It does not.

**`xSemaphore` is taken in exactly two places in the whole firmware:**

- `src/t-deck/tdeck_main.cpp:463` — the display flush
- `src/t-deck/tdeck_debug.cpp:311` — the debug panel readback (which additionally forces
  `TDECK_SDCARD_CS` and `LORA_CS` high, with a comment showing the author knew about MISO contention)

**No SD access takes it anywhere.** Not `SD.begin()`, not `setupSD()`, not `sdmap_refresh()`'s tile
reads, not `SD.exists()` in `play_file_from_sd()`, and — critically — not `audiofile.read()` inside
`Audio::processLocalFile()` (`lib/ESP32-audioI2S/src/Audio.cpp:2869`), which runs on every
`audio.loop()` while a local file plays.

Also: `tft_on()` / `tft_off()` / `setBrightness()` call `tft.init()` and `tft.writecommand()`
**without** taking it.

Two further problems with the primitive itself:

- It is created with `xSemaphoreCreateBinary()` (`tdeck_main.cpp:116`), not
  `xSemaphoreCreateMutex()`. A binary semaphore gives **no priority inheritance and no recursion** —
  the wrong primitive for a bus guard, and a guaranteed priority-inversion source the moment a
  second task touches the bus.
- ESP-IDF's SD-over-SPI host driver assumes it owns the bus exclusively (espressif/esp-idf#1597,
  #6510). Many microSD modules do not properly tristate MISO when deselected, so two simultaneous
  transfers are electrical contention, not merely slow — corruption runs in both directions.

**Fixes, in order:**

1. Convert `xSemaphore` to a recursive mutex.
2. Take it around **every** SPI transaction on that bus: SD file I/O, map tile reads, the audio
   task's `audio.loop()`, and the `tft_on`/`tft_off`/`setBrightness` display commands.
3. Optional, architecturally cleaner: give the SD card its own `spi_host_device_t`. **Sequence this
   before adding `tft.initDMA()`** — `initDMA()` calls `spi_bus_initialize()` itself and will abort
   via `ESP_ERROR_CHECK` if it targets a host `SPI.begin()` already claimed. Host separation fixes
   driver-state corruption but not electrical MISO contention on shared wires; you still want the
   mutex.

---

## 5. Responsiveness: acknowledge now, render later

### Why the UI feels slow — the arithmetic

`lv_timer_handler()` runs every timer sequentially on one stack. LVGL's own docs: _"timers are
non-preemptive, which means a timer cannot interrupt another timer."_ While `flush_cb` runs, no
input is read, nothing else renders, and `loop()` cannot advance.

Timer ordering is favourable: `lv_disp_drv_register()` runs before the indev registrations
(`tdeck_main.cpp:401` vs `:412/424/442`), and `lv_timer_create` inserts at the list head, so indev
reads run _before_ the refresh timer in the same pass. A touch can be read and painted in one pass.

Worst case with the screen already awake:

```
61 ms  in-flight flush the touch has to wait out
30 ms  one full LV_INDEV_DEF_READ_PERIOD
16 ms  refresh scheduling slack
61 ms  the acknowledgement frame's own flush
-----
~168 ms before any application processing
```

Against a <100 ms target that is already over. On a wake touch it is far worse — see below.

### The wake-path defect (highest-leverage single fix)

`touchpad_read()` (`tdeck_main.cpp:806`) — the LVGL indev `read_cb` — calls `tft_on()` synchronously
when `current_brightness_level == 0`:

```
touchpad_read()                          tdeck_main.cpp:817
 -> tft_on()                             lv_obj_functions.cpp:1976
   -> resetBrightness()                  tdeck_helpers.cpp:38
     -> setBrightness()                  tdeck_helpers.cpp:46
        digitalWrite(SD_CS/LORA_CS, HIGH); delay(5);
        tft.init();     // ST7789_Init.h: 3-4 x delay(120) + delay(10)
        tft.setRotation(1);
        tft.writecommand(TFT_DISPON); delay(50);
        digitalWrite(BACKLIGHT, 1); delayMicroseconds(30);
```

**~400-450 ms of pure blocking delay executed inside the input-read callback**, inside
`lv_timer_handler()`. Every other timer and all of `loop()` is frozen for that duration. Total
wake-touch latency lands around 530-610 ms — 5-6x over target.

```c
// read_cb must stay fast
static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t x[5], y[5];
    data->state = LV_INDEV_STATE_REL;
    if (touch.isPressed()) {
        uint8_t touched = touch.getPoint(x, y, 1);
        if (!meshcom_settings.node_keyboardlock) {
            if (current_brightness_level == 0) wake_requested = true;  // cheap flag, no I/O
            else                               tdeck_tft_timer = millis();
            if (touched > 0) {
                data->state   = LV_INDEV_STATE_PR;
                data->point.x = x[0];
                data->point.y = y[0];
            }
        }
    }
}

// in loop(), after lv_task_handler():
if (wake_requested) {
    wake_requested = false;
    tft_on();                            // still slow, but off the timer call stack
    lv_obj_invalidate(lv_scr_act());     // the wake path currently invalidates nothing
}
```

Note the second line of that fix: **`tft_on()` does not invalidate anything today.** It only toggles
backlight and sleep state. That is a separate, real bug — waking the panel leaves LVGL believing
nothing needs redrawing.

Secondary improvement: a panel that was only put to sleep does not need a full `tft.init()` register
reprogram. `SLPOUT` + `DISPON` is enough, and skips the `delay(120)`s entirely.

### The two-phase pattern

```c
static void on_button_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);

    // Phase 1 — cheap, synchronous, visible. Nothing that blocks.
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_label_set_text(status_label, "Loading...");

    // Optional: force that acknowledgement frame to the panel before blocking.
    // Costs one full flush (~37-61 ms) and must NOT be called from inside a draw event.
    lv_refr_now(NULL);

    // Phase 2 — deferred. Runs on the NEXT lv_task_handler() pass.
    lv_async_call(do_expensive_work, ctx);
}
```

Deferral mechanisms in 8.3:

- **`lv_async_call(cb, data)`** — allocates an `lv_async_info_t` via `lv_mem_alloc` (PSRAM here),
  creates a `period = 0`, `repeat_count = 1` timer, runs on the next handler pass, self-deletes.
  Cheap but not free; do not call it per frame. It defers _when work starts_, it does not make the
  work non-blocking.
- **One-shot timer with a delay** — `lv_timer_create(cb, 50, ctx)` +
  `lv_timer_set_repeat_count(t, 1)`. Use when you want the acknowledgement frame to have definitely
  flushed first.
- **Chunked state machine** — a repeating `lv_timer` that advances a bounded amount of work per
  call. The only correct approach for work that genuinely takes hundreds of ms. `delay()` or
  `vTaskDelay()` inside an event callback is not a fix; it blocks the same stack.
- **FreeRTOS worker task + queue** — for real blocking I/O. The worker never calls `lv_*`; it sets a
  flag or posts a result that the LVGL task consumes. This repo already does this correctly with
  `queueDisplayText()` / `flushDeferredDisplayUpdates()` (`lora_functions.cpp` →
  `esp32_main.cpp:1816`) and with the BLE `deviceConnected` flag diffing. **Copy that pattern.**

### FreeRTOS architecture — recommendation

**Do not introduce a dedicated LVGL task yet.** With the current fully-blocking single-buffer flush,
a second task only relocates the freeze — it would still hold the mutex the whole UI depends on for
the full flush duration. It becomes worthwhile once flush is double-buffered and DMA-driven. When
that lands:

```c
static SemaphoreHandle_t lvgl_mux;   // xSemaphoreCreateRecursiveMutex()

static inline void lvgl_lock(void)   { xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY); }
static inline void lvgl_unlock(void) { xSemaphoreGiveRecursive(lvgl_mux); }

static void lvgl_task(void *arg) {
    for (;;) {
        lvgl_lock();
        uint32_t next = lv_timer_handler();
        lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(next < 5 ? 5 : (next > 50 ? 50 : next)));
    }
}
// core 1, priority 2-3, stack >= 8 KB. Take the lock, do the O(us) lv_* call, release
// immediately. NEVER hold it across a blocking wait (I2C, SD, network).
```

### Tuning knobs

| Knob                               | Current | Recommendation                                                                                  |
| ---------------------------------- | ------- | ----------------------------------------------------------------------------------------------- |
| `LV_DISP_DEF_REFR_PERIOD`          | 16 ms   | Leave, or raise to 30 ms. Lowering buys nothing when a frame takes 37-61 ms to flush.           |
| `LV_INDEV_DEF_READ_PERIOD`         | 30 ms   | Fine for a capacitive panel; not the dominant term.                                             |
| `LV_INDEV_DEF_SCROLL_LIMIT`        | default | Raise slightly if taps register as scrolls.                                                     |
| `LV_INDEV_DEF_SCROLL_THROW`        | default | The momentum decay rate — raise it to shorten momentum animations (each frame is a real flush). |
| `LV_THEME_DEFAULT_GROW`            | **1**   | Consider 0. Every press animates a size change over `TRANSITION_TIME` (80 ms).                  |
| `LV_THEME_DEFAULT_TRANSITION_TIME` | 80 ms   | Consider 0 on this panel.                                                                       |

`lv_anim` steps at `LV_DISP_DEF_REFR_PERIOD` (`lv_anim.c:60`). Every animation frame on this device
is a real blocking SPI push, so animations are not the free polish they are on a composited system.

### GT911 interrupt-driven reads

The GT911 `INT` pin can drive an ISR that caches touch state, making `read_cb` effectively free.
This trades the 30 ms polling worst case for near zero. Given that flush time dominates the budget
by an order of magnitude, **this is not the highest-leverage fix** — do the wake path and the flush
first. `UNVERIFIED:` whether the vendored GT911 driver exposes an INT-driven mode.

`data->continue_reading` lets a buffered driver drain a whole event queue within one pass instead of
one event per read period. Not used today (single-point read); needed if gesture buffering is added.

---

## 6. Audio: why playback freezes the device

Vendored library: `schreibfaul1/ESP32-audioI2S` **2.1.0**, the legacy `driver/i2s.h` generation
(`i2s_write`, `i2s_driver_install`). Do not apply v3.x advice (`i2s_channel_write`) to it.

### Four independent mechanisms, all confirmed

**A. The reachable synchronous freeze is the CW fallback, not the documented blocking loop.**

`play_file_from_sd_blocking()` — the bare `while (audio.isRunning()) audio.loop();` with no yield —
is **defined but never called anywhere** (`esp32_audio.cpp:174`, `:234`). It is dead code. Do not
spend a cycle on it.

The path that actually fires: `msg_focus_and_alert()` (`lv_obj_functions.cpp:3974`) is invoked from
`tdeck_add_APRS_message` / `tdeck_add_MSG` (`:4226`, `:4267`) on every incoming message. If
`play_file_from_sd()` fails to start — SD absent, file missing or renamed, both realistic — it falls
back to **`play_cw('r')` synchronously on `loopTask`** (`:4005`). `play_cw('r')` blocks for
`100+100+300+100+100+100+300` = **1100 ms** with no yield. `startAudio()` has the same fallback via
`play_cw_start()`.

So: a missing notification sound file freezes the entire UI for over a second **on every incoming
message** — the same event that is supposed to repaint the message list.

There is a telling artifact right above that call: `//lv_task_handler(); Y5 check` — a previous
author saw "the UI must be flushed before the freeze" and disabled the workaround rather than
removing the freeze.

**B. The audio task priority is 23 levels above the UI.**

`xTaskCreatePinnedToCore(play_function, "audio play task", 16*1024, NULL, 50, &xHandle, 1)`
(`esp32_audio.cpp:130`) requests priority **50**. `configMAX_PRIORITIES` is 25, so the valid range
is 0-24; `xTaskCreate` does not assert on this and clamps to 24. `loopTask` — the task that calls
`lv_task_handler()` — runs at priority **1 on the same core**. Priority 24 also outranks the BT
controller (23), `esp_timer` dispatch (22), the event loop (20) and lwIP (18).

Even with the structurally correct task-based path, that gap means the audio task preempts
`loopTask` mid-flush every millisecond and runs its full decode+write burst to completion. The UI is
not deadlocked, it is starved into something indistinguishable from a freeze.

```c
xTaskCreatePinnedToCore(play_function, "audio play task", 16 * 1024, NULL,
                        3,      // was 50 (clamped to 24). Just above loopTask (1).
                        &xHandle, 1);
```

**C. Per-sample `i2s_write()`.**

`playChunk()` (`Audio.cpp:2138`) loops over every decoded sample calling `playSample()`
(`:4134`), which issues one `i2s_write()` per 4-byte stereo sample. An MP3 frame of 1152 samples
becomes 1152 kernel-level calls per `audio.loop()`. Upstream issue
schreibfaul1/ESP32-audioI2S#754 measures **~65% of one core** for single-sample writes vs **~32%**
for 16-sample batching, attributing the difference to per-call semaphore work inside IDF.

Batching this means patching vendored code — fork-local only, never send it upstream under this
repo's minimal-change policy.

**D. Unlocked SD reads on the display's SPI bus.** See §4.

### The I2S teardown race

`audio_set_mute(true)` (`esp32_audio.cpp:543`) is called only from LVGL button/menu callbacks on
`loopTask` (`event_functions.cpp:328`, `tdeck_main.cpp:689`). It calls `audio.stopSong()` then
`i2s_driver_uninstall()` **with zero synchronization** against the audio task, which may be inside
`audio.loop()` or mid-`i2s_write()` on the same `Audio` object and the very driver handle being
uninstalled. This is a genuine cross-task race, not merely "teardown in the wrong task".

Prefer muting via `audio.setVolume(0)` or the amplifier enable GPIO. If the driver must be torn down
for power, do it only when `audio.isRunning() == false` **and** the audio task has actually
suspended — poll `eTaskGetState(xHandle) == eSuspended`, do not assume `stopSong()` took effect
synchronously.

### What will not help

DMA buffering is already at the legacy driver's ceiling: `dma_buf_count = 8`, `dma_buf_len = 1024`,
both hard maxima. ~8192 stereo frames ≈ 185 ms at 44.1 kHz. **There is no buffer-size knob left.**
The fix has to be architectural: task, priority, locking.

### Recommended shape for notification sounds

1. Route **every** playback call — file and CW — through the background task. Nothing audio-related
   may run on `loopTask`.
2. Fix the priority to 3.
3. Take the SPI mutex around `audio.loop()` (coarse but correct), or patch the two `audiofile.read()`
   sites in `Audio::processLocalFile()` (finer, touches vendored code).
4. **Move notification sounds off the SD card entirely.** This library version has no
   play-from-memory API, but `connecttoFS(fs::FS&, path)` accepts any `fs::FS` — so put beeps on an
   internal LittleFS/SPIFFS partition. ESP32-S3 internal flash is on a different SPI controller from
   the external TFT+SD bus, which removes the contention completely.
   `UNVERIFIED:` whether the 16 MB partition table has room; check `partitions-16MB-safeboot.csv`.
5. **Use WAV, not MP3, for beeps.** `sendBytes()`'s `CODEC_WAV` branch is a plain `memmove` into
   `m_outBuff` — no decode at all. Cuts CPU to roughly the `i2s_write` overhead alone.

```c
// esp32_audio.cpp — route the CW fallback off loopTask
struct CwRequest { char ch; int volume; };
static QueueHandle_t cwQueue = nullptr;

static void cw_task(void *arg) {
    CwRequest req;
    for (;;)
        if (xQueueReceive(cwQueue, &req, portMAX_DELAY) == pdTRUE)
            play_cw(req.ch, req.volume);
}
// init_audio(): cwQueue = xQueueCreate(4, sizeof(CwRequest));
//               xTaskCreatePinnedToCore(cw_task, "cw task", 4096, NULL, 2, NULL, 1);
// call site:    CwRequest r{'r', 20}; xQueueSend(cwQueue, &r, 0);
```

---

## 7. Fonts and text

### What is actually compiled

Montserrat **12, 14, 16, 18, 20**, plus `lv_font_montserrat_12_subpx` and `lv_font_unscii_16`.
`LV_FONT_DEFAULT = &lv_font_montserrat_12`. Size 28 is **not** compiled.

Built-in fonts are `const` in flash. `LV_ATTRIBUTE_LARGE_CONST` and `LV_ATTRIBUTE_FAST_MEM` are both
empty here, so glyph data sits in ordinary flash, memory-mapped through the cache. **Zero RAM cost,
ever.** Approximate flash cost at bpp=4, default range: 12px ≈ 5.9 KB, 16px ≈ 8.5 KB, 20px ≈ 12 KB,
plus 1-2 KB of kerning tables each.

Two cheap wins:

- `LV_FONT_MONTSERRAT_12_SUBPX 1` compiles a whole extra font while `LV_USE_FONT_SUBPX 0` makes the
  subpixel path unreachable. Dead flash — set it to 0. Subpixel rendering is pointless on an SPI
  panel accessed as a generic RGB565 framebuffer anyway; there is no software-visible subpixel
  geometry to align to.
- `LV_FONT_UNSCII_16 1` — check whether anything references it. Nothing in `src/t-deck/` does.

### The character-range defect

Every built-in Montserrat font in this tree is generated with the same range — confirmed from the
generator comment embedded in each `.c` file:

```
-r 0x20-0x7F,0xB0,0x2022   plus ~61 FontAwesome symbol codepoints
```

That is ASCII 32-127, degree sign, bullet, and the `LV_SYMBOL_*` set. **No umlauts, no accented
characters, nothing above 0x7F.** A missing glyph renders as a hollow rectangle placeholder, and
with `LV_USE_LOG 0` there is no warning at all.

**Correct framing of the exposure:** the static UI strings are safe — grepping `src/t-deck/`, the
only umlauts are in code comments (`lv_obj_functions.cpp:1487`, `:4207`,
`event_functions.cpp:600`). Nothing feeds a German string to a label today.

The live path is different and worse: **received mesh message text is user-supplied UTF-8** and goes
straight into message bubbles. Any umlaut, accent or emoji a peer sends renders as a box. Callsigns
and free text from the network are not ASCII-constrained.

Fix — regenerate the sizes actually used with an extended range:

```
npx lv_font_conv --font Montserrat-Medium.ttf \
  -r 0x20-0x7F,0xA7,0xB0,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0x2022 \
  --font FontAwesome5-Solid+Brands+Regular.woff \
  -r 61441,61452,61453,61461,61478,61479,61480,61502,61931,62212 \
  --size 16 --format lvgl --bpp 4 --no-compress \
  -o lv_font_montserrat_16_de.c
```

Keep `--no-compress`. Compressed fonts RLE-decode per glyph at draw time into a `ps_malloc`'d
buffer — LVGL's own docs put it at ~30% slower rendering, and this device is already CPU-bound on
software rasterization with 16 MB of free flash.

Register with `LV_FONT_DECLARE(name)` (which is just `extern const lv_font_t name;`) and
`#define LV_FONT_CUSTOM_DECLARE` in `lv_conf.h`.

Trim the FontAwesome range to the symbols actually used:
`grep -o 'LV_SYMBOL_[A-Z_]*' src/t-deck/*.cpp | sort -u` — currently `ENVELOPE`, `KEYBOARD`,
`IMAGE`, `GPS`, `LIST`, `SETTINGS`, `BARS`, `EYE_OPEN`, `BATTERY_*`, `WIFI`, `BLUETOOTH`, `RIGHT`,
`TRASH`, `USB`, `VOLUME_MAX`.

### Label API costs

- `lv_label_set_text()` **never content-diffs** in 8.3 (verified in `lv_label.c`). It unconditionally
  invalidates and, for a different string, frees and reallocs. **Add your own `strcmp` guard** on any
  label refreshed on a timer tick — the clock, battery, GPS and RSSI labels all qualify.
- `lv_label_set_text_static()` skips the malloc/copy. Use it for `const char*` literals and
  long-lived buffers.
- `lv_label_set_text_fmt()` always allocates. Never in a hot loop.
- `LV_LABEL_LONG_SCROLL` / `_SCROLL_CIRCULAR` install an `LV_ANIM_REPEAT_INFINITE` offset animation
  that invalidates on **every** animation tick — up to 62/s at the 16 ms refresh period, forever,
  for as long as the label exists. The repo uses only `LONG_CLIP` and `LONG_WRAP` today. **Keep it
  that way.**
- `LV_TXT_LINE_BREAK_LONG_LEN 0` means no forced break inside an unbreakable word. German compounds
  have no internal spaces, so a long compound under `LONG_WRAP` overflows horizontally instead of
  wrapping. Set a non-zero value or use `LONG_DOT` if this becomes visible.

### Sizing

At `LV_DPI_DEF 130` on a 2.8" 320x240 panel, ~14-16 px is the readable floor and 12 px is
marginal — yet 12 px is the current default font. Keep the set to 2-3 sizes; each additional size is
flash plus a consistency liability. With `LV_USE_THEME_DEFAULT 1` the theme's `LV_DPX`-derived
paddings and scrollbar sizes are live, so DPI is not cosmetic here.

---

## 8. Widgets: scrolling, lists, pop-ups, images

### Scrolling is expensive

`_lv_obj_scroll_by_raw()` ends with `lv_obj_invalidate(obj)` on the **whole** scrollable container,
every animation tick, and repositions every child's cached coords via `lv_obj_move_children_by()`.
LVGL 8.3 has no blit-and-shift fast path.

On the ~300x180 message-list area (54,000 px) at the measured ~478 ns/px, that is roughly 26 ms of
SPI per animation frame. A default scroll animation runs several hundred ms — dozens of pushes for
one gesture.

**Concrete defect:** `msg_list_append_bubble()`'s caller scrolls with
`lv_obj_scroll_to_view(last, LV_ANIM_ON)` (`lv_obj_functions.cpp:2796`) on **every incoming
message**. Change to `LV_ANIM_OFF`. One paint instead of N.

Momentum and elastic overscroll are on by default on any `lv_obj_create` container and each is a
framework-started animation. Consider:

```c
lv_obj_clear_flag(msg_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
lv_obj_clear_flag(msg_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
```

This is a UX trade-off — confirm with the operator rather than applying it silently.

Scrollbars are a style part (`LV_PART_SCROLLBAR`), not a child object. Current settings are already
correct: `msg_list` uses `LV_SCROLLBAR_MODE_AUTO` (`:1445`), setup fields use `_OFF` or `_AUTO`, and
no `_ON` exists anywhere. Default to `_AUTO` for anything new.

### The unbounded message list

`MSG_TAB_MAX_MESSAGES = 50` (`lv_obj_functions.cpp:182`) trims the **data model** via
`msg_tabs_trim_history()` (`:2515-2518`). The **rendered** widget tree is only resynchronised on tab
switch, via `lv_obj_clean()` + rebuild in `msg_render_active_tab()`.

While a tab stays active, the fast path at `:2794` calls `msg_list_append_bubble()` with **no
matching delete**. Each bubble is a 5-9 object tree (~1.9 KB PSRAM). A long-lived active
conversation grows the widget tree without bound.

```c
// after msg_list_append_bubble(bubble):
while (lv_obj_get_child_cnt(msg_list) > MSG_TAB_MAX_MESSAGES)
    lv_obj_del(lv_obj_get_child(msg_list, 0));
```

Plain `lv_obj_del()` is correct here — this is application code, not an event callback of the object
being deleted.

**`lv_obj_del_async` is required only when deleting from inside `LV_EVENT_DELETE`** of the object
itself (LVGL's own doc comment says exactly this). Deleting an ancestor from a descendant's
`LV_EVENT_CLICKED` handler is safe synchronously — `lv_obj_del()` fires an indev reset query that
clears any pointer into the deleted subtree before the next read (`lv_indev.c:86`). LVGL's own
`lv_msgbox` close button does this, and so does this repo's `bubble_delete_event_cb` (`:3101`).
**Do not "fix" that pattern to be async — it is already correct.**

### Pop-ups and modals

**There is no working pop-up code in this repo.** `lv_obj_functions.h:19` declares
`void lv_msgbox(char*, char*)` which is never defined and never called — dead API surface. Build
against the real `lv_msgbox_create()`; `LV_USE_MSGBOX 1` is enabled.

Cost warning: any object with `style_opa < 255` is composited through a "simple layer" buffer sized
by `LV_LAYER_SIMPLE_BUF_SIZE` (24 KB = 12,288 px at 16bpp). A full-screen 320x240 translucent
backdrop is 76,800 px — **~7 chunked render-to-layer + blend-back round trips** before the final
flush, all in software on one core. **Use an opaque backdrop**, or size the overlay to only the area
that needs to look layered.

For a non-blocking toast: `lv_timer_create` one-shot + `lv_obj_del_async`. Never spin a
`while` loop calling `lv_task_handler()` to wait for a dialog result — see §2, rule 3.

Dropdowns: `lv_dropdown_open()` reparents its pre-created list to `lv_obj_get_screen()`, **not**
`lv_layer_top()` (`lv_dropdown.c:462`). The list is allocated once at create time, so opening is not
a fresh allocation. `lv_dropdown_set_options()` copies the string; `set_options_static` does not.

### Images and the map view

**The map tile path is already correct — do not "optimize" it.** `sdmap_load_tile()`
(`tdeck_sdmap.cpp`) decodes the PNG **once** with `lodepng_decode32`, converts to a raw `lv_color_t`
buffer in PSRAM, and assigns it via `lv_img_dsc_t` with `cf = LV_IMG_CF_TRUE_COLOR` and
`LV_IMG_SRC_VARIABLE`. With `LV_IMG_CACHE_DEF_SIZE 0` the decoder's open/close run on every redraw —
but for TRUE_COLOR + variable source, "open" is a single pointer assignment
(`lv_img_decoder.c:390-397`), not a decode. Cache size 0 costs nothing here.

It _would_ cost a full re-decode per redraw if the source were a **file** (PNG/BIN) or
`LV_IMG_CF_INDEXED_*` (palette rebuilt per open, `:404-451`). **Do not introduce either for map
tiles.**

`LV_COLOR_16_SWAP` is **1**. Any hand-built image data must match that byte order or colours come
out wrong.

Icons: prefer merged font glyphs over `lv_img` C arrays. The `LV_SYMBOL_*` set is already baked into
every Montserrat font — no separate symbol font to configure.

### Widget census (what actually exists)

`lv_label` 61, `lv_btn` 27, `lv_textarea` 23, `lv_obj` container 12 (+1 per message bubble),
`lv_dropdown` 3, `lv_table` 3, `lv_img` 2, `lv_tabview` 1 (+8 tabs), `lv_timer_create` 4.

**Never used:** `lv_switch`, `lv_slider`, `lv_chart`, `lv_canvas`, `lv_list`, `lv_checkbox`,
`lv_roller`, `lv_bar`, `lv_arc`, `lv_spinner`, `lv_msgbox_create`.

The UI is one persistent screen with an 8-tab `lv_tabview`, built once by `setDisplayLayout()`
before `loop()` starts. There is no `lv_scr_load()` anywhere — tab switching is
`lv_tabview_set_act()`. The "message list" is a hand-rolled `lv_obj` flex column, not `lv_list`.

---

## 9. Observability

### What already exists — extend this, do not duplicate it

**`src/t-deck/tdeck_debug.cpp`**, runtime-gated by `tdeck_dbg_redrawlog(bool)`, wired to
`--redrawlog on/off`:

| Line          | Content                                                                                                                                                    |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[REDRAW]`    | `ms;obj;cls;area x1,y1,x2,y2;ra;bt` — one per invalidate, with an 8-frame Xtensa backtrace. Rate-capped at 200/s, drops summarised as `[REDRAW];dropped;N` |
| `[REFRSTART]` | `ms;areas;<disp->inv_p>` from `render_start_cb`                                                                                                            |
| `[REFR]`      | `ms;px;t_ms` from `monitor_cb`                                                                                                                             |
| `[FLUSH]`     | `ms;area;px;sleeping;bl` from `disp_flush()`                                                                                                               |
| `[SCREEN]`    | `ms;crc;<8 CRC32 bands>;nonblack;total;t_ms` — reads the **physical panel** back via `tft.readRect()` under `xSemaphore`                                   |
| `[UISTAT]`    | active tab, drawer state, live object count, msg_list children, inv/refr totals, heap/PSRAM, TFT sleep, scroll position                                    |

**`src/instrument.{h,cpp}`** (marked `TEMPORARY`): `[INSTR-FLUSH]`, `[INSTR-LOOP]`, `[INSTR-HEAP]`,
`[INSTR-GUI]`. `INSTR_T0`/`INSTR_FLUSH` wrap the SPI push with `micros()` — **this is the flush-only
timer that `monitor_cb` cannot give you.** `instrument_report_heap()` already does
`heap_caps_get_free_size` / `get_minimum_free_size` / `get_largest_free_block` on
`MALLOC_CAP_INTERNAL` and `MALLOC_CAP_SPIRAM`.

**`src/printfdeb_functions.cpp`**: already a CSV/human toggle — `--debug csv` keeps `;` as a field
separator, `--debug man` collapses it. Note that `tdeck_debug.cpp` and the `[FLUSH]` line use raw
`Serial.printf` and do **not** respect that toggle.

**The invalidate hook is a pre-existing patch to vendored LVGL** — a weak
`lv_obj_invalidate_hook()` at `lib/lvgl/src/core/lv_obj_pos.c:838` plus the call at `:852`,
strong-overridden in `tdeck_debug.cpp`. Do not add a second one.

Critical property: the hook fires **after** `lv_obj_area_is_visible()`. A HIDDEN, off-screen or
fully clipped object produces **silence**, not a log line. A missing `[REDRAW]` therefore does not
distinguish "invalidate was never called" from "it was called and gated". Cross-check with an
object-tree walk before concluding anything.

### The four-stage diagnostic

| Stage                     | Question                              | Signal                                                                             |
| ------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------- |
| (a) no invalidation       | Was `lv_obj_invalidate*` reached?     | No `[REDRAW]` line → drop table §2, cases a-d, h, i                                |
| (b) queued but not drawn  | Did a refresh pass run?               | `[REDRAW]` with no `[REFRSTART]` within ~16 ms → starved or re-entered handler     |
| (c) drawn but not flushed | Did pixels leave the buffer?          | `[REFRSTART]` with no `[REFR]`/`[FLUSH]` → stuck in draw or on `xSemaphore`        |
| (d) flushed but wrong     | Did the right pixels reach the panel? | Two `[SCREEN]` CRC snapshots: unchanged band you expected to change, or vice versa |

### `LV_USE_LOG` — the biggest free win

It is `0`, so every `LV_LOG_WARN` / `LV_LOG_ERROR` inside LVGL core is `do{}while(0)` — including the
exact error for "modifying dirty areas in render" (drop case f). **Absence of a log line today
proves nothing.**

To use it: set `LV_USE_LOG 1`, `LV_LOG_LEVEL LV_LOG_LEVEL_TRACE` (the per-module switches are
already all `1` but are dead at `LV_LOG_LEVEL_WARN`), keep `LV_LOG_PRINTF 0`, and register a
callback. In 8.3 the signature is `void cb(const char *buf)` — v9 added a `level` parameter.

```c
extern "C" void tdeck_lvgl_log_cb(const char *buf) {
    Serial.print("[LVLOG];");   // the harness scans for the first '[';
    Serial.print(buf);          // LVGL's own "[Trace]\t..." would be mis-parsed as a tag
}
// after lv_init():
lv_log_register_print_cb(tdeck_lvgl_log_cb);
```

Most useful switch for "why didn't my object repaint": `LV_LOG_TRACE_DISP_REFR` — bounded output
(1 begin + up to 32 flush lines + 1 finished per cycle) and it names every rectangle actually
flushed. `LV_LOG_TRACE_EVENT` and `LV_LOG_TRACE_INDEV` are far too noisy to leave on
(`TRACE_INDEV` alone is ~100 lines/s idle with three indevs registered).

Revert before shipping — and note this repo's existing finding that `printf`-family calls malloc
above 64 B, which has previously starved NimBLE.

### What does not work in this build

- **`LV_USE_MEM_MONITOR` is dead code** — gated by `LV_MEM_CUSTOM == 0` at the call site
  (`lv_refr.c:420`), and `LV_MEM_CUSTOM` is 1. It will not even create the label. Use
  `instrument_report_heap()`.
- **`LV_USE_PERF_MONITOR`'s FPS is misleading** — it only counts refresh cycles with `px_num > 5000`
  over a rolling 300 ms window, and reports the theoretical maximum when none occurred. Every small
  partial repaint — the common case with `full_refresh = 0` — is invisible to it. Do not use it to
  characterise partial-refresh work.
- **`lv_obj_add_event_cb(NULL, ...)` crashes.** There is no global event hook in 8.3. The only NULL
  guard on that path is `LV_ASSERT_OBJ`, which is `0` here, so it NULL-derefs inside
  `lv_obj_allocate_spec_attr`. A global event tap needs a one-line weak-hook patch to
  `event_send_core()` in `lv_event.c`, mirroring the existing invalidate hook.
- **`lv_obj_class_t` has no `.name` field** in 8.3 — that is a v9 addition. Identify classes by
  pointer comparison against `extern const lv_obj_class_t lv_xxx_class`, exactly as
  `classify_obj()` already does.
- **`vTaskGetRunTimeStats()` / `uxTaskGetSystemState()` will not link** without rebuilding the
  Arduino-ESP32 core's FreeRTOS with `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` — an out-of-repo,
  multi-hour build, not a `build_flags` change. `uxTaskGetStackHighWaterMark()` and `esp_task_wdt_*`
  need no rebuild.

### `monitor_cb` semantics

`time` is **render + flush combined**, because `flush_cb` here is synchronous and calls
`lv_disp_flush_ready()` inline. It is not a render-only figure. The flush-only number is
`INSTR_FLUSH` / `[INSTR-FLUSH]`.

### Observer effect

On ESP32 there is **no drop path** — `Serial.printf` blocks the calling task until the USB-CDC ring
drains (unlike the nRF52 branch of `printfdeb_functions.cpp`, which has a 20 ms drop guard). High-rate
tracing does not lose data quietly, it **stalls the task being measured**. A ~120-byte `[REDRAW]`
line at 115200 baud is ~10 ms of wire time; 200 lines/s would be 2 s of blocking per second. This is
why `REDRAW_RATE_CAP` exists. Respect it in anything new.

### Top three probes, if you may only add three

1. Extend the disp-refresh line with correlated `inv_p` and heap — nearly free, answers "is anything
   redrawing at all, how big, how slow".
2. Turn the existing invalidate hook into a per-return-address histogram — answers "what keeps
   invalidating" and "what never does".
3. Register the LVGL/loop task with the task watchdog in `panic_mode = true` — the
   audio-blocks-the-UI symptom currently produces a silent freeze or reset with no evidence.
   Decode with
   `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/t_deck_plus/firmware.elf <addrs>`
   (toolchain at `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/`).

---

## 10. Confirmed defects, ranked

| #   | Defect                                                                                     | Location                                      | Effect                                         |
| --- | ------------------------------------------------------------------------------------------ | --------------------------------------------- | ---------------------------------------------- |
| 1   | `tft_on()` → `tft.init()` (~400-450 ms of `delay()`) runs inside the LVGL `read_cb`        | `tdeck_main.cpp:817` → `tdeck_helpers.cpp:94` | Wake touch freezes everything ~0.5 s           |
| 2   | Audio task created at priority 50 (clamped to 24) vs `loopTask` at 1, same core            | `esp32_audio.cpp:130`                         | UI starved for the whole of playback           |
| 3   | `play_cw('r')` runs synchronously on `loopTask` when the tone file is missing              | `lv_obj_functions.cpp:4005`                   | 1.1 s UI freeze per incoming message           |
| 4   | No SD access takes `xSemaphore`; it guards only the TFT side of a shared bus               | repo-wide                                     | Bus contention, possible corruption both ways  |
| 5   | `xSemaphore` is a binary semaphore, not a mutex                                            | `tdeck_main.cpp:116`                          | No priority inheritance; inversion once shared |
| 6   | Rendered message list never trimmed; only the model is capped at 50                        | `lv_obj_functions.cpp:2794` vs `:2515`        | Unbounded widget/PSRAM growth per active tab   |
| 7   | `lv_obj_scroll_to_view(..., LV_ANIM_ON)` on every incoming message                         | `lv_obj_functions.cpp:2796`                   | Dozens of full-container repaints per message  |
| 8   | `tft_on()` invalidates nothing — waking the panel leaves LVGL believing nothing is dirty   | `lv_obj_functions.cpp:1976`                   | Blank/stale screen after wake                  |
| 9   | `i2s_driver_uninstall()` from `loopTask` with no sync against the audio task               | `esp32_audio.cpp:543`                         | Cross-task race on the driver handle           |
| 10  | Built-in fonts have no glyphs above 0x7F; received message text is arbitrary UTF-8         | all `lv_font_montserrat_*.c`                  | Peer text renders as boxes, silently           |
| 11  | Dead `src/t-deck/lv_conf.h` shadows the real config                                        | —                                             | Misleads every reader                          |
| 12  | `LV_USE_LOG 0` with `LV_ASSERT_HANDLER while(1);`                                          | `variants/t_deck_plus/lv_conf.h:242`          | A failed assert is a silent hang               |
| 13  | Per-sample `i2s_write()` — ~2x the necessary CPU                                           | `Audio.cpp:4134`                              | Compounds #2                                   |
| 14  | Dead flash: `_12_SUBPX` font compiled while `LV_USE_FONT_SUBPX 0`; TFT_eSPI's own font set | `lv_conf.h:355`, `Setup210`                   | Wasted flash only                              |

Dead code worth removing while nearby: `play_file_from_sd_blocking()` (no callers), the declared-but-
undefined `lv_msgbox()` (`lv_obj_functions.h:19`), `tdeck_clear_text_ta()` (no callers).

---

## 11. Suggested order of work

1. **Defects 1 and 8** — move the TFT wake out of `read_cb` and invalidate the screen on wake. Small,
   self-contained, and the single biggest perceived-latency win.
2. **Defects 2 and 3** — fix the audio task priority; route the CW fallback off `loopTask`. Also
   small, and directly answers "audio blocks the device".
3. **Defects 6 and 7** — trim the rendered list, drop the scroll animation.
4. **Defects 4 and 5** — convert `xSemaphore` to a recursive mutex and take it on every SPI path.
   Do this before any DMA work.
5. **Defect 11** — delete the dead `lv_conf.h`.
6. **Buffers and DMA** — two ~1/8-screen buffers in internal DRAM, `initDMA`, and resolve the
   `endWrite`/CS question on hardware. This is the largest change; do it last and measure it alone.
7. **Defect 10** — regenerate fonts with an extended range once the sizes are settled.

Every step is independently verifiable with the existing `[REDRAW]` / `[REFR]` / `[FLUSH]` /
`[SCREEN]` harness and `tools/bench/tdeck_harness.py`. Measure before and after; do not stack
changes.

---

## 12. Open questions

- **`endWrite()` vs DMA-in-flight CS timing** with `initDMA(ctrl_cs=false)` in this TFT_eSPI version.
  Reasoned from source, not measured. Highest-risk unverified item — it affects data integrity, not
  just performance.
- **80 MHz SPI reliability** on this specific board. Plausible from the short fixed PCB traces;
  never measured on this unit.
- **PSRAM throughput multiplier** vs internal SRAM for this exact module. Confirmed slower and
  DMA-hostile; no board-specific number found.
- **LittleFS/SPIFFS space** in `partitions-16MB-safeboot.csv` for moving notification sounds off SD.
- **GT911 interrupt mode** — whether the vendored driver exposes it.
- **Exhaustive cross-task `lv_*` audit.** The "single-task" conclusion is based on reading
  `src/t-deck/*`, `esp32_audio.cpp`, `net_console.cpp` and the message dispatch chain — not all ~40
  `.cpp` files compiled into `t_deck_plus`. A stray `lv_*` call from a webserver handler was not
  ruled out with certainty.
- **`lv_async_call` and the mutex.** That it needs the LVGL mutex when called off-task follows from
  the general "every `lv_*` call" rule and community guidance (lvgl/lvgl#8237), but was not found
  stated explicitly in the 8.3 porting docs.
- **`TDECK_TFT_CS` (GPIO 12)** is commented `// Not connected` in `Setup210_LilyGo_T_Deck.h` while
  the firmware actively drives it (`tdeck_main.cpp:91,94`). Treated as a stale comment; not confirmed
  against a schematic.
