# T-Deck Plus GUI — Fable Verdict (stage 2, 2026-08-28)

Stage 2 of a two-stage review. Stage 1 (`/code-review medium`) is
[`review-tdeck-gui-20260828.md`](review-tdeck-gui-20260828.md) — 16 findings, not repeated here.
Stage 2 ran seven independent finders (heap, concurrency, correctness, performance, bounds,
test-audit, altitude), each blind to the others, followed by verification.

**[V] = independently verified** by the orchestrator against the tree, not taken on the finder's
word. Everything else is a finder claim that survived plausibility review but was **not** re-derived.

Occasion: the upstream maintainer reported an unlocated heap defect he spent two days chasing.

## The allocator asymmetry — read this first

Everything about the heap findings depends on one fact pair, both verified:

- `lv_conf.h:64` sets `LV_MEM_CUSTOM_ALLOC ps_malloc` → **every LVGL object is PSRAM, at any size**. [V]
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`
  (`framework-arduinoespressif32@3.20014.231204/tools/sdk/esp32s3/opi_opi/include/sdkconfig.h:315`)
  → **every `String` / `new` / `std::vector` block ≤ 4 KB lands on the internal heap**. [V]

Budget: ~95 KB internal, ~8.13 MB PSRAM. So a GUI defect can drain internal RAM to `abort()` while
PSRAM telemetry stays flat and healthy. **The discriminating instrument is
`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`, not free-heap** — fragmentation bites
before the sum runs out.

## Finding H1: rendered message list is never trimmed

- **File:** `src/t-deck/lv_obj_functions.cpp:2770` [V]
- **Severity:** HIGH — leading candidate for the maintainer's defect
- **Failure scenario:** `msg_tabs_add_message()` trims the _model_ to 50 entries
  (`msg_tabs_trim_history`, `:2714`), but the fast path for the already-active tab appends to the
  _view_ with no matching deletion:

  ```c
  if (index == msg_active_tab_index) {
      msg_list_append_bubble(bubble);                  // appends, never trims
      lv_obj_scroll_to_view(last, LV_ANIM_ON);
  }
  ```

  `lv_obj_clean(msg_list)` is reachable only from `msg_render_active_tab()` /
  `msg_list_show_hint()`, neither of which runs on this path. Each broadcast arriving on the
  **currently open** group tab permanently adds 9 LVGL objects (~1.9 KB PSRAM) plus
  `new HeaderEventData`, `new DeleteEventData` and four String buffers (~250 B internal).
  At ~390 messages the internal pool is gone → `abort()`, while PSRAM sits at ~740 KB of 8.13 MB.

  **Why it survived two days of hunting: it heals on any group-tab switch.** Clicking through tabs
  to inspect the problem destroys the evidence.

- **Fix:** trim the view on the fast path, mirroring the model trim.

## Finding C1: audio task created above the priority ceiling

- **File:** `src/esp32/esp32_audio.cpp:104` [V]
- **Severity:** HIGH (enabler for C2/C3)
- **Failure scenario:** `xTaskCreatePinnedToCore(play_function, "audio play task", 16*1024, NULL,
50, &xHandle, 1)` requests priority **50**; `configMAX_PRIORITIES` is **25** [V]. FreeRTOS clamps
  silently — the finder disassembled the shipped `libfreertos.a` and found `movi.n a8,24 /
