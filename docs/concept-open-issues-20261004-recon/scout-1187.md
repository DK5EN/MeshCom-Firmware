<!-- Recon report by a read-only Sonnet scout, 2026-10-04, fork-dev a9e64cf6. Line references are a snapshot; re-verify before editing. -->

# scout-1187: Firmware Auto Update (upstream issue #1187), recon at fork-dev a9e64cf6

BLUF: the app can hand over to Safeboot today, and Safeboot can already write ota_0 over HTTP (upload server).
Neither has any HTTP(S) client or TLS. Safeboot has 5184 B of image headroom (704K slot), so TLS cannot go
into Safeboot without a partition-table change (serial/web flasher only). TLS in the app is feasible
(IDF bundle on, ~125 kB free heap on Heltec V3), but the app cannot rewrite its own single app slot.
nRF52 has no autonomous OTA path. Paths relative to repo root.

## Findings

### 1. App -> Safeboot handover (ESP32 only)

- Single entry: command `--ota-update`, src/command_functions.cpp:1109-1134 (`#ifdef ESP32`). Shows a display
  message (1112-1115), delay(2000), `esp_partition_find_first(APP, FACTORY, "safeboot")` (1119),
  `esp_ota_set_boot_partition(partition)` (1122), `loopCrumbClear()` (1123), `esp_restart()` (1124).
- Callers: web button src/web_functions/web_functions.cpp:2865 -> JS `callfunction('otaupdate')` ->
  src/web_functions/web_nodefunctioncalls.cpp:32-33 `commandAction("--ota-update")`. Any `--` command from BLE
  0xA0 / serial / net console reaches the same handler (commandAction), so BLE and serial work too.
  --help line: command_functions.cpp:1174. Not compiled on nRF52.
- No parameter, NVS flag or RTC-memory flag is passed. Safeboot learns nothing about why it was entered.
  The only state is the otadata/boot-partition pointer (factory=safeboot). Safeboot reads the same NVS
  settings via init_flash() (src/safeboot/main.cpp:217), so a new schema row is visible in Safeboot
  automatically (see 5).
- The app starts WiFi ONLY if `bGATEWAY || bEXTUDP || bWEBSERVER || bNETCONSOLE`: src/esp32/esp32_main.cpp:2019
  (also 2170, 4032). A node with none of these has no STA link in the app. "Connected to the Internet" is only
  `node_hasIPaddress` (meshcom_settings.h:186); no reachability probe except NTP (UDP) and ESP32Ping (include at
  src/udp_functions.cpp:54, no `Ping.ping` call found).
- Return path: Safeboot `setBootPartition_APP()` src/safeboot/main.cpp:366-377 (`esp_ota_set_boot_partition(ota_0)`),
  then ESP.restart() (main.cpp:818) or ElegantOTA auto-reboot 2.5 s (ElegantOTA.cpp:290,427).

### 2. Safeboot internals

