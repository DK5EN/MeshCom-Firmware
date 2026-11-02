# Campaign WX-01 UV and wind telemetry (2026-11-02)

Optional LTR390 UV sensor and reed-switch anemometer on ESP32 boards, reported as the telemetry values `uvindex`
and `wind`. Upstream issue #999 (Mosei1984, Heltec WiFi LoRa 32 V2). Backlog row `WX-01`.

**State:** W1-W3 and the TLM-03 fix implemented 2026-11-02, benched without sensors; upstream PR
[#1251](https://github.com/icssw-org/MeshCom-Firmware/pull/1251) merged 2026-11-02, in fork-dev since `560e65ab` (field test necessary, debug section included).
Open: field test with real sensors, #999 comment; Q1-Q3 answered 2026-11-02; runs in the cloud together with BAT-04, schedule and setup in
[campaign-bat04-wx01-cloud-20261102.md](campaign-bat04-wx01-cloud-20261102.md).

**Verdict (2026-11-02):** feasible and small. No wire-format change: the values ride in the existing APRS `T#`
telemetry frame, whose 5 analog slots are picked by name from `node_values` (`sendTelemetry()`,
`src/loop_functions.cpp:6012`). Frame length and LoRa byte budget stay as they are, so the issue's
"MeshCom 5.0" label does not block this part.

## Operator decisions (2026-11-02)

- **ESP32 only.** No nRF52/RAK implementation: the RAK4631 flash is nearly full. nRF52 builds compile the feature
  out (`ENABLE_LTR390` / `ENABLE_WIND` not set) and the console answers "not supported on this board".
- **No bench hardware.** Neither an LTR390 nor an anemometer is on the bench. Verification is host tests plus
  "sensor absent" behaviour on bench boards. The PR is marked **field test necessary**.
- **Debug information goes into issue #999 and the PR** (section "Field-test debugging" below), so a later
  field report can be answered by reference.
- **Scope:** issue items 1 to 3 (UV sensor, wind counter with averaging, telemetry names). Item 4 (ePaper
  weather receiver) is out of scope: display-layer work on a maintainer board family, separate decision.
- **Q1 = add `gust`** (highest 3 s mean within the telemetry interval, km/h, third telemetry name `gust`).
  **Q2 = no** averaging of existing values. **Q3 = one** upstream PR, credit Mosei1984 (#999).
- **We build it; the reply on #999 is posted by the operator** (or by us with an explicit OK at that time).

## Facts from recon (2026-11-02, verify again in W0)

| Fact                       | Where                                                                                                                                                                                                                                                                                          | Consequence                                                                             |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Telemetry value names      | `sendTelemetry()` knows `batt co2 gasres hum onewire press proz qnh temp vbus vcurrent`                                                                                                                                                                                                        | Add `uvindex`, `wind` and `gust` as three more branches                                 |
| Sensor on/off flags        | `node_sset2`, `node_sset3`, `node_sset4` have no free bit (sset3 is clamped at 65535)                                                                                                                                                                                                          | New settings members needed, not flag bits                                              |
| Settings layout            | X-macro `src/meshcom_settings.h` (`M(type, name, default)`), suites `native_settings_members_esp32/nrf52`, `native_settings_roundtrip`, `native_esp32_settings_nvs`                                                                                                                            | New members ESP32-only, so the nRF52 layout (three copies on the RAK) does not move     |
| Sensor integration pattern | AHT20: toggle table `src/command_functions.cpp:625`, boot read `src/esp32/esp32_main.cpp:1500`, poll `esp32_main.cpp:5130` (60 s), status print `command_functions.cpp:7093`, JSON `sensdoc` `:7634`, web switch `web_functions.cpp:3793`, `web_setup.cpp:517`, RM read `rm_exec_read.cpp:113` | Same eight touch points for each new sensor                                             |
| Sensor debug switch        | `--wxdebug on` (`bWXDEBUG`, `node_sset3` 0x0200), already used by AHT20, BME680, BMX280                                                                                                                                                                                                        | New sensors log under `bWXDEBUG`, no new debug flag                                     |
| Heltec V2 pins             | I2C SDA 4 / SCL 15, GPS 13/12, button 0, battery 37 (`variants/heltec_wifi_lora_32_V2/configuration.h`)                                                                                                                                                                                        | Anemometer pin is a setting, no default pin; reject pins used by LoRa, I2C, GPS, button |

## Design

- **LTR390:** own minimal driver (I2C 0x53, UVS mode, gain 18x, 20-bit), no new library dependency. Pure
  conversion `raw -> UVI` in a header-only `src/ltr390_calc.h` so it runs in a native test. Sensitivity and
  window factor from the datasheet (W1a states the source and the numbers it used).
- **Wind:** GPIO interrupt (`CHANGE`, IRAM handler) with a two-state software debouncer (3 ms per transition) on
  the configured pin. The PCNT plan was dropped in W1: its glitch filter tops out at about 12.8 us, reed bounce
  lasts 0.1 to 2 ms (advisor verdict 2026-11-02).
  Speed = pulses per second x `node_wind_cal` (default 2.4 km/h per Hz, the common cup anemometer). Unit km/h.
- **Averaging:** header-only `src/wx_avg.h`, a running mean that resets when telemetry is sent. Wind is sampled
  every second and averaged over the telemetry interval (default 30 min, `TELEMETRY_INTERVAL`); UV is sampled on
  the 60 s sensor tick and averaged the same way. Existing values (`temp`, `hum`, `press`, ...) stay as they are:
  averaging them changes upstream behaviour and needs a maintainer decision (question Q2).
- **Settings (ESP32 only, behind `#ifdef ESP32` in the X-macro):** `node_wx_flags` (bit 0 UV on, bit 1 wind
  on), `node_wind_pin` (0 = off), `node_wind_cal` (float). Defaults: everything off, so an upgraded node behaves
  byte-identically.
- **Commands:** `--uv on|off`, `--wind on|off`, `--windpin <n>`, `--windcal <f>`; status in the existing sensor
  status print and in the BLE sensor record `S1` (`UV`, `UVF`, `WIND`, `WINDP`; not `SE`, which has no room left under
  the 244-byte BLE payload).
- **Build flags:** `ENABLE_LTR390` and `ENABLE_WIND` in the ESP32 sections of `platformio.ini`, not on nRF52.

## Waves

Every implementer: Sonnet 5.5, medium (frontmatter default). Advisor: fresh `general-purpose`, Opus 5.5, medium.
Briefs restate the full rule list of `/orchestrate-waves` section 2 (exclusive files, no git, no sibling
stubs, no whole-repo formatter, scoped verification, terse report, artifact proof).

| Wave | Agent                         | Exclusive files                                                                                                                                                                                                                                   | Depends on | Estimate     |
| ---- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ------------ |
| W0   | scout (Haiku, low)            | read only: confirm the recon table, PCNT API of the pinned arduino-esp32 / IDF, settings member position, test-env pattern, current flash/RAM of `heltec_wifi_lora_32_V2` and `heltec_wifi_lora_32_V3`                                            | -          | 10 min, < $1 |
| W1a  | implementer                   | new `src/ltr390.{h,cpp}`, `src/ltr390_calc.h`, `test/test_ltr390_calc/`                                                                                                                                                                           | W0         | 25 min, ~$5  |
| W1b  | implementer (parallel to W1a) | new `src/wind_counter.{h,cpp}`, `src/wx_avg.h`, `test/test_wx_avg/`                                                                                                                                                                               | W0         | 25 min, ~$5  |
| W1g  | orchestrator                  | hotspot: `platformio.ini` (`[env:native_ltr390_calc]`, `[env:native_wx_avg]`, `ENABLE_*` flags)                                                                                                                                                   | W1a, W1b   | at the gate  |
| W2   | implementer                   | `src/meshcom_settings.h`, `src/command_functions.cpp`, `src/esp32/esp32_main.cpp`, `src/loop_functions.cpp` (globals only), `src/loop_functions_extern.h`, `test/test_command_toggles/`, `test/test_command_setters/`, the settings-member suites | W1         | 25 min, ~$5  |
| W3   | implementer                   | `src/loop_functions.cpp` (`sendTelemetry()` only), `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp`, `src/rm_exec_read.cpp`, a native test for the name lookup if the function can be reached host-side                   | W2         | 20 min, ~$4  |
| W4   | orchestrator                  | docs: this file, `BACKLOG.md`, `RESUME.md`, German PR body, issue comment text                                                                                                                                                                    | W3         | 15 min       |

W2 and W3 both touch `src/loop_functions.cpp`, so they run one after the other. Advisor pass after W1, W2 and W3
(behaviour changes), about $3 each. Total roughly $25 to $30, 2.5 to 3 h wall-clock including gates.

### Acceptance per wave

- **W1a:** `ltr390_calc.h` converts known raw counts to UVI within 0.1 of a hand calculation from the datasheet;
  the driver returns "not found" cleanly without a device on the bus (no hang, no I2C error storm).
- **W1b:** `wx_avg.h` mean, gust (max of 3 s means), reset and empty-window behaviour tested (empty window reports "no value", not 0);
  pulse-to-speed conversion tested at 0, 1 and 10 Hz with the default and a custom calibration.
- **W2:** defaults off; settings roundtrip and member suites green on both platforms; nRF52 member list unchanged
  (`native_settings_members_nrf52` byte-identical); `--windpin` rejects the pins of the board's LoRa, I2C, GPS
  and button; RAK build answers `--uv on` with "not supported on this board".
- **W3:** `node_values` with `uvindex`, `wind` or `gust` fills the slot with the averaged value; without a sensor the
  slot keeps the existing "no value" behaviour of the other names; the window resets after each `T#` frame.

### Gate (after every wave)

1. `ls` the wave's files.
2. `tools/regression.sh --stage 1,2` (never bare `pio test`, one `pio` at a time).
3. Sequential builds of every board env (R2), at least all ESP32 envs plus `wiscore_rak4631`.
4. Artifact proof: `strings` on `heltec_wifi_lora_32_V2` and `_V3` `firmware.bin` finds `LTR390` and the `[WIND]`
   log tag; the same scan on the RAK image finds neither (count 0).
5. Flash/RAM regions of V2 and V3 before and after, in this file (compare regions, not hashes).
6. Bench, serialized (load `/bench` first, `identity_guard.py`, no broadcast): on DK5EN-1 (Heltec V3) `--uv on`,
   `--wind on` with no hardware attached; expect "error"/"not found" in the status, no reboot, no TASK_WDT, `T#`
   frame unchanged for the existing names.

## Field-test debugging (text for issue #999 and the PR)

The PR carries this section in German, the issue comment in English. Placeholders are filled in W4.

- **Enable:** `--wxdebug on` first (the one-time `[LTR390] not found` line is only printed when debug is already on
  at sensor start), then `--uv on`, `--windpin <gpio>`, `--windcal <km/h per Hz>`, `--wind on`.
- **Status:** the console sensor status print shows `LTR390: on (found|error)` and `WIND: on (pin <n>, cal <f>)`;
  the web info page shows `UV (LTR390): on (found|error), UVI <x>, mean <x|-> (n=<k>)` and
  `Anemometer: on (pin <n>), <x> km/h, mean <x|->, gust <x|->`; the BLE sensor record `S1` carries `UV`, `UVF`, `WIND`,
  `WINDP`. Mean and gust cover the time since the last T# value frame (since switch-on or boot while telemetry is
  inactive).
- **Log lines under `--wxdebug on`:** `[LTR390] raw=<counts> gain=18 res=20 uvi=<x.x>` every 60 s;
  `[WIND] pulses=<n>/<s> s hz=<f> kmh=<f>` every second (60 lines per minute);
  `[WX] T# uvindex=<avg|-> (n=<samples>) wind=<avg|-> (n=<samples>) gust=<max|->` each time a T# value frame is
  built.
- **Telemetry check:** `node_values` must name the slots, for example `--values uvindex,wind,gust,temp,hum`, and
  `PARM`/`UNIT` must match (`UVI`, `km/h`). **Known blocker (TLM-03, pre-existing):** on a normal node no T# frame
  goes on air: `sendTelemetry(1)` returns unless `node_parm_1` is set, and only the soft-serial XML path sets it
  (`src/tinyxml_functions.cpp`); `--parm` writes `node_parm`. Until TLM-03 is fixed, the values are visible on the web
  info page, in the status print and in BLE `S1`, but not in T#.
- **What to send with a report:** `--info`, the status print, 10 minutes of serial log with `--wxdebug on`, the
  board, the GPIO used and the anemometer model (pulses per revolution).
- **Known limits:** ESP32 only; no RAK4631, T-Deck Pro, T5 e-paper (they do not inherit the `[esp32]` flags); only
  tested host-side and without sensors on the bench (field test necessary). Wind is the mean since the previous T#
  value frame (normally one telemetry interval, up to five after the PARM/UNIT/EQNS/BITS rotation), gust the highest
  3 s mean in that window. The reed contact is debounced in software (3 ms per transition, GPIO interrupt), which
  caps the counter at about 166 Hz (about 400 km/h at 2.4 km/h per Hz); a close that follows a release within 3 ms
  is missed once, the counter re-syncs on the next pulse. The LTR390 window factor is 1.0 (no cover glass). The RM
  read reply is not extended (LoRa answer with a 105-byte budget, REVIEW R4).

## Open questions

None; Q1-Q3 answered (see Operator decisions).

## TLM-03 (found in C4, fixed 2026-11-02)

On a node without the soft-serial app no `T#` frame went on air at all (periodic timer passed `SOFTSER_APP_ID`
unconditionally since upstream v4.34x.05.16). Fixed before the WX-01 upstream PR (operator decision):
`src/telemetry_app.h`, both timer call sites, test `native_telemetry_app`. BACKLOG row `TLM-03`.

## Bench (2026-11-02, DK5EN-92, T-Beam, fork-dev `328e858a`, no sensor attached)

- `--uv on`: `LTR390: on (error)`; `--wind on`: "wind on, but no pin set"; `WIND: on (pin 0, cal 2.40)`.
- `--windpin 21` (I2C) and `--windpin 38` (button) rejected with the pin-class message.
- 75 s with both on: no reboot (uptime continuous); both switched off again afterwards.
- Still needed: a field test with a real LTR390 and anemometer (no bench hardware).
- TLM-03 on hardware (DK5EN-92, gateway, fork-dev `328e858a`): with `--parm Batt --unit V --values batt --ptime 5` the
  node sent `DK5EN-92>100001:DK5EN-92 :T#012,4.06` as `TX-UDP` (gateway: server upload only, no LoRa), visible with
  `--setinfo on` (`NEW-TMY`). Settings reset to `none` / ptime 15 afterwards.

## Wave log

| Wave | Commit                      | Gate          | Advisor                                                                                                                                                                                                      | Notes                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| ---- | --------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| -    | -                           | -             | -                                                                                                                                                                                                            | plan                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| W0   | -                           | -             | -                                                                                                                                                                                                            | Scout (C0): recon table confirmed; `sendTelemetry()` absent sensor sends the stored value (0.0 default), no omit; ESP32-only members go into `MESHCOM_SETTINGS_MEMBERS_ESP32`; framework arduino-esp32 2.0.17 / IDF 4.4.7; `Wire.begin(I2C_SDA, I2C_SCL)` in `esp32setup()` before the sensor boot reads; no device at 0x53 in the code                                                                                                         |
| W1   | see C1 row of the cloud doc | see cloud doc | REWORK (PCNT cannot debounce a reed contact; LTR390 MEAS_RATE, resolution range, `ltr390End`/wfac), then APPROVED                                                                                            | Rework by a fresh Sonnet-high implementer: GPIO-ISR debouncer, wrap-safe pulse delta, `ltr390End()`, `ltr390SetWfac()`. Host tests 14 + 17. **W2 must reject in `--windpin`:** LoRa, I2C, GPS, button pins, flash pins (classic 6-11, S3 26-32) and classic 34-39 (no internal pull-up)                                                                                                                                                         |
| W2   | see C3 row of the cloud doc | see cloud doc | REWORK (empty `LORA_DIO0` on T-Beam Supreme broke the build; radio/SPI pins missing from the `--windpin` reserved list; `SE` JSON over the BLE budget), then APPROVED with condition "build every ESP32 env" | Settings `node_wx_flags`/`node_wind_pin`/`node_wind_cal` (ESP32 member block, persist-only schema rows `node_wxflags`/`node_windpin`/`node_windcal`, not exported to the config JSON). `--windpin` rejects about 90 board pin macros (radio, SPI, I2C, GPS, button, Ethernet, SD), flash pins, classic 34-39, octal-PSRAM 33-37 and pins the chip lacks (`src/wx_pins.h`). UV 60 s gate, wind 1 s gate with measured seconds. JSON keys in `S1` |
| W3   | see C4 row of the cloud doc | see cloud doc | APPROVED (TLM-03 blocker for the on-air acceptance recorded above; doc mismatches fixed)                                                                                                                     | `src/wx_telemetry.h` (value text, `[WX]` line), `uvindex`/`wind`/`gust` in `sendTelemetry()` with the window reset after each T# value frame, web setup switches/inputs and info lines, web POST/GET. Orchestrator: web `windpin` accepts only one integer, `windcal` accepts a decimal comma; window reset on `--uv on` / `--wind on`. `src/rm_exec_read.cpp` deliberately not touched (LoRa byte budget)                                      |
