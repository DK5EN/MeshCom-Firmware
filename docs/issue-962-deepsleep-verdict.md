# Issue 962: deepsleep überarbeiten — Verdict and Implementation Plan

Date: 2026-09-05
Tree inspected: fork-main worktree at ffe31ca5, compared against upstream `icssw-org/MeshCom-Firmware` branch `dev`
Author: DK5EN

## 1. Verdict

The issue is real, and the current state is worse than the issue describes.

The automatic low-battery deepsleep is not misbehaving, it is switched off entirely. Upstream commented the whole block out for issue 1053 in v4.35p.07.11 (commit e0043a56), for every board, not only the T-Beam 1W. The dead code is still in the tree at `src/batt_functions.cpp:428-463`. Issue 662 ("Low battery standby mode", closed) is therefore de facto open again. Kurt's comment on 962 that OE3WAS's work is "already in batt_functions" refers to the `ADC_BATT_ON()` / `ADC_BATT_OFF()` helpers only, not to a low-battery guard.

Recommendation: fix the `--deepsleep` command itself (small, uncontroversial) and bring the low-battery guard back as a timer-wake loop with hysteresis, gated behind an opt-in setting. Do not build light sleep now.

## 2. What `--deepsleep` does today

Code: `src/command_functions.cpp:1042-1118` (fork), identical in upstream dev at line 1041.

### 2.1 ESP32 boards without e-ink (Heltec V2/V3/V4, TLORA, T-Beam, E22, T-Deck)

| Finding                                | Evidence                                                                                                                                                                        | Consequence                                                                                  |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| No wake source armed                   | `esp_sleep_enable_ext1_wakeup` exists only inside `#if defined(WP_DISP)` (`command_functions.cpp:1105`)                                                                         | Node sleeps until RESET. This is the "manual reset" complaint.                               |
| LoRa chip left in RX                   | `Platform::loraToSleep()` is called only in the WP_DISP branch (`command_functions.cpp:1090`)                                                                                   | SX1262/SX127x continuous RX, several mA, 100x the sleeping ESP32                             |
| OLED never put into power-save         | `grep setPowerSave src/` returns nothing; only Heltec boards switch Vext (`command_functions.cpp:1052-1057`)                                                                    | TLORA and T-Beam OLED sit on the 3.3 V rail and stay lit. This is the OLED bug in the issue. |
| TLORA button path clear can be skipped | `onebutton_functions.cpp:272-277` sets `bDisplayIsOff` and calls `sendDisplayHead(false)`, which returns early when `bSetDisplay \|\| pageHold > 0` (`loop_functions.cpp:1316`) | Last image stays on the panel                                                                |
| T-Beam PMU rails stay on               | No `PMU->disablePowerOutput` before `esp_deep_sleep_start`; `esp32_pmu.cpp:166-173` has the rail power-off commented out                                                        | LoRa and GPS rails powered through the sleep, tens of mA                                     |
| No record of the reason for the sleep  | `esp32_main.cpp:629` maps `ESP_RST_DEEPSLEEP` to a string, nothing distinguishes manual from low-battery                                                                        | OE3WAS's request for an "AKKU LOW" notice after wake is unmet                                |

### 2.2 E-ink boards (Wireless Paper, Vision Master E213)

Already fixed by PR 1050 (commits b1ca38ad, b36bc3cf): radio to sleep, visible panel clear, ext1 wake on GPIO0 plus the configured button pin, `prepareToSleep()` to about 18 uA. This is the template for the other boards.

### 2.3 nRF52 boards

- RAK4631: `esp_deep_sleep_start()` is excluded via `#if not defined(BOARD_RAK4630)`, nothing else happens. `--deepsleep` is a no-op.
- T114 and T-Echo: `bDEEP_SLEEP` flag, peripherals cut, main loop spins `delay(60000)` (`nrf52_main.cpp:1168`). Not broken, but not the same feature.

### 2.4 Low-battery guard

