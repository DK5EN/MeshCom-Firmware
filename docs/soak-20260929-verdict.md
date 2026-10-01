# Soak 2026-09-29 .. 10-01 (DK5EN-98, DK5EN-1, DK5EN-90) -- Verdict

Evaluated 2026-10-01. Review only: nothing in `src/` was changed. Method: one metric sweep over
all three captures (scratch script, `[LOG] STAT`, `[MC-DBG]`, `[NBR]` lines), the host tools
`tools/nbrhopcheck.py` and `tools/nbrrelay.py`, a comparison against the 09-27/28 captures of
the previous release, and live `--info` reads of all three nodes after the window (read only,
nothing sent on air). Logs: `~/meshlog/soak-20260929/` on the Mac; originals on rpizero
`~/meshlog/dk5en-98/`, `~/meshlog/dk5en-1/` and `~/meshlog/dk5en-90/serial-20260929.log`.

## BLUF

- **Stable on all three nodes.** 163 node-hours, 0 resets, 0 crashes, 0 log gaps over 120 s, 0
  logger reconnects, no heap trend. Uptime counters ran through: 57.2 h, 53.0 h, 53.0 h.
- **Radio path healthy.** 0 TX failures, 0 ring drops, ring depth at most 5/20. The relay queue
  latency is much better than on the previous release (DK5EN-98 normal relays p95 47 s -> 10 s).
- **Neighbour matrix clean.** 0 consistency errors in 9787 `CHECK` minutes; hop check PASS on
  all three nodes; 0 text-borne edges.
- **The gateway flag fix (F6) works.** DK5EN-98 sends its gateway HEY every 15.0 min (median,
  234 in 57 h), and DK5EN-1 kept DK5EN-98 flagged as a gateway for 53 h without one lapse.
- **BLE on DK5EN-98 with a real phone: 12799 frames sent, 0 retried, 0 dropped, 0 evicted.**
- **One regression: the battery "no cell" detection on the Heltec V3 (Finding 1).** DK5EN-1 has
  no cell but claims 100 % in 95 of 104 position beacons. The previous release got it right in
  16 of 18. Caused by the 100 ms divider window that fixed the red LED. This contradicts
  release-notes item 6/7 and the bench claim "0.00 V / 0 % after the detection window".
- **Not exercised in this soak:** the DM fixes (no DM was sent, `sent=0` on all nodes), the
  millis wrap (49.7 days), the reboot breadcrumb (no reboot happened), BLE on DK5EN-1 and the
  RAK (no client connected), `--wifiap`.

## Window and builds

| Node     | Board       | Window (host time)         | Build                    | Setup                         | Capture                  |
| -------- | ----------- | -------------------------- | ------------------------ | ----------------------------- | ------------------------ |
| DK5EN-98 | Heltec V3   | 09-29 10:10 -> 10-01 19:20 | 09:55:47, stamp 20260928 | 22 dBm, GW on, MESH on, phone | net console, rpizero     |
| DK5EN-1  | Heltec V3   | 09-29 10:56 -> 10-01 16:00 | release 10:36:35         | 2 dBm, MESH on, no cell       | net console, rpizero     |
| DK5EN-90 | RAK4631 -Os | 09-29 11:05 -> 10-01 16:00 | release 10:29:12         | 2 dBm, MESH off, Ethernet     | USB serial, Mac (DTR on) |

DK5EN-98 ran the pre-release bench image (it lacks only the one-shot position port `cc18833e`
and carries flash stamp 20260928); the two bench nodes ran the published `v4.35v.09.29-neo`
images. The DK5EN-98 window was planned until 10-02 10:10 and ended early by operator decision.

## Results per node

