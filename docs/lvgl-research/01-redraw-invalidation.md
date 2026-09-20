# Track 1 — LVGL 8.3 invalidation and redraw core mechanics: why an object does not repaint

All line numbers refer to the vendored source at `lib/lvgl/src/...` in this repo (LVGL **8.3.11**),
read directly for this report. Repo-specific files referenced: `src/t-deck/lv_conf.h`,
`src/t-deck/tdeck_main.cpp`, `src/t-deck/lv_obj_functions.cpp`, `src/t-deck/tdeck_debug.cpp`,
`src/esp32/esp32_main.cpp`, `src/esp32/esp32_audio.cpp`.

## TL;DR for the coding agent

1. The pipeline is `lv_obj_invalidate()` → `lv_obj_invalidate_area()` → `lv_obj_area_is_visible()`
   (the actual drop gate) → `_lv_inv_area()` (buffers the rect, resumes the refresh timer) →
   `_lv_disp_refr_timer()` (runs on the `LV_DISP_DEF_REFR_PERIOD` timer, 10 ms here) →
   `lv_refr_join_area()` → `refr_invalid_areas()` → `refr_area()`/`refr_area_part()` → your widgets'
   `DRAW_MAIN`/`DRAW_POST` events → `draw_buf_flush()` → `flush_cb` → your code must call
   `lv_disp_flush_ready()`.
2. **This repo already ships purpose-built instrumentation for every stage of this pipeline** —
   `src/t-deck/tdeck_debug.cpp` — do not build parallel ad-hoc instrumentation, drive the existing
   hooks (`[REDRAW]`, `[REFRSTART]`, `[REFR]`, `[FLUSH]`, `[SCREEN]`, `[UISTAT]`, see §6). It also has
   a full call-stack backtrace on every invalidate, which nothing in the LVGL core gives you for free.
3. `LV_USE_LOG` is **0** in `src/t-deck/lv_conf.h`, so `LV_LOG_WARN`/`LV_LOG_ERROR` compile to
   `do{}while(0)` (`lib/lvgl/src/misc/lv_log.h:107-117`). Every silent-drop code path inside LVGL core
   that "logs a warning" (no active screen, re-entrant invalidation during render, rounder failure,
   ...) produces **zero output** in this firmware as configured. Flip `LV_USE_LOG 1` and register
   `lv_log_register_print_cb()` before trusting the absence of a log line as evidence of anything.
4. `disp_drv.full_refresh` is **currently 0** on this branch (`tdeck_main.cpp:398`,
   commit `0a11757c`). With `full_refresh=1` (the long-standing prior setting) _every_ invalidation,
   however small, forces a full 320×240 redraw — this hides area-tracking bugs and makes "nothing
   redraws" and "everything is slow" look like the same symptom. Diagnose invalidation-area
   correctness with `full_refresh=0` (current state); diagnose flush/timing separately.