- Build: envs esp32-safeboot / esp32-S3-safeboot, platformio.ini:1200-1285. Sources: src/safeboot/*, esp32_flash.cpp,
  settings_schema.cpp (build_src_filter 1233-1238). Framework: Tasmota platform 2026.02.30 = Arduino 3.3.7 (IDF 5.x);
  the app uses espressif32@^6.13 = Arduino 2.0.17 (IDF 4.4) (platformio.ini:1111, 1203; tools/ensure_tasmota_framework.py).
  Both share one ~/.platformio package dir name (swap hazard documented there).
- WiFi: wifiConnect() main.cpp:214-: creds from NVS `node_ssid`/`node_pwd` (219-220), static IP optional (283-299),
  `WiFi.begin(..,false)` 322, PMF off 324, non-blocking join; loop() 649-: 12 s retry, 25 s -> AP_STA with open AP
  (SSID=callsign, 192.168.4.1), mDNS after got_ip. SSID "none" or `--wifiap on` -> AP only (main.cpp:235-). STA join
  is NOT gated on gateway/webserver flags (only bWIFIAP forces AP). Spec: docs/safeboot-ota-contract.md.
- HTTP server: ESPAsyncWebServer, `webServer.begin()` main.cpp:636. Endpoints: ElegantOTA.cpp:26 `/update` (page),
  :51 `/ota/start?mode=fr|fs&hash=<md5>`, :249 `/ota/upload` (POST multipart); main.cpp:497 `/ota/cancel`, :526
  `/ota/info`, :596 `/ota/state`, :617 `/ota/scan`. Unauthenticated.
- Write path: `Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)` ElegantOTA.cpp:121; optional `Update.setMD5(hash)` :147
  (MD5 is OPTIONAL; without `hash` only Update's own checks + esp_image_verify at boot-switch apply);
  `Update.write()` per chunk :356; `Update.end(true)` :381; `isFinished()` :396. Writes straight into ota_0 from the
  first chunk (single slot).
- State: src/safeboot/ota_state.h (pure C++, host-tested by test/test_safeboot_state): STALL_MS 30000, FALLBACK_MS 180000
  (ota_state.h:93-94); Idle/Receiving/Verifying/Done/Aborted; `app_valid` gate (86-90, 209, 230). RAM only. No NVS/RTC
  persistence, no on-flash log. safeboot_log.h = serial tee (UART0 + HWCDC on S3), nothing stored. Serial markers
  `[SAFEBOOT];...` listed in docs/safeboot-ota-contract.md.
- App-valid check: checkAppImageValid() main.cpp:389-418 via `esp_image_verify(SILENT)`; after a failed/partial upload the node
  stays in Safeboot (no fallback timer, no reboot loop), contract "Single app slot".
- HTTP client / TLS in Safeboot: NONE. grep for WiFiClientSecure|mbedtls|HTTPClient|crt_bundle in src/safeboot = 0 hits.
  platformio.ini:1213 and :1253 set `-D HTTPCLIENT_NOSECURE` on both safeboot envs. The Tasmota framework ships
  HTTPClient/HTTPUpdate/Network libs and libmbedtls.a/libesp-tls.a and `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1`
  (~/.platformio/packages/framework-arduinoespressif32@src-627abe.../tools/esp32-arduino-libs/esp32/*/sdkconfig.h:793),
  so it is linkable, just not used.
