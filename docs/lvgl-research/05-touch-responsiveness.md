# Track 5 — Perceived responsiveness: fast touch feedback first, expensive render second

See `00-CONTEXT.md` for shared hardware/config facts; not repeated here.

## TL;DR for the coding agent

1. **CRITICAL, repo-specific finding**: `touchpad_read()` in `src/t-deck/tdeck_main.cpp:806-829` — the
   LVGL `read_cb` for the touch indev — calls `tft_on()` synchronously on a wake touch, which chains
   into `setBrightness()` (`src/t-deck/tdeck_helpers.cpp:46`) and `tft.init()`. That init sequence
   (`lib/TFT_eSPI/TFT_Drivers/ST7789_Init.h`) contains **three `delay(120)` calls plus a `delay(10)`**
   (â‰¥370 ms of pure blocking) **plus** `tdeck_helpers.cpp`'s own `delay(5)` + `delay(50)` +
   `delayMicroseconds(30)` around it — all executed _inside_ the input-read callback, i.e. inside
   `lv_timer_handler()`. The very touch meant to wake the device stalls all LVGL processing (input,
   render, and the Arduino `loop()`) for **~400-450 ms**. This is the single largest, most concrete
   violation of "touch should react immediately" in this codebase. Move the TFT wake out of
   `read_cb` (set a flag, wake on the next `loop()` iteration or via `lv_async_call`) — see Finding 3.
2. `lv_timer_handler()` timers are **non-preemptive and run sequentially, single-threaded**, in one
   call. While a blocking `flush_cb` executes (this device: ~37 ms mean/61 ms max full-frame SPI
   push, `docs/tdeck-findings-20260828.md`), **no indev read, no other timer, and no `loop()` code
   after the call site runs** — the whole call is inside that one C stack frame.
3. Never do blocking work (I2C reads longer than ~1 ms, `delay()`, SD/SPI transactions, `tft.init()`)
   inside `read_cb` or an `LV_EVENT_*` callback. Both run synchronously inside `lv_timer_handler()`.
4. For "instant feedback then expensive work": in the event callback, do the cheap visual update
   (change state/style, set text, show a spinner) and **stop**. Do the expensive work off that call
   stack — `lv_async_call()`, a one-shot `lv_timer_create(..., 0, ...)` + `lv_timer_set_repeat_count(t,1)`,
   a chunked timer state machine, or a FreeRTOS worker task that posts results back through a queue
   and a mutex-guarded LVGL call.
5. `lv_refr_now()` forces the pending frame out synchronously — useful to guarantee the "pressed"
   frame actually reaches the panel before starting heavy work, but it **still blocks the caller for
   the full flush duration** (~37-61 ms measured here) and must not be called from inside code that
   is itself inside a `_lv_disp_refr_timer` call (re-entrancy).
6. `lv_async_call()` allocates a small heap block via `lv_mem_alloc` — on this build that's
   `ps_malloc` (PSRAM). PSRAM allocation/access is slower than internal RAM; do not call
   `lv_async_call()` in a tight per-frame loop.
7. `LV_DISP_DEF_REFR_PERIOD = 10 ms` is close to meaningless as a "10 ms refresh" promise here: actual
   full-screen flush takes 37-61 ms, so the refresh timer is effectively rate-limited by flush time,
   not by the period. Lowering the period further buys nothing; raising it to ~30 ms (matching
   `LV_INDEV_DEF_READ_PERIOD`) reduces redundant `_lv_disp_refr_timer` re-entries without changing
   real frame latency, given a single, non-DMA draw buffer.
8. `LV_INDEV_DEF_READ_PERIOD = 30 ms` is reasonable for a capacitive panel and is **not** the
   dominant latency term here — the flush and the wake-path blocking calls are.
9. This device's `flush_cb` (`disp_flush()`) is fully blocking (no DMA, single draw buffer, `buf2 ==
NULL`). This is architecturally the biggest fixed cost standing between touch and visible
   feedback; see Track-level display-buffering tracks for the fix — Track 5 only covers the
   input/event-side mitigations around it.
10. Recommended FreeRTOS shape for this device: keep everything in `loop()`/Arduino task
    (`app_cpu`/core 1, same core as WiFi task's peer client work) rather than inventing a second
    LVGL task, **unless** the display-buffering fix (double buffer + DMA) lands — a separate LVGL
    task only pays off once `flush_cb` stops blocking, otherwise it just moves the freeze into a
    task that still holds a mutex the whole UI depends on. See Finding 6 for the concrete tradeoff
    and the `lvgl_lock()/lvgl_unlock()` pattern to use if/when a second task is introduced.

## Findings

### 1. The input path: read_cb, indev read timer, disp refresh timer, and the worst case

**Claim**: In LVGL 8.3, `lv_indev_drv_register()` creates a repeating `lv_timer_t` running
`lv_indev_read_timer_cb` every `indev_drv->read_period` (default `LV_INDEV_DEF_READ_PERIOD`, 30 ms
here — `src/t-deck/lv_conf.h:84`). `lv_disp_drv_register()` creates a separate repeating timer,
`_lv_disp_refr_timer`, every `LV_DISP_DEF_REFR_PERIOD` (10 ms here — `lv_conf.h:81`). Both are plain
`lv_timer_t` instances managed by the _same_ linked list and dispatched from the _same_
`lv_timer_handler()` call. Per the LVGL 8.3 docs on timers: **"timers are non-preemptive, which means
a timer cannot interrupt another timer"** — confirmed directly in `lib/lvgl/src/misc/lv_timer.c`:
`lv_timer_handler()` walks the timer list with a single `while` loop, guarded by a static
`already_running` flag that makes a re-entrant call return immediately (`return 1;`) rather than run
concurrently.

New timers are inserted at the **head** of the list (`_lv_ll_ins_head` in `lv_timer_create`,
`lib/lvgl/src/misc/lv_timer.c`), and the list is walked from the head
(`_lv_ll_get_head`/`_lv_ll_get_next`). In `src/t-deck/tdeck_main.cpp`, `lv_disp_drv_register()` (line 401) runs _before_ `lv_indev_drv_register()` for touch/mouse/keypad (lines 412, 424, 442). Because
later-created timers sit closer to the head, **the indev read timers run before the display refresh
timer within a single `lv_timer_handler()` pass** — i.e. input is read and processed (marking
invalidated areas) before that same pass's refresh timer fires and flushes them. This is the
favorable ordering: a touch can be read and rendered in the same pass.

**The key fact requested by the operator**: because `lv_timer_handler()` is one synchronous call and
timers cannot interrupt each other, **while `flush_cb` is running (which, on this device, is fully
blocking SPI with no DMA — see `00-CONTEXT.md`), no indev read timer, no other timer, and no code
after the `lv_timer_handler()` call site in `loop()` can run.** The whole MCU is inside the
`disp_flush()` call stack until `tft.endWrite()` returns.

**Worst-case latency arithmetic for this device** (numbers from
`docs/tdeck-findings-20260828.md`: flush avg 36.7 ms / max 36.8 ms; full monitor_cb refresh —
render + flush — avg 56.9 ms / max 61 ms):

- A touch lands **just after** the indev read timer fired in the current pass (worst timing) and a
  full-screen redraw is about to flush. The indev value is not read again until the _next_ due time
  of the read timer, `LV_INDEV_DEF_READ_PERIOD` = 30 ms later — but that next `lv_timer_handler()`
  call cannot even start until the current blocking flush (≤ 61 ms measured max) finishes, because
  `loop()` cannot return to call `lv_timer_handler()` again while inside the flush.
  - Worst case: `flush time (≤61 ms, or up to ~400 ms if it triggers a wake-path `tft.init()`, see
Finding 3) + up to one full read period (30 ms) + refresh period slack (≤10 ms) + the next
frame's own flush to actually paint the acknowledgement (≤61 ms)`.
  - Cold arithmetic without the wake bug: `61 ms (in-flight flush) + 30 ms (read period) + 10 ms
(refresh scheduling slack) + 61 ms (ack frame flush) ≈ 162 ms` worst case, **before** any
    application-level processing.
  - With the wake-path bug (Finding 3) active, add ~370-450 ms on top for the first touch after
    sleep: **~530-610 ms** to first visible reaction. This exceeds the "<100 ms" target in every
    scenario and by more than 5x on a wake touch.