- Block at `src/batt_functions.cpp:428-463` is inside a `/* issue #1053 ... */` comment.
- `CDcount` / `CountDown` at `batt_functions.cpp:28-29` are commented out too.
- Kurt's closing comment on 1053: "Low battery check is disabled because it's impossible to tell if the 1W module is powered by USB. No automatic deep sleep on low battery."
- The 1053 trigger was a scaling error (2S pack measured against a 4.2 V `BAT_MAX_VOLTAGE`), not a USB detection problem. The fix disabled the feature for all boards instead of fixing the scale for one.
- `BAT_MIN_VOLTAGE` is 3.3 V on all single-cell variants, 6.5 V on `LilyGo_T-Beam-1W` (`variants/*/configuration.h`).
- The fork already has a runtime no-battery detection (`battPresentNow`, BAT-01, `batt_functions.cpp:376`), which is the missing guard for the USB-without-pack case.

## 3. Options

### Option A: fix the `--deepsleep` command (recommended)

Generalize the PR 1050 recipe to all ESP32 boards. Small diff, no behaviour change unless the user or the button asks for deepsleep.

### Option B: low-battery guard as a timer-wake loop (recommended, opt-in)

Below the cutoff the node sleeps with a timer wake of about 10 minutes. On wake it measures the battery unloaded. Above a recovery threshold (about 3.5 V single-cell) it boots normally, otherwise it goes straight back to sleep. This is the hysteresis behaviour issues 662 and 962 ask for, and it solves the solar brown-out stall because the node only draws microamps while the panel charges.

Safety against the 1053 trap:

1. Arm the guard only after the pack has once been measured above the recovery threshold in the current uptime ("seen charged" latch).
2. Skip the guard when `battPresentNow` reports a floating divider (no pack, USB only).
3. Persistent opt-in setting, default off, so upstream can merge without re-arguing 1053.

### Option C: light sleep with LoRa wake (advise against for now)

Kurt's stated favourite in the issue. Reasons to defer:

- Arduino-ESP32 prebuilt libraries have no `CONFIG_PM_ENABLE`, so light sleep must be entered manually with `esp_light_sleep_start()`. That drops every BLE and Wi-Fi connection. A node in light sleep is a LoRa-only node with no phone.
- The radio must stay in RX to hear a preamble, so the saving is the ESP32 core alone, roughly 40 mA down to 1-2 mA. Option B gets the solar case to microamps with a tenth of the code.
- Wake latency is fine (about 1 ms) but every timer, the U8g2 refresh, GPS UART and the web server need a sleep-aware rewrite.

Revisit once A and B are in and someone asks for a battery-only relay profile.

## 4. Implementation plan (A + B, one PR against upstream dev)

### 4.1 Shared sleep helper

New file `src/esp32/esp32_sleep.cpp` / `.h` with one entry point:

```c
enum SleepReason : uint8_t { SLEEP_MANUAL = 1, SLEEP_LOWBATT = 2 };
void esp32EnterDeepSleep(SleepReason reason, uint32_t wake_after_s);
```

Steps inside, in order:

1. Store `reason` and the last measured voltage in `RTC_DATA_ATTR` variables (survive deep sleep, cost no flash field).
2. Radio to sleep: `radio.sleep()` via RadioLib for all boards (the WP_DISP path keeps `Platform::loraToSleep()`).
3. Display off: `u8g2->setPowerSave(1)` when a u8g2 instance exists; keep the existing Vext handling on Heltec; keep `wpShowDeepSleep()` on e-ink; `tft_off()` on T-Deck.
4. PMU boards (T-Beam, T-Beam Supreme, T-Deck Plus): disable the LoRa and GPS rails, keep the rail that feeds the ESP32, enable PEK wake.
5. GPS: existing `GPS_SWITCH` low.
6. `ADC_BATT_OFF()` where `ADC_CTRL` exists.
7. Wake sources: ext0/ext1 on `iButtonPin` when it is an RTC-capable GPIO, plus GPIO0 on boards that route the PRG button there, plus `esp_sleep_enable_timer_wakeup(wake_after_s)` when `wake_after_s > 0`.
8. `esp_deep_sleep_start()`.

Replace the body of the `--deepsleep` command (`command_functions.cpp:1042`) and the button long-press paths (`onebutton_functions.cpp:255-277`) with calls to this helper. The T114 and T-Echo nRF52 branches stay as they are.