- Size budget: partitions-4MB-safeboot.csv and partitions-16MB-safeboot.csv: `safeboot, app, factory, 64K, 704K`
  (= 720896 B), ota_0 starts at 768K. Current images: /safeboot.bin 715712 B (-> 5184 B = 0.7 % free), /safeboot-s3.bin
  676800 B (-> 44096 B free). Guard: tools/safeboot.py:`max_size = 704*1024`. A TLS client needs roughly 100-200 kB
  (my estimate, not measured) -> does not fit either image; growing the slot moves ota_0 = new partition table, which only
  the USB / web flasher writes (the app's OTA writes ota_0 only). tools/resource_baseline.json lists lower
  "flash_used" for safeboot (686411 / 643311), measured differently, do not use it for the slot budget.
- App partition sizes: 4 MB table ota_0 = 3324K; 16 MB table ota_0 = 12288K. Current app images ~1.45-1.8 MB
  (.pio/build/heltec_wifi_lora_32_V3/firmware.bin = 1545488 B).

### 3. Version string and release assets

- Defined: src/configuration_global.h:7-9 `SOURCE_VERSION "4.40"`, `SOURCE_VERSION_SUB "a"`, `SOURCE_VERSION_WEB_SUB "a"`.
  FLASH_VERSION (release date YYYYMMDD) line 128 (20260929 now), purely informative.
- Reported: `--info` command_functions.cpp:6270-6271 (`--MeshCom %-4.4s%-1.1s (build: date/time)`); web footer
  web_functions.cpp:1302 and info table :3396; BLE I-register `FWVER` "4.40 a" command_functions.cpp:6221-6227;
  on-air `shortVERSION()` = digits at SOURCE_VERSION+2 as uint8 (aprs_functions.cpp:30-36) + letter; ExtUDP json
  `firmware` extudp_functions.cpp:652.
- Compare: tags are `v<VER>.MM.DD[.n]` on the fork (e.g. v4.40a.10.02) and bare `v<VER>` upstream (v4.40a).
  Parse `^v(\d+)\.(\d+)([a-z])` -> (major, minor, letter) tuple compare; letter goes a..z within a minor and the minor
  rolls (4.35v -> 4.40a), so tuple order is correct. The date suffix only orders fork releases of one version.
  A fork build is NOT byte-identical to official same-version (release skill), so a version compare cannot detect
  fork-only fixes. No env name / board name is compiled in: `BOARD_HARDWARE` is not unique per env (three E22 envs
  share MODUL_HARDWARE EBYTE_E22 etc.), so mapping node -> asset name needs a new per-env macro (e.g. `-DMC_ENV_NAME`).
- Asset naming: `.claude/commands/release-firmware.md` (Step 5, "39 assets"): ESP32 app = `<env>.bin` (renames:
  LilyGo_T-Beam-1W -> T-Beam-1W.bin, LilyGo_T3_S3_V1_3 -> T3_S3_V13.bin, LilyGo_T_Connect_Pro -> t_connect_pro.bin),
  nRF52 = `<env>.uf2` + `<env>.zip`, support files bootloader(-s3).bin, partitions.bin, otadata.bin, safeboot(-s3).bin.
  Verified live (gh api GET): DK5EN/MeshCom-Firmware v4.40a.10.02 has 39 assets; icssw-org/MeshCom-Firmware v4.40a has 38
  with the same naming scheme; the only name missing upstream is `T-ETH-ELITE_1262.bin` (diffed from the two asset lists).
  Each asset object carries `digest: "sha256:<hex>"` and `size`, `browser_download_url`. App asset is a plain ESP image
  (the same bytes as flasher `firmware.bin`), safe to stream into ota_0.
- Web flasher manifest (gh-pages, generated by tools/pages_flasher.py:393-440): `flash/releases.json`
  `{"releases":[{"version","date","notes","boards":[{"env","group","name","radio","chipFamily"}]}]}` and per board
  `flash/<tag>/<env>/manifest.json` `{"name","version","new_install_prompt_erase","new_install_improv_wait_time",
"builds":[{"chipFamily","parts":[{"path","offset"}]}]}`. NO checksums, no min-version, no asset size in it, and only the
  latest release is kept (`--keep 1`). It is a flasher manifest, not a good update feed. Use the Releases API
  (`digest` field) or add a small `latest.json` to gh-pages.

### 4. App network stack and headroom

- App has NO HTTPClient / WiFiClientSecure / esp_crt_bundle / HTTPUpdate use (grep src: 0 hits). mbedtls only for
  SHA-256/HMAC (phone_commands.cpp:15,217; kiss_functions.cpp:27; net_console.cpp:32; external_radio_glue.cpp:35). NTP is raw
  UDP (src/ntp_async.h, udp_functions.cpp:1089-1180), no TLS anywhere; WiFiClient only for KISS/extudp/web server.
- SDK: main-app framework (installed ~/.platformio/packages/framework-arduinoespressif32, 3.20017 = Arduino 2.0.17)
  has `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1`, DEFAULT_FULL, 200 certs (tools/sdk/esp32/sdkconfig:1423-1428), and ships
  HTTPClient, HTTPUpdate, WiFiClientSecure libs. platformio.ini main env has no override of this (grep CERTIFICATE_BUNDLE = 0 hits).
  Not verified: which of the two installed framework dirs a given main env resolves to, and the real flash cost of the
  bundle (only linked if referenced).
- Flash headroom (tools/resource_baseline.json, dated 2026-09-11, stale per memory): ESP32 4 MB boards flash_total 3403776,
  used 1.43-1.79 MB (e.g. heltec_V3 1455697 -> 1.95 MB free; ttgo_tbeam 1623521 -> 1.78 MB free; E22_XML 1787337 -> 1.62 MB).
  t_deck/t_deck_plus 12582912 total, 10.4 MB free. => a TLS client (~150-250 kB, estimate) fits flash on every ESP32 board.
- RAM: classic ESP32 static DRAM 123420/124580 (E22_XML) and 113k/124580 elsewhere; IRAM 126-127.6k of 131072 (the IRAM pressure is
  not ours, memory note iram-is-not-ours). Runtime heap on a deployed Heltec V3: min 125580, steady ~129.5k (docs/soak-20261001-440a-verdict.md:46).
  A TLS handshake needs ~40-50 kB contiguous (2x16 kB record buffers) = fits on S3; classic ESP32 + NimBLE has no number in the
  tree, measure `[HEAP]` line (esp32_main.cpp:891-893 prints free/min/max-alloc) before promising it. NimBLE heap starvation is a known failure class.
- Verdict for the paper: TLS GET of the release metadata (check) can live in the app; the image download (TLS) must run in
  the context that writes the slot, i.e. Safeboot, unless the app does raw flash writes into ota_0 itself (not today, see 7).

### 5. Settings plumbing (template: TZ-01, 614f1b36 + bcf8f083; `git show --stat`)

- 614f1b36 (setting + data): src/meshcom_settings.h:198-199 (member in MESHCOM_SETTINGS_MEMBERS_COMMON, `M(type,name,default)` /
  `A(char,name,[n],{0})`), src/config_json.h:350 (schema row `X("node_tz", CFG_STR, node_tz, CFG_NORANGE, CFG_NOESC)`, this one row
  also gives NVS persistence via settings_schema walk, and nRF52 keyed store), seeding for fresh nodes in
  src/esp32/esp32_main.cpp:~924 (flash-clear branch, not init_flash), test/test_config_json (roundtrip), platformio.ini +
  test/golden/native/variant-ini-effective.json (only because a new native env was added), test/golden/settings_schema_lint.py.
- bcf8f083 (command + UI): command in src/command_functions.cpp (`--settz` ~755-775; help line ~1190 `== Node ==`;
  --info line :6278), BLE SN1 key in :6746-6751 (BLE frame size pins in test/test_ble_phone_harness), web setup field
  src/web_functions/web_functions.cpp:2848 (input element) + web_setup.cpp:173-195 (paramName handler that calls
  commandAction("--settz ...")), info row web_functions.cpp:3403. docs: docs/settings-registers.md, docs/d1-04-settings-field-triage-20260912.md.
- Bool alternative: bits in `node_sset`/`node_sset2`/`node_sset3`/`node_sset4` (meshcom_settings.h:93,103,122,169) ; `--wifiap on/off`
  handlers command_functions.cpp:3731/3758 are the closest template. Bit collisions have bitten before (memory sset4-bit-collision).
  A new keyed `bool`/`int` member avoids bit allocation; layout changes need no FLASH_STRUCT_VERSION bump (keyed stores, 614f1b36 message).
- Safeboot impact: settings_schema.cpp is compiled into both safeboot images (platformio.ini:1235-1238, settings_schema.cpp:45-47), so
  every new schema row costs safeboot bytes (5184 B headroom on the classic image). Rows are loaded in Safeboot for free.
- nRF52: BLE settings v1 is a frozen snapshot (src/nrf52/ble_settings_v1.h); a new setting does not need to touch it.

### 6. nRF52 (RAK4631, T114, T-Echo)

- Present: Bluefruit `BLEDfu ble_dfu; ble_dfu.begin()` src/nrf52/nrf52_ble.cpp:58-59,152-153 (phone-driven OTA via the Adafruit
  bootloader DFU service), `--dfu` = GPREGRET 0x57 reboot into the UF2 bootloader (command_functions.cpp:1016-1035,
  nrf52_main.cpp:2343-2370; USB drive, needs a host), serial DFU via adafruit-nrfutil (release `.zip`). All need a phone or a USB host.
- No in-app flash writer, no dual-bank staging, no network stack with TLS on nRF52 (RAK uses W5100S Ethernet), and the Adafruit bootloader
  cannot fetch from the network. Nothing lets the app apply a downloaded image autonomously. Conclusion: nRF52 out of scope, as decided.

### 7. Risks

1. Single slot: Update writes into ota_0 from the first chunk; a failed download leaves app invalid (contract "Single app slot").
   Safeboot then stays up with an open AP and the node is off the mesh until someone intervenes. For unattended auto update this is the core risk.
   Existing integrity: MD5 only if the client sends it (ElegantOTA.cpp:143-149), image verify only afterwards. No SHA-256 check, no
   board/chip check by Safeboot itself beyond esp_image_verify (image chip-id check is IDF behaviour, not verified here), so a wrong-env
   asset (names are env-specific, no env macro in the app) would be flashed if it is a valid ESP32-family image. webflash.py checks hardware client-side only.
   Mitigations to consider: pre-fetch + verify the sha256 `digest` against a streaming hash and only then start Update (two downloads, or hash during a
   first dry pass), retry loop with backoff while app_valid=false, never start unless AP/STA link is up and battery/solar state ok.
2. Safeboot fallback timer (180 s) and stall watchdog (30 s) are tuned for a human upload (ota_state.h:93-94); an in-Safeboot download needs
   its own session path into OtaSession (onStart/onChunk/onFinal) so /ota/state stays truthful.
3. GitHub redirects: `browser_download_url` and `.../releases/assets/<id>` answer 302 to a signed objects.githubusercontent.com style URL
   (current host name not verified); HTTPClient needs setFollowRedirects and the TLS cert chain of both hosts in the bundle. Pinning one root
   is cheaper than the full bundle but breaks on CA changes.
4. API size: `releases/latest` JSON for the fork is 65,583 B (39 assets, 7.4 kB body; measured via gh api GET today). Too big to parse into
   RAM on a small heap: stream-filter (look for `"tag_name"` and the one asset's `digest`/`browser_download_url`) or add a tiny
   `latest.json` on gh-pages (fixed shape, <1 kB, own cert chain github.io). Releases API is unauthenticated 60 req/h/IP (shared NAT of a
   hamnet/club site could exhaust it; check interval of hours, jitter).
5. Time for cert validation: Safeboot has no time source (no NTP, no RTC read; ESP32 RTC timer may survive esp_restart, unverified). The app has
   NTP over UDP every 15 min (docs/ntp-timing.md) and onboard RTC on some boards (RTC-1..3). Options: check in the app (time is good) and hand
   the epoch to Safeboot via RTC_NOINIT / NVS, or skip time check in Safeboot (setInsecure, weak), or run NTP in Safeboot.
6. Which repo is the source: fork DK5EN (39 assets) vs upstream icssw-org (38 assets, tags `v4.40a` w/o date). Fork build carries unreleased fixes; the node
   must not downgrade or sidegrade. Prereleases/drafts must be filtered (`prerelease`, `draft` flags).
7. Reachability without gateway flag: WiFi is off in the app unless a network feature is on (esp32_main.cpp:2019). Auto update independent of the gateway
   flag still requires an STA link; decide whether the setting itself turns WiFi on, and what with `--wifiap on` / EXTUDP nodes.
8. Bootloader/partition table/safeboot never update via this path (only the web flasher or USB). A release that changes layout or needs a new
   safeboot image must be excluded by a compat flag in the feed (cf. release skill Step 5b text).
9. Hardware mix: some nodes are battery/solar; reboot-into-Safeboot loses LoRa relay for the download time (several minutes on a poor link).

## Open questions for the operator

1. Source repo: DK5EN fork releases, icssw-org releases, or both with a setting? Prerelease handling? Allow downgrade?
2. Check in the app, download in Safeboot (needs Safeboot TLS = partition change, or app-side flash write), or both in the app (app writes ota_0
   itself while running; risky, needs a code path from RAM)? The 5184 B headroom decides; accepting a one-time web-flasher re-layout (new 704K -> larger safeboot)
   is a fleet-wide step for every node.
3. Brick policy: acceptable that a failed download leaves the node in Safeboot until manual action? Required automatic retry window?
4. Integrity: sha256 from the API `digest` only, or signed manifest (the fork has none)? Is TLS to github.com enough trust?
5. Cert strategy: full IDF bundle vs pinned roots; time source for validation (NTP in app, handoff to Safeboot?).
6. Policy knobs: opt-in flag default off? update window (night hours, UTC/TZ-01 aware), min interval, battery floor, skip when a phone is connected?
7. Asset mapping: OK to add a per-env compile macro (all 30 envs via platformio.ini `build_flags`) or a hardware-id -> asset table in the feed?
8. nRF52: confirm out of scope (no autonomous path found).
9. Scope of "gateway independent": does the setting force WiFi STA on in the app (changes RAM and radio behaviour on non-gateway nodes)?

## Suggested file-ownership list (for a writer wave; nothing edited here)

A. Settings + command (shared files, one owner):
src/meshcom_settings.h, src/config_json.h, src/command_functions.cpp (command, help, --info, SN1 if wanted),
src/web_functions/web_functions.cpp + web_setup.cpp + web_nodefunctioncalls.cpp (UI), docs/settings-registers.md,
docs/d1-04-settings-field-triage-20260912.md, test/golden/settings_schema_lint.py, test/test_config_json/test_config_json.cpp
(+ test/test_ble_phone_harness if BLE keys change).
B. App-side checker (new files, ESP32 only): src/esp32/fw_update_check.{h,cpp} (HTTPClient/WiFiClientSecure, version parse, cert bundle),
src/esp32/esp32_main.cpp (loop hook near the 15-min block ~2632 and WiFi gating near 2019), platformio.ini (new native test env + flags),
test/golden/native/variant-ini-effective.json (if platformio.ini env list changes), new test/test_fw_update_*/ (pure version compare + JSON filter,
host-testable like tz_rule).
C. Handover contract: src/command_functions.cpp (`--ota-update` path, optional "auto" flag, NVS/RTC key), docs/safeboot-ota-contract.md first.
D. Safeboot side (only if download runs there): src/safeboot/main.cpp, src/safeboot/ElegantOTA.cpp, src/safeboot/ota_state.h
(new auto-session path/timeouts), test/test_safeboot_state/test_safeboot_state.cpp, platformio.ini safeboot env sections
(:1200-1285, remove HTTPCLIENT_NOSECURE), partitions-4MB-safeboot.csv / partitions-16MB-safeboot.csv (only if the slot grows),
tools/safeboot.py (max_size), safeboot.bin + safeboot-s3.bin (tracked), docs for the web-flasher re-layout.
E. Release/feed tooling: .claude/commands/release-firmware.md (local, gitignored), tools/pages_flasher.py (+ tools/tests/test_pages_flasher.py) if a
latest.json is added to gh-pages, release-notes.md.
F. Bench: tools/bench/ota_regression.py, tools/bench/test_ota_regression.py, docs/bench-ota-regression.md, docs/test-suite-map.md, docs/BACKLOG.md.
Overlap to watch: platformio.ini appears under B and D; src/command_functions.cpp under A and C; src/esp32/esp32_main.cpp only B; settings_schema.cpp
is shared with Safeboot (any new schema row grows both safeboot images).