- Best case (indev timer fires right after a touch, refresh timer not mid-flush): input is read,
  processed, and the invalidated area gets flushed in the same or the very next
  `lv_timer_handler()` pass — bounded below by one flush time (~37-61 ms).

**Why**: single draw buffer (`buf2 == NULL`), blocking `flush_cb`
(`src/t-deck/tdeck_main.cpp:458-476`, `xSemaphoreTake` + `tft.pushColors(..., false)` +
`lv_disp_flush_ready`), and non-preemptive cooperative timer scheduling.

**Symptom if violated / ignored**: touches feel "swallowed" for tens to hundreds of ms, worse right
after the screen wakes; this matches the operator's reported general laziness plus the specific "UI
should react immediately" ask.

**Fix direction (own this file's scope; the flush/double-buffer fix itself is out of scope for
Track 5)**: shrink the number and duration of full-frame blocking flushes on the touch path (see
Findings 3-5), and keep `read_cb` itself under a millisecond so it never adds to this budget.

**Source**: `lib/lvgl/src/misc/lv_timer.c` (read directly); LVGL 8.3 timer docs,
https://docs.lvgl.io/8.3/overview/timer.html (redirects to
https://lvgl.io/docs/open/8.3/overview/timer) — quote: "timers are non-preemptive, which means a
timer cannot interrupt another timer"; `docs/tdeck-findings-20260828.md` for measured flush/refresh
numbers; `src/t-deck/tdeck_main.cpp` lines 396-442, 458-476.

### 2. The read_cb contract: fast, non-blocking, and what this device's touch read actually costs

**Claim**: `read_cb` must return quickly (sub-millisecond to low-single-digit ms) because it runs
synchronously inside `lv_indev_read_timer_cb()`, itself inside `lv_timer_handler()` — the same
non-preemptive call stack as Finding 1. LVGL 8.3 docs describe `read_cb` as called periodically to
report current input state; the API contract (confirmed from `lib/lvgl/src/core/lv_indev.c`,
`lv_indev_read_timer_cb`) is a plain synchronous function call with no timeout or cancellation.

In `src/t-deck/tdeck_main.cpp:806-829`, `touchpad_read()` does:

```cpp
if (touch.isPressed()) {
    uint8_t touched = touch.getPoint(x, y, 1);   // I2C transaction(s) to the GT911
    ...
    if (current_brightness_level == 0)
        tft_on();             // <-- see Finding 3: this is the actual problem, not the I2C read
    ...
}
```

The GT911 I2C reads (`isPressed()`/`getPoint()`) themselves are cheap — typically low-single-digit
milliseconds for a couple of register reads at standard/fast I2C clock speeds (`UNVERIFIED` exact
figure for this specific driver/library; general I2C-touch budget, not measured in this repo).
**Polling the GT911 synchronously inside `read_cb` at this cost is acceptable** and is the standard
pattern used by most LVGL+GT911 integrations; it does not need to move to an interrupt-driven model
purely for latency reasons on this hardware.

**Interrupt-driven alternative** (not currently used here): wire the GT911 `INT` pin to a GPIO ISR.
The INT line is asserted by the controller when new touch data is ready and stays asserted until the
host reads the data register; a slow or missing read leaves the line low with no further edges. The
ISR should be minimal — set a flag or copy the raw touch registers into a small volatile buffer — and
`read_cb` then just checks the flag/copies the cached values, at effectively zero cost. This trades a
30 ms worst-case polling latency for near-zero, at the cost of extra wiring/driver complexity; given
that flush time (Finding 1) dominates the budget by 2 orders of magnitude, this is **not the highest
leverage fix** for this device, but is a legitimate future optimization once flush-side latency is
fixed. `UNVERIFIED`: whether the vendored GT911 driver used here (`touch.isPressed()/getPoint()`)
exposes an INT-driven mode at all — not confirmed from source in this pass.

