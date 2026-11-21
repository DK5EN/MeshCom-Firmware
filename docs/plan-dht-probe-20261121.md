# Plan: DHT is reported "found" without probing the sensor (P-0351)

Date: 2026-11-21. Branch `fork-dev` at `067d29cd`. Problem file `P-0351` (split from P-0073 case a2, RCA at pin
`96aaa67c72`). Backlog id `DHT-PROBE-01`. Nothing implemented in this session.

## 1. Symptom

With `--onewire on` the boot log prints `[INIT]...DHTxx Unified Sensor found` and `--status` prints
`...ONEWIRE: on (4) DS18 (...) DHT11 (found)` whether or not a DHT is connected. `--wx` then returns only the error.
The user cannot tell "no sensor on the pin" from "sensor there, read failed". Reported on 4.35p with a T-LoRa T3
(evidence 1326). After the first 30 s tick without a sensor the status flips to `(error)`, so a node with no DHT at all
says "DHT11 (error)", which reads like a broken sensor.

## 2. Root cause at HEAD

`init_onewire_dht()` (`src/onewire_functions.cpp:38`) never reads the sensor:

- `:88` `dht->begin()` only sets the pin to `INPUT_PULLUP` and arms the read timer (Adafruit DHT 1.4.x
  `DHT.cpp` `DHT::begin`; no bus traffic).
- `:90` prints `DHTxx Unified Sensor found` unconditionally.
- `:93-97` `dht->temperature().getSensor(&sensor)` returns the static name the library derives from the constructor
  type, and `dht_name_matches(sensor.name, node_dht_type)` (`src/settings_sanitize.cpp:157-162`) compares that name
  with the same configured type, so it is true for every valid type. `dht_found = true` at `:97`.
- The first real read happens in `loop_onewire_dht()` (`:126`); a NaN there sets `dht_found = false` (`:151` temperature,
  `:173` humidity) and the loop gate `if(dht_found)` (below) never reads again until the next init.
- Status: `src/command_functions.cpp:8061` `snprintf(cdht, sizeof(cdht), " (%s)", (dht_found?"found":"error"))`, printed
  at `:8077` (`...ONEWIRE: %s (%i) DS18%s DHT%i%s`).
- `dht_ok` (`onewire_functions.cpp:17`, set `:130`/`:188`, SENS-03) is the last-read-valid flag. It feeds only the RM
  `sens` path (`src/rm_exec_read.cpp:127`, `f.oneWireDhtFound = dht_ok`) and is not shown in the status line or in the
  boot log.

History: the init block is from `796873e7` (Kurt, 2025-07-20); `2ade9625` (SENS-04, 2026-11-08) only added the configured
pin and type and the `dht_name_matches` call; `2c1108e2` (SENS-03, 2026-11-02) added `dht_ok`. All are ancestors of HEAD
and none reverts another. `git log --oneline 96aaa67c72..HEAD -- src/onewire_functions.cpp src/command_functions.cpp
src/rm_exec_read.cpp src/settings_sanitize.cpp` shows `8b4d509b`, `ce05f38c`, `f4af55b0`; none touch the DHT lines (the
cited line numbers are unchanged from the pin except `command_functions.cpp`, `:7961` at the pin is `:8061` now).

Read-side facts the fix relies on (library at `.pio/libdeps/E22_1262-DevKitC/DHT sensor library`, `DHT.cpp`):
`begin()` sets `_lastreadtime = millis() - MIN_INTERVAL`, so the first `readTemperature()` reads the bus at once;
the following `readHumidity()` inside 2 s returns the cached result of that same read. A missing sensor (pull-up only)
times out in `expectPulse()` and the read returns NaN. A DHT read blocks about 21 ms (DHT11, `delay(20)` start signal)
or about 2 ms (DHT22).

## 3. Platforms

