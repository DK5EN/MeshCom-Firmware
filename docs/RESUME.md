# RESUME — pick up here

Last session: 2026-08-29, 08:00 to ~13:30 (branch `tdeck-partial-refresh-trace`, HEAD after the
wrap-up commit; everything pushed to `origin`, working tree clean). 33 commits today. The campaign
backlog is [`BACKLOG.md`](BACKLOG.md) §3.8f (TM-01 … TM-22); read its "What the scouting settled"
table before re-deriving anything. Upstream state, review verdict and branch model: §3.8g, §4.1,
[`review/2026-08-29-upstream-sync-verdict.md`](review/2026-08-29-upstream-sync-verdict.md).

## Where things stand

| Branch                        | Contents                                                                                                                                                       | Device state                            |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------- |
| `tdeck-partial-refresh-trace` | Working branch. `v4.35p_prio` + upstream `dev` `2cb6bb4d` merged + all of today's fixes and instrumentation. 0 behind `upstream/dev` as of 2026-08-29.         | **All four bench nodes run this build** |
| `v4.35p_prio`                 | Fork main (docs, tools, tests, debug code). Behind the trace branch; merge the trace branch back into it when the T-Deck work is declared done.                | —                                       |
| archive tags                  | `archive/pre-rebase-20260827{,-2}`, `archive/tdeck-partial-refresh-wip-20260828`, `archive/claude-flood-network-20260822` — the deleted branches, on `origin`. |                                         |

Bench node settings (decided 2026-08-29): Heltec `--debug off`, GPS on, BME280 on; T-Beam track
off, GPS on; T-Deck as is. Mind that the firmware's triple click (`--btn triple`, OLED harness
`track` scenario) switches GPS off together with the track page -- the harness restores the GPS
state it found since the wrap-up commit; check `--pos` after any manual triple click.

Bench fleet (all on USB, port names can move — match by USB serial; table in `BACKLOG.md` §3.8f):
T-Deck Plus `DK5EN-14` `/dev/cu.usbmodem1101`, Heltec V3 `DK5EN-93` `/dev/cu.usbserial-0001`,
T-Beam v1.2 `DK5EN-92` `/dev/cu.usbserial-573C0005841`, RAK4631 `DK5EN-90` `/dev/cu.usbmodem201301`.

## Fixed today, verified on hardware

| Item         | What                                                                                                | Measured                                                                                         |
| ------------ | --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| UP-01        | `serializeJson` bound = buffer size, not JSON length (`bleJsonFrame()` + native test)               | native 50/50                                                                                     |
| TM-01..04    | Audio task + queue; loopTask never blocks on audio; player SD reads under the bus mutex             | `audio_stall` 1 552 ms -> 23 ms                                                                  |
| TM-15        | Boot messages without the 2 s pump                                                                  | `CLIENT STARTED` 17.8 -> 4.6 s                                                                   |
| TM-18        | Trackball edges counted in an ISR (old level compare lost ~75 % of a fast roll)                     | edges == steps, 0 backlog, repaint p50 8 ms; operator: "feels good now"                          |
| TM-20        | `startNetwork()` non-blocking (async scan, no delays, no `delay(1500)` retry)                       | no loop gap > 0.7 s at boot, loop max 26 ms; was ~7 s frozen, also every 5 min while unconnected |
| TM-09        | Heltec V3/V4/Stick OLED on `Wire1` hardware I2C, 400 kHz (`-D U8X8_HAVE_2ND_HW_I2C=1` is mandatory) | frame push 579 ms -> 34.5 ms, loop max 645 -> 39 ms                                              |
| TM-08        | T-Deck header labels/colours written only on change                                                 | idle invalidations 36.9/s -> 7.0/s                                                               |
| TM-21        | `[WEB]...no ip set` once per state                                                                  | was ~125 lines/s with debug on                                                                   |
| TM-12        | Loop/heap instrument on the RAK4631                                                                 | loop avg 99.7 / max 104 ms (paced), heap 111 832 free                                            |
| TM-05        | Closed by analysis: all SPI2 users except the (mutex-guarded) audio task run on loopTask            | —                                                                                                |
| ready marker | `[BOOT];ready;ms;N;ip;X` (raw `Serial.printf`; `printfdeb` strips `;` outside `--debug csv`)        | ready 9-11 s on all ESP32 boards with WiFi joined                                                |