**`data->continue_reading`**: per LVGL 8.3 docs (https://lvgl.io/docs/open/8.3/porting/indev) —
"Setting the `data->continue_reading` flag will tell LVGL there is more data to read and it should
call `read_cb` again," confirmed in `lib/lvgl/src/core/lv_indev.c`:

```c
bool continue_reading;
do {
    _lv_indev_read(indev_act, &data);
    continue_reading = data.continue_reading;
    ...
} while(continue_reading);
```

This lets a driver that buffers multiple queued events (e.g. a FIFO from an interrupt-driven capture)
drain the whole queue within one `lv_indev_read_timer_cb` invocation instead of waiting one read
period per event. `touchpad_read()` here never sets it (single-point read, no buffering) — fine for
a single-touch use case, but if multi-touch/gesture buffering is ever added, `continue_reading`
should be used to drain the buffer within one pass, not spread over multiple 30 ms periods.

**Symptom if violated**: any blocking call inside `read_cb` (I2C hang, long delay) freezes
`lv_timer_handler()` — everything downstream (rendering, animation, other indevs) stalls for that
duration, same mechanism as Finding 1.

**Fix (code)**: keep `read_cb` to register reads and cheap state updates only. Never call display,
SD, or delay APIs from it. See Finding 3 for the concrete repo violation.

**Source**: `lib/lvgl/src/core/lv_indev.c` (read directly); LVGL 8.3 indev porting docs,
https://docs.lvgl.io/8.3/porting/indev.html → https://lvgl.io/docs/open/8.3/porting/indev.

### 3. Repo-specific violation: `tft_on()` runs inside the touch `read_cb`

**Claim**: `touchpad_read()` (`src/t-deck/tdeck_main.cpp:806-829`) calls `tft_on()` directly when a
touch is detected while `current_brightness_level == 0`. This is the wake-from-sleep path and it is
the textbook counter-example of the read_cb contract in Finding 2.

Call chain, all synchronous, all inside the `read_cb`:

```
touchpad_read()                              [src/t-deck/tdeck_main.cpp:817]
  -> tft_on()                                [src/t-deck/lv_obj_functions.cpp:1976]
    -> resetBrightness()                     [src/t-deck/tdeck_helpers.cpp:38]
      -> setBrightness(pre_sleep_brightness_level)  [src/t-deck/tdeck_helpers.cpp:46]
        -> digitalWrite(...); delay(5);                         // CS-line settle
        -> tft.init();                        // TFT_eSPI, ST7789 init sequence
             writecommand(SLPOUT); delay(120);
             ... (register writes) ...
             writecommand(COLMOD); delay(10);
             ... (more register writes, no delay) ...
             end_tft_write(); delay(120); begin_tft_write();
             writecommand(DISPON); delay(120);
             // lib/TFT_eSPI/TFT_Drivers/ST7789_Init.h
        -> tft.setRotation(1);
        -> tft.writecommand(TFT_DISPON); delay(50);
        -> digitalWrite(TDECK_TFT_BACKLIGHT, 1);
        -> delayMicroseconds(30);
```

Summed blocking delay alone: `5 + 120 + 10 + 120 + 120 + 50 = 425 ms`, plus the SPI command/data
traffic for the full ST7789 register sequence and whatever GT911 I2C read preceded it. This entire
chain executes **before `touchpad_read()` returns to `lv_indev_read_timer_cb`**, which itself is
inside `lv_timer_handler()`. Every other timer (display refresh, animations, any deferred/async work)
and all of `loop()` after the `lv_task_handler()` call site is frozen for this duration.

**Why this happened**: `tft_on()`/`setBrightness()` were written as ordinary synchronous helper
functions meant to be called from application code (menu actions, timers), not with the constraint
that one of their call sites is an LVGL `read_cb`.

**Symptom if violated**: the very first touch after the display sleeps takes ~400-450+ ms (plus one
full render/flush cycle to actually show the woken UI, per Finding 1) to produce any visible
reaction — the worst possible instance of "no immediate feedback," and it is the case most likely to
be perceived as "the device is broken" by a user tapping a sleeping screen. This is also very likely
related to the existing hypothesis H-R3 in `docs/tdeck-findings-20260828.md` ("wake path never
invalidates the screen") — the wake path being buried inside a `read_cb` makes it doubly wrong: slow
_and_ it runs in a context where calling LVGL invalidation APIs mid-read is fragile.

**Fix (code)**: never call `tft_on()`/`setBrightness()`/`tft.init()` from `read_cb`. Set a flag in
`read_cb` and act on it from `loop()` (or an `lv_async_call`) instead:

```cpp
// touchpad_read(): read_cb — must stay fast
static void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    static int16_t x[5], y[5];
    data->state = LV_INDEV_STATE_REL;

    if (touch.isPressed()) {
        uint8_t touched = touch.getPoint(x, y, 1);
        if (!meshcom_settings.node_keyboardlock) {
            if (current_brightness_level == 0) {
                wake_requested = true;           // cheap flag, no I/O
            } else {
                tdeck_tft_timer = millis();
            }
            if (touched > 0) {
                data->state = LV_INDEV_STATE_PR;
                data->point.x = x[0];
                data->point.y = y[0];
            }
        }
    }
}

// loop(), after lv_task_handler(), or in a dedicated lv_timer:
if (wake_requested) {
    wake_requested = false;
    tft_on();                 // still ~400ms, but now off the input-read call stack:
                               // lv_timer_handler() returns promptly, other timers keep running
    lv_obj_invalidate(lv_scr_act());  // explicit fix for H-R3 while at it
}
```

This does not make `tft_on()` itself fast — the ST7789 SWRST/SLPOUT sequence genuinely needs those
`delay(120)`s per the panel's own init sequence — but it stops that cost from being charged against
_every_ timer in the system, and specifically stops it from blocking the indev/refresh timers that
have nothing to do with the wake. If sub-100ms wake-touch feedback is required, the real fix is to
skip the full `tft.init()` re-init on wake (SLPOUT + DISPON alone, without the full register
reprogram, is enough to leave a panel that was only put to sleep, not power-cycled) — that is a
display-driver-layer change, out of scope for Track 5, but worth flagging to whichever track owns
`tft_off()`/`tft_on()`.

**Source**: `src/t-deck/tdeck_main.cpp:806-829`; `src/t-deck/lv_obj_functions.cpp:1976-1998`;
`src/t-deck/tdeck_helpers.cpp:38-133`; `lib/TFT_eSPI/TFT_Drivers/ST7789_Init.h` (all read directly
from the vendored source in this repo).

### 4. Event callbacks: what LVGL does while one runs, and the sanctioned deferral mechanisms

**Claim**: An `LV_EVENT_*` callback (e.g. `LV_EVENT_CLICKED` on a button) is invoked synchronously
from inside widget/input processing, which itself runs inside `lv_timer_handler()`. While the
callback runs, **nothing else happens** — no other timer fires, no rendering occurs, no further
input is read — for the exact same non-preemptive reason as Findings 1-2. A slow event callback is
indistinguishable, from a responsiveness standpoint, from a slow `read_cb` or a slow `flush_cb`.

**Deferral mechanisms available in LVGL 8.3**:

1. **`lv_async_call(lv_async_cb_t cb, void *user_data)`** (`lib/lvgl/src/misc/lv_async.c`, read
   directly): allocates a small `lv_async_info_t` via `lv_mem_alloc` (on this build: `ps_malloc`,
   PSRAM), creates a `lv_timer_t` with `period = 0` and `repeat_count = 1`
   (`lv_timer_set_repeat_count(timer, 1)`), and returns immediately. Because period is 0, the new
   timer is due immediately and runs on the **very next** `lv_timer_handler()` call — not "eventually,"
   but the next pass, which on this device is the next `loop()` iteration. The callback runs once
   and the timer self-deletes (`lv_timer_del` is implied by the repeat-count-1 mechanism in
   `lv_timer_exec`, which frees the info struct in `lv_async_timer_cb`). Cost: one small PSRAM
   allocation + one linked-list insertion per call — cheap, but not free; do not call it once per
   frame in a hot loop.

   ```c
   static void do_heavy_work(void *user_data) {
       my_ctx_t *ctx = (my_ctx_t *)user_data;
       // heavy work here, still on the same task/core as lv_timer_handler,
       // so it STILL blocks everything while it runs — see item 5 below for chunking.
   }
   lv_async_call(do_heavy_work, ctx);
   ```

   Note: `lv_async_call` defers _when_ work starts (to the next handler pass, after the current
   event callback and, importantly, after the phase-1 UI update has a chance to be flushed) — it does
   **not** make the deferred work non-blocking by itself. Long deferred work must still be chunked
   (item 5) or moved to a separate task (Finding 6).

2. **One-shot `lv_timer_create` + `lv_timer_set_repeat_count(t, 1)`**: the primitive `lv_async_call`
   is built on. Use directly when you want a **non-zero delay** before the deferred work runs (e.g.
   "start the heavy work 50 ms after the tap, once the pressed-state frame has definitely flushed"):

   ```c
   lv_timer_t *t = lv_timer_create(do_heavy_work, 50, ctx);
   lv_timer_set_repeat_count(t, 1);
   ```

   `lv_async_call` is exactly this with `period = 0`.

3. **A work queue drained by a repeating `lv_timer`**: for work that must be split into many small
   steps regardless of how it was triggered (see item 5), push a job descriptor onto a small FIFO
   (array or ring buffer, no dynamic allocation needed) from the event callback, and let a
   already-running periodic `lv_timer` (e.g. one firing every `LV_DISP_DEF_REFR_PERIOD` alongside
   the redraw) pop and advance one job by a bounded amount of work per call.

4. **FreeRTOS queue to a worker task + notify the UI task**: for genuinely blocking I/O (SD reads,
   network, audio decode — all of which are already off-loaded to their own tasks/paths elsewhere in
   this codebase per `00-CONTEXT.md`), push a request onto a `xQueueSend`-based queue consumed by a
   worker task; the worker does the blocking work on its own stack/core and, on completion, either
   sets a `volatile` result flag consumed by the next `lv_timer_handler()`-adjacent poll, or (if a
   second LVGL-owning task exists, see Finding 6) takes the LVGL mutex briefly to push the result into
   the UI. **Mutex rule**: any LVGL API call (`lv_obj_*`, `lv_label_set_text`, etc.) from a task other
   than the one that normally drives `lv_timer_handler()` must hold the LVGL mutex for the duration of
   that call, and must not hold it across a blocking wait (I2C, SD, network) — take it, do the O(µs)
   LVGL update, release it immediately.

**Symptom if violated**: a button's `LV_EVENT_CLICKED` handler that does the SD read/decode/network
call inline turns "immediate feedback" into "the whole UI freezes for the duration of the operation"
— this is very likely the direct cause of the reported "playing audio blocks the whole device"
symptom if any part of the audio start path is invoked from inside an event callback rather than
being handed to the existing FreeRTOS audio task (`00-CONTEXT.md`, `src/esp32/esp32_audio.cpp`).
`UNVERIFIED` in this pass whether the _triggering_ of `play_function()` happens inside an LVGL event
callback in `lv_obj_functions.cpp` — worth a direct check by whichever track owns the audio path; if
it does, the trigger call itself is cheap (spinning up a task), but any synchronous pre-check (file
existence, I2S teardown per `00-CONTEXT.md`) done before spawning the task would still block.

**Source**: `lib/lvgl/src/misc/lv_async.c`, `lib/lvgl/src/misc/lv_timer.c` (read directly); LVGL 8.3
timer docs, https://lvgl.io/docs/open/8.3/overview/timer — quote: "on the next invocation of
lv_timer_handler()"; general LVGL 8.3 event/task-locking guidance (FreeRTOS mutex pattern) — this is
LVGL's own documented multi-task guidance, not v8.3-specific to this repo; treat the mutex rule above
as standard LVGL RTOS-porting practice, `UNVERIFIED` against a specific v8.3 doc page in this pass
(the general "mutex around every lv_* call from other tasks" rule is longstanding LVGL guidance
across 7.x/8.x/9.x).

### 5. The two-phase pattern: immediate ack, then expensive work

**Claim**: The correct shape for "touch reacts immediately, then render the expensive part" in
LVGL 8.3, given everything above, is:

- **Phase 1 (inside the event callback, must complete in well under 1 ms of app logic)**: mutate
  only cheap widget state — add `LV_STATE_PRESSED`/`LV_STATE_DISABLED`, swap a label's text to
  "Loading…", show an `lv_spinner`, or call `lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE)` removal to
  prevent re-entrant taps. This only _marks_ the affected area invalid (`lv_obj_invalidate`,
  triggered implicitly by the style/state/content change) — it does **not** draw or flush anything
  yet; that is still gated behind the next due `_lv_disp_refr_timer` firing.
- **Optionally force that frame out now** with `lv_refr_now(disp)` (`lib/lvgl/src/core/lv_refr.c:113`,
  read directly) if the operation about to start is long enough that waiting for the next natural
  refresh tick would be perceptible. `lv_refr_now()` calls `lv_anim_refr_now()` and then
  `_lv_disp_refr_timer(disp->refr_timer)` **directly and synchronously** — i.e. it runs the same
  blocking flush as any other refresh, just immediately instead of on the timer's schedule.
  - **Correct to use here**: yes, for exactly this "force the ack frame out before starting heavy
    work" case — that's its documented purpose.
  - **Dangers**: (a) it blocks the caller (the event callback) for the full flush duration (~37-61 ms
    measured on this device) — so phase 1 is no longer sub-millisecond, it's "sub-millisecond of app
    logic + one flush," which is still far better than "app logic + heavy work + flush," but must be
    budgeted; (b) calling it re-entrantly (from inside code already running as part of a
    `_lv_disp_refr_timer` callback, e.g. from a `LV_EVENT_DRAW_*` handler) is unsafe — the refresh
    machinery is not designed to recurse into itself; (c) if called with `disp == NULL` it flushes
    **every** registered display, which is more work than intended if only one display's ack matters.
- **Phase 2 (off the event-callback call stack)**: do the expensive work (SD read, map tile decode,
  network round-trip, audio start) via one of the Finding-4 deferral mechanisms, then perform the
  real UI update (replace "Loading…" with the result, clear the spinner) from wherever phase 2
  finishes — either the chunked timer's completion step, or (if it ran on a separate FreeRTOS task)
  a mutex-guarded LVGL call back on the UI-owning context.

**Reference code**:

```c
typedef struct {
    lv_obj_t *btn;
    lv_obj_t *spinner;
    lv_obj_t *label;
    char path[64];
} load_ctx_t;

static void heavy_work_cb(void *user_data)
{
    load_ctx_t *ctx = (load_ctx_t *)user_data;

    // Phase 2: the actual expensive operation (example: SD read + decode).
    // This still runs on the same task as lv_timer_handler() on this device
    // (no dedicated LVGL task, per Finding 6) -- so it MUST be chunked (item 5 in TL;DR /
    // see the state-machine note below) if it can run longer than a couple of ms,
    // or handed to a FreeRTOS worker task if it is inherently blocking I/O (SD/audio/network).
    bool ok = load_and_decode(ctx->path);

    // Phase 2b: apply the real result. Safe here because we're still on the
    // LVGL-owning context (this callback itself ran via lv_async_call, i.e.
    // from lv_timer_handler()) -- no mutex needed on this device's current
    // (single-task) architecture. If moved to a worker task (Finding 6),
    // this block would need lvgl_lock()/lvgl_unlock() around it instead.
    lv_obj_del(ctx->spinner);
    lv_label_set_text(ctx->label, ok ? "Done" : "Failed");
    lv_obj_clear_state(ctx->btn, LV_STATE_DISABLED);

    lv_mem_free(ctx);
}

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_target(e);

    // ---- Phase 1: immediate, cheap acknowledgement ----
    lv_obj_add_state(btn, LV_STATE_DISABLED);       // prevent re-entrant taps
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, "Loading...");

    load_ctx_t *ctx = lv_mem_alloc(sizeof(load_ctx_t));
    ctx->btn = btn;
    ctx->label = label;
    ctx->spinner = lv_spinner_create(lv_scr_act(), 1000, 60);
    lv_obj_center(ctx->spinner);
    snprintf(ctx->path, sizeof(ctx->path), "/sd/map/%s.bin", get_selected_tile());

    // Force the "Loading..." + spinner frame out NOW, before starting phase 2,
    // instead of waiting for the next 10 ms refresh tick to (maybe) coalesce
    // with whatever phase 2 does next.
    lv_refr_now(NULL);

    // ---- Phase 2: deferred, expensive work ----
    lv_async_call(heavy_work_cb, ctx);
}
```

**Why `lv_async_call` and not a direct function call for phase 2**: a direct call keeps everything
on the _same_ call stack as the event callback, so from LVGL's point of view nothing was deferred —
the whole click handler, ack included, is still one uninterrupted blocking span, and the
`lv_refr_now()` frame was flushed for nothing since the next thing that happens is another block.
Deferring via `lv_async_call` guarantees `lv_timer_handler()` returns to `loop()` between the ack and
the heavy work, which is what actually lets the panel show the ack frame and lets any other pending
timer/indev work run before phase 2 starts.

**Source**: `lib/lvgl/src/core/lv_refr.c:113-128` (`lv_refr_now`, read directly);
`lib/lvgl/src/misc/lv_async.c` (read directly); pattern is standard LVGL practice, not tied to a
single doc page.

### 6. Chunking long work; why `delay()`/`vTaskDelay` in an event callback is not a fix

**Claim**: If phase 2 (Finding 5) cannot be handed to a separate FreeRTOS task (e.g. it's pure CPU
work like decoding many small map tiles, or it must run on the same task as LVGL for simplicity), it
must be split into bounded chunks driven by a repeating `lv_timer`, not run to completion in one
callback invocation — even if deferred via `lv_async_call`, a single `lv_async_call` callback that
itself takes 300 ms to run **still blocks everything for 300 ms**, exactly like Finding 3's
`tft_on()`. Deferring _when_ work starts does not bound _how long_ it runs once started.

**Pattern** — state machine advanced a bounded amount per timer tick:

```c
typedef enum { JOB_IDLE, JOB_READING, JOB_DECODING, JOB_DONE } job_state_t;

typedef struct {
    job_state_t state;
    FILE *f;
    size_t bytes_done;
    size_t bytes_total;
    uint8_t *buf;
    lv_obj_t *label;
} chunk_job_t;

static chunk_job_t job;

static void chunk_timer_cb(lv_timer_t *t)
{
    if (job.state == JOB_IDLE) return;

    uint32_t deadline = lv_tick_get() + 5;   // budget: 5 ms of work this tick

    while (lv_tick_get() < deadline) {
        switch (job.state) {
        case JOB_READING: {
            size_t n = fread(job.buf + job.bytes_done, 1, 512, job.f);
            job.bytes_done += n;
            if (n < 512 || job.bytes_done >= job.bytes_total) job.state = JOB_DECODING;
            break;
        }
        case JOB_DECODING:
            // decode_one_step(...) -- do a bounded slice of decode work
            job.state = JOB_DONE;
            break;
        case JOB_DONE:
            lv_label_set_text(job.label, "Ready");
            job.state = JOB_IDLE;
            lv_timer_pause(t);
            return;
        default:
            return;
        }
    }
    // budget exhausted for this tick; resume on the next call
}

// created once, runs alongside the redraw cadence, paused when idle:
lv_timer_t *chunk_timer = lv_timer_create(chunk_timer_cb, LV_DISP_DEF_REFR_PERIOD, NULL);
```

Each timer invocation does at most ~5 ms of work, then returns control to `lv_timer_handler()`,
which then lets the indev-read and display-refresh timers run in the same or a subsequent pass —
the UI stays responsive (spinner keeps animating, touches keep being read) while the job completes
over many ticks instead of one big block.

**Why `delay()`/`vTaskDelay()` inside the event callback is not a fix**: both simply suspend the
_current_ task without returning to `lv_timer_handler()`. Since this device's `loop()` and LVGL share
one task (no dedicated LVGL task currently — Finding 7), a `delay()` inside an event callback still
blocks input reading, rendering, and every other timer for its duration; it does not yield to
`lv_timer_handler()`'s other work, it only yields to _other FreeRTOS tasks_ (WiFi/BLE stack, audio
task), which is a different and unrelated kind of "not blocking." A `delay(5)` loop that also calls
`lv_task_handler()` on every iteration — the pattern already used at boot in
`src/t-deck/tdeck_main.cpp:282-286` (`while (...) { lv_task_handler(); delay(5); }`) — is
categorically different and _is_ an acceptable busy-wait shape, because it keeps re-entering
`lv_timer_handler()`, letting input/refresh timers fire; but it must not be used **inside** an event
callback or an existing `lv_timer_handler()` call, because `lv_timer_handler()` is guarded against
re-entrancy (`already_running` flag in `lib/lvgl/src/misc/lv_timer.c`) and a nested call from inside
a callback would just return immediately (`return 1;`) without doing anything, while the outer
`delay()` still blocks the one call stack everything depends on.

**Symptom if violated**: UI appears completely hung for the duration of the delay/blocking loop,
touches queued during it are lost or coalesced to stale coordinates once processing resumes.

**Source**: `lib/lvgl/src/misc/lv_timer.c` (`already_running` guard, read directly);
`src/t-deck/tdeck_main.cpp:282-286` (existing repo pattern, read directly, cited as an example of the
_correct_ boot-time busy-wait shape, not as something to copy into an event callback).

### 7. FreeRTOS architecture on ESP32-S3 for this device

**Claim**: Three architectures were asked to be compared.

**(a) Everything in `loop()` (current state)**: single task (Arduino's `loopTask`), pinned to core 1
by the Arduino-ESP32 core, default priority 1. LVGL, TFT flush, touch read, and most application
logic share this one task/call stack. WiFi/BLE system tasks run on core 0 in Arduino-ESP32
(well-established platform behavior: system/RF tasks pinned to core 0, `loopTask` pinned to core 1 —
`UNVERIFIED` exact priority numbers for this specific `platform = espressif32 @ 6.6.0` / Arduino
core 2.x combination in this pass, general figures from ESP32 forum/community consensus place
`loopTask` at priority 1 and WiFi-related tasks at a higher fixed priority around `ESP_TASKD_EVENT_PRIO`
region). **This is what the repo does today.** Given the fully blocking, non-DMA `flush_cb`, this
architecture makes every full-screen flush (~37-61 ms) and every wake-path call (~400+ ms, Finding 3)
a hard freeze of the entire application, including anything that would otherwise run cooperatively
via `delay()`-yielding elsewhere.

**(b) A dedicated, pinned LVGL task with a recursive mutex; all other tasks go through the mutex**:
move `lv_timer_handler()` into its own FreeRTOS task (recommend pinned to **core 1**, same core as
today's `loopTask`/Arduino code, to avoid adding cross-core contention with the WiFi/BLE stack on
core 0), at a priority **above** the default Arduino loop priority but **below** anything
latency-critical to RF (so WiFi/BLE keep-alives are never starved) — a common concrete choice in the
LVGL ESP32 community examples is priority 2-5 for the LVGL task versus priority 1 for
`loopTask`-equivalent app logic, kept below WiFi/BLE task priorities. Recommended stack size for an
LVGL task on ESP32-S3 doing SPI TFT work with `LV_DRAW_COMPLEX` enabled: **8-12 KB**
(`UNVERIFIED` precise minimum for this exact widget/font mix — 8 KB is a commonly cited safe starting
point in LVGL ESP-IDF port examples; profile with `uxTaskGetStackHighWaterMark()` before shipping).
All other tasks (audio, SD, application logic still in `loop()`/other tasks) must never call an
`lv_*` API without holding the LVGL mutex first, and must **never** hold that mutex across a blocking
call (I2C, SPI-to-SD, network). Use a **recursive** mutex specifically because `lv_timer_handler()`
itself, and code called from inside LVGL callbacks that also needs to call back into `lv_*` APIs, may
re-enter on the same task without deadlocking:

```c
static SemaphoreHandle_t lvgl_mutex;

bool lvgl_lock(int timeout_ms)
{
    const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex, ticks) == pdTRUE;
}

void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mutex);
}

void lvgl_task(void *arg)
{
    for (;;) {
        uint32_t next_ms;
        if (lvgl_lock(-1)) {
            next_ms = lv_timer_handler();
            lvgl_unlock();
        } else {
            next_ms = 5;
        }
        vTaskDelay(pdMS_TO_TICKS(next_ms > 0 ? next_ms : 1));
    }
}

// from any OTHER task:
if (lvgl_lock(50)) {
    lv_label_set_text(some_label, "updated");
    lvgl_unlock();
}
```

**Verdict**: this architecture is the standard, well-documented LVGL multi-task pattern and is
correct _in general_, but on **this device, as currently built** (fully blocking, non-DMA
`flush_cb`), moving `lv_timer_handler()` to its own task does **not** fix the core problem — the LVGL
task itself is still frozen for the full flush duration on every refresh, it has just moved which
task is frozen. The only thing it buys, without also fixing the flush, is that _other_ FreeRTOS tasks
(audio, SD, network) stop being blocked by LVGL's freezes, since they're no longer sharing a task/call
stack with it — which does directly address the reported "playing audio blocks the whole device"
symptom, **provided** the audio task does not itself take the LVGL mutex around its blocking I2S/SD
work (per `00-CONTEXT.md`, the audio task already runs on its own pinned task with `vTaskDelay(1)`
yields — it should simply never call `lv_*` APis directly without going through `lvgl_lock`/`unlock`,
and never hold that lock across `audio.loop()`).

**(c) LVGL task + a separate flush/DMA completion path**: architecture (b) plus converting
`disp_flush()` to start a DMA transfer and return immediately, calling `lv_disp_flush_ready()` from a
DMA-complete ISR or a low-latency task notified by that ISR. This is the combination that actually
fixes the root latency problem (Finding 1's 37-61 ms and Finding 3's 400+ ms stop being _whole-system_
freezes and become, at most, "LVGL task busy" windows that no longer stall audio/network/other UI
input processing). This requires double buffering (`buf2 != NULL`) to be worthwhile — with a single
buffer, LVGL still must wait for the in-flight DMA to finish before it can safely start drawing into
the same buffer again, so the wall-clock flush cost doesn't disappear, it just stops blocking
_other_ tasks. **This is out of Track 5's scope** (display buffering/DMA is a flush-side change, not
an input/event-side one) but is the necessary companion to make (b) fully pay off; flag it to the
track that owns `disp_flush()`/`lv_disp_draw_buf_init`.

**Recommendation for this codebase right now**: adopt **(a) with the Finding-3 fix** (move
`tft_on()` off the `read_cb` call stack) and the Finding-5/6 event-callback discipline (never block
in a callback, defer + chunk) as the immediate, low-risk change. Treat **(b)+(c)** as the follow-up
once the flush path is converted to DMA/double-buffered — introducing a second task around a still-
fully-blocking flush adds mutex/priority complexity for a smaller win than fixing the flush directly
first.

**Tick source**: this build already uses `LV_TICK_CUSTOM 1` with
`LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())` (`src/t-deck/lv_conf.h:88-91`) rather than an `esp_timer`-
driven `lv_tick_inc()`. `millis()` on Arduino-ESP32 has ~1 ms resolution, which is adequate for a
10-30 ms period system; an `esp_timer`-based periodic `lv_tick_inc(1)` (1 ms hardware timer ISR) is
the alternative and is marginally more accurate under heavy task contention, but is not needed to hit
the latency targets in this document — the tick source is not on the critical path found by this
track. No change recommended here.

**Source**: `src/t-deck/lv_conf.h:86-95` (read directly); Arduino-ESP32 core/WiFi task pinning is
well-established community knowledge (ESP32 forum threads on `loopTask`/WiFi task priority/core
assignment) — `UNVERIFIED` against this repo's exact pinned `platform = espressif32 @ 6.6.0` version
in this pass, treat the core/priority numbers as directional, not exact; `lvgl_lock/unlock` pattern
is the standard LVGL ESP-IDF port idiom, not unique to this repo.

### 8. Touch/indev tuning knobs (LVGL 8.3 defaults, all confirmed from `lib/lvgl/src/hal/lv_hal_indev.h` /

`lv_hal_indev.c`, read directly — this repo does not override any of these in `lv_conf.h`, so the
touch indev uses these built-in defaults verbatim)

