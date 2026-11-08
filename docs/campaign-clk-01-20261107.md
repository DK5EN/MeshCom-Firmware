# Campaign CLK-01: display clock ticks every second, free-running clock does not drift

Opened 2026-11-07 on `fork-dev`. Backlog row: `CLK-01` in [BACKLOG.md](BACKLOG.md) 3.2 "Implement".
Status: **planned, nothing implemented, nothing measured.** Every cause below is read from the code, not
observed on a bench.

## Bottom line

A field tester reports that the clock on the display of a LilyGo T3 S3 V1.3 often lags 1-2 minutes and then
suddenly jumps back to the right time. The code has two independent defects that both produce this picture, plus
one gate that makes the display coarse:

1. **F1 - the OLED clock freezes on the message page.** After a received text, the top line stops updating until
   some other page is drawn. This is the likely cause in the field report (node on WLAN, time source NTP).
2. **F2 - the OLED redraws the clock only every 15 s.** At most 15 s stale, never minutes; but it violates the goal.
3. **F3 - the free-running clock loses time.** `Clock::CheckEvent()` counts exactly one second per call and drops
   the loop latency of every tick. Without NTP, GPS, RTC or phone, the clock runs slow until the next `{CET}`
   beacon resets it, and then it jumps.

**Goal:** the clock on every display (OLED, TFT, T-Deck) visibly ticks every second on every page, and the
free-running clock between two `{CET}` beacons stays within 2 s of true time.

**Order:** instrument first (W0), measure the "before" on Heltec V3, T-Beam and T-Deck in one parallel USB run
(W1), then fix (W2) and measure the "after" with the same run (W3), then the upstream PR (W4). All three defects
are upstream code (V4.18, `90cef2f0`), so the fix goes upstream.

## Field report

| Item            | Value                                                                                                                                      |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| Date            | 2026-11-07, after the tester flashed v4.40a.11.06 with the fork web flasher                                                                |
| Node            | DK1OM-3, `LilyGo_T3_S3_V1_3`, OLED (u8g2), no GPS, no RTC                                                                                  |
| Status page     | `UTC offset 1.0 [NTP]`, Timezone none, Gateway off, Mesh on, GPS off, Display on                                                           |
| Symptom (verb.) | "Das Board aktualisiert recht oft seine Uhrzeit nicht. Die hängt dann schon mal 1-2 Minuten zurück und stellt sich dann plötzlich wieder." |
| Open question   | Is the stale time on the OLED, and is a received text on screen at that moment?                                                            |

The node is on WLAN, so its clock is set from NTP on every loop pass (`src/esp32/esp32_main.cpp:4270`); F3 does not
apply to it while WLAN is up. That points at F1.

## Findings

### F1 - OLED clock frozen while a message page is shown

- `sendDisplayText()` sets `iDisplayType = 0` (`src/loop_functions.cpp:2971`) and keeps the page for at least 30 s
  (`DisplayOffWait`, `:2962`). The top line carries the time of the draw.
- `sendDisplayTime()` returns at once while `iDisplayType == 0` (`src/loop_functions.cpp:2129`).
- Only a page change redraws the clock: a received position (`sendDisplayPosition()` sets `iDisplayType = 1`), a
  button press, the info page (`iDisplayType = 9`). On a busy mesh that is typically 1-2 minutes, then the clock
  "jumps".
- Further silent gates in the same path: `pageHold > 0`, `bSetDisplay`, `bDisplayIsOff`, and a configured ping
  (`node_pingcall`/`pingtime`/`pingcount` set skips `sendDisplayTime()` entirely, `:2698`).
- Affects every U8g2 board (`MC_HAS_U8G2`, `src/configuration_global.h:254`): Heltec V2/V3/V4, T-Beam family,
  T3 S3, TTGO LoRa32, Wireless Stick and more. TFT boards go through the same `sendDisplayTime()` (`displayTFT()`).

### F2 - 15-second redraw gate

- `sendDisplayTime()` draws the top line only when `node_date_second` is 0, 15, 30 or 45 (`src/loop_functions.cpp:2180`;
  a second gate of the same kind at `:1964`). A loop pass that misses that exact second skips a whole quarter.
- To tick every second, the gate goes. Risk: one full OLED frame per second. `--oledlog on` measures the frame time
  (`[OLED];frame` with duration); the existing frame CRC skip (`oled_skipped`) stays.

### F3 - free-running clock runs slow

