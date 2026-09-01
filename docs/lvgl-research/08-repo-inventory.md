# Track 8 — Inventory of the existing T-Deck GUI code in this repository

## TL;DR for the coding agent

- **CRITICAL, verify before touching any `LV_*` config-dependent fix**: there are TWO different,
  materially divergent `lv_conf.h` files that can plausibly be the one the `t_deck_plus`
  PlatformIO env actually compiles: `src/t-deck/lv_conf.h` and `variants/t_deck_plus/lv_conf.h`
  (byte-identical to `variants/t_deck/lv_conf.h`). They disagree on `LV_DISP_DEF_REFR_PERIOD`
  (10 ms vs 16 ms), `LV_SPRINTF_CUSTOM` (1 vs 0), `LV_COLOR_16_SWAP` (0 vs 1), and — decisively —
  on which Montserrat font sizes are enabled. See Finding 10.1: do not trust the shared-context
  file's `lv_conf.h` facts without re-verifying which file the compiler actually picks up.
- The whole UI is built **once**, before `loop()` starts, by `setDisplayLayout(lv_scr_act())`
  (`tdeck_main.cpp:148`, called from `initTDeck()`). There is no `lv_scr_load()` anywhere in the
  T-Deck code — screen switching is entirely `lv_tabview_set_act()` on one persistent
  `lv_tabview_t` (`tv`), 8 tabs, all created up front.
- Every `lv_*` call in the T-Deck GUI code that this track could find executes from a single
  FreeRTOS task: the Arduino main loop task. This includes the message-received path
  (`OnRxDone` → … → `tdeck_add_MSG`), the BLE/WiFi header-icon updates (deferred via flag-polling
  in `loop()`, not called from the BLE/WiFi task), and all debug/harness serial commands
  (`checkSerialCommand()` in `loop()`). The audio playback task
  (`play_function`, `esp32_audio.cpp:509`) never calls any `lv_*` function. **Do not assume a
  cross-task LVGL race is the cause of the reported symptoms** — this track found no evidence of
  one; see Finding 5.1.
- `msg_focus_and_alert()` (`lv_obj_functions.cpp:3974`) is the one place that both drives LVGL and
  can play audio: it calls `play_file_from_sd()` (async, backgrounds correctly onto the audio
  task) **but falls back to `play_cw('r')`** (fully synchronous, blocking `i2s_write` loop) if the
  configured message tone file is missing/muted-check fails. `play_cw()` runs in whichever task
  called it — here, the main loop task — so a missing/misconfigured message tone file blocks the
  entire UI (and LoRa RX servicing) for the duration of the CW tone. See Finding 4.1.
- The message list (`msg_list`, an `lv_obj_create` flex-column container) is only fully
  resynchronized to its (trimmed) data model — via `lv_obj_clean()` + rebuild — when a message tab
  is switched to (`msg_render_active_tab()`). While a tab is the _active_ one and stays active,
  incoming messages only ever **append** a new bubble (`msg_list_append_bubble`,
  `lv_obj_create(msg_list)`); the corresponding model-side trim (`MSG_TAB_MAX_MESSAGES = 50`,
  `msg_tabs_trim_history()`) never deletes the matching old LVGL child objects. The widget tree for
  a long-lived, actively-viewed conversation can therefore grow without bound. See Finding 7.2.
- Only one explicit `lv_obj_invalidate()` call exists in the whole T-Deck code:
  `lv_obj_invalidate(lv_scr_act())` in `msg_focus_and_alert()`, gated on `bWithAudio && tv != NULL
&& active tab is not 1 or 7`. There is no `lv_refr_now`, no `lv_obj_refresh_style`, no
  `lv_obj_refresh_ext_draw_size`, no `lv_disp_enable_invalidation` anywhere. Waking the display
  (`tft_on()`, `tdeck_helpers.cpp:1976`) does **not** invalidate anything — it only toggles the TFT
  backlight/sleep state. See Finding 3.1 and 10.2.
- `lv_task_handler()` is called from exactly 3 places: the main `loop()`
  (`esp32_main.cpp:3847`, unconditional every iteration), inside `addMessage()`'s 2-second
  busy-wait at boot (`tdeck_main.cpp:284`, `while(...) { lv_task_handler(); delay(5); }`, boot-time
  only, single-threaded), and once at the end of `tdeck_reset_msg_tabs()`
  (`lv_obj_functions.cpp:4307`). None of these three sites are inside an LVGL event callback, so
  none are the classic "calling `lv_task_handler()` from inside `lv_task_handler()`" reentrancy
  trap — but the `tdeck_reset_msg_tabs()` call site is worth double-checking if it is ever invoked
  from a path that itself started inside an event callback (see Finding 4.2).
- `tdeck_debug.cpp` already provides `[REDRAW]`, `[REFR]`, `[REFRSTART]`, `[UISTAT]`, `[TAB]`,
  `[DRAWER]`, `[TFT]`, `[SCREEN]`, `[BOOT]` serial lines and a strong-override hook
  (`lv_obj_invalidate_hook`) with an Xtensa backtrace. A _second_, independent instrumentation
  layer, `src/instrument.h`/`src/instrument.cpp` (repo root, not under `src/t-deck/`), provides
  `[INSTR-HEAP]`, `[INSTR-FLUSH]`, `[INSTR-LOOP]`, `[INSTR-GUI]`. Both are consumed by
  `tools/bench/tdeck_parse.py`. Extend these two files/formats; do not invent a third. See
  Finding 9.1 for the exact line grammars.
- The widget palette is narrow: `lv_label` (61 call sites), `lv_btn` (27), `lv_textarea` (23),
  `lv_obj` as plain container (12, plus one per incoming message bubble at runtime),
  `lv_dropdown` (3), `lv_table` (3), `lv_tabview` (1) + `lv_tabview_add_tab` (8),
  `lv_img` (2), `lv_timer_create` (4). **Never used anywhere**: `lv_switch`, `lv_slider`,
  `lv_chart`, `lv_canvas`, `lv_list_create` (the LVGL built-in list widget), `lv_checkbox`,
  `lv_roller`, `lv_bar`, `lv_arc`, `lv_msgbox_create`. A custom `void lv_msgbox(char*, char*)` is
  _declared_ in `lv_obj_functions.h:19` but never defined or called anywhere — dead API surface,
  do not assume any modal dialog exists in this UI.

## Findings

### 1. Screens and tabs

**Claim**: The T-Deck UI is a single persistent screen (`lv_scr_act()`, never replaced) holding one
`lv_tabview_t` object (global `lv_obj_t *tv`) with 8 tabs, created once in `setDisplayLayout()`
(`src/t-deck/lv_obj_functions.cpp:473`), called once from `initTDeck()`
(`src/t-deck/tdeck_main.cpp:148`) before `loop()` starts. Tab switching is exclusively
`lv_tabview_set_act(tv, idx, LV_ANIM_OFF|LV_ANIM_ON)`. There is no `lv_scr_load()` /
`lv_disp_load_scr()` call anywhere under `src/t-deck/`.

