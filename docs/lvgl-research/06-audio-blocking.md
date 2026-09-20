# Track 6 — ESP32-audioI2S: why playback blocks the whole device, and how to fix it

See `00-CONTEXT.md` for shared hardware/software facts; not repeated here.

Vendored library: `lib/ESP32-audioI2S`, `library.json`/`library.properties` report
`schreibfaul1/ESP32-audioI2S` **version 2.1.0**, repository URL rewritten to the
`esphome/ESP32-audioI2S` fork (a stale mirror of schreibfaul1's tree, not actively diverging).
This is the **legacy** API generation: `#include <driver/i2s.h>`, `i2s_write()`,
`i2s_driver_install()`/`i2s_driver_uninstall()` — not the new `i2s_std`/`i2s_channel_write()`
driver used by ESP32-audioI2S v3.x on top of ESP-IDF 5.x. Do not apply v3.x-era advice
(`i2s_channel_write`, `AudioOutputI2S`-style APIs) to this vendored copy without checking it still
applies to `driver/i2s.h`-era code. `platformio.ini` builds this against
`espressif32@^6.13.0` (Arduino core 2.x, confirmed via installed toolchain:
`CONFIG_FREERTOS_HZ=1000`, `configMAX_PRIORITIES=25`) — **not** `6.6.0` as stated in
`00-CONTEXT.md`; see Contradictions below.

## TL;DR for the coding agent

1. The confirmed, reachable, fully-synchronous freeze path is `play_cw()` / `play_cw_start()` /
   `playTone()` in `src/esp32/esp32_audio.cpp` — called directly (no task, no yield except inside
   a single `i2s_write` call) from `msg_focus_and_alert()` (incoming-message path, on `loopTask`)
   and from `startAudio()` at boot, whenever the primary SD playback fails to start. Fix: never
   call `play_cw`/`playTone` synchronously from a caller that also owns `lv_task_handler()`; route
   it through the existing `play_function` task instead.
2. `xTaskCreatePinnedToCore(play_function, ..., 50, &xHandle, 1)` in `play_file_from_sd()`
   (`esp32_audio.cpp:131`) requests **priority 50**, but `configMAX_PRIORITIES` on this build is
   **25** (valid range 0–24). FreeRTOS **silently clamps** out-of-range priorities to
   `configMAX_PRIORITIES - 1` = **24** — no assert, no error (`xTaskCreate` does not `configASSERT`
   on this; `vTaskPrioritySet` does). Priority 24 is at or above every ESP-IDF system task
   (WiFi/BT controller ~23, `esp_timer` dispatch ~22, event loop ~20, lwIP ~18) and is 23 levels
   above `loopTask` (priority 1, the task that calls `lv_task_handler()`). Fix: pass an explicit,
   in-range priority — 2–4 is enough headroom above `loopTask` without contending with the network
   stack.
3. `sendBytes()` → `playChunk()` → `playSample()` calls `i2s_write()` **once per stereo sample**
   (4 bytes), not once per decoded frame/buffer. This is a confirmed upstream-reported CPU cost
   (schreibfaul1/ESP32-audioI2S#754): ~65% of one core at 320 kbps/48 kHz stereo MP3 for
   single-sample writes vs ~32% for 16-sample-batched writes. Budget for this; do not assume
   `audio.loop()` is cheap.
4. The audio task's SD reads are **not** protected by the app's `xSemaphore` (the binary semaphore
   that `disp_flush()` in `src/t-deck/tdeck_main.cpp` takes around every TFT SPI transaction).
   Neither `setupSD()`, nor `play_file_from_sd()`'s `SD.exists()` check, nor — critically — the
   vendored `Audio.cpp`'s `audiofile.read()` inside `processLocalFile()` (called from every
   `audio.loop()` while playing a local file) take it. ESP-IDF's own SD-over-SPI host driver
   assumes it owns the bus exclusively (espressif/esp-idf#1597, #6510) — concurrent, unlocked TFT
   and SD transactions on the same physical SPI peripheral can corrupt either side, not just be
   slow. Fix: wrap every `audio.loop()` call in the audio task with
   `xSemaphoreTake(xSemaphore, ...)` / `Give` (coarse but correct), or patch the two
   `audiofile.read()` call sites in vendored `Audio.cpp::processLocalFile()` directly (finer, but
   touches vendored code).
5. `audio_set_mute(true)` (`esp32_audio.cpp:543`, invoked only from LVGL button/menu callbacks
   running on `loopTask`) calls `audio.stopSong()` then `i2s_driver_uninstall(i2s_num)` with **zero
   synchronization** against the audio task, which may be mid-`audio.loop()`/mid-`i2s_write()` in a
   different, higher-priority task at that exact instant. This is a genuine cross-task race on
   shared `Audio` object state and the I2S driver handle, not merely "teardown running in the wrong
   task" as originally suspected — see Contradictions.
6. DMA buffering is already at the ESP-IDF legacy `driver/i2s.h` maximum:
   `dma_buf_count = 8`, `dma_buf_len = 1024` (both hard driver ceilings), set in the `Audio`
   constructor and duplicated in `audio_set_mute()`'s re-install path. There is no config knob left
   to grow buffering tolerance; ~8192 stereo frames ≈ 185 ms at 44.1 kHz, ~512 ms at the
   library's default 16 kHz init rate. The fix has to be architectural (task/priority/locking),
   not buffer-size tuning.
7. This library version has **no play-from-memory-buffer API** — only `connecttohost()`
   (network), `connecttospeech()` (TTS), and `connecttoFS(fs::FS&, path)` /
   `connecttoSD(path)` (any `fs::FS`, so SPIFFS/LittleFS work, not just `SD`). For notification
   beeps, move the source file off the SD/TFT-shared SPI bus entirely: store it on an internal
   LittleFS/SPIFFS partition (ESP32-S3 internal flash is on a separate SPI controller from the
   external TFT+SD bus) and call `audio.connecttoFS(LittleFS, "/beep.wav")`.
8. WAV playback in this library is decode-free: `sendBytes()`'s `CODEC_WAV` branch does a plain
   `memmove` into `m_outBuff`, no Helix/AAC/FLAC decode call. Prefer uncompressed WAV for short
   notification sounds — cuts CPU cost to ~0 beyond the per-sample `i2s_write` overhead in (3).
9. `play_file_from_sd_blocking()` (the literal `while (audio.isRunning()) { audio.loop(); }` with
   no yield at all, described in `00-CONTEXT.md`) is **defined but never called anywhere in this
   repo** — confirmed by `grep -rn play_file_from_sd_blocking src/`. It is not the mechanism behind
   the reported freeze. Do not spend a fix cycle on it beyond deleting it or leaving it alone; if
   kept, add `vTaskDelay(1)` and treat it as the wrong tool for the reachable call sites.
10. Never call `i2s_driver_uninstall()`/`i2s_driver_install()` from a task other than the one that
    owns the running playback, without first guaranteeing the audio task is not inside
    `audio.loop()`. Prefer muting via the PA/amplifier enable GPIO (if present on T-Deck Plus) or
    `audio.setVolume(0)` over tearing down the I2S driver at runtime; if the driver must be torn
    down to save power, do it only when `audio.isRunning() == false` and after the audio task has
    actually suspended (poll `eTaskGetState(xHandle) == eSuspended`, not just call `stopSong()` and
    assume it took effect synchronously).

## Findings

### F1 — The reachable freeze path is the CW/tone fallback, not the documented blocking loop

**Claim**: `play_cw()`, `play_cw_start()`, and the `playTone()` they call
(`src/esp32/esp32_audio.cpp:242-276, 281-465, 470-504`) run fully synchronously in the caller's
task, with no dedicated task and no yield other than the blocking wait inside a single
`i2s_write(..., 100 ticks)` call per millisecond of tone. `play_cw('r')` alone (three Morse symbols)
blocks for `DOT+SYMBOL_PAUSE+DASH+SYMBOL_PAUSE+DOT+SYMBOL_PAUSE+LETTER_PAUSE`
= `100+100+300+100+100+100+300` = **1100 ms**, entirely inside the calling task, with
`lv_task_handler()` never invoked.

**Why**: `msg_focus_and_alert(bWithAudio)` in `src/t-deck/lv_obj_functions.cpp:3985-4009` is the
alert path invoked from `tdeck_add_APRS_message`/`tdeck_add_MSG` (both at
`src/t-deck/lv_obj_functions.cpp:4226`, `:4267`) whenever a new message arrives and is not being
bulk-loaded from a file. Those add-message functions run from the packet-receive path inside the
Arduino `loop()` (`src/esp32/esp32_main.cpp`), i.e. on **`loopTask`** — the same task that calls
`lv_task_handler()` at `esp32_main.cpp:3847`. Inside `msg_focus_and_alert`, if
`play_file_from_sd(meshcom_settings.node_audio_msg.c_str(), 12)` returns `false` — which happens
whenever `!bSDDected` or the configured audio file does not exist on the SD card (both realistic:
SD not inserted, file renamed/missing/corrupted) — the code falls back to `play_cw('r')`
synchronously, on `loopTask`. `startAudio()` (`tdeck_main.cpp:209-219`, called once at boot from
`esp32_main.cpp` near line 1733, inside BLE setup) has the identical fallback pattern via
`play_cw_start()`.

Notably, a comment in `lv_obj_functions.cpp` right above the audio call (`// Force a task handler
run to update the UI before playing audio` / `//lv_task_handler(); Y5 check`) shows a developer
already identified "UI must be flushed before the freeze" and disabled the workaround rather than
fixing the root cause.

**Symptom if violated**: Every time the configured notification MP3 is missing or the SD card is
absent, incoming-message alerts freeze the whole UI (no redraw, no touch response) for roughly
1-2 seconds per alert — this reproduces the "playing audio blocks the whole device" symptom
directly and is entirely independent of the task-based audio player's priority bugs (Findings
F2-F4 below).

**Fix**: Never call `play_cw`/`play_cw_start`/`playTone` from a task that also owns
`lv_task_handler()`. Route the CW fallback through the same `play_function`-style task dispatch
used for file playback (e.g., queue a "play CW `<char>`" request that a dedicated low-priority
task consumes with `playTone`'s `i2s_write` calls, or reuse `xTaskCreatePinnedToCore` with the
CW sequence as the task body). Minimal patch:

```cpp
// esp32_audio.cpp — add a small task wrapper, mirroring play_function()
struct CwRequest { char ch; int volume; };
static QueueHandle_t cwQueue = nullptr;

void cw_task(void *parameter) {
    CwRequest req;
    for (;;) {
        if (xQueueReceive(cwQueue, &req, portMAX_DELAY) == pdTRUE) {
            play_cw(req.ch, req.volume);   // still uses the blocking playTone() internally,
        }                                   // but now off loopTask
    }
}

// call once from init_audio():
// cwQueue = xQueueCreate(4, sizeof(CwRequest));
// xTaskCreatePinnedToCore(cw_task, "cw task", 4096, NULL, 2, NULL, 1);

// replace direct play_cw('r') calls with:
// CwRequest req{'r', 20}; xQueueSend(cwQueue, &req, 0);
```

**Source**: read directly — `src/esp32/esp32_audio.cpp` (playTone/play_cw/play_cw_start),
`src/t-deck/lv_obj_functions.cpp:3985-4009,4200-4267`, `src/t-deck/tdeck_main.cpp:209-219`,
`src/esp32/esp32_main.cpp:1733,3847`.

### F2 — Audio task priority request (50) exceeds `configMAX_PRIORITIES` (25) and is silently clamped to 24

**Claim**: `xTaskCreatePinnedToCore(play_function, "audio play task", 16*1024, NULL, 50, &xHandle,
1)` (`esp32_audio.cpp:131-139`) requests priority 50. On this build,
`configMAX_PRIORITIES = 25` (confirmed:
`~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/*/include/
freertos/include/esp_additions/freertos/FreeRTOSConfig.h:81`), so the valid range is 0–24.
FreeRTOS's `xTaskCreate`/`xTaskCreatePinnedToCore` does not `configASSERT` on an out-of-range
priority (only `vTaskPrioritySet` does); it silently clamps to `configMAX_PRIORITIES - 1` = **24**.

**Why**: `loopTask` (Arduino's `setup()`/`loop()` task, which calls `lv_task_handler()`) is created
with priority **1** (`framework-arduinoespressif32/cores/esp32/main.cpp:71`:
`xTaskCreateUniversal(loopTask, "loopTask", ..., NULL, 1, &loopTaskHandle,
ARDUINO_RUNNING_CORE)`), and `ARDUINO_RUNNING_CORE` is core 1 by default — the **same core** the
audio task is pinned to. ESP-IDF's own system tasks top out well below the clamp ceiling:
`ESP_TASK_BT_CONTROLLER_PRIO = configMAX_PRIORITIES-2 = 23`,
`ESP_TASK_TIMER_PRIO (esp_timer dispatch) = configMAX_PRIORITIES-3 = 22`,
`ESP_TASKD_EVENT_PRIO (event loop) = configMAX_PRIORITIES-5 = 20`,
`ESP_TASK_TCPIP_PRIO (lwIP) = configMAX_PRIORITIES-7 = 18`
(`esp_system/include/esp_task.h`). At the clamped priority 24, the audio task outranks every one
of those, and is 23 levels above `loopTask`. A ready task at priority 24 preempts a priority-1 task
the instant it becomes ready — there is no scheduling slack left for `loopTask` to make progress
while the audio task has anything to do.

**Symptom if violated**: Even with the `play_function` task (the "good", non-blocking dispatch
path) doing the right thing structurally (own task, `vTaskDelay(1)` between iterations), the sheer
priority gap means every ~1 ms tick the audio task preempts `loopTask` mid-flush/mid-layout and
runs its full "read one InBuff block + decode one frame + drain `m_validSamples` via per-sample
`i2s_write`" burst to completion before yielding again. This produces a UI that is not technically
deadlocked but is starved into what looks and feels like a freeze for the duration of playback —
matches the reported symptom for the task-based (SD file, not CW) playback path too, distinct from
F1's synchronous CW path.

**Fix**: Pass an explicit, sane priority. `loopTask` = 1; give the audio task just enough headroom
to preempt `loopTask` for short bursts but stay below anything network/BLE related:

```cpp
xTaskCreatePinnedToCore(
    play_function,
    "audio play task",
    16 * 1024,
    NULL,
    3,          // was 50 (silently clamped to 24) — pick just above loopTask (1)
    &xHandle,
    1
);
```

Verify no other task in this firmware assumes it's safe above priority ~10 before picking the
exact number; grep `src/` for other `xTaskCreate*` calls to check for collisions.

**Source**:
`~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/*/include/
freertos/include/esp_additions/freertos/FreeRTOSConfig.h` (configMAX_PRIORITIES=25,
CONFIG_FREERTOS_HZ=1000);
`~/.platformio/packages/framework-arduinoespressif32/cores/esp32/main.cpp` (loopTask priority 1);
`~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/include/
esp_system/include/esp_task.h` (system task priorities); FreeRTOS forum confirming the silent
clamp: https://forums.freertos.org/t/assert-on-priority/12061 ("if you set a way too high prio, it
will be reduced to the highest allowed in the system" — `vTaskPrioritySet` asserts,
`xTaskCreate` did not at time of posting); FreeRTOS Kernel Book PR discussing the same clamp:
https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/pull/73.diff.

### F3 — `playChunk()`/`playSample()` call `i2s_write()` once per stereo sample, not per buffer

**Claim**: `Audio::playChunk()` (`Audio.cpp:2138-2219`) loops over every decoded sample calling
`playSample()`, and `Audio::playSample()` (`Audio.cpp:4134-4166`) issues one
`i2s_write((i2s_port_t)m_i2s_num, (const char*)&s32, sizeof(uint32_t), &m_i2s_bytesWritten, 1000)`
per sample — a single 4-byte (16-bit stereo) write per call, with a 1000-tick (1000 ms at this
build's `CONFIG_FREERTOS_HZ=1000`) timeout. For an MP3 frame of 1152 samples this is 1152 separate
kernel-level `i2s_write` calls per `audio.loop()` invocation that reaches `sendBytes()`.

**Why**: Each `i2s_write()` call on the legacy `driver/i2s.h` implementation does internal
bookkeeping (queue/semaphore operations) per call; this is not free even when it doesn't block.
schreibfaul1/ESP32-audioI2S issue #754 quantifies this directly: sending samples one at a time
measured **~65% CPU load on one core** for a 320 kbps/48 kHz stereo MP3, vs **~32%** when 16 frames
are batched into one write — the reporter attributes the difference to "creating a semaphore in
IDF-lib for each call to i2s_write." (Note: that issue was filed against a later library version
using `i2s_channel_write`, but the vendored copy here has the identical one-`i2s_write`-per-sample
structure in `playSample()`, so the same order-of-magnitude overhead applies.)

**Symptom if violated**: Elevated CPU consumption purely from I2S dispatch overhead, on top of
actual decode cost — this compounds Finding F2: the audio task, already running far above
`loopTask`'s priority, burns extra cycles per sample it does not need to, lengthening every burst
during which `loopTask` cannot run.

**Fix**: Two options, in order of invasiveness:

1. **Don't touch vendored code**: accept the overhead, but make sure F2's priority fix and F4's
   locking fix are in place so the extra CPU time doesn't translate into UI starvation.
2. **Patch vendored `Audio.cpp`** (if in scope): batch `playSample()` calls — accumulate N samples
   into a local buffer and issue one `i2s_write()` per N samples instead of per sample. This is a
   surgical, well-precedented change (matches the fix the upstream reporter already prototyped) but
   does modify `lib/ESP32-audioI2S/src/Audio.cpp`, which conflicts with this project's "minimal
   changes only" policy for anything destined for the upstream MeshCom PR — treat this as
   fork-local, not something to send upstream, if applied.

**Source**: `lib/ESP32-audioI2S/src/Audio.cpp:2138-2219` (playChunk), `:4134-4166` (playSample);
https://github.com/schreibfaul1/ESP32-audioI2S/issues/754 ("sending samples 1 by 1 consumes lots of
cpu and smaller bugs - improvement included" — 65% vs 32% CPU, closed "not planned" upstream).

### F4 — Audio task's SD reads bypass the app's TFT/SD SPI-bus semaphore entirely

**Claim**: `xSemaphore` (a `xSemaphoreCreateBinary()` in `src/t-deck/tdeck_main.cpp:116-118`) is
taken in exactly two places in the T-Deck source: `disp_flush()`
(`tdeck_main.cpp:463`, `portMAX_DELAY`) and the debug screen-readback function
(`tdeck_debug.cpp:311`, which explicitly drives `TDECK_SDCARD_CS` and `LORA_CS` HIGH first,
commented "Keep other SPI slaves off the shared MISO line, as the wake path does" — i.e. the
developers already know concurrent SPI slave activity corrupts reads on this bus). It is
**never** taken around SD access: not in `setupSD()`, not around `SD.exists()` in
`play_file_from_sd()` (`esp32_audio.cpp:106`), and — because the vendored `Audio.cpp` has zero
knowledge of any app-defined semaphore — not around `audiofile.read()` inside
`Audio::processLocalFile()` (`Audio.cpp:2869`), which runs on **every** `audio.loop()` call while
playing a local file, from inside the audio task.

**Why**: The T-Deck Plus wires the SD card and the TFT onto the same physical SPI bus/pins
(`00-CONTEXT.md`, confirmed by the CS-toggling code in `tdeck_debug.cpp`). ESP-IDF's own SD-over-SPI
host driver documents that it assumes exclusive ownership of the bus — sharing it with another SPI
device requires the application to serialize access itself; this is a widely reported class of bug,
not specific to this codebase (espressif/esp-idf#1597 "Support SD-SPI bus sharing", #6510 "Sharing
SPI bus between TFT and sdcard results in error"; LVGL forum "Display and SD card have trouble
sharing the SPI bus on ESP32"). Since the audio task's SD reads run entirely outside the app's
`xSemaphore`, they can execute concurrently with a `disp_flush()` SPI transaction on `loopTask`
with no arbitration at all.

**Symptom if violated**: Beyond the priority-driven starvation in F2, this is a correctness bug:
concurrent, unlocked SPI transactions on the shared bus during playback can corrupt TFT writes
(visible display glitches) and/or SD reads (audio glitches, decode errors, or `SD.read()` returning
garbage that `read_MP3_Header`/`read_WAV_Header` then rejects, tripping the "no syncword found,
try next chunk" retry path in `Audio::sendBytes()` — itself extra CPU work). Depending on how far
TFT_eSPI on this board bypasses the ESP-IDF SPI driver's own device-level locking (register-level
fast paths are common in TFT_eSPI for ESP32), corruption or a wedged bus is possible, not just a
performance hit.

**Fix (app-level, no vendored-code changes — recommended)**: wrap the entire `audio.loop()` call
in `play_function()` with the app's `xSemaphore`. Coarser than ideal (holds the TFT/SD lock for the
full decode+write burst of one loop() iteration, so combine with F2's priority fix and F3's
awareness of per-sample write cost to keep that burst short), but correct and minimal:

```cpp
// esp32_audio.cpp, play_function()
while (audio.isRunning()) {
    if (meshcom_settings.node_mute) break;
    if (xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(50)) == pdTRUE) {
        audio.loop();
        xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(1);
}
```

Also guard the `SD.exists()` check in `play_file_from_sd()` the same way. `xSemaphore` is declared
`extern` already in `tdeck_debug.cpp`; add the same `extern SemaphoreHandle_t xSemaphore;` to
`esp32_audio.cpp` (it currently only references its own, unrelated `audioSemaphore`).

**Fix (vendored-code, finer-grained, fork-local only)**: patch the two `audiofile.read()` sites in
`Audio::processLocalFile()` (`Audio.cpp:2869` and the EOF-drain path at `:2936`) to take/give the
app's semaphore only around the actual `read()` call, not around decode/I2S-write — requires
exposing `xSemaphore` to the vendored library (e.g. a weak `extern` or a callback hook), and is a
bigger footprint change to `lib/ESP32-audioI2S`.

**Source**: read directly — `src/t-deck/tdeck_main.cpp:51,116-118,458-478`,
`src/t-deck/tdeck_debug.cpp:295-335`, `src/esp32/esp32_audio.cpp:94-161`,
`lib/ESP32-audioI2S/src/Audio.cpp:2829-2977` (processLocalFile);
https://github.com/espressif/esp-idf/issues/1597;
https://github.com/espressif/esp-idf/issues/6510;
https://forum.lvgl.io/t/display-and-sd-card-have-trouble-sharing-the-spi-bus-on-esp32/15312.

### F5 — `audio_set_mute()` tears down the I2S driver with no synchronization against the audio task

**Claim**: `audio_set_mute(bool mute)` (`esp32_audio.cpp:543-579`) is called only from LVGL
event-callback code — `src/t-deck/event_functions.cpp:328` (a sound-on/off button) and
`src/t-deck/tdeck_main.cpp:689` (a settings toggle) — both of which execute inside
`lv_task_handler()`, i.e. on `loopTask`. When muting, it calls `audio.stopSong()` (only if
`audio.isRunning()`) and then unconditionally `i2s_driver_uninstall(i2s_num)`. Neither call
synchronizes with the audio task (`play_function`, running on core 1 at whatever priority F2
resolves to), which may at that exact moment be inside `audio.loop()` — possibly mid-`i2s_write()`
— manipulating the same `Audio` object's internal state (`m_f_running`, `InBuff` read/write
pointers, `m_i2s_num`) and the very I2S driver handle being uninstalled.