| Macro                               | Default                                                 | What it does                                                                                                                                                                              | Recommendation for this 320x240 capacitive panel                                                                                                                                                                                                                                                                                                                                                                                                           |
| ----------------------------------- | ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `LV_INDEV_DEF_READ_PERIOD`          | 30 ms (`lv_conf.h:84`, overridden from library default) | How often `read_cb` is polled.                                                                                                                                                            | Keep at 30 ms. Lower (e.g. 10-15 ms) would shave a few ms off worst-case input latency but this device's dominant latency term is flush time (Finding 1), not read period — not worth the extra I2C traffic/CPU.                                                                                                                                                                                                                                           |
| `LV_INDEV_DEF_SCROLL_LIMIT`         | 10 px                                                   | Minimum drag distance before a press becomes a scroll gesture instead of a click.                                                                                                         | 10 px is reasonable for a 320x240 capacitive panel; lower it (e.g. 5-7) only if users report taps being misread as scrolls, but too low causes accidental scroll-vs-click ambiguity. No change recommended without user feedback.                                                                                                                                                                                                                          |
| `LV_INDEV_DEF_SCROLL_THROW`         | 10 (%)                                                  | Momentum decay rate for scroll release (higher = stops sooner, lower = coasts longer). Drives `lv_obj_scroll_by(..., LV_ANIM_ON)` in `lib/lvgl/src/core/lv_indev_scroll.c`.               | Given the blocking, non-DMA flush (Finding 1), scroll momentum re-renders on every animation tick (`LV_DISP_DEF_REFR_PERIOD`-paced, see Finding 9) and each tick can cost a real flush. Consider **raising** this value (e.g. 15-20) to shorten the coast duration and reduce the number of expensive animated refreshes triggered per swipe, at the cost of a less "flingy" feel. Directly trades polish for flush-bound responsiveness on this hardware. |
| `LV_INDEV_DEF_LONG_PRESS_TIME`      | 400 ms                                                  | Time held before `LV_EVENT_LONG_PRESSED` fires.                                                                                                                                           | 400 ms is a standard, reasonable default; no repo-specific reason to change it.                                                                                                                                                                                                                                                                                                                                                                            |
| `LV_INDEV_DEF_LONG_PRESS_REP_TIME`  | 100 ms                                                  | Repeat interval for `LV_EVENT_LONG_PRESSED_REPEAT` (e.g. holding a +/- button).                                                                                                           | 100 ms is fine as long as whatever the repeat handler does is cheap (Finding 4/5 discipline) — each repeat is effectively a mini event callback; if it triggers a full flush every 100 ms that's an additional ~40-60 ms bus cost every repeat, tolerable but worth being aware of for any held-button UI (e.g. brightness/volume steppers).                                                                                                               |
| `LV_INDEV_DEF_GESTURE_LIMIT`        | 50 px                                                   | Minimum drag distance before a swipe registers as a screen-level gesture (`LV_EVENT_GESTURE`).                                                                                            | Reasonable default for a 320px-wide panel (~15% of width); no change recommended absent specific gesture-navigation UI.                                                                                                                                                                                                                                                                                                                                    |
| `LV_INDEV_DEF_GESTURE_MIN_VELOCITY` | 3 (LVGL internal units, ~px per read period)            | Minimum speed for a drag to count as a gesture rather than a slow scroll.                                                                                                                 | Default is fine; not a responsiveness lever for this device's actual bottleneck.                                                                                                                                                                                                                                                                                                                                                                           |
| `lv_indev_wait_release(indev)`      | API, not a macro                                        | Blocks further click processing on that indev until the current press is released — used to suppress a click event you don't want to fire (e.g. after showing a modal from a long-press). | Use when phase-1 feedback (Finding 5) changes what a still-held finger is touching (e.g. a button morphs into a different control) and you must prevent a spurious click on release. Cheap, synchronous, safe to call from an event callback.                                                                                                                                                                                                              |
| `lv_indev_reset(indev, obj)`        | API, not a macro                                        | Resets the input device's processing state, optionally for one object; used when an object being interacted with is deleted mid-gesture.                                                  | Call this whenever phase-2 work (Finding 5) results in deleting/replacing the very object the user just pressed (e.g. replacing a "Loading" placeholder with real content) to avoid LVGL dereferencing a stale `indev_obj_act` pointer — this repo already needs this discipline anywhere a tapped widget gets destroyed as part of its own click handler's _effects_.                                                                                     |