| Platform                                                      | Reaches the code?                                                                                                                                                                                                                                                       |
| ------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32 (boards with `OneWire_GPIO > 0`)                        | Yes. `init_onewire_dht()` at `src/esp32/esp32_main.cpp:1984` (setup), loop gate `if(dht_found) loop_onewire_dht();` at `:5322-5323`, first tick at 30 s after boot (`onewireTimeWait` is 0, `:5303`).                                                                   |
| nRF52 (only `wiscore_rak4631`, `OneWire_GPIO WB_IO1`, RAK130) | Yes. `init_onewire_dht()` at `src/nrf52/nrf52_main.cpp:960` (before `api_init_lora()`), loop gate `:2734-2735`, `onewireTimeWait = millis() - 10000` so the first tick is about 20 s after boot (`:2723-2724`). T114 and T-Echo do not define `OneWire_GPIO` (no code). |
| Runtime                                                       | `--onewire on` (`command_functions.cpp:3241`), and the `--dht`/pin commands (`:3308`, `:3341`) call `init_onewire_dht()` after the process is long up.                                                                                                                  |

Both mains share `src/onewire_functions.cpp`, `src/command_functions.cpp` and the globals
(`src/loop_functions.cpp:477`, `src/loop_functions_extern.h:216-217`). Both mains gate the loop on `dht_found`, so both
need the same gate change. Boards with `OneWire_GPIO -1` (Heltec V3/V4, tracker, wireless paper, T-Beam Supreme,
...) return early at `:60-79` and never reach the probe unless `--owgpio` is set.

## 4. Fix design

Goal: `found` means "a read returned numbers". Three user-visible states instead of one lie.

1. Probe on the first loop tick, not in `init_onewire_dht()`. The DHT11 datasheet wants about 1 s after power-up
   before the first read, and `init_onewire_dht()` on nRF52 runs a few hundred milliseconds after a cold start. The
   first tick is 20 to 30 s after boot (and up to 30 s after a runtime `--onewire on`, when the sensor has long been
   powered), so the settle problem does not arise and `setup()` is not blocked. Init keeps `dht_found = false`,
   `dht_ok = false`, and arms `dht_probe_left = DHT_PROBE_TRIES` (3 attempts, so a probe tolerates two failed reads: about
   60 s of tries at the 30 s tick).
2. Pure decision functions in `src/settings_sanitize.{h,cpp}` next to `dht_type_valid` and `dht_name_matches` (already
   compiled in `[env:native]`, `platformio.ini:299`, suite `test_settings_sanitize`):
   - `void dht_probe_step(bool &found, uint8_t &tries_left, bool read_valid)`: if already found, nothing changes
     (a later bad read must not clear it); on a valid read `found = true`, `tries_left = 0`; on an invalid read
     `tries_left` decrements down to 0.
   - `const char *dht_status_word(bool found, bool last_read_ok, uint8_t tries_left)`: found and ok gives `found`;
     found and not ok gives `read error`; not found and `tries_left > 0` gives `probing`; else `not found`.
3. `src/onewire_functions.cpp`: remove the `Unified Sensor found` print and the `dht_name_matches` block (`:90-97`); keep
   `getSensor()` only to set `delayMS`; arm the probe counter. In `loop_onewire_dht()` run while
   `dht != nullptr && (dht_found || dht_probe_left > 0)`, do the existing two `getEvent()` calls, then
   `dht_probe_step(dht_found, dht_probe_left, read_ok)` and `dht_ok = read_ok`. Delete the two `dht_found = false` at `:151`
   and `:173` (a transient NaN no longer permanently disables a sensor that was found; `dht_ok` carries the current
   state). Log `[INIT]...DHTxx found` / `not found` once, on the transition, instead of the unconditional line.
4. Both mains: the gate `if(dht_found) loop_onewire_dht();` becomes an unconditional `loop_onewire_dht();` (the function
   gates itself; one line each, `esp32_main.cpp:5322`, `nrf52_main.cpp:2734`).
