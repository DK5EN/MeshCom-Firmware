# RESUME — pick up here

Last session: 2026-08-30, ~09:00 to ~14:15 — Wave W (WiFi, TM-34) in the morning, then **TM-35
(async NTP), TM-31 (UDP instrument, gateway relay fix, upstream #568 answered), TM-16 (boot time),
TM-11/TD-01 closed, HL-03/HL-04**. All pushed (`df861d58`), working tree clean, branch
`tdeck-partial-refresh-trace`. The WLAN report is
[`wifi-report-20260830.md`](wifi-report-20260830.md) (German, before/after 0/24 → 24/24). The
campaign backlog is [`BACKLOG.md`](BACKLOG.md) §3.8f (TM-01 … TM-37); read its "What the scouting
settled" table before re-deriving anything.

**Bench state when we stopped:** all four nodes run the current build; the T-Deck's `node_mute` was
left at 1, the persist flags at 0, as found. No background runs are alive — the 14-h WiFi soak
(TM-36) died at ~12:17 when the TM-31 work took the Heltec and T-Beam ports. Upstream state, review verdict and branch model: §3.8g, §4.1,
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

- **TD-01 / TM-11 / TM-24 — CLOSED 2026-08-30** (24/24 first joins, `got_ip` median 10.4 s, 0
  disconnects; see the late-session section below). Kept here only as the record of how it was
  found. 12 boots per arm on DK5EN-14, same hour:
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
- **TM-31** instrument built (`gwflood.py`, `--srvip` hook); appeared blocked because UDP from the
  Heltec never seemed to reach the Mac. _Superseded 2026-08-30: no tcpdump and no Orbi change were
  needed — the LAN was fine and the verdict came from a blind instrument. See the late-session
  section below._
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

## 2026-08-30 afternoon: TM-35 async NTP, TM-31 unblocked + gateway defect

- **TM-35 done.** `src/ntp_async.{h,cpp}` (`NtpAsync`) replaces `NTPClient` on both platforms: the
  48-byte request goes out on the shared gateway socket and the call returns at once; the reply is
  harvested by the normal receive path (`getUDP()` / `getMeshComUDP()` offer every datagram to
  `tryConsume()` before parsing it as a MeshCom frame). 2.5 s timeout, 5 s/60 s backoff, mode-4 /
  stratum / epoch validation, `[NTP];ok|timeout|txfail|kod` markers. It also removes a second
  defect: `NTPClient::forceUpdate()` flushed _every_ queued datagram off the shared socket before
  sending, so each refresh could eat pending GATE/CONF frames.
  **Gate met:** RAK 600 s steady state `rak_instr600_ntpasync_b_20260830.json` — **loop max 145 ms,
  0 gaps > 250 ms** (was 314 ms / 1 gap), `eth_state` max 2.9 ms (was carrying the 213 ms NTP
  stall); the 15-min refresh fell inside the window at `[NTP];ok;epoch;1788084488;rtt;106`, i.e.
  106 ms on the wire at zero loop cost. Native `test_ntp_async` 10/10.
- **TM-31 unblocked — the LAN was never the problem.** The old "no datagram reaches the Mac"
  verdict came from a blind instrument: the whole ESP32 UDP receive path logs only through
  `DEBUG_MSG()` (compiled away, `DO_DEBUG 0`) and `printfdeb()` behind `--debug on`. New fork-only
  instrument: `--udplog on/off` (`[UDP];rx`/`[UDP];tx` per datagram) and `--udpstat`
  (`[UDPSTAT];bind;..;rx;..;tx;..;tx_fail;..`), counters always kept. Both directions measured
  good: Mac -> Heltec 3/3, Heltec -> Mac 6/6 with `--srvip`. **No sudo tcpdump, no Orbi change.**
- **TM-31 found a real gateway defect (ESP32 only), fixed.** `getMeshComUDPpacket()` evaluated the
  dedup gate `is_new_packet()` _after_ the `msg_type_b == 0x21` branch had already inserted the
  msg_id via `addLoraRxBuffer()` — so every UDP position frame deduplicated against the entry it
  had just written itself (`RX_DEDUP_ADD slot N`, 17 ms later `RX_DEDUP_DUP slot N`) and an ESP32
  gateway **never relayed a UDP position frame to LoRa**. Baseline `gwflood_instr_20260830.json`:
  30 ingress, 36 `[UDP];rx` seen, **0 queued, 0 TX, 0 observer RX** at every inter-arrival from 8 s
  to 0.5 s. Fix: read the dedup gate before that branch (`is_new_packet()` has no side effects),
  and skip the now-redundant second insert on the queue path so the ring insert rate is unchanged.
  The nRF52 gateway path never had the early insert. After the fix the injected frames radiate
  (`TX-LoRa ... x02F9FE00/01/02`) and the T-Beam observer hears them (`RX-LoRa2 ... x02F9FE02`).
- `gwflood.py` now enables `--udplog`, reports ingress / `[UDP];rx` seen / `RING_WRITE src=udp_rx`
  queued / LoRa TX / observer RX so a loss is attributable to a stage, and falls back to a forced
  `--reboot` for a node that does not reset when the port is opened (T-Beam v1.2).
- **TM-31 result -- upstream #568 answered, with the mechanism named.** Definitive run
  `gwflood_fixed_settle300_20260830.json` (30 frames, gaps 8/4/2/1/0.5 s, 300 s settle so the queue
  can drain):

  | gap  | in  | queued | tx  | rx  | ingress->air median | max   |
  | ---- | --- | ------ | --- | --- | ------------------- | ----- |
  | 8 s  | 6   | 6      | 6   | 3   | 78 s                | 226 s |
  | 4 s  | 6   | 6      | 6   | 4   | 162 s               | 180 s |
  | 2 s  | 6   | 6      | 6   | 5   | 241 s               | 291 s |
  | 1 s  | 6   | 6      | 3   | 1   | 310 s               | 321 s |
  | .5 s | 6   | 6      | 0   | 0   | --                  | --    |

  **Nothing is lost at ingress** -- 30/30 datagrams reach the socket and 30/30 are queued. The
  radio drains at roughly one frame per 20 s under bench channel load, so the ingress-to-air
  latency climbs from 78 s to over 5 minutes. At 1 s and below the **20-slot TX ring saturates**
  (`queued=19/20`) and the firmware then discards the arriving frame:
  6x `RING_DROP_NEW ... prio=4 type=21 (queue full, no lower prio to evict)` (msg_ids
  `...16/18/19/1A/1B/1C`, all in the 1 s and 0.5 s groups) plus 1x `RING_DROP_PRIO` that evicted the
  node's **own** HEY (`type=40`) to make room. So a gateway fed faster than it can radiate does not
  drop at the network edge -- it delays by minutes and then drops at the TX ring, and it starves
  its own traffic while doing so. That matches #568 (the lost packet was the one on the shortest
  inter-arrival) and our DG0OPK TX-queue-latency finding. From a user's point of view a message
  radiated 4 minutes late is indistinguishable from lost, and downstream dedup windows will discard
  it anyway.