**Why**: `audioSemaphore` in this file only guards the _entry point_ of `play_file_from_sd()`
(deciding whether to start a new play or resume a suspended task) — it is never held across the
lifetime of playback, so it provides no protection here. `Audio::stopSong()` mutates shared state
with no lock; `i2s_driver_uninstall()` frees the driver's internal DMA descriptor memory and ISR
resources while another task may still reference them via an in-flight `i2s_write()`.

**Symptom if violated**: Toggling mute while a sound is playing races the audio task; possible
outcomes include a crash (use-after-free on I2S driver internals), a hang (audio task stuck
inside a call into a half-torn-down driver), or at minimum audible glitches. This corrects the
`00-CONTEXT.md` framing that "the I2S driver is torn down... from inside the audio task" — the
current code does not do that (`audio_set_mute` runs on `loopTask`, not the audio task) — the real
issue is a **cross-task** race, not a same-task ordering bug, which is arguably worse because there
is no lock at all between the two tasks, not even a coarse one.

**Fix**: Do not call `i2s_driver_uninstall()` opportunistically from `loopTask`. Either:

- signal the audio task to stop and wait for it to actually reach a safe state
  (`eTaskGetState(xHandle) == eSuspended` or `eBlocked` after a bounded poll) before uninstalling,
  or
