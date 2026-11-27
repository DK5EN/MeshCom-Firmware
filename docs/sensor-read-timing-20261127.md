# Sensor read timing: read only before the position beacon? (2026-11-27)

Trigger: Telegram, Kurt OE1KBC, 2026-11-27, after the talk on 2026-11-26. Code state: `fork-dev` `d787c11b`.

## Verdict

- **Proposal A (read sensors only shortly before the next position beacon): not recommended.** The saving is a few
  milliseconds of I2C per minute; the values have more consumers than the beacon; several sensors need continuous
  reads. Kept as low-priority idea SENS-07.
- **Proposal B (spread the sensor calls over different loop passes): largely in place.** Every sensor has its own
  timer; BMP3, MCU811 (CCS811) and INA226 already run through the shared loop scheduler. Only BMx280/AHT20/SHT21 share
  one timer and one pass, at a cost of a few ms.
- **Real defect found on the way: SENS-06.** The CCS811 (CO2, "MCU811" in the code) driver blocks the whole loop with
  `delay(500)`, and the runtime command `--811 on` blocks it for up to 5.5 s.

## The proposal

Kurt observed that measured values go out over LoRa only with the position beacon. Two suggestions:

- **A:** read the sensors only a short time before the next position is sent; saves loop time and energy.
- **B:** spread the sensor calls over different loop passes, so the message loop stays less loaded.

## What the code does today

| Sensor                | Read interval                 | Where                                                    | Cost per read                       |
| --------------------- | ----------------------------- | -------------------------------------------------------- | ----------------------------------- |
| BMx280, AHT20, SHT21  | 60 s, one shared timer        | `esp32_main.cpp` ~5500 (`BMXTimeWait`), nRF52 same block | I2C, a few ms; BMx280 in two passes |
| BMP3, MCU811, INA226  | own timers, loop scheduler    | `loop_scheduler.h`, `loop_actions_esp32/nrf52.cpp`       | I2C, a few ms (MCU811: see SENS-06) |
| BME680                | 60 s or sensor end time       | `esp32_main.cpp` ~5570 (`bme680_timer`)                  | I2C, asynchronous end time          |
| OneWire (DS18B20/DHT) | 30 s                          | `esp32_main.cpp` ~5420 (`onewireTimeWait`)               | with retry reduction                |
| Wind                  | every 1 s (pulse count)       | `esp32_main.cpp` ~5480                                   | counter read, no bus                |
| Telemetry beacon      | own timer, default 15 min     | `esp32_main.cpp` ~5305 (`telemetry_timer`)               | sends the current values            |
| Position beacon       | `posinfo_interval`, own timer | `esp32_main.cpp` ~5128 (`posinfo_timer`)                 | sends the current values            |

The values land in `meshcom_settings.node_*`. Consumers besides the position beacon: `--wx`, web GUI, display weather
page, APRS-IS client, extended UDP, remote management (`sens`), the telemetry beacon, BLE settings to the phone.

## Why proposal A does not pay off

- **Saving is negligible.** One I2C read takes a few ms, once a minute. Power goes into LoRa RX, ESP32 CPU, WiFi and
  the display. The BMx280 runs in forced mode and sleeps between measurements on its own.
- **Stale values elsewhere.** Reading only before the beacon leaves web GUI, display, `--wx`, APRS-IS and telemetry
  with values up to one beacon interval old.
- **Some sensors need continuous reads.** Wind needs pulse counting with mean and gust over time; the CCS811 needs a
  warm-up; the BMx280 needs two steps (start measurement, read later). A single read "shortly before" would need lead
  time and retries per sensor: more code and more failure paths for no measurable gain.
- **Where energy really is:** CPU sleep states, WiFi, display; see PWR-02 (low-power relay profile).

## Proposal B

Every sensor already has its own timer, so their calls only meet in one pass by coincidence. A fixed phase offset per
sensor (for example BMx280 at second 0, AHT20 at 10, OneWire at 20) would make that deterministic. With the current
cost per read it does not change loop latency measurably. Part of SENS-07, low priority.

## SENS-06: blocking delay in the CCS811 driver

- `loopMCU811()` (`src/mcu811.cpp:79-92`): when `ccs.available()` is false, the loop body calls `delay(500)` once and
  returns `false`. The `while` never loops a second time (unconditional `return` inside), so the counter
  `maxMCU811ValideCount` is dead. Each miss stops the whole main loop for 500 ms: no LoRa RX handling, no TX ring, no
  web, no BLE. ESP32 and nRF52 (shared driver, called from `loop_actions_esp32.cpp:250` and
  `loop_actions_nrf52.cpp:145`).
- `setupMCU811()` (`src/mcu811.cpp:46-57`): up to 10 x `delay(500)` = 5.5 s until the sensor reports ready. At boot
  this is tolerable; but `--811 on` (`command_functions.cpp:2874`) calls it at runtime from the loop, so a command from
  console, web or phone stops the node for up to 5.5 s.
- Fix direction: no `delay()` in either path. Loop read: return `false` on "not available" and let the scheduler retry
  on the next interval (or a short retry interval, as BMx280 and OneWire do with `lreduction`). Setup: a non-blocking
  ready state machine (probe each pass until ready or a deadline), `mcu811_found` set when ready.
- Evidence needed: a host test that drives `loopMCU811()` / setup with a stub CCS811 that reports "not available" and
  asserts no blocking wait (fails before, passes after); bench with a CCS811 if one is at hand.