| Metric                             | DK5EN-98                                            | DK5EN-1                  | DK5EN-90 (RAK)                 |
| ---------------------------------- | --------------------------------------------------- | ------------------------ | ------------------------------ |
| Resets / log gaps > 120 s          | 0 / 0                                               | 0 / 0                    | 0 / 0                          |
| Heap min / first-q / last-q median | 124684 / 129400 / 129392                            | 128140 / 131576 / 131576 | 104668 flat                    |
| Frames received (new)              | 7412                                                | 5345                     | 5453                           |
| TX frames / TX failures            | 6186 / 0                                            | 4290 / 0                 | 207 / 0                        |
| Ring depth max / drops             | 4/20 / 0                                            | 5/20 / 0                 | 1/20 / 0                       |
| Relay latency p50 / p95 / max      | 3.6 / 10.5 / 77.6 s                                 | 5.8 / 13.7 / 38.7 s      | 2.9 / 8.2 / 16.7 s             |
| Channel use (5-min avg / max)      | 9.2 % / 16 %                                        | 4.3 % / 11 %             | 4.3 % / 12 %                   |
| CRC errors                         | 949                                                 | 0                        | 1                              |
| NBR CHECK minutes / errors         | 3430 / 0                                            | 3183 / 0                 | 3174 / 0                       |
| NBR rows / edges max               | 34 / 87                                             | 7 / 15                   | 7 / 14                         |
| Server link                        | 6850 heartbeats, max gap 90 s, 0 keepalive failures | --                       | Ethernet up, 53 DHCP renews ok |
| BLE (`--info` after window)        | s12799 r0 d0 t0 e0 mtu255                           | no client                | no client                      |

The DK5EN-98 latency maximum (77.6 s) is a priority-5 HEY; text relays peak at 20.6 s. Its CRC
errors come with RX boost on and a far larger heard area (34 rows against 7); the bench nodes
see almost none.

## Finding 1: Heltec V3 without a cell reports a full battery again (regression)

> **Status 2026-10-01: fixed** in `5c3a932d` (BAT-03, window spread of 8 reads 2 ms apart,
> limit 300 mV), hardware PASS on DK5EN-1 with and without a cell. Campaign, measurements and
> results: `docs/batt-nocell-campaign-20261001.md`.

- **Severity:** medium. Wrong data on air (`/B=100` for a node on USB) and in the app/web. No false
  deep sleep: the phantom reads 4.4 V, which is never "low". No crash risk.
- **Evidence:** DK5EN-1 (no cell, `--maxv 4.2`): 95 of 104 own position beacons carry `/B=100`;
  twelve live `--info` reads at 19:25-19:31 show 4.21-4.68 V, 100 %. On the previous release
  (`~/meshlog/dk5en-1/2026-09-28-u.log`) 16 of 18 beacons had no `/B=`. DK5EN-98 (no cell either,
  `--maxv 3.5`) sent `/B=100` in 7 of 113 beacons and reads 0.00 V now.
- **Cause:** `battDetectUpdate()` (`src/batt_pipeline.h:284`) calls a sample implausible only
  outside [0.55, 1.15] x maxv or after a jump of more than 250 mV, and flips back to "present"
  after 10 plausible samples in a row. With the new 100 ms divider window
  (`BATT_SCHED_SWITCHED_PERIOD_MS`, `batt_function_old.cpp` Heltec switched reader) the floating
  input reads 4.2-4.7 V with small jumps: inside the 2.31-4.83 V band at maxv 4.2, and rarely
  more than 250 mV apart. So the verdict returns to "present" after 10 samples (5 min) and stays.
  At maxv 3.5 the band ends at 4.03 V, which is why DK5EN-98 is mostly right.
- **Why the bench missed it (likely):** the 09-29 bench read was taken right after the absent streak
  (6 samples, 3 min) had fired, before 10 plausible samples could flip it back.
- **Fix options (decision needed):** (a) a "present" decision that needs the reading to sit
  below maxv + margin, e.g. band top 1.05 x maxv, since a floating Heltec divider reads above
  the 4.2 V a charged cell can show; (b) sample the detector at the spacing it was tuned for
  (two or more short divider windows ~500 ms apart per 30 s cycle), where the floating charger
  output showed jumps up to 1.17 V; (c) revert the Heltec window to the old long on-time
  (brings the red LED back). Which signature separates a cell from no cell inside a 100 ms
  window is unmeasured; measure before choosing. BACKLOG `BAT-NOISE` covers the area; this
  finding makes it a bug, not a tuning item.
