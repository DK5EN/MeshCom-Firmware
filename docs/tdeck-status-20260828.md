# T-Deck Plus campaign — interim status (2026-08-28)

> **Superseded for handover purposes by [`tdeck-handover.md`](tdeck-handover.md).** That document is
> the entry point for the fix team: goal, the three blocking problems, the full item list, and the
> state of the deliberately dirty working tree. This file remains as the session record.

Resumption point. Records **what is done, what is in flight, and what is open**, so the next session
starts from this file instead of re-deriving context.

Device under test: `DK5EN-14`, T-Deck Plus, ESP32-S3, 16 MB flash, 8 MB OPI PSRAM,
`/dev/cu.usbmodem1101`, `192.168.68.71`, net console on 2323.
Stimulus node: `DK5EN-93`, Heltec V3, `/dev/cu.usbserial-0001`, `192.168.68.66`, net console on 2323.

## 1. Done

### 1.1 Commits

| Hash       | What                                                                                |
| ---------- | ----------------------------------------------------------------------------------- |
| `de12d7a3` | Backlog: MHeard-MOD collision (from another session) + GUI review stages 1+2        |
| `089b5df2` | Correction: G09 tile decode lives in PSRAM, not internal heap                       |
| `d26e39d5` | **C0 — temporary measurement instrumentation** (`--heap`, `--instr`, `--instreset`) |
| `22abfc2f` | Measurement baseline before the first fix                                           |
| `da8c3aeb` | TD-01 reproduced on a second board; HL-01 narrowed to T-Deck                        |
| `a640f8e4` | HL-01 table row aligned with the corrected prose                                    |
| `2f514847` | H1 measured on hardware — mechanism confirmed, failure mode refuted                 |

**No defect fix is committed yet.** `d26e39d5` is instrumentation only. Everything else is
documentation and measurement.

### 1.2 Hardware and infrastructure

- T-Deck Plus flashed, provisioned (`DK5EN-14`), on WLAN, net console reachable.
- **SD card rebuilt.** It previously carried a Raspberry Pi image, so the firmware had been mounting
  the 537 MB `bootfs` partition — hence `Total space: 509 MB`. Reformatted FAT32/MBR; now
  `Total space: 30417 MB`. `/maps/europe` holds **5 909 tiles, z0-z9, 203 MB**, verified loading:
  `Kachel geladen & dekodiert: /maps/europe/3/4/4.png (256x256, 131072 Bytes)`.
- Heltec V3 flashed and provisioned as `DK5EN-93`, used as the stimulus source.
- Soak sampler running against `DK5EN-14` every 15 min via net console (scratchpad `soak.py`,
  pause with `touch soak.csv.PAUSE`).

### 1.3 Review findings on record

- Stage 1 (`/code-review medium`): 16 findings — [`review-tdeck-gui-20260828.md`](review-tdeck-gui-20260828.md)
- Stage 2 (`/fable-review`, 7 blind finders + verification): [`tdeck-gui-verdict.md`](tdeck-gui-verdict.md)
- Measurements: [`tdeck-baseline-20260828.md`](tdeck-baseline-20260828.md)
- Backlog entry points: [`BACKLOG.md`](BACKLOG.md) §3.8a (HL-01..04), §3.8b (TD-01..06),
  §3.8c (stage 1), §3.8e (stage 2 verdict)

### 1.4 Claims that measurement killed

The point of instrumenting first. Each of these was believed at some stage and is now disproved:

| Claim                                                     | Reality                                                                              |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| "The device reboots constantly"                           | Every host USB port open resets it. 240 s uninterrupted: **one** reset, ours.        |
| G09: each tile change costs ~256 KB internal heap         | `ALWAYSINTERNAL=4096` → anything >4 KB goes to PSRAM                                 |
| H1 exhausts the internal pool, `abort()` at ~390 messages | Internal heap did not fall at all; PSRAM fell 2 760 B/message → ~2 800 msgs headroom |
| G01 explains the maintainer's heap defect                 | Real UAF, but PSRAM-only, immediate not monotonic, needs a human zooming             |
| TD-01 is T-Deck RF / signal strength                      | Heltec sees the same AP **22-28 dB stronger** and fails identically at −47 dBm       |
| HL-01 affects every headless board                        | The `node_wifion` gate is `#if`-guarded T-Deck-only                                  |
| G06 `addMessage()` stalls the main loop                   | **All** its callers are in `esp32setup()`. Boot cost, not runtime.                   |
| Raising `LV_DISP_DEF_REFR_PERIOD` cuts the flush rate     | 16 ms → 40 ms gave 184 → 189 flushes/60 s. Wirkungslos.                              |
| CI gates the native test suites                           | Both fork workflows `disabled_manually`; suites run **nowhere** automatically        |

## 2. Where we stand right now

### 2.1 Uncommitted in the working tree

`src/t-deck/tdeck_main.cpp`, `lv_obj_functions.cpp`, `event_functions.cpp` — the **P1 attempt**:

- **G07 fixed**: `lv_disp_draw_buf_init()` now gets the pixel count, not the byte count. This was
  mandatory — without it the change below would overflow the buffer by ~150 KB.
- `disp_drv.full_refresh = 0` (partial refresh).
- Explicit `lv_obj_invalidate()` on: the map object in `refresh_map()`, the whole screen when the
  tab drawer closes, and the whole screen on every tab change.

**Measured effect — this part worked:**

