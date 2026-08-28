# LVGL / T-Deck Plus research — raw track output

These nine files are the **unedited output of a parallel research wave** (2026-08-28), eight
read-only agents plus the shared brief they all worked from. They are kept for provenance, source
URLs, and the detail that did not fit the synthesis.

## Read the synthesis first

**[`../tdeck-lvgl-agent-guide.md`](../tdeck-lvgl-agent-guide.md) is the authoritative document.**
It is the reconciled, verified version. These raw files are supporting material only.

## Known-wrong claims in these files — do not act on them

The shared brief (`00-CONTEXT.md`) was written before the effective build configuration was
established, and it states several facts that turned out to be wrong. Every track inherited them.
The errors were found and corrected during synthesis; the corrections live in the guide, **not** in
the files below.

### 1. The wrong `lv_conf.h` (affects every track)

The brief and all eight tracks cite `src/t-deck/lv_conf.h`. **That file is never compiled.** With
`-D LV_CONF_INCLUDE_SIMPLE`, LVGL resolves `#include "lv_conf.h"` through the `-I` search path;
`pio project metadata -e t_deck_plus` shows `src/t-deck` is not on it and `variants/t_deck_plus`
is. There is no `src/lv_conf.h`, no `include/lv_conf.h`, no `lib/lvgl/lv_conf.h`.

**`variants/t_deck_plus/lv_conf.h` is the effective configuration.**

| Setting                       | Stated in these files | Actually compiled                             |
| ----------------------------- | --------------------- | --------------------------------------------- |
| Montserrat fonts              | only 28               | 12, 14, 16, 18, 20 (28 absent)                |
| `LV_FONT_DEFAULT`             | `&..._28`             | `&lv_font_montserrat_12`                      |
| `LV_DISP_DEF_REFR_PERIOD`     | 10 ms                 | 16 ms                                         |
| `LV_SPRINTF_CUSTOM`           | 1                     | 0                                             |
| `LV_COLOR_16_SWAP`            | 0                     | 1                                             |
| `LV_COLOR_SCREEN_TRANSP`      | 1                     | 0                                             |
| `LV_USE_THEME_DEFAULT`        | 0                     | **1**, with `GROW 1` and `TRANSITION_TIME 80` |
| `LV_FONT_MONTSERRAT_12_SUBPX` | n/a                   | 1 (dead flash — `LV_USE_FONT_SUBPX` is 0)     |

Unaffected and still valid: `LV_USE_LOG 0`, `LV_COLOR_DEPTH 16`, `LV_MEM_CUSTOM 1` with
`ps_malloc`, `LV_TICK_CUSTOM 1` on `millis()`, `LV_IMG_CACHE_DEF_SIZE 0`, `LV_USE_ASSERT_OBJ 0`,
`LV_ASSERT_HANDLER while(1);`.

Track 8 flagged this ambiguity correctly and could not settle it under its audit-only constraint.
It was settled afterwards from the build metadata.

### 2. Consequences of that error inside specific tracks

- **Track 3 (fonts)** analyses Montserrat-28, which is not built. Its conclusions still hold —
  every built-in Montserrat font in this tree shares the same generator range
  (`-r 0x20-0x7F,0xB0,0x2022`), so the missing-glyph finding applies unchanged to 12/14/16/18/20.
  Only the font name is wrong. Its §5 claim that no theme is active is wrong.
- **Track 5 (touch)** concludes that no default press/transition animations exist. Wrong — the
  default theme is active with `GROW` and an 80 ms transition.
- **Track 3 (fonts)** reasons about `LV_USE_FONT_SUBPX 0` from the dead file. The value happens to
  be 0 in the real config too, but `LV_FONT_MONTSERRAT_12_SUBPX 1` compiles an unusable extra font.

### 3. Other corrections

- **Track 3's umlaut finding is real but mis-framed.** It is presented as a live UI defect. It is
  not: the only umlauts in `src/t-deck/` are in code comments (`lv_obj_functions.cpp:1487`, `:4207`,
  `event_functions.cpp:600`). No LVGL label is fed a German string today. The genuine exposure is
  **received mesh message text**, which is arbitrary user-supplied UTF-8.
- **Track 6 claims the platform is `espressif32@6.13.0`** and lists this as contradicting the brief.
  The brief was right. `variants/t_deck_plus/platformio.ini:4` pins `6.6.0` at env level, which
  overrides the `^6.13.0` in the root `platformio.ini:332` — confirmed by the resolved framework
  `framework-arduinoespressif32@3.20014.231204` (Arduino core 2.0.14). Track 6's derived
  FreeRTOS facts (`configMAX_PRIORITIES = 25`, `CONFIG_FREERTOS_HZ = 1000`) come from that same
  installed package and **are** correct.
- **The brief suggested `lv_obj_add_event_cb(NULL, ...)` as a global event hook.** Track 7 correctly
  rejected this: it is not valid in 8.3 and NULL-derefs in this build because `LV_USE_ASSERT_OBJ`
  is 0. Do not use it.

### 4. Claims verified as correct during synthesis

Spot-checked directly against the tree and confirmed: the `tft_on()`-inside-`read_cb` chain and its
~400-450 ms of blocking `delay()`; the audio task priority of 50 against `loopTask` at 1;
`play_file_from_sd_blocking()` having no callers; `play_cw('r')` being reachable from the
incoming-message path; `xSemaphore` being taken in only two places with no SD access guarded;
`MSG_TAB_MAX_MESSAGES` trimming the model but not the widget tree; `LV_USE_LOG 0`; the
pre-existing `lv_obj_invalidate_hook` patch in vendored LVGL.

## The tracks

| File                               | Topic                                                                           |
| ---------------------------------- | ------------------------------------------------------------------------------- |
| `00-CONTEXT.md`                    | The shared brief every agent worked from. **Contains the config errors above.** |
| `01-redraw-invalidation.md`        | Invalidation pipeline, the silent-drop catalogue, re-entrancy, threading        |
| `02-buffers-partial-refresh.md`    | Draw buffers, `full_refresh`/`direct_mode`, DMA, PSRAM, shared SPI bus          |
| `03-fonts-text.md`                 | Font architecture, flash cost, `lv_font_conv`, label API costs                  |
| `04-widgets-scroll-popup-icons.md` | Scrolling cost, list trimming, `lv_obj_del_async`, msgbox, images               |
| `05-touch-responsiveness.md`       | Input path latency, two-phase pattern, deferral, FreeRTOS shape                 |
| `06-audio-blocking.md`             | ESP32-audioI2S internals, priority, per-sample `i2s_write`, I2S teardown        |
| `07-observability.md`              | Existing instrumentation, LVGL log module, driver hooks, observer effect        |
| `08-repo-inventory.md`             | Widget census, invalidate call sites, threading map, existing line formats      |

Each file carries its own source URLs and an explicit `Open questions / UNVERIFIED` section. Those
sections are worth reading — several of them mark claims that were never confirmed on hardware.
