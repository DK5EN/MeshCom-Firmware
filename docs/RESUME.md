# RESUME — pick up here

Last session: 2026-08-29, 08:00 to ~13:30 (branch `tdeck-partial-refresh-trace`, HEAD after the
wrap-up commit; everything pushed to `origin`, working tree clean). 33 commits today. The campaign
backlog is [`BACKLOG.md`](BACKLOG.md) §3.8f (TM-01 … TM-34); read its "What the scouting settled"
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

| Item         | What                                                                                                | Measured                                                                                           |
| ------------ | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| UP-01        | `serializeJson` bound = buffer size, not JSON length (`bleJsonFrame()` + native test)               | native 50/50                                                                                       |
| TM-01..04    | Audio task + queue; loopTask never blocks on audio; player SD reads under the bus mutex             | `audio_stall` 1 552 ms -> 23 ms                                                                    |
| TM-15        | Boot messages without the 2 s pump                                                                  | `CLIENT STARTED` 17.8 -> 4.6 s                                                                     |
| TM-18        | Trackball edges counted in an ISR (old level compare lost ~75 % of a fast roll)                     | edges == steps, 0 backlog, repaint p50 8 ms; operator: "feels good now"                            |
| TM-20        | `startNetwork()` non-blocking (async scan, no delays, no `delay(1500)` retry)                       | no loop gap > 0.7 s at boot, loop max 26 ms; was ~7 s frozen, also every 5 min while unconnected   |
| TM-09        | Heltec V3/V4/Stick OLED on `Wire1` hardware I2C, 400 kHz (`-D U8X8_HAVE_2ND_HW_I2C=1` is mandatory) | frame push 579 ms -> 34.5 ms, loop max 645 -> 39 ms                                                |
| TM-08        | T-Deck header labels/colours written only on change                                                 | idle invalidations 36.9/s -> 7.0/s                                                                 |
| TM-21        | `[WEB]...no ip set` once per state                                                                  | was ~125 lines/s with debug on                                                                     |
| TM-12        | Loop/heap instrument on the RAK4631                                                                 | loop avg 99.7 / max 104 ms (paced), heap 111 832 free                                              |
| TM-05        | Closed by analysis: all SPI2 users except the (mutex-guarded) audio task run on loopTask            | —                                                                                                  |
| ready marker | `[BOOT];ready;ms;N;ip;X` (raw `Serial.printf`; `printfdeb` strips `;` outside `--debug csv`)        | ready 9-11 s on all ESP32 boards with WiFi joined                                                  |
| TD-03 / H1   | Active-tab message view trimmed like the model (`msg_list_trim_view()`, 50 bubbles)                 | harness `trim`: 60 -> 50 children; saturated view -584 B PSRAM / 20 msgs (was 2 760 B/msg)         |
| UP-02        | Last `delay()` out of `add_map_point()` (LoRa RX path, 30x in `refresh_map()`)                      | harness `map --map-stations 40`: recycle branch wraps, 40/40, no crash; loop max unchanged (noise) |
| TM-22/10/27  | SSD1306 full-buffer, frame CRC, unchanged frames skipped (`oledFrameUnchanged()`)                   | OLED harness 8/8 Heltec V3 + T-Beam; blank CRC `0xefb5af2e` both; repeat page -> skip              |
| TM-33 (a)    | GT911 `begin()` retried 5x / 100 ms on the T-Deck (upstream #64 `touch: failed`)                    | boot scenario: `touch;1;touch_tries;1`; failure path not reproducible on the bench                 |
| TM-33 (b)    | `--display on/off` drives the T-Deck TFT (`tft_on()`/`tft_off()`, upstream #690)                    | harness `displaycmd`: before 0/4, fixed 4/4; `sleep` scenario unaffected                           |

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
  (`BENCH_WIFI_NO_BSSID=1` build, `tools/bench/experiments/bootloop.py`, 75 s per boot) before
  shipping. _Revised 2026-08-29 by TM-34: "no auto-reconnect" is wrong — it is on by default; the
  pin is what stops the driver choosing another AP. See `wifi-findings-20260829.md` §6._
- **Tile format on the SD card** — parked as low priority by decision (2026-08-29): stability and
  functionality first, optimisation later. Options and the `[SDMAP]` read/decode split stay in
  `tdeck-findings-20260828.md`.
- **OLED (TM-22/TM-10/TM-27) fixed 2026-08-29:** SSD1306 full-buffer on the `#else` ladder (no
  bench node for that half), frame-buffer CRC per push, identical frames skipped. OLED harness 8/8
  on Heltec V3 and T-Beam (`display` CRC round trip, `dirty`). Frame push stays ~32/36 ms — that
  is 1 KB at 400 kHz; further savings need partial updates, not a faster bus.

## Harnesses (run from `tools/bench/runs/` so the raw logs land there)

- `python3 tools/bench/tdeck_harness.py --list` — 14 scenarios (boot, idle, tabs, drawer, inject,
  audio, audio_stall, sleep, screen, map, nav, input, heap, trim). All 14 green on DK5EN-14.
- `python3 tools/bench/oled_harness.py --list [--port …]` — 8 scenarios (boot, pos, inject, pages,
  display, track, timing, dirty) for every U8g2 board. 8/8 on Heltec V3 and T-Beam.
- `tools/bench/experiments/rolltest.py` — operator trackball roll test (edge vs level mode).
- Both harnesses wait for `[BOOT];ready`, accept `--scenario a,b --skip c`, and switch the panel on.
- Opening a port reboots every ESP32 node; the RAK needs `dtr=True`; the T-Deck ignores serial for
  ~11 s after `CLIENT STARTED`; the Heltec prints `;`-less lines unless `--debug csv`.

## Firmware bench hooks added today (fork-only, all default-off or log-only)

`--btn click|double|triple`, `--oledstat`, `--oledlog on/off`, `--injectpos` on OLED boards,
`--scroll <tab> <dy>`, `--key <text>`, `--ball <dir> <n>`, `--balledge on/off`, `--balledges
[reset]`, `[TFT];on/off`, `[KEY]`, `[BALL]`, `[OLED];frame`, `[WIFI];event;…`, `[BOOT];ready`.
Build-flag experiments: `BENCH_BLE_ADV_LATE`, `BENCH_WIFI_NO_BSSID`.

## Priorities as set by the operator (2026-08-29 evening)

1. T-Deck: UI performance, mouse pointer, stability, code quality (incl. handover §4.2 review
   flaws and TD-03 heap: `msg_list` never trimmed, H1/C2 in `tdeck-gui-verdict.md`).
2. OLED redraw on Heltec V3 and T-Beam (TM-22, TM-10).
3. Boot time on all four platforms: T-Beam, T-Deck, RAK nRF52, Heltec V3 (TM-16; RAK not yet
   profiled).
4. Automated integration/regression harnesses over USB serial on all four platforms (RAK has no
   harness yet; T-Deck 13 scenarios, OLED 7).

- **WiFi (revised 2026-08-29 late, reverses the evening decision):** TM-20 (non-blocking
  `startNetwork()`, all boot delays removed) **stays OUT of the PR.** It feels good on the bench,
  but without the waits the station no longer hears the beacons of every nearby AP and can
  associate with the weakest BSSID of a mesh WLAN — shipped as is it would break nodes behind
  multi-AP WLANs. The 7 s freeze it removes is real, so it goes upstream only together with a
  scan/selection policy. That policy is **TM-34**, a separate research track (BACKLOG §3.8f):
  (a) driver capabilities, (b) scan/wait/select-best-AP options, (c) log output for the
  WiFi-stalls-the-node case (blocks even LoRa RX), (d) SSID-only association, (e) re-connecting,
  (f) roaming, (g) band/AP steering. TM-34 absorbs TM-24 and the TD-01/TM-11 confirmation run;
  findings doc first, then its own bench protocol and its own PR.
  **Desk half delivered 2026-08-29:** [`wifi-findings-20260829.md`](wifi-findings-20260829.md)
  answers (a)-(g) from the driver source and `sdkconfig`, with fix plan F1-F9 (§9) and bench arms
  A0-A5 (§10); runner `tools/bench/experiments/bootloop.py`. **No bench arm has been run yet.**
  Four things it changes: the pin — not the missing wait — is the defect (full-channel scan plus
  sort-by-signal is the driver doing our job better, and on every reconnect); `setAutoReconnect`
  was already `true`, so TM-24's premise is wrong and the real problem is three restart owners
  fighting; the largest `loopTask` blocker left is `hostByName()` at up to 31 s, not WiFi bring-up;
  and **TM-20 should not be reverted** — it ships together with the selection fix, in one PR.
- **TD-03 heap defect: fixed 2026-08-29, goes into the PR.** RAK boot profile + harness needed for platform
  parity (TM-25/26). Screen CRC on Heltec and T-Beam (TM-27; T-Deck has no readback). TM-06/07
  lower priority (cumbersome, no longer highest). Full automation low priority — the manual
  procedure is `automation-runner-runbook.md` (TM-29).
- **E290 Wireless Paper hardware arrives the week of 2026-09-01** (TM-28, delayed until it is
  here) — e-paper redraw is the next display target (WP_DISP code paths, `PAGE_MAX 10`, browse loop in `singleClick()`); needs its
  own harness scenarios and a frame instrument.
- Tile format (TM-23) parked; upstream sync stays merge + net-diff review each time.

## 2026-08-29 late session (TM-30/31/32, TM-13, TM-25/26, A5)

- **TM-32 fixed** (settings plausibility at load, native 10/10, bench-verified with `--flashpoke`),
  **TM-13 done** (`INSTR_SECTION`, `[INSTR-SECT]`, gap attribution), **TM-25/26 done** (RAK ready
  marker, boot profile, `rak_harness.py`). **TM-35 new**: RAK gateway `getUDP()` blocks the loop
  1.6–3.3 s every ~20 s, `sendHey()` 0.7–1.4 s.
- **TM-30 not reproduced** (35 min `uptime` run on DK5EN-14: latency, loop max and heap flat, no
  crash) — the scenario stays as the regression gate for #1083.
- **TM-31** instrument built (`gwflood.py`, `--srvip` hook); blocked: UDP from the Heltec never
  reaches the Mac (see BACKLOG row). Needs `sudo tcpdump` or the Orbi isolation check.
- **TM-34 arms**: A0/A5 first attempt **void** — `bootloop.py` opened the port with DTR/RTS asserted,
  which does not reset the T-Deck Plus (24/24 "no reset marker"); fixed (dtr/rts False). A5 (24
  boots on `ORBI63_Guest`, WPA2) re-run: **24/24 first joins, got_ip median 9.6 s, 0 disconnects**
  (`tools/bench/runs/bootloop_A5_20260829-230435/summary.txt`) vs 4/12 on WPA2/WPA3 `ORBI63` —
  the first-join failure is WPA3-SAE, not steering; the WiFi fix must force WPA2-PSK.
  **A0 run 2026-08-30 on WPA2/WPA3 `ORBI63`: 0/24 first joins, got_ip median 55.8 s / max 56.5 s,
  240 disconnects (216× AUTH_EXPIRE, 24× AUTH_FAIL)** — every boot pins the router BSSID
  `5A:…:8B` (-68…-75 dBm, same radio as A5's guest VAP at the same RSSI), fails 9 attempts and the
  driver's auto-reconnect lands the 10th at ~55 s (`tools/bench/runs/bootloop_A0_20260830-084907/`).
  Baseline for F1–F4 is therefore 0/24, not TM-11's 4/12.
- Firmware bench hooks added (fork-only): `--flashpoke <field> <value>`, `--srvip <ip>`,
  `[BOOT];ready` on nRF52, `[INSTR-SECT]`/`[INSTR-GAPS]`/`[INSTR-LOOP];gap`.

## 2026-08-30 Wave W (TM-34 WiFi fix, TM-35 parity instrumentation)

- **WiFi bring-up rewritten on all ESP32 boards** (`udp_functions.cpp`, `esp32_main.cpp`): driver
  owns AP selection (`ALL_CHANNEL_SCAN` + sort-by-signal, `persistent(false)`), SSID-only
  `begin()`, **WPA2-PSK forced on WPA2/WPA3 APs** via `esp_wifi_disable_pmf_config()` (SAE needs
  PMF; `WIFI_SAE_POLICY` 1 = default), `got_ip` harvested by event (no blind window, no boot radio
  reset), watchdog 180 s grace -> reconnect -> 360 s radio reset, async DNS off `loopTask`,
  `[WIFI];stall/assoc/link`, `--wifistat`, `--wifidrop`, `--wifi on/off` (HL-01), HL-02 default.
- **Arm A4p1 on WPA2/WPA3 `ORBI63`: T-Deck 24/24 first joins (`got_ip` median 14.2 s uptime, 0
  disconnects, 0 stalls), Heltec 12/12 (11.2 s), T-Beam 12/12 (10.6 s)** — baseline A0 was 0/24
  at 55.8 s. `[WIFI];assoc … pmf;0;wpa3;0` on every join = WPA2-PSK negotiated. `--wifidrop`
  reconnect 4.0 s. Runs in `tools/bench/runs/bootloop_A4p1_*`.
- `got_ip` median <= 6 s from bring-up is **not** met as measured from power-on: bring-up starts at
  ~5 s (T-Deck) and our diagnostic scan costs ~5 s before `begin()`; the driver join itself is
  ~4 s. The scan is log-only now (dwell 300 ms x 13 channels) — shortening or dropping it is the
  lever if 14 s matters; not done, keep the field diagnosis unless the operator decides.
- **RAK parity (TM-35)**: `[ETH];stall;<site>` on every W5100S socket/DHCP/link call,
  `[ETH];event;link|got_ip|dhcp|reset`, `[ETH];link` heartbeat, `--ethstat`, `--ethdrop`
  (= `resetDHCP()`, 126 ms on the bench). The 1.6–3.3 s `getUDP()` stall did not show in a 2-min
  window (`udp_rx` max 1 ms) — the marker names it when it recurs. Fix still open.
- Soak runner `tools/bench/experiments/wifisoak.py` (held-open sessions, `--wifidrop` every
  10 min, per-event CSV, reconnect distribution, `--parse-only`). Reducer verified on a smoke log.
- Gates: native 60/60, `test_tdeck_parse` 56/56, all four boards build, OLED harness Heltec 8/8,
  RAK harness boot/info/instr/mheard PASS (`lora` needs `--peer-port`, as before). T-Beam OLED
  `dirty` failed 3/3 **while the other nodes were boot-looping** (LoRa boot beacons flip the
  page) — re-run with the bench quiet before reading it as a regression.
- **A4p0 (SAE kept): 24/24 addressed by 19.7 s, all SAE, but every second boot +4.5 s (silent SAE
  retry) — the A0 0/24 was pin + SAE together. Policy 1 (WPA2-PSK) stays default.**
- **Hook stall found and fixed:** `WiFi.getMode()` in the 60-s link heartbeat, called every loop
  pass, blocked `loopTask` 2.7–2.9 s per boot while the driver scanned (`[INSTR-LOOP];gap … in;lvgl`
  in every A4p1/A4p0 boot, absent in A0/A5). Loop hooks now use STA_START/STOP events only. Rule:
  never call `esp_wifi_*` / `WiFi.getMode()` from the loop unless connected. A4p1b re-measures.
- **A4p1b (hook fix): T-Deck 24/24, `got_ip` median 14.1 s, 0 disconnects, 0 stalls.** The one
  remaining 2.6–3.5 s loop gap per boot (~8.5 s uptime, `section_ms` 32 = outside every section,
  right after GPS/UBLOX init) is pre-existing: the A0/A5 build predates the gap reporter and A0
  shows the same single scan-poll in that window. TM-16 lead, not WiFi.
- **TM-35 with the `[ETH]` instrument, 600 s:** loop max 314 ms = the 15-min NTP round trip
  (`[ETH];stall;ntp;ms;213`), 122 UDP RX with `udp_rx` max 5 ms; the 1.6–3.3 s `getUDP()` stall
  did not reproduce. Bounding NTP needs an async client on the shared gateway socket — operator
  decision (BACKLOG TM-35).
- **Gates after the hook fix:** T-Deck harness 15/15 PASS (`tdeck_w_20260830.json`), OLED
  Heltec 8/8. T-Beam OLED `all` on a quiet bench: 6/8 — `display` and `dirty` fail on harness
  sensitivities, not on firmware: the panel had already auto-switched off when `display` began
  (first `--display off` draws 0 frames, `offwait_ms` 12952), and `dirty` landed on page 5 whose
  content changes every draw (three `--display on` pairs, all `frame`, CRCs differ 250 ms apart;
  yesterday's green run was on page 3). Standalone `dirty` is invalid by construction (starts at
  5 s uptime, first frame inside the window). Harness fix: pin the page and wake the panel first.
- **Overnight soak started 2026-08-30 11:26** (`tools/bench/runs/wifisoak_W_20260830-112600/`,
  14 h, `--wifidrop` every 10 min on T-Deck/Heltec/T-Beam, detached `nohup`; interim
  `summary.txt` every 10 min). Reduce with `wifisoak.py --parse-only wifisoak_W_*/{tdeck,heltec,tbeam}.log`.
- Open: overnight `wifisoak.py` on all
  three boards; WPA3-only APs cannot associate with policy 1 (no PMF) — documented trade, an
  adaptive fallback (SAE first, PMF off after 2x `AUTH_EXPIRE`) would cost ~9 s on every boot.

## Next session, in order

Done in the 2026-08-29 waves: TD-03, UP-02, TM-22/10/27, TM-33 (a)/(b), TM-32, TM-13, TM-25/26,
TM-30 (not reproduced). The A0 arm (24 boots, `ORBI63`, fixed runner) runs in a separate session.

1. **Wave W shipped 2026-08-30 (see section above); remaining: A4p0 arm result, overnight soak, BACKLOG/RESUME after the soak.** Original plan — after A0 is in: F1+F2 (driver-owned selection, SSID-only,
   `persistent(false)`), **plus WPA2-PSK forced on WPA2/WPA3 APs** (A5 proved the first-join
   `AUTH_EXPIRE` is SAE), then F3 (event-driven `got_ip`, no blind window — seen live on the Heltec),
   F4 (watchdog grace), F5 (`[WIFI];stall`), F6 (DNS off `loopTask`), F7. Instrumentation:
   `[WIFI];assoc` (SSID/BSSID/chan/RSSI/auth at every `got_ip` and disconnect), `[WIFI];link`
   60-s heartbeat, `--wifistat`, `--wifidrop`; soak runner over 12–24 h on T-Deck, T-Beam, Heltec
   in parallel; acceptance per `wifi-findings-20260829.md` §10. SNR is not available on ESP32.
2. **TM-35** (RAK gateway `getUDP()` 1.6–3.3 s / `sendHey()` 0.7–1.4 s loop stalls) — bound the
   W5100S socket calls; `rak_harness.py --scenario instr` is the gate.
3. **TM-31** — unblock the LAN path (`sudo tcpdump -ni en0 udp port 1990` on the Mac / Orbi client
   isolation; fallback: RAK as the gateway under test), then run `gwflood.py`.
4. **TM-17** (`bAllStarted` on a clean join, folds into F3), **TM-33 (c)** (WiFi switch shows the
   intent flag, folds into W), **HL-01/02** (`node_wifion` GUI-only) — all WiFi-adjacent, take
   them inside Wave W.
5. **TM-28** E290 Wireless Paper when the hardware is here (week of 2026-09-01): frame instrument,
   OLED-harness scenarios, `[OLED];crc` for e-paper.
6. Lower: TM-06/07 (LoRa raw injection + SPI trace), TM-14, TM-19, TM-29, Wave 0.6 (native suite),
   Wave 2 on nRF52 (CONC-15..18, N-14..16), UP-05/06.
7. Then the PRs (operator decision, not now): T-Deck PR, OLED PR, WiFi PR — per BACKLOG §4.1.
