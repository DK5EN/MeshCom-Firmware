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
- T114 and T-Echo: `bDEEP_SLEEP` flag, peripherals cut, main loop spins `delay(60000)` (`nrf52_main.cpp:1168`). Not broken, but not the same feature. Section 6 has the full analysis and the nRF52 plan.

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

## 6. nRF52 boards (RAK4631, T114, T-Echo): verdict and plan

Added 2026-09-05 after checking the fork tree, the Adafruit nRF52 core 1.10700.0, SX126x-Arduino 2.0.32, RAK's documentation, beegee's RAK4631-DeepSleep example and Meshtastic's nRF52 sleep path. Sources in 6.6.

### 6.1 Verdict

The proposal "take the loop task away from the scheduler, put LoRa to sleep, cut the peripheral rail" describes RAK's **System ON** low-power pattern. That pattern is real and documented, but it is the wrong tool for `--deepsleep` and for the low-battery guard. Both want **System OFF**, which is one core call and behaves exactly like the ESP32 deep sleep (wake is a reset). Recommendation:

1. Implement `--deepsleep` on all three nRF52 boards as System OFF via the core's `systemOff(pin, level)`. About 20 lines, no new library, no task surgery.
2. Bring the low-battery guard to nRF52 with the same opt-in setting as on ESP32, but with an **LPCOMP wake on battery recovery** instead of the ESP32 timer wake, because System OFF has no timer wake source.
3. Do not build the semaphore-blocked "light sleep" now. It is cheap on nRF52 (unlike ESP32, BLE survives it), but MeshCom's loop is millis-polling throughout and the USB CDC task defeats it whenever a cable is plugged in.

### 6.2 What the nRF52 side does today, and why it is not a sleep

| Board   | Trigger                                                                               | What happens                                                                                                                                                                                                                                                                         | What is still burning                                                                                                                                                                                                                                                                                                                                                                                                       |
| ------- | ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RAK4631 | `--deepsleep`                                                                         | `#if not defined(BOARD_RAK4630)` skips `esp_deep_sleep_start()`. Only the shared preamble runs, and none of its `#if`s (`vEXT_CTRL`, `GPS_SWITCH`, Heltec Vext) match the RAK. Nothing changes. No long-press path either: `BUTTON_PIN` is `WB_IO6` and only exists with a RAK13002. | Everything: SX1262 in continuous RX, W5100S on `3V3_S`, RAK12500 GPS on `3V3_S`, BLE advertising, USB CDC.                                                                                                                                                                                                                                                                                                                  |
| T114    | `--deepsleep`, long press                                                             | `stop_advertising()`, GPS rail (`PIN_VEXT_CTL`) off, TFT LEDA and VDD off, `LORA_NRSET` pulled LOW (SX1262 held in reset), `bDEEP_SLEEP = true`, loop body replaced by `delay(60000)` (`nrf52_main.cpp:1168`).                                                                       | USB CDC task, LORA task (idle on its semaphore, harmless), SoftDevice with BLE stack initialised but not advertising. The CPU itself does sleep between ticks: `delay()` is `vTaskDelay()` and the core runs `configUSE_TICKLESS_IDLE 1` with `sd_app_evt_wait()` in the idle path. So this is a soft-off in the low tens of uA range if USB is unplugged, not a fault. Exit: a second `--deepsleep` over serial, or RESET. |
| T-Echo  | long press only (`boardPWROff()`, `nrf52_functions.cpp:143`); `--deepsleep` unhandled | Battery-empty logo on the e-ink, `Power_On_Pin` LOW (peripheral rail), `bDEEP_SLEEP = true`, same `delay(60000)` loop.                                                                                                                                                               | BLE keeps advertising (no `stop_advertising()`), SX1262 is not put to sleep (`Radio.Sleep()` never called; whether the rail cut reaches the radio depends on the T-Echo power tree), USB CDC. No wake other than RESET.                                                                                                                                                                                                     |

Two facts that the current code gets right by accident and the proposal would get wrong on purpose:

- The loop task is never "taken from the scheduler" today, it blocks in `vTaskDelay()`. Blocking is all that is needed: the Adafruit core's FreeRTOS is tickless, so as soon as every task is blocked the port calls `sd_app_evt_wait()` and the nRF52840 is in System ON low power. `suspendLoop()` (core `main.cpp:101`, a `vTaskSuspend` on the loop task) exists but is not needed for that.
- The current draw in that state is set by the peripherals, not by the CPU. RAK's own guidance and forum measurements: nRF52840 + SoftDevice blocked on a semaphore is 2-3 uA; an SX1262 that was initialised but not sent to sleep is 1.5 mA, after `Radio.Sleep()` 13 uA; USB CDC keeps a FreeRTOS task running that "never sleeps" and must not be initialised for the low figures. On the RAK gateway build the W5100S alone is up to 132 mA.

### 6.3 The two nRF52 sleep models, and which one `--deepsleep` needs

| Property                   | System ON low power (RAK "semaphore loop")                                                                                                                             | System OFF (`systemOff()`)                                                                                                                                                                                       |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Entry                      | Every task blocks (`xSemaphoreTake(sem, portMAX_DELAY)` in the loop, or `suspendLoop()`); tickless idle calls `sd_app_evt_wait()`                                      | `sd_power_system_off()` (SoftDevice active) or `NRF_POWER->SYSTEMOFF = 1`; the Adafruit core wraps both in `systemOff(pin, wake_logic)` (`wiring.c:147`) together with the GPIO SENSE setup                      |
| State                      | RAM, SoftDevice, BLE connection, RTC, all peripherals kept; code continues after the blocking call                                                                     | CPU and all peripherals off; wake is a **reset**, execution restarts at the reset vector, `RESETREAS` reports the OFF wake. RAM retention is optional per section and off by default                             |
| Wake sources               | Any interrupt: LoRa DIO1 (SX126x-Arduino LORA task), FreeRTOS timer, BLE event, GPIO, UART                                                                             | GPIO DETECT via SENSE (the button), LPCOMP (analog threshold, e.g. battery voltage), NFC field, VBUS detected (USB plugged in), RESET pin. **No RTC or timer wake**                                              |
| Current, module only       | 2-3 uA MCU; 13 uA with SX1262 in sleep; 35 uA in RAK's LoRaWAN example; 120 uA TX-only P2P with a wake timer; about 6 mA with `SetRxDutyCycle` and four nodes chatting | Datasheet System OFF is sub-uA to about 1.5 uA depending on RAM retention; RAK quotes 2.0 uA for the RAK4631 with LoRa and BT asleep. The 3V3_S rail must be off, otherwise the sensors, GPS and W5100S dominate |
| Fits `--deepsleep`         | Only as a "soft off" like the T114 today. Needs the whole polling loop rewritten around events to be more than that                                                    | Yes. Identical semantics to `esp_deep_sleep_start()`, which is what the command already means on ESP32                                                                                                           |
| Fits the low-battery guard | No: the ESP32 plan's "sleep 10 min, wake, measure" cannot be built without a wake timer, and a timer-driven System ON loop keeps the SoftDevice and USB path alive     | Yes, via LPCOMP: arm the comparator on the battery divider with an upward threshold and the node wakes by itself when the pack has recharged. Meshtastic uses exactly this for its nRF52 low-battery shutdown    |

Meshtastic's nRF52 path (`cpuDeepSleep()` in `main-nrf52.cpp`) is the reference implementation of the System OFF variant: `Wire.end()`, `SPI.end()`, `Serial.end()`, `Serial1.end()`, BLE off, `PIN_3V3_EN` (= `WB_IO2`, "1 is on") LOW on RAK4631, variant shutdown, LPCOMP armed for battery recovery, then `sd_power_system_off()`. Their bug 4378 ("freezing the board") was about entering that path, not about the mechanism, and issue 2822 is the open request for a timer wake, which confirms that System OFF has none.

### 6.4 Plan for nRF52 (fits into the PR from section 4)

#### 6.4.1 `nrf52EnterDeepSleep(SleepReason reason)` in `src/nrf52/nrf52_sleep.cpp`

Order matters: quiet the radio first, cut rails second, arm wake last.

