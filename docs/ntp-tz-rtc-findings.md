# NTP + TZ string + onboard RTC: findings

Date: 2026-10-04. Tree: `fork-dev` at `94ca1a50`. Status: analysis only, nothing changed, nothing
bench-verified.

## BLUF

- **No TZ string exists.** Local time is one fixed float, `node_utcoff`. The manual change at the
  DST switch, twice a year, is real.
- **Worth implementing:** a POSIX TZ string (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`) from which the
  effective offset is derived at runtime.
- **`{CET}` is not part of the problem:** the frame carries UTC; only its name is misleading.
- **The onboard RTC has two offset bugs and one timer bug.** Fix them first as their own small PR,
  then add the TZ string.

## 1. Current state

| Item                     | Where                                                  | Notes                                                       |
| ------------------------ | ------------------------------------------------------ | ----------------------------------------------------------- |
| Offset setting           | `src/meshcom_settings.h:106`                           | `float node_utcoff`, default `MC_PLATFORM_DEFAULT(2, 0)`    |
| Config key               | `src/config_json.h:272`                                | `node_utcof`, range -12.0 .. 14.0                           |
| Serial command           | `src/command_functions.cpp:693`                        | `--utcoff +/-99.9`                                          |
| Time zone label          | `src/loop_functions.cpp:6277` `getTimeZone()`          | returns only `"UTC"` (offset 0) or `"LT"`                   |
| NTP update interval      | `src/configuration_global.h:555`                       | `NTP_UPDATE_TIME 240` min                                   |
| NTP servers              | `src/udp_functions.cpp:1089-1180`                      | `node_ntp` / `node_ownntp`, Hamnet literals, `pool.ntp.org` |
| Central clock set funnel | `src/clock.cpp:340` `Clock::SetClock(time_t, bool)`    | every clock source ends here                                |
| Offset applied           | `src/clock.cpp:459` `Clock::setCurrentTime(fUTC, ...)` | `mktime(UTC fields) + fUTC*3600`                            |
| DST / tzset / TZ         | nowhere in `src/`                                      | no `configTzTime`, `setenv("TZ")`, `tzset`, DST rule        |

Consequences:

- The ESP32 default of +2 is CEST. It is wrong for five months of the year on every node that was
  never touched. The nRF52 default is 0 (UTC).
- **The clock holds local time ("node epoch" = UTC + `node_utcoff`), not UTC.** `getUnixClock()`
  subtracts the offset again. `node_utcoff` is read in about 50 places (list in section 6).

## 2. `{CET}` server frame: UTC despite the name

- Parser: `src/loop_functions.cpp:2491-2519`, format `{CET}YYYY-MM-DD hh:mm:ss`.
- It is only accepted when there is no RTC, no GPS fix and no NTP
  (`!bRTCON && !posinfo_fix && !bNTPDateTimeValid`).
- The code calls `MyClock.setCurrentTime(node_utcoff, ...)`, so it treats the payload as UTC.
- Field evidence: `{CET}2026-09-26 18:15:14` was received at 20:15:50 CEST. The payload is UTC, so
  the code is correct.
- `{CET}<...` frames are not forwarded (`src/lora_functions.cpp:1938-1941`).
- Nothing to fix. At most, add a comment that `{CET}` carries UTC.

## 3. Onboard RTC bugs (code reading, not bench-verified)

RTC driver: `src/rtc_functions.cpp` (PCF8563 or DS3231 over I2C). `ENABLE_RTC` is the default in
23 of 31 variants (`src/configuration_default.h:135`) and becomes active (`bRTCON`) only when a
chip answers.

The intended convention, judged by the manual write paths, is **RTC = UTC**:

- Phone time: `src/phone_commands.cpp:375` writes the raw value, then reads it back
  `+ node_utcoff`.
- `--settime`: `src/command_functions.cpp:764`, same pattern.
- `--setrtc`: `src/command_functions.cpp:3960` writes the raw value.
- ESP32 read: `src/esp32/esp32_main.cpp:3046-3063`, `utc + node_utcoff`, then
  `setCurrentTime(0.0, ...)`. That is correct, one offset.

### RTC-1: ESP32 writes local time into the UTC RTC

- `src/esp32/esp32_main.cpp:3040` (GPS branch) and `:3122` (NTP branch) call
  `setRTCNow(node_date_*)`.
- `node_date_*` holds **local time** (UTC + offset): see `src/time_functions.cpp:143-152` and
  `src/gps_functions.cpp:1127`.
- Failure: a node with an RTC chip and offset +2 loses GPS or NTP, or reboots without them. The
  RTC read path adds +2 again, so the node shows **UTC+4**. That wrong time goes into the node
  epoch, NBR matrix clock and timestamps.
- Fix: write UTC to the RTC (`getUnixClock()`, or `node_date_*` minus the offset).

### RTC-2: nRF52 read path adds the offset twice

- `src/nrf52/nrf52_main.cpp:1252-1272`:
  `now = utc + node_utcoff`, then `MyClock.setCurrentTime(node_utcoff, now...)`.
- So the offset is applied twice. ESP32 correctly passes `0.0` at this point.
- The nRF52 default offset is 0, which hides the bug. A RAK with an RTC module and `--utcoff 2`
  shows UTC+4.
- Fix: pass `0.0` as on ESP32, or drop the first addition.
- Side note: on nRF52, NTP (Ethernet, `:1296`) overwrites the clock after the RTC block on every
  pass, and nRF52 never writes NTP or GPS time back into the RTC. The RTC drifts unless it is set
  by hand.

### RTC-3: inverted "only every minute" timer (ESP32)

- `src/esp32/esp32_main.cpp:3037` and `:3119`:
  `if((uint32_t)(millis() - rtc_refresh_timer) < 60000)`, then `rtc_refresh_timer = millis()`.
- During the first minute after boot the condition is true. After every write it stays true.
  Result: an I2C write to the RTC on **every loop pass** while GPS or NTP is valid, not once a
  minute. On T-Beam v3 / E22-S3 each write also does `Wire.end()` / `Wire.begin()`.
- Fix: `>= 60000` (with `rtc_refresh_timer == 0` as the first-run trigger, as the NTP code does).

### Regression tests for the RTC PR

- Host test (`env:native`): put the offset arithmetic into a pure helper,
  e.g. `rtcToNodeEpoch(utc, off)` / `nodeToRtcUtc(local, off)`. Assert that a round trip
  RTC -> node -> RTC with off = 2 / -5 / 5.5 is the identity. It fails before the fix and passes
  after.
- Timer: a helper `rtcRefreshDue(now, last)` with a test that the second call within 60 s is false.
- Bench: needs a node with an RTC chip. No known bench node has one; check the T-Beam Supreme
  (PCF8563) or add a DS3231 module.

## 4. TZ string: proposed design

### Behaviour

- New setting `node_tz` (string, e.g. 40 chars), holding a POSIX TZ string such as
  `CET-1CEST,M3.5.0,M10.5.0/3`.
- **Empty = previous behaviour:** the fixed `node_utcoff` applies unchanged. Old configurations
  are not touched.
- When it is set, `node_utcoff` becomes a **derived runtime value**, computed from the TZ string
  and the current UTC time:
  - on every clock set (NTP, GPS, phone, RTC, `{CET}`, `--settime`), and
  - once a minute in the loop, which catches the switch at 02:00 / 03:00 even without a new time
    source.
- When the offset changes, re-anchor `MyClock` once: node epoch = UTC + new offset.

### Implementation choice

- **Own small parser** for `stdoffset[dst[offset][,Mm.w.d[/time],Mm.w.d[/time]]]`, plain C, about
  40-60 lines, no heap.
- Why not libc `setenv("TZ")` + `tzset()`:
  - ESP32 could do it (`configTzTime`). nRF52 newlib would pull in extra code, and the RAK flash is
    about 96 % full.
  - The behaviour would differ between platforms.
  - `env:native` can test the parser directly against host `localtime_r` with the same TZ string
    as the oracle.
- Rule format: support only `M` rules (that covers EU and US). `Jn` / `n` day rules can be
  rejected with a message.

### Touch points

| Area                                  | Change                                                                                |
| ------------------------------------- | ------------------------------------------------------------------------------------- |
| `src/meshcom_settings.h`              | new field `node_tz` (string); check flash/NVS layout and the nRF52 settings copies    |
| `src/config_json.h` / settings schema | key `node_tz`, string, validated by the parser                                        |
| `src/command_functions.cpp`           | `--settz <string>` / `--settz none`, help text, `--info` shows TZ + active offset     |
| Web GUI                               | TZ field next to the UTC offset (`src/web_functions/web_setup.cpp:165`, `:819`)       |
| BLE / app                             | `nsetdoc["UTCOF"]` (`src/command_functions.cpp:6637`) keeps sending the derived value |
| `src/loop_functions.cpp:6277`         | `getTimeZone()` returns the abbreviation (`CET` / `CEST`) when a TZ is set            |
| New `src/tz_rule.{h,cpp}`             | parser + `tzOffsetAt(utc)`; host test in `test/`                                      |
| Clock funnel                          | recompute the offset on each `setCurrentTime` / `SetClock`, plus a 60 s tick          |

### Known side effect

- A DST switch moves the node epoch by one hour. Everything stored as node epoch sees that jump:
  `/topo.dat` ages (`src/topo_ui.cpp:91`, `:209`, `:251`), the NBR matrix clock
  (`src/clock.cpp:353`), the T-Deck message store, MHeard timestamps.
- That is exactly what a manual `--utcoff` change does today, so it is not a new risk. It just
  happens automatically, at night.
- Cleaner long term: hold the clock as UTC and apply the offset only for display. That is a large
  refactor (about 50 sites) and not part of this request.

### Tests

- Host: parser cases (CET/CEST, US Eastern, no DST `UTC0`, half-hour `<+0530>-5:30`, broken
  strings); offset just before and just after both EU switch times 2026 and 2027; a round trip
  across the switch.
- Bench: set `--settz` on one ESP32 node, `--settime` shortly before a simulated switch (rule
  adjusted to "today + 2 min"), check the offset jump and `--info`.

## 5. Order of work

1. **PR A (bug fix, small):** RTC-1, RTC-2, RTC-3 plus host regression tests. Can go upstream on
   its own.
2. **PR B (feature):** `node_tz` + parser + `--settz` + web field + `getTimeZone()`. The PR text in
   German for upstream must name the settings layout change explicitly.
3. Optional: a comment at the `{CET}` parser saying the payload is UTC.

## 6. Appendix: `node_utcoff` read sites (tree `94ca1a50`)

`src/mh_phone.cpp:54,70` · `src/phone_commands.cpp:379,396` · `src/time_functions.cpp:143` ·
`src/gps_functions.cpp:1127,1277` · `src/config_json.h:272` ·
`src/command_functions.cpp:695,699,768,785,5493,6189,6637` · `src/topo_ui.cpp:91,209,251` ·
`src/tinyxml_functions.cpp:276,279,309,311` · `src/loop_functions.cpp:683,685,2515,6279` ·
`src/softser_functions.cpp:98` · `src/t-deck-pro/tdeck_pro.cpp:635` ·
`src/t-deck-pro/ui_deckpro.cpp:1279` · `src/t-deck/lv_obj_functions.cpp:4223,4752` ·
`src/t-deck/event_functions.cpp:564` · `src/esp32/esp32_main.cpp:3012,3048` ·
`src/esp32/esp32_flash.cpp:360` · `src/nrf52/ble_settings_v1.cpp:91,296` ·
`src/nrf52/ble_settings_v1.h:135,351` · `src/nrf52/nrf52_main.cpp:1257,1271,1311,2878` ·
`src/web_functions/web_setup.cpp:165,166,819` ·
`src/web_functions/web_functions.cpp:1610,2840,3145,3388`
