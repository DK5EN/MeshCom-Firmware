# Track 7 — Full debug observability for an LVGL 8.3 UI on ESP32-S3

See `00-CONTEXT.md` for shared constraints. This track is written against the actual state of the
repo on branch `tdeck-partial-refresh-trace`, not a green-field design: **three instrumentation
layers already exist and are wired into the firmware.** Everything below either documents what is
already there (so the agent extends it instead of duplicating it) or fills a genuine, verified gap.

## What already exists — read this before adding anything

1. **`src/t-deck/tdeck_debug.cpp` / `.h`** — redraw/UI observability, gated by a runtime flag
   (`tdeck_dbg_redrawlog(bool)`), wired to `--redrawlog on/off` in `src/command_functions.cpp`.
   - `[REDRAW];ms;..;obj;0xADDR;cls;<name>;area;x1;y1;x2;y2;ra;0xRETADDR;bt;pc,pc,..[;name;..]` —
     one line per `lv_obj_invalidate_area()` call, rate-capped at 200 lines/s
     (`REDRAW_RATE_CAP`), with a `[REDRAW];dropped;N` summary line once/s when the cap is hit.
   - `[REFR];ms;..;px;..;t_ms;..` from `disp_drv.monitor_cb` (already wired,
     `tdeck_dbg_monitor_cb`), `[REFRSTART];ms;..;areas;N` from `disp_drv.render_start_cb`
     (`tdeck_dbg_render_start_cb`, reads `disp->inv_p`).
   - `[UISTAT]` one-shot snapshot: active tab, drawer state, live object count
     (`count_objs_recursive`, walks `lv_obj_get_child_cnt`/`lv_obj_get_child`), msg_list child
     count, invalidate/refresh totals, heap/PSRAM free, TFT sleep state, scroll position.
   - `[SCREEN];ms;..;crc;<8 CRC32s>;nonblack;..;total;..;t_ms;..` — reads back the panel frame
     memory in 8 bands via `tft.readRect()` under `xSemaphore`, fingerprints each band. This is the
     only way to prove pixels actually changed on the physical panel, independent of what LVGL
     _thinks_ it drew — use it to catch a flush that "succeeded" but wrote stale/garbage data.
   - `classify_obj()` maps an `lv_obj_t*` to a short class name **by pointer comparison against
     `extern const lv_obj_class_t lv_xxx_class` symbols** (`&lv_label_class`, `&lv_btn_class`, …).
     This is the correct v8.3 technique — see Finding 5.1.
   - **The invalidation hook itself is a pre-existing patch to vendored LVGL**, not something to
     add — see Finding 4.1.
2. **`src/instrument.h` / `src/instrument.cpp`** — flush/loop/heap/GUI-count scaffolding, marked
   `TEMPORARY` (removable via `git revert` or `#define INSTRUMENT_ENABLED 0`).
   - `INSTR_T0(v)` / `INSTR_FLUSH(v)` wrap the SPI push in `disp_flush()`
     (`src/t-deck/tdeck_main.cpp:464-466`) with `micros()`, feeding `instrument_note_flush()`.
     This is the **flush-only** timer that `monitor_cb` cannot give you (see Finding 2.1) — reuse
     it, do not re-invent it.
   - `INSTR_LOOPTICK()` in the Arduino main loop (`src/esp32/esp32_main.cpp:1838`) times the gap
     between successive loop iterations — catches a stall regardless of which call site caused it.
   - `instrument_report_heap(tag)` already does exactly what Finding 3.2 below asks for:
     `heap_caps_get_free_size/get_minimum_free_size/get_largest_free_block` on
     `MALLOC_CAP_INTERNAL` and `MALLOC_CAP_SPIRAM`. Wired to `--heap` and `--heap <tag>`.
   - `instrument_report_timing()` → `[INSTR-FLUSH]`/`[INSTR-LOOP]` (n/total_us/avg_us/max_us).
   - `instrument_report_gui()` → `[INSTR-GUI]` (msg_list children, active-tab bubble count,
     persisted message model count, map point count) — a leak detector for one specific known
     hypothesis (H1 in `docs/tdeck-findings-20260828.md`), not a generic object-leak counter.
   - All wired to `--instr` (reset/heap/timing/gui in one shot) in `src/command_functions.cpp`.
3. **`src/printfdeb_functions.cpp` / `src/printfdeb_format.h`** — the repo's general debug-print
   layer, **already a CSV/human toggle**: `--debug csv` sets `bDEBUGCSV=true` and `;` in every
   `printfdeb()` format string stays a real field separator; `--debug man` (default) collapses `;`
   to a space for human reading. **This is the machine-parseable design Finding 7 below asks for —
   it already exists at the `printfdeb()` layer.** Two caveats that matter to any new trace code:
   - `tdeck_debug.cpp` and `instrument_note_flush`'s `[FLUSH]` line in `disp_flush()` call
     **raw `Serial.printf`**, not `printfdeb()` — their `;` separators are unconditional and do
     **not** respect `--debug csv/man`. New trace lines must pick one convention and say which.
   - On ESP32 (unlike the nRF52 branch of `printfdeb_functions.cpp`, which has a 20 ms
     drop-not-block guard, `cdcReady()`), there is **no drop path** — `Serial.printf`/`printfdeb`
     block the calling task until the USB-CDC TX ring buffer drains. High-rate tracing does not
     lose data quietly; it stalls the task that logs. This is the mechanism behind the
     observer-effect arithmetic in Finding 7.

None of the above is LVGL's own log module (`LV_USE_LOG`) — that is currently **off**
(`src/t-deck/lv_conf.h:233`, `#define LV_USE_LOG 0`) and nothing in the firmware uses it. Findings
1–3 below cover what turning it on would and would not buy over the existing custom hooks.

## TL;DR for the coding agent

- Do not re-implement redraw tracing, flush timing, heap probing or CSV/human toggling — they
  exist (`tdeck_debug.cpp`, `instrument.cpp`, `printfdeb_functions.cpp`). Extend them.
- `LV_USE_LOG` is off. Turning it on for `LV_LOG_TRACE_DISP_REFR`/`EVENT`/etc. is free (compiles to
  nothing when off) but its output format (tab-delimited, starts with `[Trace]`/`[Info]`/...) will
  collide with the existing harness convention of "skip to the first `[`" — see Finding 1.3.
- `disp_drv.monitor_cb`'s `time` argument is **render + flush combined**, because this driver's
  `flush_cb` is synchronous (no DMA, `lv_disp_flush_ready()` called inline). It is not a
  render-only figure. The flush-only figure already exists separately: `INSTR_FLUSH`/`[INSTR-FLUSH]`.
- `LV_USE_MEM_MONITOR` is dead code in this build: gated by `LV_MEM_CUSTOM == 0` at the call site
  (`lv_refr.c:420`), and this repo has `LV_MEM_CUSTOM 1`. Do not enable it and expect output — it
  will not even create the label. The substitute (`heap_caps_get_free_size` etc.) already exists
  in `instrument_report_heap()`.
- `LV_USE_PERF_MONITOR`'s FPS number only counts refresh cycles where `px_num > 5000`, averaged
  over a rolling 300 ms window, capped at the refresh timer's theoretical max (100 fps at
  `LV_DISP_DEF_REFR_PERIOD=10`). Every small partial-repaint cycle (the common case once
  `full_refresh=0`) is invisible to it. Do not use it to characterize partial-refresh performance.
- The per-object invalidation hook (`lv_obj_invalidate_hook`, weak symbol in
  `lib/lvgl/src/core/lv_obj_pos.c:838`, already overridden in `tdeck_debug.cpp`) is a **pre-existing
  patch to vendored LVGL**. Do not add a second one. It fires only for invalidations that pass
  `lv_obj_area_is_visible()` — a HIDDEN object, an off-screen object, or one fully clipped by an
  ancestor produces **silence**, not a log line. Cross-check with the object-tree walker (Finding 5) before concluding "invalidate is never called."
- `lv_obj_add_event_cb(NULL, cb, LV_EVENT_ALL, NULL)` is **not valid** in LVGL 8.3 and will crash
  (NULL deref inside `lv_obj_allocate_spec_attr`) in this build, because `LV_USE_ASSERT_OBJ` (the
  only compiled-in NULL check on that path) is `0` here. A global event tap needs a one-line patch
  to `lv_event.c`'s `event_send_core()`, mirroring the existing invalidate-hook pattern — see
  Finding 5.3. This corrects the approach suggested in the shared brief.
- `lv_obj_class_t` (LVGL 8.3) has **no `.name` field** — that is a v9 addition. Class identification
  must be by pointer comparison against `extern const lv_obj_class_t lv_xxx_class`, exactly as
  `classify_obj()` already does.
- `vTaskGetRunTimeStats()`/`uxTaskGetSystemState()` will not link against this repo's precompiled
  Arduino-ESP32 2.x core (`platform = espressif32 @ 6.6.0`) without rebuilding the framework's
  FreeRTOS with `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y` — a multi-hour, out-of-repo build step,
  not a `build_flags` addition. Budget it only if per-task CPU% is a hard requirement;
  `uxTaskGetStackHighWaterMark()` and `esp_task_wdt_*` need no such rebuild.
- If only three probes may be added, add: (1) the disp-refresh CSV line (Finding 2), because it is
  nearly free and answers "is anything redrawing at all, how big, how slow"; (2) turn the
  already-patched invalidate hook into a per-return-address histogram (Finding 4.3), because it
  answers "what keeps invalidating" and "what never does"; (3) a task-watchdog-backed backtrace on
  the LVGL/loop task (Finding 6.4), because the audio-blocks-the-UI symptom currently manifests as
  a silent freeze or reset with no evidence at all.

## Findings

### 1. LVGL 8.3 built-in logging (`LV_USE_LOG`, per-module trace switches)

**1.1 — Current state: off, and every one of the eight trace switches is already set to `1` in
`lv_conf.h`, waiting for `LV_USE_LOG` to flip on.**

**Claim**: `src/t-deck/lv_conf.h:233` has `#define LV_USE_LOG 0`. Lines 250-257 already define
`LV_LOG_TRACE_MEM/TIMER/INDEV/DISP_REFR/EVENT/OBJ_CREATE/LAYOUT/ANIM` all to `1`, but they are
dead while `LV_USE_LOG` is 0 (each is only consulted inside `#if LV_USE_LOG && LV_LOG_TRACE_x`
guards, e.g. `lib/lvgl/src/core/lv_obj.h:399`).
**Why**: whoever configured this file intended to use per-module tracing and left the switches
primed; only the master switch needs to move.
**Symptom if violated**: assuming any `LV_LOG_TRACE_*` output exists today — it does not; grepping
the firmware's serial log for `[Trace]` finds nothing.
**Fix**: flip `LV_USE_LOG` to `1` only for a debug build variant (do not ship it on permanently —
see 1.3 for the cost), keep `LV_LOG_LEVEL` at `LV_LOG_LEVEL_TRACE` to let the per-module switches
matter, keep `LV_LOG_PRINTF 0`, and register a print callback (1.2).
**Source**: `src/t-deck/lv_conf.h:233-259` (read directly).

**1.2 — `LV_LOG_PRINTF` vs `lv_log_register_print_cb()`, and the exact v8.3 output shape.**