- avoid driver teardown for muting altogether: call `audio.setVolume(0)` (cheap, already
  thread-adjacent since only ever called from the entry-guarded `play_file_from_sd`/task-create
  path) or drive a hardware amplifier-enable/shutdown GPIO if the T-Deck Plus audio path has one,
  and reserve `i2s_driver_uninstall()` for an explicit, playback-confirmed-stopped power-down path
  (e.g. on screen sleep), not a live mid-song mute toggle.

```cpp
void audio_set_mute(bool mute) {
    meshcom_settings.node_mute = mute;
    if (mute) {
        audio.setVolume(0);                 // immediate, safe from any task
        if (xHandle != NULL) {
            // wait briefly for play_function to notice node_mute and exit its loop
            for (int i = 0; i < 50 && eTaskGetState(xHandle) == eRunning; i++) {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        // only now is it safe to i2s_driver_uninstall(), if power-saving requires it
    } else {
        audio.setVolume(previous_volume);
        // ... re-install only if actually uninstalled
    }
}
```

**Source**: read directly — `src/esp32/esp32_audio.cpp:509-579`,
`src/t-deck/event_functions.cpp:328`, `src/t-deck/tdeck_main.cpp:689`.

### F6 — DMA buffering is already at the legacy I2S driver's maximum; no headroom to add via config

