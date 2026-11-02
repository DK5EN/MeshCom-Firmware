# Campaign WX-01 UV and wind telemetry (2026-11-02)

Optional LTR390 UV sensor and reed-switch anemometer on ESP32 boards, reported as the telemetry values `uvindex`
and `wind`. Upstream issue #999 (Mosei1984, Heltec WiFi LoRa 32 V2). Backlog row `WX-01`.

**State:** plan only, nothing dispatched. Execute with `/orchestrate-waves`, starting at W0.

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
- **We build it; the reply on #999 is posted by the operator** (or by us with an explicit OK at that time).

## Facts from recon (2026-11-02, verify again in W0)

| Fact                       | Where                                                                                                                                                                                                                                                                                          | Consequence                                                                             |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Telemetry value names      | `sendTelemetry()` knows `batt co2 gasres hum onewire press proz qnh temp vbus vcurrent`                                                                                                                                                                                                        | Add `uvindex` and `wind` as two more branches                                           |
| Sensor on/off flags        | `node_sset2`, `node_sset3`, `node_sset4` have no free bit (sset3 is clamped at 65535)                                                                                                                                                                                                          | New settings members needed, not flag bits                                              |
| Settings layout            | X-macro `src/meshcom_settings.h` (`M(type, name, default)`), suites `native_settings_members_esp32/nrf52`, `native_settings_roundtrip`, `native_esp32_settings_nvs`                                                                                                                            | New members ESP32-only, so the nRF52 layout (three copies on the RAK) does not move     |
| Sensor integration pattern | AHT20: toggle table `src/command_functions.cpp:625`, boot read `src/esp32/esp32_main.cpp:1500`, poll `esp32_main.cpp:5130` (60 s), status print `command_functions.cpp:7093`, JSON `sensdoc` `:7634`, web switch `web_functions.cpp:3793`, `web_setup.cpp:517`, RM read `rm_exec_read.cpp:113` | Same eight touch points for each new sensor                                             |
| Sensor debug switch        | `--wxdebug on` (`bWXDEBUG`, `node_sset3` 0x0200), already used by AHT20, BME680, BMX280                                                                                                                                                                                                        | New sensors log under `bWXDEBUG`, no new debug flag                                     |
| Heltec V2 pins             | I2C SDA 4 / SCL 15, GPS 13/12, button 0, battery 37 (`variants/heltec_wifi_lora_32_V2/configuration.h`)                                                                                                                                                                                        | Anemometer pin is a setting, no default pin; reject pins used by LoRa, I2C, GPS, button |

## Design

- **LTR390:** own minimal driver (I2C 0x53, UVS mode, gain 18x, 20-bit), no new library dependency. Pure
  conversion `raw -> UVI` in a header-only `src/ltr390_calc.h` so it runs in a native test. Sensitivity and
  window factor from the datasheet (W1a states the source and the numbers it used).
- **Wind:** ESP32 PCNT hardware counter with glitch filter on the configured pin (debounce of the reed contact
  without an ISR); GPIO-interrupt fallback only if PCNT is not usable on the framework version found in W0.
  Speed = pulses per second x `node_wind_cal` (default 2.4 km/h per Hz, the common cup anemometer). Unit km/h.
- **Averaging:** header-only `src/wx_avg.h`, a running mean that resets when telemetry is sent. Wind is sampled
  every second and averaged over the telemetry interval (default 30 min, `TELEMETRY_INTERVAL`); UV is sampled on
  the 60 s sensor tick and averaged the same way. Existing values (`temp`, `hum`, `press`, ...) stay as they are:
  averaging them changes upstream behaviour and needs a maintainer decision (question Q2).
- **Settings (ESP32 only, behind `#ifdef ESP32` in the X-macro):** `node_wx_flags` (bit 0 UV on, bit 1 wind
  on), `node_wind_pin` (0 = off), `node_wind_cal` (float). Defaults: everything off, so an upgraded node behaves
  byte-identically.
- **Commands:** `--uv on|off`, `--wind on|off`, `--windpin <n>`, `--windcal <f>`; status in the existing sensor
  status print and in `sensdoc` (`UV`, `UVF`, `WIND`, `WINDP`).
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
- **W1b:** `wx_avg.h` mean, reset and empty-window behaviour tested (empty window reports "no value", not 0);
  pulse-to-speed conversion tested at 0, 1 and 10 Hz with the default and a custom calibration.
- **W2:** defaults off; settings roundtrip and member suites green on both platforms; nRF52 member list unchanged
  (`native_settings_members_nrf52` byte-identical); `--windpin` rejects the pins of the board's LoRa, I2C, GPS
  and button; RAK build answers `--uv on` with "not supported on this board".
- **W3:** `node_values` with `uvindex` or `wind` fills the slot with the averaged value; without a sensor the
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

- **Enable:** `--uv on`, `--wind on`, `--windpin <gpio>`, `--windcal <km/h per Hz>`, then `--wxdebug on`.
- **Status:** the sensor status print shows `LTR390: on (found|error)` and `WIND: on (pin <n>, cal <f>)`; the JSON
  sensor record carries `UV`, `UVF`, `WIND`, `WINDP`.
- **Log lines under `--wxdebug on`:** `[LTR390] raw=<counts> gain=18 res=20 uvi=<x.x>` per 60 s tick;
  `[WIND] pulses=<n>/<s> s hz=<f> kmh=<f>` per tick; `[WX] T# uvindex=<avg> (n=<samples>) wind=<avg> (n=<samples>)`
  when telemetry is sent.
- **Telemetry check:** `node_values` must name the slots, for example `--values uvindex,wind,temp,hum,press`, and
  `PARM`/`UNIT` must match (`UVI`, `km/h`).
- **What to send with a report:** `--info`, the status print, 10 minutes of serial log with `--wxdebug on`, the
  board, the GPIO used and the anemometer model (pulses per revolution).
- **Known limits:** ESP32 only; no RAK4631; only tested host-side and without sensors on the bench (field test
  necessary); wind is the mean over the telemetry interval, no gust value.

## Open questions

- **Q1:** a gust value (max of 3 s windows) as a third name `gust`? Cheap in W1b, not asked for in the issue.
- **Q2:** average the existing values (`temp`, `hum`, `press`) over the telemetry interval too, as the issue
  proposes? Changes upstream behaviour; ask the maintainers in the PR, default no.
- **Q3:** one upstream PR or two (UV, wind)? Default one, credit Mosei1984 (#999) for the proposal.

## Wave log

| Wave | Commit | Gate | Advisor | Notes |
| ---- | ------ | ---- | ------- | ----- |
| -    | -      | -    | -       | plan  |
