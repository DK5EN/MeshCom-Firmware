# Code Review — T-Deck Plus GUI layer (2026-08-28)

Target: `src/t-deck/` (7 798 lines; `lv_obj_functions.cpp` 4 313, `event_functions.cpp` 985,
`tdeck_main.cpp` 808, `tdeck_sdmap.cpp` 305, `tdeck_helpers.cpp` 277). Rule taxonomy:
[`codequality-rules.md`](codequality-rules.md); defect classes carried forward from
[`code-audit-20260626.md`](code-audit-20260626.md).

Occasion: the device (`DK5EN-14`) was handed over by the upstream FW maintainer, who was hunting an
unlocated heap defect in the T-Deck. The brief was therefore not "find that bug" but "find that
**class** of bug". Bring-up context and hardware notes: [`BACKLOG.md`](BACKLOG.md) §3.8a/§3.8b.

Hardware: ESP32-S3, 16 MB flash, 8 MB OPI PSRAM, LVGL 8.3.11, SD on SPI shared with TFT and LoRa.
Measured free internal heap in steady state: **~95 KB** (`[HEAP] … 95 936 90 252 81 908`).

## Verification status

Findings marked **[verified]** were re-checked by hand against the tree, independent of the review
agent, before this document was written. The remainder are reported as the agent found them and are
**not** yet independently confirmed.

| #   | Sev.    | File:line                   | Finding                                                             | Topic |
| --- | ------- | --------------------------- | ------------------------------------------------------------------- | ----- |
| G01 | HIGH    | `lv_obj_functions.cpp:1777` | Dangling LVGL pointers → use-after-free **[verified]**              | TD-03 |
| G02 | HIGH    | `lv_obj_functions.cpp:1723` | `ic <= MAX_MAP` reads one past `strMaps[5]` **[verified]**          | —     |
| G03 | HIGH    | `lv_obj_functions.cpp:3570` | Lat/lon hemisphere signs swapped; both branches dead **[verified]** | —     |
| G04 | MEDIUM  | `lv_obj_functions.cpp:183`  | Unbounded Arduino `String` growth on internal heap **[verified]**   | TD-03 |
| G05 | MEDIUM  | `tdeck_main.cpp:600`        | SD I/O + PNG decode + ~870 ms `delay()` inside LVGL `read_cb`       | TD-05 |
| G06 | MEDIUM  | `tdeck_main.cpp:262`        | `addMessage()` busy-blocks the main loop for 2 s                    | TD-05 |
| G07 | MEDIUM  | `tdeck_main.cpp:351`        | LVGL draw-buffer size passed in bytes, not pixels                   | —     |
| G08 | MEDIUM  | `tdeck_main.cpp:337`        | Unchecked `malloc` fallback → NULL deref at boot                    | —     |
| G09 | MEDIUM  | `tdeck_sdmap.cpp:216`       | Unbounded, unvalidated allocation from SD file size                 | TD-04 |
| G10 | MEDIUM  | `tdeck_sdmap.cpp:52`        | Active map set clamped to compile-time, not discovered, count       | TD-04 |
| G11 | MEDIUM  | `tdeck_sdmap.cpp:186`       | Stale current-tile state suppresses retry after failure             | TD-04 |
| G12 | MEDIUM  | `event_functions.cpp:598`   | `memcmp` reads 49 bytes past `node_passwd[15]`                      | —     |
| G13 | MEDIUM  | `event_functions.cpp:652`   | Uninitialised `iNewPower` persisted to flash                        | —     |
| G14 | MEDIUM  | `lv_obj_functions.cpp:4290` | Timestamp format mismatch makes clock restore dead code             | —     |
| G15 | LOW/MED | `tdeck_main.cpp:113`        | Binary semaphore as SPI mutex, taken with `portMAX_DELAY`           | —     |
| G16 | LOW     | `lv_obj_functions.cpp:3642` | `millis()` wraparound comparison (STAB-05)                          | —     |

## The heap question (TD-03)

Two independent mechanisms, both plausible causes of the maintainer's report.

### G01 — dangling LVGL object pointers → use-after-free [verified]

`add_map_point()`. When the callsign already exists in `map_point_call[]`, lines 1756/1763 call
`lv_obj_del(map_point[ip])` and `lv_obj_del(map_point_label[ip])` but **do not NULL the slots**.
Control then reaches the early return at 1777:

```c
if (!sdmap_in_current_tile(dlat, dlon))
    return;                      // slots still hold freed pointers
```

Reproduction: a station beacons a position inside the current tile (objects created) → moves out of
the tile → beacons again (objects deleted, early return, slots dangle) → beacons again
(`lv_obj_del()` on freed objects).

The correct pattern already exists **in the same function**, on the ring-eviction path at 1800-1813,
which does set `map_point_call[…] = ""` and `map_point[…] = NULL` after deleting. The `bFound` path
simply omits it.

Severity is raised by the allocator: `lv_conf.h:64` sets `LV_MEM_CUSTOM_ALLOC ps_malloc`, so this
corrupts the **PSRAM** heap, where the LVGL draw buffer and the map tile also live.

Fix: NULL `map_point[ip]`, `map_point_label[ip]` and `map_point_call[ip]` immediately after the
deletes, before any early return.

### G04 — unbounded Arduino `String` growth on the internal heap [verified]

`lv_obj_functions.cpp:184`:

```c
static std::vector<std::pair<String, MsgBubble>> persisted_msgs;
static const size_t PERSISTED_MSG_LIMIT = 1000;
```

`MsgBubble` carries four `String`s (`header`, `timestamp`, `body`, `gps`), plus the pair's key
`String` — **five heap allocations per message, up to 5 000 blocks** at the limit. Arduino `String`
uses plain `malloc`/`realloc`, so all of this lands on the **internal** heap, not PSRAM — the same
~95 KB pool that must also fund BLE and WiFi.

On top of that, each rendered bubble `new`s a `HeaderEventData` (2 Strings) and a `DeleteEventData`
(4 Strings) at lines 2908/2932, released only when the LVGL object is deleted. Re-rendering a
50-message tab churns roughly 300 String allocations per render.

This is the most plausible cause of the reported heap exhaustion, and it contradicts MEM-01/MEM-03
directly. Note the asymmetry that makes it easy to miss: LVGL's own objects go to PSRAM, so PSRAM
telemetry looks healthy while the internal heap is the one under pressure.

Fix direction: a fixed-size ring of `char[]` records, or move the bodies to PSRAM.

## GUI latency (TD-05)

### G05 — blocking work inside an LVGL input-device read callback

`keypad_read()` (`tdeck_main.cpp:600`) is an `indev.read_cb`, invoked from `lv_task_handler()` on
every refresh. It calls `sdmap_zoom_in()/out()` then `sdmap_refresh()` — SD open, `malloc`, full PNG
decode, `ps_malloc` — then `refresh_map()`, which loops `MAX_POINTS = 30` slots calling
`add_map_point()`, each containing `delay(19)` (line 1758) and `delay(10)` (line 1806).

Worst case: **~870 ms of pure `delay()`** plus SD I/O, inside a read callback, with the display
frozen and the shared SPI bus held. Fix: defer to a flag processed outside `lv_task_handler()`, and
remove the `delay()` calls.

### G06 — `addMessage()` busy-blocks the caller for 2 s

`tdeck_main.cpp:262`: `uint32_t run = millis() + 2000; while(...) { lv_task_handler(); delay(5); }`.
Called from the main loop on WiFi/GPS state changes (`esp32_main.cpp:863, 865, 1755, 1761, 1768`), so
each event stalls the loop — and therefore LoRa RX servicing — for two seconds.
`tdeck_addMessage()` stacks four of them, i.e. **8 s at boot**. Both a latency and a packet-loss
source (STAB-03).

## SD map (TD-04)

These bear directly on installing the Europe tile set, and should be fixed before a large card is
populated.

- **G09** — `size_t fsize = f.size(); malloc(fsize)` with no upper bound and no zero check.
  `lodepng_decode32` then allocates a second `4 * w * h` buffer, also unbounded (a 1024×1024 PNG is
  4 MB). Even the success path costs ~256 KB of internal heap transiently **per tile change**.
  Cap at `SDMAP_MAX_PNG_BYTES`, reject `fsize == 0`, validate `pngW/pngH == SDMAP_TILE_PX`, prefer
  `ps_malloc` for `pngRaw`.
- **G10** — `sdmap_set_active_set()` clamps to `SDMAP_SET_COUNT - 1` (4) rather than to the
  `sdmap_setCount` actually discovered. With two sets on the card and a persisted `node_map` of 3,
  the node builds paths from an empty `sdmap_dirs[3]` → `"/0/0/0.png"`, the map silently never
  loads, and zoom is dead.