### 4.2 Boot-side wake handling

In `esp32setup()` right after the reset-reason print (`esp32_main.cpp:636`):

1. Read the RTC reason. If `SLEEP_LOWBATT`:
   - Enable the ADC (`ADC_BATT_ON()`), take the unloaded reading before Wi-Fi, BLE and LoRa init.
   - If below the recovery threshold: log one line, call `esp32EnterDeepSleep(SLEEP_LOWBATT, LOWBATT_RECHECK_S)` again. Total awake time stays under 500 ms.
   - If above: clear the RTC reason, set a one-shot flag `bWokeFromLowBatt` and boot normally.
2. `bWokeFromLowBatt` produces one display line ("AKKU LOW -> Sleep, now X.XX V") on the first display head and one serial/BLE info line. This is the OE3WAS request.

### 4.3 Low-battery guard

Restore the block at `batt_functions.cpp:428` with these changes:

- Condition: `bLowBattSleepEnabled && battPresentNow && bSeenCharged && BatVoltage <= BAT_MIN_VOLTAGE && BatVoltage > 1.0`.
- `bSeenCharged` latches true the first time `BatVoltage >= LOWBATT_RECOVER_V` after boot.
- Keep the `CountDown` debounce (6 consecutive readings at 2 Hz, 3 s).
- On trigger: `esp32EnterDeepSleep(SLEEP_LOWBATT, LOWBATT_RECHECK_S)`. Drop the `--display off` call, it persisted `node_sset` and left the display disabled after a wake.
- Thresholds as per-variant defines with defaults: `LOWBATT_RECOVER_V` = `BAT_MIN_VOLTAGE + 0.2`, `LOWBATT_RECHECK_S` = 600.

### 4.4 Setting

- New command `--lowbatt on|off`, persisted in a free `node_sset` bit (check availability first; fallback is a new byte in the settings struct, which would need a `FLASH_STRUCT_VERSION` discussion with Kurt before it goes upstream).
- Default off. `--info` prints the state.
- Report in the PR that the default can flip to on once field feedback exists.

### 4.5 Bench verification

Fleet on the desk: Heltec V3 (dk5en-93), T-Beam v1.2 (dk5en-92), T-Deck Plus (dk5en-14), RAK4631 (dk5en-90). No TLORA available.

| Test                        | Board             | Pass criterion                                                                                                           |
| --------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `--deepsleep` sleep current | Heltec V3, T-Beam | Inline meter on the battery lead below 1 mA (target tens of uA on Heltec)                                                |
| Button wake                 | Heltec V3, T-Beam | Node boots on user button, reset reason line shows DEEPSLEEP                                                             |
| OLED dark in sleep          | Heltec V3, T-Beam | Visual, panel fully off, not just cleared                                                                                |
| Low-batt trigger            | Heltec V3         | Bench supply ramped 3.6 -> 3.2 V, node sleeps after the 3 s debounce, log line present                                   |
| Recovery loop               | Heltec V3         | Supply at 3.3 V: node wakes every 10 min, re-sleeps within 500 ms; supply at 3.6 V: normal boot with the AKKU LOW notice |
| 1053 regression             | Heltec V3         | USB only, no pack: guard never fires (`battPresentNow` false)                                                            |
| Seen-charged latch          | Heltec V3         | Boot at 3.2 V from cold: no sleep until the pack has been above 3.5 V once                                               |
| nRF52 untouched             | RAK4631           | `--deepsleep` behaves as before, build warning-free                                                                      |
| Full build matrix           | all 32 envs       | `pio run` green, resource deltas reported                                                                                |

Prerequisite not on the desk: a bench supply or a discharged pack plus an inline current meter. Without it the sleep-current and threshold rows stay unverified and the PR should say so.

### 4.6 PR description (German, per project rules)

Must list: files and functions changed, why the guard was re-enabled as opt-in, the 1053 root cause (scaling, not USB), the seen-charged latch and `battPresentNow` guard, the RTC-memory reason flag, the measured sleep currents per board, and the boards that were not bench-tested (TLORA, Heltec V2/V4, E22).