**Source**: `lib/lvgl/src/hal/lv_hal_indev.h` and `lib/lvgl/src/hal/lv_hal_indev.c` (defaults, read
directly); `lib/lvgl/src/core/lv_indev_scroll.c` (scroll-throw mechanics, read directly);
`lib/lvgl/src/core/lv_indev.c` (`lv_indev_wait_release`, `lv_indev_reset`, read directly).

### 9. `LV_DISP_DEF_REFR_PERIOD`: 10 ms is not automatically better than 30 ms here

**Claim**: `LV_DISP_DEF_REFR_PERIOD` sets how often the `_lv_disp_refr_timer` timer is _due_ to run,
not how long a refresh takes. On this device, a full-screen refresh takes 37-61 ms measured
(`docs/tdeck-findings-20260828.md`) — **3.7x to 6.1x longer than the 10 ms period**. Because
`lv_timer_handler()` timers are non-preemptive (Finding 1), the refresh timer cannot fire again until
the current one finishes; LVGL does not "pile up" multiple pending refreshes into a queue — each
refresh pass processes whatever invalidated areas exist _at the time it runs_ and merges/coalesces
them, then the timer becomes due again only after its full period has elapsed _from its last run_,
by which point it is almost always immediately overdue and fires again on the very next
`lv_timer_handler()` call. In practice, with `full_refresh` and a slow flush, the refresh timer
effectively runs back-to-back, rate-limited by flush time, not by the configured 10 ms — **lowering
`LV_DISP_DEF_REFR_PERIOD` below the actual flush time buys nothing**, and raising it toward or above
the flush time (e.g. 30-40 ms) does not make refresh slower in practice (the flush is still the
limit) while it does two useful things: (1) it reduces how often the refresh timer's own list-walk
and due-time bookkeeping runs relative to the indev timer in `lv_timer_handler()`'s per-pass
overhead — a minor effect; (2) more importantly, when `full_refresh=0` (partial refresh, the branch
under trial per `00-CONTEXT.md`), a shorter period causes more frequent _small_ refresh passes for
incrementally-invalidated areas (e.g. a blinking cursor, a live clock), each with per-flush fixed
overhead (`xSemaphoreTake`, `tft.setAddrWindow`, `startWrite`/`endWrite`); a period closer to the
indev read period (30 ms) coalesces more of those into one pass without perceptibly increasing
input-to-feedback latency, since 30 ms is already within the sub-100ms budget.