**Claim**: `m_i2s_config.dma_buf_count = 8; m_i2s_config.dma_buf_len = 1024;` is set in the `Audio`
constructor (`Audio.cpp:179-180`, comment "max buffers" / "max value") and duplicated identically
in `audio_set_mute()`'s re-install path (`esp32_audio.cpp:570-571`). These are the hard ceilings of
the legacy `driver/i2s.h` (`dma_buf_count` ≤ 128 is the doc'd cap but `dma_buf_len * dma_buf_count`
≤ 4092×... practically the library authors picked the values they consider the practical max for
this driver generation). 8×1024 = 8192 stereo samples buffered ≈ 185 ms at 44.1 kHz, ≈ 512 ms at
the library's default 16 kHz init rate.

**Why this matters for the fix, not as a lever**: because the buffer is already maximized, the
tolerance for a _delayed_ `audio.loop()` call is already as generous as this driver generation
allows — on the order of hundreds of ms. If starvation (F2) is bad enough to exceed that window,
the DMA underruns and playback audibly glitches/restarts; you cannot buy more slack by raising
`dma_buf_count`/`dma_buf_len` further, only by fixing the scheduling (F1/F2) or reducing the reader
side's own latency (F3/F4).

**Fix**: none needed at the config layer; do not spend effort trying to "increase DMA buffering" —
it is a dead end on this driver. Put the effort into F1/F2/F4 instead.