**Tab creation order** (`lv_obj_functions.cpp:613-620`), which is also the tab index used
everywhere (`tabview_event_cb` switch, `tdeck_dbg_tab()`, `TAB_NAMES[]` in `tdeck_debug.cpp:48`):

| idx | var  | icon                 | debug name (`tdeck_debug.cpp`) | meaning (per `tabview_event_cb`, `event_functions.cpp:888`)                                                                                                                                                     |
| --- | ---- | -------------------- | ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | `t2` | `LV_SYMBOL_ENVELOPE` | `"msg"`                        | MSG (message list, `msg_list`)                                                                                                                                                                                  |
| 1   | `t5` | `LV_SYMBOL_KEYBOARD` | `"keyboard"`                   | SND (compose/send, `text_input`, focuses on tab select)                                                                                                                                                         |
| 2   | `t3` | `""`                | `"heart"`                      | POS (own position)                                                                                                                                                                                              |
| 3   | `t7` | `LV_SYMBOL_IMAGE`    | `"map"`                        | MAP (`map_ta`, `sdmap_refresh`/`refresh_map` on select)                                                                                                                                                         |
| 4   | `t6` | `LV_SYMBOL_GPS`      | `"gps"`                        | GPS (`tdeck_refresh_track_view()` on select)                                                                                                                                                                    |
| 5   | `t4` | `LV_SYMBOL_LIST`     | `"list"`                       | MHD (mheard table, `mheard_ta`)                                                                                                                                                                                 |
| 6   | `t8` | `""`                | `"menu"`                       | PATH (`path_ta`) — **note**: `tdeck_debug.cpp`'s label `"menu"` for idx 6 is a naming mismatch with `tabview_event_cb`'s `// case 6: PATH`; both refer to the same tab, just named differently in the two files |
| 7   | `t1` | `LV_SYMBOL_SETTINGS` | `"settings"`                   | SET (`tdeck_refresh_SET_view()` on select)                                                                                                                                                                      |

Selecting a tab also shows/hides `msg_controls` (`lv_obj_add_flag`/`clear_flag`
`LV_OBJ_FLAG_HIDDEN`) and calls `tdeck_hide_tab_menu()`.

**Overlay / drawer**: a separate "tab menu drawer" (`tab_menu_header`, `tab_menu_button`, etc.,
built inside `setDisplayLayout()` starting around `lv_obj_functions.cpp:490`) is a fixed header bar
with WiFi/BT/GPS/battery/time icons and buttons for keyboard-lock and standby-lock, independent of
the tabview content area; `tdeck_show_tab_menu()`/`tdeck_hide_tab_menu()`/`tdeck_toggle_tab_menu()`
toggle `LV_OBJ_FLAG_HIDDEN` on the tab bar (`lv_tabview_get_tab_btns(tv)`), exposed to the debug
harness via `tdeck_dbg_drawer()`.

**Global/static `lv_obj_t*` objects** (from `lv_obj_functions_extern.h` plus in-file statics):
`tv`, `msg_list`, `map_ta`, `map_no_data_label`, `position_ta`, `mheard_ta`, `path_ta`, `track_ta`,
`text_input`, `dm_callsign`, `msg_controls`, `dropdown_aprs`, `dropdown_country`,
`dropdown_mapselect`, all `setup_*` fields (callsign/lat/lon/alt/utc/aprsgroup/…/txpower),
`btn_gps`/`btn_mesh`/`btn_noallmsg`/`btn_persist_*`/`btn_soundon`/`btn_track`/`btn_wifi*`/
`btn_webserver`, `btn_time_label*`, `btn_batt_label*`, header icons
(`tab_menu_icon_label`, `tab_kbl_icon_label`, `tab_standby_icon_label`, `header_time_label`,
`header_batt_label`, `header_batt_icon`, `header_sat_label`, `header_sat_icon`,
`header_wifi_icon`, `header_bt_icon`), plus file-local statics `msg_tab_bar`, `msg_tab_hint_label`,
`msg_list_hint_label`, `track_clear_timer`, `msg_flush_timer`, `sys_msg_save_timer`.

**Source**: `src/t-deck/lv_obj_functions.cpp:473-624`, `src/t-deck/lv_obj_functions_extern.h`,
`src/t-deck/tdeck_main.cpp:146-148,194`, `src/t-deck/event_functions.cpp:888-941`,
`src/t-deck/tdeck_debug.cpp:47-51`.

### 2. Widget census

Counts are call-site counts of the `lv_*_create()` functions in `src/t-deck/lv_obj_functions.cpp`
(153 KB, 4337 lines); `tdeck_main.cpp` adds one more `lv_img_create` for the trackball cursor.

| widget               |                                                                       count | representative call site                                                                                                                                          |
| -------------------- | --------------------------------------------------------------------------: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `lv_label_create`    |                                                                          61 | `lv_obj_functions.cpp:717` (first header label)                                                                                                                   |
| `lv_btn_create`      |                                                                          27 | `lv_obj_functions.cpp:499` (`tab_menu_button`)                                                                                                                    |
| `lv_textarea_create` |                                                                          23 | `lv_obj_functions.cpp:749` (`setup_callsign`)                                                                                                                     |
| `lv_obj_create`      | 12 (+ 1 per message bubble wrapper at runtime, `lv_obj_functions.cpp:2849`) | `lv_obj_functions.cpp:451`                                                                                                                                        |
| `lv_dropdown_create` |                                                                           3 | `lv_obj_functions.cpp:812` (`dropdown_aprs`)                                                                                                                      |
| `lv_table_create`    |                                                                           3 | `lv_obj_functions.cpp:1455` (`position_ta`), `1539` (`mheard_ta`), `1573` (`path_ta`)                                                                             |
| `lv_img_create`      |                                                                           2 | `lv_obj_functions.cpp:1477` (`map_ta`), `tdeck_main.cpp:428` (trackball cursor `trackball_cursor_obj`)                                                            |
| `lv_tabview_create`  |                                                1 (+ 8 `lv_tabview_add_tab`) | `lv_obj_functions.cpp:601`                                                                                                                                        |
| `lv_timer_create`    |                                                                           4 | `lv_obj_functions.cpp:2467` (`msg_flush_timer`, 60 s), `2710`/`2779` (`sys_msg_save_timer`, debounce, 2 s, one-shot), `4046` (`track_clear_timer`, 2 s, one-shot) |

**Never used anywhere in `src/t-deck/`**: `lv_switch`, `lv_slider`, `lv_chart`, `lv_canvas`,
`lv_list_create` (LVGL's built-in list widget — the "message list" is a hand-rolled
`lv_obj_create` + flex-column, not `lv_list`), `lv_checkbox`, `lv_roller`, `lv_bar`, `lv_arc`,
`lv_spinner`, `lv_calendar`, `lv_meter`, `lv_line_create`. `lv_msgbox_create` (the LVGL built-in) is
also never called; the header-declared custom `void lv_msgbox(char*, char*)`
(`lv_obj_functions.h:19`) has no matching definition anywhere in `lv_obj_functions.cpp` — it is
dead declared-but-unimplemented API.