- **G11** — every failure path in `sdmap_refresh()` returns without resetting
  `sdmap_currentTileX/Y`, so `sdmap_in_current_tile()` keeps answering `true` for the stale
  position and no reload is attempted. Set both to `-1` on each failure return.

## Correctness

- **G02** [verified] — `getMapDropboxID()` loops `ic <= MAX_MAP` over `String strMaps[MAX_MAP]`
  (`tdeck_extern.cpp:21`, 5 elements), calling `compareTo()` on the object past the end. The
  sibling `getMapID()` at line 1661 has the correct `ic < MAX_MAP`. (BND-05)
- **G03** [verified] — `tdeck_add_pos_point()` tests `if(lat_c == 'W')` and `if(lon_c == 'S')`.
  `aprs_functions.cpp:1172-1176` guarantees `lat_c ∈ {N,S}` and `lon_c ∈ {E,W}`, so **both branches
  are dead**: every southern-hemisphere station is plotted in the north, every western station in
  the east. Repeated verbatim in `tdeck_add_to_pos_view()` at 3749/3753.
- **G14** — `parseTimestamp()` requires `length() == 19` and `YYYY.MM.DD HH:MM:SS`, but
  `build_timestamp_string()` (line 2388) emits `DD.MM.YY HH:MM`, 14 chars. So
  `getLatestMessageTimestamp()` always returns 0 and the boot-time clock recovery in
  `time_functions.cpp:136` silently never uses message history.

## Robustness

- **G07** — `lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_BUFFER_SIZE)`: the 4th parameter is
  `size_in_px_cnt`, but `LVGL_BUFFER_SIZE` is `TFT_WIDTH * TFT_HEIGHT * sizeof(lv_color_t)` — twice
  the pixel count. Currently masked by `buf2 == NULL` and `full_refresh = 1`; becomes a ~150 KB
  overflow the moment either changes. Pass `TFT_WIDTH * TFT_HEIGHT`.
- **G08** — if `ps_malloc(LVGL_BUFFER_SIZE)` fails, the fallback `malloc()` of 153 600 bytes of
  internal RAM is almost certain to fail too, and is never re-checked; `buf` reaches
  `lv_disp_draw_buf_init()` as NULL and the first render writes to address 0.
- **G12** — `char cNewPassword[64]` compared against `node_passwd` (`char[15]`,
  `esp32_flash.h:105`) with `memcmp(..., sizeof(cNewPassword))`, reading 49 bytes of adjacent struct
  members. "Password unchanged" is therefore decided partly by unrelated settings. (BND-04)
- **G13** — `int iNewPower;` then `sscanf(cNew, "%i", &iNewPower)` with no return check; on empty or
  non-numeric input the stack garbage is written to `meshcom_settings.node_power` and persisted.
- **G15** — `xSemaphoreCreateBinary()` guards the shared TFT/SD/LoRa SPI bus and is taken with
  `portMAX_DELAY` in `disp_flush()` (line 419). RACE-02 requires `xSemaphoreCreateMutex()` (priority
  inheritance); RACE-09 forbids `portMAX_DELAY`. If the holder is lost, `lv_disp_flush_ready()` is
  never called and LVGL wedges permanently with no diagnostic.
- **G16** — `if(lastsavePOSPersistence + 30000 > millis())` is the exact form STAB-05 forbids.

## Checked and cleared

Recorded so they are not re-raised:

- Deleting `del_btn` from inside its own `LV_EVENT_CLICKED` handler is safe — LVGL 8.3.11 guards it
  via `_lv_event_mark_deleted()` (`lv_event.c:151`), and the handler does not touch `data` after the
  render.
- The dual `lv_obj_add_event_cb(..., ded)` registrations for `LV_EVENT_DELETE` and
  `LV_EVENT_CLICKED` are not a double-free: `event_send_core` matches on the per-descriptor filter.
- `map_no_data_label` is a child of `t7`, not `map_ta`, so `lv_obj_clean(map_ta)` (line 1933) does
  not leave it dangling.
- `add_map_point()`'s ring index cannot exceed `MAX_POINTS - 1`.

## Status

No code changed. This is stage 1 of a two-stage review; stage 2 (`/fable-review`, adversarial
verification with advisor gating) is still to run. Nothing here has a regression test yet — per the
campaign rule, every fix needs one that fails before and passes after.