**Recommended value**: **keep 10 ms while `full_refresh=1`** (no behavioral difference either way,
since flush time dominates and this avoids unnecessary config churn); if/when the
`tdeck-partial-refresh-trace` branch's `full_refresh=0` change lands, **raise to ~20-30 ms** to reduce
per-tick coalescing overhead for small partial refreshes without adding perceptible latency (both
values are well under the 100 ms budget).

**Does LVGL "pile up" work / starve input while a blocking flush runs?** No pile-up mechanism exists
to observe — because `lv_timer_handler()` is a single synchronous call, the refresh timer's callback
(`_lv_disp_refr_timer`) simply does not return control until the flush (`disp_flush()`,
`lv_disp_flush_ready`) completes, so by construction nothing else — including the indev read timer —
can run _during_ that flush (this is the same mechanism as Finding 1, restated here specifically for
the refresh-period question). There is no separate "queue" of refresh requests; invalidated areas
accumulate in LVGL's internal invalid-area list until the next time the refresh timer's callback
actually executes, at which point it draws and flushes whatever is currently marked invalid.

**Source**: `lib/lvgl/src/core/lv_refr.c` (`_lv_disp_refr_timer`, `full_refresh` branches at lines
236, 638, 643, read directly); `lib/lvgl/src/misc/lv_timer.c` (non-preemptive execution, read
directly); `docs/tdeck-findings-20260828.md` (measured flush/refresh numbers).

