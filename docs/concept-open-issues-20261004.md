# Open upstream issues: firmware concept and wave plans

Date: 2026-10-04. Base: `fork-dev` at `8fe58f67` (recon line references were taken at `a9e64cf6`;
`command_functions.cpp` moved by a few lines since, re-verify before editing). Recon reports:
[`concept-open-issues-20261004-recon/`](concept-open-issues-20261004-recon/).

Status: CONCEPT, ready for an `/orchestrate-waves` implementation campaign. Nothing coded.
Backlog rows: AU-01..06, NMTU-01..04, RM-01..07, SNF-GW-01..06, BLC-01..05.

## 1. Bottom line

Of the 14 open issues in icssw-org/MeshCom-Firmware, five are firmware work for this fork
(#1187, #1188, #1189, #1190, #1191). Four are closed in substance and need a reply, five are
hardware or app topics outside this campaign (section 2).

| Issue | Feature                             | Verdict in one line                                                                                                                                                                     | Platforms            | Size    |
| ----- | ----------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ------- |
| #1187 | Firmware auto update                | The app downloads over TLS and stages the image in the unused tail of its own slot; Safeboot applies it offline. No partition change. Safeboot cannot hold TLS (5 KB headroom).         | ESP32 only           | 5 waves |
| #1190 | MTU for firmware and Safeboot       | RAK already has `node_ethmtu` + W5100S MSS cap (#1183). ESP32 and Safeboot get the same setting applied to the lwIP `netif->mtu`. UDP paths are already below 760 B and never fragment. | ESP32, RAK, Safeboot | 1 wave  |
| #1189 | Secure remote management via LoRa   | Plaintext command DM with a 64-bit HMAC-SHA256 tag, keyed by `node_passwd`, monotonic counter, allowlist, executed from the loop task. New portable HMAC also serves nRF52.             | all boards           | 3 waves |
| #1188 | S&F for PMs from the central server | Stage 1 of the existing concept: store hook in both `GATE` handlers, `:sto` upload, server `:ack` purge. The base-call match already exists. Stage 3 waits for a server measurement.    | S3 + RAK4631         | 2 waves |
| #1191 | BLE AutoReconnect                   | Both stacks already re-advertise instantly; the gap is the app, whose reconnect loop is commented out. Firmware: one ESP32 session-reset race, nRF52 reason logging, counters, bench.   | all boards + app     | 1 wave  |

Recommended campaign order (section 9): NMTU, BLC, SNF-GW, RM, AU. Small and self-contained first,
the two that touch `command_functions.cpp`, `config_json.h` and `esp32_main.cpp` the most last.

## 2. Classification of all 14 open issues

| Issue | Title (short)                             | Class               | Disposition                                                                                                                                                   |
| ----- | ----------------------------------------- | ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #1191 | AutoReconnect                             | firmware + app      | Section 8. Firmware hardening; app must re-enable its reconnect loop (`Connect.tsx:908-931`, "RECONNECT DISABLED").                                           |
| #1190 | Change MTU, FW and Safeboot               | firmware            | Section 5.                                                                                                                                                    |
| #1189 | Secure remote management via LoRa         | firmware + McApp    | Section 6. McApp side: `~/Desktop/mcapp-hmac-remote-admin.md`.                                                                                                |
| #1188 | S&F for PMs via the central server        | firmware + server   | Section 7. Firmware stage 1 has no server dependency; routing by the server is unknown (bench M1-M4).                                                         |
| #1187 | Firmware auto update                      | firmware            | Section 4. nRF52 out of scope: no autonomous flash path (BLE DFU and `--dfu` need a host).                                                                    |
| #1184 | CAJOE radiation sensor                    | hardware feature    | Not in this campaign. Needs a GPIO pulse counter (ISR, debounce) plus a telemetry field; same mechanism as the #999 anemometer. Candidate for one later wave. |
| #1182 | Wireless Tracker backlight on Display OFF | verify-only         | DK5EN replied 2026-10-02 "fixed in the new FW". Ask the reporter to confirm on 4.40a, then close.                                                             |
| #1181 | CLI to delete the whole config            | closed in substance | `--cleanflash` exists (`command_functions.cpp:990`). Reply with the command and the RAK note (DFU does not wipe settings); close.                             |
| #1134 | Heltec HT-CT62 board                      | hardware            | Not in this campaign. ESP32-C3, no C3 env exists, no bench hardware.                                                                                          |
| #1076 | APRS telemetry status bits (MCP23017)     | diagnosed           | Feature shipped in 4.35t; the reporter's missing `/D=` is boot-time detection. Fix = detection retry in the 5 s poll. One small wave, outside this campaign.  |
| #1073 | Charge/discharge current                  | hardware feature    | PMU boards (AXP192/2101) can report it; others need an INA2xx. Not in this campaign.                                                                          |
| #999  | UV and wind telemetry                     | hardware feature    | Reporter offered split PRs; maintainers said "we will check". Not in this campaign.                                                                           |
| #224  | Store messages in the mesh                | closed in substance | S&F shipped in 4.40a for directly heard nodes (DK5EN reply 2026-10-02). Residual (multi-hop retrieval) is a MeshCom 5 topic. Close.                           |
| #106  | ALARM code word                           | app                 | Trigger word and distinct sound belong in the app. Firmware option (buzzer on GPIO) is a separate feature request. Close or move to the app repo.             |

## 3. Cross-cutting rules for the implementation campaign

### 3.1 Platform scope

| Family                       | WiFi | Eth | BLE | AU                    | NMTU         | RM  | SNF-GW                    | BLC | Bench node                              |
| ---------------------------- | ---- | --- | --- | --------------------- | ------------ | --- | ------------------------- | --- | --------------------------------------- |
| ESP32 classic (T-Beam, E22)  | yes  | no  | yes | yes (heap to measure) | yes          | yes | no (no `ENABLE_MSGSTORE`) | yes | DK5EN-92 T-Beam v1.2                    |
| ESP32-S3 (Heltec V3, T-Deck) | yes  | no  | yes | yes                   | yes          | yes | yes                       | yes | DK5EN-1 Heltec V3, DK5EN-14 T-Deck Plus |
| ESP32-S3 T-ETH-ELITE         | yes  | yes | yes | yes                   | yes          | yes | yes                       | yes | none                                    |
| nRF52840 RAK4631             | no   | yes | yes | no                    | yes (exists) | yes | yes                       | yes | DK5EN-90                                |
| nRF52840 T-Echo, T114        | no   | no  | yes | no                    | no           | yes | no                        | yes | none                                    |
| Safeboot (classic, S3)       | yes  | no  | no  | apply step            | yes          | no  | no                        | no  | DK5EN-1, DK5EN-92                       |

### 3.2 Decisions taken with the operator on 2026-10-04

| Id  | Question             | Decided                                                                                         |
| --- | -------------------- | ----------------------------------------------------------------------------------------------- |
| D1  | Scope of this paper  | The five DK5EN/oldnat issues #1187-#1191. Others classified only.                               |
| D2  | AU update source     | GitHub Releases. Repository is a setting, default `icssw-org/MeshCom-Firmware`.                 |
| D3  | AU architecture      | App downloads and stages in the `ota_0` tail; Safeboot applies offline. No partition re-layout. |
| D4  | AU on nRF52          | Out of scope (no autonomous OTA path).                                                          |
| D5  | RM authentication    | HMAC-SHA256 tag, key derived from `node_passwd`. TOTP rejected (ADR-TOTP superseded).           |
| D6  | RM replay protection | Persisted monotonic counter per node. No clock dependency.                                      |
| D7  | RM v1 command set    | Operational allowlist including `reboot` (section 6.4). Hard block list fixed.                  |
| D8  | RM McApp side        | Separate paper on the Desktop; the tag is computed on the McApp backend, never in the browser.  |
| D9  | Docs delivery        | This paper, the McApp paper and BACKLOG rows committed on `fork-dev`.                           |

### 3.2a Decisions of the approval round (2026-10-04 evening)

These supersede the rows they name.

| Id      | Decision                                                                                                                                                                                                                                                                                                                                                                                                        |
| ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NMTU-D2 | Default MTU is 1280 in the app (all boards) and in Safeboot; `--mtu 1500` raises it. No migration: a stored value (4.40a nodes hold 1500) is kept; the release note tells users to run `--mtu 1280`.                                                                                                                                                                                                            |
| AU-D2   | Replaces D2/AU-D7. `node_updchan` 0 = prod (`icssw-org/MeshCom-Firmware`, default), 1 = dev (`DK5EN/MeshCom-Firmware`), both compiled in. No free repo string, no `node_updrepo`. `--updchan prod\|dev`. Not in the RM allowlist.                                                                                                                                                                               |
| AU-D9   | Both channels read `releases/latest` only; prereleases and drafts are skipped. Every dev test release becomes "Latest" on the fork and is what the web flasher serves; release notes must say so.                                                                                                                                                                                                               |
| AU-D10  | The release build sets `-D MC_BUILD_TAG="vX.YYz[.MM.DD]"`; a local build has an empty tag. One comparator for both channels: `(major, minor, letter)` first; on a tie the `MM.DD` date decides, a candidate is newer if its date lies 1..182 days after the running date modulo the year (New Year safe); a tag without date is the oldest of its version. Empty running tag = `SOURCE_VERSION`+`SUB`, no date. |
| SNF-D6  | STOR announces the store set intersected with calls heard directly on LoRa within 12 h. The exact own call (call+SSID) is never announced (delivered directly); own base call with another SSID is announced when heard. No echo-HEY (option 3a dropped).                                                                                                                                                       |
| SNF-D7  | `node_stor` int row 0/1, default 0, `--stor on\|off`, `--info` line. Off = no STOR datagram. Not in the RM allowlist. The real server is used only after the operator's approval; bench uses the mock server, extended for STOR in SNF-GW W3.                                                                                                                                                                   |

### 3.3 Hotspot files shared by several features

The orchestrator owns these at every gate; writer briefs never list them unless the row says so.

| File                                                          | Touched by                  | Rule                                                                                          |
| ------------------------------------------------------------- | --------------------------- | --------------------------------------------------------------------------------------------- |
| `platformio.ini`                                              | every feature (native envs) | orchestrator only; regenerate `test/golden/native/variant-ini-effective.json` after each edit |
| `src/command_functions.cpp`                                   | AU, NMTU, RM, BLC           | one writer per wave at most; features run sequentially (section 9)                            |
| `src/config_json.h`, `src/meshcom_settings.h`                 | AU, RM                      | schema rows appended at the end, one writer per wave                                          |
| `src/esp32/esp32_main.cpp`                                    | AU, NMTU, BLC               | one writer per wave; hot file (TZ/RTC commits)                                                |
| `src/web_functions/web_functions.cpp`, `web_setup.cpp`        | AU, NMTU, RM                | one writer per wave                                                                           |
| `src/lora_functions.cpp`                                      | RM, SNF-GW                  | one writer per wave                                                                           |
| `src/loop_functions.cpp`                                      | RM, SNF-GW                  | one writer per wave                                                                           |
| `variants/*/platformio.ini` (31 files)                        | AU (`MC_ENV_NAME`)          | scripted edit by the orchestrator, one commit                                                 |
| `docs/BACKLOG.md`, `docs/test-suite-map.md`, `docs/RESUME.md` | all                         | orchestrator, docs commit per wave                                                            |

### 3.4 Gates and conventions (from `docs/ntp-tz-rtc-wave-plan.md` and `tools/regression.sh`)

- Writers: `implementer` (Sonnet, high), exclusive file sets, no git, no `pio` unless the brief
  assigns the build slot, never bare `pio test`, one `pio` process at a time.
- Every pure-logic piece is a header-only or small `.cpp` carve-out with its own `[env:native_*]`
  Unity env (pattern `platformio.ini:692` and `:709`), a row in `docs/test-suite-map.md` section 5,
  and the golden json regenerated by the orchestrator.
- Settings: one `X(...)` row in `config_json.h` gives JSON export, NVS and the nRF52 keyed store. No
  `FLASH_STRUCT_VERSION` bump. Golden lints that must stay green: `settings_schema_lint`,
  `help_parity_lint`, `info_switch_lint`, `producer_match_lint`, `nano_printf_lint`.
- Gate per wave: `tools/regression.sh --stage 1,2`, named board builds with flash percentage versus
  baseline (RAK is at 96.1 %), jsdom suites if web JS changed, advisor pass for behaviour changes,
  commit `<area>(<ID>): ...`.
- Artifact proof: `strings .pio/build/<env>/firmware.elf | grep -c <token>` on every env that must
  carry the feature, and 0 on every env that must not.
- Bench: `tools/bench/identity_guard.py` first, DM or group `TEST` only, 2 dBm, own callsigns only,
  ports by USB serial, `lsof` before opening a port, DTR for RAK and S3.
- Upstream PR per feature: `pr/<topic>` from `upstream/dev`, `src/` and `variants/` only, one
  squashed commit, German description (what, why, tested on, reviewer notes). Fork-only: `test/`,
  `tools/`, `docs/`, native envs, `pre:` scripts, `-Werror`, `--port "$UPLOAD_PORT"`.

## 4. AU: Firmware auto update (#1187)

### 4.1 Facts the design rests on (recon `scout-1187.md`)

- `ota_0` is the only application slot on every board; the app cannot rewrite the partition it
  runs from. Only Safeboot (factory slot, 704 KB) writes `ota_0` today
  (`src/safeboot/ElegantOTA.cpp:121-396`).
- Safeboot image headroom: classic 5184 B (715712 of 720896 B), S3 44 KB. A TLS client needs well
  over 100 KB. TLS in Safeboot means a partition re-layout and a USB or web-flasher reflash of every
  node. Rejected for v1 (D3).
- The app framework (Arduino 2.0.17, IDF 4.4) ships `HTTPClient`, `WiFiClientSecure` and the
  certificate bundle (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, 200 roots). The app has at least
  1.6 MB of free flash on every ESP32 env. Heap: about 125 KB free on a deployed Heltec V3; classic
  ESP32 with NimBLE is unmeasured (W0 item).
- Handover to Safeboot is `--ota-update` (`command_functions.cpp:1091` at HEAD, `:1109-1134` in the recon): set boot partition,
  `loopCrumbClear()`, restart. No data crosses the reboot today. Safeboot reads the normal NVS
  settings through `init_flash()` (`src/safeboot/main.cpp:217`).
- The 4 MB table gives `ota_0` 3324 KB at 768 KB, the 16 MB table 12288 KB. App images are
  1.43-1.79 MB (classic 4 MB boards), 1.55 MB (Heltec V3).
- GitHub: `releases/latest` JSON is 65 KB (stream-scan, never buffer); unauthenticated limit 60
  requests per hour per IP; asset objects carry `digest: "sha256:<hex>"`, `size` and
  `browser_download_url`; downloads redirect (302) to an object host. Both the icssw-org release
  (38 assets) and the fork release (39) use `<env>.bin` with three renames (`T-Beam-1W.bin`,
  `T3_S3_V13.bin`, `t_connect_pro.bin`).
- No env name is compiled into the app; `BOARD_HARDWARE` is not unique per env (three E22 envs share
  one). Asset mapping needs a new per-env macro.
- Version: `SOURCE_VERSION "4.40"` + `SOURCE_VERSION_SUB "a"` (`configuration_global.h:7-9`).
  Upstream tags are `v4.40a`, fork tags `v4.40a.10.02`. Compare `(major, minor, letter)`; the fork
  date suffix orders only fork builds of one version.

### 4.2 Design: stage in the slot tail, apply offline

```
  app (loop task)                                  Safeboot (boot)
  ---------------                                  ---------------
  timer 24 h +-2 h jitter, WiFi up, NTP valid,
  no phone connected, battery >= floor
     |
  CHECK   GET https://github.com/<repo>/releases/latest   (no redirect follow)
          Location: .../releases/tag/<tag>  -> version compare
     |  newer
  META    GET https://api.github.com/repos/<repo>/releases/tags/<tag>
          stream-scan for "name":"<MC_ENV_NAME>.bin" -> size, digest, url
     |  size <= stage_limit, digest present, not draft/prerelease
  DOWNLOAD GET url (follow redirects), erase stage range, write 4 KB blocks,
          SHA-256 running; digest mismatch -> erase stage, back off
     |
  READBACK crc32 over the staged bytes (esp_rom_crc32_le)
  RECORD   NVS "fwstage": magic, tag, env, off, len, crc32, sha256, ts
     |  STAGED: wait for the update window (TZ-01 local time) and idle
  --ota-update ------------------------------------>  read record
                                                       crc32(staged) == record
                                                       esp_image_verify(staged pos)
                                                       erase head [0,len), copy tail->head
                                                       esp_image_verify(ota_0)
                                                       clear record, boot app, restart
```

- **Stage region:** `ota_0` offset `stage_off = slot_size / 2` rounded down to 64 KB, length
  `slot_size - stage_off`. A download is accepted only if `asset.size <= slot_size - stage_off` and
  the running image ends below `stage_off` (`esp_ota_get_running_partition()` plus
  `esp_image_get_metadata()` for the image length). On 4 MB boards that is 1662 KB; E22_XML
  (1.79 MB) and any future image above the line is refused with a logged reason and a web notice,
  not a silent skip. 16 MB boards have no practical limit.
- **Why offline apply:** Safeboot needs no network, no TLS and no time for the auto path; the
  copy is idempotent, so a power loss during the copy leaves the record in place and the next boot
  retries; a corrupted head keeps the node in Safeboot through the existing `app_valid` gate
  (`ota_state.h:86-90`) instead of boot-looping. Safeboot's code growth is a copy loop, a CRC
  (ROM function) and a Preferences read: a few KB. The classic image has 5184 B. W1 measures it;
  if it does not fit, the fallback is dropping ESPmDNS from Safeboot (it is reached by IP from the
  app's web GUI anyway) which frees far more than needed.
- **Writing into the running partition:** `esp_partition_write()` on the `ota_0` partition is
  allowed at any offset; the flash driver disables the cache for the duration of each write on
  both cores. The constraint is only that the range must not overlap the mapped image, which the
  `stage_off` rule guarantees. Verified on the bench in W2 before any fleet use (AU bench case B1).
- **Integrity chain:** TLS to github.com with the IDF bundle (`setCACertBundle`), the API `digest`
  compared against the streamed SHA-256, a read-back CRC, `esp_image_verify` on the staged position
  (chip id, segments, checksum) and again on `ota_0` after the copy. No downgrade, no sidegrade:
  the staged tag must compare greater than the running version. Draft and prerelease releases are
  skipped (`releases/latest` already excludes them; the explicit tag fetch re-checks the flags).
- **Time:** certificate validation needs a valid clock; the check runs only with
  `bNTPDateTimeValid` (or GPS/RTC-valid per `Clock::SetClock` sources). Safeboot needs no time.
- **WiFi gating:** today STA starts only for gateway, extern UDP, web server or net console
  (`esp32_main.cpp:2019`). `--autoupdate on` joins that list, which is exactly "independent of the
  gateway flag".
- **Downtime:** the node is off the mesh for the Safeboot copy (seconds) plus two reboots, not for
  the download. The update window defaults to 03:00-05:00 local (TZ-01 offset); without a valid
  TZ rule the window is UTC.
- **Phone connected:** no check and no handover while `isPhoneReady`; the staged image waits.

### 4.3 Settings and surfaces

| Item           | Value                                                                                                                                                      |
| -------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `node_autoupd` | `X(..., CFG_INT, 0..2)` 0 off (default), 1 check and notify, 2 check, download and install                                                                 |
| `node_updchan` | `X(..., CFG_INT, 0..1)` 0 prod (icssw-org, default), 1 dev (DK5EN) -- AU-D2 replaced the free `node_updrepo` string                                        |
| CLI            | `--autoupdate off                                                                                                                                          | notify | auto`, `--updchan prod | dev`, `--update check`(run the check now),`--update install`(stage now, handover in the window),`--update status` |
| `--info`       | `AUTOUPD: <mode> repo=<repo> last=<ts> avail=<tag or none> staged=<tag or none> next=<window>`                                                             |
| Web            | setup field (mode select, repo text), info row, banner "Update <tag> available" with "Install" button that runs `--update install`; info row `MC_ENV_NAME` |
| BLE SN1        | key `AU` = mode, `AUV` = available tag (SN is full at 228/244 B; SN1 budget per `command_functions.cpp:6742`)                                              |
| Serial markers | `[AU];check;...`, `[AU];stage;...`, `[AU];refuse;reason;...`, `[AU];handover;...` (Safeboot: `[SAFEBOOT];apply;...`)                                       |
| `MC_ENV_NAME`  | `-D MC_ENV_NAME=\"<env>\"` in every `variants/*/platformio.ini`; the three renames live in a table in `fw_update_check.cpp`                                |

Record (NVS namespace `fwstage`, written by the app, cleared by Safeboot):

```
magic u32 'FWS1' | tag char[24] | env char[32] | off u32 | len u32 | crc32 u32 | sha256 u8[32] | ts u32
```

Safeboot treats a wrong magic, an `env` different from its own `MC_ENV_NAME` (Safeboot is built
per chip family, so it compares the chip id instead), or a CRC mismatch as "no staged image" and
clears the record.

### 4.4 Decisions and defaults (AU)

| Id    | Question                                          | Default                                                                                                                                                                         |
| ----- | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| AU-D1 | Default mode                                      | off. `notify` is the recommended setting in release notes; `auto` is opt-in.                                                                                                    |
| AU-D2 | Check interval                                    | 24 h with +-2 h jitter, first check 10 min after boot with WiFi. No setting.                                                                                                    |
| AU-D3 | Update window                                     | 03:00-05:00 local, fixed in v1.                                                                                                                                                 |
| AU-D4 | Battery floor                                     | no handover below 3.6 V on battery (`node_maxv` scale), mains-powered nodes always.                                                                                             |
| AU-D5 | Retry after failure                               | download: 3 attempts per release, 6 h apart, then wait for the next release. Safeboot apply failure: record cleared, node stays in Safeboot AP mode as today (manual recovery). |
| AU-D6 | Classic ESP32 TLS                                 | W0 measures the handshake on DK5EN-92. If it fails, v1 compiles AU for S3 only (`CONFIG_IDF_TARGET_ESP32S3`).                                                                   |
| AU-D7 | Repository switching                              | the setting accepts any `owner/repo`; the fork release feed is a valid target.                                                                                                  |
| AU-D8 | Layout changes (bootloader, partitions, Safeboot) | never updated by this path. A release whose layout changes must say so in its notes; AU cannot detect it in v1.                                                                 |

### 4.5 Waves (AU)

Preamble: writers are `implementer` (Sonnet, high). Build slot: at most one `pio run` per wave,
assigned in the table. `platformio.ini`, the golden json and the 31 variant inis are orchestrator
edits.

#### W0 recon and feasibility (orchestrator + one scout, read-only + bench)

- Measure heap and TLS on DK5EN-92 (classic) and DK5EN-1 (S3) with a throwaway sketch-level test:
  `WiFiClientSecure` + bundle to `https://api.github.com/` HEAD. Record free heap before, during,
  after; record flash delta. Decide AU-D6.
- Measure Safeboot classic image after adding a Preferences read, `esp_rom_crc32_le` loop and
  `esp_image_verify` on a position (the call already links). Decide the ESPmDNS fallback.
- Confirm `esp_partition_write` into `ota_0` beyond the running image on DK5EN-1 (write 64 KB at
  `stage_off`, read back, reboot, app still valid).

#### W1 pure logic and settings (3 writers, parallel)

| Owner | Files (exclusive)                                                                                                                                                                                                                                                                                                  | Verification                                                                                                         |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------- |
| A1    | `src/fw_update.h` (new, header-only state machine: timer, policy gates, version compare, stage_off rule, record encode/decode), `test/test_fw_update/` (new)                                                                                                                                                       | `pio test -e native_fw_update` (env added by the orchestrator before dispatch)                                       |
| A2    | `src/fw_update_assets.h` (new: env to asset name table, the three renames, release JSON stream scanner for one asset), `test/test_fw_update_assets/` (new, with a 65 KB fixture cut from the live API)                                                                                                             | `pio test -e native_fw_update_assets`                                                                                |
| A3    | `src/meshcom_settings.h`, `src/config_json.h` (two rows), `src/command_functions.cpp` (`--autoupdate`, `--updrepo`, `--update`, help lines, `--info` line, SN1 keys), `test/golden/settings_schema_lint.py`, `test/test_command_setters/`, `test/test_config_json/`, `test/test_ble_phone_harness/` (SN1 size pin) | `pio test -e native_command_setters`, `native_config_json`, `native_ble_phone_harness`; `sh test/golden/selftest.sh` |

- Interface fixed up front by the orchestrator: `struct FwStageRecord`, `fwUpdateTick(now, inputs) -> action`,
  `fwAssetName(env) -> const char*`, `fwScanRelease(stream, env, out)`.
- Regression proof: version compare must order `v4.35v` < `v4.40a` < `v4.40b` < `v4.41a` and reject
  `v4.40a.10.02` versus `v4.40a` as "same".
- Gate: stage 1+2, builds `heltec_wifi_lora_32_V3` and `ttgo_tbeam`, artifact check
  `grep -c autoupdate` >= 1 on both ELFs.

#### W2 network and staging (1 writer + orchestrator, serialized: hardware)

| Owner | Files (exclusive)                                                                                                                                                                                                                                  | Verification                                                       |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| B1    | `src/esp32/fw_update_net.{h,cpp}` (new: HTTPS check, meta scan, streaming download into the stage region, SHA-256, CRC read-back, record write), `src/esp32/esp32_main.cpp` (one tick call next to the 15 min block ~`:2632`, WiFi gating `:2019`) | build `heltec_wifi_lora_32_V3`; bench B1 on DK5EN-1 (orchestrator) |
| orch  | `variants/*/platformio.ini` (`MC_ENV_NAME`, scripted), `platformio.ini`, golden json                                                                                                                                                               | `pio run` of all default envs once, sequentially                   |

- Bench B1 (DK5EN-1, own release feed `DK5EN/MeshCom-Firmware` pointed at a test release with a
  known image): `--updrepo`, `--autoupdate notify`, `--update check` shows the tag; `--update install`
  stages; `--update status` shows crc and len; power-cycle; record survives.
- Gate: stage 1+2, flash and heap deltas recorded against baseline, advisor pass.

#### W3 Safeboot apply (1 writer, serialized: safeboot build slot)

| Owner | Files (exclusive)                                                                                                                                                                                                                                                                               | Verification                                                                                    |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| C1    | `src/safeboot/main.cpp` (apply step before `wifiConnect()`), `src/safeboot/fw_apply.h` (new, host-testable copy/verify plan), `src/safeboot/ota_state.h` (one `Applying` state so `/ota/state` stays truthful), `test/test_safeboot_state/`, `tools/safeboot.py` (size guard unchanged, report) | `pio test -e native_safeboot`; build `esp32-safeboot` and `esp32-S3-safeboot`; size report both |

- Bench B2 (DK5EN-1 then DK5EN-92): staged image from W2, `--update install` with the window
  forced open (`--update install now`), observe `[SAFEBOOT];apply;...`, node boots the new version,
  record cleared. B3: pull power during the copy, confirm the retry on next boot.
- Rebuilt `safeboot.bin` and `safeboot-s3.bin` are committed with the wave (they are tracked).

#### W4 web GUI and notify (1 writer)

| Owner | Files (exclusive)                                                                                                                                                                            | Verification                                                            |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| D1    | `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp`, `src/web_functions/web_nodefunctioncalls.cpp`, `tools/webgui_badge_test.js` or a new `tools/webgui_update_test.js` | jsdom suite; `info_switch_lint`, `producer_match_lint`; build Heltec V3 |

#### W5 gate, bench, docs, upstream slice (orchestrator)

- Full stage 1+2, all default envs built once, `tools/bench/ota_regression.py` unchanged and green
  (manual OTA must keep working), AU bench B1-B3 recorded in `docs/bench-ota-regression.md`.
- Docs: `docs/safeboot-ota-contract.md` (new "Staged apply" section), `docs/settings-registers.md`,
  `docs/architecture/11-wire-format.md` (+html) for SN1 keys, `docs/test-suite-map.md`, BACKLOG rows
  AU-01..06, RESUME.
- Upstream PR slice: `src/fw_update*.h`, `src/esp32/fw_update_net.*`, `src/safeboot/*`,
  `src/command_functions.cpp`, `config_json.h`, `meshcom_settings.h`, web files, `variants/*`
  (`MC_ENV_NAME`). The fork feed is not an upstream concern; the default repo is icssw-org.

### 4.6 Open items for the maintainers

- Whether icssw-org will keep the `<env>.bin` asset scheme stable and tag only `vX.YYz`.
- Whether a release that changes the partition layout gets a marker asset (for example
  `LAYOUT-CHANGE`) so AU can refuse it. v1 cannot detect it.

### 4.7 W0 results (bench 2026-10-05) -- the design does not fit, decision needed

Throwaway INSTRUMENT probe (not committed): `WiFiClientSecure` with the IDF certificate bundle
that `libmbedtls.a` already links (`_binary_x509_crt_bundle_start`, 63.7 KB, no embedding
needed), GET `api.github.com/repos/DK5EN/MeshCom-Firmware/releases/latest`.

| Board                | Result                                                                                                          |
| -------------------- | --------------------------------------------------------------------------------------------------------------- |
| DK5EN-1 Heltec V3    | HTTP 200, 67 KB JSON with `tag_name`, handshake 2.2 s; free heap 119 KB, minimum during TLS 63 KB               |
| DK5EN-92 T-Beam v1.2 | in the loop task: TASK_WDT reset. In its own task: HTTP 200, handshake 5.3 s; free heap 58.6 KB, minimum 5.4 KB |

Flash cost of the TLS stack (WiFiClientSecure + mbedtls TLS/x509 + bundle), measured as the
image delta on Heltec V3: **188 KB**. Release Heltec V3 is 1,558,821 B today, so an AU image is
about 1.76 MB. The 4 MB table (`partitions-4MB-safeboot.csv`, used by Heltec V3 and most S3 and
all classic boards) gives `ota_0` 3324 KB, stage region `slot/2` = 1,701,888 B: **an AU image no
longer fits its own staging region** (it would need 3.5 MB for running + staged). Only the
16 MB-table boards (T-Deck, T-Deck Plus) fit as designed.

Consequences:

- AU-D6 decided: classic ESP32 out of v1 (5.4 KB minimum heap, WDT unless a dedicated task, and
  the image size rule above).
- The download must run in its own FreeRTOS task (S3 too: 2.2 s blocking handshake plus the body).
- D3 (stage the raw image in the `ota_0` tail) fails on 4 MB-table S3 boards. Options in 4.8.

### 4.8 Options after W0

| Option                                | How                                                                                                                                                                                                                               | Cost / risk                                                                                                                            |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| A. Compressed staging (recommended)   | Releases also carry `<env>.bin.zz` (zlib). The app stages the compressed image (about 1.0-1.1 MB) in the tail; Safeboot inflates it into the head with the ROM `tinfl` (ESP32/S3 ROM miniz), CRC + `esp_image_verify` as planned. | Release process adds one asset per env; prod works only once icssw-org publishes the `.zz` assets. Safeboot code small (ROM inflater). |
| B. Trim the certificate set           | Embed only the roots for github.com and objects.githubusercontent.com instead of the 64 KB bundle.                                                                                                                                | Saves about 60 KB: image 1.70 MB against a 1.70 MB limit -- no margin, the next feature breaks AU. GitHub CA changes break it.         |
| C. New partition table on 8 MB boards | Heltec V3 has 8 MB flash; give it an 8 MB table with a staging partition.                                                                                                                                                         | One-time USB/web-flasher reflash of every such node; 4 MB-flash boards stay excluded.                                                  |
| D. 16 MB-table boards only            | Ship AU for T-Deck / T-Deck Plus first.                                                                                                                                                                                           | Tiny audience; the Heltec V3 fleet gets nothing.                                                                                       |

### 4.9 Decisions after W0 (operator, 2026-10-05)

| Id     | Decision                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| AU-D11 | Compressed staging (option A): every release also carries `<asset>.bin.zz` (zlib). The app downloads the `.zz` into the END of `ota_0` (`stage_off` = slot size minus the 64 KB-rounded compressed length; refused if the running image reaches into it); Safeboot inflates it into the head with the ROM `tinfl` and aborts if the inflated image would reach `stage_off`.                                                                                                                  |
| AU-D12 | Trimmed root CAs (option B): only the roots of the GitHub hosts AU talks to (api.github.com, github.com, the release-asset object host), compiled in as PEM; no 64 KB bundle. GitHub is the only supported source.                                                                                                                                                                                                                                                                           |
| AU-D13 | Classic ESP32 stays in scope: W2 measures heap reductions on DK5EN-92 (download in its own task, BLE quiet during the download, smaller TLS buffers) before AU is enabled there.                                                                                                                                                                                                                                                                                                             |
| AU-D14 | A flash memory map of the `ota_0` image (`tools/flash_map.py`, report on the operator's Desktop) drives a later de-bloat wave to regain headroom.                                                                                                                                                                                                                                                                                                                                            |
| AU-D15 | (advisor W1) The stage record carries the expected inflated length (`ilen`, from the raw `.bin` asset size); the app refuses a release whose `ilen` would reach `stage_off`, Safeboot re-checks before erasing the head. Record magic `FWS2`, 112 B.                                                                                                                                                                                                                                         |
| AU-D16 | (advisor W1) Reinstall guard: the app persists the tag it last staged/handed over and never installs the same tag again (`fwShouldInstall`); a running image without `MC_BUILD_TAG` reports an undated version, so any dated same-letter release looks newer. The release build MUST export `MC_BUILD_TAG` (W5: release skill + `.bin.zz` assets).                                                                                                                                           |
| AU-D17 | (advisor W1) The battery floor (AU-D4) also gates the DOWNLOAD step in W2 (heavier than the handover copy); the W2 caller feeds the whole release JSON (about 65 KB) to the scanner instead of stopping at the first asset match.                                                                                                                                                                                                                                                            |
| AU-D18 | (W2 bench 2026-10-05) Classic ESP32: with the trimmed roots mbedTLS verifies the whole P-384 ECDSA chain itself and fails with -9984 (X509 verify failed) on DK5EN-92, while the S3 passes with the same code; W0 measured about 5 KB minimum heap during TLS there. Suspected allocation failure inside the signature check. Next experiment: pause NimBLE (deinit/re-init) around CHECK/DOWNLOAD on classic, measure, then decide. Until then AU on classic stays notify-only in practice. |
| AU-D19 | (W3 bench 2026-10-05) IDF 4.4 write-protects the running app partition (esp_partition_main_flash_region_safe; DANGEROUS_WRITE_ABORTS -> abort). The app stages through a private esp_flash_t copy whose os_func has no region_protected hook, used by one helper that only accepts [ota0+stageOff, end) after fwStageLayout proved the running image ends below stageOff.                                                                                                                    |
| AU-D20 | (AU-D18 resolved, bench 2026-10-05) Classic ESP32 pauses NimBLE (`deinit(true)`, full rebuild via `bleStackStart()`) around CHECK/DOWNLOAD when no phone is connected (`AU_BLE_PAUSE`, classic only). DK5EN-92: free heap 89,240 B / largest block 59,380 B, dev-channel CHECK succeeded (was -9984), BLE resumed, ble_cycle 3/3. A CHECK started while a phone is connected skips the pause and may fail with -9984 on classic -- by design.                                                |

## 5. NMTU: MTU setting for firmware and Safeboot (#1190)

### 5.1 Facts (recon `scout-1190.md`)

- RAK4631 already has it: `node_ethmtu` (`meshcom_settings.h:174`, `config_json.h:349`, range
  1280..1500, default 1500), `--ethmtu` (`command_functions.cpp:3574-3600`), web field RAK-only
  (`web_functions.cpp:2888-2890`), applied by `webApplyEthMss()` (`web_functions.cpp:86-144`) which
  writes `Sn_MSSR = mtu - 40` on every CLOSED socket before `web_server.begin()`. Shipped as the
  #1183 fix (`50fdfe65`), present in `upstream/dev`.
- ESP32: lwIP is prebuilt with `CONFIG_LWIP_TCP_MSS=1436`; a `-D` is shadowed by `sdkconfig.h` and
  lwIP is not recompiled. Runtime lever: `netif->mtu` on the lwIP netif
  (`esp_netif_get_netif_impl()`); lwIP's `tcp_eff_send_mss()` caps the advertised and the effective
  MSS at `netif->mtu - 40` when `TCP_CALCULATE_EFF_SEND_MSS` is on (the lwIP default, assumed on
  for the prebuilt libs, verified on the bench in W1). No `esp_netif_set_mtu` and no `TCP_MAXSEG`
  socket option exist in either framework.
- TCP consumers on ESP32: web server (WiFiServer), net console 2323, KISS 8001, external radio
  client; on RAK: web server (EthernetServer). AU adds an HTTPS client (section 4). Everything else
  is UDP with datagrams of at most about 730 B (extern JSON) and 255 B (gateway), so a tunnel MTU
  of 1280 or more never fragments them.
- Safeboot already loads `node_ethmtu` through the shared schema in `wifiConnect()`
  (`src/safeboot/main.cpp:217`); it uses ESPAsyncWebServer over AsyncTCP, which has no MSS setter,
  so the `netif->mtu` route is the only one there too.

### 5.2 Design

- Keep the setting key `node_ethmtu` (no migration). Add `--mtu <1280..1500>` as the user-facing
  command on every board, keep `--ethmtu` as an alias (RAK users and docs). Default 1280 (NMTU-D2 in 3.2a).
- ESP32 app: header-only `src/esp32/netif_mtu.h` with `applyNetifMtu(uint16_t)` that sets `mtu` on
  the STA and AP netifs. Called from the WiFi event handler on `GOT_IP` and `AP_START`, and from
  the Ethernet `GOT_IP` on T-ETH-ELITE, always before the first `accept()`. The WiFi OFF/STA cycles
  at `udp_functions.cpp:643-688` recreate the netif, so the event hook is the right place, not
  setup.
- Safeboot: same header, included via the existing `build_src_filter` rule (header-only, no new
  `.cpp`), called in `safebootWifiEventLog` on `GOT_IP` and after the AP start (`main.cpp:194`,
  `:249`), before `webServer.begin()` (`:636`).
- RAK: unchanged apart from the command alias. The MSSR latch rule stays as documented.
- Web: the field moves out of `#if BOARD_RAK4630` and is shown on every board; `--info` prints
  `MTU: <n>` everywhere.

### 5.3 Decisions and defaults (NMTU)

| Id      | Question                                                   | Default                                                                                                                                                            |
| ------- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| NMTU-D1 | Rename the setting                                         | no, key stays `node_ethmtu`; `--mtu` and `--ethmtu` both set it                                                                                                    |
| NMTU-D2 | ESP32 default                                              | superseded: 1280, see 3.2a                                                                                                                                         |
| NMTU-D3 | If `netif->mtu` does not move the SYN-ACK MSS on the bench | ship the setting as "outgoing only" (it still caps our send segments, which helps the AU download) and record the gap; a pcb-level hack on AsyncTCP is not pursued |
| NMTU-D4 | Pin the RAK13800-W5100S library                            | yes, to the commit in use today (`platformio.ini:84` is unpinned)                                                                                                  |
| NMTU-D5 | Safeboot binaries                                          | rebuilt and committed with the wave                                                                                                                                |

### 5.4 Wave (NMTU, one wave, 2 writers)

| Owner | Files (exclusive)                                                                                                                                                                                                                                                             | Verification                                                                                          |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| A1    | `src/esp32/netif_mtu.h` (new), `src/esp32/esp32_main.cpp` (event hook), `src/esp32/esp32_eth.cpp` (T-ETH hook), `src/safeboot/main.cpp` (two calls), `test/test_netif_mtu/` (new, host test of the clamp and the "apply once per link-up" guard)                              | `pio test -e native_netif_mtu`; build `heltec_wifi_lora_32_V3`, `esp32-safeboot`, `esp32-S3-safeboot` |
| A2    | `src/command_functions.cpp` (`--mtu` alias, help, `--info` on all boards), `src/web_functions/web_functions.cpp`, `src/web_functions/web_setup.cpp` (field for all boards), `test/golden/settings_schema_lint.py`, `test/test_command_setters/`, `docs/settings-registers.md` | `pio test -e native_command_setters`; `sh test/golden/selftest.sh`                                    |
| orch  | `platformio.ini` (native env, W5100S pin), golden json, `safeboot.bin`, `safeboot-s3.bin`                                                                                                                                                                                     | stage 1+2                                                                                             |

- Bench (orchestrator, DK5EN-1 and DK5EN-92 over WiFi; DK5EN-90 regression): on the Mac,
  `sudo tcpdump -i en0 -nn 'tcp[tcpflags] & tcp-syn != 0 and host <ip>'` while opening the web GUI;
  the node's SYN-ACK must show `mss 1240` after `--mtu 1280` and `mss 1436` at 1500. Same on the
  Safeboot page after `--ota-update`. RAK: 60 page loads at 1400 as in the #1183 bench.
- Artifact check: `grep -c applyNetifMtu` >= 1 on the two app ELFs and both Safeboot ELFs.
- Upstream PR slice: `src/esp32/netif_mtu.h`, `esp32_main.cpp`, `esp32_eth.cpp`,
  `src/safeboot/main.cpp`, `command_functions.cpp`, web files, `platformio.ini` pin line only.

## 6. RM: secure remote management via LoRa (#1189)

### 6.1 Facts (recon `scout-1189.md`)

- HMAC-SHA256 exists three times, copy-pasted, ESP32-only, all for TCP challenge-response:
  `net_console.cpp:124-215` (console 2323, key `node_passwd`), `kiss_functions.cpp:488-502` (KISS
  8001, same key), `esp32/external_radio_glue.cpp:131-136` (build-time key). Nothing authenticates
  a LoRa frame. nRF52 compiles none of it; the only nRF52 crypto is `hash_pin` on the CC310.
- `node_passwd` is `char[15]`, at most 14 characters, space-padded, set by `--passwd`, masked in
  `--info` (`mask_secret.h`), exported in clear in the config JSON (operator decision 2026-08-30).
- Inbound DM to the own call: `OnRxDone` (`lora_functions.cpp:695`) -> `decodeAPRS` -> own-call
  branch at `:1604` (exact `strcmp` with SSID); `{ping}` at `:1608` is the precedent for a reply
  from RX context (`SendPong`, `loop_functions.cpp:3636`). Dedup: msg_id ring of 60 (`:960`) and
  `dmDedupCheck` (`:1755`, 16 slots, 1 h). Gateway ingress twins at `udp_frame_esp32.cpp:428` and
  `udp_frame_nrf52.cpp:409` deliver internet DMs to the same node.
- Task context: ESP32 `OnRxDone` runs in loopTask (T5 and T-Deck-Pro: a separate lora task); nRF52
  runs it in the LORA task and in the 1 KB timer-service task. No HMAC, no `printfdeb`, no flash
  write in RX context on nRF52. The in-tree deferral pattern is `queueDisplayText` (single slot,
  overwrites).
- `commandAction` (`command_functions.cpp:505/606`) is not reentrant, splits chained `--a --b`,
  writes flash and prints through `printfdeb`; loop task only, no output capture.
- Text rewriting on the way: `{` becomes `(` (`dm_text_escape.h`), `%` is decoded, 160 characters
  maximum, frame at most 255 B. McApp strips whitespace and a trailing `{digits`, treats a leading
  `!` as a bot command and a leading `-` text goes to local `commandAction` in the firmware
  (`loop_functions.cpp:4040`).
- Clock: no "valid" flag; RAK without Ethernet and Heltec without GPS or RTC may have none. A
  counter scheme needs no clock (D6).
- Existing weak precedent `{MCP}` (N-01, WONTFIX): do not extend.

### 6.2 Wire format

Command, sent as a normal DM from the SysOp's node to the managed node:

```
RM1 <ctr> <cmd> [args] <tag>
RM1 42 reboot 3f9ac2e17b0d5e44
RM1 43 txpower 2 9d0e...   (16 hex)
RM1 0 sync <tag>           (ctr 0 is reserved for the counter query)
```

- `RM1` protocol id and version. Never starts with `-`, `!`, `{`, `:` and contains no `{` or `%`.
- `ctr` decimal 1..4294967295, strictly greater than the node's persisted high-water mark.
- `cmd [args]` from the allowlist (6.4), lower case, no leading dashes, single spaces.
- `tag` = first 8 bytes of HMAC-SHA256(K, canonical), hex lower case, where
  `canonical = "RM1|" + dst + "|" + src + "|" + ctr + "|" + cmd [+ " " + args]`,
  `dst` = the managed node's full call as configured, `src` = the sender call from the frame path,
  `K = SHA-256(node_passwd with trailing spaces stripped)`. Binding `dst` prevents replay against a
  sibling node that shares the password; binding `src` ties the reply address.
- Reply, a DM from the managed node to `src`:

```
RM1 <ctr> ok <status text>        RM1 42 ok rebooting
RM1 <ctr> err <reason>            RM1 43 err range
RM1 0 ok ctr=<hwm> v=<version>    (answer to sync)
```

followed by a tag over `"RM1R|" + dst + "|" + src + "|" + ctr + "|" + result`, so the SysOp's
client can verify it is the node that answered. Length budget: about 60 characters, far below the
160 limit.

- **Silent on failure:** a bad tag, an unknown command or a counter at or below the high-water
  mark produces no reply (no oracle, no DM storm), only a serial marker `[RM];reject;<reason>` and a
  counter in `--info`. Exception: the same `ctr` with the same valid tag within 10 minutes re-sends
  the cached reply without re-executing (lost-reply recovery, idempotent).
- **Rate limit:** at most one accepted command per 10 s; after 3 rejects in 90 s the node ignores
  `RM1` for 5 min (ADR-TOTP numbers, kept).
- **Where accepted:** LoRa only (`!msg_server`), DM to the exact own call, never group or
  broadcast. The gateway ingress twins are not wired. `RM1 ` DMs are excluded from store-node
  custody (they would be rejected by the counter anyway, but they must not occupy a slot).
- **Enable:** `--rm on|off` (default off) and a non-empty `node_passwd`; otherwise `RM1` DMs are
  ordinary text.
- **Counter state:** high-water mark persisted on every accepted command (loop task, one small
  write; the pattern of `src/msgid_counter.h`), plus the last `(ctr, reply)` in RAM.

### 6.3 Execution path

```
OnRxDone own-call branch (:1604)            loop task (esp32loop / nrf52loop)
  payload starts with "RM1 " ---> rmQueuePush(src, payload)  (2-slot ring, critical section)
                                             rmDrain(): verify tag, counter, allowlist
                                                        -> execute via rmExecute(cmd)
                                                        -> build reply, tag it
                                                        -> sendMessage("{src}RM1 ...")
```

- `rmExecute` is a small table that maps allowlisted commands to the existing setters
  (`commandAction` with a single, non-chained string from the table, never the raw text) or to a
  direct call (`reboot` sets a flag that the loop honours after the reply has been queued).
- All crypto runs in the loop task on both platforms. The portable `src/hmac_sha256.h` (RFC 6234
  SHA-256 + RFC 2104 HMAC, Arduino-free, about 150 lines) is the implementation on nRF52 and the
  host; on ESP32 it may delegate to mbedtls but the host test pins both to RFC 4231 vectors. The
  three existing mbedtls sites are left alone in the upstream PR (minimal change); a fork-only
  dedup wave may follow.

### 6.4 Command allowlist (D7)

| Command (remote form)                                                     | Mapped to                     | Reply status text                                        |
| ------------------------------------------------------------------------- | ----------------------------- | -------------------------------------------------------- |
| `reboot`                                                                  | reboot flag after reply       | `rebooting`                                              |
| `status`                                                                  | none                          | `v=<ver> up=<min> bat=<%> heap=<kB> gw=<0/1> mesh=<0/1>` |
| `sendpos`, `sendtrack`                                                    | `--sendpos`, `--sendtrack`    | `sent`                                                   |
| `gps on                                                                   | off`, `track on               | off`, `display on                                        | off`, `gateway on | off`, `mesh on | off` | the toggle table | `gps=on` etc. |
| `txpower <n>`                                                             | `--txpower n`, n <= board max | `txpower=<n>`                                            |
| `setout <a0..b7> <on\|off>` (RM W1 decision, matches the console command) | `--setout <pin> <on\|off>`    | `<pin>=<on\|off>`                                        |                   |
| `sync` (ctr 0)                                                            | none                          | `ctr=<hwm> v=<ver>`                                      |

Hard-blocked forever, whatever the tag: `cleanflash`, `ota-update`, `dfu`, `deepsleep`, `setcall`,
`passwd`, `webpwd`, `btcode`, `setssid`, `setpwd`, `wifiset`, `updrepo`, `updchan`, `autoupdate`, `rm`, `stor`,
anything with `--` or `;` inside.

### 6.5 Decisions and defaults (RM)

| Id    | Question                   | Default                                                                                                                                        |
| ----- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| RM-D1 | Key                        | `node_passwd` (D5). Accepted consequence: shared with console and KISS, exported in clear in the config JSON. Documented in the release notes. |
| RM-D2 | Tag length                 | 64 bit, hex. 32 bits of counter plus 64 bits of tag over the air at LoRa rates is not brute-forceable in practice.                             |
| RM-D3 | Who may send               | any call holding the key. Same-base-call is logged, not enforced (the source is spoofable anyway).                                             |
| RM-D4 | Reply retries              | the reply uses `sendMessage` with the normal PN retry ladder; the command itself is not retried by the node.                                   |
| RM-D5 | Counter loss on the sender | `sync` query; the SysOp's client stores the counter per target (McApp paper).                                                                  |
| RM-D6 | Gateway path               | LoRa only in v1. A SysOp on the internet side goes through their own node's LoRa.                                                              |
| RM-D7 | nRF52 in v1                | yes, via the portable HMAC. Crypto in loop task only.                                                                                          |

### 6.6 Waves (RM)

#### W1 crypto and protocol core (3 writers, parallel, host only)

| Owner | Files (exclusive)                                                                                                                                                                                                                                                | Verification                               |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| A1    | `src/hmac_sha256.h` (new, header-only), `test/test_hmac_sha256/` (new, RFC 4231 + FIPS 180-4 vectors)                                                                                                                                                            | `pio test -e native_hmac_sha256`           |
| A2    | `src/remote_cmd.h`, `src/remote_cmd.cpp` (new: parse, canonical string, verify, counter state, allowlist, reply build, rate limit; no Arduino includes), `test/test_remote_cmd/` (new)                                                                           | `pio test -e native_remote_cmd`            |
| A3    | `tools/remote_cmd.py` (new: tag computation and a sender over the net console or the node's UDP, mirrors `tools/hmac_connect.py`), `tools/tests/test_remote_cmd.py` (same vectors as A2, shared JSON fixture owned by A2 at `test/test_remote_cmd/vectors.json`) | `pytest -q tools/tests/test_remote_cmd.py` |

- Interface fixed up front: `bool rmParse(const char*, RmCmd&)`, `bool rmVerify(const RmCmd&, dst, src, key32)`,
  `RmVerdict rmCheck(...)`, `size_t rmReply(...)`, `struct RmState { uint32_t hwm; ... }`.
- Regression proof: a replayed `ctr`, a tag over a different `dst`, a chained `--a --b` and a
  payload with `{` must all be rejected in the host test; the vectors file pins the exact tags that
  `tools/remote_cmd.py` produces.

#### W2 firmware integration (3 writers, parallel, disjoint)

| Owner | Files (exclusive)                                                                                                                                                                                                                                                                                                       | Verification                                                                                                                                                                        |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B1    | `src/lora_functions.cpp` (hook after `{pong}` `:1627`, before `:1651`; store-node exclusion at `:1854-1860`), `src/rm_queue.h` (new, 2-slot ring with the `queueDisplayText` critical-section pattern)                                                                                                                  | build `wiscore_rak4631` (build slot)                                                                                                                                                |
| B2    | `src/loop_functions.cpp` (`rmDrain()`, reply via `sendMessage`, reboot flag), `src/loop_functions_extern.h`, `src/esp32/esp32_main.cpp` and `src/nrf52/nrf52_main.cpp` (one drain call each next to `:2139` / `:1434`), `src/msgid_counter.h` pattern reuse for the persisted hwm (new keyed store entry, read at boot) | compile-only `heltec_wifi_lora_32_V3` after B1's build finished (orchestrator serializes)                                                                                           |
| B3    | `src/command_functions.cpp` (`--rm on                                                                                                                                                                                                                                                                                   | off`, `--info`line with reject counters, help),`src/config_json.h`, `src/meshcom_settings.h` (`node_rm`int row),`test/golden/settings_schema_lint.py`, `test/test_command_setters/` | `pio test -e native_command_setters`; `sh test/golden/selftest.sh` |

- Artifact check: `grep -c rmDrain` >= 1 on RAK, Heltec V3 and T-Beam ELFs.
- Gate: stage 1+2, builds RAK (flash % versus 96.1 baseline), Heltec V3, T-Beam; advisor pass.

#### W3 bench, web surface, docs (orchestrator + 1 writer)

| Owner | Files (exclusive)                                                                                                         | Verification                                     |
| ----- | ------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| C1    | `src/web_functions/web_functions.cpp`, `web_setup.cpp` (switch `rm`, info row with counters), `Web-API_documentation.txt` | jsdom; `info_switch_lint`, `producer_match_lint` |

- Bench (own nodes, 2 dBm, DM only): DK5EN-1 commands DK5EN-90 with `tools/remote_cmd.py` through
  DK5EN-1's net console: `status`, `display off`, `display on`, `sync`, `reboot`; replay the
  captured `reboot` frame with `--injectraw`: no reply, `[RM];reject;replay`; wrong tag: no reply;
  bad tag 4 times: lockout marker. Then DK5EN-14 (ESP32-S3) as the managed node.
- Docs: `docs/adr-remote-hmac.md` (supersedes `adr-totp-remote-led.md`, which moves to the
  archive), `docs/architecture/11-wire-format.md` (+html) new section "RM1 command DM",
  `docs/settings-registers.md`, test-suite-map rows, BACKLOG RM-01..07, RESUME.
- Upstream PR slice: `src/hmac_sha256.h`, `src/remote_cmd.*`, `src/rm_queue.h`, hooks in
  `lora_functions.cpp`, `loop_functions.cpp`, the two main loops, `command_functions.cpp`, schema
  rows, web files. German description must state the amateur-radio-law position: command and reply
  are plaintext, the tag only authenticates.

## 7. SNF-GW: S&F for PMs that arrive through the central server (#1188)

The design is `docs/snf-gateway-concept-20261002.md` (stage 1, measurement, stage 3a/3b). This
section records what the recon changed and turns stage 1 into waves. Line numbers below are at
`a9e64cf6`; the concept's numbers were taken at `42dbf03a` and drift by 3-41 lines (table in the
recon report).

### 7.1 What the recon confirmed and corrected (recon `scout-1188.md`)

- The issue's rule "own callsign, any SSID" already exists: store mode `own` matches the base call
  (`msgstore.cpp:287`, `sameBaseCall`), and `msgstoreStore` excludes only the exact own call
  (`:317`). A gateway node runs the mailbox today (no `bGATEWAY` condition). Mode `own` on a gateway
  already holds sibling-SSID PMs that arrive by RF. The missing piece is server ingress only.
- The three concept gaps are unchanged at HEAD: no `msgstore` call in `udp_frame_esp32.cpp`,
  `udp_frame_nrf52.cpp` or `udp_functions.cpp`; `glueNotify` has no `addNodeData`
  (`msgstore_glue.cpp:159,193`); server routing is unknown.
- Six omissions in the concept:
  1. Neither `GATE` handler has a PN-repeat (XOR retry) check; the RF path has one at
     `lora_functions.cpp:963-981`. A retry copy via the server has a new msg_id and would refresh
     `stored_ms`. The hook must run the same `pnVariantIds` test.
  2. The mailbox stores stripped text plus `(src, dst, nnn)`, not the frame; delivery rebuilds a
     hop-0 frame with a fresh msg_id (`msgstore_glue.cpp:93-142`). "Store unchanged" is impossible
     and not needed.
  3. The handler appends the own call to the source path at `udp_frame_esp32.cpp:280-281` before
     any later hook. The echo guard ("path already contains the own call") must run before that
     append.
  4. A server-side `:ackNNN` for a held PM is not parsed in the UDP handlers; `msgstoreOnAck` must
     be called there, or a PM acked via the server stays held until expiry.
  5. `:sto` is not registered in the own-TX table or the dedup ring; if the server reflects the
     uploaded notice, the node treats it as a new foreign frame.
  6. nRF52: `OnRxDone` (LORA task) and the `GATE` handler (loop task) would both call
     `msgstoreStore`. The `gen` guard (`msgstore_api.h:56-62`) covers hook-versus-loop only. The
     store and ack calls from the UDP hook need the same critical section as `queueDisplayText`,
     and `msgstoreStore` must contain no `printfdeb` inside that region (W1 checks).

### 7.2 Design delta (stage 1, as built)

| Item                         | Decision                                                                                                                                                                                                                                              |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shared hook helper           | `src/msgstore_hook.h` (header-only): exclusions (`*`, group, `:rej`, `{`-prefix, PN repeat, `RM1 ` prefix from section 6), path-contains-own-call guard, payload strip. Used by `OnRxDone` and both `GATE` handlers.                                  |
| Hook position in `GATE`      | inside the is-new gate (`bUdpMsgIsNew` on ESP32, inline at `nrf52:477`), before the own-call append, for frames whose destination is not the exact own call. Cases: `:ackNNN` -> `msgstoreOnAck`; DM with `{NNN` and eligible dst -> `msgstoreStore`. |
| One-shot LoRa copy           | unchanged: the `udp_rx` enqueue stays; the ladder waits the existing 60 s.                                                                                                                                                                            |
| `:sto` feedback              | `glueNotify` additionally calls `addNodeData(buf,len,0,0)` when gateway and IP; registers the frame with `insertOwnTx` and `addLoraRxBuffer` like `loop_functions.cpp:4488-4495`. LoRa copy kept.                                                     |
| Store set on the server path | whatever `--store` mode is configured (`own`, `list`, `heard`); the issue's case is `own`.                                                                                                                                                            |
| Boards                       | S3 and RAK4631 only (`ENABLE_MSGSTORE`, `configuration_global.h:319`). Classic ESP32 gateways do not store; documented.                                                                                                                               |
| Stage 2 and 3                | unchanged from the concept: bench M1-M4 after stage 1 ships; `--storeannounce` only if M1 says routing depends on uploads.                                                                                                                            |

### 7.3 Decisions and defaults (SNF-GW)

| Id        | Question                                               | Default                                                                                   |
| --------- | ------------------------------------------------------ | ----------------------------------------------------------------------------------------- |
| SNF-GW-D1 | Store when the destination is directly heard right now | yes (concept default); the ladder waits 60 s behind the one-shot copy                     |
| SNF-GW-D2 | Suppress the LoRa `:sto` when the PM came via `GATE`   | no, keep both                                                                             |
| SNF-GW-D3 | Hold time                                              | `--storetime` as configured (24 h default); RAM-only, a reboot drops held PMs, documented |
| SNF-GW-D4 | "DK5EN" and "DK5EN-0" are the same base                | yes, as `baseCall` does today                                                             |
| SNF-GW-D5 | Multi-hop sibling                                      | out of scope; delivery needs direct presence (hop 0), as in 4.40a                         |

### 7.4 Waves (SNF-GW)

#### W1 helper and core (2 writers, parallel, host only)

| Owner | Files (exclusive)                                                                                                                                                                                    | Verification                                                       |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| A1    | `src/msgstore_hook.h` (new), `src/lora_functions.cpp` (replace the inline conditions `:1845-1880` with the helper; peer-delivery test stays RF-only), `test/test_msgstore_hook/` (new)               | `pio test -e native_msgstore_hook`; build `wiscore_rak4631` (slot) |
| A2    | `src/msgstore.cpp`, `src/msgstore_api.h` (export `sameBaseCall`, audit `msgstoreStore`/`msgstoreOnAck` for printf inside the critical region, add the nRF52 cross-task guard), `test/test_msgstore/` | `pio test -e native_msgstore` (existing env)                       |

#### W2 server ingress and feedback (3 writers, parallel, disjoint)

| Owner | Files (exclusive)                                                                                                                                                                            | Verification                                                              |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| B1    | `src/esp32/udp_frame_esp32.cpp` (hook before `:280`, ack purge, PN-repeat test)                                                                                                              | build `heltec_wifi_lora_32_V3` (slot)                                     |
| B2    | `src/nrf52/udp_frame_nrf52.cpp` (twin), `test/test_udp_frame_twin/test_main.cpp` (agreement case per row of concept 3.1 plus echo guard and ack)                                             | `pio test -e native_udp_frame_twin`                                       |
| B3    | `src/msgstore_glue.cpp` (`:sto` upload, own-TX and dedup registration), `test/test_msgstore` glue stub only if the existing stub file is separate (else report and the orchestrator applies) | compile-only `wiscore_rak4631` after B1's build (orchestrator serializes) |

- Regression proof (B2): the twin test must fail on the old handlers for "server PM to sibling SSID
  is stored" and pass after.
- Bench (orchestrator): `tools/mock` UDP 1990 server on the Mac feeding a `GATE` PM for DK5EN-92 to
  DK5EN-90 (RAK, gateway on, store `own`), DK5EN-92 off: `[STORE]` shows the slot, `:sto` appears
  in the mock's upload log; DK5EN-92 on: delivery, `:ack`, slot purged. Then the same with the real
  server from an own internet-side client (McApp) and M1-M4 as in the concept.
- Docs: `snf-gateway-concept-20261002.md` status line and section 3 corrections,
  `commands-store-node.md`, `client-integration-store-forward.md`, `CHANGELOG-snf.md`,
  `11-wire-format.md` (+html) new section ":sto and the store node" (missing today), test-suite-map,
  BACKLOG SNF-GW-01..06 (SNF-GW parent row kept).
- Upstream PR slice: `src/msgstore_hook.h`, `msgstore.cpp`, `msgstore_api.h`, `msgstore_glue.cpp`,
  `lora_functions.cpp`, the two UDP twins. The concept's open question "review with Kurt first"
  (BACKLOG row SNF-GW) still applies to the PR, not to the fork build.

#### W3 STOR announce (2 writers + orchestrator, SNF-D6/D7)

| Owner | Files (exclusive)                                                                                                                                                                                               | Verification                                                |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| C1    | `src/stor_announce.h` (new: announce-set builder, encoder per `snf-gateway-concept-20261002.md` 5.3, chunking below 255 B, change debounce), `test/test_stor_announce/` (new)                                   | `pio test -e native_stor_announce`                          |
| C2    | `src/command_functions.cpp` (`--stor on\|off`, help, `--info`), `src/config_json.h`, `src/meshcom_settings.h` (`node_stor`), schema lint, `test/test_command_setters/`, mock server STOR support under `tools/` | `pio test -e native_command_setters`; selftest; mock pytest |
| orch  | `src/udp_functions.cpp` (send next to `sendKEEP()`, 15 min + debounced on change), `platformio.ini`, golden json                                                                                                | stage 1+2; mock-server bench on DK5EN-90 and DK5EN-1        |

## 8. BLC: BLE AutoReconnect (#1191)

### 8.1 Facts (recon `scout-1191.md`)

- The node is a peripheral. It cannot connect to the phone; it can only be reliably connectable.
- ESP32 (NimBLE 2.2.3, `MAX_CONNECTIONS=1`): `pServer->advertiseOnDisconnect(true)`
  (`esp32_main.cpp:1925`) restarts advertising inside the library right after `onDisconnect`, with
  no delay and no duration limit. A failed connect also restarts it (lib). Connection parameters
  requested at connect: 30-60 ms, 4 s supervision timeout (`:377`, iOS rejected 1.8 s, `dbd5db9c`).
  No bonding by design (N-07 WONTFIX); the PIN is an app-layer SHA-256 in the hello frame.
- nRF52 (Bluefruit): `restartOnDisconnect(true)`, `start(0)` forever (`nrf52_ble.cpp:185-201`);
  the library restarts on `BLE_GAP_EVT_DISCONNECTED`. Supervision timeout is the 2 s library
  default; nothing is requested.
- The app (Ionic, `@capacitor-community/bluetooth-le` 8.3.0, release 4.29) has a reconnect loop
  but it is commented out under "RECONNECT DISABLED" (`src/pages/Connect.tsx:908-931`, resume
  trigger `:285-296`); instead `BleDiscoAlert` tells the user to reconnect. The Android plugin
  hard-codes `connectGatt(..., autoConnect=false)`.
- Firmware defects found: (1) ESP32 per-session reset runs only on the loop-observed edge
  `!deviceConnected && oldDeviceConnected` (`esp32_main.cpp:3288-3299`); a disconnect and reconnect
  between two loop passes leaves `isPhoneReady` at 1 for the new central (skips the hello and PIN
  check). nRF52 resets inside its callbacks and has no such race. (2) nRF52 discards the
  disconnect reason (`nrf52_ble.cpp:256-257`); ESP32 prints it (`:385`). (3) No counters, no
  advertising self-check (`isAdvertising()` is never called). (4) `tools/bench/ble_cycle.py` has no
  gap-0 or unclean-drop mode and does not measure disconnect-to-first-advert.

### 8.2 Design

Firmware deliverable: make the node provably connectable at all times and observable; the
reconnect itself is an app change, specified in 8.4 for the app maintainers.

| Item                      | Change                                                                                                                                                                                                                                                                              |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32 session reset       | move the per-session reset into `onConnect` (reset `isPhoneReady`, `g_ble_uart_is_connected`, `bAckInfo`, prepare flags) and keep the loop edge for logging only; `deviceConnected` becomes `volatile`. Extract the flag set into `src/ble_session.h` (header-only, host-testable). |
| nRF52 reason logging      | print `[BLE ];disconnect;reason;0x%02x;text;...` in `disconnect_callback`, short (under 64 B, no `Print::printf` malloc), same decode table as `bleReasonStr`.                                                                                                                      |
| Counters                  | connects, disconnects by class (timeout 0x08, remote 0x13, local 0x16, failed 0x3E, other), advertising restarts; shown in `--info` as `BLE: con=<n> dis=<n> to=<n> rem=<n> adv=<n>` and in `[BLE ];stat` every 15 min with `--bledebug on`.                                        |
| Advertising self-check    | ESP32: every 5 s while `!deviceConnected`, if `!pAdvertising->isAdvertising()` then `start()` and count. nRF52: `Bluefruit.Advertising.isRunning()` likewise.                                                                                                                       |
| Rings on disconnect       | no flush (BLC-D1); delivery is already gated on `isPhoneReady`, so queued frames wait behind the new hello and config burst.                                                                                                                                                        |
| nRF52 supervision timeout | unchanged in v1 (BLC-D2); needs an iOS bench.                                                                                                                                                                                                                                       |

### 8.3 Decisions and defaults (BLC)

| Id     | Question                                 | Default                                                                         |
| ------ | ---------------------------------------- | ------------------------------------------------------------------------------- |
| BLC-D1 | Flush phone rings on disconnect          | no; stale frames are delivered after the next hello, which is today's behaviour |
| BLC-D2 | nRF52 supervision timeout 2 s versus 4 s | leave 2 s; revisit with an iOS capture on DK5EN-90                              |
| BLC-D3 | Bonding                                  | stays off (N-07); connect-by-public-address works without it                    |
| BLC-D4 | NUS UUID in the short advert             | no change; `--blelong` already exists for UUID-filtered scans                   |
| BLC-D5 | Firmware disconnect on silent app        | no; the 4 s supervision timeout is the stack's job                              |

### 8.4 App side (for the app maintainers, outside this campaign)

- Re-enable the reconnect loop in `Connect.tsx` with exponential backoff (2, 4, 8, 16, 30 s cap)
  for every non-manual disconnect, including app resume; stop on manual disconnect or after
  10 min; one 5 s scan per attempt is what the existing `reconnectBLE()` does.
- Android: expose `autoConnect=true` in the plugin (`Device.kt:354-372`) or patch locally; the
  firmware's public address makes connect-by-address work.
- iOS: a pending `connect` to the known peripheral UUID instead of a 15 s timeout.
- Expect the node to be unconnectable for up to 4 s (ESP32) or 2 s (nRF52) after a silent phone
  loss (supervision timeout); the first attempt may hit 0x3E and must not be treated as fatal.

### 8.5 Wave (BLC, one wave, 3 writers)

| Owner | Files (exclusive)                                                                                                                                                                                         | Verification                                                            |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| A1    | `src/esp32/esp32_main.cpp` (callbacks `:350-396`, loop `:3274-3314`, self-check), `src/ble_session.h` (new), `test/test_ble_session/` (new)                                                               | `pio test -e native_ble_session`; build `heltec_wifi_lora_32_V3` (slot) |
| A2    | `src/nrf52/nrf52_ble.cpp` (reason, counters, self-check)                                                                                                                                                  | compile-only `wiscore_rak4631` after A1's build                         |
| A3    | `tools/bench/ble_cycle.py` (`--gap 0`, `--drop unclean` via a scan-only abort, metric disconnect-to-first-advert), `tools/bench/test_ble_cycle.py` (new), `src/command_functions.cpp` (`--info` BLE line) | `pytest -q tools/bench/test_ble_cycle.py`; `help_parity_lint`           |

- Regression proof (A1): host test for the edge race: connect, disconnect, connect within one tick
  must leave `isPhoneReady == 0` (fails on the old edge logic, passes on the callback reset).
- Bench (orchestrator): `ble_cycle.py --cycles 50 --gap 0` and `--drop unclean --cycles 20` against
  DK5EN-1 and DK5EN-90; 0 failed connects after the first attempt, disconnect-to-advert under
  500 ms, counters in `--info` match the tool's count.
- Docs: `docs/architecture/08-defect-catalogue.md` (new entry for the ESP32 edge race),
  `CHANGELOG-stability.md`, `11-wire-format.md` only if the `--info` line is on the wire,
  test-suite-map, BACKLOG BLC-01..05. A reply on #1191 pointing the app maintainers to 8.4.
- Upstream PR slice: `esp32_main.cpp`, `ble_session.h`, `nrf52_ble.cpp`, `command_functions.cpp`.

## 9. Campaign order and resume point

| Step | Feature | Waves | Why in this position                                                                            |
| ---- | ------- | ----- | ----------------------------------------------------------------------------------------------- |
| 1    | NMTU    | 1     | smallest, touches `esp32_main.cpp` and `command_functions.cpp` once, rebuilds Safeboot early    |
| 2    | BLC     | 1     | small, independent, closes a real race; `esp32_main.cpp` again but after NMTU has landed        |
| 3    | SNF-GW  | 3     | no settings surface, no `command_functions.cpp`; `lora_functions.cpp` before RM's hook          |
| 4    | RM      | 3     | `lora_functions.cpp` hook lands after SNF-GW's helper exists (RM's custody exclusion uses it)   |
| 5    | AU      | 5     | largest, hardware-serialized waves, `variants/*` sweep, Safeboot binaries rebuilt a second time |

Each feature is its own commit series `<area>(<ID>): ...` and its own upstream PR candidate. The
orchestrator applies hotspot edits (section 3.3) at each gate, updates this paper's status table
and the BACKLOG rows per wave, and never runs two `pio` processes.

Status:

| Feature | Wave | State                                                                                                                                                                                                                                                                                                                                                               | Commit      |
| ------- | ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| NMTU    | W1   | done: bench DK5EN-1 app+Safeboot MSS 1240/1436, AP path and classic Safeboot not benched                                                                                                                                                                                                                                                                            | this commit |
| BLC     | W1   | done: ble_cycle 50x gap0 + 20x unclean on DK5EN-1 and DK5EN-90, 0 failures, advert median 346-654 ms (max 2865, scanner-inclusive); RAK counters con=70 dis=70 match; open B1 bleQueue carry-over                                                                                                                                                                   | this commit |
| SNF-GW  | W1   | done: msgstore_hook.h shared decision (+RM1 exclusion), MSGSTORE_LOCK on all hooks and msgstoreLoop sections, msgstoreSameBaseCall; host-only wave, no bench                                                                                                                                                                                                        | this commit |
| SNF-GW  | W2   | done: GATE hook both twins, server :ack purge, :sto upload; bench DK5EN-90 + mock (INSTRUMENT image for --srvip): PM to DK5EN-93 held, :sto at the mock, server :ack124 purged                                                                                                                                                                                      | this commit |
| SNF-GW  | W3   | done: STOR (node_stor, default off), stor_announce.h, mock STOR support; bench DK5EN-90 + mock: off = 0 datagrams, on = 1 datagram with DK5EN-1/92/98 (direct, own base), own call excluded                                                                                                                                                                         | this commit |
| RM      | W1   | done: hmac_sha256.h, remote_cmd.{h,cpp}, tools/remote_cmd.py + 21 shared vectors; setout form a0..b7 on/off; rate rejects do not count to the lockout; host only                                                                                                                                                                                                    | this commit |
| RM      | W2   | done: rm_queue.h + RX hook (LoRa, own call, --rm on + passwd), rm_runtime (exec table, hwm two-slot store on nRF52, fail closed), --rm; bench RAK<->Heltec both directions: status/display/sync/reboot, hwm survives reboot, replay rejected, replies shown at the sender; fixed on the bench: nRF52 rename failure, replies parsed as commands                     | this commit |
| RM      | W3   | done: web switch `rm` + info row (live on DK5EN-1), docs/adr-remote-hmac.md (TOTP ADR archived), 11-wire-format RM1 + :sto/STOR sections                                                                                                                                                                                                                            | this commit |
| AU      | W0   | done (bench): TLS ok on S3 and (in a task) classic; TLS costs 188 KB flash -> raw staging does not fit 4 MB-table boards; decision needed (4.8)                                                                                                                                                                                                                     |             |
| AU      | W1   | done: fw_update.h (policy, compare, layout, FWS2 record, reinstall guard), fw_update_assets.h (asset table, JSON scanner), --autoupdate/--updchan, MC_ENV_NAME/MC_BUILD_TAG via tools/mc_build_defines.py; host only                                                                                                                                                | this commit |
| AU      | W2   | done: fw_update_net (task, 5 trimmed roots, chunked decoder, digest+CRC, FWS2 record), auTick, --update check/install/status, make_zz.py; bench DK5EN-1: prod not newer, dev v4.40a.10.02 newer + no_asset; classic DK5EN-92: tls -9984 (verify failed, suspected heap) -- AU-D13 open                                                                              | this commit |
| AU      | W3   | done: Safeboot apply (CRC, erase head, ROM tinfl inflate, esp_image_verify, tries<=3, self-restart retry), app handover + --update apply, stage writes via a guarded private esp_flash_t (IDF protects the running partition), INSTRUMENT stagelan; bench DK5EN-1 end-to-end: 1.1 MB .zz staged in 9 s, Safeboot applied 1,722,160 B in 10.3 s, booted v4.40a.10.06 | this commit |
| AU      | W4   | done: web setup card (auto update mode, channel), info row (env, tag, avail/installable, staged, error, Check now), banner with Install / Apply now; checked live on DK5EN-1                                                                                                                                                                                        | this commit |
| AU      | W5   | done: release v4.40a.10.05 with 27 .bin.zz assets; dev-channel test on DK5EN-1: GitHub check, real download 1,102,751 B in 10.8 s, Safeboot apply 1,728,752 B in 10.5 s, boots v4.40a.10.05; old-Safeboot guard benched on DK5EN-92                                                                                                                                 | bfe5310c    |

Next: campaign complete. Open: STOR server approval, SNF M1-M4, prod .bin.zz assets upstream, upstream PRs.
`tools/mock/meshcom_server.py` (tests `tools/mock/test_mock_server.py`).

## 10. Questions for the maintainers and other repos

- Server (icssw-org): routing of PMs to store gateways (concept M1-M4), acceptance of a `STOR`
  datagram, treatment of `:sto` from a gateway (section 7).
- App (MeshCom-MobileApp): re-enable the reconnect loop (section 8.4).
- McApp: the HMAC admin cycle, `~/Desktop/mcapp-hmac-remote-admin.md`.
- Release process: `<env>.bin` asset names and version tags stay stable; a layout-change marker
  (section 4.6).