Cross-board regression after all of it: Heltec V3, T-Beam, RAK4631 boot clean, LoRa TX/RX in every
direction between the four nodes, `--info` OK (BACKLOG §3.8f "Cross-board regression").

## Measured, not fixed — decisions pending

- **TD-01 / TM-11 / TM-24 (WiFi first join fails).** 12 boots per arm on DK5EN-14, same hour:
  baseline 4/12, BLE advertising deferred 3/12 (**BLE hypothesis refuted**), join by SSID only
  8/12. Every failure logs `[WIFI];event;disconnected;reason;2` (= `AUTH_EXPIRE`). Root cause named
  in the evening (TM-24): **the firmware has no roaming** — no 802.11k/v/r in the SDK build, no
  auto-reconnect, channel+BSSID pinned at join — while the bench WLAN is a steering mesh (router +
  satellite). Fix direction decided: join by SSID only, let the driver roam,
  `WiFi.setAutoReconnect(true)`, keep the ping watchdog as last resort. Confirm with a 24-boot run
  (`BENCH_WIFI_NO_BSSID=1` build, `scratchpad/bootloop.py` pattern, 75 s per boot) before shipping.
- **Tile format on the SD card** — parked as low priority by decision (2026-08-29): stability and
  functionality first, optimisation later. Options and the `[SDMAP]` read/decode split stay in
  `tdeck-findings-20260828.md`.
- **T-Beam display** (TM-22): already on hardware I2C (37.8 ms/frame), but the SSD1306 variant is
  still page mode and there is no dirty flag (TM-10).

## Harnesses (run from `tools/bench/runs/` so the raw logs land there)

- `python3 tools/bench/tdeck_harness.py --list` — 13 scenarios (boot, idle, tabs, drawer, inject,
  audio, audio_stall, sleep, screen, map, nav, input, heap). All 13 green on DK5EN-14.
- `python3 tools/bench/oled_harness.py --list [--port …]` — 7 scenarios (boot, pos, inject, pages,
  display, track, timing) for every U8g2 board. 7/7 on Heltec V3 and T-Beam.
- `tools/bench/experiments/rolltest.py` — operator trackball roll test (edge vs level mode).
- Both harnesses wait for `[BOOT];ready`, accept `--scenario a,b --skip c`, and switch the panel on.
- Opening a port reboots every ESP32 node; the RAK needs `dtr=True`; the T-Deck ignores serial for
  ~11 s after `CLIENT STARTED`; the Heltec prints `;`-less lines unless `--debug csv`.

## Firmware bench hooks added today (fork-only, all default-off or log-only)

`--btn click|double|triple`, `--oledstat`, `--oledlog on/off`, `--injectpos` on OLED boards,
`--scroll <tab> <dy>`, `--key <text>`, `--ball <dir> <n>`, `--balledge on/off`, `--balledges
[reset]`, `[TFT];on/off`, `[KEY]`, `[BALL]`, `[OLED];frame`, `[WIFI];event;…`, `[BOOT];ready`.
Build-flag experiments: `BENCH_BLE_ADV_LATE`, `BENCH_WIFI_NO_BSSID`.

## Next session, in order

1. TD-01 confirmation run (24 boots, SSID-only) -> fix (TM-11), then re-run both harnesses.
2. TM-22/TM-10: T-Beam SSD1306 to full-buffer, dirty flag for OLED pushes (Heltec + T-Beam).
3. TM-06/TM-07: LoRa raw-frame injection + SPI register trace to retire the NOP mitigation.
4. Then the PR: build `pr/tdeck-ui` from `upstream/dev` per BACKLOG §4.1 — firmware files only
   (audio wave, flush mitigation, G07, map composition, g/h keys, SD 20 MHz, WiFi bring-up, Heltec
   OLED, header labels, UP-01) — no instrumentation, tools, tests or docs. German per-file
   description. Decide whether the Heltec OLED and WiFi changes go in the same PR or separate ones.
