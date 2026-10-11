# Upstream corpus: fork-dev against upstream/dev (2026-10-11)

Preparation for upstream PRs. No PR is opened from this document yet.

| Baseline   | Commit     | Note                                                        |
| ---------- | ---------- | ----------------------------------------------------------- |
| fork-dev   | `256b71ca` | analysed tree (pinned worktree)                             |
| upstream   | `90ac580d` | upstream/dev, #1204 (4.40a)                                 |
| merge-base | `e1e2acea` | use `git diff e1e2acea 256b71ca` for the fork's own changes |

Divergence: fork 157 commits ahead, upstream 23 ahead. A two-dot diff against upstream/dev shows
upstream's 23 newer commits (LED, charge LED, radiolib flag sort) reversed; read it with care.

Method: six read-only finder agents (commits, two source slices, conflicts, upstream bugs, build
measurements), one adversarial verifier on every "still present upstream" claim, 26 + 4 real builds.
Raw finder reports live outside the repo (session scratchpad); this document holds the verified
result.

## 1. Bug fixes that fix upstream issues

All verified against upstream `90ac580d` (file:line in the upstream tree).

| ID        | Defect                                                                                                                         | Upstream location                                               | Severity             | Standalone size                      | Fork test              |
| --------- | ------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------- | -------------------- | ------------------------------------ | ---------------------- |
| N-36      | Fresh ESP32 with SSID "none" never starts its softAP: SSID gate returns before the AP branch                                   | `udp_functions.cpp:625` (AP at :636/:649)                       | high (fresh nodes)   | ~25 lines (`wifi_start_gate.h`)      | `test_wifi_start_gate` |
| SNF-WLNK  | Store node keeps and hourly replays DMs to/from WLNK-1 / APRS2SOTA; WLNK-1 bare `ack<digits>` not recognised (Winlink lockout) | `lora_functions.cpp:1836-1858`                                  | wrong on-air         | ~15 lines                            | `test_msgstore_hook`   |
| CMD-BMX   | `--bme/--bmp/--bmx off` masks `node_sset3 & 0x7FEF`, clearing NOPMOTHER (0x8000) and saving it                                 | `command_functions.cpp:2143`                                    | silent setting loss  | 1 line                               | `test_command_toggles` |
| RTC-1     | ESP32 writes node-local time into the UTC RTC and adds the offset again on read                                                | `esp32_main.cpp:3069, 3077, 3151`                               | wrong clock (RTC)    | ~60 lines + `rtc_offset.h`           | `test_rtc_offset`      |
| RTC-2     | nRF52 applies `node_utcoff` twice                                                                                              | `nrf52_main.cpp:1257, 1271`                                     | wrong clock (RTC)    | rides with RTC-1                     | `test_rtc_offset`      |
| RTC-3     | Refresh gate `< 60000` inverted: RTC written every loop pass, or never until millis wraps                                      | `esp32_main.cpp:3066, 3148`                                     | I2C load / stale RTC | 4 lines                              | `test_rtc_offset`      |
| TX-ONCE   | `{CET}`/`{MCP}`/`{SET}` queued with status 0xFF, scored as relay, collide with neighbour relays                                | `loop_functions.cpp:4366`, `txring_functions.cpp:130`           | on-air timing, low   | ~10 lines                            | `test_txring`          |
| EXT-01    | Bare `--extudpip` reads past the terminator (`msg_text+11` for a 10-char command)                                              | `command_functions.cpp:2616`                                    | wrong config         | 1 line                               | none (bench)           |
| EXT-03    | EXTUDP target may be broadcast/multicast (own IP is already refused upstream)                                                  | `command_functions.cpp:2611-2624`                               | LAN flooding         | ~140 lines (`extudp_target.h`)       | `test_extudp_target`   |
| POS-RANGE | `--setlat/--setlon` and BLE 0x70/0x80 accept \|lat\|>90, \|lon\|>180, NaN                                                      | `command_functions.cpp:3888-3925`, `phone_commands.cpp:470-505` | wrong data           | ~60 lines (`node_position.h`)        | `test_node_position`   |
| N-38      | BLE onConnect does not reset `isPhoneReady`/`bAckInfo`; a fast reconnect inherits the old session                              | `esp32_main.cpp:347-372, 3296-3308`                             | stale session (race) | ~10 lines minimal                    | `test_ble_session`     |
| T5-01     | `j`/`len2` uninitialised in `line_full_format`; `if(e->code = LV_EVENT_CLICKED)` assignment                                    | `t5-epaper/ui.cpp:69-77, 1218`                                  | UB on T5             | ~40 lines                            | build only             |
| TD-HEMI   | T-Deck map: `tdeck_add_pos_point` tests lat for 'W' and lon for 'S', so southern/western stations are never negated            | `t-deck/lv_obj_functions.cpp:3801-3806`                         | wrong map position   | 4 lines                              | none                   |
| SB-REASON | Safeboot reports every `Update.end()` failure as MD5 mismatch                                                                  | `safeboot/ElegantOTA.cpp:381-389`                               | diagnostic           | ~40 lines + regenerate safeboot bins | `test_safeboot_state`  |
| RF-09     | Poland `track_freq` 434.855f in the nRF52 Hz column                                                                            | `country_profile.cpp:104`                                       | cosmetic (`--info`)  | ~30 lines                            | `test_country_twin`    |
| WEB-04    | Web delivery ticks read from the 20-slot own-message ring, vanish after ~20 own frames                                         | `loop_functions.cpp:465`, `web_functions.cpp:3150-3154`         | UI                   | not standalone (~110 new lines)      | `test_own_msg_status`  |