- **Ruled out (2026-10-01): a divider on/off step check.** The GPIO37 switch sits between the
  battery terminal and the ADC, and without a cell that terminal is the charger output (~4.4 V).
  DK5EN-1, no cell, shows the full step at every boot (`ADC_CTRL_PIN probe: high=886-991
low=1`). The step proves the divider, not the cell.
- **Test:** `test/test_batt_detect` with a recorded 4.2-4.7 V floating series at 30 s cadence
  must end "absent" and stay there; today it ends "present".

## Finding 2: foreign gateways flap in the NBR gateway flag (by design, safe direction)

- DK5EN-98 logged 126 `1|HG` / 122 `0|EXP` changes, all for stations on other firmware
  (DL2JA-2 49, DL2JA-3 34, DH6MAV-12 34, DM6CS-12 34, ...). Their HG HEYs come up to 199 min
  apart (DL2JA-2: median 7.7 min, p90 72 min), longer than the 45-min hold.
- While unflagged, such a gateway counts as a dependent, which means more case-A relays, never
  fewer. No action needed; it disappears as those nodes move to firmware with the 15-min
  refresh. The hold could be raised to 3 h for foreign rows if the extra relays matter.

## Finding 3: own HEY/HN frames are missing from the `TX-LoRa` capture lines (logging only)

- DK5EN-1 shows only its 104 position beacons as `TX-LoRa`, the RAK only its 208 `HN` reports;
  their HEYs reach the neighbours (DK5EN-1 heard 3 RAK `H@`, DK5EN-98 7) but are not echoed in
  the sender's own TX capture. DK5EN-98 does log its `HG`/`HN`. Not a radio fault; it limits
  what host tools can count from the sender side. Low priority.

## Verified fixes from the 09-28 verdict and this campaign

| Item                                  | Result                                                                       |
| ------------------------------------- | ---------------------------------------------------------------------------- |
| F2 line ends on CRC/CAD dumps         | PASS: 948/948 CRC dumps and 7324/7324 CAD lines end their line               |
| F6 gateway HEY refresh (sender)       | PASS: DK5EN-98 HG every 15.0 min median, max 29 min                          |
| F6 gateway flag hold (receiver)       | PASS: DK5EN-98 never lapsed at DK5EN-1 in 53 h; foreign rows lapse at 45 min |
| F3 `--info` boot reason               | PASS: DK5EN-98 `RESET_REASON=3 SW` (the OTA), DK5EN-1 `1 POWERON`            |
| BLE-N1..N3 (ESP32, real phone)        | PASS: 12799 sent, 0 retry, 0 drop, 0 evicted, MTU 255                        |
| RAK -Os image                         | PASS: 53 h, 0 resets, Ethernet stable, HN reports heard by both Heltecs      |
| Battery pipeline, Heltec without cell | FAIL: Finding 1                                                              |
| F1/F4, DM-17 DM statistics            | not exercised (no DM traffic)                                                |
| INS-05 crumb clear, GW-02, TIME-01    | not exercised (no reboot, no `--wifiap`, no wrap)                            |
| Heltec red LED                        | not checked by eye                                                           |

## Unchanged, not a regression

- `ONRXDONE_SLOW` (> 50 ms): 1.6 per received frame on DK5EN-98, the same as on 09-27/28
  (1.8 / 1.6); DK5EN-1 1.0; RAK 0 (max 22 ms). It tracks the debug print volume.
- `RX_IRQ_STALE` 2446 / 3144 / 0: ESP32 only, no lost frame attributed to it.

## Config notes (not firmware)

- DK5EN-1 has `--utcoff 1` (its log clock runs one hour behind CEST); DK5EN-98 and the RAK use 2.
- DK5EN-98 has `--maxv 3.5` although no cell is fitted; it hides most of Finding 1 there.
- DK5EN-1 and DK5EN-90 still carry `nbrdebug`, `loradebug` and `txcapture` on (the RAK capture
  does not restore flags).