minu a8,a6,a8` with **no `configASSERT`**. The task therefore runs at 24, the highest task
  priority in the system, and preempts `loopTask` at an arbitrary instruction. That is what makes
  C2–C5 reachable at all.
- **Fix:** pick a deliberate priority below the system tasks.

Note the scope lesson: this is in `src/esp32/`, not `src/t-deck/`, so **stage 1's file scope could
not have found it** — yet the task is spawned by every incoming mesh text message
(`loop_functions.cpp:2423` → `lv_obj_functions.cpp:3979`).

## Finding C2: audio semaphore guards setup, not playback

- **File:** `src/esp32/esp32_audio.cpp:115` [V]
- **Severity:** HIGH — second candidate for the maintainer's defect, different pool from G01
- **Failure scenario:** `xSemaphoreGive(audioSemaphore)` runs immediately after task creation,
  **before** playback begins [V], so it protects ~1 ms of setup rather than the song. Two messages
  arriving inside one sound: `loopTask`'s `connecttoFS()` → `setDefaults()` closes `audiofile` and
  calls `MP3Decoder_FreeBuffers()` while the audio task is inside `processLocalFile()` reading into
  exactly those buffers → **use-after-free on the internal heap**.
- **Precondition:** only fires when audio files are actually present on SD.
- **Fix:** hold the semaphore for the lifetime of playback, or refcount the decoder buffers.

## Finding P1: every screen update is a full blocking SPI frame

- **File:** `src/t-deck/tdeck_main.cpp:363`, flush at `:414-428` [V]
- **Severity:** HIGH — this is the baseline answer to "why is the GUI sluggish"
- **Failure scenario:** `disp_drv.full_refresh = 1` is active (`#ifdef BOARD_HAS_PSRAM`, and
  `t_deck_plus` builds with `-DBOARD_HAS_PSRAM=1`) [V], with a **single** draw buffer
  (`lv_disp_draw_buf_init(..., NULL, ...)`) [V] and a **non-DMA** blocking flush
  (`tft.pushColors(...)`) [V]. So _every_ LVGL invalidation pushes the whole framebuffer:
  320 × 240 × 2 B = 153 600 B. At `SPI_FREQUENCY 27000000` (`lib/TFT_eSPI/User_Setup.h:357`) [V]
  that is **~45 ms of pure transfer**, inline in the Arduino loop, holding the SPI semaphore that
  also fronts SD and LoRa.

  The once-per-second clock tick alone therefore costs ~45 ms; so does every keypress and icon
  change. This is a constant tax, distinct from the multi-hundred-ms outliers in stage 1.

  _Correction to the finder:_ it assumed 40 MHz and reported ~31 ms. Actual clock is 27 MHz, so the
  real cost is **higher**, not lower.

- **Fix:** dual partial buffers and/or DMA flush. Measure before and after.

### P2 (MEDIUM-HIGH) multiplies P1

`lv_obj_scroll_to_view(last, LV_ANIM_ON)` at `lv_obj_functions.cpp:2770` [V] and `:2704` —
inconsistent with `LV_ANIM_OFF` used elsewhere in the same file. Each animation tick is a separate
full-screen flush, so one incoming message serialises into ~200-300 ms of blocking transfer.

## Finding F1: unbounded row growth from SD-supplied `/pos.dat`

- **File:** `src/t-deck/lv_obj_functions.cpp:3708` [V]
- **Severity:** HIGH
- **Failure scenario:** the load loop does `posrow++` then
  `lv_table_set_cell_value(position_ta, posrow, 0, ...)` with **no cap**, while the sibling
  increment at `:3805` correctly guards `if(bnotfound && posrow < MAX_POSROW)` [V]. `MAX_POSROW`
  (40, `variants/t_deck_plus/configuration.h:146`) is referenced **exactly once in the entire
  codebase** — at the site that already checks [V]. LVGL grows the table per row
  (`lv_table.c:95` → `lv_table_set_row_cnt`, `:112` `lv_mem_realloc`) [V], so a large or corrupt
  `/pos.dat` drives unbounded reallocation on every boot.
- **Correction to the finder:** it attributed this to the internal heap. `lv_mem_realloc` is
  `ps_realloc` here, so it exhausts **PSRAM**. Still a real SD-content-driven vector — and directly
  relevant now that a 30 GB card is going in.
- **Fix:** cap at `MAX_POSROW` in the load path.

## Finding C3: I2S driver torn down under the audio task

- **File:** `src/esp32/esp32_audio.cpp` (`audio_set_mute()`)
- **Severity:** HIGH
- **Failure scenario:** `i2s_driver_uninstall()` is called from an LVGL button handler while the
  audio task is parked inside `i2s_write(..., portMAX_DELAY)`.
- **Fix:** stop the task and join it before uninstalling.

## Remaining survivors