**Table draw callbacks**: `position_ta_draw_event`, `mheard_ta_draw_event`, `path_ta_draw_event`
(`event_functions.cpp:966-985`) all delegate to `table_center_first_row()`, which reads
`lv_event_get_draw_part_dsc(e)` and center-aligns row 0's label — registered via
`LV_EVENT_DRAW_PART_BEGIN`-class events on the table objects (event registration itself is in
`lv_obj_functions.cpp`, not shown here; the callbacks are declared in `event_functions.h:43-45`).

**Source**: as above; widget-type counts obtained via
`grep -c '\blv_<widget>_create(' src/t-deck/lv_obj_functions.cpp`.

### 3. Every explicit invalidate/refresh call

**Claim**: There is exactly **one** explicit `lv_obj_invalidate()` call in the entire T-Deck GUI
code, and zero calls to `lv_refr_now`, `lv_obj_refresh_style`, `lv_obj_refresh_ext_draw_size`,
`lv_obj_update_layout`, or `lv_disp_enable_invalidation`.

- `lv_obj_functions.cpp:3992`: `lv_obj_invalidate(lv_scr_act())`, inside `msg_focus_and_alert()`
  (`lv_obj_functions.cpp:3974`). Guarded by: `tv != NULL && bWithAudio` (i.e. only for messages
  that carry an audio alert, not for silent/system messages) **and** the tab was just switched to
  0 because the active tab was neither 1 (SND) nor 7 (SET). A commented-out line right after it
  (`//lv_task_handler(); Y5 check`, `lv_obj_functions.cpp:3994`) shows a prior author considered
  and then disabled an immediate forced `lv_task_handler()` call at that same spot — i.e. this is a
  location that has already been tuned once during the current fix campaign.
- **Why it matters**: every _other_ redraw in this codebase relies entirely on LVGL's normal
  dirty-area tracking from the underlying setter calls (`lv_label_set_text`,
  `lv_obj_add_flag`/`clear_flag(LV_OBJ_FLAG_HIDDEN)`, `lv_obj_set_style_*`, `lv_img_set_src`,
  `lv_table_set_cell_value`, etc.), each of which internally calls `lv_obj_invalidate()` on the
  affected object per normal LVGL 8.3 semantics. `tdeck_debug.cpp`'s `lv_obj_invalidate_hook()` is
  a **strong override of a weak hook LVGL itself calls from inside `lv_obj_invalidate_area()`**
  (`lib/lvgl/src/core/lv_obj_pos.c`), so it fires for _every_ invalidate LVGL performs internally,
  not just the one explicit call above — this is the correct place to look for "was this object
  ever invalidated" questions, not a grep for `lv_obj_invalidate(`.
- **Symptom if you add more explicit invalidates carelessly**: `LV_USE_PERF_MONITOR` and
  `LV_USE_MEM_MONITOR` are both 0 in every candidate `lv_conf.h` (see Finding 10.1), so there is no
  on-screen FPS counter to catch a redraw storm; the `[REDRAW]` rate cap in `tdeck_debug.cpp`
  (200 lines/sec, `tdeck_debug.cpp:42`) exists specifically because uncontrolled invalidate logging
  can itself flood the serial line — the same risk applies to uncontrolled invalidate _calls_.

**Source**: `grep -n "lv_obj_invalidate\|lv_refr_now\|lv_obj_refresh_style\|lv_obj_refresh_ext_draw_size\|lv_obj_update_layout\|lv_disp_enable_invalidation" src/t-deck/*.cpp` → only the one hit above (plus the hook plumbing in `tdeck_debug.cpp`, which is instrumentation, not application-level invalidation).

### 4. Every `lv_task_handler()`/`lv_timer_handler()` call site

There is no `lv_timer_handler()` call anywhere (LVGL 8.3's timer/task handler is still named
`lv_task_handler()` in this vendored version; `lv_timer_handler` is a v8.3-compatible alias that
this codebase does not use).

1. **`src/esp32/esp32_main.cpp:3847`** — unconditional, once per `loop()` iteration, gated only by
   `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`. This is the steady-state driver.
   Immediately preceded by the display-timeout check (`tft_off()` if idle ≥
   `TDECK_TFT_TIMEOUT` seconds) in the same `#if` block. **Not** inside a busy-wait or an event
   callback — this is the normal, correct call site.

2. **`src/t-deck/tdeck_main.cpp:284`** — inside `addMessage()`'s boot-time busy-wait:

   ```cpp
   void addMessage(const char *str) {
       ...
       uint32_t run = millis() + 2000;
       while ((int32_t)(millis() - run) < 0) {
           lv_task_handler();
           delay(5);
       }
   }
   ```

   Only called from `initTDeck()` (boot messages) and `tdeck_addMessage()` (also boot-time,
   `tdeck_main.cpp:834`) — i.e. before `loop()`/the main `lv_task_handler()` call site is ever
   reached, single-threaded, not re-entrant. **Flag for the coding agent**: this pattern (blocking
   2 s per boot message, one call per `addMessage()` invocation, and `initTDeck()` calls it ~5
   times for boot status lines plus 2 more for the version banner) adds up to several seconds of
   boot-time delay by construction — not itself a bug, but worth knowing if boot time is ever in
   scope.

3. **`src/t-deck/lv_obj_functions.cpp:4307`** — end of `tdeck_reset_msg_tabs()`:
   ```cpp
   void tdeck_reset_msg_tabs(void) {
       msg_tabs_clear_all();
       ...
       for (const auto &p : persisted_msgs) { msg_tabs_add_message(...); ... }
       ...
       lv_task_handler();   // "Force a screen refresh to ensure UI is updated"
   }
   ```
   `tdeck_reset_msg_tabs()` is called from `tdeck_clear_text_ta()` (`tdeck_main.cpp:856`); this
   track did not find a caller of `tdeck_clear_text_ta()` anywhere in `src/t-deck/` or the wider
   tree (grep for `tdeck_clear_text_ta(` under `src/` turns up only the definition and the
   declaration) — **this function currently appears to be dead code**, called from nowhere, so its
   `lv_task_handler()` call site is not presently reachable at runtime. If the coding agent wires a
   caller for it, check whether that caller could itself be inside an LVGL event callback (e.g. a
   "clear messages" button handler) — calling `lv_task_handler()` from inside an event callback
   that LVGL's own `lv_task_handler()` is currently running through is the classic LVGL 8.x
   reentrancy hazard (nested display-refresh, corrupted draw-buffer state).

**Source**: `grep -n "lv_task_handler\|lv_timer_handler" src/t-deck/*.cpp src/esp32/esp32_main.cpp`.

### 5. Threading map