`Clock::CheckEvent()` (`src/clock.cpp:93`):

```cpp
u32Diff    = ((u32Next_m - u32Start_m) / 1000);   // always 1
tsClock_m += u32Diff;
...
u32Start_m = millis();                             // lateness of this tick is dropped
u32Next_m  = u32Start_m + 1000;
```

- Every tick counts 1 s, but the real tick lasts 1 s plus the loop latency past `u32Next_m`. At 40 ms average
  latency that is 4 % slow, about 2.4 min per hour (illustration, not measured).
- A loop pass that blocks 3 s (flash write, WLAN connect, long display draw) still counts 1 s.
- Originally the clock ticked once per minute (the comment `((60 - suClock_m.tm_sec) * 1000)`); at 1 s per tick the
  same rounding loss is 60 times larger.
- Only visible when nothing else sets the clock. On ESP32 the NTP branch calls `setCurrentTime()` every loop pass,
  GPS and RTC likewise, so `CheckEvent()` never ticks there. Exposed: LoRa-only nodes, and any node during a WLAN
  outage, between two `{CET}` beacons (about one per 5 min on the air, `docs/campaign-tx-pool.md` D1).

### F4 - T-Deck and other RTC boards ignore `{CET}`

- `{CET}` is accepted only when `!bRTCON && !posinfo_fix && !bNTPDateTimeValid` (`src/loop_functions.cpp:2914`).
- T-Deck, T-Deck Plus, T-Deck Pro, T114, T5 e-paper, Wireless Paper, Vision Master E213 and T-ETH-Elite define
  `ENABLE_RTC`. With a working RTC they never run free and never take `{CET}`. There is no console switch to turn
  the RTC off (`bRTCON` is set only in `rtc_functions.cpp:60`).
- For the test the T-Deck therefore needs the `--timesrc` gate (W0) to run on `{CET}` only; without it, it is the
  RTC control node.
- The T-Deck label (`tdeck_update_time_label()`) is refreshed on every change of `node_date_second`, independent
  of the OLED gates; F1 and F2 do not apply to it. Whether the LVGL label really repaints every second is a W1 check.

### Out of scope

E-ink boards (Wireless Paper, T5, E290) refresh the clock every 10 s by design (panel wear). Unchanged.

## Acceptance criteria