| ID  | Sev.    | File:line                                                | Item                                                                                                                                                              |
| --- | ------- | -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| K1  | HIGH    | `lv_obj_functions.cpp:1962-2008`                         | `tft_on()` gates KB-backlight restore on `node_keyboardlock` instead of `node_kbllightlock`; `tft_off()` kills it unconditionally                                 |
| C4  | MED-HI  | `tdeck_helpers.cpp:85`                                   | Raw `digitalWrite(TDECK_SDCARD_CS, HIGH)` outside any bus lock, mid-`ff_sd_read`                                                                                  |
| H2  | MEDIUM  | `lv_obj_functions.cpp:184`                               | `String(String&&)` not `noexcept` (`WString.h:67`) → vector growth deep-copies every buffer while originals live                                                  |
| K2  | MEDIUM  | `lv_obj_functions.cpp:3611`                              | Ring wraps to `1`, sibling wraps to `0` [V] — slot 0 frozen at boot forever                                                                                       |
| K3  | MEDIUM  | `event_functions.cpp:107-138`                            | APRS dropdown has no case for `'>'` (Car), unreachable case for `'('`                                                                                             |
| C5  | MEDIUM  | `esp32_audio.cpp`                                        | `vTaskResume`/`vTaskSuspend(NULL)` lost wakeup → dropped alert, stranded open file                                                                                |
| A1  | MEDIUM  | `event_functions.cpp:834-883`, `tdeck_main.cpp:586-623`  | Map recenter+zoom+redraw copy-pasted **4×**; a fix at one site misses three                                                                                       |
| A2  | MEDIUM  | `lv_obj_functions.cpp:329-382`, `tdeck_main.cpp:565-584` | Backlight/keyboard lock implemented twice, already diverged: touch path updates `node_modus` but never calls `tft_off()/tft_on()`; keyboard path does the reverse |
| F2  | MEDIUM  | `tdeck_main.cpp:236`                                     | `strMaps[MAX_MAP]` filled with loop bound `SDMAP_SET_COUNT` — latent OOB **write**, currently masked because both are 5                                           |
| F3  | LOW-MED | 4 × `lv_textarea_set_max_length`                         | Max length = full field width instead of width−1 → silent last-char truncation                                                                                    |
| H3  | LOW-MED | `lv_obj_functions.cpp:4266`                              | Latent iterator-invalidation UAF, held off only by a bool                                                                                                         |
| P3  | LOW     | `tdeck_main.cpp:433-451`                                 | Keypad I2C transaction every 30 ms with no `Wire.setTimeOut()`                                                                                                    |

## Refuted and corrected claims — do not re-investigate

- **G01 (stage 1) does not explain the maintainer's heap defect.** It is a real use-after-free, and
  more easily reached than stage 1 stated (zooming alone suffices). But: LVGL is PSRAM-only, so it
  is the wrong pool; it corrupts immediately rather than declining monotonically; and it needs a
  human zooming. Keep the fix, drop the theory.
- **G04 (stage 1) was overstated by ~2×.** Arduino `String` has SSO up to 13 chars, so `gps` and the
  group key never allocate — 2-3 blocks per message, not 5. Secondary to H1.
- **G15's premise was wrong, though its diagnosis was right.** The SPI bus _is_ arbitrated — by the
  Arduino HAL's per-bus mutex, not by `xSemaphore`. TFT_eSPI binds `SPIClass& spi = SPI`
  (`TFT_eSPI_ESP32_S3.c:27`), the same object `SD` uses, and `CONFIG_DISABLE_HAL_LOCKS` is unset.
  `xSemaphore` has exactly one taker and is vestigial. The only real holes are C4's raw CS writes.
- **"CI gates the native test suites on every PR" is false.** Both fork workflows are
  `disabled_manually` (`gh workflow list -R DK5EN/MeshCom-Firmware`) [V]; last `CI Build` run
  2026-08-21. `ci-build.yml` does not exist upstream at all (404 on its default branch) [V].
  So `native`, `native_aprs`, `native_dedup`, `native_capture`, `native_aprs_fuzz` and
  `native_extradio` run **nowhere automatically** — every "green suite" claim in the docs is a
  manual local run. Larger than the finder's T-01, which only flagged two of the six.
  _Trap:_ bare `gh` in this working copy resolves to **upstream**, not the fork.
- **No LVGL call is made from a foreign task** — all three `lv_task_handler` sites and every
  non-loop context were checked.
- **No ISR exists in `src/t-deck/`** — `button.check()` is never called, so the AceButton handler is
  dead code.
- Also cleared: `LV_IMG_CACHE_DEF_SIZE=0` does not cause repeated tile decoding (tiles are
  pre-decoded to a raw buffer); message-list rendering is incremental, not a full rebuild;
  `sdmap_refresh` free-ordering is correct; ring indices in `add_map_point` are in range;
  ~a dozen globals are single-writer by construction.

## Status

**No code changed. No regression tests exist for any of this.** Per the campaign rule every fix
needs a test that fails before and passes after — and note that, per the refuted-claims section,
there is currently no automation that would run such a test.

`src/t-deck/` has **0 % test coverage**. Natively testable pure logic worth extracting first
(~100-150 lines): `sdmap_lon2xf/lat2yf/project()`, `parseTimestamp()`/`build_timestamp_string()`
(encodes G14 directly), `getMapID()`/`getMapDropboxID()` (encodes G02 directly), `escape_json()`.
All are blocked only by file-scope `#include <SD.h>` / `<TFT_eSPI.h>`, the same seam extraction the
project already applies elsewhere.