| Metric                | Before    | After     | Factor |
| --------------------- | --------- | --------- | ------ |
| Blocking SPI per 60 s | 6.92 s    | 0.45 s    | 15.5x  |
| Mean flush            | 36 631 µs | 2 365 µs  | 15.5x  |
| Idle duty cycle       | 11.5 %    | 0.75 %    |        |
| Max loop stall        | 72 026 µs | 25 021 µs | 2.9x   |

Operator confirms tab switching is now "extrem schnell", and all tabs render correctly **except the
map**.

### 2.2 The blocking problem: map tab under partial refresh

Reproduction (operator, with photos):

1. Open drawer, tap the map icon — nothing appears, except a **single pixel row** of the map in the
   line above the drawer.
2. Open the drawer again — this **shifts the whole map down by the drawer's height**, and only then
   does it draw.
3. Afterwards, going from the map to Message Compose leaves stale content on screen. Only from the
   map; every other tab is clean.

The "shifted down by exactly the drawer height" detail is the important one: this is not purely a
missing invalidation, it is a **layout offset**. Showing/hiding the tab bar changes the content
origin, and the map image is drawn against the wrong origin until something forces a relayout. Under
`full_refresh = 1` that was invisible because the whole screen was repainted every time anyway.

Three invalidation patches were tried and did **not** fix it (map object, drawer close, tab change).
They did fix every other tab, so they are worth keeping — but the map needs the layout question
answered, not more invalidation.

### 2.3 Decision pending on P1

Two ways forward, and the choice has not been made:

- **A — keep partial refresh** and fix the map layout. Keeps the 15x win. Risk: the GUI code is
  written throughout on the assumption that everything is always fully repainted, so the number of
  remaining stale-area sites is unknown.
- **B — revert `full_refresh` to 1 and make the flush non-blocking via DMA** (second draw buffer in
  PSRAM + `pushPixelsDMA`). Rendering semantics stay **identical**, so no visual risk at all. The
  36.7 ms still happens on the wire, but the main loop no longer waits for it — which removes the
  measured harm (blocked loop, starved LoRa RX). More work, but it holds.

Recommendation: **B**, with A's invalidation patches kept only if they are still needed under B
(they are not — B does not change rendering).

## 3. Open

### 3.1 Confirmed defects, not yet fixed

| ID    | What                                                                        | Evidence                    |
| ----- | --------------------------------------------------------------------------- | --------------------------- |
| P1    | Full-screen flush blocks the loop 11.5 % of the time                        | measured, 36.65 ms x ~3/s   |
| TD-01 | Boot association fails; node offline ~5 min after every power-on            | reproduced on two boards    |
| H1    | Rendered message list never trimmed (view 60 vs model 50)                   | measured, PSRAM 2 760 B/msg |
| G02   | `ic <= MAX_MAP` reads past `strMaps[5]`                                     | verified by reading         |
| G03   | Hemisphere signs dead; S/W stations plotted N/E                             | verified by reading         |
| K2    | Ring wraps to 1 instead of 0; slot 0 frozen at boot                         | verified by reading         |
| G01   | Dangling LVGL slots after delete (UAF, PSRAM)                               | verified by reading         |
| F1    | `posrow` uncapped when loading `/pos.dat` from SD                           | verified by reading         |
| C1/C2 | Audio task at priority 50 vs ceiling 25; semaphore released before playback | verified by reading         |

### 3.2 Untested, and now the last heap candidate

**C2** — use-after-free on the MP3 decoder buffers when two messages land inside one sound. Since H1
turned out to be PSRAM-only, C2 is the remaining candidate for the internal-heap defect the upstream
maintainer reported. **Needs: a test tone on the SD card, and audio unmuted in the GUI** (the
operator muted it during this session, which also exercised the C3 path).

### 3.3 New, from operator testing

- **GPS tab font** is too large — constant scrolling — and visually poor. Cosmetic, unscheduled.
- **Map keyboard navigation** requested: pan with `j`/`k` and arrows, zoom keys. This is a feature,
  not a fix. Note the structural obstacle: the map is currently **tile-centred** —
  `sdmap_refresh()` always loads the tile containing the current position and `sdmap_project()`
  computes pixel offsets _within that one tile_. Panning requires a map centre decoupled from the
  GPS position plus loading and stitching 2-4 neighbour tiles, otherwise the view runs off the edge
  after 256 px.

### 3.4 Standing constraints

- **LoRa needs ~20 s between messages.** At 10 s spacing, 5 of 40 test messages were lost.
- **No broadcasts.** Group 9999 or direct DM only — broadcasts go worldwide.
- **Net console is single-client.** The soak sampler connects briefly and disconnects; pause it with
  `touch soak.csv.PAUSE` before driving the console manually.
- **Opening the USB serial port reboots the T-Deck.** Use the net console for anything that must not
  perturb state.
- **Nothing runs in CI.** `pio test -e native` (45) and `-e native_aprs` (36) must be run by hand.

## 4. Suggested order for the next session

1. Decide P1: A or B (recommendation: B).
2. Commit whatever P1 becomes, with the measured before/after in the message.
3. C3 — the cheap verified one-liners (G02, G03, K2, G01), one commit, with native tests where the
   logic can be extracted.
4. C2 test: tone on SD, audio on, two messages inside one sound, watch internal heap.
5. Then H1 (C1) and the SD-map robustness set (F1, G09, G10, G11).
6. TD-01 experiment: disable BLE at boot on one node and see whether the first association succeeds.