- **TM-31 text vs position, measured** (`gwflood_mixed_20260830.json`, 15 text + 15 position frames
  alternating, gaps 8/4/2/1/0.5 s, 300 s settle): **text 15/15 radiated, position 9/15** -- and the
  ring-drop lines say why: 5x `RING_DROP_PRIO ... prio=4 type=21 ... replaced_by_prio=3`, i.e. the
  arriving text frames evicted queued positions, plus 2x the node's own HEY evicted by positions and
  2x HEY tail-dropped. So the priority ladder does what it should under a real flood: text survives,
  positions are sacrificed, and the node's own background traffic goes first. `--assert-relay`
  passed (31/30 queued). Note the class: a UDP-relayed text is enqueued with status `0xFF` =
  `RING_STATUS_DONE`, so `getMessagePriority()` reads it as a _relay_ -> `MSG_PRIO_NORMAL` (3), not
  the path-based broadcast `MSG_PRIO_HIGH` (2). NORMAL still beats position (4), so the conclusion
  holds -- but whoever changes the relay path's status byte moves the priority of all UDP traffic.
- **Regression tests added** (`pio test -e native_aprs`): `test_txring_flood` (8) pins the queue-full
  policy -- equal priority is tail-drop, higher priority is head-drop, text evicts positions, a
  position evicts HEY, ACK always gets in, a 30-frame flood accepts exactly `MAX_RING-1`, and
  `MAX_RING == 20` is pinned so changing the buffering horizon is deliberate. `test_gwflood_frames`
  (6) runs the real `decodeAPRS()` over the injector's checked-in fixture
  (`test/support/gwflood_frames.txt`, regenerate with `gwflood.py --frame mixed --dump-frames`), so
  a malformed injector can no longer masquerade as a gateway that drops everything.
- Open on TM-31 (policy, not defects): tail-drop vs head-drop on a full queue and whether
  `MAX_RING = 20` (~6 min of buffered airtime at the bench drain rate) is right for a gateway. Both
  are now pinned by tests, so a change is a reviewable diff rather than a silent behaviour shift.
  The former open point -- "the sweep only injects position beacons" -- is closed: `--frame
pos|text|mixed` covers the 0x3A case #568 reports, and the measurement above is its answer.

## 2026-08-30 late: TM-16 boot time, TM-11/TD-01 closed, HL-03/HL-04, TM-37 filed