5. `lv_obj_set_x/y/width/height/size` to an unchanged value is a **guaranteed no-op**: no style
   write, no invalidate, no event. Confirmed at `lv_obj_pos.c:61,75` (position) and
   `lv_obj_pos.c:166` (size, `lv_obj_refr_size`: _"Do nothing if the size is not changed... It is
   very important else recursive resizing can occur"_). If a widget is repositioned/resized to the
   value it logically already had (e.g. re-running a layout pass with stale cached geometry), LVGL
   will not repaint it even though your code "set" it.
6. `_lv_inv_area()` refuses outright and returns with **no trace at all** (not even a dropped log
   line) when called while `disp->rendering_in_progress == true` — i.e. invalidating an object from
   inside a `DRAW_MAIN`/`DRAW_POST`/`COVER_CHECK` event callback of the _same_ refresh pass
   (`lv_refr.c:212-215`, `LV_LOG_ERROR("detected modifying dirty areas in render")` — silenced by
   point 3 above in this build). This is a classic "I called `lv_obj_invalidate()`, nothing
   happened" trap and it is completely silent in this firmware's current config.
7. The invalid-area ring is `LV_INV_BUF_SIZE = 32` (default, unset in `lv_conf.h`,
   `lv_hal_disp.h:32`). Overflowing it does **not** drop the newest area — it discards the tracking
   of all pending distinct rectangles and substitutes one full-screen rectangle for this refresh
   pass (`lv_refr.c:252-259`). Net visual effect: correct, but a hidden full-screen redraw on a
   frame that looks like it should have been a small partial one — a source of "why is this one
   frame slow" that will only show up in `[REFR]` timing, not in missing pixels.
8. `lv_timer_handler()`/`lv_task_handler()` (the latter is a static-inline alias,
   `lv_api_map.h:35`) is guarded by a function-local `static bool already_running` — a **re-entrant
   call returns immediately (`return 1`) without running a single timer**, including the display
   refresh timer (`lv_timer.c:72-76`). This repo calls `lv_task_handler()` from three places:
   the main `loop()` (`esp32_main.cpp:3847`), a 2-second busy-wait in `addMessage()`
   (`tdeck_main.cpp:284`), and once at the end of `tdeck_reset_msg_tabs()`
   (`lv_obj_functions.cpp:4307`). **Verified by static call-graph as of this branch: none of these
   three are currently nested inside one another** — all `addMessage()`/`tdeck_addMessage()`/
   `tdeck_clear_text_ta()` call sites resolve to the one-shot `setup()` path in `esp32_main.cpp`
   (lines 864, 1293, 1303, 1756-1780), which completes before `loop()` (and hence the outer
   `lv_task_handler()` call) ever runs. Do **not** report this as the live cause of the redraw bug
   without new evidence — but treat the pattern (`lv_task_handler()` in a busy-wait, called from
   arbitrary future call sites) as a landmine: the moment any of these functions is reachable from
   an LVGL event callback (e.g. a button click handler that calls `tdeck_clear_text_ta()`), every
   nested `lv_task_handler()` call inside it becomes a silent 2-second no-op that also blocks the
   _outer_ refresh cycle from returning.
9. This firmware is **effectively single-task with respect to LVGL** as of this branch: grep for
   every `xTaskCreate*` in `src/` and trace each task body — none of them (`gps_task`, `lora_task`,
   `a7682_task`, `con_auth`, the audio `play_function`) calls any `lv_*`/`tdeck_add_*` UI function.
   All LVGL calls happen on the Arduino loop task. **LVGL's thread-safety rule (below) is therefore
   not the live bug either, but it is exactly the kind of constraint a coding agent will violate the
   moment it "fixes" the audio-blocking symptom (Track 2/8 concern) by moving UI updates into the
   audio task or a new task** — flag any PR that adds `lv_*` calls to a second task without a mutex.
10. `LV_TICK_CUSTOM 1` with `LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())` (`lv_conf.h:88-91`): the tick
    source is Arduino `millis()`, a free-running hardware-timer-backed counter that advances
    regardless of task scheduling or `loop()` starvation. **The "stalled tick" failure mode
    (§Findings 10) cannot occur in this firmware's current configuration** — rule it out early
    rather than instrumenting for it.
11. `lv_disp_enable_invalidation(disp, bool)` is a **reference count**, not a flag
    (`disp->inv_en_cnt += en ? 1 : -1`, `lv_disp.c:433`; default `1`, `lv_hal_disp.c:187`). An
    unbalanced disable/enable pair leaves invalidation off forever with no error of any kind. Not
    called anywhere in this repo today (verified by grep) — rule out, but check again if any new
    code calls it.
12. `lv_obj_set_local_style_prop()` (what every `lv_obj_set_style_*()` wrapper ultimately calls)
    **always** calls `lv_obj_refresh_style()` → `lv_obj_invalidate(obj)` unconditionally
    (`lv_obj_style.c:270-276, 167-216`). Setting a style property through the object API cannot
    silently skip invalidation. The only way to get a style change that doesn't repaint is to
    mutate a shared `lv_style_t` object directly (`lv_style_set_*(&my_style, ...)`) without
    following up with `lv_obj_report_style_change(&my_style)` (or `NULL` for all) — not present
    anywhere in this repo's T-Deck code (verified by grep), but check any code that keeps a
    module-level `static lv_style_t` and mutates it after the fact.

## Findings

### 1. The exact 8.3 pipeline, function by function

**Claim**: `lv_obj_invalidate(obj)` (`lv_obj_pos.c:856`) computes `obj->coords` expanded by
`_lv_obj_get_ext_draw_size(obj)`, then calls `lv_obj_invalidate_area(obj, &area)`
(`lv_obj_pos.c:840`), which:

1. Resolves `lv_obj_get_disp(obj)` (`lv_obj_tree.c:264`) and bails if
   `!lv_disp_is_invalidation_enabled(disp)`.
2. Calls `lv_obj_area_is_visible(obj, &area_tmp)` (`lv_obj_pos.c:873`) — **this is the actual
   drop gate**, see Finding 2.
3. Calls the repo's `lv_obj_invalidate_hook(obj, &area_tmp, __builtin_return_address(0))` — a
   `__attribute__((weak))` hook this repo overrides in `tdeck_debug.cpp:151` (see §6).
4. Calls `_lv_inv_area(disp, &area_tmp)` (`lv_refr.c:206`), which intersects the area with the
   screen rect, handles `full_refresh` (short-circuits to one full-screen area, §Finding 7), applies
   `rounder_cb`, dedupes against already-queued areas (`_lv_area_is_in`), appends to
   `disp->inv_areas[disp->inv_p++]` (wrapping per `LV_INV_BUF_SIZE`, §Finding 7), and calls
   `lv_timer_resume(disp->refr_timer)`.
5. The display's `refr_timer` (period = `LV_DISP_DEF_REFR_PERIOD`, 10 ms here) next fires
   `_lv_disp_refr_timer(tmr)` (`lv_refr.c:287`): pauses itself immediately (line 301, re-armed only
   by the next `_lv_inv_area` call via `lv_timer_resume`), runs `lv_obj_update_layout()` on the
   active/previous screen and top/sys layers, then
   `lv_refr_join_area()` → `refr_sync_areas()` (double-buffer direct-mode only, not applicable here
   — single buffer, `buf2 == NULL`) → `refr_invalid_areas()`.
6. `refr_invalid_areas()` (`lv_refr.c:587`) sets `disp->rendering_in_progress = true`, calls
   `render_start_cb` once, then for each un-joined area calls `refr_area()` → `refr_area_part()`,
   which finds the top covering object (`lv_refr_get_top_obj`), draws background if nothing covers,
   walks the object tree via `refr_obj_and_children()` → `refr_obj()` → `lv_obj_redraw()` (sends
   `LV_EVENT_DRAW_MAIN_BEGIN/MAIN/MAIN_END` then recurses into children then
   `DRAW_POST_BEGIN/POST/POST_END`), and finally calls `draw_buf_flush()`.
7. `draw_buf_flush()` (`lv_refr.c:1272`) sets `draw_buf->flushing = 1` and calls your `flush_cb`
   (via `call_flush_cb`, which applies `drv->offset_x/y`). **Your `flush_cb` must call
   `lv_disp_flush_ready(disp)`** or `draw_buf->flushing` never clears and the _next_
   `refr_area_part()` call blocks forever in its `while(draw_buf->flushing) wait_cb()` spin
   (`lv_refr.c:711`) — this repo's `disp_flush()` does call it, synchronously, inside the same
   `xSemaphoreTake`/`Give` block (`tdeck_main.cpp:474`), so this specific failure mode is ruled out
   _unless_ `xSemaphoreTake(xSemaphore, portMAX_DELAY)` itself never returns — see cross-reference
   below.
8. Back in `_lv_disp_refr_timer`, if `inv_p != 0` it clears `inv_areas`/`inv_area_joined`, calls
   `monitor_cb` with elapsed time and pixel count, then frees draw-time scratch buffers.

**Cross-reference (not this track's territory, flag for the audio/SPI track)**: `disp_flush()`
blocks on `xSemaphoreTake(xSemaphore, portMAX_DELAY)`, the same semaphore that serializes TFT and
SD access (per shared context). If that semaphore is ever held elsewhere for longer than expected
(SD I/O, audio path touching SPI), `disp_flush()` — and therefore the entire
`_lv_disp_refr_timer()` call, and therefore the entire outer `lv_task_handler()` call, and
therefore `loop()` — stalls for the duration. This _would_ look exactly like "no redraw happens":
the invalidation queued fine, the refresh timer fired fine, it is simply blocked inside the flush.
Distinguish this from a genuine invalidation-drop with the `[REFRSTART]`/`[FLUSH]` timestamp gap
(§6, stage c/d).

**Source**: `lib/lvgl/src/core/lv_refr.c`, `lib/lvgl/src/core/lv_obj_pos.c`, this repo, read in full
for this report.

### 2. The complete catalogue of silent invalidation drops in 8.3

Each item below is confirmed against source, with the exact gate.

| #   | Condition                                                                                                                                                                                                                                                                                                                                                                    | Where                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       | Confirmed                                                                                                                                                                                                                                |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| a   | `obj` or any ancestor has `LV_OBJ_FLAG_HIDDEN`                                                                                                                                                                                                                                                                                                                               | `lv_obj_area_is_visible`, `lv_obj_pos.c:875` (self) and `:908` (walks up via `lv_obj_get_parent` loop, breaks on first hidden ancestor)                                                                                                                                                                                                                                                                                                                                                                                     | yes                                                                                                                                                                                                                                      |
| b   | `obj`'s screen is not the active screen, previous screen, top layer, or sys layer                                                                                                                                                                                                                                                                                            | `lv_obj_pos.c:878-885` — explicit check against `lv_disp_get_scr_act/prev/layer_top/layer_sys`. **This is the exact mechanism behind "leftover invalidation from an object on a screen that lost `lv_scr_load`"**: an object living on a screen you created but never loaded, or the _old_ screen after `lv_scr_load_anim` completes and evicts `prev_scr`, is invalidated into nothing.                                                                                                                                    | yes                                                                                                                                                                                                                                      |
| c   | Area does not intersect the object's own (ext-size-expanded) coords, or any ancestor's coords, after successive `_lv_area_intersect` truncation walking up the parent chain                                                                                                                                                                                                  | `lv_obj_pos.c:888-899, 910-...` (truncation loop; not fully quoted above but present through the rest of `lv_obj_area_is_visible`)                                                                                                                                                                                                                                                                                                                                                                                          | yes                                                                                                                                                                                                                                      |
| d   | Area intersect with the physical screen rect is empty (partly/fully off-screen), or the object has zero width/height so its coords collapse to an empty rect                                                                                                                                                                                                                 | `_lv_inv_area`, `lv_refr.c:232-233`: `suc = _lv_area_intersect(&com_area, area_p, &scr_area); if(!suc) return;`                                                                                                                                                                                                                                                                                                                                                                                                             | yes                                                                                                                                                                                                                                      |
| e   | `disp->inv_en_cnt <= 0` (via unbalanced `lv_disp_enable_invalidation`)                                                                                                                                                                                                                                                                                                       | `lv_disp.c:210,441-449`, default `1` at creation, `lv_hal_disp.c:187`                                                                                                                                                                                                                                                                                                                                                                                                                                                       | yes — not exercised anywhere in this repo                                                                                                                                                                                                |
| f   | `_lv_inv_area()` called while `disp->rendering_in_progress == true` (invalidating from inside your own `DRAW_MAIN`/`DRAW_POST`/`COVER_CHECK` event handler for the pass currently rendering)                                                                                                                                                                                 | `lv_refr.c:212-215` — hard return, `LV_LOG_ERROR` only (silent in this build, `LV_USE_LOG=0`)                                                                                                                                                                                                                                                                                                                                                                                                                               | yes                                                                                                                                                                                                                                      |
| g   | `disp->inv_p == LV_INV_BUF_SIZE` (32, default) when a _new distinct_ area arrives                                                                                                                                                                                                                                                                                            | Not a drop — see Finding 7, it degrades to one full-screen area, still redraws, just not what you expected and not what your area-based accounting assumes                                                                                                                                                                                                                                                                                                                                                                  | yes                                                                                                                                                                                                                                      |
| h   | `lv_disp_get_default()` (used by any call that passes `disp = NULL`, e.g. some porting/demo code) returns the wrong display, or `NULL` when no display has been registered yet at that point in boot                                                                                                                                                                         | `lv_disp.c` throughout (`if(!disp) disp = lv_disp_get_default();`)                                                                                                                                                                                                                                                                                                                                                                                                                                                          | mechanism confirmed; **not applicable to this repo** — exactly one `lv_disp_drv_t`/one `lv_disp_drv_register()` call exists (`tdeck_main.cpp:390-401`), so there is no "wrong display" to get                                            |
| i   | The refresh timer (`disp->refr_timer`) is paused and nothing calls `lv_timer_resume()` on it again                                                                                                                                                                                                                                                                           | `_lv_inv_area` itself calls `lv_timer_resume(disp->refr_timer)` on every accepted invalidation (`lv_refr.c:239,260`), so a _paused-and-never-resumed_ timer cannot happen through normal invalidation — it can only happen if something else calls `lv_timer_pause(disp->refr_timer)` directly and no further invalidation ever arrives to resume it, or if `lv_timer_handler()` itself is never called again (Finding 8/9)                                                                                                 | this repo pauses two _other_ timers (`msg_flush_timer`, `track_clear_timer` at `lv_obj_functions.cpp:2263,2266`), never `disp->refr_timer` — verified by grep, ruled out                                                                 |
| j   | Style changed on a _shared_ `lv_style_t` object already added to N objects, via `lv_style_set_*()` directly, without a following `lv_obj_report_style_change()`/`lv_obj_refresh_style()`                                                                                                                                                                                     | `lv_style_set_prop()` (`lv_style.c:283`) mutates only the style struct, no invalidate anywhere in that call — invalidation only happens through `lv_obj_refresh_style()` (`lv_obj_style.c:167-216`), which is what `lv_obj_report_style_change()` (`lv_obj_style.c:153`) drives per affected screen tree                                                                                                                                                                                                                    | yes; not present in this repo's T-Deck code (grep found no `lv_obj_report_style_change`/bare `lv_style_set_*` calls in `src/t-deck/`)                                                                                                    |
| k   | `LV_STYLE_PROP_EXT_DRAW`-flagged property (e.g. shadow width/spread, outline) changed via **direct manipulation of `obj->styles[]` or a style struct** bypassing `lv_obj_set_style_*`, so `lv_obj_refresh_ext_draw_size()` never runs — the object's cached `ext_draw_size` stays stale and the _old_, too-small invalidation rect clips the new (larger) shadow/outline off | `lv_obj_refresh_style`, `lv_obj_style.c:206-208`: `if(prop == LV_STYLE_PROP_ANY                                                                                                                                                                                                                                                                                                                                                                                                                                             |                                                                                                                                                                                                                                          | is_ext_draw) lv_obj_refresh_ext_draw_size(obj);` — only runs on this call path | yes, mechanism confirmed; not exercised through the object API, only relevant if code writes styles by hand |
| l   | Widget internals mutated directly instead of through the widget API — e.g. writing into a label's internal text buffer (`lv_label_t.text` via a private cast) instead of `lv_label_set_text()`, or pushing into a chart's `lv_chart_series_t.points` array instead of `lv_chart_set_next_value()`                                                                            | No source citation possible (this is an absence-of-call argument, not a code path) — but it follows directly from Finding 1: the _only_ way to reach `_lv_inv_area()` is through `lv_obj_invalidate()`/`lv_obj_invalidate_area()`, both of which are explicit function calls. Nothing in LVGL polls widget state for changes.                                                                                                                                                                                               | logically certain, flag any direct struct-field write to a widget's private state (i.e. any `((lv_label_t*)obj)->...`-style cast) found during audit as a guaranteed redraw bug                                                          |
| m   | `lv_obj_set_pos`/`set_x`/`set_y`/`set_size`/`set_width`/`set_height` called with the value the object already effectively has                                                                                                                                                                                                                                                | `lv_obj_pos.c:61,75` (`set_x`/`set_y`: compares against the current local style value, calls `lv_obj_set_style_x/y` only if different) and `lv_obj_pos.c:166` (`lv_obj_refr_size`: `if(lv_obj_get_width(obj) == w && lv_obj_get_height(obj) == h) return false;`)                                                                                                                                                                                                                                                           | yes                                                                                                                                                                                                                                      |
| n   | Object created/reparented inside a flex/grid container but never laid out before the redraw pass expects final coordinates — the object still has stale/zero `coords` from creation, so the invalidate that _did_ fire invalidated the wrong (old) rectangle                                                                                                                 | Layout is deferred (`lv_obj_mark_layout_as_dirty`) and only forced at `lv_obj_update_layout()`, which `_lv_disp_refr_timer` calls on `act_scr`/`prev_scr`/top/sys layers _before_ refreshing (`lv_refr.c:309-313`) — so a normal refresh cycle does flush pending layout first; the drop case is code that reads `obj->coords` (e.g. to build a custom invalidation rect, or a manual scroll/position calc) _between_ the layout-marking call and the next refresh timer tick, i.e. before `lv_obj_update_layout()` has run | mechanism confirmed from source; specific trigger is call-order-dependent, flag any code that reads `obj->coords` right after a `lv_obj_set_flex_*`/`lv_obj_align`/parent-add call without an explicit `lv_obj_update_layout(obj)` first |
| o   | `lv_timer_handler()`/`lv_task_handler()` called re-entrantly                                                                                                                                                                                                                                                                                                                 | see Finding 8 in TL;DR and Finding 3 below — separated out because it's high-value enough to need its own section                                                                                                                                                                                                                                                                                                                                                                                                           | yes                                                                                                                                                                                                                                      |

**Fix pattern for the whole table**: for (a)-(d), (n) call `lv_obj_invalidate(obj)` again _after_
the state that determines visibility/geometry is final (after `lv_obj_clear_flag(obj,
LV_OBJ_FLAG_HIDDEN)`, after `lv_scr_load()`, after layout settles) rather than before. For (f), move
the second `lv_obj_invalidate()` call to `LV_EVENT_DRAW_POST_END` of the _outer_ object or to
`lv_async_call()` so it runs on the next pass. For (j)/(k)/(l), always go through the `lv_obj_*`
setter API, never touch `lv_style_t`/widget-private fields directly. For (m), if you must force a
redraw despite unchanged geometry, call `lv_obj_invalidate(obj)` explicitly — it is unconditional
(modulo Finding 2's visibility gate).

### 3. Re-entrant `lv_timer_handler()` — mechanism and this repo's exposure

**Claim**: `lv_timer_handler()` (`lv_timer.c:67`) has a function-local
`static bool already_running`. On entry, if it is already `true`, the function logs a trace line
(silent unless `LV_LOG_TRACE_TIMER`, itself gated behind `LV_USE_LOG`, which is 0 here) and
**returns `1` immediately without executing a single timer callback** — not the display refresh
timer, not indev read, not animation, nothing (`lv_timer.c:73-76`). It is not a queue, not a
deferral — the call is simply a no-op.

**Why**: this is LVGL's cheap re-entrancy guard, not a scheduler. It exists to stop a genuine
recursive-call crash (corrupting the timer linked-list iterator), not to make nested calls "work
later."

**Symptom if violated**: any code path that ends up calling `lv_task_handler()` while a `loop()`-
level `lv_task_handler()` call is still on the C stack (e.g. inside an `LV_EVENT_CLICKED` callback
fired synchronously during `lv_indev_read()`'s own place inside `lv_timer_handler`, or inside a
`DRAW_*` event during a refresh pass) gets nothing: no invalidated areas drawn, no `flush_cb`
called, and the busy-wait pattern this repo uses (`while(cond) { lv_task_handler(); delay(5); }`,
`tdeck_main.cpp:282-286`) degrades to "just `delay(5)` in a loop for up to 2 seconds, doing
absolutely nothing to the display," while also holding the _outer_ `lv_task_handler()` call — and
therefore the outer refresh pass and `loop()` — hostage on the call stack for that whole span.

**This repo's exposure, verified by static call-graph today**:

- `addMessage()` (`tdeck_main.cpp:277-287`) is called only from `esp32_main.cpp` lines 864, 1756,
  1762, 1769, and from `tdeck_addMessage()` (lines 839, 842, 845, 848), which is itself called only
  from `esp32_main.cpp:1293,1303`. **Every one of those call sites is inside the one-shot startup
  sequence, before `loop()` starts** — confirmed by reading the surrounding code, which is
  WiFi/GPS/radio init gated by `#if defined(BOARD_T_DECK) ...` blocks that run once. Not nested.
- `tdeck_reset_msg_tabs()`'s trailing `lv_task_handler()` (`lv_obj_functions.cpp:4307`) is reached
  only via `tdeck_clear_text_ta()` (`tdeck_main.cpp:854-857`), called from exactly one place,
  `esp32_main.cpp:1780`, again inside the one-shot startup sequence.
- **Conclusion**: as this branch stands, none of the three `lv_task_handler()` call sites are
  currently nested. This mechanism does **not** explain the operator's live "no redraw" symptom by
  itself. Do not chase it as the root cause without first finding a _new_ call path (e.g. a future
  change that calls `tdeck_reset_msg_tabs()` or `addMessage()` from inside a button's
  `LV_EVENT_CLICKED` handler, which absolutely would trigger this).
- **Still fix it** as a latent defect: replace every `lv_task_handler()`-in-a-busy-wait with either
  (a) removing the busy-wait and letting the normal `loop()` cadence redraw, or (b) if a genuine
  "flush now" is needed, use `lv_refr_now(NULL)` (Finding 7 below) instead of re-entering the timer
  handler, or (c) guard with a module-level re-entrancy flag your own code checks before calling
  `lv_task_handler()` from a new call site.

**Source**: `lib/lvgl/src/misc/lv_timer.c:67-146`, and the repo files cited above, read in full.

### 4. Threading — LVGL 8.3 is not thread-safe, and what "not thread-safe" actually breaks

**Claim**: LVGL 8.3 provides no internal locking. The sanctioned pattern (LVGL porting docs) is a
single mutex taken before _every_ `lv_timer_handler()` call and around _every other_ `lv_*` call
made from any other task, with exactly two documented exceptions that are safe to call without the
mutex: `lv_tick_inc()` and `lv_disp_flush_ready()`.

```c
void lvgl_thread(void) {
    while (1) {
        mutex_lock(&lvgl_mutex);
        lv_timer_handler();
        mutex_unlock(&lvgl_mutex);
        thread_sleep(10);
    }
}
void other_thread(void) {
    mutex_lock(&lvgl_mutex);
    lv_obj_t *img = lv_img_create(lv_scr_act());
    mutex_unlock(&lvgl_mutex);
}
```

**Why**: every `lv_*` call mutates shared, unprotected state — the object tree, `disp->inv_areas[]`,
the linked list of timers/animations/events. There is no atomicity anywhere. Two `lv_*` calls
interleaved from two tasks can corrupt the timer linked list (the exact same list
`lv_timer_handler`'s iterator walks — see Finding 3), corrupt `inv_areas[]` mid-write (torn reads:
one task's `_lv_inv_area()` half-writes a rect while `_lv_disp_refr_timer()` on another task reads
it), or free an object one task is still dereferencing.

**Symptom if violated**: not necessarily a crash — often exactly this ticket's symptom, "an object
is created/updated but no redraw is triggered," because the corruption is silent (a linked-list
node skipped, an area rect half-updated) rather than a hard fault. This makes it a _plausible but,
per Finding 9, currently unverified_ explanation category for the reported bug: it is the kind of
bug this exact symptom looks like, but this repo's current call graph does not exhibit it (no
second task calls `lv_*`).

**`lv_async_call` note**: `lv_async_call(cb, data)` schedules `cb` to run on the _next_
`lv_timer_handler()` call, on whichever task calls it — it is **not** a thread-safe deferral
mechanism by itself. If called from a non-LVGL task, that call must _also_ be wrapped in the same
mutex as every other `lv_*` call, because `lv_async_call` itself mutates a shared internal list.
`UNVERIFIED` from primary LVGL 8.3 docs (the official os-porting page for 8.x does not mention
`lv_async_call` by name in the fetched excerpt); the "still needs the mutex" rule is a direct
consequence of Finding 4's "every `lv_*` call" wording and is consistent with community guidance
found during research (GitHub issue `lvgl/lvgl#8237`, "Is `lv_async_call`, `lv_timer_create`
thread-safe?" — not independently re-verified against LVGL maintainer reply text in this pass).

**This repo, verified**: no `xTaskCreate*`/`xTaskCreatePinnedToCore` task body in `src/`
(`gps_task`, `lora_task`, `a7682_task`, `con_auth`, `play_function` — the audio playback task) calls
any `lv_*` or `tdeck_add_*`/`tdeck_reset_*` UI function, confirmed by `grep -n "lv_\|tdeck_"` over
each task's source file. All LVGL calls originate from the Arduino `loop()`/`setup()` task. **The
threading rule is therefore not the live cause of the reported redraw bug on this branch** — but it
is the single most important constraint to hand to any future change that moves UI updates onto the
audio task, a BLE/Wi-Fi callback, or a new task created to fix the "audio blocks the UI" symptom
(shared-context symptom 4): if that fix ends up calling `lv_*` from that task, it must take a mutex
shared with the `loop()`-level `lv_task_handler()` call, or introduce a queue that only ever gets
drained on the LVGL task (post an event/struct, let `loop()` call the actual `lv_*` setter).

**Source**: `https://docs.lvgl.io/8/porting/os.html` (redirects to
`https://lvgl.io/docs/open/8/porting/os`) — pattern and the two safe-exception functions quoted
directly from that page; repo call-graph confirmed by direct grep over `src/`.

### 5. `lv_tick_inc` correctness

**Claim**: LVGL's animation/timer scheduling (`lv_timer_time_remaining`, `lv_anim` progress, the
refresh timer's own period) is driven entirely by `lv_tick_get()`, which under `LV_TICK_CUSTOM`
evaluates `LV_TICK_CUSTOM_SYS_TIME_EXPR` on every call rather than accumulating ticks pushed by
`lv_tick_inc()`. If the tick source stalls or runs backward, every time-based decision in LVGL
stalls with it: `lv_timer_handler` still _executes_ (the guard in Finding 3 is unrelated to tick
correctness) but timers whose period hasn't elapsed per the stalled clock never fire, so the
refresh timer can sit paused indefinitely even though invalidation succeeded and queued areas
correctly — this presents as "the object should be there, it was invalidated, but the screen never
updates," i.e. functionally identical to a dropped invalidation from the outside, even though
invalidation worked (`lv_timer_handler` at `lv_timer.c:89-96` even detects and warns about
`handler_start == 0` for 100 consecutive calls specifically because a stuck/zero tick is a known
failure mode — silent here, `LV_USE_LOG=0`).

**This repo, verified**: `LV_TICK_CUSTOM 1`, `LV_TICK_CUSTOM_INCLUDE "Arduino.h"`,
`LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())` (`lv_conf.h:88-91`). `millis()` on ESP32/Arduino is backed
by the hardware `esp_timer`/RTOS tick and free-runs independent of task starvation or `loop()`
blocking — it cannot "stall" the way a manually-pushed `lv_tick_inc()` counter can if the code
calling it gets starved. **This failure mode does not apply to this firmware as configured.** Rule
it out; do not add tick-related instrumentation for this bug.

**Source**: `lib/lvgl/src/misc/lv_timer.c` (tick-zero warning), `src/t-deck/lv_conf.h:87-95`.

### 6. Diagnostic recipe — and the instrumentation already in this repo

**This repo already implements a complete, stage-by-stage diagnostic harness** in
`src/t-deck/tdeck_debug.cpp` (guarded `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`),
wired into the pipeline exactly at the seams a coding agent would otherwise have to instrument by
hand. Use it; do not duplicate it.

| Stage                                                  | Question                                                                               | This repo's existing hook                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | What to look for                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| (a) no invalidation reached LVGL at all                | Did `lv_obj_invalidate*` get called for this object/update?                            | `lv_obj_invalidate_hook(obj, area, ret_addr)` — a **strong override of a `__attribute__((weak))` hook LVGL core calls at `lv_obj_pos.c:852`**, implemented at `tdeck_debug.cpp:151-190`. Enable with `tdeck_dbg_redrawlog(true)`; emits `[REDRAW];ms;<t>;obj;<ptr>;cls;<widget class>;area;x1;y1;x2;y2;ra;<return addr>;bt;<8-frame Xtensa backtrace>` at up to 200 lines/sec (rate-capped, drops counted and flushed as `[REDRAW];dropped;N`). The backtrace (`collect_backtrace`, `esp_backtrace_get_next_frame`) tells you _which caller_ invalidated (or didn't) without manual `__builtin_return_address` chasing.                                              | If your update produces _no_ `[REDRAW]` line: you're in Findings 2(a-d)/2(l)/2(m) territory — the call never reached `lv_obj_invalidate_area`, or `lv_obj_area_is_visible` gated it before the hook fires (**note**: the hook fires _after_ the visibility gate, `lv_obj_pos.c:849-852`, so a missing `[REDRAW]` line does not distinguish "never called `lv_obj_invalidate`" from "called it but `lv_obj_area_is_visible` returned false" — for that distinction, temporarily set a breakpoint/print at the top of `lv_obj_invalidate_area` before the gate, or check `LV_USE_LOG=1` + `LV_LOG_TRACE_DISP_REFR` output for related warnings). |
| (b) invalidation reached LVGL but was dropped/absorbed | Did the queued area make it to a refresh pass?                                         | `tdeck_dbg_render_start_cb` → `[REFRSTART];ms;<t>;areas;<disp->inv_p>` (wired as `disp_drv.render_start_cb`, `tdeck_main.cpp:400`, fires from `refr_invalid_areas()` at `lv_refr.c:604-606`, before any drawing). Also `s_inv_total` (cumulative `_lv_inv_area` acceptances) vs `s_refr_total` (cumulative refresh passes) reported via `tdeck_dbg_uistat()` → `[UISTAT];...;inv_total;N;refr_total;M;...`.                                                                                                                                                                                                                                                          | A `[REDRAW]` line with no following `[REFRSTART]` within ~10 ms (`LV_DISP_DEF_REFR_PERIOD`) means the refresh timer never fired for that invalidation — check whether `disp->refr_timer` is paused and never resumed (Finding 2i), or whether `lv_task_handler()` is being starved/re-entered (Finding 3/8).                                                                                                                                                                                                                                                                                                                                   |
| (c) rendered but not flushed                           | Did drawing happen but the pixels never left the draw buffer?                          | `tdeck_dbg_monitor_cb` → `[REFR];ms;<t>;px;<pixel count>;t_ms;<elapsed>` (wired as `disp_drv.monitor_cb`, fires once per refresh pass from `_lv_disp_refr_timer` at `lv_refr.c:351-353`, **only if `inv_p != 0` after the pass**, line 328). The flush itself is separately logged: `disp_flush()` in `tdeck_main.cpp:470-473` prints `[FLUSH];ms;<t>;area;x1;y1;x2;y2;px;<w*h>;sleeping;<tft_is_sleeping>;bl;<brightness>` right after `pushColors()`/before `lv_disp_flush_ready()`, gated by the same `tdeck_dbg_redrawlog_enabled()` flag.                                                                                                                       | A `[REFRSTART]` with no following `[REFR]`/`[FLUSH]` (or a large gap before it) means you're stuck inside `refr_area_part`'s drawing or its `while(draw_buf->flushing) wait_cb()` spin (`lv_refr.c:709-712`) — almost certainly the shared `xSemaphore` (cross-reference in Finding 1) being held elsewhere (SD card access, audio I2S teardown).                                                                                                                                                                                                                                                                                              |
| (d) flushed but wrong area/pixels                      | Did the right rectangle, with the right content, actually land on the panel?           | `tdeck_dbg_screencrc()` → `[SCREEN];ms;<t>;crc;<8 CRC32 values, one per 30-row band>;nonblack;<N>;total;<pixels>;t_ms;<elapsed>` — reads the **physical panel back over SPI** (`tft.readRect`, under the same `xSemaphore`) and CRC32s it band-by-band (`tdeck_debug.cpp:292-347`). Compare two consecutive `[SCREEN]` snapshots bit-for-bit: unchanged CRCs across a band you expected to change means the flush never reached the panel (or a full-refresh/no-refresh mismatch); changed CRCs in a band you did _not_ touch means an over-large invalidation rect or a stale-coords bug (Finding 2n).                                                              | Combine with `[UISTAT]`'s `ml_y1/ml_y2`/`last_y1/last_y2`/`scroll_y`/`scroll_bottom` fields (`tdeck_debug.cpp:222-239`) to correlate the message-list widget's _logical_ geometry against what the panel readback shows.                                                                                                                                                                                                                                                                                                                                                                                                                       |
| general state dump                                     | What does LVGL currently believe about the UI?                                         | `tdeck_dbg_uistat()` → `[UISTAT]` (active tab, drawer open/closed, live object count via `count_objs_recursive(lv_scr_act())`, msg_list child count, heap/PSRAM free, TFT sleep state and backlight level). `tdeck_dbg_tab(idx)` reports `inv_delta` — the `s_inv_total` delta caused by a single `lv_tabview_set_act()` call, a quick way to check "does switching to this tab invalidate what I expect, and nothing more."                                                                                                                                                                                                                                         |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| LVGL-core-level trace (not yet enabled in this build)  | Fine-grained per-refresh-pass detail from LVGL itself, independent of the repo's hooks | `LV_USE_LOG` is **0** in `src/t-deck/lv_conf.h:233`; `LV_LOG_TRACE_DISP_REFR` etc. are defined at lines 250-257 but are **inside the `#if LV_USE_LOG` block and do not compile in** while `LV_USE_LOG` is 0. To get `REFR_TRACE(...)` output from `lv_refr.c` (macro at `lv_refr.c:90-94`, wraps `LV_LOG_TRACE`, prints "begin"/"finished" and `call_flush_cb`'s area+pointer at `lv_refr.c:1317`) you must set `LV_USE_LOG 1`, set `LV_LOG_LEVEL` to `LV_LOG_LEVEL_TRACE`, keep `LV_LOG_TRACE_DISP_REFR 1`, and register a print callback with `lv_log_register_print_cb()` — none of this is currently active, so absence of core log output today proves nothing. |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |

