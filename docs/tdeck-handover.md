# T-Deck Plus — handover to the fix team

Written 2026-08-28 at the end of a bring-up and analysis session. **The working tree is left dirty
on purpose** (see §6). Read §1 and §6 before touching anything.

Companion documents, all committed:

| Document                                                       | Contents                                    |
| -------------------------------------------------------------- | ------------------------------------------- |
| [`tdeck-status-20260828.md`](tdeck-status-20260828.md)         | session status, commit list, refuted claims |
| [`tdeck-baseline-20260828.md`](tdeck-baseline-20260828.md)     | all measurements, before/after              |
| [`review-tdeck-gui-20260828.md`](review-tdeck-gui-20260828.md) | stage 1 review, 16 findings                 |
| [`tdeck-gui-verdict.md`](tdeck-gui-verdict.md)                 | stage 2 review, verdict + refuted claims    |
| [`BACKLOG.md`](BACKLOG.md) §3.8a-e                             | backlog entries HL-, TD-, G-                |

---

## 1. The goal

Everything below serves one target: **a full UI regression test that runs over USB serial and is
readable without ever looking at the screen.**

1. **The UI must update fast.** Measured baseline: a full-screen SPI flush costs 36.65 ms and fires
   ~3x/second at idle — 11.5 % of wall-clock time, blocking, while holding the SPI semaphore that
   also fronts SD and the LoRa radio.
2. **The WiFi defect must be fixed.** Every node is absent from the network for ~5 minutes after
   each power-on (§4, TD-01).
3. **Memory must be verifiable.** Two heap candidates were investigated and both failed to explain
   the defect the upstream maintainer reported. It is still unlocated (§4).
4. **Full logging over USB serial**, machine-readable.
5. **Everything must be drivable and observable from a script**, with no human at the display.

### 1.1 Commands the firmware needs and does not have

This is the concrete deliverable. None of these exist today:

| Command (suggested)          | Purpose                                                                                                                   |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `--injectmsg <group> <text>` | Inject a message as if it had arrived over LoRa. Removes the second node and the 15-20 s LoRa spacing from every UI test. |
| `--tab <n>` / `--tab list`   | Switch tabs. Must print which redraw path ran, which flags were set, which objects were invalidated.                      |
| `--drawer on/off`            | Open and close the tab drawer, same debug output.                                                                         |
| `--playtone start            | msg                                                                                                                       | <file>` | Trigger boot tone and message tone. **Must print an explicit error when the file is missing or the codec is unsupported** — today a missing file is silent. |
| `--uistat`                   | Object counts, dirty-area count, last flush area, per-tab render state.                                                   |
| `--redrawlog on/off`         | Per-invalidation trace: which object, which area, which call site.                                                        |

The last one is the important one. Every remaining defect in §3 is a _missing invalidation_, and
none of them are visible from outside today. Without a redraw trace, the fix team is in the same
position we were: changing something, flashing, and asking a human what the screen looked like.

### 1.2 Hard constraint: USB serial resets the T-Deck Plus

**Opening the USB serial port resets the device** — `rst:0x15 (USB_UART_CHIP_RESET)`. This is the
ESP32-S3 native USB-Serial/JTAG peripheral, and it happens with `dtr=False, rts=False`, and with the
repo's own `tools/bench/serial_session.py`, which documents the opposite ("dtr/rts False so ESP32
boards don't reset"). That claim holds for CP2102 boards such as the Heltec V3, **not** for the
T-Deck Plus.

Practical consequence for the test harness: **hold one serial session open for the whole run.** The
first open resets the device, which is what a test wants anyway (clean boot); every command after
that is free. Do not open and close per command — that is a reboot per command.

The alternative channel is the **net console on TCP 2323** (`--netconsole on`,
`tools/hmac_connect.py`), which does not reset the device. It is single-client. It was the only way
to observe state during this session.

---

## 2. Read this before planning the work

**The defect-checklist approach was not sufficient.** Two review stages ran against
`docs/codequality-rules.md` — a `/code-review medium` and a 7-finder `/fable-review` with
adversarial verification. Together they produced ~36 findings, and the verification pass was
genuinely valuable: it killed several plausible-but-wrong claims.