- **TM-16 done.** Two remaining boot costs measured on DK5EN-14 and cut:
  1. `SetupUBLOX()` ended with `WaitPause()` + `sendUBX_MON_VER()` + `readUBXbin()`. The version
     string feeds only a `[GPS_VER]` debug line (`ver` is file-local, no other reader),
     `readUBXbin()` always runs into its 500 ms timeout because it only retriggers and never
     returns early, and `WaitPause()` waits up to 1000 ms for the next character block. Now gated
     on `iGPSDEBUG >= 2`: **SetupUBLOX 1 933 -> 899 ms**, GPS init total 3 503 -> 2 001 ms, module
     configuration untouched.
  2. `startNetwork()` ran a full `WiFi.scanNetworks()` before every `begin()` (3-5 s), but since
     Wave W the driver picks the AP itself (`WiFi.begin(ssid, pwd, 0, NULL, false)`) and
     `wifiLogScan()` only prints and discards. The scan is now skipped on the **first** bring-up
     after reset (`[WIFI];scan;skipped;first_bringup`) and kept on every later one -- radio
     restart, watchdog, i.e. after something went wrong -- so the field diagnosis survives exactly
     where it is needed. Verified with `--wifi off` / `--wifi on`, which prints the AP list again.
     `--wifistat` reports `bringups;N`.

  **Measured over 24 boots** (`bootloop_TD01_close_20260830_20260830-134157/`): `got_ip` median
  **10 394 ms** / max 10 437, ready median **10 949 ms** / max 11 399 -- against Wave W's 24-boot
  baseline of `got_ip` median 14.1 s. Heltec ready 8.2 s, T-Beam 7.5 s, RAK boot/info PASS; GPS
  still detects UBLOX and gets a fix on all of them. T-Deck harness 15/15.

- **TM-11 / TD-01 closed.** The same 24-boot run is the confirmation: **24/24 first joins, 0
  disconnects, 0 connection-error bursts, 0 `[WIFI];stall`.** Chain: 4/12 (TM-11 baseline) -> 0/24
  at 55.8 s (arm A0, BSSID pin + SAE) -> 24/24 at 14.1 s (Wave W) -> 24/24 at 10.4 s (after TM-16).

- **HL-03 done -- and it was the opposite of what the row said.** `--mute on/off` already existed;
  the gap ran the other way: `btn_soundon` called `audio_set_mute()` and then `save_settings()`
  **without touching** `meshcom_settings.node_mute`, so the GUI wrote the OLD value to flash, the
  toggle did not survive a reset, and button and command disagreed about the state. The button now
  goes through `commandAction("--mute on/off")` like every other switch on that page, and the
  command itself now calls `save_settings()` -- it never persisted either.

- **HL-04 done.** `--persistflash on/off`, `--persistsd on/off`, `--immediatesave on/off`, each
  writing the field and saving, with `[PERSIST];flash|sd|immediate;<0/1>`. `--persistsd` also calls
  `loadPosPersistence()` exactly as the GUI switch does -- without it the node keeps working from
  the old store until the next reset. New `--persiststat` prints all four flags in one line.
  Verified on hardware including survival across a reset.

- **TM-37 filed** (operator question): every outgoing frame, the user's own included, goes through
  the same 20-slot TX ring and is radiated one at a time, so messages typed in quick succession are
  spooled. A user text is CRITICAL (DM) or HIGH (broadcast) and normally evicts something lower --
  but when the ring holds only equal-or-higher priority entries, `addTxRingEntry()` returns -1 and
  `sendMessage()` **ignores the return value**: the message is gone and the sender is never told.
  Wanted: act on the return value, and warn the user before the loss when the queue fills.

## Next session, in order

Done 2026-08-29/30: Wave W (TM-34 F1–F7 + WPA2-PSK, TM-17, HL-01/02, TM-33 (c)), TD-03, UP-02,
TM-22/10/27, TM-33 (a)/(b), TM-32, TM-13, TM-25/26, TM-30 (not reproduced), **TM-35, TM-31, TM-16,
TM-11/TD-01, HL-03/HL-04**.

0. **New intake 2026-08-30 — the operator's list of 11 points is filed, not started.** Read
   `BACKLOG.md` §3.8h (`CS-01`..`CS-03`: max-hop over serial + NVRAM, web drop-down, config
   download/upload), §3.8i (`GW-01` HEY parity with `--gateway on`; `TLM-01`/`TLM-02` telemetry
   definitions **parked** behind `TLM-03`, the soft-serial review) and §3.8f `TM-38`..`TM-42`
   (AP-reboot recovery test, country servers, OTA in the regression, T-Deck colour/geometry
   display test, group `TEST`). **Two decisions are waiting on the operator:** whether a server
   `{SET}` may overwrite a persisted max-hop value (CS-01), and what the config-export hash
   protects against plus whether secrets travel in the file (CS-03).