**Claim**: with `LV_LOG_PRINTF 1`, LVGL calls the C library `printf()` directly
(`lib/lvgl/src/misc/lv_log.c:88-91`) — on Arduino-ESP32 this is not guaranteed to reach the same
USB-CDC `Serial` stream the rest of the firmware uses (stdio routing depends on
`ARDUINO_USB_CDC_ON_BOOT`/IDF console config); do not rely on it. With `LV_LOG_PRINTF 0`, LVGL
formats into a **512-byte stack buffer** and calls the registered callback exactly once per log
call: `void my_cb(const char * buf)` — a single, already-formatted string, in LVGL 8.3. The exact
format (`lib/lvgl/src/misc/lv_log.c:98-101`):

```
[Trace]\t(12345.678, +12)\t lv_obj_invalidate: message text \t(in lv_obj_pos.c line #845)\n
```

i.e. level name, seconds.millis timestamp, delta-ms since the previous log call, calling function
name, the formatted message, source file (basename only) and line — tab-separated, one call to
`_lv_log_add()` per line, real `\n` terminated.
**Why it matters here**: `_lv_log_add()` truncates the _message_ to 255 bytes
(`char msg[256]` under `LV_SPRINTF_CUSTOM`) before formatting into the 512-byte `buf`; a very long
custom log call is silently truncated, not overflowed.
**LVGL 9 difference**: the callback signature changed to
`void my_cb(lv_log_level_t level, const char * buf)` — a `level` parameter was added. Code
written against a v9 tutorial/example will not compile against this vendored v8.3 tree.
**Fix**:

```c
extern "C" void tdeck_lvgl_log_cb(const char * buf)
{
    /* Not printfdeb(): LVGL's own '\t' and level-prefix format does not
     * match the ';'-delimited convention; keep it on its own tag so the
     * harness parser (which scans for the first '[') is not confused by
     * this also starting with '['. */
    Serial.print("[LVLOG];");
    Serial.print(buf);   /* buf already ends in '\n' */
}
/* in setup(), after lv_init(): */
lv_log_register_print_cb(tdeck_lvgl_log_cb);
```

**Symptom if the `[LVLOG];` prefix is skipped**: the existing bench harness
(`tools/bench/tdeck_harness.py`, see `docs/tdeck-handover.md` §1.2) "skips to the first `[`" to
strip the echoed command; an unprefixed `[Trace]\t...` line also starts with `[` and will be
mis-parsed as a tag by any code that assumes `[TAGNAME];` is the shape.
**Source**: `lib/lvgl/src/misc/lv_log.c:64-118` (read directly);
[LVGL 9.3 Logging docs, callback signature](https://docs.lvgl.io/9.3/details/debugging/log.html)
(cross-checked against v8.3 header, confirms the v9 signature added `level`).

**1.3 — The eight per-module switches: exact call sites, noise, and what each diagnoses.**

All eight compile to nothing unless `LV_USE_LOG` is 1; each guards a small number of call sites
that only fire under `LV_LOG_LEVEL <= LV_LOG_LEVEL_TRACE` (i.e. `LV_LOG_LEVEL` must be `TRACE`,
not just `LV_USE_LOG` being on — leaving `LV_LOG_LEVEL_WARN` as in the current `lv_conf.h` line 243
disables all eight regardless of their own value).

| Switch                    | File(s)                                                                                                                  | Call sites                                                                                                                                                                                                                                             | Noise                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | Diagnoses                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `LV_LOG_TRACE_MEM`        | `lib/lvgl/src/misc/lv_mem.c`                                                                                             | `lv_mem_alloc`/`lv_mem_free`/`lv_mem_realloc` entry+exit (`MEM_TRACE`, e.g. line 127/129/160)                                                                                                                                                          | High: fires on **every LVGL-internal allocation** (draw buffers, style copies, event descriptors, object struct itself via `lv_obj_class_create_obj`) — hundreds/sec during any layout churn. **Note**: still active even with `LV_MEM_CUSTOM 1`, because `lv_mem_alloc()` is a thin wrapper that calls `LV_MEM_CUSTOM_ALLOC` (`ps_malloc`) and traces around it (`lv_mem.c:127-160`); it does **not** trace the app's own direct `ps_malloc()`/`malloc()` calls (e.g. in `tdeck_debug.cpp`'s screen-CRC buffer), only LVGL's own internal ones. | Allocation storms, a specific widget's construction cost, whether an internal alloc failed (also visible without tracing via `LV_LOG_INFO` on failure, `lv_mem.c:139`).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `LV_LOG_TRACE_TIMER`      | `lib/lvgl/src/misc/lv_timer.c`                                                                                           | `TIMER_TRACE`, begin/per-timer-run/finished (`lv_timer.c:69,74,112,...`)                                                                                                                                                                               | Moderate: one "begin" per `lv_timer_handler()` call (i.e. once per `lv_task_handler()` call site — and this repo calls `lv_task_handler()` from busy-wait loops too, see `00-CONTEXT.md`), plus one line per timer that actually runs.                                                                                                                                                                                                                                                                                                           | Whether `lv_task_handler()`/`lv_timer_handler()` is being starved (a busy-wait elsewhere blocking the main loop means no "begin" lines at all — a **timing gap** in the trace is the signal, not a specific line).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `LV_LOG_TRACE_INDEV`      | `lib/lvgl/src/hal/lv_hal_indev.c`                                                                                        | one line before `indev_read_cb` is invoked (`lv_hal_indev.c:185`)                                                                                                                                                                                      | High and constant: fires once per registered indev per `LV_INDEV_DEF_READ_PERIOD` (30 ms here) **regardless of activity** — 3 indevs (touch, mouse/trackball, keypad) registered on T-Deck ⇒ ~100 lines/s baseline even fully idle.                                                                                                                                                                                                                                                                                                              | Whether an indev's read callback is even being called (rules out "indev task died" vs "indev reports no change"); does not by itself show what data was read.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `LV_LOG_TRACE_DISP_REFR`  | `lib/lvgl/src/core/lv_refr.c`                                                                                            | `REFR_TRACE("begin")`/`"finished"` once per `_lv_disp_refr_timer()` call (lines 289, 319, 451), **plus one line per actually-flushed rectangle** inside `call_flush_cb` (line 1317, `"Calling flush_cb on (x1;y1)(x2;y2) area with %p image pointer"`) | Moderate, bounded: at most 1 begin + `LV_INV_BUF_SIZE` (32) flush lines + 1 finished per refresh cycle; in practice 2-6 lines per cycle at this repo's measured invalidation rates (`docs/tdeck-findings-20260828.md` §2-3).                                                                                                                                                                                                                                                                                                                     | Whether a refresh cycle ran at all in a given window (a missing "begin"/"finished" pair means the refresh timer never fired — check `disp->inv_p`, `lv_disp_is_invalidation_enabled()`, or a paused `refr_timer`), and exactly which rectangles were flushed to the panel. **This is the closest single stock switch to "why didn't my object repaint"**, but it only proves a refresh cycle happened and what it flushed — it does not know about your object. Combine it with the already-existing `[REDRAW]` invalidate-hook log (Finding 4) and the object-tree walker (Finding 5) to get the actual answer: no `[REDRAW]` line for the object ⇒ invalidate was never called or was filtered by `lv_obj_area_is_visible()` (HIDDEN/off-screen/clipped, see Finding 4.2); a `[REDRAW]` line but no matching flushed rectangle in `DISP_REFR` trace ⇒ the refresh timer didn't run yet (still coalescing) or was starved by a blocked main loop. |
| `LV_LOG_TRACE_EVENT`      | `lib/lvgl/src/core/lv_event.c`                                                                                           | `EVENT_TRACE("Sending event %d to %p with %p param", e->code, ...)`, one line per `lv_event_send()` (line 428)                                                                                                                                         | Very high: fires for **every** event, including internal draw-phase events (`LV_EVENT_DRAW_MAIN`, `LV_EVENT_DRAW_PART_BEGIN`/`END`) that fire once per widget per frame — thousands/sec during any full-screen redraw. Only prints the numeric event code, not a name.                                                                                                                                                                                                                                                                           | Whether a specific event ever reaches `event_send_core()` at all; combine with Finding 5.3's name table since the trace only prints the integer code.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `LV_LOG_TRACE_OBJ_CREATE` | `lib/lvgl/src/core/lv_obj_class.c` (lines 45, 55, 82), `lib/lvgl/src/core/lv_obj.c` (lines 422, 447, in the delete path) | Once per `lv_obj_class_create_obj()` call and once per `lv_obj_del()` call                                                                                                                                                                             | Low: proportional to widget churn, not to redraw rate — cheap to leave on.                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | Object construction/destruction order and count; the leading suspect for a leak that the object-count field in `[UISTAT]`/`[INSTR-GUI]` already flags in aggregate — this switch gives per-object attribution.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `LV_LOG_TRACE_LAYOUT`     | `lib/lvgl/src/extra/layouts/flex/lv_flex.c:312`, `lib/lvgl/src/extra/layouts/grid/lv_grid.c:382`                         | `"finished"` only, once per flex/grid layout pass                                                                                                                                                                                                      | Low-moderate: this repo's UI is built with fixed coordinates predominantly (`lv_obj_functions.cpp`), so flex/grid layout passes should be rare; a burst of these lines pinpoints unexpected layout recalculation (e.g. from `LV_EVENT_SIZE_CHANGED` cascading).                                                                                                                                                                                                                                                                                  | Whether a flex/grid container is recalculating more often than expected — does not cover manual-coordinate widgets, which never hit this code path.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| `LV_LOG_TRACE_ANIM`       | `lib/lvgl/src/misc/lv_anim.c`                                                                                            | `TRACE_ANIM("begin")`/`"finished"`, lines 78 and 113, around `lv_anim_start()`                                                                                                                                                                         | Low: proportional to animation starts, not per-frame.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | Whether an animation was actually registered/started (LVGL animations run through the same timer subsystem — a missing "begin" for an expected animation points at the call site, not the animation engine).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |

**Source**: exact line numbers from `lib/lvgl/src/core/lv_refr.c`, `lv_event.c`,
`lv_obj_class.c`, `lv_obj.c`, `lib/lvgl/src/misc/lv_mem.c`, `lv_timer.c`, `lv_anim.c`,
`lib/lvgl/src/hal/lv_hal_indev.c`, `lib/lvgl/src/extra/layouts/{flex,grid}/*.c` (all read directly
in this repo's vendored copy).

### 2. Driver hooks: `monitor_cb`, `render_start_cb`, `clean_dcache_cb`, `feedback_cb`

**2.1 — `monitor_cb(disp_drv, time, px)`: `time` is render _and_ flush combined in this driver,
because `flush_cb` is synchronous.**

**Claim**: `lib/lvgl/src/core/lv_refr.c` computes `elaps = lv_tick_elaps(start)`
(line ~348) **after** `refr_invalid_areas()` returns (line 320), and `refr_invalid_areas()`
synchronously calls `call_flush_cb()` (line 1315→1327, `drv->flush_cb(drv, &offset_area, color_p)`)
as part of drawing each area. `monitor_cb` is then called with this combined `elaps` and the
accumulated `px_num` (line 351-352). This is generically true for _any_ LVGL 8.3 driver whose
`flush_cb` blocks until the transfer is done and calls `lv_disp_flush_ready()` inline — which is
exactly this repo's `disp_flush()` (`src/t-deck/tdeck_main.cpp:458-476`: `tft.startWrite()` /
`pushColors()` / `tft.endWrite()`, no DMA, `lv_disp_flush_ready()` called before the function
returns). If a driver instead kicks off a DMA transfer and calls `lv_disp_flush_ready()` from an
ISR later, `monitor_cb`'s `time` would be render-only. **For this repo it is render+flush.**
**Why it matters**: the existing `docs/tdeck-findings-20260828.md` §2 already reports this exact
combined figure ("Mean/max refresh 56.9/61 ms (monitor_cb, render + flush)") separately from the
flush-only figure from `INSTR_FLUSH`/`[INSTR-FLUSH]` (avg 36.7 ms) — the ~20 ms gap between the two
is the render-only cost (layout, drawing into `draw_buf`, mask/gradient work under
`LV_DRAW_COMPLEX 1`). Conflating the two would misattribute render-vs-transfer cost.
**Symptom if violated**: reporting `monitor_cb`'s `time` as "flush time" or "SPI time" overstates
the driver's transfer cost and understates LVGL's own drawing cost, or vice versa.
**Fix**: keep both instruments — `monitor_cb` (already wired, `tdeck_dbg_monitor_cb`) for the
combined figure, `INSTR_FLUSH` (already wired) for flush-only; render-only is the difference
`monitor_cb.time - INSTR_FLUSH` for the _same_ refresh cycle (correlate by the shared `millis()`
timestamp printed by both `[REFR]` and `[INSTR-FLUSH]`/`[FLUSH]`, or accumulate flush time inside
`render_start_cb`...`monitor_cb` and subtract once per cycle rather than per `flush_cb` call, since
one refresh cycle can flush multiple rectangles).
**Source**: `lib/lvgl/src/core/lv_refr.c:289-353,587-624,1315-1327` (read directly); `src/t-deck/
tdeck_main.cpp:458-476`, `src/instrument.cpp:35-41`, `docs/tdeck-findings-20260828.md` §2-3.

**2.2 — Ready-to-use one-line-per-frame CSV emitter, layered on the existing hooks.**

Both `monitor_cb` and `render_start_cb` are already wired
(`src/t-deck/tdeck_main.cpp:399-400`). Extend `tdeck_dbg_monitor_cb` in `tdeck_debug.cpp` rather
than adding a third callback — `disp_drv_t` has exactly one slot for each, so a second
implementation would have to chain through the first anyway:

```c
extern "C" void tdeck_dbg_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t time_ms, uint32_t px)
{
    (void)disp_drv;
    s_refr_total++;
    s_last_refr_px = px;
    s_last_refr_ms = time_ms;

    if(!s_redrawlog_on) return;

    lv_disp_t * disp = lv_disp_get_default();
    uint32_t inv_areas = (disp != NULL) ? disp->inv_p : 0;   /* NOTE: already reset to 0 by the
                                                                 time monitor_cb runs -- see 2.1's
                                                                 call order. Capture inv_p inside
                                                                 render_start_cb instead (already
                                                                 done, [REFRSTART]) and correlate
                                                                 by timestamp, or add a static
                                                                 counter incremented once per
                                                                 render_start_cb call. */
    Serial.printf("[FRAME];ms;%lu;areas;%lu;px;%lu;render_flush_ms;%lu;"
                  "heap_int;%lu;heap_psram;%lu\n",
                  (unsigned long)millis(), (unsigned long)inv_areas, (unsigned long)px,
                  (unsigned long)time_ms,
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
```

`disp->inv_p` is already zeroed by `_lv_disp_refr_timer` (`lv_refr.c:339`) before `monitor_cb`
runs, so the area count must come from `render_start_cb`'s snapshot
(`tdeck_dbg_render_start_cb` already prints it as `[REFRSTART];areas;N`) — correlate the two lines
by their shared `millis()` timestamp rather than trying to read `inv_p` a second time inside
`monitor_cb`. This is a real gotcha in the existing code (`tdeck_debug.cpp:209-211`,
`tdeck_debug.cpp:192-202`): the two counters are correct individually but cannot both be read from
inside `monitor_cb`.
**Source**: `lib/lvgl/src/core/lv_refr.c:339` (`disp_refr->inv_p = 0;`), `src/t-deck/
tdeck_debug.cpp:192-212` (read directly).

**2.3 — `clean_dcache_cb`: not applicable to this driver.**

**Claim**: `clean_dcache_cb` exists to flush CPU data-cache lines before a DMA-capable peripheral
reads the draw buffer, and is called from `lv_disp.c:416-417` and three GPU backend files (NXP
PXP/VG-Lite, Arm-2D — none of which are enabled here). This driver's `flush_cb` uses
`tft.pushColors()`, a blocking, CPU-driven SPI write with no DMA and no cache-coherency concern.
**Fix**: do not wire `clean_dcache_cb` — there is nothing for it to do until the flush path moves
to a DMA-driven SPI transfer, at which point it becomes necessary again (ESP32-S3's SPI DMA reads
from PSRAM/DRAM, and the D-cache over PSRAM must be flushed before a DMA engine reads it).
**Source**: `lib/lvgl/src/core/lv_disp.c:410-418`, `lib/lvgl/src/draw/nxp/*`,
`lib/lvgl/src/draw/arm2d/lv_gpu_arm2d.c:1564` (read directly).

**2.4 — `indev_drv.feedback_cb(indev_drv, code)`: fires for every event during indev processing,
not just events on the object the indev touched.**

**Claim**: `feedback_cb` is called from `lv_event.c:433` inside `event_send_core()`, guarded only
by `lv_indev_get_act() != NULL` — i.e. it fires for **every** `lv_event_send()` call that happens
while any input device's read/process cycle is active, which in practice is most of the time
(`LV_INDEV_DEF_READ_PERIOD` 30 ms, and processing runs after every read). It is not scoped to the
specific object the indev is pressing/dragging.
**Why it matters**: using `feedback_cb` for "play a click sound on touch" (a stated wish in
`00-CONTEXT.md` — fast touch feedback) requires filtering by `code` (e.g. only
`LV_EVENT_PRESSED`/`LV_EVENT_CLICKED`) inside the callback; treating every call as "the user
touched something" will fire on internal draw events too.
**Fix**:

```c
static void indev_feedback_cb(lv_indev_drv_t * drv, uint8_t code)
{
    if(code == LV_EVENT_PRESSED) { /* fast, cheap feedback only, e.g. set a flag consumed by loop() */ }
}
/* indev_drv.feedback_cb = indev_feedback_cb; for the touchpad/keypad drv, before lv_indev_drv_register() */
```

**Source**: `lib/lvgl/src/core/lv_event.c:428-434`, `lib/lvgl/src/hal/lv_hal_indev.h:98` (read
directly).

### 3. Performance and memory monitors

**3.1 — `LV_USE_PERF_MONITOR`'s FPS figure only counts near-full-screen refresh cycles.**

**Claim**: in `_lv_disp_refr_timer()` (`lib/lvgl/src/core/lv_refr.c:363-419`), the FPS accumulator
only advances when `px_num > 5000` (line 381) — a hard-coded threshold, not configurable. Every
refresh cycle with `px_num <= 5000` (a single label update, a small icon, most partial-refresh
activity once `full_refresh=0`) updates neither `elaps_sum` nor `frame_cnt`. Every 300 ms
(`lv_tick_elaps(perf_monitor.perf_last_time) < 300`, line 385), the window is closed: `fps =
1000 * frame_cnt / elaps_sum`, capped at `fps_limit = 1000 / refr_timer->period` (100 for this
repo's 10 ms period), and if **no** qualifying (>5000 px) cycle occurred in the 300 ms window,
`fps` is reported as `fps_limit` itself (line 402-404: `if frame_cnt == 0, fps = fps_limit`) — i.e.
**an idle-but-not-fully-quiescent partial-refresh UI reports a misleadingly perfect FPS number**,
because "no qualifying frame" is treated the same as "hit the frame-rate ceiling."
`cpu = 100 - lv_timer_get_idle()` is LVGL's own idle-time accounting (time spent inside
`lv_timer_handler()` doing nothing vs. running timers), not an OS-level CPU percentage — it says
nothing about time spent outside `lv_timer_handler()` (e.g. in `esp32loop()`'s other work, or in a
blocking audio call).
**Why it matters**: with `disp_drv.full_refresh` currently being trialled at `0`
(`00-CONTEXT.md`), turning on `LV_USE_PERF_MONITOR` to "measure the redraw fix" would report a
number dominated by whichever full-screen tab switches happen to occur in each 300 ms sample, and
silently paper over the partial-refresh traffic the fix is actually about.
**Fix**: do not use `LV_USE_PERF_MONITOR` to characterize this firmware's redraw behavior. Use the
existing `[REFR]`/`monitor_cb` line (Finding 2.2) and compute your own rate/throughput from it —
every refresh cycle is captured, not just >5000 px ones.
**Source**: `lib/lvgl/src/core/lv_refr.c:363-419` (read directly, full function reproduced above in
research — every branch traced).

**3.2 — `LV_USE_MEM_MONITOR` requires `LV_MEM_CUSTOM == 0` and is compiled out entirely here; the
substitute already exists in this repo.**