| Id  | Criterion                                                                                                    | Measured by                                                   |
| --- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------- |
| A1  | On OLED and T-Deck the displayed second advances every second on every page, message page included.          | `[DISPCLK];draw` lines: no gap > 1 s over 30 min; screenshots |
| A2  | Free-running clock (`--timesrc cet`) stays within 2 s of host time over 2 h, before each `{CET}` correction. | `[CLOCK];stat` vs host timestamp; `[CLOCK];set` delta         |
| A3  | A `{CET}` correction moves the clock by at most 2 s (beyond the beacon's own transit age).                   | `[CLOCK];set;src;CET;delta_ms`                                |
| A4  | Per-second OLED redraw costs no more than 5 % loop time and no LoRa RX loss.                                 | `--oledlog` frame time, `--instr` loop sections               |
| A5  | No regression on NTP, GPS and RTC paths; `--stage 1,2` green.                                                | regression gate                                               |

## Instrumentation (W0)

All of it under `-D INSTRUMENT_ENABLED=1`, RAM-only, absent from a normal board build (same rule as `--airgap`).

| Item                             | Output / behaviour                                                                                                                                                          |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[CLOCK];stat` every 60 s        | `epoch;<tsClock_m>;ms;<millis>;ticks;<n>;late_sum_ms;<sum>;late_max_ms;<max>;lost_ms;<real elapsed - counted>;src;<cTimeSource>` - the drift of F3 measured inside the node |
| `[CLOCK];set` on every clock set | `src;<NTP\|GPS\|RTC\|CET\|PHONE\|CMD>;old;<epoch>;new;<epoch>;delta_ms`. NTP/GPS/RTC set every loop pass, so log only when `\|delta\| >= 1 s` or once per 60 s              |
| `--clockstat`                    | the `[CLOCK];stat` line on demand                                                                                                                                           |
| `--timesrc all\|cet\|none`       | gate in `Clock::setCurrentTime()`: `cet` accepts only `{CET}` (NTP, GPS, RTC, phone ignored, `{CET}` accepted even with RTC), `none` lets the clock run free. Default `all` |
| `[DISPCLK];draw`                 | per draw of the clock line: `hms;<hh:mm:ss>;type;<iDisplayType>`                                                                                                            |
| `[DISPCLK];skip`                 | per second without a draw, with the reason: `type0`, `setdisplay`, `off`, `ping`, `gate15`                                                                                  |
| `--oledshot` (serial)            | the u8g2 frame buffer (1 KiB at 128x64) as one base64 line `[OLEDSHOT];<w>x<h>;<b64>`, same layout as `GET /screen.oled` (`src/oled_shot.h`), for runs without WLAN         |
| `--tdshot` (serial, T-Deck)      | the PSRAM shadow frame (320x240 RGB565, 150 KiB) as base64 in chunks; about 15 s at 115200 baud, so only on demand                                                          |

Implemented (W0 part 2) - final command set and line formats. Code: `src/clock_instrument.{h,cpp}`, hooks in
`Clock::SetClock/CheckEvent/setCurrentTime`, `sendDisplayTime()` and the WP_DISP clock branches, short arms in
`commandAction()`. Pure helpers: `src/clock_tick.h` (`clockTickLateMs`, `clockLostMs`), `src/b64enc.h` (base64, host test in
`test_oled_shot`).

| Command / line                                                                         | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[CLOCK];set;src;<S>;old;<epoch>;new;<epoch>;delta_ms;<signed>`                        | S = NTP, GPS, RTC, CET, PHONE, CMD, SER, OTHER (boot restore, TZ re-anchor, topo restore, offset change). `delta_ms` = new time minus the node's time at that instant (tick epoch + ms into the second). Logged if `\|delta\| >= 1 s`, else the first set per source and then once per 60 s per source                                                                                                                                        |
| `[CLOCK];stat;epoch;..;ms;..;ticks;..;late_sum_ms;..;late_max_ms;..;lost_ms;..;src;..` | every 60 s from `CheckEvent()` and on `--clockstat`. `ticks`, `late_*` count since boot; `lost_ms` = real ms since the last set minus counted seconds x 1000, taken at the last tick (reset by every set). `src` = last accepted named source (not `cTimeSource`, which callers set even when `--timesrc` ignored the set)                                                                                                                    |
| `--clockstat`                                                                          | the stat line on demand                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `--timesrc all\|cet\|none`                                                             | gate in `Clock::setCurrentTime()`; prints `[CLOCK];timesrc;<mode>`. `cet` accepts only the `{CET}` frame (also with an RTC fitted), `none` ignores all. Internal `SetClock()` calls (boot restore in `time_functions.cpp`, TZ re-anchor, topo restore) are NOT gated: they happen without a time source and keep the offset consistent. RAM only, default `all`. Callers still run their own bookkeeping (`cTimeSource`, `bNTPDateTimeValid`) |
| `[DISPCLK];draw;hms;<hh:mm:ss>;type;<iDisplayType>`                                    | at the clock-line draw in `sendDisplayTime()`; WP_DISP: at each `wpRefreshClock()` call (every 10 s). At most one line per second                                                                                                                                                                                                                                                                                                             |
| `[DISPCLK];skip;<reason>`                                                              | reason from `dispClkReason()`: type0, off, setdisplay, gate15, plus `ping` from the caller (ping configured, `sendDisplayTime()` not called). WP_DISP: `off` or `gate15` (the 10 s gate). `hold` is never emitted (`pageHold` is not consulted by the gate) and `nodisplay` (no u8g2, e-paper, T-Deck: no clock line in `sendDisplayTime()`) is suppressed in the caller. At most one line per second                                         |
| `--oledshot`                                                                           | `[OLEDSHOT];<w>x<h>;<base64 of the u8g2 buffer>` (one line, 1368 chars at 128x64) or `[OLEDSHOT];err;<no display\|display off\|not a full frame>`                                                                                                                                                                                                                                                                                             |
| `--tdshot` (T-Deck, T-Deck Plus)                                                       | `[TDSHOT];begin;320x240;153600;200`, 200 x `[TDSHOT];chunk;<idx>;<base64 of 768 bytes>`, `[TDSHOT];end`; raw RGB565 big endian as `/screen.rgb565`. First use switches the PSRAM shadow on, forces a redraw and prints `[TDSHOT];err;shadow was off, now on - repeat ...`; repeat after the redraw. `Serial.flush()`, `meshcom_wdt_feed()` and `yield()` per chunk                                                                            |

Host side:

- `tools/bench/clock_drift.py`: opens up to three ports in parallel (resolved by USB serial), stamps every line with
  host time (NTP-synced Mac), parses `[CLOCK]` and `[DISPCLK]`, writes per node a `.jsonl` and a `.summary` to
  `tools/bench/runs/harness/` (drift curve, jump sizes, longest display gap) and decodes `[OLEDSHOT]`/`[TDSHOT]`
  into PNGs via the decoders of `oled_screenshot.py` and `tdeck_screenshot.py`.
- Native tests, red before the fix:
  - `test_clock_tick`: mocked `millis()`; 3600 ticks each 40 ms late must give 3600 s; one 3 s block must count 3 s.
  - `test_display_clock`: the gate decision of `sendDisplayTime()` pulled into a pure function; on the message page
    (`iDisplayType == 0`) and at second 7 it must draw.

## Bench plan (W1 before, W3 after)

### Nodes

| Node     | Board       | USB serial        | Time sources normally | Role in the test                                |
| -------- | ----------- | ----------------- | --------------------- | ----------------------------------------------- |
| DK5EN-1  | Heltec V3   | 0001              | NTP                   | OLED, F1/F2/F3                                  |
| DK5EN-92 | T-Beam v1.2 | 573C000584        | GPS, NTP              | OLED, F1/F2/F3, GPS path                        |
| DK5EN-14 | T-Deck Plus | E0:72:A1:AD:65:E0 | RTC, GPS, NTP         | TFT/LVGL label, F3 only via `--timesrc cet`, F4 |

DK5EN-98 stays untouched; it is the gateway whose `{CET}` beacons the bench nodes receive.

### Preconditions

- One INSTRUMENT build per board, flashed once (`/bench`, `/build-firmware`), `MC_AU_DEV_CHANNEL=1`.
- `tools/bench/identity_guard.py` before the run; `lsof` on every port; no Chrome Web Serial, no other
  `serial_capture.py` on the ports. Opening the ports reboots all three ESP32; the run starts after `[BOOT];ready`.
- **RX stays on, TX stays quiet:** `{CET}` must be received, so `--airgap` is not usable (it drops RX too).
  Instead `--mesh off` (no relay of foreign frames), TX power at most 2 dBm, no test messages except `--injectmsg`
  (local, never transmitted). DK5EN-1 is on the mesh only deliberately; this run is RX only.
- `--mesh off` is a stored setting (`TG_SAVE`); note each node's value before the run and restore it after.

### Scenarios (all three nodes in parallel, one `clock_drift.py` run each)

| Id  | Scenario                    | Steps                                                                                            | Expect before fix                                            | Expect after fix                      |
| --- | --------------------------- | ------------------------------------------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------- |
| S1  | Baseline, normal sources    | 10 min, `--timesrc all`; `--oledshot`/`--tdshot` every 60 s                                      | `[DISPCLK];skip;gate15` 3 of 4 s on OLED; no drift           | draw every second; no drift           |
| S2  | Message page                | `--injectmsg` to group TEST, then 3 min without page change; shot every 15 s; then `--injectpos` | OLED: `skip;type0` until the position, clock frozen in shots | draw every second on the message page |
| S3  | `{CET}` only, 2 h           | `--timesrc cet`; T-Beam additionally `--gps off`; every `[CLOCK];set` logged; shot every 5 min   | `lost_ms` grows, jump of seconds to minutes at each `{CET}`  | `\|delta\| <= 2 s` at each `{CET}`    |
| S4  | Real WLAN off (Heltec only) | SSID removed (settings write, reboot), GPS off; 1 h; serial shots only                           | same as S3; confirms the gate simulates the real case        | same as S3 after fix                  |
| S5  | Free run, no source         | `--timesrc none`, 30 min                                                                         | pure F3 drift, no correction                                 | drift within crystal tolerance        |

S4 changes stored settings on DK5EN-1; restore the SSID afterwards and check the start page.

`--tdshot` blocks the loop for its whole duration (about 15 s at 115200 baud, 200 chunks), so `CheckEvent()` ticks are
missed and the clock loses that time under the current scheme (F3). Take `[CLOCK];stat` readings away from the shots
(not within about 20 s after a `--tdshot`) or discount the step in the F3 drift curve; with `--oledshot` the effect is
negligible (one 1368-character line). While `--timesrc` is not `all`, the instrument build also skips the hardware RTC
write-back and forces the free-running clock path on RTC boards, so the gate really leaves the clock alone.

### Evidence kept

Per run in `tools/bench/runs/harness/`: `clock-<node>-<stamp>.jsonl`, `.summary`, and the PNGs the doc cites.
Raw `*.log` stays gitignored. The wave commit carries only the cited files.

## Fix design (W2, after the W1 numbers)

- **F3:** count whole elapsed seconds and keep the remainder:

  ```cpp
  uint32_t now  = millis();
  uint32_t secs = (uint32_t)(now - u32Start_m) / 1000;
  tsClock_m  += secs;
  u32Start_m += secs * 1000;
  u32Next_m   = u32Start_m + 1000;
  ```

  Day/hour events and the TZ-01 minute re-anchor keep working because they compare the old and new `struct tm`.

- **F1:** on the message page redraw only the top line (clock, battery) every second, leave the message body
  untouched; `pageHold` and `bSetDisplay` keep blocking the full page, not the clock line.
- **F2:** drop the 0/15/30/45 gate in both places; keep the frame CRC skip. If A4 fails, draw the clock line into a
  partial buffer update (`u8g2.updateDisplayArea`) instead of a full frame.
- **Ping gate:** the ping display shares the top line; decide in W2 whether it keeps priority.

## W1 results (2026-11-08, partial: S1 and S5)

INSTRUMENT builds of `fork-dev` `d9bd847f` on DK5EN-1 (Heltec V3), DK5EN-92 (T-Beam), DK5EN-14 (T-Deck Plus), USB,
`clock_drift.py`. The first S1 run is void: Heltec and T-Beam had the stored `--display off`, so every draw was
`skip;off` and every shot `display off`. Re-run with `--display on` (restored to off afterwards).

| Run | Node   | Duration | Drift total | Lost ticks | Longest display gap | Skips (type0 / gate15) | Clock sets    |
| --- | ------ | -------- | ----------- | ---------- | ------------------- | ---------------------- | ------------- |
| S1  | Heltec | 10 min   | +0.39 s     | 0          | **374.6 s**         | 410 / 163              | GPS 17, NTP 9 |
| S1  | T-Beam | 10 min   | +0.07 s     | 0          | 15.1 s              | 434 / 144              | GPS 11, NTP 2 |
| S1  | T-Deck | 10 min   | +0.71 s     | 0          | no DISPCLK hook     | --                     | GPS 13, NTP 3 |
| S5  | Heltec | 30 min   | **-5.38 s** | 5.9 s      | **481.4 s**         | 1063 / 673             | boot only     |
| S5  | T-Beam | 30 min   | **-8.54 s** | 8.4 s      | **466.8 s**         | 1060 / 674             | boot only     |

- **F3 confirmed:** with no source the software clock loses 11 to 17 s per hour, almost exactly the lost-tick sum;
  late ticks, not crystal drift.
- **F1 confirmed on the OLED boards:** `type0` skips dominate; the clock was not redrawn for 6 to 8 minutes while a
  non-clock page was up (S1 Heltec, both S5 runs).
- **Normal sources hide it:** with GPS and NTP (S1) the drift stays under 1 s because the clock is reset every
  minute or so.
- Open: S2, S3 (2 h), S4 (WLAN off). Summaries: `tools/bench/runs/harness/clock-*-20261108-084638.summary` (S1),
  `clock-*-20261108-085654.summary` (S5).

## Waves

| Wave | Content                                                                        | Gate                                                                                     |
| ---- | ------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| W0   | Instrumentation, `--timesrc`, serial shots, `clock_drift.py`, red native tests | `--stage 1,2`; red tests fail for the stated reason; INSTRUMENT builds of the three envs |
| W1   | Bench "before": S1-S5 on DK5EN-1, -92, -14 in parallel                         | numbers and shots in this doc                                                            |
| W2   | Fix F1, F2, F3                                                                 | red tests green, `--stage 1,2`, all board envs build                                     |
| W3   | Bench "after": same run                                                        | A1-A4 met                                                                                |
| W4   | Upstream PR (German body), `/submit-pr`                                        | R1-R4 of `REVIEW.md`                                                                     |

## Open questions

- Confirm with the tester that the stale time is on the OLED and coincides with a message page (F1 vs F3).
- `{CET}` transit age: a beacon relayed over several hops arrives late (age gate 150 s). A3 must subtract the
  payload age, or the "after" run blames the clock for the network.