0b. **FL-01 fixed this session (2026-08-30): a node could be driven to beacon at loop rate.**
`sendPosition()`'s shot path (`--sendpos`, user button, and the unauthenticated EXTUDP
telemetry injection) had no rate limit at all; the field evidence is 25 146 position frames in
21 minutes from one station. `src/beacon_rate.h` + a 30 s floor in `sendPosition()`, native
`test_beacon_rate` 6/6, `pio test -e native` 76/76, ESP32 + nRF52 build, **and proven on
DK5EN-93**: the second `--sendpos` prints `[POS];shot;suppressed;since_ms;89;min_ms;30000`, and
after a 34 s pause the next one prints `[POS];shot;resumed;suppressed;1` and goes out. DK5EN-93
runs this build now, the other three bench nodes do not. `sendHey()` has the same missing floor
and is still open. Detail and
the two corrections to the mcmap finding: `BACKLOG.md` §3.8j.

0c. **Third intake 2026-08-30 (§3.8k), plus one more fix.** New: `RX-01` (discard frames whose
source callsign is still `XX0XXX` — seen relayed over four hops), `TX-01` (such a node must not
transmit at all — the other half of RX-01, guard in `doTX()` plus `addTxRingEntry()`), `BP-01` (TX back-pressure to
the sender as Q-code notices QRS/QRT/QTA plus QRV once the queue clears (only if a warning went out before), 80 % refusal for locally originated messages only —
this is the design for `TM-37`, and it needs bench regressions on all four boards), `FL-02`
(`sendHey()` needs the same 30 s floor as `FL-01`; 17 of 18 field events were `hey`).
**`CS-04` fixed:** `getparam()` searched for `"/setparam/?"` and took the substring from the `=`
instead of up to it — the whole read half of the Web-API was dead. Verified on DK5EN-93.
The mcmap finding now carries the corrections in its §10.

1. **TM-36 — restart the WiFi soak.** The 14-h run died at ~12:17 when the TM-31 work took the
   Heltec and T-Beam ports; ~51 min survived (checked in: 5 drops, reconnect median 4.0 s, 0
   unsolicited disconnects, 0 stalls). It needs all three USB ports exclusively, so start it when
   nothing else will touch the bench:
   `cd tools/bench/runs && nohup python3 ../experiments/wifisoak.py … &`, reduce with
   `wifisoak.py --parse-only`.
2. **TM-37 — a dropped outgoing message is silent.** Filed this session on the operator's question.
   `sendMessage()` ignores the `addTxRingEntry()` return value, so a message lost to a full TX ring
   never reaches the air and the sender is never told. Minimum: act on the return value and push a
   failure notice to phone + display. Wanted: a back-pressure warning at a high-water mark so the
   person stops typing. `test_txring_flood` already pins the ring policy, so the change is testable
   natively; bench check is `gwflood.py` with a parallel send burst.
3. **TM-31 leftovers — policy, not defects.** Tail-drop vs head-drop on a full queue, and whether
   `MAX_RING = 20` (~6 min of buffered airtime at bench drain rate) is right for a gateway. Both are
   pinned by tests, so either change is a reviewable diff. Same file as TM-37.
4. **TM-28** E290 Wireless Paper when the hardware is here (week of 2026-09-01): frame instrument,
   OLED-harness scenarios, `[OLED];crc` for e-paper.
5. **TD-05** GUI latency on the T-Deck — cause still unidentified; leads G05/G06 in §3.8f (SD + PNG
   decode with an ~870 ms `delay()` in the LVGL `read_cb`, `addMessage()` blocking 2 s / 8 s at
   boot). **TD-04** Europe map tiles on SD. **TD-06** full serial+net test rig.
6. **Upstream sync**: UP-03 (ours wins the merge, two hunks merged silently — check by hand),
   UP-04 (keep his hunk, drop ours from the PR), UP-05 (watch), UP-06 (trace consumers, small PR +
   test).
7. Lower: TM-06/07 (LoRa raw injection + SPI trace), TM-14, TM-19, TM-23 (tile format, parked),
   TM-29, Wave 0.6 (native suite), Wave 2 on nRF52 (CONC-15..18, N-14..16).
8. Then the PRs (operator decision, not now): T-Deck PR, OLED PR, WiFi PR — per BACKLOG §4.1.
   Note TM-20 ships only together with the WiFi selection policy, and Kurt owns review/merge
   upstream.

## Gates that were green when we stopped

`pio test -e native` 70/70 · `pio test -e native_aprs` 50/50 · `test_tdeck_parse` 56/56 ·
T-Deck harness 15/15 · RAK harness boot/info/instr PASS · OLED harness Heltec 8/8 · builds:
t_deck_plus, t_deck_pro, heltec_wifi_lora_32_V3, ttgo_tbeam, wiscore_rak4631, T-ETH-ELITE_1262.