5. `src/command_functions.cpp:8059-8061`: `cdht` becomes 16 bytes (`" (read error)"` is 13 characters plus the
   terminator; the current 10 would truncate) and prints `dht_status_word(dht_found, dht_ok, dht_probe_left)`.
   `--onewire on`/`off` (`:3223`, `:3249`) keep resetting `dht_found`; add `dht_probe_left = 0` on `off`.
6. New extern `dht_probe_left` next to `dht_found` (`loop_functions_extern.h:216`, definition `loop_functions.cpp:477`).
7. `dht_name_matches` loses its only caller: remove it and `test_dht_name_matches`
   (`test/test_settings_sanitize/test_settings_sanitize.cpp:349`), or keep both if the operator wants the constructor
   cross-check; default in this plan is to remove (no dead code).

Reuse: `dht_ok` and `rm_exec_read.cpp:127` stay as they are (RM `sens` already reports only a valid read, so that path is
correct today and its behaviour does not change, except that it can now recover after a transient NaN). No wording
elsewhere parses `(found)` or `(error)` (`grep -rnE "DHT.{0,6}\((found|error)\)" tools test docs` finds no consumer; the
golden dumps under `test/golden` have `bONEWIRE` off and print no DHT suffix).

Out of scope: DS18. `init_onewire_ds18()` also sets `one_found = true` straight after `ds.begin()`
(`onewire_functions.cpp:230`, `:248`), while `ds18_found` (`:17`) is the real CRC-valid flag; a separate item if wanted,
the status line could print `ds18_found` with the same helper pattern.

## 5. Test

Host (no hardware): the decision logic, in suite `test_settings_sanitize`, env `native` (it already lists the suite).
Add to `test/test_settings_sanitize/test_settings_sanitize.cpp` (`RUN_TEST` count 28 now):

| Test                                             | Assertion                                                                                                                                                                                                                                                           | Fails today because                                                                                                         |
| ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `test_dht_probe_no_sensor_never_found`           | `found = false`, `tries = 3`; three `dht_probe_step(found, tries, false)` leave `found == false`, `tries == 0`.                                                                                                                                                     | `dht_probe_step` does not exist (compile error is the before-run); the old init sets found true.                            |
| `test_dht_probe_valid_read_found`                | A valid read on the 2nd attempt gives `found == true`, `tries == 0`.                                                                                                                                                                                                | same                                                                                                                        |
| `test_dht_found_survives_bad_read`               | After `found`, a later invalid read leaves `found == true` (regression for the permanent disable at `:151`, `:173`).                                                                                                                                                | same                                                                                                                        |
| `test_dht_status_words`                          | `(true,true,0)` gives `found`, `(true,false,0)` gives `read error`, `(false,false,2)` gives `probing`, `(false,false,0)` gives `not found`; longest word plus `" ()"` fits 16 bytes.                                                                                | same                                                                                                                        |
| `test_dht_init_has_no_static_found` (source pin) | `slurp("src/onewire_functions.cpp")` contains `dht_probe_step(` and contains neither `dht_name_matches(` nor `Unified Sensor found`; `slurp("src/command_functions.cpp")` contains `dht_status_word(`. Pattern: `test/test_ping_budget/test_ping_budget.cpp:47-57`. | `onewire_functions.cpp:90` and `:95` contain exactly those strings; `command_functions.cpp:8061` hardcodes `found`/`error`. |

The source pin is the test that is red on unchanged logic: it pins that detection comes from a read and not from the
driver name. The probe against a real bus cannot run on the host (the DHT library needs pins); the seam is the pure
step function plus the pin. Gate after the change (one `pio` at a time): `pio test -e native -f test_settings_sanitize`,
`pio run -e ttgo_tbeam` (ESP32 with `OneWire_GPIO 4`) and `pio run -e wiscore_rak4631` (nRF52 with `OneWire_GPIO`).

## 6. Bench

Needs a board with a DHT11 or DHT22 and a free GPIO (the bench fleet's T-Beam, `OneWire_GPIO 4`; see the local `/bench`
skill; identity guard first, `lsof <port>` first, TEST group and own call sign only, no on-air test is needed).