1. Log one line, `Serial.flush()`. The reason goes into `GPREGRET2` (retained through System OFF, the same idea as the ESP32 `RTC_DATA_ATTR`). Boot-side read in `nrf52setup()` before the display comes up, matching 4.2.
2. `Radio.Sleep()` (SX126x-Arduino, warm start). On T114 this replaces the `LORA_NRSET` LOW trick; a chip in reset is fine but a chip in sleep is the documented state and re-inits cleanly after the wake reset.
3. BLE: `stop_advertising()`, then `Bluefruit.disconnect()` if a phone is attached. The SoftDevice stays up; it is what executes `sd_power_system_off()`.
4. Display: T114 TFT LEDA and VDD off as today; T-Echo e-ink `Batterie_Vide_logo()` and rail off as today; RAK has no display.
5. Rails: RAK `WB_IO2` LOW (3V3_S off: kills W5100S, RAK12500 and every sensor slot in one stroke); T114 `PIN_VEXT_CTL` LOW; T-Echo `Power_On_Pin` LOW.
6. `Serial.end()`, `Serial1.end()`, `Wire.end()`, `SPI.end()` as in Meshtastic, so no peripheral clock and no pull-up leaks.
7. Wake: `systemOff(iButtonPin, LOW)` when a button pin is configured (T114, T-Echo, RAK with RAK13002). When none is configured, call `sd_power_system_off()` directly and document that the node wakes on RESET or on USB plug-in only.
8. For `SLEEP_LOWBATT` additionally arm LPCOMP on the battery ADC input with `NRF_LPCOMP_REFSEL_*` at the recovery threshold, detect UP, before step 7. The comparator input must be one of AIN0-7; check the variant's `PIN_VBAT`.

#### 6.4.2 Command and button dispatch

- `--deepsleep` on nRF52: replace the `#if not defined(BOARD_RAK4630)` block and the T114 `bDEEP_SLEEP` toggle with `nrf52EnterDeepSleep(SLEEP_MANUAL)`. The `bDEEP_SLEEP` flag, the `delay(60000)` loop and the "second `--deepsleep` wakes it" behaviour go away; the wake is the button or RESET, same as ESP32.
- Long press T114 and T-Echo: call the command. `boardPWROff()` becomes a thin wrapper or is deleted.
- Low-battery guard (4.3): same condition and same opt-in bit; on nRF52 the trigger calls `nrf52EnterDeepSleep(SLEEP_LOWBATT)`. The "recheck every 10 min" loop from 4.2 does not exist here, the LPCOMP wake replaces it.

#### 6.4.3 Bench verification (RAK4631 dk5en-90 on the desk, T114 and T-Echo not on the desk)

| Test                            | Board                       | Pass criterion                                                                                                                                                                                                                             |
| ------------------------------- | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `--deepsleep` enters System OFF | RAK4631                     | Serial port disappears, W5100S link LED off (3V3_S cut), inline meter on VBAT below 50 uA. Without a meter: USB current on a hub with readout, expect the module at the noise floor                                                        |
| Wake on USB plug-in             | RAK4631                     | Replug USB: node boots, `[INIT]` shows reset reason OFF (`sd_power_reset_reason_get`, print it, the stub for it is already in `boardInit()` as a comment). Watch for the DevZone case where the UF2 bootloader lands in DFU on a VBUS wake |
| Wake on button                  | RAK4631 + RAK13002, or T114 | Press: boot with reset reason OFF, reason byte from `GPREGRET2` says manual                                                                                                                                                                |
| LPCOMP recovery wake            | RAK4631                     | Bench supply on VBAT at 3.2 V: guard fires, node sleeps; ramp to 3.6 V: node boots by itself, reason byte says low-batt, "AKKU LOW" line printed once                                                                                      |
| Ethernet after wake             | RAK4631                     | DHCP lease and `[BOOT];ready;...;eth;1` after the wake reset, no manual reset needed                                                                                                                                                       |
| Serial-open recovery            | RAK4631                     | Bench harness still works after the wake (the 1200-baud touch rescue in the RAK pitfalls note is the fallback)                                                                                                                             |

Prerequisites: the inline meter and bench supply from 4.5, and a RAK13002 IO board if the button wake is to be proven on the RAK itself. Without a RAK13002 the RAK proof is USB and LPCOMP only, and the button row is a T114 job.

### 6.5 What to tell issue 962 about nRF52

"Wie beim ESP32 braucht es keinen Eingriff in den Scheduler. Der nRF52 hat einen echten System-OFF-Modus (`systemOff()` im Adafruit-Core, ~2 uA), aus dem er wie der ESP32 per Reset aufwacht: Taste, USB-Stecken oder ein LPCOMP-Schwellwert auf der Akkuspannung. Einen Timer-Wakeup gibt es dort nicht, deshalb wird die Akku-Hysterese auf dem nRF52 ueber den Komparator geloest statt ueber ein 10-Minuten-Intervall. Vorher muessen SX1262 (`Radio.Sleep()`), BLE-Advertising, Display und der `3V3_S`-Pin (RAK `WB_IO2`) aus, sonst bleibt der Modul-Strom im mA-Bereich."