Effort estimate: two bench days for A + B including verification, plus one day for the PR text and matrix build.

## 5. References

### Upstream issues and PRs

- Issue 962, "[≈≈≈] deepsleep überarbeiten", karamo, 2026-05-27: https://github.com/icssw-org/MeshCom-Firmware/issues/962
- Issue 662, "Low battery standby mode", closed: https://github.com/icssw-org/MeshCom-Firmware/issues/662
- Issue 1053, "t-beam 1w, default max voltage 4.2 instead of 8.2", closed, caused the global disable: https://github.com/icssw-org/MeshCom-Firmware/issues/1053
- PR 1054 (contains e0043a56 "v4.35p TBEAM 1W deepsleep deactivated"): https://github.com/icssw-org/MeshCom-Firmware/pull/1054
- PR 1050, Wireless Paper deepsleep path, Stego-Lab: https://github.com/icssw-org/MeshCom-Firmware/pull/1050
- PR 862, T114 / Heltec V2 / TTGO deepsleep commits 3c415824, b1964557, 5d75a7f5: https://github.com/icssw-org/MeshCom-Firmware/pull/862
- Issue 992, e-ink deepsleep visibility (referenced in code comments): https://github.com/icssw-org/MeshCom-Firmware/issues/992

### Links posted in issue 962 by karamo

- https://prilchen.de/esp32-sleep-modi-verstehen-und-testen/
- https://www.arrow.com/de/resources/articles/2022/06/esp32-power-consumption-can-be-reduced-with-sleep-modes.html
- https://www.lst-iot.com/de/the-5-most-common-esp32-low-power-traps-with-solutions/

### Code locations (fork-main at ffe31ca5, line numbers match upstream dev within a few lines)

- `src/command_functions.cpp:1042-1118` — `--deepsleep` command
- `src/command_functions.cpp:1019-1040` — `--display off` (persists `node_sset`)
- `src/onebutton_functions.cpp:240-285` — long-press per-board deepsleep dispatch
- `src/batt_functions.cpp:224-265` — `ADC_BATT_ON()` / `ADC_BATT_OFF()`
- `src/batt_functions.cpp:376` — `battPresentNow` runtime no-battery detection (BAT-01)
- `src/batt_functions.cpp:428-463` — commented-out low-battery guard
- `src/batt_functions.h:107-112` — helper prototypes, `bWpAkkuLow`
- `src/loop_functions.cpp:1314-1345` — `sendDisplayHead()` early return and `#C` clear
- `src/loop_functions.cpp:1909-1955` — `wpShowDeepSleep()` e-ink AKKU LOW screen
- `src/esp32/esp32_main.cpp:181, 1933` — `bDEEP_SLEEP` idle loop
- `src/esp32/esp32_main.cpp:620-634` — reset reason to string
- `src/esp32/esp32_pmu.cpp:163-191` — PMU rail setup, rail power-off commented out
- `src/Platforms/WirelessPaper/power_controls.cpp`, `src/Platforms/VisionMasterE213/power_controls.cpp` — `loraToSleep()`, `prepareToSleep()` templates
- `src/nrf52/nrf52_functions.cpp:141-152` — `boardPWROff()` T-Echo
- `src/nrf52/nrf52_main.cpp:1168` — nRF52 `bDEEP_SLEEP` delay loop
- `variants/ttgo-lora32-v21/configuration.h:32-44, 89` — TLORA battery pins, `BAT_MIN_VOLTAGE` 3.3, button GPIO12
- `variants/LilyGo_T-Beam-1W/configuration.h:77` — `BAT_MIN_VOLTAGE` 6.5

### ESP-IDF and library API used by the plan

- `esp_sleep_enable_ext0_wakeup`, `esp_sleep_enable_ext1_wakeup`, `esp_sleep_enable_timer_wakeup`, `esp_deep_sleep_start`, `esp_sleep_get_wakeup_cause`, `esp_reset_reason`
- `RTC_DATA_ATTR` for state that survives deep sleep
- U8g2 `setPowerSave(uint8_t)`
- RadioLib `sleep()`
- XPowersLib `disablePowerOutput(channel)`, PEK IRQ wake
