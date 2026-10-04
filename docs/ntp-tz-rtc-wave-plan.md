# NTP / TZ string / RTC: wave plan

Date: 2026-10-04. Base: `fork-dev` at `26324810`. Findings: `docs/ntp-tz-rtc-findings.md`.
Status: **W1-W3 committed, W4 docs committed, W4 bench OPEN** (flashing a bench node was blocked by the session's auto-mode permission; needs the operator).

## BLUF

- Four waves, run with `/orchestrate-waves`. Each wave ends in one commit on `fork-dev`.
- W1 = PR A (RTC bug fixes). W2 + W3 = PR B (TZ string). W4 = full gate, bench, docs.
- **No `FLASH_STRUCT_VERSION` bump.** `node_tz` follows the `max_hop_text` precedent: a struct
  field plus a keyed schema row. No node loses its settings on update.
- **No libc `setenv("TZ")`.** `Clock::setCurrentTime()` ends in `localtime_r()`
  (`src/clock.cpp:480`), which today only works because TZ is unset. Setting TZ would apply the
  offset twice.
- The campaign ends with commits on `fork-dev`. **No upstream PR** for now (operator, 2026-10-04).

## Corrections to the findings paper (re-checked at `26324810`)

| Item  | Paper says                         | Code says                                                                                                                                                                                                                                                                                                                                          |
| ----- | ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| RTC-3 | NTP branch writes every loop pass  | The NTP write (`esp32_main.cpp:3127`) sits inside the once-per-boot "Starttime setzen" block (`node_update[0] == 0x00`). Only the GPS branch (`:3045`) writes every pass. A bare `>= 60000` would make the NTP write **never** happen: it runs once, inside the first minute. The fix must be `rtc_refresh_timer == 0 \|\| millis() - t >= 60000`. |
| RTC-1 | `node_date_*` local, RTC gets that | Confirmed (`gps_functions.cpp:1127`, `time_functions.cpp:143`).                                                                                                                                                                                                                                                                                    |
| RTC-2 | nRF52 adds the offset twice        | Confirmed (`nrf52_main.cpp:1257` + `:1271`).                                                                                                                                                                                                                                                                                                       |
| §4    | settings layout change             | No layout bump needed: both stores are keyed (`settings_store_nrf52.cpp:351`, ESP32 schema walk `esp32_flash.cpp:388`). A missing key leaves the default `{0}` = empty TZ = previous behaviour. BLE v1 (`ble_settings_v1.h`) is a frozen, independent snapshot and is not touched.                                                                 |
| §4    | 60 s tick in the loop              | `Clock::CheckEvent()` already returns `eEventMinute`, and both platform loops call it. The tick lives in `clock.cpp`, so neither platform main changes for PR B.                                                                                                                                                                                   |
| new   | one more `node_utcoff` writer      | `tinyxml_functions.cpp:311` overwrites the offset from an XML station attribute and saves it. With a TZ set, that write must be skipped.                                                                                                                                                                                                           |
| new   | T-Deck UI writer                   | `t-deck/event_functions.cpp:564` sets `node_utcoff` directly. Same rule as `--utcoff` (decision D1).                                                                                                                                                                                                                                               |

## Decisions (defaults; confirm or change)

| ID  | Question                        | Default                                                                                                                                                                                                                                   |
| --- | ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | `--utcoff` while a TZ is set    | **Decided:** clears `node_tz` and prints a note. A manual offset wins. Same for the T-Deck UI path.                                                                                                                                       |
| D2  | `node_tz` default on first boot | **Decided:** `CET-1CEST,M3.5.0,M10.5.0/3` on ESP32 in `init_flash()` (fresh node and `--clean`); empty on nRF52. Updated nodes keep an empty TZ: the NVS key is missing, so the `{0}` initializer applies. The commit message names this. |
| D3  | PR A scope                      | **Decided:** the three bugs plus GPS/NTP -> RTC write-back once a minute on **both** platforms. nRF52 gains the write-back; on ESP32 the NTP write moves out of the once-per-boot block.                                                  |
| D4  | BLE API                         | **Decided:** extend the vocabulary. Push: the node settings JSON (`sendNodeSetting()`, `nsetdoc`) carries a new `TZ` key next to `UTCOF`. Put: the app can set it through the BLE settings/command path (exact form from W3 recon).       |
| D5  | Rule syntax                     | POSIX `std offset [dst [offset] [,Mm.w.d[/time],Mm.w.d[/time]]]`, quoted `<+0530>` names, `M` rules only. `Jn` / `n` rejected with a message.                                                                                             |
| D6  | Persisting the derived offset   | Not written to flash on a DST switch. The stored `node_utcoff` gets corrected at boot by the first clock set, so it is never more than one boot stale.                                                                                    |

## Waves

Model tiers: writers are `implementer` (Sonnet, high). The orchestrator gates, and `/fable-review`
runs the advisor pass on W1 and W3. Orchestrator-owned hotspot in every wave: `platformio.ini`
(native env / `test_filter` / `build_src_filter` edits).

### W1: PR A, RTC fixes (1 writer)

| Owner | Files                                                                                                                                                                            |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1    | new `src/rtc_offset.h` (header-only, pure), new `test/test_rtc_offset/`, `src/esp32/esp32_main.cpp`, `src/nrf52/nrf52_main.cpp`, `src/loop_functions.cpp` (`{CET}` comment only) |
| orch  | `platformio.ini`: `test_rtc_offset` into `[env:native]` `test_filter`                                                                                                            |

- Helpers: `rtcRefreshDue(now, last)`, `rtcUtcFromNodeEpoch(local, off)`, and the read-path
  composition that the call sites use (RTC UTC -> `setCurrentTime` args).
- RTC-1: both ESP32 `setRTCNow()` calls write UTC (`getUnixClock()` -> RTClib `DateTime`).
- RTC-2: nRF52 passes `0.0` to `setCurrentTime`, as ESP32 does.
- RTC-3: `rtc_refresh_timer == 0 || millis() - t >= 60000` at both ESP32 sites.
- ESP32 NTP write (D3): move it out of the once-per-boot "Starttime setzen" block, so a valid NTP
  time refreshes the RTC once a minute, gated by `rtcRefreshDue()`.
- nRF52 write-back (D3): mirror the ESP32 shape in `nrf52loop()`: with a GPS fix or valid NTP,
  write UTC to the RTC through `rtcRefreshDue()`; read the RTC only without them. Same helpers.
- **Regression proof, before/after:** the agent first extracts the helpers with today's arithmetic
  and inline timer verbatim and runs the suite (must be RED: round trip off by 2x the offset; second
  refresh within 60 s returns true). Then it applies the fix and runs again (GREEN). Both outputs go
  into the report.
- Round-trip cases: offset 2, -5, 5.5, 0; timer: first call due, second call at +59 s not due, at
  +60 s due, millis wrap.
- Agent verification: `pio test -e native -f test_rtc_offset`, `pio run -e heltec_wifi_lora_32_V3`,
  `pio run -e wiscore_rak4631` (in that order, never in parallel). Artifact check: string-scan for
  a code path is not possible (no strings), so the orchestrator diffs `objdump` of `esp32loop` or
  accepts the native test plus a code read.
- Gate: native stage 1, RTC-enabled build of `ttgo_tbeam` (GPS + RTC branch), advisor pass, commit
  `rtc(RTC-1..3): ...`.

### W2: PR B foundations (2 writers, parallel, disjoint files, separate build trees)

| Owner | Files                                                                                                                                                                                                                         | Verification                                                          |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| B1    | new `src/tz_rule.{h,cpp}`, new `test/test_tz_rule/`                                                                                                                                                                           | new env `[env:native_tz_rule]` (orchestrator adds it before dispatch) |
| B2    | `src/meshcom_settings.h` (`A(char, node_tz, [40], {0})` after `max_hop_text`), `src/config_json.h` (`X("node_tz", CFG_STR, ...)`), `src/esp32/esp32_flash.cpp` (`init_flash()` default, D2), `test/test_config_json/` fixture | `native_config`, `native_settings_members_esp32`, `..._nrf52`         |

- B1 API: `bool tzParse(const char*, TzRule*)` (no heap, fixed struct), `int32_t tzOffsetSec(const
TzRule*, uint32_t utc)`, `const char* tzAbbrev(const TzRule*, uint32_t utc)`.
- B1 tests: host `localtime_r` under `setenv("TZ")` is the oracle (test-only; firmware never calls
  it). Cases: CET/CEST, US Eastern, `UTC0`, `<+0530>-5:30`, `NZST-12NZDT,M9.5.0,M4.1.0/3` (southern
  hemisphere), the last second before and the first second after both EU switches in 2026 and 2027,
  and broken strings (empty, overlong, `J60`, missing comma, bad month).
- B2: the `test_settings_members` gate must stay green (the field has a schema row). No
  `FLASH_STRUCT_VERSION` edit; the brief bans it explicitly.
- Gate: all three native envs, advisor pass skipped (pure module plus data plumbing; W3 covers the
  behaviour), commit.

### W3: PR B integration (2 writers, parallel)

| Owner | Files                                                                                                                                                                                                                                                                                                                   |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C1    | `src/clock.cpp` (re-anchor in `SetClock(time_t, bool)` + minute tick in `CheckEvent`), `src/command_functions.cpp` (`--settz`, `--settz none`, help, `--info`, D1), `src/loop_functions.cpp` (`getTimeZone()` abbreviation), `src/tinyxml_functions.cpp` (skip when a TZ is set), `src/t-deck/event_functions.cpp` (D1) |
| C2    | `src/web_functions/web_setup.cpp`, `src/web_functions/web_functions.cpp` (field next to UTC offset, settings table row; sets via `--settz` through `commandAction`)                                                                                                                                                     |

- The funnel (one place for every clock source): `utc = tsNow - node_utcoff*3600`; if a TZ is set,
  `off = tzOffsetSec(utc)`; if it differs, update `node_utcoff` and `tsNow = utc + off`. The minute
  tick calls the same re-anchor.
- Build split: C1 builds `heltec_wifi_lora_32_V3`, C2 builds `ttgo_tbeam`. RAK and T-Deck run at
  the gate.
- Artifact check: `strings firmware.elf | grep -c settz` >= 1 on Heltec and RAK.
- Gate: stage 1+2, builds `heltec_wifi_lora_32_V3`, `wiscore_rak4631` (flash % vs. the current
  96.1 %), `t_deck_plus`, `t_deck_pro`, web GUI jsdom harness if the field touches its JS, advisor
  pass, commit.

### W4: full gate, bench, docs (orchestrator, serialized)

- `tools/regression.sh --stage 1,2`, then stage 3 on the bench fleet.
- Bench, `identity_guard.py` first, no RF traffic needed:
  - DK5EN-1 (Heltec V3): `--settz CET-1CEST,M3.5.0,M10.5.0/3`, `--info` shows CEST/+2; a rule whose
    switch falls 2 min after `--settime` shows the offset jump within one minute; `--settz none`
    restores fixed-offset behaviour; reboot keeps `node_tz`.
  - DK5EN-90 (RAK): same smoke test (nRF52 keyed store round-trips `node_tz`).
  - RTC fixes: **no bench node has an RTC chip**; host tests and code review only (decided). The
    commit message says it was not tested on hardware.
  - D2 default: string scan of the ESP32 image for the CET literal; no bench node gets wiped to
    prove it.
- Docs: `docs/BACKLOG.md` (RTC-1..3, TZ-01, deferred D3 items), `docs/test-suite-map.md` (two new
  suites), this plan's status column, `prettier`. Commit.

## Status

Deviation: W1 and W2 have disjoint file sets, so they run as one parallel wave (3 writers). Every
`pio` call goes through one `lockf` lock (one pio process machine-wide). They still get separate
commits. Orchestrator added `[env:native_rtc_offset]` and `[env:native_tz_rule]` to
`platformio.ini` before dispatch.

W1+W2 gate (combined tree): Unity 50 envs / 1718 cases green; golden selftest red only on
`variant-ini-effective` (the two new native envs), baseline regenerated after reading the diff;
`heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `wiscore_rak4631`, `t_deck_plus` build. W3 interface
(`tzApplyNow()`, `tzActiveAbbrev()`) declared in `src/clock.h` by the orchestrator.

W3 BLE vocabulary (D4, from recon): `SN` is at 228 of 244 bytes, so `TZ` goes into `SN1` (65 B
today, about 99 B with the CET rule, at most 110 B). Put = `--settz` over the existing 0xA0 text
command path. Spec: `docs/architecture/11-wire-format.md` section 4.2/4.4 (W4).

| Wave | State                                                                                                                                                                                                 | Commit      |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| W1   | done: advisor REWORK (nRF52 GPS write used frozen node_date_*), fixed and re-built                                                                                                                    | `9242f693`  |
| W2   | done: B2 stopped on D2 (init_flash seeds every boot); default moved to the flash-clear branch                                                                                                         | `614f1b36`  |
| W3   | done: advisor REWORK (web echo of node_tz could inject markup -- tzParse accepts `<plaintext>`-style names; now HTML-escaped; --utcoff clears only on a parsed value; `none` case-insensitive on web) | `bcf8f083`  |
| W4   | docs done (BACKLOG RTC-1..3/TZ-01 + open RTC-04..06/TZ-02, test-suite-map, wire format SN1/--settz, settings-registers, d1-04 row, schema lint); bench NOT run                                        | see git log |

## W4 bench: open, for the operator

Flashing was refused by the session's permission layer, so nothing ran on hardware. DK5EN-1 was
not reachable (not on USB here, no web server at `.76`/`.62`); DK5EN-92 (T-Beam, `.75`, guard ok,
before: `UTC-OFF 2.0 [NTP]`, Flash-Version 20260724) is the ready substitute.

1. `pio run -e ttgo_tbeam` and `python3 tools/webflash.py --env ttgo_tbeam 192.168.68.75`
   (`pio run -e wiscore_rak4631 --target upload --upload-port <port of 230D6EBB3266D20E>` for RAK).
2. `--info`: expect `...TZ none` (updated node keeps an empty rule, D2).
3. `--settz CET-1CEST,M3.5.0,M10.5.0/3` -> `TZ ..., now CEST UTC+2.0`.
4. DST jump without a time source: `--settz XST-1XDT,M1.1.0,M10.1.0/HH:MM` with HH:MM = local
   time + 2 min on a first Sunday of October (or adjust the rule to today); within ~3 min
   `--info` shows `TZOFF +1.0 [XST]` and the clock 1 h back.
5. `--settz J60` -> rejected, nothing changed. Reboot -> rule persists. `--utcoff 2` -> rule
   cleared. Restore: `--settz none`, `--utcoff 2`.