### 6.6 Sources checked 2026-09-05

- Adafruit nRF52 core 1.10700.0, local: `cores/nRF5/main.cpp` (loop task, `suspendLoop()` / `resumeLoop()`), `cores/nRF5/wiring.c:147` (`systemOff()` = GPIO SENSE + `sd_power_system_off()`), `cores/nRF5/delay.c` (`delay()` = `vTaskDelay()`), `freertos/config/FreeRTOSConfig.h:52` (`configUSE_TICKLESS_IDLE 1`), `port_cmsis_systick.c:203` (`sd_app_evt_wait()` in the idle path)
- SX126x-Arduino 2.0.32, local: `radio.cpp:1062` (`RadioSleep()` warm start), `board.cpp:489` (LORA task on its own semaphore), `lora_hardware_uninit()` suspends that task
- RAK WisBlock Low Power Example (LoRaWAN): semaphore-blocked loop, "the Serial port MUST NOT be initialized ... FreeRTOS is as well starting a task ... that prevents the MCU from sleeping": https://github.com/RAKWireless/WisBlock/blob/master/examples/RAK4630/communications/LoRa/LoRaWAN/Low_Power_Example.md
- beegee-tokyo/RAK4631-DeepSleep (LoRa P2P, the example the user pointed at): `xSemaphoreTake(taskEvent, portMAX_DELAY)` in `loop()`, timer ISR gives the semaphore, `Radio.Sleep()` in every OnTx/OnRx callback under `TX_ONLY`, else `Radio.SetRxDutyCycle()`; README quotes 120 uA TX-only and about 6 mA with duty-cycle RX and traffic: https://github.com/beegee-tokyo/RAK4631-DeepSleep/tree/main/PlatformIO/LoRa-DeepSleep/src
- RAK forum "RAK4631 deep sleep without LoRaWAN": SX1262 not initialised = idle 1.5 mA, after `Radio.Sleep()` 13 uA, LoRaWAN example 35 uA, `Bluefruit.Advertising.stop()`: https://forum.rakwireless.com/t/rak4631-deep-sleep-without-lorawan/10942
- RAK forum "RAK4631 deep sleep until reset", beegee: `Radio.Sleep()` before `sd_power_mode_set()`, 2-3 uA measured: https://forum.rakwireless.com/t/rak4631-deep-sleep-until-reset/7253
- RAK4631 datasheet, 2.0 uA sleep with LoRa and BT asleep: https://docs.rakwireless.com/product-categories/wisblock/rak4631/datasheet/
- RAK19007 quick start, "Set IO2=1, 3V3_S is on. Set IO2=0, 3V3_S is off.": https://docs.rakwireless.com/product-categories/wisblock/rak19007/quickstart/
- RAK13800 datasheet, W5100S powered from `3V3_S`, 132 mA active: https://docs.rakwireless.com/product-categories/wisblock/rak13800/datasheet/
- Meshtastic `cpuDeepSleep()` for nRF52 (`Wire/SPI/Serial.end()`, BLE off, `PIN_3V3_EN` LOW, LPCOMP, `sd_power_system_off()`): https://github.com/meshtastic/firmware/blob/master/src/platform/nrf52/main-nrf52.cpp
- Meshtastic RAK4631 variant, `PIN_3V3_EN (34)`, "IO2 is ALSO used to control 3V3_S power (1 is on)": https://github.com/meshtastic/firmware/blob/master/variants/nrf52840/rak4631/variant.h
- Meshtastic issue 4378 (nRF52 low-battery System OFF) and 2822 (no timer wake from System OFF on nRF52): https://github.com/meshtastic/firmware/issues/4378, https://github.com/meshtastic/firmware/issues/2822
- Nordic DevZone on System OFF and VBUS wake: "The device will always wake up from system OFF when voltage is detected on the VBUS", and the bootloader-lands-in-DFU trap: https://devzone.nordicsemi.com/f/nordic-q-a/120350/nrf5-sdk-system-power-off-mode-wake-up-issue
- nRF52840 Product Specification, POWER chapter (System OFF wake sources, wake is reset, RAM retention, I_OFF): https://docs.nordicsemi.com/bundle/ps_nrf52840/page/power.html. The page itself could not be fetched here (too large for the tool), the statements above are from the datasheet as known and consistent with every secondary source listed.