**Source**: `lib/ESP32-audioI2S/src/Audio.cpp:175-221`; `src/esp32/esp32_audio.cpp:559-577`.

### F7 — No memory-buffer playback API; WAV avoids decode cost; use internal flash for notification sounds

**Claim**: `Audio.h`'s public surface (`lib/ESP32-audioI2S/src/Audio.h:162-181`) exposes
`connecttohost()` (network stream), `connecttospeech()` (TTS synthesis, needs a `lang`/`speech`
string, not audio data), `connecttoFS(fs::FS &fs, const char *path, uint32_t resumeFilePos = 0)`,
and `connecttoSD(path)` (a thin wrapper: `return connecttoFS(SD, path, resumeFilePos);`,
`Audio.cpp:629-631`). There is **no** "play from a `uint8_t*`/PROGMEM buffer" entry point in this
library version — unlike ESP8266Audio's `AudioFileSourcePROGMEM`. `connecttoFS` takes any
`fs::FS&`, so it is not actually tied to the SD card — `LittleFS`/`SPIFFS` (backed by internal
flash, on a SPI controller separate from the shared external TFT+SD bus) work identically:
`audio.connecttoFS(LittleFS, "/beep.wav")`.

Separately, `sendBytes()`'s `CODEC_WAV` branch (`Audio.cpp:3725-3730`) does a plain `memmove(m_outBuff,
data, len)` with sample-count math — no Helix MP3/AAC/FLAC decoder call at all for WAV. AAC/MP3/FLAC
all invoke a real software decoder (`MP3Decode`/`AACDecode`/`FLACDecode`) per frame.

**Why**: For short, frequent notification sounds (message-arrived beep, boot chime), the goal is
minimum CPU and zero contention with the SD/TFT bus, not compression ratio — flash space for a
handful of short WAV files is cheap (16 MB flash on this board), and skipping both SD-bus
contention (F4) and decode CPU (part of F3's overhead budget) removes two of the mechanisms behind
the freeze at once, independent of fixing the scheduling bugs.

**Symptom if violated**: keeping notification sounds as compressed files on the SD card means every
notification beep pays the full SD-bus-contention (F4) and decode-CPU (F3) cost this document
describes, even after F1/F2/F5 are fixed.

**Fix**: mount a small LittleFS/SPIFFS partition, copy the notification WAV(s) onto it at
build/provisioning time (or write them once from the SD card on first boot as a migration step),
and switch `meshcom_settings.node_audio_start`/`node_audio_msg` playback to
`audio.connecttoFS(LittleFS, path)` instead of `audio.connecttoFS(SD, path)`
(`play_file_from_sd()` currently hardcodes `SD` at `esp32_audio.cpp:109`). Keep SD-backed
`connecttoFS(SD, ...)` only for larger, occasional content where SD's capacity actually matters.

**UNVERIFIED**: whether this firmware's PlatformIO build for `t_deck_plus` already declares a
LittleFS/SPIFFS partition in its partition table (`board_build.partitions`) — check
`platformio.ini`'s `t_deck_plus`/`t_deck_pro` sections and whatever `.csv` they reference before
assuming a spare internal-flash filesystem is available without a partition-table change.

**Source**: `lib/ESP32-audioI2S/src/Audio.h:162-181`; `lib/ESP32-audioI2S/src/Audio.cpp:629-631,
3711-3806` (sendBytes, CODEC_WAV branch).

## Alternatives — recommendation

Stay on **ESP32-audioI2S** (do not migrate to ESP8266Audio, ESP-IDF ADF, or a custom minimal I2S
WAV player). Reasoning, given this project's own "cherry-pick the absolute minimum, do not rewrite
large parts" policy (`CLAUDE.md`):

- The library already supports everything the T-Deck feature set needs (MP3/AAC/FLAC/WAV,
  `connecttoFS` against arbitrary `fs::FS`), and the app already has the right _shape_ of fix in
  place — a dedicated pinned task (`play_function`) doing `audio.loop(); vTaskDelay(1);` — it is
  simply misconfigured (F2: priority) and missing one lock (F4). These are small, targeted patches
  to `src/esp32/esp32_audio.cpp`, not a library swap.
- A custom minimal I2S WAV player (extending the existing `playTone()` raw-`i2s_write` pattern)
  is worth adding **in addition**, specifically for short notification beeps/CW (F1's fix and F7's
  recommendation both point this direction) — but as a small addition alongside
  ESP32-audioI2S for file-based MP3/AAC playback, not a replacement for it.
- ESP-IDF ADF (`esp_audio`) pulls in a much larger dependency surface and a different threading
  model than this codebase already uses elsewhere; disproportionate for "short beeps + occasional
  file playback."

## Rules to hand the coding agent

1. Never call `play_cw`, `play_cw_start`, or `playTone` from any code path that also calls
   `lv_task_handler()` in the same task (currently `msg_focus_and_alert`'s fallback and
   `startAudio()`'s fallback both do). Dispatch them via a task/queue instead (F1).
2. Fix the `xTaskCreatePinnedToCore(play_function, ..., 50, ...)` priority argument to an in-range,
   deliberately chosen value (2-4), never a number picked without checking
   `configMAX_PRIORITIES` for the target build (F2).
3. Wrap every `audio.loop()` call in `play_function()`, and the `SD.exists()` check in
   `play_file_from_sd()`, in `xSemaphoreTake(xSemaphore, ...)`/`Give` — the same semaphore
   `disp_flush()` uses — so SD and TFT SPI transactions are never concurrent (F4).
4. Do not call `i2s_driver_uninstall()` from `audio_set_mute()` without first confirming (poll
   `eTaskGetState`) that the audio task is not mid-`audio.loop()`; prefer `audio.setVolume(0)` or a
   hardware mute line for the interactive mute toggle (F5).
5. Do not attempt to fix this by raising `dma_buf_count`/`dma_buf_len` — they are already at the
   library's chosen maximum for the legacy I2S driver (F6).
6. Move notification-sound source files off the SD card onto an internal LittleFS/SPIFFS partition,
   and prefer WAV over MP3/AAC for them, before or alongside the scheduling fixes — this removes
   two of the freeze mechanisms (SD-bus contention, decode CPU) regardless of how the task
   scheduling fixes land (F7).
7. Treat `play_file_from_sd_blocking()` as dead code — grep confirmed zero callers. Either delete it
   or leave it untouched; do not spend a fix cycle "fixing" its missing yield unless a new caller is
   introduced (F1/Contradictions).
8. If patching `lib/ESP32-audioI2S/src/Audio.cpp` directly (e.g. F3's per-sample batching, or F4's
   fine-grained lock) is considered, flag it explicitly as a vendored-library change that is
   fork-local only and out of scope for an upstream MeshCom PR, per this project's minimal-change
   policy — the app-level fixes (F1, F2, F4's coarse variant, F5) achieve the goal without touching
   `lib/`.
9. Before relying on an internal LittleFS/SPIFFS partition for F7, verify the `t_deck_plus`
   partition table actually reserves space for one (UNVERIFIED here).

## Open questions / UNVERIFIED

- Whether `xTaskCreate`'s silent-clamp-without-assert behavior (vs. `vTaskPrioritySet`'s assert) is
  literally true for the exact FreeRTOS kernel version vendored in `espressif32@6.13.0`'s
  `framework-arduinoespressif32` — confirmed via the FreeRTOS forum thread for FreeRTOS in general
  and is consistent with the observed absence of a boot-time assertion failure in this firmware
  (which would otherwise be an obvious, already-reported crash), but the exact `tasks.c` source for
  this specific prebuilt `libfreertos.a` was not available to grep directly (ESP-IDF ships it
  precompiled for the Arduino core).
- Whether TFT_eSPI on this board (`lib/TFT_eSPI`) uses the Arduino `SPIClass`/ESP-IDF
  `spi_master` driver path (which has its own internal per-bus lock between distinct
  `spi_device_handle_t`s) or a register-level fast path that bypasses it — this affects how bad F4's
  unlocked concurrent access actually is in practice (silent corruption vs. occasional glitch vs.
  driver-level serialization saving it most of the time). Not read in this track; Track 1-5 or a
  dedicated TFT_eSPI track may already cover this.
- Whether the `t_deck_plus` PlatformIO env's partition table has spare space for a LittleFS/SPIFFS
  partition for F7's recommendation (grep `platformio.ini` / the referenced `.csv` before assuming).
- Exact real-world CPU-percentage cost of MP3/AAC/FLAC decode alone (isolated from the per-sample
  `i2s_write` overhead in F3) on this specific ESP32-S3 at 240 MHz was not independently measured
  here; only the combined "single-sample vs batched write" figure from upstream issue #754 was
  found and cited. Track 7 (observability) should be the one to add a per-`audio.loop()` timing
  histogram if a precise split is needed.

## Contradictions with `00-CONTEXT.md`

- **`platformio.ini` platform version**: context states `platform = espressif32 @ 6.6.0`; the
  actual `[esp32]` base env (which `t_deck`/`t_deck_plus`/`t_deck_pro` extend) uses
  `platform = espressif32@^6.13.0` (`platformio.ini:332`). This does not change any conclusion in
  this document (Arduino core 2.x either way; `CONFIG_FREERTOS_HZ=1000`,
  `configMAX_PRIORITIES=25` confirmed for the installed 6.13.0-era toolchain), but the exact
  version string should be corrected wherever cited.
- **"The caller's `while (audio.isRunning()) { ... audio.loop(); }` with no yield at all" as an
  active freeze mechanism**: this function (`play_file_from_sd_blocking`) exists exactly as
  described but is **never called** anywhere in the repository (`grep -rn