**Claim**: the mem-monitor label creation block in `lv_refr.c` is gated
`#if LV_USE_MEM_MONITOR && LV_MEM_CUSTOM == 0 && LV_USE_LABEL` (line 420) — with `LV_MEM_CUSTOM 1`
in this repo's `lv_conf.h`, the entire block does not exist in the compiled binary even if
`LV_USE_MEM_MONITOR` is turned on; it is not merely "a label with zeros in it", the code simply
is not there. Separately, the underlying `lv_mem_monitor()` API function itself is also a no-op
under `LV_MEM_CUSTOM 1` (`lib/lvgl/src/misc/lv_mem.c:246-268`: the entire body is
`#if LV_MEM_CUSTOM == 0 ... #endif`, so it returns a zero-initialized `lv_mem_monitor_t` and does
nothing else) — so even calling it directly from application code would report all zeros, not an
error and not real data.
**Fix**: use `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` (LVGL's object heap, since
`LV_MEM_CUSTOM_ALLOC` is `ps_malloc`) and `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` /
`heap_caps_get_largest_free_block(...)` / `heap_caps_get_minimum_free_size(...)` for
fragmentation and worst-case-ever tracking. **This already exists**:
`src/instrument.cpp:71-79` (`instrument_report_heap`, wired to `--heap`/`--heap <tag>`), reporting
exactly `int_free`/`int_min`/`int_largest`/`psram_free`/`psram_largest`. Add a PSRAM
`_min`/minimum-free equivalent if fragmentation _in PSRAM_ (where LVGL's object heap lives) needs
tracking too — `instrument_report_heap` currently tracks minimum-free only for
`MALLOC_CAP_INTERNAL`, not `MALLOC_CAP_SPIRAM`.
**Source**: `lib/lvgl/src/core/lv_refr.c:420-451`, `lib/lvgl/src/misc/lv_mem.c:246-268` (both read
directly), `src/instrument.cpp:71-79`, [ESP-IDF Heap Memory Allocation
docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html)
(API surface for `heap_caps_*`, `multi_heap_info_t`).

### 4. Tracing invalidation itself

**4.1 — The per-object invalidation hook already exists as a patch to vendored LVGL. Do not
re-patch it.**

**Claim**: `lib/lvgl/src/core/lv_obj_pos.c:838` defines
`void __attribute__((weak)) lv_obj_invalidate_hook(const lv_obj_t * obj, const lv_area_t * area,
void * ret_addr) { (void)obj;(void)area;(void)ret_addr; }`, called from `lv_obj_invalidate_area()`
at line 852, immediately before `_lv_inv_area()`. This is **not stock LVGL 8.3.11** — it is a
6-line addition to the vendored copy (marked `/* MeshCom: redraw trace hook */` at both the
definition and call site), already overridden with a strong (non-weak) symbol in
`tdeck_debug.cpp:151` (`lv_obj_invalidate_hook`), which walks the Xtensa call stack
(`esp_backtrace_get_next_frame`) and emits the `[REDRAW]` line described above. `ret_addr` is
`__builtin_return_address(0)` taken **inside `lv_obj_invalidate_area`**, so it always points into
`lv_obj_invalidate()` (the near-universal caller), not into user code — the backtrace walk exists
specifically to get past that one useless frame (`docs/tdeck-handover.md`'s bench-facts §5 already
documents this exact gotcha).
**Symptom if violated (i.e. if an agent adds a second, competing hook)**: a linker error (duplicate
strong symbol) if done the same way, or silently-lost coverage if done via a different mechanism
that the existing `--redrawlog` command doesn't know about.
**Fix**: extend `tdeck_dbg_redrawlog` / `lv_obj_invalidate_hook` in `tdeck_debug.cpp`, do not touch
`lv_obj_pos.c` again.
**Source**: `lib/lvgl/src/core/lv_obj_pos.c:838,852` and `src/t-deck/tdeck_debug.cpp:133-190` (both
read directly).

**4.2 — The hook is downstream of `lv_obj_area_is_visible()`: a HIDDEN, off-screen, or
fully-clipped object produces silence, not a log line.**

**Claim**: `lv_obj_invalidate_area()` (`lv_obj_pos.c:840-854`) checks
`lv_disp_is_invalidation_enabled(disp)` and then `lv_obj_area_is_visible(obj, &area_tmp)` **before**
calling the hook; both return early on failure, skipping the hook entirely. `lv_obj_area_is_visible`
(`lv_obj_pos.c:873-918`) returns `false` for any of: the object itself has `LV_OBJ_FLAG_HIDDEN`;
its screen is not the active/previous screen or the top/sys layer; the area doesn't intersect the
object's own bounds (+ext-draw-size, unless `LV_OBJ_FLAG_OVERFLOW_VISIBLE`); or **any ancestor**
has `LV_OBJ_FLAG_HIDDEN` or clips the area to nothing via its own bounds. All of these are silent —
same as "nobody ever called `lv_obj_invalidate()` for this object" from the hook's point of view.
`lv_disp_is_invalidation_enabled()` is separately toggled by `lv_disp_enable_invalidation()`
(`lv_disp.c:425-449`), used internally e.g. during screen-load transitions — a false negative here
would look identical to the other two.
**Why it matters**: this is directly the "object created/updated but no redraw is triggered"
symptom in `00-CONTEXT.md` — if the object is HIDDEN, off-screen, or an ancestor clips it, the
`[REDRAW]` log will show **nothing** for that object even though the setter that should have
triggered a repaint ran correctly. This looks identical, from the log alone, to "the setter never
called `lv_obj_invalidate()`".
**Fix**: when a `[REDRAW]` silence is observed for an object expected to repaint, immediately
cross-check with `lv_obj_is_visible(obj)` (public API, `lib/lvgl/src/core/lv_obj_pos.c:920-936`,
wraps the same `lv_obj_area_is_visible` check against the object's own bounds) and the tree-walker
dump (Finding 5.2) for HIDDEN flags up the parent chain, rather than assuming the setter is at
fault.
**Source**: `lib/lvgl/src/core/lv_obj_pos.c:840-936` (read directly, full function traced).

**4.3 — Three `_lv_inv_area()` call sites bypass the hook entirely; and the `LV_INV_BUF_SIZE`
overflow behaviour is a silent escalation to full-screen refresh.**

**Claim**: `_lv_inv_area()` (`lib/lvgl/src/core/lv_refr.c:206`) is called from four places:
`lv_obj_pos.c:853` (through the hook, covered above) and three direct calls in
`lib/lvgl/src/core/lv_disp.c` — `lv_disp_set_bg_color()` (line 169),
`lv_disp_set_bg_image()` (line 190), `lv_disp_set_bg_opa()` (line 210) — none of which pass through
`lv_obj_invalidate_hook`. This firmware does not appear to call any of these three APIs today
(they set the _display's_ background, not a widget's), so the gap is currently theoretical, but an
agent adding background-color/-image code would get an invisible-to-`[REDRAW]` invalidation.
Separately, `_lv_inv_area()` itself (`lv_refr.c:206-252`) implements the overflow behaviour asked
about: `LV_INV_BUF_SIZE` is `32` (`lib/lvgl/src/hal/lv_hal_disp.h:32`, unchanged in this repo's
`lv_conf.h`); each new non-overlapping invalidated area is appended to `disp->inv_areas[]`
(`inv_p` incremented) up to index 31; on the 33rd distinct area in one refresh cycle, the code does
**not** grow or wrap the array — it resets `disp->inv_p = 0` and stores the **full screen area**
instead (`lv_refr.c:243-247`: `else { disp->inv_p = 0; lv_area_copy(&disp->inv_areas[disp->inv_p],
&scr_area); } disp->inv_p++;`). This is a silent full-screen-refresh escalation with no log,
warning, or return value indicating it happened — indistinguishable from a legitimate single
full-screen invalidation unless you were watching `inv_p` across the whole cycle.
**Why it matters**: with `disp_drv.full_refresh = 0` (the branch under trial), a UI update that
touches many small, non-overlapping regions in one refresh window (e.g. many message bubbles
updating at once) can silently degrade to a full-screen repaint — exactly the kind of "full-screen
repaints are slow" symptom in `00-CONTEXT.md`, but with a root cause invisible to `[REFR]`/`[FRAME]`
alone (both would just show "1 area, full-screen px count", identical to an intentional full
refresh).
**Fix — detecting the overflow without patching `_lv_inv_area()`** (it has no hook point): compare,
inside `render_start_cb` (already wired, fires **before** `inv_p` is reset by the refresh, per
Finding 2.2's ordering note), whether `disp->inv_p == 1` **and** the sole area equals
`{0,0,hor_res-1,ver_res-1}` **and** the `[REDRAW]` log in the same window recorded more than one
distinct small area beforehand:

```c
extern "C" void tdeck_dbg_render_start_cb(lv_disp_drv_t * disp_drv)
{
    (void)disp_drv;
    lv_disp_t * disp = lv_disp_get_default();
    if(disp != NULL && disp->inv_p == 1) {
        lv_area_t * a = &disp->inv_areas[0];
        bool is_full = (a->x1 == 0 && a->y1 == 0 &&
                        a->x2 == lv_disp_get_hor_res(disp) - 1 &&
                        a->y2 == lv_disp_get_ver_res(disp) - 1);
        if(is_full && s_pending_small_area_count > 1) {
            Serial.printf("[INVOVERFLOW];ms;%lu;areas_before;%u\n",
                          (unsigned long)millis(), s_pending_small_area_count);
        }
    }
    s_pending_small_area_count = 0;   /* reset for the next window; increment this counter
                                          in lv_obj_invalidate_hook when area is not full-screen */
    if(!s_redrawlog_on) return;
    uint32_t n = (disp != NULL) ? disp->inv_p : 0;
    Serial.printf("[REFRSTART];ms;%lu;areas;%lu\n", (unsigned long)millis(), (unsigned long)n);
}
```

This is a heuristic (it cannot distinguish "genuinely one big invalidation" from "32-area
overflow that collapsed to one"), but a real minimal patch is one line if certainty is required:
add `disp->inv_overflowed = 1;` (a new bit needing an `lv_hal_disp.h` struct field, or a static
counter keyed by `disp` if the struct must not change) at `lv_refr.c:244`, and read/clear it from
`render_start_cb`. Given the "no LVGL edits beyond what's already there" preference implied by
Finding 4.1, prefer the heuristic; only patch `lv_refr.c` if the heuristic proves unreliable in
practice.
**Source**: `lib/lvgl/src/core/lv_refr.c:206-252` (full function read and traced),
`lib/lvgl/src/hal/lv_hal_disp.h:31-32,186-188`, `lib/lvgl/src/core/lv_disp.c:150-211` (all read
directly).

### 5. Object-tree and event observability

**5.1 — `lv_obj_class_t` has no name field in 8.3; class identification is by pointer comparison,
exactly as the existing `classify_obj()` does.**

**Claim**: `lib/lvgl/src/core/lv_obj_class.h:49-63` — the struct has `base_class`,
`constructor_cb`, `destructor_cb`, `event_cb`, `width_def`, `height_def`, `editable`, `group_def`,
`instance_size`; no string/name field. Every built-in widget exposes its class as an
`extern const lv_obj_class_t lv_xxx_class;` symbol (e.g. `lv_label_class`, `lv_btn_class`); the
only correct way to name a class at runtime is `lv_obj_get_class(obj) == &lv_xxx_class`, which is
exactly what `tdeck_debug.cpp:53-71`'s `classify_obj()` already does for the 14 widget types this
firmware uses.
**LVGL 9 difference**: v9 added a `.name` field to the class struct for exactly this purpose —
code or examples that do `lv_obj_get_class(obj)->name` will not compile against this v8.3 tree.
**Fix**: when a new widget type is introduced, add one more `if(cls == &lv_newwidget_class) return
"newwidget";` line to `classify_obj()` — do not look for a name field.
**Source**: `lib/lvgl/src/core/lv_obj_class.h:44-63` (read directly);
[LVGL 9.3 lv_obj_class API docs](https://lvgl.io/docs/open/9.3/API/misc/lv_log) region /
general v9 widget-class docs confirm the name-field addition is v9-only (cross-referenced against
the vendored v8.3 header, which has no such field).

**5.2 — Full object-tree dump: coordinates, flags, class, parent.**

Extends the existing `count_objs_recursive()` (`tdeck_debug.cpp:81-90`, which only counts) into a
full recursive dump, reusing `classify_obj()` and `known_name()` already in the same file:

```c
static void dump_obj_tree(const lv_obj_t * obj, int depth)
{
    if(obj == NULL) return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);   /* lv_obj_pos.h:210 */

    const char * cls = classify_obj(obj);
    const char * name = known_name(obj);
    const lv_obj_t * parent = lv_obj_get_parent(obj);   /* lv_obj_tree.h:125 */

    Serial.printf("[OBJTREE];depth;%d;obj;0x%08lx;cls;%s;parent;0x%08lx;"
                  "x1;%d;y1;%d;x2;%d;y2;%d;"
                  "hidden;%d;clickable;%d;scrollable;%d;visible;%d",
                  depth, (unsigned long)(uintptr_t)obj, cls,
                  (unsigned long)(uintptr_t)parent,
                  (int)coords.x1, (int)coords.y1, (int)coords.x2, (int)coords.y2,
                  lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
                  lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) ? 1 : 0,
                  lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) ? 1 : 0,
                  lv_obj_is_visible(obj) ? 1 : 0);   /* see Finding 4.2 */
    if(name != NULL) Serial.printf(";name;%s", name);
    Serial.print("\n");

    uint32_t child_cnt = lv_obj_get_child_cnt(obj);      /* lv_obj_tree.h:145 */
    for(uint32_t i = 0; i < child_cnt; i++) {
        dump_obj_tree(lv_obj_get_child(obj, i), depth + 1);   /* lv_obj_tree.h:138 */
    }
}
/* call as dump_obj_tree(lv_scr_act(), 0); wire to a new --objtree command */
```

Recursion depth is bounded by this firmware's UI depth (a handful of levels — tabview → tab →
container → widget); no explicit depth cap is needed for this UI, but add one (e.g. bail past
depth 16) before shipping this as a generic tool, since a future runaway parent/child cycle would
otherwise stack-overflow the calling task.
**Source**: `lib/lvgl/src/core/lv_obj_tree.h:125,138,145`, `lib/lvgl/src/core/lv_obj_pos.h:210`,
`lib/lvgl/src/core/lv_obj.h:90-94` (flag bit values), all read directly; reuses
`src/t-deck/tdeck_debug.cpp:53-79`.

**5.3 — A global event tap requires a one-line patch to `lv_event.c`, not
`lv_obj_add_event_cb(NULL, ...)`.**

**Claim (correcting the approach suggested in `00-CONTEXT.md`)**: `lv_obj_add_event_cb()`
(`lib/lvgl/src/core/lv_event.c:162-176`) takes `lv_obj_t * obj` and immediately calls
`lv_obj_allocate_spec_attr(obj)`, which dereferences `obj->spec_attr` — passing `NULL` is a
guaranteed NULL-pointer crash, not a documented "global handler" feature. The only NULL-guard on
this path, `LV_ASSERT_OBJ` (`lib/lvgl/src/core/lv_obj.h:389-397`), is gated by
`LV_USE_ASSERT_OBJ`, which is `0` in this repo's `lv_conf.h` (line 271) — so the call doesn't even
get a controlled `while(1)` halt, it segfaults directly. **There is no `lv_obj_add_event_cb(NULL,
...)` global-handler feature in LVGL 8.3.**
**Fix**: every event, on every object, already funnels through one function —
`event_send_core()` in `lib/lvgl/src/core/lv_event.c:424-434` — which is exactly where
`LV_LOG_TRACE_EVENT`'s `EVENT_TRACE` already sits (line 428, no code needed if `LV_USE_LOG` and
that switch are on, see Finding 1.3 for its numeric-only, high-noise limitation). For a
selectively-loggable, named, rate-capped version matching the rest of this firmware's
instrumentation, add the same weak-hook pattern used for invalidation (Finding 4.1) at the same
choke point:

```c
/* lib/lvgl/src/core/lv_event.c, mirroring the lv_obj_invalidate_hook pattern already in
 * lv_obj_pos.c -- add near the top of event_send_core(), after EVENT_TRACE(...): */
void __attribute__((weak)) lv_event_send_hook(lv_event_t * e) { (void)e; }
...
static lv_res_t event_send_core(lv_event_t * e)
{
    EVENT_TRACE(...);
    lv_event_send_hook(e);   /* MeshCom: global event trace hook */
    lv_indev_t * indev_act = lv_indev_get_act();
    ...
```

Then in `tdeck_debug.cpp`, override the weak symbol and translate `e->code` to a name via a small
switch/table over the `LV_EVENT_*` enum (`lib/lvgl/src/core/lv_event.h:33-90`) rather than logging
the bare integer — this is the missing piece `LV_LOG_TRACE_EVENT` doesn't give you. Rate-cap it the
same way `[REDRAW]` already is (`REDRAW_RATE_CAP`), since draw-phase events alone
(`LV_EVENT_DRAW_MAIN`/`DRAW_PART_BEGIN`/`DRAW_PART_END`) fire per-widget-per-frame.
**Symptom if this patch is skipped and `LV_USE_LOG`+`LV_LOG_TRACE_EVENT` is used instead**: you get
numeric codes only (`"Sending event %d to %p"`), no target-object class/name, and it is coupled to
turning on the whole `LV_USE_LOG` machinery (1.3's cost) rather than being independently
switchable.
**Source**: `lib/lvgl/src/core/lv_event.c:162-176,424-434` (read directly),
`lib/lvgl/src/core/lv_obj.h:389-397` and `src/t-deck/lv_conf.h:271` (`LV_USE_ASSERT_OBJ 0`, read
directly), `lib/lvgl/src/core/lv_event.h:32-90` (full `LV_EVENT_*` enum, read directly).

**5.4 — Counting objects to catch leaks: already partially covered; the generic form is one call.**

**Claim**: `count_objs_recursive()` (`tdeck_debug.cpp:81-90`) already gives a total live-object
count from `lv_scr_act()` down, reported in `[UISTAT]`. `instrument_report_gui()`
(`instrument.cpp`) gives a domain-specific count (message bubbles, persisted-model count) for one
specific hypothesis. Neither currently breaks the total down **by class**, which is the generic
leak-attribution tool (e.g. "labels are growing without bound" vs "buttons are").
**Fix**: extend `dump_obj_tree` (5.2) with an aggregation pass, or add a lighter sibling that skips
the per-object print and only tallies:

```c
static void count_by_class(const lv_obj_t * obj, uint32_t * counts /* one slot per known class */)
{
    counts[class_index(classify_obj(obj))]++;   /* map the classify_obj() string to a small enum */
    uint32_t n = lv_obj_get_child_cnt(obj);
    for(uint32_t i = 0; i < n; i++) count_by_class(lv_obj_get_child(obj, i), counts);
}
/* call periodically (e.g. from --uistat or a new --objcount) and diff between calls to spot a
 * monotonically-growing class */
```

**Source**: `src/t-deck/tdeck_debug.cpp:81-90,214-240`, `src/instrument.cpp:99-111` (both read
directly).

### 6. FreeRTOS/ESP32-side observability

**6.1 — `vTaskGetRunTimeStats()`/`uxTaskGetSystemState()` need a rebuilt Arduino-ESP32 2.x
framework; not available via `build_flags`.**

**Claim**: the precompiled FreeRTOS inside the Arduino-ESP32 2.x package (this repo:
`platform = espressif32 @ 6.6.0`, i.e. Arduino core 2.x on IDF 4.4.x per `00-CONTEXT.md`) ships
with `CONFIG_FREERTOS_USE_TRACE_FACILITY`/`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` off, and this
is baked into the precompiled `libfreertos.a` the PlatformIO package ships — a
`-DCONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=1` `build_flags` entry has no effect because the
functions that would use it are compiled out of the library already linked, not re-compiled from
source by a plain Arduino/PlatformIO build. Enabling it requires cloning
`espressif/esp32-arduino-lib-builder`, editing `configs/defconfig.esp32` to add
`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`, running `./build.sh -t esp32` (roughly an hour), and
replacing `~/.platformio/packages/framework-arduinoespressif32/tools/sdk` with the rebuilt output —
an out-of-repo, per-machine toolchain change, not something that ships in a PR.
**Symptom if assumed free**: a linker error (`undefined reference to vTaskGetRunTimeStats`) or,
worse, a successful link against a stub that returns nothing useful, depending on exactly which
symbols the precompiled lib exports.
**Fix**: do not add per-task CPU% instrumentation via `vTaskGetRunTimeStats` to this firmware
without first confirming (build attempt, not assumption) whether this specific toolchain snapshot
already has it enabled — it is plausible but unverified either way for this exact PlatformIO
package pin. If unavailable and per-task CPU% is genuinely needed, the cheaper substitute is
`INSTR_LOOPTICK`-style manual instrumentation (already exists, `src/instrument.cpp:44-58`) applied
to each suspect task's own loop, rather than a generic all-tasks view.
**Source**: [ESP32 forum — "Undefined Reference to uxTaskGetSystemState"](https://www.esp32.com/viewtopic.php?t=3674),
[Enabling runtime statistics in ESP32-arduino — Positroid blog](https://positroid.tech/en/post/platformio-esp32-stats)
(fetched directly: confirms build_flags alone is insufficient, full framework rebuild steps
listed), [ESP-IDF FreeRTOS docs](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-reference/system/freertos.html)
(confirms `configUSE_TRACE_FACILITY` is the underlying gate). **Marked UNVERIFIED**: whether this
repo's exact pinned package (`espressif32 @ 6.6.0`) happens to already have the flag on — not
tested in this audit, only the general Arduino-ESP32 2.x precompiled-library behavior is confirmed.

**6.2 — `uxTaskGetStackHighWaterMark()`: no special sdkconfig needed, cheap, should be added.**

**Claim**: unlike the runtime-stats family, `uxTaskGetStackHighWaterMark(TaskHandle_t)` is a basic
FreeRTOS API present in the standard build (it walks the already-painted stack looking for the
high-water mark, no `configUSE_TRACE_FACILITY` dependency) — this is a different FreeRTOS feature
gate than 6.1's, and is commonly used unconditionally in ESP32 Arduino sketches.
**Fix**: call it periodically (e.g. from the existing `--instr`/`[INSTR-*]` reporting path) for
every task handle this firmware already holds (the audio playback task per `00-CONTEXT.md`'s
`play_function()` description, the main loop task via `xTaskGetCurrentTaskHandle()`), reporting
bytes remaining, not words — the return value is in `StackType_t` units (words on ESP32, 4 bytes
each) so multiply by `sizeof(StackType_t)` for a byte figure comparable to the stack size passed to
`xTaskCreate`.
**Symptom if not instrumented**: a stack-overflow-adjacent corruption (a real risk given the audio
task's blocking I2S teardown/reinstall under load, flagged in `00-CONTEXT.md`) manifests as a
random-looking crash with no prior warning, instead of a monotonically shrinking high-water-mark
trend visible for many runs beforehand.
**Source**: standard FreeRTOS API, cross-referenced against this repo's existing
`esp_task_wdt_add(NULL)` usage pattern (`src/esp32/esp32_main.cpp:1798`) which confirms task
handles are already available in this codebase for the main task. UNVERIFIED against this specific
toolchain pin (not build-tested in this audit), but this API has no known Arduino-ESP32
precompiled-library gating, unlike 6.1's family.

**6.3 — Task watchdog (`esp_task_wdt_*`) is already used in this repo; extend it to turn a UI
stall into a decoded backtrace instead of a silent reset.**

**Claim**: `esp_task_wdt_add(NULL)` and `esp_task_wdt_reset()` are already called from
`src/esp32/esp32_main.cpp` (lines 1798, 1840) and `src/udp_functions.cpp` (multiple sites around
blocking WiFi calls) — the pattern of registering a task and resetting it around known-blocking
operations already exists in this codebase. On Arduino-ESP32 2.x, `esp_task_wdt_init(timeout_s,
panic_mode)` is the init call (the `esp_task_wdt_reconfigure()` struct-based API is a 3.x-only
change, not relevant to this repo's 2.x core); with `panic_mode = true`, an unfed registered task
triggers the ESP-IDF panic handler, which prints a backtrace and, with PlatformIO's
`monitor_filters = esp32_exception_decoder` (already set for this env,
`platformio.ini:381,420`), gets auto-symbolized **if** `pio device monitor` is attached at the
moment of the panic.
**Why this matters for the audio-blocks-UI symptom**: `00-CONTEXT.md` describes a blocking audio
path (`while (audio.isRunning()) { ...; audio.loop(); }`, no yield) that can starve the LVGL/main
loop task for the duration of playback. If the LVGL task is registered with the TWDT and that
starvation exceeds the watchdog timeout, today it most likely resets silently (the default
non-panic-mode behavior on many Arduino-ESP32 setups just reboots) — the exact "reboot, not proof"
failure mode this whole track exists to eliminate.
**Fix**: register the task that calls `lv_task_handler()` with the TWDT
(`esp_task_wdt_add(NULL)` from that task's own context, already the established pattern in this
repo) with `panic_mode = true` set at `esp_task_wdt_init()` time, and make sure that call happens
**before** any of the known-blocking paths (`addMessage()`'s `while(...) { lv_task_handler(); delay(5); }`
loop already in `00-CONTEXT.md`, and the blocking `audio.isRunning()` loop) — a WDT reset with
`panic_mode=false` on an already-registered-but-not-yet-panicking task gives nothing; the point is
to convert "stall → silent reboot" into "stall → backtrace printed → (optionally) reboot."
**Symptom if only `esp_task_wdt_reset()` calls are added without `panic_mode=true`**: the watchdog
still resets the device on a genuine stall, but no backtrace is produced — no improvement in
observability, only in "the device eventually comes back."
**Source**: `src/esp32/esp32_main.cpp:1798,1840`, `src/udp_functions.cpp:612-702` (all read
directly, confirms the existing usage pattern in this exact codebase);
[ESP-IDF Watchdogs docs](https://docs.espressif.com/projects/esp-idf/en/v5.1.3/esp32/api-reference/system/wdts.html)
(TWDT panic-and-backtrace behavior, `esp_task_wdt_init`/`esp_task_wdt_add` API surface — the 2.x
signature, not the 3.x `esp_task_wdt_reconfigure()` struct API, cross-checked against this repo
being on Arduino core 2.x per `00-CONTEXT.md`).

**6.4 — `esp_timer_get_time()`, `heap_caps_check_integrity`, `CONFIG_HEAP_POISONING`.**

**Claim**: `esp_timer_get_time()` (microsecond, monotonic, wraps at ~2^63) is a standard ESP-IDF
API available unconditionally — already effectively duplicated by this repo's own `micros()`-based
`INSTR_T0`/`instrument_note_flush` (`micros()` also wraps correctly per the comment at
`instrument.cpp:52`, "wraps correctly on uint32", though at 32-bit width it wraps roughly every 71
minutes vs. `esp_timer_get_time()`'s 64-bit range) — no need to add a second microsecond clock,
reuse `micros()`/the existing `INSTR_T0` macro for consistency with what's already instrumented.
`heap_caps_check_integrity(caps, print_errors)` walks heap structures looking for corruption;
"Basic mode" (its default, no poisoning) can still detect structural corruption (bad block-size
headers, broken free lists) but **cannot** detect a buffer overwrite that stays within a block's
allocated size plus alignment padding — that specifically needs `CONFIG_HEAP_POISONING_LIGHT` (or
`_COMPREHENSIVE`), which adds 9-12 bytes of canary per allocation (head canary `0xABBA1234`, tail
`0xBAAD5678`) and, like 6.1's runtime-stats flag, is an `sdkconfig` option baked into the
precompiled framework — same rebuild cost as Finding 6.1, not a `build_flags` toggle, and Arduino-
ESP32's default is poisoning **disabled**.
**Fix**: use `heap_caps_check_integrity_all(true)` (prints on failure) as a periodic sanity check
regardless of poisoning — it is free and already links; do not attempt to enable
`CONFIG_HEAP_POISONING_*` in this repo without accepting the same lib-rebuild cost as 6.1, and
budget it only if a specific heap-corruption hypothesis (not just "let's have better heap
debugging") justifies an hour-long one-time toolchain rebuild.
**Source**: [ESP-IDF Heap Memory Debugging docs, v5.0](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/heap_debug.html)
(canary values, poisoning levels, `heap_caps_check_integrity` semantics — fetched via search,
cross-checked against the general precompiled-library-gating pattern already confirmed for 6.1);
`src/instrument.cpp:44-58` (existing `micros()`-based timing, read directly).

**6.5 — `esp_log_level_set()` vs `CORE_DEBUG_LEVEL=1` (this env's actual build flag).**

**Claim**: `platformio.ini`'s `t_deck_plus` env sets `-DCORE_DEBUG_LEVEL=1`
(`variants/t_deck_plus/platformio.ini:32`) — this controls Arduino-ESP32's own `log_e/log_w/log_i/
log_d/log_v` macro family (levels: 0=none, 1=error only, up to 5=verbose) at **compile time**; it
does not affect ESP-IDF's separate `ESP_LOGx` macros or the runtime `esp_log_level_set()` call,
which govern IDF-component logging (WiFi stack, BLE stack, etc.) independently. Because
`CORE_DEBUG_LEVEL=1`, only `log_e()` (error) calls anywhere in this codebase or its libraries
currently produce output; any `log_w`/`log_i`/`log_d` call an agent adds for debugging is silently
compiled to nothing until `CORE_DEBUG_LEVEL` is raised for a debug build.
**Fix**: for firmware-level debug prints, prefer this repo's own `printfdeb`/`Serial.printf`
conventions (already CSV/human-toggle-aware, see the repo inventory above) over `log_d`/`log_v` —
they are unconditional at runtime and match the rest of the instrumentation; reserve
`CORE_DEBUG_LEVEL` bumps for diagnosing library-internal (TFT_eSPI, WiFi, BLE stack) behavior where
`log_x` calls already exist in vendored code and can't be swapped for `printfdeb`.
**Source**: `variants/t_deck_plus/platformio.ini:32` (read directly);
[Core Debug Level in ESP32 — iotespresso.com](https://iotespresso.com/core-debug-level-in-esp32/)
and the arduino-esp32 GitHub discussion on `esp_log_level_set` vs `CORE_DEBUG_LEVEL` decoupling
(cross-checked, consistent with the general "Arduino macros compiled at `CORE_DEBUG_LEVEL`,
separate from IDF's runtime-settable `esp_log_level_set`" behavior — **UNVERIFIED** for the exact
patch level of this pinned `espressif32 @ 6.6.0` package; treat as the documented general behavior
of the arduino-esp32 2.x line, not confirmed against this repo's exact commit of that package).

### 7. Runtime-switchable trace design

**7.1 — The pieces already exist; the gap is a unified bitmask instead of N independent booleans.**

**Claim**: today there is exactly one runtime trace toggle relevant to LVGL
(`s_redrawlog_on`/`tdeck_dbg_redrawlog(bool)`, gating `[REDRAW]`/`[REFR]`/`[REFRSTART]` together as
one unit) plus a separate CSV/human toggle at the `printfdeb()` layer (`bDEBUGCSV`, orthogonal —
affects formatting, not what gets logged). There is no way to enable, say, INDEV tracing without
also getting REFR tracing, and no way to independently rate-limit different trace domains (the
existing `REDRAW_RATE_CAP = 200/s` in `tdeck_debug.cpp` applies to the one flag that exists).
**Fix**: a `uint16_t` bitmask, one bit per domain, replacing the single boolean, checked at each
call site exactly the way `s_redrawlog_on` already is:

```c
/* tdeck_debug.h */
#define TDECK_TRACE_REFR   (1u << 0)   /* [REDRAW]/[REFR]/[REFRSTART] -- supersedes s_redrawlog_on */
#define TDECK_TRACE_FLUSH  (1u << 1)   /* [FLUSH] in disp_flush(), already gated by
                                           tdeck_dbg_redrawlog_enabled() today -- fold in */
#define TDECK_TRACE_INDEV  (1u << 2)   /* new: per Finding 1.3's INDEV switch, or a custom
                                           per-read-cycle log if LV_USE_LOG stays off */
#define TDECK_TRACE_EVENT  (1u << 3)   /* new: Finding 5.3's global event hook */
#define TDECK_TRACE_AUDIO  (1u << 4)   /* new: esp32_audio.cpp playback state transitions */
#define TDECK_TRACE_HEAP   (1u << 5)   /* [INSTR-HEAP]-equivalent per-frame, not just on demand */

void tdeck_dbg_trace_set(uint16_t mask);   /* replaces tdeck_dbg_redrawlog(bool) */
uint16_t tdeck_dbg_trace_get(void);
```

Wire it to a new `--trace <hex-mask>` (or `--trace refr,flush,event` name-list form, parsed the
same way the existing `--tab <n>`/`commandCheck` pattern in `command_functions.cpp` already parses
arguments) alongside the existing `--redrawlog on/off` (keep the old command as `--trace 0x03` /
`--trace 0` sugar for backward compatibility with `docs/tdeck-findings-20260828.md`'s recorded
runs, so old harness scripts keep working).
**Source**: `src/t-deck/tdeck_debug.cpp:35,123-131,192-212`, `src/command_functions.cpp:4513-4577`
(existing command-parsing pattern, read directly).

**7.2 — Rate limiting exists for one domain; generalize it, and state the observer-effect
principle explicitly.**

**Claim**: `REDRAW_RATE_CAP = 200` lines/s with a per-second `dropped` counter already exists
(`tdeck_debug.cpp:42-45,158-171`) for the `[REDRAW]` domain specifically. Any new trace domain
added under 7.1's bitmask needs the same treatment, because — critically — **the act of emitting a
trace line changes the timing being measured**: `Serial.printf`/`printfdeb` block on this platform
once the USB-CDC TX ring buffer (256 B default under Arduino-ESP32's `HWCDC`) fills and the host
hasn't drained it yet (confirmed: the ESP32 branch of `printfdeb_functions.cpp` has **no**
drop-instead-of-block path, unlike the nRF52 branch's `cdcReady()` 20 ms-then-drop guard — see the
repo inventory above), and formatting itself (the `_lv_log_add` 512-byte stack buffer, or any
`snprintf` in `tdeck_debug.cpp`'s own hooks) costs CPU time inside the very call path being
measured (e.g. inside `lv_obj_invalidate_hook`, which runs on every invalidation, on the same task
that also runs `lv_task_handler()`). A trace domain with no rate cap, under enough load, can turn
"measuring the redraw rate" into "the redraw rate is now bottlenecked by the serial port."
**Fix**: give every domain in 7.1 its own rate cap and dropped-counter, generalizing
`tdeck_debug.cpp`'s existing per-second window pattern (lines 41-45, 158-171) into a small
reusable helper keyed by domain bit, and always print a domain's `dropped` count once per window
even when suppressing lines — so a rate-limited trace still proves _how much_ was suppressed,
rather than looking identical to "nothing happened."
**Source**: `src/t-deck/tdeck_debug.cpp:41-45,158-171` (read directly);
`src/printfdeb_functions.cpp:36-69` (ESP32 vs nRF52 blocking-vs-dropping behavior, read directly).

**7.3 — Byte-budget arithmetic: at what rate does logging distort the measurement?**

**115200 baud (classic UART, 8N1, 10 bits/byte incl. start+stop)**: this env's `platformio.ini`
sets `monitor_speed = 115200` (`platformio.ini:167,381-382,420-421`), but the T-Deck Plus env also
sets `-DARDUINO_USB_CDC_ON_BOOT=1` (`variants/t_deck_plus/platformio.ini:33`) — the physical
transport is the ESP32-S3's **native USB-CDC**, not a UART, so the configured "115200" is
vestigial/cosmetic for this board (kept for `pio device monitor`'s benefit, largely ignored by the
native-USB peripheral). The arithmetic below is included because (a) it's what the task asks for,
and (b) it is the right model for any board on this codebase that _does_ use a real UART (e.g. if
a future variant lacks native USB):

- Effective throughput: 115200 / 10 = **11,520 bytes/s**.
- A `[REDRAW]` line as currently formatted (`tdeck_debug.cpp:184-189`) is roughly 140-190 bytes
  with an 8-frame backtrace (8 × 11 bytes of `0xXXXXXXXX,` plus ~90 bytes of fixed fields/labels).
  At the measured baseline invalidation rate of 32/s (`docs/tdeck-findings-20260828.md` §2), that's
  32 × ~165 B ≈ **5,280 B/s**, already 46% of the UART's total capacity consumed by one trace
  domain alone, before the rest of the firmware's own serial traffic (net console mirroring, other
  debug output) is added. At the existing 200 lines/s cap, that domain alone would need
  200 × 165 ≈ **33,000 B/s** — nearly **3x** the UART's capacity — meaning the rate cap, as
  currently set, would _itself_ saturate a real 115200-baud UART and turn `Serial.printf` into a
  blocking bottleneck on the calling task (LVGL/main loop) rather than a passive observer.
- Rule of thumb: keep `(lines/s) × (bytes/line) < ~0.3 × baud/10` (30% headroom) on a real UART to
  avoid the trace itself becoming the dominant consumer of loop time; for this repo's 200/s cap and
  ~165 B lines, that means real-UART boards should cut the cap to roughly **20 lines/s**, not 200.

**USB-CDC (this board's actual transport)**: no fixed "baud" ceiling — throughput is bounded by (a)
the 256-byte default TX ring buffer inside Arduino-ESP32's `HWCDC` driver (blocks the writer once
full, per the USB CDC docs found for this board — see Source), (b) USB Full-Speed bulk-transfer
bandwidth (12 Mbit/s link-layer ceiling, realistically several hundred KB/s to low-MB/s of
sustained application throughput once framing/host overhead is accounted for), and (c) whether a
host reader is attached and draining at all — with nothing attached, the same 256-byte buffer fills
and every subsequent `Serial.printf` call blocks indefinitely (this is the documented
"opening/closing the port resets the T-Deck Plus" hazard from `00-CONTEXT.md`/repo memory
compounding with "no host attached blocks the writer" — a trace domain left on with no harness
listening does not silently accumulate a backlog, it **stalls the emitting task**). At 200
`[REDRAW]` lines/s × ~165 B ≈ 33,000 B/s, USB-CDC's link-layer capacity is not the bottleneck (33
KB/s is a small fraction of even a conservative several-hundred-KB/s estimate); the 256-byte ring
buffer and host-side read-loop latency are the practical limits instead — a slow-reading Python
harness (e.g. one doing per-line regex work synchronously in its read loop) can still make the
firmware block, independent of raw USB bandwidth.
**Rule of thumb for USB-CDC**: the meaningful budget is not bytes/s but **buffer-fills/refresh-
cycle** — if a single LVGL refresh cycle (as short as a few ms in partial-refresh mode, per
`docs/tdeck-findings-20260828.md` §3's 7.7 ms mean) can emit more than ~256 bytes of trace output
(roughly 1.5 `[REDRAW]` lines), that cycle risks filling the ring buffer and blocking mid-cycle;
keep per-refresh-cycle trace output well under that, which the domain-level rate caps in 7.1/7.2
should target directly (cap in **lines per refresh cycle**, not just lines per second, for domains
tied to the refresh timer specifically).
**Source**: `platformio.ini:167,381-382,420-421`, `variants/t_deck_plus/platformio.ini:33` (both
read directly); [Arduino-ESP32 USB CDC docs](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_cdc.html)
(default TX buffer size, `setTxBufferSize`); ESP32 forum thread on USB-CDC large-transfer data loss
(cross-check for the "blocks/loses data under load" behavior, not authoritative but consistent
with the buffer-size finding); `docs/tdeck-handover.md` §1.2 and repo memory
(`tdeck-plus-bench-pitfalls.md`) for the port-open-resets-device hazard.

### 8. Assertions and crash forensics

**8.1 — Exact current assert configuration and its cost.**

| Macro                         | This repo's setting | What it catches                                                                                                                                                                                                                                                                                                                                                        | Cost                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| ----------------------------- | ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `LV_USE_ASSERT_NULL`          | `1`                 | Any LVGL API called with a `NULL` required pointer (`LV_ASSERT_NULL(p)`, expands to a `p != NULL` check + `LV_ASSERT_HANDLER`)                                                                                                                                                                                                                                         | One pointer compare per guarded call — negligible, "very fast" per LVGL's own comment; leave on.                                                                                                                                                                                                                                                                                                                                                              |
| `LV_USE_ASSERT_MALLOC`        | `1`                 | Every `lv_mem_alloc()`-family call whose underlying `ps_malloc`/`malloc` returned `NULL` (`lv_mem.c` callers wrap results in `LV_ASSERT_MALLOC`)                                                                                                                                                                                                                       | Negligible — leave on; this is the only thing standing between a PSRAM exhaustion and an unexplained NULL-deref crash three calls later.                                                                                                                                                                                                                                                                                                                      |
| `LV_USE_ASSERT_STYLE`         | `0`                 | A style object used before `lv_style_init()` or corrupted (`style->sentinel != LV_STYLE_SENTINEL_VALUE`, `lv_style.h:580-584`)                                                                                                                                                                                                                                         | Cheap (one more field compare) if turned on — this repo has it off for no evident reason; turning it on for a debug build is low-risk.                                                                                                                                                                                                                                                                                                                        |
| `LV_USE_ASSERT_MEM_INTEGRITY` | `0`                 | Full `lv_mem_test()` walk of LVGL's internal heap structures — but **only under `LV_MEM_CUSTOM == 0`** (`lv_assert.h:69-72` calls `lv_mem_test()`, itself a no-op-adjacent function under custom malloc, mirroring Finding 3.2's `lv_mem_monitor()` gating). With this repo's `LV_MEM_CUSTOM 1`, turning this on buys **nothing** — same trap as `LV_USE_MEM_MONITOR`. | N/A here — does not apply to this build's memory configuration; use `heap_caps_check_integrity_all()` (Finding 6.4) instead.                                                                                                                                                                                                                                                                                                                                  |
| `LV_USE_ASSERT_OBJ`           | `0`                 | `lv_obj_has_class(obj, expected_class)` and `lv_obj_is_valid(obj)` (use-after-free/wrong-type) on every object-touching API — this is the guard that, if it _were_ on, would have turned Finding 5.3's `lv_obj_add_event_cb(NULL, ...)` crash into a controlled halt instead of a raw NULL-deref                                                                       | "Slow" per LVGL's own comment — it walks the class hierarchy and a live-object registry on every call; expect a real frame-time cost if enabled, proportional to how many `LV_ASSERT_OBJ`-guarded calls happen per frame (most `lv_obj_*` setters). Worth turning on **temporarily** while chasing a specific use-after-free/wrong-type hypothesis, not as a standing debug-build default given the redraw-performance work already in flight on this branch. |

`LV_ASSERT_HANDLER` is `while(1);` (`lv_conf.h:275`) — on assert, the calling task spins forever.
This is not silent forever, though: if that task is registered with the TWDT (Finding 6.3) and
`panic_mode=true`, the watchdog will fire on the spin and produce a backtrace pointing at the
`while(1)` — assign the LVGL/main task to the TWDT specifically so an LVGL assert becomes a decoded
backtrace instead of an indefinite hang.
**Fix**: for a debug build investigating a specific memory-safety hypothesis, turn on
`LV_USE_ASSERT_STYLE` (cheap, currently off for no evident reason) and, narrowly and temporarily,
`LV_USE_ASSERT_OBJ`; leave `LV_USE_ASSERT_MEM_INTEGRITY` off permanently (it is inert under this
repo's memory config) and use `heap_caps_check_integrity_all()` instead.
**Source**: `src/t-deck/lv_conf.h:266-275` (read directly), `lib/lvgl/src/misc/lv_assert.h` (full
file read), `lib/lvgl/src/misc/lv_style.h:575-588`, `lib/lvgl/src/core/lv_obj.h:389-397` (all read
directly).

**8.2 — Decoding an ESP32-S3 backtrace for this exact PlatformIO build.**

**Claim**: the toolchain's `addr2line` for this target is installed at
`~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line` (confirmed
present in this environment), and the build's ELF is at
`.pio/build/t_deck_plus/firmware.elf` (per this repo's env name, `[env:t_deck_plus]`).
**Fix — exact command**:

```
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
  -pfiaC -e .pio/build/t_deck_plus/firmware.elf \
  0x420xxxxx 0x420yyyyy 0x420zzzzz
```

(`-p` pretty-print, `-f` function names, `-i` inline frames, `-a` show the address before each
result, `-C` demangle C++ names) — feed it the space-separated hex addresses from either a Guru
Meditation dump's `Backtrace:` line, or from this repo's own `[REDRAW]`/`bt;` field (already hex,
comma-separated — swap commas for spaces).
**Two paths depending on how the crash was captured**:

1. **Live via `pio device monitor`** with `monitor_filters = esp32_exception_decoder` (already set
   for this env, `platformio.ini:381,420`) — auto-decodes inline, **but** opening the monitor port
   reboots the T-Deck Plus (repo memory: `tdeck-plus-bench-pitfalls.md`), so this only works if the
   crash is expected to happen _after_ the monitor is already attached and the harness's own
   held-open serial session (per `docs/tdeck-handover.md` §1.2) is not otherwise in use.
2. **Offline from a captured log** (the harness's own recorded `.log` file, or a `[REDRAW];bt;...`
   line) — copy the hex addresses out, run the `addr2line` command above manually. This is the
   only option compatible with the harness's "hold one session open for the whole run" constraint,
   since it doesn't require a second serial attachment.
   **Source**: local filesystem check (`~/.platformio/packages/toolchain-xtensa-esp32s3/bin/`,
   confirmed present), `platformio.ini:381,420` (monitor_filters, read directly),
   `variants/t_deck_plus/platformio.ini:1` (env name), `docs/tdeck-handover.md` §1.2 and repo memory
   (port-open-resets-device hazard); [Espressif — Inspecting backtrace after ESP32 panic using
   xtensa toolchain (Medium)](https://stephencowchau.medium.com/inspebacktrace-stack-trace-after-esp32-using-espressif-xtensa-toolchain-7b0bf35905c1)
   and [platformio/platform-espressif32#1083](https://github.com/platformio/platform-espressif32/issues/1083)
   (addr2line flag usage, confirms `-pfiaC` convention).

### 9. Prioritised "instrument this first" list

Given the five symptoms in `00-CONTEXT.md` and the instrumentation already in place, if only three
new probes may be added:

1. **Extend the disp-refresh CSV line (Finding 2.2) to include `disp->inv_p` (via `render_start_cb`
   correlation) and heap.** Nearly free (one more `Serial.printf` call, already-computed values),
   and it is the single line that answers "is anything redrawing, how much, how slow, right now" —
   the foundation every other symptom investigation (laggy UI, unreliable redraws, slow full
   repaints) sits on. This is a small extension to code that already exists and is already wired.
2. **Turn the already-patched invalidate hook into a per-return-address histogram** (a small
   `std::map`-free fixed-size table keyed by `ret_addr`/backtrace-frame-1, counting calls per
   window, evicting the coldest entry on overflow) rather than only a rate-capped log stream. This
   directly answers "why did my object not repaint" (a hypothesis object's address never appears)
   and "what's spamming invalidations" (the existing baseline finding that one battery-header
   function caused 100% of idle invalidations, `docs/tdeck-findings-20260828.md` §2, would have
   been visible from a histogram in seconds instead of requiring a dedicated backtrace-decode
   session) — reuses 100% of the existing hook infrastructure (Finding 4.1), only the aggregation
   changes.
3. **Register the LVGL/main-loop task with the task watchdog in `panic_mode=true`** (Finding 6.3),
   using the exact `esp_task_wdt_add`/`esp_task_wdt_init` pattern already present elsewhere in this
   codebase. This is the one gap none of the existing instrumentation covers: the audio-blocks-the-
   UI symptom and any future stall currently produce either nothing (if under the default WDT
   timeout) or a silent reset (if over it) — no evidence either way. This turns "device rebooted,
   unclear why" into a decoded backtrace pointing at the exact blocking call, addressed with the
   already-confirmed `addr2line` command (Finding 8.2).

Deliberately **not** in the top three: `LV_USE_LOG` (Finding 1) — real value, but its output format
needs the harness-compatibility fix (1.2) and duplicates ground the existing `[REDRAW]`/`[REFR]`
system already covers for the REFR/EVENT domains; the object-tree walker (Finding 5.2) — valuable
for investigation but reactive (run on demand once a symptom is already suspected), not a standing
probe; the bitmask trace redesign (Finding 7.1) — a real quality-of-life improvement once more
domains exist, but premature with only two domains (REFR, FLUSH) currently wired.

## Rules to hand the coding agent

1. Before adding any new debug output, grep `tdeck_debug.cpp`, `instrument.cpp`, and
   `printfdeb_functions.cpp` for an existing mechanism — this firmware already has three
   overlapping-but-distinct instrumentation layers; a fourth ad hoc one is a regression, not a fix.
2. Do not add a second `lv_obj_invalidate_hook`-style patch to vendored LVGL. One already exists at
   `lib/lvgl/src/core/lv_obj_pos.c:838,852`; extend its override in `tdeck_debug.cpp` instead.
3. Never call `lv_obj_add_event_cb(NULL, ...)` — it crashes in this build (Finding 5.3). For a
   global event tap, patch `event_send_core()` in `lib/lvgl/src/core/lv_event.c` with a weak hook
   mirroring the existing invalidate-hook pattern.
4. Do not enable `LV_USE_MEM_MONITOR` or `LV_USE_ASSERT_MEM_INTEGRITY` expecting real data — both
   are inert no-ops under this repo's `LV_MEM_CUSTOM 1`. Use `heap_caps_get_free_size`/
   `heap_caps_get_largest_free_block`/`heap_caps_check_integrity_all` instead (already partly
   wired via `instrument_report_heap()`).
5. Do not use `LV_USE_PERF_MONITOR`'s FPS figure to evaluate partial-refresh performance — it only
   samples refresh cycles with `px_num > 5000` and silently reports the theoretical max FPS when no
   such cycle occurred in a 300 ms window (Finding 3.1). Use the `[REFR]`/`[FRAME]` line instead.
6. Any new trace line must declare, in a one-line comment at its call site, whether it follows the
   raw-`Serial.printf`-with-unconditional-`;` convention (`tdeck_debug.cpp` style) or the
   `printfdeb()`-with-`--debug csv/man`-toggled-`;` convention — do not mix silently.
7. Any new trace domain must be independently rate-limitable (extend the pattern in
   `tdeck_debug.cpp:41-45,158-171`) and must print its own suppressed-count once per window even
   when dropping lines — a silently-dropped trace is indistinguishable from "nothing happened."
8. Do not budget `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`, `CONFIG_HEAP_POISONING_*`, or any other
   `sdkconfig`-level option as a `build_flags` change — they require rebuilding the Arduino-ESP32
   framework out-of-repo (Findings 6.1, 6.4) and are not achievable inside a normal PR.
9. When registering a task with the task watchdog for crash-forensics purposes (Finding 6.3),
   always set `panic_mode=true` at `esp_task_wdt_init()` — a non-panicking WDT reset gives no
   backtrace and is observability-neutral, only availability-positive.
10. Decode any captured backtrace with
    `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e
.pio/build/t_deck_plus/firmware.elf <addrs>` — do not open a live `pio device monitor` session
    to catch a crash unless the harness's single held-open serial session (per
    `docs/tdeck-handover.md` §1.2) is not otherwise required, since opening a second port resets
    the device.

## Open questions / UNVERIFIED

- Whether this repo's exact pinned `platform = espressif32 @ 6.6.0` package happens to already
  ship `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`/`CONFIG_HEAP_POISONING_*` enabled by some
  non-default packaging choice — general Arduino-ESP32 2.x precompiled-library behavior (both off,
  rebuild required) is confirmed via search, but not build-tested against this exact package
  snapshot in this audit (no builds were run, per the audit-only constraint).
- Whether `esp_log_level_set()`/`CORE_DEBUG_LEVEL` decoupling (Finding 6.5) holds exactly as
  described for this repo's specific arduino-esp32 core version — confirmed as the documented
  general behavior of the 2.x line via search, not confirmed against this repo's exact commit.
- The exact byte size of a `[REDRAW]` line used in Finding 7.3's arithmetic (~140-190 B) is a
  computed estimate from the format string in `tdeck_debug.cpp:184-189`, not a measured average
  from a captured log — `docs/tdeck-findings-20260828.md`'s raw logs (`tools/bench/tdeck_run_*.log`)
  were not read in this audit (out of scope: reading existing measured data vs. reading source).
- Whether `uxTaskGetStackHighWaterMark()` and `esp_timer_get_time()` are in fact link-clean against
  this exact toolchain snapshot was not build-verified (audit-only constraint); both are asserted
  free of the `CONFIG_FREERTOS_USE_TRACE_FACILITY`-style gating based on general FreeRTOS/ESP-IDF
  API documentation, not tested here.
- Whether turning on `LV_USE_LOG` with only `LV_LOG_TRACE_DISP_REFR`/`EVENT` (and not the other six
  switches) measurably changes frame timing on real hardware was not tested — Finding 7's
  observer-effect arithmetic is derived from the serial-transport numbers, not a measured
  before/after on this specific combination.

## Sources

- `src/t-deck/lv_conf.h` — this repo's actual LVGL config; read in full for the logging/assert/
  monitor blocks (lines 233-297).
- `src/t-deck/tdeck_debug.cpp`, `tdeck_debug.h` — existing redraw/UI-state instrumentation, read in
  full.
- `src/instrument.h`, `src/instrument.cpp` — existing flush/loop/heap/GUI-count instrumentation,
  read in full.
- `src/printfdeb_functions.cpp`, `src/printfdeb_format.h` — existing CSV/human debug-print
  convention, read in full.
- `src/command_functions.cpp` — command-parsing wiring for all `--redrawlog`/`--uistat`/`--heap`/
  `--instr`/etc. commands (grepped and spot-read around lines 4480-4602, 808).
- `src/t-deck/tdeck_main.cpp` — `disp_drv` setup and `disp_flush()` (lines 370-477).
- `src/esp32/esp32_main.cpp`, `src/udp_functions.cpp` — existing `esp_task_wdt_*` usage pattern.
- `variants/t_deck_plus/platformio.ini`, `platformio.ini` — build flags (`CORE_DEBUG_LEVEL=1`,
  `ARDUINO_USB_CDC_ON_BOOT=1`), `monitor_speed`/`monitor_filters`.
- `docs/tdeck-findings-20260828.md`, `docs/tdeck-handover.md` — existing measured baselines and
  bench-harness gotchas, cited but not repeated wholesale.
- `lib/lvgl/src/core/lv_refr.c` — refresh timer, perf/mem monitor, `_lv_inv_area`, `call_flush_cb`;
  read in full for the relevant functions.
- `lib/lvgl/src/core/lv_obj_pos.c` — `lv_obj_invalidate_area`/`lv_obj_area_is_visible`/the existing
  weak-hook patch; read in full for the relevant functions.
- `lib/lvgl/src/core/lv_obj_class.c`, `lv_obj_class.h`, `lv_obj.c`, `lv_obj.h`, `lv_event.c`,
  `lv_event.h`, `lv_obj_tree.h`, `lv_obj_pos.h` — object model, event dispatch, tree-walk API;
  read directly for signatures and trace call sites.
- `lib/lvgl/src/misc/lv_log.c`, `lv_log.h`, `lv_mem.c`, `lv_timer.c`, `lv_anim.c`, `lv_assert.h`,
  `lv_style.h` — logging internals, trace macros, assert macros; read in full/directly.
- `lib/lvgl/src/hal/lv_hal_disp.h`, `lv_hal_indev.h` — `lv_disp_drv_t`/`lv_indev_drv_t` struct
  fields (`monitor_cb`, `render_start_cb`, `clean_dcache_cb`, `feedback_cb`), `LV_INV_BUF_SIZE`.
- `lib/lvgl/src/extra/layouts/flex/lv_flex.c`, `grid/lv_grid.c`, `lv_layouts.h` — layout trace call
  sites.
- `lib/lvgl/src/draw/nxp/pxp/lv_gpu_nxp_pxp.c`, `arm2d/lv_gpu_arm2d.c` — other `clean_dcache_cb`
  callers, confirming it is GPU/DMA-backend-specific and unused by this driver.
- [LVGL 9.3 Logging docs](https://docs.lvgl.io/9.3/details/debugging/log.html) — used only to
  confirm the v8→v9 `lv_log_register_print_cb` callback-signature change (added `level` param).
- [ESP-IDF FreeRTOS docs, v4.4.6](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-reference/system/freertos.html)
  — `configUSE_TRACE_FACILITY` gating for `vTaskGetRunTimeStats`/`uxTaskGetSystemState`.
- [Enabling runtime statistics in ESP32-arduino — Positroid blog](https://positroid.tech/en/post/platformio-esp32-stats)
  — fetched directly; confirms full-framework-rebuild requirement under PlatformIO/Arduino 2.x.
- [ESP-IDF Watchdogs docs, v5.1.3](https://docs.espressif.com/projects/esp-idf/en/v5.1.3/esp32/api-reference/system/wdts.html)
  — TWDT panic/backtrace behavior, `esp_task_wdt_init`/`_add` API surface (2.x signature).
- [ESP-IDF Heap Memory Allocation docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html)
  and [Heap Memory Debugging docs, v5.0](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/heap_debug.html)
  — `heap_caps_*` API surface, poisoning canary values and levels.
- [Core Debug Level in ESP32 — iotespresso.com](https://iotespresso.com/core-debug-level-in-esp32/)
  — `CORE_DEBUG_LEVEL` vs `esp_log_level_set` general behavior (arduino-esp32 2.x line).
- [Arduino-ESP32 USB CDC docs](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_cdc.html)
  — default TX buffer size (256 B), `setTxBufferSize`/`setRxBufferSize`.
- [xtensa-esp32s3-elf-addr2line usage — Medium](https://stephencowchau.medium.com/inspebacktrace-stack-trace-after-esp32-using-espressif-xtensa-toolchain-7b0bf35905c1)
  and [platformio/platform-espressif32#1083](https://github.com/platformio/platform-espressif32/issues/1083)
  — `addr2line` flag conventions (`-pfiaC`), cross-checked against this environment's actual
  toolchain path.