Not upstream bugs (verified): upstream t_echo and heltec_t114 link fine at `-Ofast` (measured 90.4 % and
86.8 % flash); the reported overflow was the fork's own RM growth. The second hemisphere site `tdeck_add_to_pos_view` prints the value
next to its letter and is correct (dead code at most); #1200 and #1201 (temperature offset) are
merged upstream. Field effect still needs on-air proof for SNF-WLNK (lockout) and N-38 (race).

Cherry-pick caution: 75 of the 86 firmware commits also touch docs or tools. Every upstream PR
takes the firmware hunks by path (`git show <sha> -- src variants platformio.ini`) plus its test.
Commit `cd7a8bac` (labelled docs) rewrote comments in six upstream source files to point at
fork-only `docs/archive/` paths; those comment edits must not travel upstream.

## 2. What upstream is missing (fork features)

| Family                               | Essence                                                                                                    | Size                | Boards       |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------- | ------------------- | ------------ |
| Remote management RM1 (#1189)        | HMAC-signed DM to manage another node over LoRa or server; read/write command set; Remote setup page       | 25 new files, +8.3k | all          |
| Auto-update (#1187) + Safeboot apply | GitHub release check over TLS, compressed asset staged in ota_0 tail, Safeboot inflates offline            | ~+4.2k              | ESP32        |
| Web request guard                    | Host allowlist (DNS rebinding) and X-MC/Origin check; now behind `MC_WEB_GUARD`, default 0 (see section 5) | 620 lines header    | web boards   |
| TZ-01 time zones                     | POSIX TZ rule engine with DST, `--settz`, SN1 "TZ"                                                         | ~650 (with RTC)     | all          |
| Store-and-forward gateway + STOR     | store node also takes PMs from the server path; STOR announce datagram (off by default)                    | ~1100               | all          |
| WEB-04 own-message status            | durable delivery status table, single write path, T-Deck ACK glyph                                         | ~250                | all          |
| NMTU-01 network MTU                  | `--mtu` on all boards and all interfaces (upstream: RAK W5100S only)                                       | ~170                | all          |
| BLE BLC-01..03                       | session reset in callbacks, advertising self-check, counters                                               | ~330                | ESP32, nRF52 |
| HEY echo upload                      | gateway uploads own HEY echoes as received                                                                 | ~75                 | gateways     |
| EXTUDP target safety                 | refuse broadcast/multicast targets                                                                         | ~140                | all          |
| GW-LINK "Server:" line               | gateway link status line                                                                                   | ~155                | gateways     |
| T-Deck/T5 UI                         | position persistence, PSRAM tile cache, warning cleanup                                                    | ~400                | T-Deck, T5   |
| Build/test                           | 66 native test envs, `-Werror` on 5 envs, `-Os` on t_echo/t114, `tools/mc_build_defines.py`                | ~1650 ini lines     | build        |

## 3. Conflicts on integration

`git merge-tree --write-tree upstream/dev 256b71ca` conflicts in exactly five files (verified twice):

| File                                  | Cause                                                                        | Resolution                                                        |
| ------------------------------------- | ---------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| `.gitignore`                          | both append at EOF (#1204 CMake/vcxproj vs fork bench/gwbench rules)         | keep both blocks                                                  |
| `src/command_functions.cpp` ~l.367    | upstream `BOARD_LED_ACTIVE_LOW` (481fcf79) vs fork `bRemoteLed` early return | keep fork guard, use the macro for the level                      |
| `src/esp32/esp32_main.cpp` ~l.2689    | upstream LED flashing #1202 vs fork `REMOTE_LED_PIN` yield                   | wrap upstream logic in the RM yield; build ttgo_tbeam + heltec_v3 |
| `src/loop_functions_extern.h`         | `extern bool bCHARGE_LED` vs fork RM LED block                               | keep both                                                         |
| `src/web_functions/web_functions.cpp` | upstream battery row with "Charge Indicator LED" vs fork AU-09 status table  | take upstream battery block, keep AU block after it               |

All other files auto-merge (`platformio.ini`, `loop_functions.cpp`, `nrf52_main.cpp`, `web_setup.cpp`).

## 4. What fork-dev needs to backport

| Upstream change                               | Needed                                    | Conflict |
| --------------------------------------------- | ----------------------------------------- | -------- |
| fe74ebf3 radiolib flag dedupe/sort (#1193)    | yes                                       | no       |
| 481fcf79 `BOARD_LED_ACTIVE_LOW` (#1195)       | yes                                       | yes      |
| #1196 charge indicator LED (6 commits)        | yes                                       | yes      |
| #1202 board LED flashing (b4a0e17e, 01a42700) | yes                                       | yes      |
| #1204 `.gitignore`                            | yes                                       | yes      |
| #1200, #1201 temperature offset               | no (already in fork)                      | no       |
| b82e36c0 `workflow_dispatch`                  | no (reverted by 0eb33bfa, net diff empty) | no       |

The fork's own `meshcom-ci.yml` carries `if: false` on the build job (line 27), so a tag on a
fork-dev commit never builds or uploads, even with the workflow enabled.

## 5. Consequences for upstream (compatibility)

- **Web request guard:** as written it 403s scripts, curl and mcapp clients without `X-MC: 1`.
  Now gated: `MC_WEB_GUARD` defaults to 0, upstream endpoints behave as upstream (no check,
  CORS `*`). Still guarded with the flag off: every request the router sends to an RM handler
  (`GET/POST /rm*`, the remote page, also via Referer), `/setparam/?rm=` and `?rmstrict=`, and
  the `/?nodepassword` rewrite when it would smuggle an RM route. `-D MC_WEB_GUARD=1` restores
  the full guard. Branch `web-guard-flag`, two advisor rounds, host test `native_web_guard`.
- **MTU default 1500 → 1280:** nodes that saved settings keep their stored value; fresh or wiped
  nodes get 1280, and the fork applies it to every interface on every board (upstream: RAK W5100S
  MSS only). Either keep 1500 as the upstream default or state it in the PR.
- **New settings:** `node_tz`, `node_stor`, `node_rm`, `node_rmstrict`, `node_autoupd`,
  `node_updchan`; `FLASH_VERSION` 20260929 → 20261006; fresh ESP32 gets a CET/CEST rule.
- **New NVS:** `rm_hwm`, `rm_snd` (Counters), `RmNodes/nodes`, `fwstage/{rec,last,hand,tries}`.
- **On air:** RM1 DM payloads without `{NNN` (no ACK/retry); a node without RM shows plain text.
  STOR datagram off by default.
- **`--passwd`** now rejects invalid values; clearing it turns RM off.
- **nRF52 flash (measured):** upstream at `-Ofast` is 90.4 % (t_echo) and 86.8 % (t114). The fork
  with RM overflows at `-Ofast`, so it switched both to `-Os`: 77.6 % and 74.5 %. An RM PR must
  carry that build-flag change, which trades speed for size on those two boards.
- **Safeboot:** every node needs a one-time new Safeboot (web flasher/USB) before auto-update
  works; the app detects an old Safeboot (`hand`/`nocap`) and stays locked.

## 6. Auto-update: measured facts and decisions

Measured on 11 ESP32 envs, fork `256b71ca` and upstream `90ac580d`, all builds green. All 11 use
`partitions-4MB-safeboot.csv` (`ota_0` = 3,403,776 B), including 8 MB and 16 MB boards; only
T-Deck/T-Deck Plus use the 16 MB table.

| Env                    | Flash | Upstream bin | Fork bin  | Fork zlib | Plain stages? | zlib stages? |
| ---------------------- | ----- | ------------ | --------- | --------- | ------------- | ------------ |
| ttgo_tbeam_SX1262      | 4 MB  | 1,666,592    | 1,951,856 | 1,223,097 | no            | yes          |
| ttgo_tbeam             | 4 MB  | 1,658,448    | 1,943,872 | 1,217,795 | no            | yes          |
| E22_XML-DevKitC        | 4 MB  | 1,658,208    | 1,944,336 | 1,220,842 | no            | yes          |
| E22_1262-DevKitC       | 4 MB  | 1,638,224    | 1,924,048 | 1,209,886 | no            | yes          |
| E22-DevKitC            | 4 MB  | 1,638,112    | 1,923,904 | 1,209,789 | no            | yes          |
| esp32-loraprs-e22      | 4 MB  | 1,637,856    | 1,923,664 | 1,209,681 | no            | yes          |
| heltec_wifi_lora_32_V2 | 8 MB  | 1,629,264    | 1,915,104 | 1,203,179 | no            | yes          |
| ttgo-lora32-v21        | 4 MB  | 1,626,896    | 1,912,768 | 1,201,868 | no            | yes          |
| LilyGo_T-Beam-1W       | 16 MB | 1,559,904    | 1,846,800 | 1,166,881 | no            | yes          |
| LilyGo_T3_S3_V1_3      | 4 MB  | 1,555,792    | 1,842,800 | 1,164,540 | no            | yes          |
| heltec_wifi_lora_32_V3 | 8 MB  | 1,537,968    | 1,824,976 | 1,154,200 | no            | yes          |

- zlib ratio 0.626-0.632; every `.zz` starts `78 da` and inflates byte-identical.
- Plain staging limit (real `fwStageLayout()`): 1,699,840 B; one byte more is `FW_REF_OVERLAP`.
- AU code cost on Heltec V3: ~60 KB flash (22 KB AU objects + ~40 KB newly linked HTTPClient,
  WiFiClientSecure, cert bundle, mbedtls error strings). Largest upstream image + AU (~1.73 MB)
  is already over the plain limit, so upstream needs the compressed asset too.
- Compressed staging headroom on the largest fork image: 5-6 %. Watch it.
- Fork image is 285-287 KB larger than upstream on every env; AU is ~21 % of that.

| Safeboot bin             | Fork                  | Upstream              | Slot      |
| ------------------------ | --------------------- | --------------------- | --------- |
| `safeboot.bin` (classic) | 711,664 (9,232 free)  | 715,712 (5,184 free)  | 720,896 B |
| `safeboot-s3.bin` (S3)   | 672,624 (48,272 free) | 676,800 (44,096 free) | 720,896 B |

Options evaluated:

| Option                                  | Verdict                                                                                    |
| --------------------------------------- | ------------------------------------------------------------------------------------------ |
| Compress only HTML/CSS/JS in the image  | rejected: web literals 116 KB → 37 KB saves ~79 KB, gap is 125-251 KB and grows            |
| Safeboot downloads plain `.bin` itself  | rejected: no room for TLS (classic Safeboot 9 KB free, built with `HTTPCLIENT_NOSECURE`)   |
| GitHub compresses on the fly            | impossible: release assets come from Azure Blob, no `Content-Encoding`, full bytes on wire |
| Plain `.bin` fallback when small enough | rejected: upstream + AU already over 1,699,840 B on classic boards                         |
| Repartition                             | rejected: no space gain on 4 MB chips; field rewrite of `0x8000` bricks on power loss      |
| Stage above 4 MB on 8/16 MB boards      | feasible (~60-100 lines + record version), optional; Heltec V2 is classic (9 KB Safeboot)  |
| **Compressed asset published by CI**    | **chosen**                                                                                 |

Decisions:

- Asset name `<base>.autoupdate` (no `.bin`, so nobody flashes it). Longest name
  `E22_1262_S3-DevKitC-1-N16R8.autoupdate` = 38 chars + NUL fits the 40-byte buffers
  (`fw_update_net.cpp:464`, `fw_update_assets.h:91`) with one byte spare; raise both to 48 and add
  a host test over all env names. No release carries `.zz` yet, so the rename costs nothing.
- Safeboot formats (verified): fork Safeboot web upload sniffs the first byte (0x78 zlib, else
  raw, `ElegantOTA.cpp:427-434`); the offline staged apply is inflate-only; upstream Safeboot is
  raw-only.
- Upstream CI change (its own small PR): one step after the rename block running
  `tools/make_autoupdate.py` (stdlib only) with `continue-on-error: true`, reading the original
  `firmware.bin` files (the rename `mv` lines run backgrounded with `&`), plus one artifacts glob
  `.pio/build/*/*.autoupdate`. Upstream recently reverted a YAML change (b82e36c0 / 0eb33bfa), so
  it gets its own PR with a German rationale and a line in their `release.md`.
- Proof before the PR: run upstream/dev + the CI commit on a throwaway test branch and tag on the
  fork (risk accepted). The fork's `meshcom-ci.yml` keeps `if: false`, so our releases stay safe.

## 7. Open items

- Rename wave `.zz` → `.autoupdate` (src, tests, `tools/make_zz.py`, `/release-firmware`).
- CI proof run, then the CI PR.
- `web-guard-flag` branch: committed; merge into fork-dev after the RM regression
  session finishes.
- Committed Safeboot images are not byte-reproducible (711,600 vs 711,664 B on rebuild).
- No PR series yet: preparation mode.