**Claim (the most important finding of this track)**: this track could not find **any** call site
where an `lv_*` function executes from a FreeRTOS task other than the Arduino main loop task (the
one that also runs `setup()`/`loop()` and therefore `lv_task_handler()`). The reported symptoms
("no redraw triggered", "messages don't repaint the list", "audio blocks the UI") do **not** appear
to be caused by a cross-task LVGL race in this codebase as it stands; they are more likely caused
by call-graph/logic issues (missing invalidate paths, the widget-tree-growth issue in Finding 7.2,
or the synchronous CW fallback in Finding 4.1/8's audio note) than by thread-safety violations.
This should be treated as a strong lead, not a certainty — see the caveats below.

**Every `xTaskCreate*` reachable from the T-Deck build** (`grep -rn xTaskCreate src/`, filtered to
files compiled into `t_deck_plus` per `build_src_filter = ${esp32.src_filter} +<t-deck/*>
-<t-deck-pro/*> -<t5-epaper/*>` in `variants/t_deck_plus/platformio.ini`):

| task                                   | file:line                                             | core | priority |  stack | touches `lv_*`?                                                                                                                                                                                                                                                       |
| -------------------------------------- | ----------------------------------------------------- | ---: | -------: | -----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `audio play task` (`play_function`)    | `esp32_audio.cpp:131-139` (`xTaskCreatePinnedToCore`) |    1 |       50 | 16 KiB | **No.** Body (`esp32_audio.cpp:509-538`) is `while(audio.isRunning()) { ...; audio.loop(); vTaskDelay(1); } audio.stopSong(); vTaskSuspend(NULL);` in a `for(;;)` — no LVGL, no display, no SD-card-vs-SPI-semaphore interaction beyond what `Audio.cpp` itself does. |
| `con_auth` (net console TCP 2323 auth) | `net_console.cpp:383` (`xTaskCreatePinnedToCore`)     |    1 |        1 | 3072 B | **No** — `grep -n "lv_" src/net_console.cpp` is empty. This is the debug-console-log task (memory: "Net debug console port 2323"), unrelated to the harness's serial command channel.                                                                                 |

`src/t-deck-pro/*` and `src/t5-epaper/*` also `xTaskCreate` (GPS/LoRa/button tasks) but are
excluded from the `t_deck_plus` build by `build_src_filter` and are a different board family —
irrelevant to this inventory, listed only so the coding agent doesn't confuse them with T-Deck
Plus.

**Semaphores/mutexes**:

| handle                    | created                               | guards                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ------------------------- | ------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `xSemaphore` (binary)     | `tdeck_main.cpp:116` (`initTDeck()`)  | The shared TFT/SD SPI bus. Taken in `disp_flush()` (`tdeck_main.cpp:463`) around every `tft.startWrite()/setAddrWindow()/pushColors()/endWrite()`, and in `tdeck_dbg_screencrc()` (`tdeck_debug.cpp:311`) around the panel readback. **`setBrightness()`/`tft_on()`/`tft_off()` (`tdeck_helpers.cpp`) do _not_ take `xSemaphore`** despite calling `tft.init()`, `tft.writecommand()` — these run only from the main task today so it is not currently a race, but any future caller of `tft_on()`/`setBrightness()` from a second task would collide with `disp_flush()` on the SPI bus without this mutex protecting it. SD-card access (`SD.begin`, `sdmap_refresh()`'s `SD.open`/`File::read`) does **not** take `xSemaphore` either — the shared-SPI-bus contract documented in the shared context file ("A single FreeRTOS semaphore serialises TFT and SD access") is only actually enforced on the TFT side of the bus in this code; SD reads run unguarded relative to `xSemaphore`, currently safe only because SD access, like everything else, happens exclusively from the main task. |
| `audioSemaphore` (binary) | `esp32_audio.cpp:60` (`init_audio()`) | Guards `play_file_from_sd()`'s task-creation/resume section and `play_file_from_sd_blocking()`'s `connecttoFS` call — i.e. only guards _starting_ playback, not the playback loop itself (matches the shared-context note: "the audio semaphore guarding only setup and not playback").                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |

**Deferred cross-task hand-offs this codebase already uses correctly** (i.e. the pattern to follow
if new cross-task UI updates are ever needed): `bPendingDisplayText`/`pendingDisplayMsg`/
`pendingDisplayRssi`/`pendingDisplaySnr` (set by `queueDisplayText()` in `lora_functions.cpp`,
consumed by `flushDeferredDisplayUpdates()` in the main loop, `esp32_main.cpp:1816`, called at
`esp32_main.cpp:2037` and `3873`) and the BLE `deviceConnected`/`oldDeviceConnected` flag-diffing at
`esp32_main.cpp:2831-2882` that gates the `tdeck_update_header_bt()` calls. Both are read-in-main-
loop, write-from-elsewhere patterns; neither writer touches `lv_*` directly.

**Message-received call chain** (documented already, verbatim, in `src/test_inject.cpp:5-10`, which
this track independently confirmed by reading `esp32_main.cpp` and `loop_functions.cpp`):

```
OnRxDone (delivered synchronously from the main-loop poll on ESP32, esp32_main.cpp:3869-3997,
          comment: "poll() delivers RX synchronously via glueRxSink -> OnRxDone (main-loop ...)")
  -> decodeAPRS(...)                                        [aprs_functions.cpp]
  -> queueDisplayText(aprsmsg, rssi, snr)                    [lora_functions.cpp:133, static]
  -> main loop flushDeferredDisplayUpdates()                 [esp32_main.cpp:1816]
  -> sendDisplayText(...)                                    [loop_functions.cpp:2052]
  -> tdeck_add_MSG(aprsmsg, true)   (via loop_functions.cpp:2423 and :3462)
                                                              [t-deck/lv_obj_functions.cpp:4182]
```

Every hop in this chain runs in the main task. **Caveat**: this track traced the ESP32 RX path only
(the comment at `esp32_main.cpp:3869` is explicit that this is synchronous-from-main-loop on
ESP32); it did not audit whether any nRF52-only code path is compiled into the T-Deck build (it
should not be, given `BOARD_T_DECK_PLUS` is ESP32-only, but this was not exhaustively re-verified
against every `#if defined(BOARD_RAK4630)` branch in `lora_functions.cpp`).

**Caveats on the "no cross-task LVGL calls" claim**: (a) this is a `grep`/read-based audit of
`src/t-deck/*`, `src/esp32/esp32_audio.cpp`, `src/net_console.cpp`, and the message/command
dispatch chain in `src/loop_functions.cpp`/`src/command_functions.cpp`/`src/esp32/esp32_main.cpp`
— it did not exhaustively read every one of the ~40 `.cpp` files under `src/` that are compiled
into `t_deck_plus`, so a stray `lv_*` call from, e.g., a WiFi-webserver request handler running on
its own task was not ruled out with certainty; (b) the debug/harness serial commands
(`--tab`, `--drawer`, `--uistat`, `--redrawlog`, `--tft`, `--screencrc` — wired in
`command_functions.cpp:4515-4587`) are reached via `checkSerialCommand()`
(`esp32_main.cpp:4099`, called from `loop()` at `esp32_main.cpp:3324`), confirmed main-task; they
are **not** reachable through `net_console.cpp`'s TCP 2323 console (that task carries no
`commandAction()` call at all — it is a read-only log mirror per the existing memory note).

**Source**: `grep -rn xTaskCreate src/`, `grep -n xSemaphoreCreate src/t-deck/*.cpp
src/esp32/esp32_audio.cpp`, `src/test_inject.cpp:1-31`, `src/esp32/esp32_main.cpp:1813-2040,
2831-2882, 3324, 3847-3875, 3997, 4099-4110`, `src/loop_functions.cpp:2423, 3462`,
`src/net_console.cpp` (full read), `src/command_functions.cpp:4505-4590`.

### 6. Fonts

See Finding 10.1 for the full config-file ambiguity; this finding states only what the _code_
references, independent of which `lv_conf.h` wins.

**Fonts referenced by `&lv_font_montserrat_N` in `src/t-deck/lv_obj_functions.cpp`** (via
`grep -oE '&lv_font_montserrat_[0-9]+'`): sizes **12, 14, 16, 18** — 16 call sites total (12: 3
sites at `lv_obj_functions.cpp:653,682,694`; 14: 3 sites at `554,566,711` plus later header labels;
16: 1 site at `482`; 18: the remaining ~9 header-icon/label sites, e.g. `511, 526, 541, 547, 561,
573, 579, 585`, plus `1862`). **Zero** explicit references to `&lv_font_montserrat_28` anywhere in
the code — size 28 is only ever the _default_ font (`LV_FONT_DEFAULT`) in whichever `lv_conf.h`
sets it that way.

**`LV_SYMBOL_*` usage** (42 total references, `grep -c`): `LV_SYMBOL_ENVELOPE`, `KEYBOARD`,
`IMAGE`, `GPS`, `LIST`, `SETTINGS` (tab icons), `BARS`, `EYE_OPEN` (drawer buttons),
`BATTERY_EMPTY`/`BATTERY_FULL`/`BATTERY_*` (battery indicator, dynamic), `WIFI`, `BLUETOOTH`
(header icons), `RIGHT`, `TRASH`, `USB`, `VOLUME_MAX`. These are glyphs baked into every Montserrat
bitmap font LVGL ships (the symbol range is merged into each `lv_font_montserrat_N`, not a
separate font), so as long as _a_ Montserrat font that's actually enabled is used for these labels,
the symbols render; there is no separate "symbol font" config to check.

**Consequence for whichever `lv_conf.h` wins** (Finding 10.1): if `src/t-deck/lv_conf.h` (only
`LV_FONT_MONTSERRAT_28 1`, all others 0) is the one actually compiled, then all 16 call sites
above reference an undeclared identifier (`lv_font.h`'s `LV_FONT_DECLARE` macros for sizes 12/14/
16/18 are themselves compiled out when the corresponding `LV_FONT_MONTSERRAT_N` macro is 0 — see
`lib/lvgl/src/font/lv_font.h:132-157`) and the T-Deck Plus firmware **could not compile**. Since
this firmware evidently does build (recent commits reference successful builds, RAM-comparison
docs exist), the far more likely conclusion is that `variants/t_deck_plus/lv_conf.h` (which enables
12/14/16/18/20, disables 28) is the config that actually wins for this environment — but this track
could not run a build to prove it (audit-only constraint). **Do not silently assume either config
without checking** — see the concrete verification steps in Finding 10.1.

**Source**: `grep -noE '&lv_font_montserrat_[0-9]+|LV_FONT_MONTSERRAT_[0-9]+' src/t-deck/*.cpp
src/t-deck/*.h`, `lib/lvgl/src/font/lv_font.h:132-220`, `lib/lvgl/src/font/lv_font_montserrat_18.c:13-17`.

### 7. Message list / log rendering

**Data model**: `MsgBubble` (`lv_obj_functions.cpp:160`, fields: `type` (`System`/`Incoming`/
`Outgoing`, enum at line 153), `header`, `timestamp`, `body`, `gps`/`sd`/`wlan` status strings).
Grouped by conversation into `std::vector<MsgTabEntry> msg_tab_entries` (line 178,
`MsgTabEntry.bubbles` is `std::vector<MsgBubble>`), each entry capped at `MSG_TAB_MAX_MESSAGES = 50`
(line 182) via `msg_tabs_trim_history()` (line 2513). Cross-conversation persistence is
`std::vector<std::pair<String, MsgBubble>> persisted_msgs` (line 185), capped at
`PERSISTED_MSG_LIMIT = 1000` (line 187), written to SD (`log_message_to_sd`,
`save_persisted_messages`) either immediately (`meshcom_settings.node_immediate_save`) or batched
every `FLUSH_THRESHOLD` messages.

**Widget**: `msg_list` (global `lv_obj_t*`, an `lv_obj_create` flex-column container — **not**
LVGL's `lv_list` widget — created at `lv_obj_functions.cpp:1435-1448`, `LV_SCROLLBAR_MODE_AUTO`,
`LV_DIR_VER` scroll, `LV_FLEX_FLOW_COLUMN`). Each visible message is one child `lv_obj_create`
"bubble wrapper" (`msg_list_append_bubble`, `lv_obj_functions.cpp:2842-2856` and following, not
fully read past the header/body-label construction) sized `lv_pct(100)` wide,
`LV_SIZE_CONTENT` tall.

**Exact path from "message received" to "widget updated"** (call chain fully confirmed, see
Finding 5's threading map for the earlier hops):

```
tdeck_add_MSG(aprsmsg, bWithAudio)        [lv_obj_functions.cpp:4182]
  -> builds a MsgBubble, determines "conversation" (tab) key
  -> msg_tabs_add_message(conversation, bubble)   [lv_obj_functions.cpp:2677]
       -> msg_tabs_get_or_create_entry(...)        (may create a new MsgTabEntry + tab button)
       -> entry->bubbles.push_back(bubble); msg_tabs_trim_history(entry->bubbles);  // MODEL only
       -> persist to SD / persisted_msgs (skipped while loading_messages_from_file)
       -> if (index == msg_active_tab_index):
              msg_list_append_bubble(bubble)        // WIDGET: appends one lv_obj_create child
              lv_obj_scroll_to_view(last_child, LV_ANIM_ON)
          else:
              msg_tabs_select_index(index)          // switches tabs -> msg_render_active_tab()
                                                      //   -> msg_list_clear() (lv_obj_clean)
                                                      //   -> rebuild all bubbles from the (trimmed) model
  -> if (!loading_messages_from_file): msg_focus_and_alert(bWithAudio)
       -> tft_on()
       -> if bWithAudio and current tab is not SND(1)/SET(7):
              lv_tabview_set_act(tv, 0, LV_ANIM_OFF)
              lv_obj_invalidate(lv_scr_act())        // the one explicit invalidate in the codebase
       -> if bWithAudio: play_file_from_sd(...) or play_cw('r')  // see Finding 8/4.1
```

No explicit `lv_task_handler()` call anywhere in this chain — it relies entirely on the next
`loop()` iteration's unconditional `lv_task_handler()` call (Finding 4, site 1) to actually flush
pixels to the panel. Since this whole chain runs in the same main-loop iteration that will shortly
call `lv_task_handler()` anyway (assuming `tdeck_add_MSG()` isn't itself called from deep inside a
`loop()` sub-call that's followed by many more statements before `lv_task_handler()` runs), this
should normally work — but it also means any exception/early-return in this chain, or any long
delay elsewhere in the same `loop()` iteration (e.g., `play_cw()` blocking, Finding 4.1) directly
delays the flush of the newly-added message.

**Finding 7.2 — bubbles are trimmed in the model but not always in the widget tree.** `MsgTabEntry`
holds ≤50 `MsgBubble`s at all times (`msg_tabs_trim_history`), and `persisted_msgs` is separately
capped at 1000. But the **live widget children of `msg_list`** are only ever pruned by
`msg_list_clear()` → `lv_obj_clean(msg_list)`, which is called exclusively from
`msg_render_active_tab()` (full resync on tab switch) and `msg_list_show_hint()`. The
"already active, just append" branch of `msg_tabs_add_message()`
(`lv_obj_functions.cpp:2789-2799`) calls only `msg_list_append_bubble()` — it never deletes an old
child even though the model it was built from was just trimmed to 50 entries one call earlier
(line 2738, _before_ the branch that decides to append). **Consequence**: a conversation tab that
stays the active tab while receiving a sustained stream of messages will have its `lv_obj` child
count under `msg_list` grow past 50 and keep growing for as long as that tab stays selected; the
mismatch only self-heals the next time any tab switch triggers `msg_render_active_tab()`. This
matches the shape of the H1 finding referenced in `docs/tdeck-gui-verdict.md` ("msg_list grows
without bound while the model stays at 50") — this track confirms the code-level mechanism.

**Source**: `src/t-deck/lv_obj_functions.cpp:87,153-260,1435-1448,2513-2856,4049-4076,4179-4270`.

### 8. Scroll, popup and image usage

**Scrollbars**: `LV_SCROLLBAR_MODE_OFF` is used pervasively on `lv_textarea`/single-line inputs
(≥16 sites, e.g. `lv_obj_functions.cpp:462,707,757,834,858,882,938,1074,...`) and on
`msg_tab_bar` (`2454`); `LV_SCROLLBAR_MODE_AUTO` is used on multi-line/longer inputs
(`setup_wifissid`, `setup_wifipassword`, `setup_utc`, `setup_txpower`, `setup_stone`,
`setup_mtone`) and on `msg_list` itself (`1445`). No other scrollbar mode
(`LV_SCROLLBAR_MODE_ON`/`ACTIVE`) appears anywhere.

**Modal/msgbox usage**: none. `lv_msgbox_create` (LVGL built-in) is never called. The custom
`void lv_msgbox(char*, char*)` declared in `lv_obj_functions.h:19` has no matching definition —
dead API (see Widget census, Finding 2). Do not assume any confirmation dialog exists; anything
resembling a "toast" is the ad-hoc `track_ta` "POSITION SENT" text swap with a 2 s
`lv_timer_create` auto-clear (`tdeck_send_track_view()`, `lv_obj_functions.cpp:4027-4047`), or the
System-type `MsgBubble`s appended to the message list itself (Finding 7).

**`lv_img` sources**: two. `map_ta` (`lv_obj_functions.cpp:1477`) is driven by
`sdmap_refresh()`/`lv_img_set_src(img, &sdmap_dsc)` (see below); `trackball_cursor_obj`
(`tdeck_main.cpp:428`) uses the compiled-in `mouse_cursor_icon` (`LV_IMG_DECLARE`, sourced from
`src/t-deck/mouse_cursor_icon.c`) and is shown/hidden (`LV_OBJ_FLAG_HIDDEN`) based on trackball
activity with a 750 ms auto-hide (`TRACKBALL_CURSOR_SHOW_TIME_MS`, `tdeck_main.cpp:74`, driven from
`mouse_read()`, the indev read callback — so this hide/show also only ever runs on the main task,
via `lv_task_handler()`'s indev polling).

**Map tile rendering path** (`src/t-deck/tdeck_sdmap.cpp`): `sdmap_refresh(lv_obj_t *img, double
lat, double lon)` (line 162) computes a slippy-map tile index at the current zoom, walks _down_
zoom levels on an `SD.exists()` miss until it finds a tile file (`/maps/<set>/<zoom>/<x>/<y>.png`),
reads the whole PNG file into a `malloc()` buffer, decodes it with `lodepng_decode32()` (declared
`extern "C"`, no header include — the vendored lodepng functions are called via a hand-written
forward declaration, `tdeck_sdmap.cpp:9-13`), converts RGBA8888 → native `lv_color_t` pixel-by-
pixel into a **`ps_malloc()`'d** buffer (`newbuf`, PSRAM), calls
`lv_img_cache_invalidate_src(&sdmap_dsc)` before swapping the static `lv_img_dsc_t sdmap_dsc`'s
`data`/`data_size`/`w`/`h` fields, frees the previous tile buffer, and finally
`lv_img_set_src(img, &sdmap_dsc)`. On any failure path (`SD.exists` miss at every zoom,
`SD.open` failure, `malloc` failure, `lodepng` decode error, `ps_malloc` failure) it instead
`lv_obj_clear_flag(map_no_data_label, LV_OBJ_FLAG_HIDDEN)` to show a "no data" label. Every call to
`sdmap_refresh()` this track found originates from the main task (tab-switch handler
`tabview_event_cb`, the zoom in/out button handlers, and the SYM+O/SYM+I keypad shortcuts in
`keypad_read()`) — consistent with Finding 5's overall conclusion. SD-card reads here are not
guarded by `xSemaphore` (see Finding 5's semaphore table).

**Source**: `src/t-deck/lv_obj_functions.cpp` (scrollbar-mode grep, msgbox grep), `tdeck_sdmap.cpp`
(full read), `event_functions.cpp:834-883` (zoom handlers), `tdeck_main.cpp:636-678` (keypad SYM+O/
SYM+I).

### 9. Existing instrumentation

Two independent, currently-coexisting instrumentation layers feed
`tools/bench/tdeck_parse.py`/`tdeck_harness.py`. **Extend these, do not add a third.**

**Layer 1 — `src/t-deck/tdeck_debug.cpp`/`.h`** (T-Deck-specific, board-gated
`#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`):

- `tdeck_dbg_redrawlog(bool on)` / `tdeck_dbg_redrawlog_enabled()` — runtime on/off gate for the
  `[REDRAW]`/`[REFR]`/`[REFRSTART]` lines; counters (`s_inv_total`, `s_refr_total`,
  `s_last_refr_px`, `s_last_refr_ms`) accumulate **regardless** of the gate.
- `lv_obj_invalidate_hook(obj, area, ret_addr)` — a **strong override of LVGL's own weak hook**,
  called from inside `lv_obj_invalidate_area()` in `lib/lvgl/src/core/lv_obj_pos.c` for _every_
  invalidate LVGL performs (not just the app's one explicit call, Finding 3). Rate-capped at 200
  lines/sec (`REDRAW_RATE_CAP`); overflow is summed and reported once per second as
  `[REDRAW];dropped;<n>`. Classifies the object via `lv_obj_get_class()` pointer comparison
  (`classify_obj`, `tdeck_debug.cpp:53-71`: `obj/label/img/btn/ta/tabview/btnm/list/dd/sw/slider/
cb/line/canvas`, `?` otherwise — note `sw`/`slider`/`canvas`/`list` classes are recognized by
  this classifier even though the app never creates those widget types, Finding 2) and identifies
  `tv`/`msg_list`/`map_ta` by pointer via `known_name()`. Emits an Xtensa backtrace
  (`collect_backtrace`, 8 frames, skips the hook + `lv_obj_invalidate_area` frames) as
  comma-separated `0x`-hex addresses for `addr2line`.
- `tdeck_dbg_monitor_cb(disp_drv, time_ms, px)` — wired as `disp_drv.monitor_cb` in
  `setupLvgl()` (`tdeck_main.cpp:399`); emits `[REFR]` and updates `s_refr_total`/`s_last_refr_px`/
  `s_last_refr_ms`.
- `tdeck_dbg_render_start_cb(disp_drv)` — wired as `disp_drv.render_start_cb`
  (`tdeck_main.cpp:400`); emits `[REFRSTART]` with the disp's pending invalidated-area count
  (`disp->inv_p`).
- `tdeck_dbg_uistat()` — one-shot snapshot: active tab, drawer open/closed, total object count
  (recursive `lv_obj_get_child_cnt` walk from `lv_scr_act()`), `msg_list` child count, the
  accumulated counters above, redrawlog gate state, `ESP.getFreeHeap/getMinFreeHeap/getFreePsram`,
  TFT sleep state + brightness level, `msg_list`'s scroll_y/scroll_bottom and its own vs. its last
  child's `coords.y1/y2`, and `lv_disp_get_ver_res(NULL)`.
- `tdeck_dbg_tab_list()` / `tdeck_dbg_tab(int idx)` — list the 8 tabs (`TAB_NAMES[]`) or switch
  to one via `lv_tabview_set_act`, reporting the invalidate-count delta caused by the switch.
- `tdeck_dbg_drawer(bool open)` — opens/closes the header drawer via
  `tdeck_show_tab_menu()`/`tdeck_toggle_tab_menu()`.
- `tdeck_dbg_tft(int mode)` — `1`→`tft_on()`, `0`→`tft_off()`, `2`→state-only; always prints
  `[TFT]`.
- `tdeck_dbg_screencrc()` — reads back the **panel frame memory itself** (not the LVGL draw
  buffer) in 8 horizontal 320×30 bands via `tft.readRect()`, under `xSemaphore`, with `TDECK_SDCARD_CS`/`LORA_CS` forced high first to keep other SPI slaves off the shared MISO line; computes a per-band CRC32 (table-less, reflected, `0xEDB88320`) and a non-black pixel count, emits `[SCREEN]`.

All of the `Serial.printf` calls in this file use **raw `Serial.printf`, not `printfdeb`/
`printlndeb`** — i.e. they are unconditional on `bDEBUG` and not routed through whatever
`printfdeb_functions.cpp` does (log level, net-console mirroring, etc.).

**Layer 2 — `src/instrument.h`/`src/instrument.cpp`** (repo root `src/`, not under `src/t-deck/`,
explicitly marked "TEMPORARY MEASUREMENT SCAFFOLDING -- not intended for upstream", removable by
reverting one commit or setting `INSTRUMENT_ENABLED=0`; auto-enabled whenever `ESP32` is defined):

- `INSTR_T0(v)` / `INSTR_FLUSH(v)` — wrap `disp_flush()`'s body in `tdeck_main.cpp:464,469` to time
  every flush in microseconds via `instrument_note_flush()`.
- `INSTR_LOOPTICK()` — called once per `loop()` iteration at `esp32_main.cpp:1838`, measuring the
  inter-call period to catch a stalled loop regardless of which call site caused it.
- `instrument_report_heap(tag)` → `[INSTR-HEAP];<tag>;int_free;<n>;int_min;<n>;int_largest;<n>;
psram_free;<n>;psram_largest;<n>` (`instrument.cpp:76`).
- `instrument_report_timing()` → `[INSTR-FLUSH];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>` and
  `[INSTR-LOOP];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>` (`instrument.cpp:90,96`).
- `instrument_report_gui()` → `[INSTR-GUI];msg_list_children;<n>;active_tab_bubbles;<n>;
persisted_msgs;<n>;map_points;<i>` on T-Deck boards, else
  `[INSTR-GUI];not_available_on_this_board` (`instrument.cpp:108,114`).
- `instrument_reset()` → `[INSTR];reset` (`instrument.cpp:68`).
- All of these go through `printfdeb()` (`printfdeb_functions.h`), **unlike** Layer 1's raw
  `Serial.printf` — worth knowing if `bDEBUG`/log-level gating ever suppresses one but not the
  other.

**What `tools/bench/tdeck_parse.py` expects** (from its own docstring, `tdeck_parse.py:10-41`,
this is the authoritative grammar, reproduced here so the coding agent doesn't have to
cross-reference two files): `[REDRAW];ms;<n>;obj;0x<hex>;cls;<name>;area;<x1>;<y1>;<x2>;<y2>;ra;
0x<hex>[;bt;<hex,hex,...>][;name;<tv|msg_list|map_ta>]`, `[REDRAW];dropped;<n>`, `[REFR];ms;<n>;
px;<n>;t_ms;<n>`, `[REFRSTART];ms;<n>;areas;<n>`, `[UISTAT];tab;<n>;drawer;<0|1>;objs;<n>;
msg_list;<n>;inv_total;<n>;refr_total;<n>;last_refr_px;<n>;last_refr_ms;<n>;redrawlog;<0|1>;
heap_free;<n>;heap_min;<n>;psram_free;<n>[optionally trailing tft_sleeping/bl/scroll_y/
scroll_bottom/ml_y1/ml_y2/last_y1/last_y2/scr_h]`, `[TAB];<idx>;<name>` / `[TAB];active;<idx>` /
`[TAB];set;<idx>;inv_delta;<n>;` / `[TAB];err;range`, `[DRAWER];<0|1>`, `[INJECT];ok;id;<hex>;
dst;<s>;src;<s>;len;<n>` / `[INJECT];err;<reason>`, `[AUDIO];play;<what>[;vol;<n>]` /
`[AUDIO];err;<kind>;<detail>` / `[AUDIO];info;<text>` / `[AUDIO];eof;<file>`,
`[INSTR-HEAP];<tag>;int_free;<n>;int_min;<n>;int_largest;<n>;psram_free;<n>;psram_largest;<n>`,
`[INSTR-FLUSH];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>`,
`[INSTR-LOOP];n;<n>;total_us;<n>;avg_us;<n>;max_us;<n>`,
`[INSTR-GUI];msg_list_children;<n>;active_tab_bubbles;<n>;persisted_msgs;<n>;map_points;<i>`,
`[TFT];sleeping;<0|1>;bl;<n>;timer_age_ms;<n>`, `[SCREEN];ms;<n>;crc;<hex0>,...,<hex7>;
nonblack;<n>;total;76800;t_ms;<n>[;sleeping;1]` / `[SCREEN];err;<reason>`, `[BOOT];msg;<text>` /
`[BOOT];audio;file;<name>|cw|none` / `[BOOT];init;sd;<0|1>;touch;<0|1>;kb;<0|1>;psram_buf;<0|1>;
t_ms;<n>`. `parse_line()` is defensive: an unrecognized `[TAG]` or malformed field list returns
`None` rather than raising, and it skips any prefix before the first `[` (handles the firmware
echoing a command with no trailing newline before its reply). **If the coding agent adds a new
debug command/line, it must add a matching `_parse_*` entry to `_DISPATCH` in `tdeck_parse.py`
or the harness will silently drop the new line as unrecognized.**

**Source**: `src/t-deck/tdeck_debug.cpp` (full read), `src/t-deck/tdeck_debug.h` (full read),
`src/instrument.h` (full read), `src/instrument.cpp:60-115` (printf calls), `src/command_functions.cpp:4505-4590` (serial command wiring), `tools/bench/tdeck_parse.py:1-49` (docstring/grammar).

## Rules to hand the coding agent

1. Before relying on **any** `lv_conf.h` fact (refresh period, font set, `LV_SPRINTF_CUSTOM`,
   `LV_COLOR_16_SWAP`, default font), empirically determine which of `src/t-deck/lv_conf.h` or
   `variants/t_deck_plus/lv_conf.h` the `t_deck_plus` PlatformIO env actually includes — e.g. by
   temporarily adding a distinct `#pragma message` or `#error` to each candidate and building
   `pio run -e t_deck_plus`, or by inspecting `pio run -e t_deck_plus -v`'s compiler invocation for
   the `-I` order. Do not edit both files "to be safe" without first confirming which one is live —
   editing the dead one is a silent no-op.
2. Treat "which task calls this `lv_*` function" as already-answered for the code paths in
   Finding 5 (main task only) — do not add a mutex/queue "fix" for a race this track found no
   evidence of, without first identifying the specific new call site that would introduce one.
3. When fixing the message-redraw symptom, look first at Finding 7.2 (widget-tree growth while a
   tab stays active) and the fact that the redraw of a newly-appended message currently depends on
   the _next_ `lv_task_handler()` call happening promptly afterward in the same `loop()` iteration
   — not at cross-task signaling.
4. When fixing the "audio blocks the UI" symptom, start at `msg_focus_and_alert()`'s
   `play_cw('r')` fallback (Finding 4.1) — this is a real, synchronous, main-task-blocking call
   path, unlike the properly-backgrounded `play_file_from_sd()` async path.
5. Any new debug/instrumentation line must be added to `tools/bench/tdeck_parse.py`'s
   `_DISPATCH` table (Finding 9) or the harness will not see it.
6. Any new widget style referencing `&lv_font_montserrat_N` must first confirm that size is
   enabled in whichever `lv_conf.h` wins (Rule 1) — do not add a new font-size reference without
   checking, given 16 existing references already sit on this exact fault line.
7. Do not assume `xSemaphore` protects SD-card access — it currently only wraps TFT SPI transfers
   (Finding 5's semaphore table); `tdeck_sdmap.cpp`'s `SD.open`/`SD.read`/`SD.exists` calls run
   unguarded, safe today only because everything is single-task.

## Open questions / UNVERIFIED

- **Which `lv_conf.h` actually compiles for `t_deck_plus`** (Finding 10.1 / 6). This track's best
  supported hypothesis, based on the code's font usage (16 live references to sizes 12/14/16/18,
  zero to 28) matching `variants/t_deck_plus/lv_conf.h`'s enabled set and _not_
  `src/t-deck/lv_conf.h`'s, is that `variants/t_deck_plus/lv_conf.h` is the winning file — but this
  is inferred from indirect evidence (the firmware must build), not from inspecting an actual
  compiler invocation, which this audit-only track was not permitted to run.
- Whether any file under `src/` outside the ones this track read (roughly `src/t-deck/*`,
  `src/esp32/esp32_audio.cpp`, `src/esp32/esp32_main.cpp`, `src/loop_functions.cpp`,
  `src/command_functions.cpp`, `src/net_console.cpp`, `src/test_inject.cpp`,
  `src/instrument.{h,cpp}`) calls an `lv_*` function from a task other than main — not
  exhaustively ruled out (Finding 5's caveat).
- Whether `msg_list_append_bubble()`'s full body (this track read through
  `lv_obj_functions.cpp:2856`, not the remainder of the bubble-construction code past that point)
  does anything else relevant to redraw correctness beyond what's summarized in Finding 7 — the
  header/body-label styling detail past line ~2900 was not read in full.
- Whether `tdeck_clear_text_ta()` (Finding 4, site 3) is truly unreachable dead code, or whether
  some caller exists outside the paths this track's greps covered (e.g. a not-yet-wired UI button,
  or a caller added in a very recent uncommitted change — the working tree was reported clean at
  session start).

## Sources

All findings are from direct reads/greps of this repository at the commit checked out at session
start (`tdeck-partial-refresh-trace`, HEAD `0a11757c`); no web sources were used for this track.

- `src/t-deck/tdeck_main.cpp` — full read
- `src/t-deck/lv_obj_functions.cpp` — targeted reads + greps (153 KB / 4337 lines; not read
  end-to-end per the track brief)
- `src/t-deck/lv_obj_functions.h`, `lv_obj_functions_extern.h` — full read
- `src/t-deck/event_functions.cpp`, `event_functions.h` — full read
- `src/t-deck/tdeck_debug.cpp`, `tdeck_debug.h` — full read
- `src/t-deck/tdeck_helpers.cpp`, `tdeck_extern.cpp`, `tdeck_extern.h`, `tdeck_sdmap.cpp` — full read
- `src/t-deck/lv_conf.h`, `variants/t_deck_plus/lv_conf.h`, `variants/t_deck/lv_conf.h` — full read/diff
- `src/esp32/esp32_audio.cpp`, `esp32_audio.h` — full read
- `src/esp32/esp32_main.cpp` — targeted reads + greps
- `src/net_console.cpp`, `src/test_inject.cpp`, `src/instrument.h`, `src/instrument.cpp` — full/near-full read
- `src/loop_functions.cpp`, `src/command_functions.cpp` — targeted greps/reads
- `variants/t_deck_plus/configuration.h`, `variants/t_deck_plus/platformio.ini`, `platformio.ini` — full read
- `lib/lvgl/src/font/lv_font.h`, `lib/lvgl/src/font/lv_font_montserrat_18.c`, `lib/lvgl/lvgl.h` — targeted reads
- `tools/bench/tdeck_parse.py` — full read