But **none of the three problems that actually block the device today were found by either stage.**
They were found by measuring, and by the operator using the device. Worse, several review findings
were confidently wrong about _mechanism_ while being right about _code_:

- G06 was reported as "called from the main loop, stalls it for 2 s". Every caller is in
  `esp32setup()`.
- H1 was reported as internal-heap exhaustion with `abort()` at ~390 messages. It is PSRAM-only,
  with ~2 800 messages of headroom.
- G09 was reported as ~256 KB of internal heap per tile change. It is PSRAM.

**What is missing is an analysis of the GUI's redraw contract**, not more defect classes. The
central fact — that essentially the whole T-Deck GUI relies on the display being fully repainted
several times a second, and almost no widget announces its own dirty area — does not appear in
either review. It only became visible when full-screen repainting was switched off.

Suggested framing for the next analysis: _for every screen element, what invalidates it, and who
guarantees that?_ That question, asked systematically across `src/t-deck/`, is the work.

---

## 3. The three blocking problems

### 3.1 Map view — wrong section shown, GPS position off-screen

**Symptom (operator):** open the drawer, tap the map icon — nothing appears. Move the trackball and
the map emerges only where the cursor passed. Open the drawer again and the map shifts down by the
drawer's height and finally draws. Pressing `+` lands somewhere in Croatia instead of Munich.

**What is NOT the cause — verified, do not re-investigate:**

- Tile loading is correct. The node had a valid fix at 48.4076 N / 11.7386 E (Freising), and the
  tiles for that position exist at **every** zoom level on the card (checked z0-z9 against the
  archive).
- `sdmap_project()` and the Web-Mercator maths are correct.
- The tiles are 256x256, matching `SDMAP_TILE_PX`.

**Actual cause:** `map_ta` is sized `SDMAP_TILE_PX` (256x256), while the visible tab area is only
about `LV_VER_RES * 0.72` ≈ 172 px tall. The 84 px overhang becomes a scroll region, and the tile
sat rigidly at its start. Depending on where in the tile the position falls, the blue dot is below
the visible edge — so the user sees an arbitrary crop, and zooming appears to wander across Europe.
The drawer consumes further height on top of that.