1. No sensor on the pin: boot with `--onewire on`. Expected: no `found` line at boot; `--status` right after boot shows
   `DHT11 (probing)`; after 60 to 90 s `DHT11 (not found)`; `--sens` over RM answers as today (nothing present).
2. Sensor connected: `--onewire on` (or reboot). Expected: `(probing)` until the first tick, then `(found)`;
   `--wx` returns temperature and humidity; RM `sens` lists T/H.
3. Disconnect the sensor while running. Expected: `(read error)`, polling continues, reconnect returns to `(found)`
   without `--onewire on` (before the fix: `(error)` forever).
4. `--dht 22` with a DHT11 attached: the read fails or returns wrong data; record what `(found)` means then (a DHT11
   read through the DHT22 decoder often returns plausible numbers; the probe cannot tell the types apart).

## 7. Upstream impact

Yes, affected. `git show upstream/dev:src/onewire_functions.cpp` has the same init: `dht->begin()` at `:82`, the
unconditional print at `:84`, `dht_name_matches` at `:89`, `dht_found = true` at `:91`, and the NaN clears at `:143`,
`:164`; status at `upstream/dev:src/command_functions.cpp:7597`; gate at `upstream/dev:src/esp32/esp32_main.cpp:5062`.
Upstream has no `dht_ok` (count 0). PR shape: the same change without the `dht_ok` lines; German body, from
`upstream/dev`, minimal port (helpers, `onewire_functions.cpp`, the two gates, the status line). Nothing is sent from this
plan.

## 8. Risks

- Late verdict: the status says `probing` for up to 30 s after boot or `--onewire on`. Accepted; the alternative
  (blocking probe in setup) risks false "not found" on a cold DHT11 on nRF52.
- After three failed probes the node stops reading until the next init (same as today's stop after one NaN); a sensor
  plugged in later needs `--onewire on` again. Variant: keep probing every tick while not found (21 ms blocking read per
  30 s with a DHT11). Open question for the operator.
- A found sensor now keeps being polled after a transient NaN; `node_temp`/`node_hum` keep their last value on a bad
  read (existing behaviour of the NaN branches). Beacons may therefore carry a stale value while the status says
  `read error`.
- Output wording changes (`not found`, `read error`, `probing` instead of `error`). No tool in the tree parses it; field
  scripts outside the tree might.
- `dht_name_matches` removal touches an upstream-visible helper and one existing test.
- DHT11 and DHT22 share the 1-wire-like protocol; a probe proves "something answered with a valid checksum", not the
  type.

## 9. Decisions needed (operator)

- D1, after three failed probes: (a) stop polling, as today after a NaN (no bus traffic, a sensor plugged in later is
  only found after a reboot; recommended), or (b) keep probing every tick (hot-plug works, a missing sensor costs a
  bus read per tick and the status stays `probing`).
- D2, `dht_name_matches` loses its only caller: (a) remove it and `test_dht_name_matches` (recommended, no dead code),
  or (b) keep it for a later type check (dead code until then).

## 10. Evidence checklist

- Cited commits `796873e7`, `2ade9625`, `2c1108e2` are ancestors of HEAD (`git merge-base --is-ancestor`, rc 0 for the
  latter; all appear in `git blame` of `:88-97`/`:17`); none is reverted.
- Every path:line in section 2 and 3 was read at HEAD (`067d29cd`).
- ESP32 and nRF52 checked separately: `esp32_main.cpp:1984`, `:5303`, `:5322`; `nrf52_main.cpp:960`, `:2723-2735`;
  `OneWire_GPIO` list from `variants/*/configuration.h` (nRF52: only `wiscore_rak4631`).
- Not verified: Adafruit library version actually resolved for each env (read from the `E22_1262-DevKitC` libdeps copy,
  `library.json` missing there; `platformio.ini:74` pins `^1.4.6`); no hardware run.