### 10. Animations: cost, step rate, and what to trim on this SPI panel

**Claim**: LVGL's animation subsystem runs on its own internal timer created once at
`lv_anim_core_init()`: `lib/lvgl/src/misc/lv_anim.c:60` — `_lv_anim_tmr = lv_timer_create(anim_timer,
LV_DISP_DEF_REFR_PERIOD, NULL);` (read directly). **This confirms explicitly**: the animation step
rate is tied to `LV_DISP_DEF_REFR_PERIOD` (10 ms here), meaning every active `lv_anim_t` gets stepped
roughly every 10 ms, each step typically invalidating some area and thus queuing more work for the
(expensive, 37-61 ms) refresh timer. An animation running for e.g. 300 ms produces on the order of 30
steps, and if each step's invalidated area forces a distinguishable refresh, that is up to 30 flush
cycles at ~37-61 ms each competing for the same non-preemptive call stack as touch input — a single
animation can dominate the device for well over a second of wall-clock time if not careful, even
though the animation's own logical duration is only 300 ms.

**Default LVGL animations relevant here, and what to trim**:

- **Scroll momentum ("throw")**: confirmed real and active — `lib/lvgl/src/core/lv_indev_scroll.c`
  calls `lv_obj_scroll_by(scroll_obj, ..., LV_ANIM_ON)` on release when there's residual velocity.
  This is the animation most likely to be visible/costly on a message list or menu. See Finding 8's
  recommendation to raise `LV_INDEV_DEF_SCROLL_THROW` to shorten it.
- **Dropdown open animation**: **not present** in this build — checked `lib/lvgl/src/widgets/
lv_dropdown.c` directly; its internal list positioning uses `lv_obj_scroll_to_y(..., LV_ANIM_OFF)`
  explicitly. No default open/close animation to trim here in 8.3 for this widget as configured.
- **Button press "grow" transition**: this is a **theme** feature
  (`LV_THEME_DEFAULT_GROW`/`LV_THEME_DEFAULT_TRANSITION_TIME` under `LV_USE_THEME_DEFAULT`, see
  `lv_conf.h:576-587`) and `LV_USE_THEME_DEFAULT` is **0** in this build, with no `lv_theme_*`/
  `lv_disp_set_theme` call found anywhere under `src/t-deck/` (grepped directly) — so **no
  theme-driven press/transition animation is active in this codebase today**. Any perceived "press
  feedback" currently comes only from explicit application code (state/style changes made directly
  in event handlers), which is exactly the Finding-5 pattern to keep using — cheap, explicit,
  no animation timer involvement, no discovery needed on this axis.
- **Style transitions (`lv_style_transition_dsc_t`)**: not currently used by this codebase for any
  widget (no theme, and no direct greps found for `lv_style_transition_dsc_init`/`lv_style_set_
transition` under `src/t-deck/` in this pass — `UNVERIFIED`: confirm with a repo-wide grep before
  relying on "none exist" as exhaustive). If phase-1 acknowledgement styling (Finding 5) is later
  implemented via a style transition instead of an immediate property set, be aware that a
  transition is itself an `lv_anim_t` stepped at `LV_DISP_DEF_REFR_PERIOD` — use a **short** duration
  (e.g. 80-120 ms) so it does not itself become the latency source it was meant to hide, and prefer
  an immediate (non-transitioned) property change for the very first "acknowledge the tap" frame,
  reserving transitions for secondary polish only.

**Recommendation**: on this slow-SPI-panel build, treat every `LV_ANIM_ON` call as "N extra flush
cycles, N ≈ duration/refresh_period" and budget accordingly; prefer `LV_ANIM_OFF` for anything in the
touch-feedback path itself (per Finding 5's phase 1, which already uses immediate property changes,
not animations); keep animation use to cases where the animated motion itself is the point (scroll
momentum) and shorten those via the throw setting rather than removing scrolling animation outright
(removing it entirely would feel broken on a capacitive panel).

**Source**: `lib/lvgl/src/misc/lv_anim.c:60` (read directly, the load-bearing fact for this finding);
`lib/lvgl/src/core/lv_indev_scroll.c` (read directly); `lib/lvgl/src/widgets/lv_dropdown.c` (read
directly); `src/t-deck/lv_conf.h:576-590` and grep of `src/t-deck/` for `lv_theme_`/`lv_disp_set_theme`
(none found, read directly).

### 11. Latency budget table for this device (target: < 100 ms touch-to-visible-feedback)

| Component                                                | Best case                   | Typical                     | Worst case (current code)                           | Notes                                                                                                                                                                  |
| -------------------------------------------------------- | --------------------------- | --------------------------- | --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| GT911 I2C read (`touchpad_read`)                         | <1 ms                       | 1-3 ms                      | 1-3 ms                                              | `UNVERIFIED` exact figure for this driver; not the bottleneck either way.                                                                                              |
| Wait for indev read timer to be due                      | 0 ms                        | 0-15 ms                     | up to 30 ms (`LV_INDEV_DEF_READ_PERIOD`)            | Only matters if a flush isn't already in flight; see next row for why it's usually moot.                                                                               |
| **Wake-path blocking call inside `read_cb`** (Finding 3) | 0 ms (screen already awake) | 0 ms (screen already awake) | **~400-450 ms** (`tft.init()` + surrounding delays) | Only on the first touch after sleep; entirely avoidable by moving `tft_on()` off `read_cb` (Finding 3 fix). **This is the single highest-leverage fix in this track.** |
| Event callback (phase 1, immediate ack)                  | <1 ms                       | <1 ms                       | unbounded if the contract (Finding 4) is violated   | Must stay sub-millisecond of app logic; enforced by code review, not by LVGL itself.                                                                                   |
| Flush of the ack frame (`disp_flush`, blocking SPI)      | ~37 ms                      | ~37-57 ms                   | ~61 ms measured (`docs/tdeck-findings-20260828.md`) | Dominant, unavoidable-without-DMA cost; out of Track 5's scope to fix directly (see Finding 6c).                                                                       |
| **Total, screen already awake**                          | ~38 ms                      | ~40-60 ms                   | ~91 ms (30+61)                                      | Within the <100 ms target in typical/best case; worst case is borderline and dependent on unlucky timer-due-time alignment.                                            |
| **Total, screen waking from sleep**                      | n/a                         | n/a                         | **~530-610 ms**                                     | Exceeds target by 5-6x; entirely attributable to Finding 3 and fixable independently of the flush/DMA work.                                                            |