**Attempted fix, in the working tree, UNVERIFIED:** centre the viewport on the home marker in
`add_map_point()` (`lv_obj_scroll_to()` on `map_ta`'s parent). It was flashed but never confirmed by
the operator before the session ended. **Assume nothing about it.**

### 3.2 Incoming messages do not trigger a redraw — REGRESSION WE INTRODUCED

**Symptom (operator, found last):** a newly arrived message is not displayed. It becomes visible
only after the screen is moved by opening the tab drawer.

**This is a regression caused by the uncommitted change in §6.** With `full_refresh = 1`, the whole
screen was repainted several times a second, so no widget ever needed to announce a dirty area.
Switching to partial refresh exposed that they do not.

This is the single most important finding of the session, and it generalises: **the map, the
message list, and probably other elements share one root cause.** It also means the 15x performance
win in §6 cannot be shipped until the redraw contract is fixed — the two are the same task.

### 3.3 No sound plays, correct format not yet found

**Status: unresolved, and the last two attempts were our error, not necessarily the firmware's.**

Two files were tried and neither produced a usable tone:

| Attempt | Format                           | Result                   |
| ------- | -------------------------------- | ------------------------ |
| 1       | MP3, mono, 22 050 Hz, 32 kbps    | noise / garbage, no sine |
| 2       | MP3, stereo, 44 100 Hz, 128 kbps | no sound at all          |

Known facts for the next attempt:

- The library is **`lib/ESP32-audioI2S` v2.1.0** and dispatches on the file extension
  (`Audio.cpp:682-724`): `.mp3`, `.m4a`, `.aac`, `.wav`, `.flac`.
- The I2S path is installed at **`.sample_rate = 16000`, `I2S_BITS_PER_SAMPLE_16BIT`,
  `.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT`** (`esp32_audio.cpp:524-539`).
- `play_file_from_sd()` appends `.mp3` only when the name contains no dot
  (`esp32_audio.cpp:83-84`), so `name.wav` is passed through unchanged.
- **A missing or unplayable file is silent.** `[AUDIO]..file X not found on SD` only appears when
  `SD.exists()` fails; a codec failure produces nothing at all. This is why `--playtone` with
  explicit error reporting is in §1.1.

**Recommended next step: try an uncompressed `.wav` at 16 000 Hz, 16-bit stereo** — it matches the
I2S configuration exactly and bypasses the MP3 decoder, which separates "wrong file" from "broken
decode path" in one test.

The operator has since cleared the tone settings and will use the built-in tone.

---

## 4. Everything identified so far

Severity is the reviewers'; **Status is what measurement actually established.**

### 4.1 Measured

| ID    | Where                        | What                                                                                               | Status                                                                                              |
| ----- | ---------------------------- | -------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| P1    | `tdeck_main.cpp:363,414-428` | `full_refresh=1` + non-DMA flush: 36.65 ms x ~3/s = 11.5 % duty, blocking, holds the SPI semaphore | measured; partial-refresh fix in tree, causes §3.2                                                  |
| TD-01 | `udp_functions.cpp:629-698`  | Boot association fails; only the 5-minute retry succeeds. Node offline ~5 min after every power-on | reproduced on **two** boards, at −47 dBm. Not RF. Leading untested hypothesis: BLE/WiFi coexistence |
| H1    | `lv_obj_functions.cpp:2770`  | Rendered message list never trimmed while the model is (view 60 vs model 50)                       | confirmed; **PSRAM** 2 760 B/msg, ~2 800 msgs headroom — not the internal-heap defect               |
| C2    | `esp32_audio.cpp:115`        | Audio semaphore released before playback; decoder buffers can be freed under the reading task      | race is real in code; **no per-playback leak measured** (two rounds, full recovery)                 |
| G07   | `tdeck_main.cpp:351`         | Draw buffer size passed in bytes, not pixels                                                       | fixed in tree (mandatory for partial refresh)                                                       |

### 4.2 Verified by reading, not yet fixed

| ID  | Where                                                    | What                                                                                                                                                      |
| --- | -------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G01 | `lv_obj_functions.cpp:1777`                              | `lv_obj_del()` without NULLing the slots, then an early return → UAF on the next beacon. The correct pattern exists 20 lines below in the same function   |
| G02 | `lv_obj_functions.cpp:1723`                              | `ic <= MAX_MAP` over a 5-element array; the sibling function 60 lines up is correct                                                                       |
| G03 | `lv_obj_functions.cpp:3570,3749`                         | `if(lat_c == 'W')` / `if(lon_c == 'S')` — both branches dead. Southern stations plotted north, western plotted east. Duplicated verbatim in two functions |
| K2  | `lv_obj_functions.cpp:3611`                              | Ring wraps to `1`, sibling wraps to `0`. Slot 0 written once at boot and never reused                                                                     |
| F1  | `lv_obj_functions.cpp:3708`                              | `posrow` uncapped when loading `/pos.dat`; `MAX_POSROW` is referenced exactly once in the whole codebase, at the site that already checks                 |
| C1  | `esp32_audio.cpp:104`                                    | Audio task created at priority **50**; `configMAX_PRIORITIES` is **25**. Silently clamped to 24 — the highest task priority in the system. Enables C2-C5  |
| C3  | `esp32_audio.cpp`                                        | `i2s_driver_uninstall()` from an LVGL button handler while the audio task sits in `i2s_write(..., portMAX_DELAY)`                                         |
| C4  | `tdeck_helpers.cpp:85`                                   | Raw `digitalWrite(TDECK_SDCARD_CS, HIGH)` outside any bus lock, mid-`ff_sd_read`                                                                          |
| G08 | `tdeck_main.cpp:337`                                     | Unchecked `malloc` fallback after `ps_malloc` fails → NULL to `lv_disp_draw_buf_init()`                                                                   |
| G10 | `tdeck_sdmap.cpp:52`                                     | Active map set clamped to the compile-time count, not the discovered count. A correctly populated card can look empty                                     |
| G11 | `tdeck_sdmap.cpp:186`                                    | Failure paths do not reset `sdmap_currentTileX/Y`, so retries are suppressed                                                                              |
| G12 | `event_functions.cpp:598`                                | `memcmp` reads 49 bytes past `node_passwd[15]`                                                                                                            |
| G13 | `event_functions.cpp:652`                                | Uninitialised `iNewPower` persisted to flash when `sscanf` fails                                                                                          |
| G14 | `lv_obj_functions.cpp:4290`                              | `parseTimestamp()` expects 19 chars, the writer emits 14 → boot-time clock recovery is dead code                                                          |
| G15 | `tdeck_main.cpp:113`                                     | Binary semaphore used as the SPI mutex, taken with `portMAX_DELAY`. Note: the bus **is** arbitrated by the Arduino HAL mutex; `xSemaphore` is vestigial   |
| G16 | `lv_obj_functions.cpp:3642`                              | `millis()` wraparound comparison                                                                                                                          |
| A1  | `event_functions.cpp:834-883`, `tdeck_main.cpp:586-623`  | Map recenter+zoom+redraw copy-pasted **4x** — a fix at one site misses three                                                                              |
| A2  | `lv_obj_functions.cpp:329-382`, `tdeck_main.cpp:565-584` | Backlight/keyboard lock implemented twice with already-diverged side effects                                                                              |
| H2  | `lv_obj_functions.cpp:184`                               | `String(String&&)` is not `noexcept`, so vector growth deep-copies every buffer while the originals live                                                  |

### 4.3 Dead code and dead configuration — cheap wins

| What                                 | Evidence                                                                                                                                                   |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `HEAP_TEST` never defined            | Six `#ifdef` sites, no `-D` anywhere. `log_message_to_sd()` is an **empty function** — there is no SD message persistence at all                           |
| `T_DECK_SPIFFS` never defined        | `save_/load_persisted_messages()` compile to nothing                                                                                                       |
| `persisted_msgs` is pure dead weight | Up to 1 000 entries accumulated on the internal heap, never written, never read back. Removable with no behaviour change                                   |
| `node_persist_to_sd` (GUI toggle)    | Controls the dead `log_message_to_sd()`                                                                                                                    |
| Two divergent `lv_conf.h`            | `src/t-deck/lv_conf.h` (period 10) is **not** the active one; `variants/t_deck_plus/lv_conf.h` (period 16) is. Verified empirically with `#pragma message` |
| No CI at all                         | Both fork workflows `disabled_manually`; `ci-build.yml` does not exist upstream. `native` (45 cases) and `native_aprs` (36) run **nowhere** automatically  |
| `src/t-deck/` test coverage          | **0 %**                                                                                                                                                    |

### 4.4 Serial-unreachable settings

Blocks the automation goal. All GUI-only:

| Setting                                                            | Note                                                                                                                                                                           |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `node_wifion` (HL-01)                                              | Only assigned in the `btn_wifi` handler. The gate that reads it is `#if`-guarded **T-Deck-only**, so other boards are unaffected — but a headless T-Deck can never join a WLAN |
| `audio_set_mute` (HL-03)                                           | No serial equivalent                                                                                                                                                           |
| `node_audio_start` / `_msg`                                        | Tone file names, Setup-tab textareas only                                                                                                                                      |
| `node_persist_to_flash` / `_to_sd` / `node_immediate_save` (HL-04) | GUI-only                                                                                                                                                                       |

Already serial-complete, do not duplicate: `--gps`, `--track`, `--webserver`, `--wifiap`, `--mesh`,
`--nomsgall`.

### 4.5 Operator requests, not defects

- **GPS tab font** too large (constant scrolling) and visually poor.
- **Map keyboard navigation**: pan with `j`/`k` and arrows. Note `SYM`+`I` zoom already exists
  (`tdeck_main.cpp:637`). Structural obstacle: the map is **tile-centred** — panning needs a map
  centre decoupled from the GPS position plus stitching 2-4 neighbour tiles, or the view runs off
  the edge after 256 px.

---

## 5. Bench setup as left

|           |                                                                                                                 |
| --------- | --------------------------------------------------------------------------------------------------------------- |
| DUT       | `DK5EN-14`, T-Deck Plus, `/dev/cu.usbmodem1101`, `192.168.68.71`, net console 2323 on                           |
| Stimulus  | `DK5EN-93`, Heltec V3, `/dev/cu.usbserial-0001`, `192.168.68.66`, net console 2323 on                           |
| Reference | `DK5EN-98`, Heltec V3, `192.168.68.56`                                                                          |
| SD card   | reformatted FAT32/MBR, 30 GB. `/maps/europe` = 5 909 tiles z0-z9, plus `c2test.mp3` (do not trust it, see §3.3) |

Operating constraints learned the hard way:

- **LoRa needs ~20 s between messages.** At 10 s spacing, 5 of 40 test messages were lost.
- **No broadcasts** — they go worldwide. Group 9999 or direct DM only.
- **Net console is single-client.**
- **macOS writes AppleDouble `._*` files onto FAT.** The first tile copy produced 6 173 of them,
  one 16 KB cluster each, ~100 MB wasted. Delete them after any copy to the card.
- The tile downloader's own size estimate was **4.9x low** (31.5 MB claimed, 153.3 MB actual), and
  the archive contained 86 duplicate paths.

---

## 6. THE TREE IS LEFT DIRTY — read before building

Three files are modified and **not committed**:

```
 M src/t-deck/event_functions.cpp    (+8)
 M src/t-deck/lv_obj_functions.cpp   (+52)
 M src/t-deck/tdeck_main.cpp         (+20/-4)
```

They contain the **P1 partial-refresh attempt**:

1. `tdeck_main.cpp` — G07 fixed (`lv_disp_draw_buf_init` gets the pixel count) and
   `disp_drv.full_refresh = 0`. The G07 fix is **mandatory** for the second change; without it,
   partial rendering overflows the buffer by ~150 KB.
2. `lv_obj_functions.cpp` — explicit `lv_obj_invalidate()` in `refresh_map()`, a whole-screen
   invalidate when the tab drawer closes, and the **unverified** viewport-centring fix from §3.1.
3. `event_functions.cpp` — whole-screen invalidate on every tab change.

**Measured effect (real, reproduced across three runs):**

| Metric                | Before    | After     | Factor |
| --------------------- | --------- | --------- | ------ |
| Blocking SPI per 60 s | 6.92 s    | 0.45 s    | 15.5x  |
| Mean flush            | 36 631 µs | 2 365 µs  | 15.5x  |
| Idle duty cycle       | 11.5 %    | 0.75 %    |        |
| Max loop stall        | 72 026 µs | 25 021 µs | 2.9x   |

**Known regressions from this change:** §3.1 (map) and §3.2 (incoming messages invisible). Tab
switching itself became, in the operator's words, "extrem schnell", and all other tabs render
correctly.

### Your options

- **Keep it** and fix the redraw contract (§2). This is the real work and it is worth doing — the
  win is large and the underlying weakness is genuine.
- **`git checkout src/t-deck/`** to get a correctly drawing device back immediately, at the cost of
  the 15x.
- **Third path, not yet tried:** revert `full_refresh` to 1 and make the flush non-blocking with
  **DMA** plus a second draw buffer in PSRAM. Rendering semantics stay identical, so no visual risk
  at all; the 36.7 ms still happens on the wire but the main loop stops waiting for it. This removes
  the measured harm without touching the invalidation contract. **Keep the G07 fix either way** —
  it is correct on its own.

### Also present but committed

`d26e39d5` added temporary instrumentation (`--heap`, `--instr`, `--instreset`) in `src/instrument.h`
and `src/instrument.cpp` plus four guarded hook sites. It is **meant to be removed** before any
upstream PR: `git revert d26e39d5` removes it completely, or define `INSTRUMENT_ENABLED=0`. Do not
remove it before the work in §1 is done — it is the only quantitative instrument available.