play_file_from_sd_blocking src/` returns only its own definition and declaration). The actual
  reachable fully-synchronous blocking path is the CW/tone fallback (F1), which the shared context
  did not call out specifically.
- **"The I2S driver is torn down and re-installed at runtime in the mute path" / "I2S teardown
  happening under the audio task itself"**: confirmed the teardown exists
  (`audio_set_mute()` → `i2s_driver_uninstall()`), but it runs on **`loopTask`** (via LVGL button
  callbacks), not inside the audio task. The real hazard is a **cross-task** race with no lock at
  all (F5), which is a different — and arguably more serious, since there is zero synchronization
  rather than merely wrong-task ordering — failure mode than "torn down under the audio task
  itself" implies.
- **"The audio semaphore guarding only setup and not playback"**: confirmed exactly as stated for
  `audioSemaphore` (guards only `play_file_from_sd()`'s task-creation decision). Separately and
  additionally, the _unrelated_ `xSemaphore` (TFT/SD SPI-bus lock) is not taken by the audio path
  at all, which is the more consequential gap (F4) — worth distinguishing the two semaphores
  clearly in the merged document since they are easy to conflate by name similarity.

## Sources

- `lib/ESP32-audioI2S/src/Audio.cpp`, `Audio.h`, `library.json`, `library.properties` — vendored
  library source and version metadata (repo).
- `src/esp32/esp32_audio.cpp`, `esp32_audio.h` — app-level audio dispatch, task creation, mute path
  (repo).
- `src/t-deck/tdeck_main.cpp`, `lv_obj_functions.cpp`, `tdeck_debug.cpp`, `event_functions.cpp` —
  callers, `xSemaphore` definition/usage, LVGL callback contexts (repo).
- `src/esp32/esp32_main.cpp` — `loop()`/`lv_task_handler()` call site, `startAudio()` invocation
  (repo).
- `platformio.ini` — actual platform version and env structure (repo).
- `~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/*/include/
freertos/include/esp_additions/freertos/FreeRTOSConfig.h` — `configMAX_PRIORITIES=25`,
  `CONFIG_FREERTOS_HZ=1000` (installed toolchain, not a URL — locally inspected).
- `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/main.cpp` — `loopTask` priority
  1, `ARDUINO_RUNNING_CORE` (installed toolchain).
- `~/.platformio/packages/framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/include/
esp_system/include/esp_task.h` — ESP-IDF system task priority constants (installed toolchain).
- https://github.com/schreibfaul1/ESP32-audioI2S/issues/754 — per-sample `i2s_write` CPU cost
  (65% vs 32% CPU for single vs batched writes), closed "not planned" upstream.
- https://forums.freertos.org/t/assert-on-priority/12061 — confirms `xTaskCreate` silently clamps
  an out-of-range priority to `configMAX_PRIORITIES - 1`, unlike `vTaskPrioritySet`.
- https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/pull/73.diff — FreeRTOS Kernel Book text on the
  same priority-clamping behavior.
- https://github.com/espressif/esp-idf/issues/1597 — "Support SD-SPI bus sharing": ESP-IDF's SD
  host driver assumes exclusive bus ownership.
- https://github.com/espressif/esp-idf/issues/6510 — "Sharing SPI bus between TFT and sdcard
  results in error", concurrent access corrupts SD reads without app-level serialization.
- https://forum.lvgl.io/t/display-and-sd-card-have-trouble-sharing-the-spi-bus-on-esp32/15312 —
  practical workaround pattern (wait for SD/TFT CS to be idle before the other device's
  transaction) matching this app's own `xSemaphore` design intent.