**Rule for the coding agent**: when asked to diagnose a specific "object X did not redraw" report,
the fastest path is: `tdeck_dbg_redrawlog(true)`, reproduce, then read the `[REDRAW]`/`[REFRSTART]`/
`[REFR]`/`[FLUSH]` stream in order. A gap at any adjacent pair pinpoints the stage from the table
above without needing to add new instrumentation.

### 7. `lv_refr_now()` — when it's right and when it's a bug

**Claim**: `lv_refr_now(disp)` (`lv_refr.c:113-128`) calls `lv_anim_refr_now()` then directly invokes
`_lv_disp_refr_timer(disp->refr_timer)` (or for every registered display if `disp == NULL`),
synchronously, bypassing the normal timer scheduling entirely. It runs a full refresh pass — layout
update, join, draw, flush — right now, on the calling task/stack.

**Right use**: a one-shot, deliberate "I need pixels on the panel before I return control" moment —
e.g. showing a splash/progress screen right before a long blocking operation that will itself starve
`lv_task_handler()` for a while (a firmware update, a long SD write) and you need the _last_ frame
drawn before you leave the LVGL loop entirely. It is meant to be called a handful of times, not per
frame.

**Bug pattern**: calling `lv_refr_now()` instead of just letting the next scheduled
`lv_task_handler()` tick perform the refresh, from inside code that already runs on the LVGL task on
a normal cadence — this reintroduces exactly the reentrancy hazard of Finding 3 if that call happens
to be reached from inside an event callback that is itself inside a refresh pass (same
`disp->rendering_in_progress` and `already_running`-adjacent hazards apply: `_lv_disp_refr_timer` is
not re-entrancy-guarded by itself the way `lv_timer_handler` is — it can be legitimately called
directly by `lv_refr_now`, but calling it while `disp_refr` set to the _same_ display is already
mid-refresh will re-enter `refr_invalid_areas()`/`draw_buf_flush()` against buffer state
(`draw_buf->flushing`, `disp_refr->rendering_in_progress`) that the outer call hasn't unwound yet).
**Never call `lv_refr_now()` from inside a `DRAW_*`/`COVER_CHECK` event or from inside
`flush_cb`/`monitor_cb`/`render_start_cb`.**

**This repo**: `lv_refr_now` is not called anywhere in `src/t-deck/` (verified by grep) — not the
current bug, but the correct recommended tool if a future fix needs a guaranteed-flushed frame
before a blocking SD/audio operation (relevant to shared-context symptom 4/5, "fast touch feedback,
then render the expensive part afterwards" — a candidate pattern is: update the touched widget,
`lv_refr_now(NULL)` once to flush that immediate feedback frame, _then_ do the expensive work,
_never_ inside a callback that's already mid-refresh).

**Source**: `lib/lvgl/src/core/lv_refr.c:113-128`; grep over `src/t-deck/` for absence of use.

## Rules to hand the coding agent

1. Before touching redraw code, enable `tdeck_dbg_redrawlog(true)` and read the `[REDRAW]` →
   `[REFRSTART]` → `[REFR]`/`[FLUSH]` → `[SCREEN]` chain for the specific failing update. Do not
   guess; the instrumentation already exists (§6).
2. If a `[REDRAW]` line is missing entirely for an update you expected to invalidate something,
   check in this order: (1) `LV_OBJ_FLAG_HIDDEN` on the object or any ancestor, (2) whether the
   object's screen equals `lv_scr_act()`/`lv_scr_prev()`/top/sys layer, (3) whether the object's
   coords are zero-sized or fully clipped by an ancestor, (4) whether you're setting a position/size
   value equal to the current one (Finding 2m — this is silent by design), (5) whether you touched
   widget-private state instead of calling the widget's setter API (Finding 2l).
3. If a `[REDRAW]` line exists but no `[REFRSTART]` follows within ~10 ms, do **not** assume a
   dropped invalidation — check `disp->refr_timer`'s pause state and whether `lv_task_handler()` is
   being called re-entrantly (Finding 3) or starved by a long-running block elsewhere in `loop()`.
4. Temporarily flip `LV_USE_LOG` to `1` in `src/t-deck/lv_conf.h` (with `LV_LOG_LEVEL_TRACE` and a
   registered print callback) whenever you need LVGL core's own diagnostics — as shipped
   (`LV_USE_LOG 0`), every `LV_LOG_WARN`/`LV_LOG_ERROR` in the core (including the exact "detected
   modifying dirty areas in render" error for Finding 2f) is a silent no-op. Revert before shipping;
   it adds `printf`-style overhead that is itself dangerous near NimBLE per this repo's own prior
   finding on malloc-in-printf.
5. Never call `lv_obj_invalidate()`/any `lv_obj_set_style_*()`/`lv_obj_set_pos/size()` from inside a
   `LV_EVENT_DRAW_MAIN`/`DRAW_MAIN_BEGIN`/`DRAW_MAIN_END`/`DRAW_POST*`/`COVER_CHECK` handler for the
   object currently being drawn — it is silently dropped by the `rendering_in_progress` gate
   (Finding 2f). Defer to `lv_async_call()` or to the next natural refresh tick instead.
6. Never introduce a `while(cond) { lv_task_handler(); delay(N); }` busy-wait pattern reachable from
   inside an LVGL event callback (button click, tab switch, any `add_event_cb` target) — trace the
   full call graph first. If such a pattern is required, use `lv_refr_now()` for a single guaranteed
   flush instead of re-entering `lv_task_handler()` (Finding 3, Finding 7).
7. If any fix for the audio-blocks-UI symptom moves an `lv_*` call onto a second FreeRTOS task (the
   audio task, a new dedicated task, a callback fired from Wi-Fi/BLE), it must either (a) wrap every
   such call and the `loop()`-level `lv_task_handler()` call in the same mutex, or (b) never call
   `lv_*` off the LVGL task at all — post data through a queue and let the LVGL task's normal cadence
   apply it. Do not ship an `lv_*` call from a second task unguarded (Finding 4).
8. Do not add `lv_tick_inc()`-related instrumentation or "fix the tick" changes — this firmware uses
   `LV_TICK_CUSTOM` bound to Arduino `millis()`, which cannot stall independently of `loop()`
   (Finding 5). If redraws stop, the tick is not why.
9. When you must change `disp_drv.full_refresh`, understand the tradeoff precisely (Finding TL;DR-4):
   `1` masks area-tracking bugs by always drawing everything; `0` (current) exposes real area bugs
   but makes any area-computation defect (Finding 2n, ext-draw-size staleness) visible as a
   genuinely missing patch of pixels rather than a slow full-frame draw. Do not flip it back to `1`
   as a "fix" for a partial-redraw bug without first using `[SCREEN]` CRCs to confirm the area
   computation itself, or you will hide the defect instead of fixing it.
10. `LV_INV_BUF_SIZE` overflow (>32 distinct non-overlapping invalidated rects between refresh
    ticks) degrades silently to one full-screen redraw for that pass — it is not lossy, but it will
    show up as an unexplained slow frame in `[REFR]` timing. If you see that, look for a burst of
    many small, non-adjacent invalidations in a single 10 ms window (e.g. redrawing every row of a
    list individually instead of the list container once) rather than assuming a flush bug.

## Open questions / UNVERIFIED

- Whether `lv_async_call()` itself requires the LVGL mutex when called from a non-LVGL task is
  stated by community sources (GitHub `lvgl/lvgl#8237`) but was not independently re-confirmed
  against the primary LVGL 8.3 porting/os documentation page, whose fetched excerpt did not mention
  `lv_async_call` at all. Treat the "wrap it in the mutex too" guidance as correct-by-inference from
  the general rule ("every `lv_*` call") rather than a directly quoted doc statement.
- Whether any _future_ code path (not present on this branch) could make `addMessage()` or
  `tdeck_reset_msg_tabs()` reachable from inside an LVGL event callback was not checked beyond the
  current call graph — this is a static-analysis-in-time-of-writing statement, not a guarantee that
  holds after further changes on this or other branches.
- The exact wording LVGL 8.3's official docs use for the `LV_INV_BUF_SIZE` overflow behavior was not
  fetched from `docs.lvgl.io`; the behavior described here (Finding 7 / TL;DR-7) is read directly
  from `lv_refr.c:252-259` and is a primary-source claim, not a doc-quote.

## Sources

- `lib/lvgl/src/core/lv_refr.c` (this repo, LVGL 8.3.11 vendored) — full pipeline, `_lv_inv_area`,
  `_lv_disp_refr_timer`, `refr_invalid_areas`, `refr_area`, `refr_area_part`, `draw_buf_flush`,
  `call_flush_cb`, `lv_refr_now`.
- `lib/lvgl/src/core/lv_obj_pos.c` (this repo) — `lv_obj_invalidate`, `lv_obj_invalidate_area`,
  `lv_obj_area_is_visible`, `lv_obj_set_x/y`, `lv_obj_refr_size`, the repo's `lv_obj_invalidate_hook`
  weak-symbol insertion point.
- `lib/lvgl/src/core/lv_obj_style.c` (this repo) — `lv_obj_refresh_style`,
  `lv_obj_set_local_style_prop`, `lv_obj_report_style_change`.
- `lib/lvgl/src/core/lv_disp.c` (this repo) — `lv_disp_enable_invalidation`/
  `lv_disp_is_invalidation_enabled` (reference-counted), `lv_disp_set_bg_*` direct `_lv_inv_area`
  callers.
- `lib/lvgl/src/misc/lv_timer.c` (this repo) — `lv_timer_handler`'s `already_running` guard, tick-
  stall warning.
- `lib/lvgl/src/misc/lv_log.h` (this repo) — `LV_LOG_WARN`/`LV_LOG_ERROR` compiling to no-ops when
  `LV_USE_LOG` is 0.
- `lib/lvgl/src/hal/lv_hal_disp.h` / `lv_hal_disp.c` (this repo) — `LV_INV_BUF_SIZE` default (32),
  `inv_en_cnt` default (1).
- `lib/lvgl/src/lv_api_map.h` (this repo) — `lv_task_handler` as an inline alias of
  `lv_timer_handler`.
- `src/t-deck/lv_conf.h`, `src/t-deck/tdeck_main.cpp`, `src/t-deck/lv_obj_functions.cpp`,
  `src/t-deck/tdeck_debug.cpp`, `src/esp32/esp32_main.cpp`, `src/esp32/esp32_audio.cpp` (this repo)
  — every repo-specific claim above (`full_refresh=0`, tick config, existing debug hooks, call
  graphs for `addMessage`/`tdeck_reset_msg_tabs`, absence of cross-task `lv_*` calls).
- `https://lvgl.io/docs/open/8/porting/os` (redirected from `https://docs.lvgl.io/8/porting/os.html`)
  — official LVGL 8.x thread-safety guidance, the mutex pseudocode, and the two safe-without-mutex
  exceptions (`lv_tick_inc`, `lv_disp_flush_ready`), quoted in Finding 4.
- `https://lvgl.io/docs/open/9.0/CHANGELOG` and related search results (`github.com/lvgl/lvgl`
  issues #4011, #8650, #3298, #6538; `forum.lvgl.io` v8-to-v9 migration thread) — v9 differences
  summarized in TL;DR context: `lv_disp_drv_t`/`lv_indev_drv_t` removed in favor of `lv_display_t`/
  `lv_indev_create()`, buffer size now specified in bytes via `lv_display_set_buffers()`,
  `monitor_cb` removed in favor of an `LV_EVENT_RENDER_READY` event, `lv_conf.h` restructured. Not
  independently re-verified line-by-line against v9 source (out of scope — this repo is 8.3.11) but
  sufficient to warn the coding agent that any v9-era blog post, forum answer, or AI-generated
  snippet describing `lv_display_t`, `lv_display_set_buffers`, or `LV_EVENT_RENDER_READY` does not
  apply to this codebase.