**How to measure** (do not duplicate Track 7's observability tooling — this row only points at it):
this repo already has `--redrawlog on/off` and `monitor_cb`/`render_start_cb` instrumentation per
`00-CONTEXT.md` and `docs/tdeck-findings-20260828.md`; use those existing hooks (timestamp at
`touchpad_read()` press-detected vs. timestamp at the corresponding `[REFR]`/flush-complete log line)
to get a real touch-to-pixel number for this device rather than relying solely on the arithmetic
above. See Track 7's file for the full instrumentation inventory.

## Rules to hand the coding agent

1. Never call any function that performs SPI/I2C/SD/network I/O, `delay()`, or `tft.init()`-class
   panel reinitialization from inside an LVGL `read_cb` or an `LV_EVENT_*` callback. Both execute
   synchronously inside the single, non-preemptive `lv_timer_handler()` call.
2. Fix `touchpad_read()` (`src/t-deck/tdeck_main.cpp:806-829`) so it only sets a flag on a
   wake-requested touch; move the actual `tft_on()` call to `loop()` (after the `lv_task_handler()`
   call) or an `lv_async_call`-deferred callback. Do not leave `tft_on()` reachable from `read_cb`.
3. For any UI action that triggers work costing more than roughly 1-2 ms of CPU beyond simple widget
   property/state changes: do the visible acknowledgement (state change, spinner, disable) directly
   in the event callback with plain `lv_obj_*` calls (no `LV_ANIM_ON` for this first frame); then
   call `lv_async_call()` (or a one-shot `lv_timer_create` + `lv_timer_set_repeat_count(t,1)` if a
   deliberate delay is wanted) to run the expensive work; never inline the expensive work in the
   callback itself.
4. If the acknowledgement must be guaranteed visible before the expensive work starts (not just
   before it _finishes_), call `lv_refr_now(NULL)` immediately after the phase-1 changes and before
   deferring phase 2 — but only from a call site that is not itself already inside a
   `_lv_disp_refr_timer` invocation (i.e. not from a draw/refresh-related event), and budget its cost
   (~37-61 ms on this device) into the perceived total.
5. Any work that must run in multiple steps over more than ~5 ms total must be chunked: a repeating
   `lv_timer` doing a bounded amount of work per call (the state-machine pattern in Finding 6), not a
   single long-running callback, and not a `delay()`/`vTaskDelay()` loop that doesn't call back into
   `lv_timer_handler()`. The one exception is a top-level (not-inside-an-LVGL-callback) busy-wait that
   itself calls `lv_task_handler()` on every iteration (as already done at
   `src/t-deck/tdeck_main.cpp:282-286`) — that pattern is fine outside of callbacks, never inside one.
6. Do not introduce a second, LVGL-owning FreeRTOS task purely to fix touch responsiveness until the
   `flush_cb` is converted to DMA with double buffering (`buf2 != NULL`) — on the current
   fully-blocking, single-buffer flush, a dedicated LVGL task only relocates the freeze, it does not
   remove it, though it does correctly stop _other_ tasks (notably audio) from sharing that freeze if
   they are not sharing the LVGL task. If/when a second task is introduced, use a **recursive** mutex
   (`xSemaphoreCreateRecursiveMutex`) with the `lvgl_lock()`/`lvgl_unlock()` pattern in Finding 7, and
   never hold that mutex across a blocking I2C/SPI/network call from any task.
7. Do not lower `LV_DISP_DEF_REFR_PERIOD` below its current 10 ms — it has no effect while flush time
   (37-61 ms) dominates. If/when `full_refresh=0` (partial refresh) ships, consider raising it to
   20-30 ms to reduce per-tick fixed overhead on small partial refreshes; either value stays within
   budget.
8. Do not change `LV_INDEV_DEF_READ_PERIOD` (30 ms) — it is not the bottleneck on this device; the
   wake-path bug (rule 2) and the flush cost dominate by more than an order of magnitude.
9. Avoid `LV_ANIM_ON` in the touch-feedback (phase 1) path; each animated step costs roughly one more
   refresh-timer pass at up to ~61 ms. Reserve animation for scroll momentum, and consider raising
   `LV_INDEV_DEF_SCROLL_THROW` (e.g. to 15-20) to shorten how many animated ticks a swipe triggers on
   this hardware.
10. Whenever an event callback's _effects_ (including deferred phase-2 work) delete or replace the
    object that was originally pressed, call `lv_indev_reset(indev, obj)` on the relevant indev(s)
    before or as part of that deletion to avoid a stale `indev_obj_act` reference; call
    `lv_indev_wait_release(indev)` when a still-held finger must not trigger a click on release
    because phase 1 changed what's under it.

## Open questions / UNVERIFIED

- Exact I2C transaction cost of `touch.isPressed()`/`touch.getPoint()` for the specific GT911 driver
  vendored/used in this repo — not measured in this pass; assumed low-single-digit ms based on
  general I2C-touch experience, not confirmed against this exact driver's source.
- Whether the vendored GT911 driver exposes an interrupt-driven read mode at all (Finding 2) — not
  confirmed from source in this pass; only general external documentation about GT911 INT-pin
  behavior was checked.
- Whether the audio-start trigger path is reachable from inside an LVGL event callback (Finding 4) —
  flagged as a likely contributor to the "audio blocks the whole device" symptom but not traced end
  to end in this pass; recommend the track/agent owning `src/esp32/esp32_audio.cpp` and
  `lv_obj_functions.cpp`'s call sites confirm this directly.
- Exact Arduino-ESP32 `loopTask`/WiFi-task priority numbers for this repo's pinned
  `platform = espressif32 @ 6.6.0` / Arduino core 2.x combination — general community figures cited
  in Finding 7, not verified against this exact toolchain version.
- Recommended LVGL task stack size (8-12 KB suggested in Finding 7) is a general starting figure, not
  profiled against this repo's actual widget/font/PSRAM configuration.
- Whether `lv_style_transition_dsc_t` is used anywhere in `src/t-deck/` — a targeted grep in this pass
  found none, but was not repo-exhaustive; confirm before stating "not used" as fact in the merged
  document if that claim matters to another track.
- The LVGL 8.3 official docs pages for `porting/indev`, `overview/timer`, `porting/display`, and
  `overview/animation` did not explicitly document the "flush_cb blocks all input reading" mechanism
  in their prose — that fact is derived directly from reading `lib/lvgl/src/misc/lv_timer.c` and
  `lib/lvgl/src/core/lv_indev.c` source in this repo, not from an explicit doc statement, though it
  is consistent with and implied by the documented "non-preemptive" timer behavior.

## Sources

- `lib/lvgl/src/core/lv_indev.c` — read directly; `lv_indev_read_timer_cb`, `continue_reading` loop.
- `lib/lvgl/src/misc/lv_timer.c` — read directly; `lv_timer_handler` non-reentrancy, LL head-insertion
  order.
- `lib/lvgl/src/misc/lv_async.c` — read directly; `lv_async_call` implementation (allocates via
  `lv_mem_alloc`, period-0/repeat-1 timer).
- `lib/lvgl/src/core/lv_refr.c` — read directly; `lv_refr_now`, `_lv_disp_refr_timer`, `full_refresh`
  branches.
- `lib/lvgl/src/hal/lv_hal_indev.h` / `lv_hal_indev.c` — read directly; `LV_INDEV_DEF_*` default
  values and their assignment in `lv_indev_drv_init`.
- `lib/lvgl/src/core/lv_indev_scroll.c` — read directly; scroll-throw momentum animation.
- `lib/lvgl/src/widgets/lv_dropdown.c` — read directly; confirms no default open animation.
- `lib/lvgl/src/misc/lv_anim.c` — read directly; animation timer tied to `LV_DISP_DEF_REFR_PERIOD`.
- `src/t-deck/lv_conf.h` — read directly; all local config values cited throughout.
- `src/t-deck/tdeck_main.cpp` — read directly; `touchpad_read`, `disp_flush`, driver registration
  order, boot-time busy-wait pattern.
- `src/t-deck/lv_obj_functions.cpp` — read directly; `tft_on()`.
- `src/t-deck/tdeck_helpers.cpp` — read directly; `setBrightness()`/`resetBrightness()`.
- `lib/TFT_eSPI/TFT_Drivers/ST7789_Init.h` — read directly; ST7789 init sequence delay costs.
- `docs/tdeck-findings-20260828.md` — measured flush/refresh timing numbers (in-repo, cited per
  shared context rules, not reproduced wholesale).
- https://lvgl.io/docs/open/8.3/overview/timer — LVGL 8.3 timer docs (redirect target of
  `docs.lvgl.io/8.3/overview/timer.html`); "non-preemptive" quote, `lv_async_call` timing/allocation
  description.
- https://lvgl.io/docs/open/8.3/porting/indev — LVGL 8.3 indev porting docs (redirect target of
  `docs.lvgl.io/8.3/porting/indev.html`); `read_cb` contract, `continue_reading` semantics.
- https://lvgl.io/docs/open/8.3/porting/display — LVGL 8.3 display porting docs; blocking-vs-DMA
  `flush_cb` guidance, `lv_disp_flush_ready` call-site requirement.
- https://lvgl.io/docs/open/8.3/overview/animation — LVGL 8.3 animation docs; general animation
  API description (did not yield v8.3-specific detail beyond what source-reading confirmed).
- ESP32 forum threads on Arduino-ESP32 `loopTask`/WiFi task core pinning and priority (general
  community reference, `UNVERIFIED` against this repo's exact toolchain version) —
  https://esp32.com/viewtopic.php?t=1161, https://esp32.com/viewtopic.php?t=18446.
- GT911 INT-pin interrupt behavior (general reference, not from a source verified against the driver
  used in this repo) — https://github.com/espressif/esp-bsp/issues/351,
  https://www.kadidisplay.com/blog-news/how-to-connect-a-gt911-capacitive-touch-panel-to-lvgl-on-esp32-s3/.
