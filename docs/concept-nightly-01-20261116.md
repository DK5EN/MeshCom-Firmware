# Concept: nightly fork releases with opt-in automatic install

**CI builds `fork-nightly` every night at 00:05 as today's `v4.40a.MM.DD`, and opted-in ESP32 nodes on the dev channel install it at their own minute from 03:00 to 04:00. A 44-byte NVS record shows what happened and what the node had to do; a crash-loop guard parks a failing node in Safeboot.**

Recommendation: visible and recoverable before automatic. Order: pure logic and CI (W1, W2 in parallel), recording, guard and Safeboot v4, displays, the automatic install last (W7). Then a fault drill and 14 green nights on DK5EN-1 and DK5EN-92 before 2026-12-18. Backlog item `NIGHTLY-01`.

Status 2026-11-16: concept, no code changed. The seven operator decisions below are fixed. The open questions at the end are new decisions this concept needs.

## Operator decisions 2026-11-16

1. **New stable branch `fork-nightly`.** It moves only with the operator's explicit OK, as a fast-forward to a green `fork-dev` state. CI still builds and publishes every night at 00:05, also when nothing changed, so the update procedure runs every night.
2. **Published exactly as today:** tag `v4.40a.MM.DD`, normal release, Latest, in DK5EN/MeshCom-Firmware. The dev channel reads it unchanged. Upstream tags do not exist on the fork remote, so nothing clashes. One release per day: the tag parser rejects a same-day `.N`.
3. **Automatic install is opt-in per node**, default off, dev channel only. Today the automatic install is compiled out and an `#error` forbids it (`src/fw_update.h:30-36`). That lock is lifted only for this opt-in path.
4. **Release notes:** CI writes a short note every night ("nightly of fork-nightly `<sha>`, same firmware as `<tag>`") with the Important box and a link to the last full notes. Full notes come from `/release-firmware` only when a promotion is OK'd.
5. **Crash-loop guard:** after an automatic install, 3 boots without reaching "healthy" make the node stay in Safeboot (reachable over WLAN for a manual reinstall) and switch its own automatic install off.
6. **Timing:** each node gets a fixed minute between 03:00 and 04:00 local time, derived from its callsign.
7. **Feedback:** a GitHub issue template "Nightly report" (node, board, nightly tag, what broke, `--info`), linked from the nightly notes.

ESP32 only: nRF52 has no auto update (`src/rm_au_none.cpp`). The nightly release still carries the nRF52 assets for manual flashing.

## Operator answers to the concept questions (2026-11-16, second interview)

These override the recommendations further down where they differ.

1. **Healthy does not require WLAN.** A missing WLAN is recorded only, so a router that reboots at night cannot park a good node.
2. **No Safeboot v4 requirement.** Opt-in works on Safeboot v3 with the app-side fallback (P2). Wave W5 (Safeboot v4) is dropped. Add a cap on the park cycle so flash-state writes stop after a bounded number of cycles.
3. **Pre-download reboot only as the last resort, on every board type.** First the existing mitigations (drop BLE and the rest); only if they do not free enough RAM, reboot and try again. No board list.
4. **Opt-in EXTUDP outcome line: yes.** One line to the configured EXTUDP address after each attempt, only when EXTUDP is on.
5. **Keep every nightly for now.** The daily build of an unchanged `fork-nightly` is a test for some weeks. After that, CI builds only when `fork-nightly` has moved.
6. **Releases only via `fork-nightly`.** No more hand-cut `v*` tags from `fork-dev`.
7. **The operator merges into `fork-nightly` himself.** No promotion gate run by the orchestrator; it only reports which `fork-dev` state passed its gate.
8. **Upstream wave 6 decision is parked.** The nightly path is tested on the fork dev channel first; the operator wants to gain confidence before deciding what goes upstream.

## Key facts

### Install path today

- **The automatic install cannot be compiled:** `AU_AUTO_INSTALL` is forced to 0 and any other value is an `#error` (`src/fw_update.h:30-36`). A stored mode 2 (auto) acts as notify (`fwEffectiveMode()`, `:488-491`).
- **The fences are pinned by tests:** `tools/tests/test_au_fences.py:13-17` asserts the `#error`, `:38` and `:60` assert that the automatic handover and the automatic download start sit inside `#if AU_AUTO_INSTALL`.
- **The automatic branches already exist, fenced:** download start `src/esp32/esp32_main.cpp:3318-3341`, handover `:3253-3281`, both inside `#if AU_AUTO_INSTALL`. The handover sequence itself is `auHandoverRecord()` (`:3040-3069`), shared with the remote go.
- **The policy window:** a handover is allowed only between 03:00 and 05:00 local, without a phone, on mains or with the battery at 3600 mV or more (`src/fw_update.h:459-461`, `fwTick()` `:583`).
- **Skip reasons for an automatic download, in order:** blocked, clock, phone, battery, backoff, attempts, staged (`fwAutoSkipReason()`, `src/fw_update.h:495-513`). The node prints `[AU];auto;skip;<reason>` once per check (`src/esp32/esp32_main.cpp:3336`).
- **No TX gate exists:** none of the skip reasons looks at the TX ring. A handover can reboot the node with frames still queued.
- **Download retries:** at most 3 attempts per tag, 6 h apart (`src/fw_update.h:462-463`). A lowheap refusal while a phone held BLE uses no attempt (`fwErrConsumesAttempt()`, `:880-884`) and waits 10 min instead (`src/esp32/esp32_main.cpp:3175`).
- **Reinstall guard:** a candidate must be newer than the running image and differ from the last installed tag (`fwShouldInstall()`, `src/fw_update.h:414-428`). A new date tag every night always passes this.
- **Year wrap is safe:** `fwCompare()` takes a date as newer when it lies 1 to 182 days ahead modulo the year (`src/fw_update.h:175-200`), so `01.01` follows `12.31`.
- **The schedule module never installs:** `src/fw_schedule.h:4-6`. It derives the release instant from the tag date at 12:00 UTC (`:21-25`, `:38`), so delay 0 plans a tag of the 17th for the night of the 18th.

### Mitigations the node performs today

These are the real "steps taken" candidates. Each has a log line already.

| Mitigation                                                               | Log line                                         | Where                                                     |
| ------------------------------------------------------------------------ | ------------------------------------------------ | --------------------------------------------------------- |
| BLE stack paused for the TLS job (classic ESP32, no phone linked)        | `[AU];shed;ble`                                  | `src/esp32/fw_update_net.cpp:1503-1513`                   |
| BLE forced off although a phone is linked (BLE guard, after 30 min wait) | `[AU];shed;guard`                                | `fw_update_net.cpp:1497-1501`, `src/fw_update.h:464`      |
| TFT sprite freed (25.6 KB, Wireless Tracker class)                       | `[AU];shed;tft`                                  | `fw_update_net.cpp:1516-1520`, `src/fw_update.h:1058`     |
| Closed web sockets purged before the gate is read again                  | `[AU];shed;tw;<n>;free;<before>;<after>`         | `fw_update_net.cpp:768-790`                               |
| KISS TCP stopped, then the net console session if that closes the gap    | `[AU];shed;services;console;<0/1>;kiss;<0/1>;..` | `fw_update_net.cpp:738-753`, `:791-812`                   |
| Job restarted on a fresh stack when the heap was split                   | `[AU];shed;restart`                              | `fw_update_net.cpp:1532-1545`, `src/fw_update.h:1078`     |
| Services restored after the job                                          | `[AU];shed;restore`                              | `fw_update_net.cpp:756-764`                               |
| 10 min wait after a lowheap refusal with a phone on BLE                  | `[AU];fail;lowheap_phone`                        | `fw_update_net.cpp:1555-1561`, `esp32_main.cpp:3171-3176` |
| Download retried (attempt 2 or 3 of a tag)                               | `[AU];fail;<token>` before it                    | `src/fw_update.h:821`, `fwAttemptFailed()`                |
| Waited for a phone, the battery or the clock                             | `[AU];auto;skip;<phone/battery/clock>`           | `src/esp32/esp32_main.cpp:3336`                           |
| BLE resume retried (3 times in the job, then by the loop)                | none (heap mark `resumed`)                       | `fw_update_net.cpp:1566-1576`, `esp32_main.cpp:519`       |
| Safeboot apply retried after a power loss mid-apply (at most 3 tries)    | `[SAFEBOOT];apply;retry;restart;try;<n>`         | `src/safeboot/main.cpp:700-710`, `fw_apply.h:34`          |
| TLS buffers in PSRAM (S3 with PSRAM; a build property, not a step)       | `[AU];tlsalloc;psram`                            | `fw_update_net.cpp:316`                                   |

Not done today, although the operator listed them as examples: a reboot before the download, and a pause of LoRa or of the gateway. This concept adds the reboot (see AU-24 and AU-25 below), not the pause.

### Heap

- **TLS peak on classic ESP32:** the P-384 chain verify needs about 69 KB of total heap, so the gate wants 76000 B free (`src/esp32/fw_update_net.cpp:122-133`). S3 runs with 119 KB free and has no total-free gate.
- **T-Beam free heap in use:** about 53 to 61 KB after 150 web requests, gateway off (`docs/BACKLOG.md:259`, TBEAM-HEAP-01). That is below the TLS gate: the BLE pause is what makes a download possible at all.
- **BLE pause only on classic:** `AU_BLE_PAUSE` is 1 only for `CONFIG_IDF_TARGET_ESP32` (`src/esp32/fw_update_net.h:240-246`). Heltec V3 (DK5EN-1) is an S3 without PSRAM and pauses nothing.

### Safeboot and the stage record

- **Partition layout:** Safeboot is the factory app, the firmware the single `ota_0` slot (`partitions-4MB-safeboot.csv`). AU rewrites only `ota_0`; a new Safeboot reaches a node only by USB (web flasher).
- **Stage record:** FWS2, 112 bytes in NVS `fwstage`/`rec` with tag, env, offsets, CRC32 and SHA-256 (`src/fw_update.h:299-314`). Other keys there: `last` (reinstall guard), `tries` (Safeboot apply counter), `hand`, `nocap` (`src/esp32/fw_update_net.cpp:400-411`).
- **Reboot into Safeboot:** `auRebootToSafeboot()` sets the factory partition as boot partition and restarts (`src/command_functions.cpp:269-296`). It refuses on a DIO bootloader (AU-16).
- **Safeboot boot:** clears `hand` and `nocap`, applies a staged record offline, then sets `ota_0` and restarts (`src/safeboot/main.cpp:769-794`, `:643-729`). Safeboot v3 is current (`src/safeboot/safeboot_ver.h:26`).
- **Safeboot leaves by itself:** an idle Safeboot reboots into the app after 180 s if `ota_0` is valid (`src/safeboot/ota_state.h:101`, log `[SAFEBOOT];fallback;reason;timeout`, `main.cpp:1132`). Today it cannot "stay".
- **Boot diagnostics in the app:** reset reason and the loop breadcrumb of the previous boot are printed and kept for `--info` (`src/esp32/esp32_main.cpp:1416-1442`). Nothing counts boots.

### Visibility today

- **Web:** `GET /austatus` returns `st, job, run, avail, newer, inst, staged, err, pct, phone, guard, sbok, mode, chan, etaS, ifree, iblk, sbwhy` (`src/web_functions/au_ui_state.h:232-257`), extended by `up` and `img` (`src/fw_update.h:899`, `:935`).
- **Console:** `--update status` prints one `[AU];status;...` line (`src/command_functions.cpp:5906`).
- **RM:** `upd` answers `m= c= n= a= st= b= g= ck= e=` (`src/rm_exec_ext.h:54-100`), about 75 of 105 body characters (`src/rm_format.h:107`).
- **Displays:** OLED and T-Deck show nothing of AU except the "OTA using WiFi" screen before the reboot into Safeboot (`src/command_functions.cpp:278-281`).
- **No outcome survives a reboot:** after an install the node knows its new tag, but not when, how long, or what it had to shed.

### CI today

- **Trigger:** `MeshCom CI` fires only on pushed tags `v*` and `ci-proof-*` (`.github/workflows/meshcom-ci.yml:24-28`). Tag name feeds `MC_BUILD_TAG` (`:80`), Latest (`:165`) and the verify step (`:232`, 68 assets).
- **Duration:** about 54 min per run (runs of v4.40a.11.08 and v4.40a.11.06). A 00:05 start publishes around 01:00.
- **Default branch of the fork is `fork-dev`;** issues are enabled. The issue templates live in `.github/ISSUE_TEMPLATE/` (Markdown templates today, no issue forms).

## Design overview

| Step | Time (local)        | What                                                                                       |
| ---- | ------------------- | ------------------------------------------------------------------------------------------ |
| 1    | 00:05               | CI builds `fork-nightly`, publishes `v<VER>.MM.DD` as Latest at about 01:00                |
| 2    | slot minus 50 min   | Node with uptime above 3 h reboots once, so the download runs on a fresh heap (new)        |
| 3    | slot minus 45 min   | Nightly check of the dev channel; a newer installable release starts the download at once  |
| 4    | slot (03:00..03:59) | All gates pass: handover, Safeboot applies, node boots the new image                       |
| 5    | slot plus 5 min     | New image reaches "healthy": outcome `ok` written, probation ends                          |
| 6    | any time            | Outcome visible on web, console, RM, OLED, T-Deck; "Report on GitHub" link on the web page |

Slot: `minute = FNV-1a-32(uppercase callsign with SSID) mod 60`, at 03:00 local plus that minute. Local time is the TZ rule time the node already uses (`src/esp32/esp32_main.cpp:3150-3154`). Both DST changes in Europe leave 03:00 to 04:00 intact.

The nightly path does not use the delay and weekday plan of `fw_schedule.h`: its noon assumption would push every nightly one night later. The countdown notice shows "tonight 03:27" instead.

The periodic check (22 to 26 h, `fwCheckDone()`, `src/fw_update.h:792-799`) stays as it is for notify mode. Dev-channel nodes in notify mode will now see a newer release every day; that is expected.

## (A) Telemetry and issue reporting

### Options

| Option                                                                   | Node RAM (classic / S3)                    | What leaves the node, to whom                      | Cost                                              | Verdict                       |
| ------------------------------------------------------------------------ | ------------------------------------------ | -------------------------------------------------- | ------------------------------------------------- | ----------------------------- |
| A1. Outcome record in NVS, read by `--info`, web, RM                     | 44 B NVS, under 100 B RAM, read on demand  | Nothing by itself                                  | Small; base for all other options                 | **Recommended, base**         |
| A2. Web page builds a pre-filled GitHub issue link, the browser opens it | 0 B (JS in flash, about 1.5 KB)            | Whitelisted fields, by the user, on GitHub         | Only users with a GitHub account; public issue    | **Recommended**               |
| A3. RM pull: new read command for the last outcome                       | 0 B extra (existing reply buffer)          | Over LoRa or server, to the RM operator            | Own nodes only (needs the RM key)                 | **Recommended for the fleet** |
| A4. One JSON line over EXTUDP to the user's own LAN collector            | about 256 B stack, only while sending      | Same fields, to the user's LAN target              | Only nodes with EXTUDP configured                 | Optional, operator question   |
| A5. One DM per night to a collector callsign                             | about 100 B transient                      | Same fields, on air and on the server, unencrypted | One frame per node per night; third party sees it | Defer                         |
| A6. Status frame on the MeshCom server UDP path                          | about 100 B                                | To the upstream server                             | New frame type, needs the upstream server team    | Reject                        |
| A7. HTTPS POST from the node                                             | TLS peak about 69 KB total: not on classic | To an endpoint someone must run                    | Endpoint, host name, certificate roots, privacy   | Reject                        |

Why A1 plus A2 plus A3: they cost the small nodes almost nothing, and the user stays in control. A3 covers the operator's own bench nodes without any push. A2 turns a failure into a GitHub issue with one click and one review.

Why not A7: a classic ESP32 has 53 to 61 KB free in use (`docs/BACKLOG.md:259`) and TLS needs about 69 KB (`src/esp32/fw_update_net.cpp:122-130`). A second TLS session per night doubles the exposure to AU-24 and AU-25 for no gain.

### The GitHub link (A2)

- `mcau.js` fetches `/austatus` (with the new `last` object) and builds `https://github.com/DK5EN/MeshCom-Firmware/issues/new?template=nightly_report.yml&title=...&<field>=...`.
- Issue forms accept field values as URL parameters. The link stays under 2 KB; the node sends nothing itself.
- The page shows a preview of the exact text before the user opens the link. GitHub then shows the form again for editing.
- The `--info` field stays empty in the link. The form asks the user to paste `--info` and to remove addresses and network names first.

### Privacy

| May leave the node (opt-in, user action)                              | Never leaves the node                                         |
| --------------------------------------------------------------------- | ------------------------------------------------------------- |
| Callsign with SSID, board env name                                    | WLAN name, passwords, RM keys                                 |
| Tag before and after, result and phase word, error token              | IP addresses, gateway and server host names                   |
| Time of the attempt (UTC), durations, steps bitmask and names, counts | Position, message contents, heard-node lists                  |
| Heap minima during the job, reset reason of a failed boot             | Anything from `--info` that the user did not paste themselves |

Docs, notes and the issue form never name a server host. Callsigns are public by nature in amateur radio; the issue is public too, and the form says so.

## (B) Visibility of the last nightly

What every surface shows, from the outcome record: last attempt time (local), tag before and after, result, download time and total time, steps taken, nights ok and failed since opt-in.

| Surface                | Shape                                                                                                                                                           | Change                                                                                                                          |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| Web, Info page AU card | Line "Last nightly: Tue 17 Nov 03:27, 11.16 to 11.17, installed, download 3 min 4 s, total 6 min 52 s". Second line: steps in words. Button "Report on GitHub". | `/austatus` gets a `last` object appended after the closing brace, like `up` and `img`, so the goldens of `auStatusJson()` stay |
| Console                | `--update last` prints `[AU];last;res;ok;from;..;to;..;at;..;dl_s;..;tot_s;..;steps;0x..;names;ble,kiss;ok;12;fail;1`; `--info` gets one line                   | `src/command_functions.cpp`                                                                                                     |
| RM                     | New read command `upl`, reply `r=ok f=11.16 t=11.17 at=11170327 dl=184 tot=412 s=0013 n=1/0/0/1 c=12/1` (about 75 characters)                                   | Separate from `upd`, which has only about 30 characters left                                                                    |
| OLED                   | After an automatic install, the first 8 s of the boot show "Nightly ok 11.17" or "Nightly FAIL, back on 11.16"                                                  | Through the existing `startDisplay()` text screen                                                                               |
| T-Deck                 | One line under the version label in the info panel                                                                                                              | `src/t-deck/lv_obj_functions.cpp:1070`                                                                                          |
| Safeboot (v4)          | Parked banner: "Nightly 11.17 failed 3 boots. Upload a firmware, or release and boot the app."                                                                  | Safeboot page `src/safeboot/ota.html`                                                                                           |

Result words: `ok`, `current` (newest already running), `no_nightly` (Latest is not from today: CI missing or late), `skipped:<reason>`, `dl_fail:<token>`, `apply_fail`, `parked`, `running` (the node died mid-way; seen at the next boot).

## (C) Steps taken

### The record

One NVS blob `fwstage`/`out`, 44 bytes, fixed little-endian layout with encode and decode like the stage record (`src/fw_update.h:299-398`). The app writes it at each phase change, at most about six writes per night.

| Offset | Size | Field        | Meaning                                                                                                                      |
| ------ | ---- | ------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| 0      | 4    | `magic`      | `FWN1`                                                                                                                       |
| 4      | 1    | `result`     | Result word, see (B)                                                                                                         |
| 5      | 1    | `phase`      | idle, prep, check, download, staged, handover, apply, probation, done                                                        |
| 6      | 1    | `err`        | Error token as a code: lowheap, lowheap_ble_off, lowheap_phone, lowheap_tls, tls, nomem, stall, size, env, nvs, reset, other |
| 7      | 1    | `skip`       | Skip reason as a code: phone, battery, clock, tx, backoff, attempts, sleep, hold, safeboot                                   |
| 8      | 5    | `from`       | Packed tag before: major, minor, letter, month, day (0 = undated)                                                            |
| 13     | 5    | `to`         | Packed tag after                                                                                                             |
| 18     | 4    | `startUtc`   | Start of the attempt, node UTC (0 = clock not valid)                                                                         |
| 22     | 2    | `dlS`        | Download seconds                                                                                                             |
| 24     | 2    | `totS`       | Start to healthy, seconds; 0xFFFF unknown                                                                                    |
| 26     | 4    | `steps`      | Bitmask below                                                                                                                |
| 30     | 1    | `dlTries`    | Download attempts used                                                                                                       |
| 31     | 1    | `lowheapN`   | Heap gate refusals                                                                                                           |
| 32     | 1    | `applyTries` | Safeboot apply tries (v4 writes it; 0 = unknown)                                                                             |
| 33     | 1    | `boots`      | Boots until healthy, or until parked                                                                                         |
| 34     | 2    | `minFreeKB`  | Lowest free heap during the job, KB                                                                                          |
| 36     | 2    | `minBlkKB`   | Lowest largest block during the job, KB                                                                                      |
| 38     | 2    | `nightsOk`   | Installs that reached healthy since opt-in                                                                                   |
| 40     | 2    | `nightsFail` | Failed or parked nights since opt-in                                                                                         |
| 42     | 1    | `flags`      | bit 0 WLAN back after install, bit 1 auto install switched off by the guard, bit 2 Safeboot v4 seen                          |
| 43     | 1    | reserved     | 0                                                                                                                            |

### Steps bitmask

| Bit   | Name (shown)         | Source today                                      |
| ----- | -------------------- | ------------------------------------------------- |
| 0     | BLE paused           | `[AU];shed;ble`                                   |
| 1     | BLE forced off       | `[AU];shed;guard`                                 |
| 2     | display buffer freed | `[AU];shed;tft`                                   |
| 3     | sockets purged       | `[AU];shed;tw;<n>` with n above 0                 |
| 4     | KISS stopped         | `[AU];shed;services;..;kiss;1`                    |
| 5     | console closed       | `[AU];shed;services;console;1`                    |
| 6     | job restarted        | `[AU];shed;restart`                               |
| 7     | heap wait 10 min     | backoff after a lowheap refusal                   |
| 8     | download retried     | attempt 2 or 3                                    |
| 9     | waited for phone     | skip reason phone                                 |
| 10    | waited for battery   | skip reason battery                               |
| 11    | waited for clock     | skip reason clock                                 |
| 12    | waited for TX        | new TX gate                                       |
| 13    | rebooted before      | new reboot before download                        |
| 14    | apply retried        | Safeboot v4 reports `tries` above 1               |
| 15    | BLE resume retried   | resume needed more than one try                   |
| 16-31 | reserved             | for example a gateway pause, if one is ever built |

### Surviving the reboots

- **Before the handover** the app writes `phase=handover`, `startUtc`, `dlS`, `steps`, counts. Safeboot does not touch `out`.
- **Safeboot v4** writes a 3-byte `sbres` (result, tries, apply seconds). With v3 the app infers the apply result from the running tag and leaves `applyTries` 0.
- **At each boot in probation** the app increments `boots`. At healthy it writes `result=ok`, `phase=done`, `totS` = handover time minus start, plus apply seconds, plus uptime.
- **A node that dies mid-job** (for example the TASK_WDT of AU-24) leaves `phase=download`, `result=running`. The next boot turns that into `dl_fail:reset` with the reset reason.

## Healthy and the crash-loop guard

### Healthy

| Option                  | Effect                                                                                             | Risk                                                              | Verdict           |
| ----------------------- | -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | ----------------- |
| H1. Uptime 5 min only   | Catches crash loops                                                                                | A node without radio or without heap passes                       | Not enough        |
| **H2. Process health**  | Setup returned, loop alive for 300 s, LoRa chip init ok, free internal heap above a floor at 300 s | Floors are estimates (16 KB classic, 24 KB S3); tune on the bench | **Recommended**   |
| H3. H2 plus WLAN back   | Proves remote recoverability                                                                       | A router that reboots at night parks a good node                  | Operator question |
| H4. H3 plus server link | Proves the gateway path                                                                            | An internet outage parks the fleet                                | Reject            |

With H2, WLAN is recorded (`flags` bit 0) but does not count. Healthy not reached 600 s after boot means the app restarts itself; that boot counts as failed.

### Boot counter

1. Before the handover the app writes NVS `fwstage`/`probe` = packed target tag, and `boots` = 0.
2. First statement of `setup()` on ESP32 (`src/main.cpp:35`, before `SPI.begin()`): with `probe` set, read the reset reason. Power-on and deep-sleep wakes do not count; every other reason increments `boots`.
3. Running tag differs from `probe`: Safeboot did not apply. Result `apply_fail`, `probe` cleared, the old image runs on.
4. `boots` above 3: park. Write `hold` = image id of the running image (the 4-byte `img` of `/austatus`) plus target tag, switch the automatic install off, result `parked`, set the Safeboot partition, restart.
5. Healthy reached: result `ok`, `probe` and `boots` cleared, `nightsOk` plus 1.

### How Safeboot honours the park

| Option                                                           | Effect                                                                                 | Cost                                                                                             | Verdict                          |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ | -------------------------------- |
| **P1. Safeboot v4 reads `hold` and suspends its 180 s fallback** | Node stays in Safeboot, page shows the park, "Release" clears `hold` and boots the app | Safeboot change, USB flash once per node (web flasher)                                           | **Recommended; opt-in needs v4** |
| P2. App only: a held image goes straight back to Safeboot        | Works with Safeboot v3; node cycles app, Safeboot 180 s, app                           | About 480 otadata writes a day while parked; app code runs every cycle                           | Backstop only                    |
| P3. ESP-IDF bootloader rollback (`PENDING_VERIFY`)               | Also catches a crash before `setup()`                                                  | New bootloader by USB, interacts with Safeboot's boot-partition writes and the AU-16 DIO history | Reject now, revisit later        |
| P4. Count boots in Safeboot                                      | Safeboot sees every boot                                                               | Every reboot goes through Safeboot: slower, more flash writes                                    | Reject                           |

Leaving the park: a manual upload in Safeboot boots an image whose id differs from `hold`, so the app clears `hold` and boots normally. The automatic install stays off until the user switches it on again. The record keeps `parked` as history.

## What must never happen

| Rule                                                            | Gate today                                                                   | New gate                                                                                                   |
| --------------------------------------------------------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| No install on battery below 3600 mV                             | `fwTick()` handover and `fwAutoSkipReason()` battery (`src/fw_update.h:461`) | none                                                                                                       |
| No install while a phone is connected                           | skip reason phone; the BLE guard counts as a phone (`src/fw_update.h:1047`)  | none                                                                                                       |
| No handover while TX is pending                                 | none                                                                         | `tx`: TX ring depth above 0 (`txRingDepth()`) or a frame in flight; wait up to 10 min, then skip the night |
| No install without a valid clock                                | skip reason clock                                                            | none                                                                                                       |
| No install that Safeboot cannot apply                           | `fwNetSafebootCapable()`, DIO check (`src/esp32/esp32_main.cpp:3262-3272`)   | nightly path needs Safeboot v4                                                                             |
| No older or same tag                                            | `fwShouldInstall()` and `last`                                               | none                                                                                                       |
| No image of another board                                       | `[AU];refuse;env` (`src/esp32/fw_update_net.cpp:1165`)                       | none                                                                                                       |
| No install on prod channel or in an upstream build              | `AU_AUTO_INSTALL` fence                                                      | nightly fence `AU_NIGHTLY_INSTALL` = dev channel compiled in, and channel dev set                          |
| Not more than one install per night, not during probation       | none                                                                         | NVS night stamp; `probe` set blocks                                                                        |
| Not after the guard parked the node or switched the install off | none                                                                         | `hold`, opt-in flag                                                                                        |
| Not with deep sleep configured                                  | none                                                                         | `sleep`: wakes would distort the boot counter                                                              |
| Not while a job, a remote go or a web upload runs               | `fwNetStart()` busy, `fwApplyRebootAct()`                                    | none                                                                                                       |
| Not outside 03:00 to 05:00 local                                | window (`src/fw_update.h:459-460`)                                           | slot minute inside it                                                                                      |
| Not on DK5EN-98                                                 | default off                                                                  | bench rule: never opted in                                                                                 |

The new reasons extend `fwAutoSkipReason()` in its pure form, so the order stays host-tested.

## Interaction with AU-24 and AU-25

- **AU-24:** DK5EN-92 refuses with `lowheap_tls` after hours of uptime; a first download hit TASK_WDT in the gateway (`docs/BACKLOG.md:332`). **AU-25:** DK5EN-1 stopped twice at about 54 % with `nomem` after 3 h; after a reboot it went through (`:336`).
- **Both appear after long uptime.** The reboot before the download (step 2 above, uptime over 3 h, once per night) is the mitigation. It is recorded as bit 13, so the nights show whether it is needed.
- **The nightly is the AU-24/AU-25 measurement:** `minFreeKB`, `minBlkKB`, `lowheapN` and the err token are recorded every night on both bench nodes, with and without the prior reboot.
- **A TASK_WDT during the download** happens on the old image, before any install. The crash-loop guard does not cover it; the record shows `dl_fail:reset` with the reset reason.
- **Condition for wave 6:** either AU-24 and AU-25 are root-caused and fixed, or 14 consecutive nights on DK5EN-1 and DK5EN-92 show no lowheap failure with the reboot in place.

## CI design

### Options

| Option                                                                  | Effect                                | Risk                                                                                     | Verdict         |
| ----------------------------------------------------------------------- | ------------------------------------- | ---------------------------------------------------------------------------------------- | --------------- |
| C1. Add `schedule` to `meshcom-ci.yml`                                  | One file                              | Every step keys on `github.ref_name`; under `schedule` that is the default branch        | Reject          |
| **C2. New `nightly.yml` calls `meshcom-ci.yml` as a reusable workflow** | One build path for tags and nightlies | `meshcom-ci.yml` needs inputs for tag, commit and notes                                  | **Recommended** |
| C3. `nightly.yml` pushes a tag that fires `meshcom-ci.yml`              | No change to the build workflow       | A tag pushed with the workflow token fires no workflow; needs a personal token as secret | Reject          |

### `nightly.yml`

- **Triggers:** `cron: '5 23 * * *'` (00:05 CET) and `cron: '5 22 * * *'` (00:05 CEST); GitHub cron runs in UTC. Plus `workflow_dispatch` with a `dry_run` input.
- **Plan job, first:** date and hour in `Europe/Berlin`. A scheduled run proceeds only between 00:00 and 02:59 local; the off-season cron then skips by itself (23:05 in winter) or acts as a retry (01:05 in summer).
- **Tag:** `v<SOURCE_VERSION><SOURCE_VERSION_SUB>.MM.DD`, read from `src/configuration_global.h` at the `fork-nightly` commit, date in `Europe/Berlin`. A UTC date would name the previous day.
- **One per day:** a published release with that tag ends the run green and does nothing. `concurrency: nightly`, no cancel, serialises a delayed run and the retry.
- **Build:** `uses: ./.github/workflows/meshcom-ci.yml` with `tag`, `commit` (the `fork-nightly` sha) and `notes`. The reusable workflow checks out that commit, sets `MC_BUILD_TAG` from `inputs.tag`, and creates the tag on publish.
- **Unchanged nights still build:** `MC_BUILD_TAG` is inside the image (AU-D16), so the assets of an earlier night cannot be reused under a new tag.
- **Build failure:** no publish, the draft is deleted in an `if: failure()` step, GitHub mails the failure. Nodes report `no_nightly` that night. A manual dispatch later the same day uses the same date.
- **Late run:** a run that starts after 02:59 skips. Nodes check from 02:15 on, so a release after about 03:00 reaches half of them at best. The operator can dispatch by hand; nodes take it the next night.
- **Dry run:** `dry_run: true` builds as a draft with a `ci-proof-nightly-<date>` tag and never publishes. Delete draft and tag afterwards, as for `ci-proof-*`.

### Notes

- **Repeat night** (same sha as an earlier release): body = Important box, one line "Nightly of fork-nightly `<sha7>`, same source as `<first tag of this sha>`", link to that release's notes, link to the "Nightly report" form.
- **First night after a promotion:** body = Important box, the nightly line, then `release-notes.md` from the promoted commit.
- **Important box** keeps today's "not the official firmware" text and adds: "Nightly build. It installs itself only on nodes that opted in; everyone else updates by hand."

### Retention

Keep every release that is the first of its sha (the promotions) and the newest 14 nights. Delete older repeat nights with their tags (`gh release delete <tag> -R DK5EN/MeshCom-Firmware --cleanup-tag --yes`). Never touch Latest. A node running a deleted tag is unaffected: AU compares by date.

### Issue form

New `.github/ISSUE_TEMPLATE/nightly_report.yml` on `fork-dev`: fields `node`, `board`, `nightly_tag`, `before_tag`, `result`, `steps`, `what_broke`, `info` (paste, with the redaction hint). Field ids match the URL parameters of (A2).

## Promotion recipe

1. **The OK names a commit** on `fork-dev`. `git merge-base --is-ancestor origin/fork-nightly <sha>` must hold; otherwise no promotion, never a force push.
2. **Gates on that commit:** `tools/regression.sh --stage 1,2` green; every board env builds (REVIEW.md R2); a `ci-proof-*` run on the commit verifies 68 assets.
3. **Bench:** the `ci-proof` images go to DK5EN-1 and DK5EN-92 through the AU path (`--update stagelan`, then apply), 30 min healthy, `--update check` sane, no `parked` or `apply_fail` on either node in the last 3 nights.
4. **Full notes:** `/release-firmware` steps 1 and 2 (docs, `release-notes.md`, journal) on `fork-dev`, committed. That docs commit is the one promoted.
5. **Fast-forward:** `git push origin <sha>:refs/heads/fork-nightly`. Branch protection on `fork-nightly`: no force push, no deletion.
6. **Next 00:05** publishes it with the full notes. Next morning: verify the release (68 assets, tag string in one image), then the web flasher step of `/release-firmware` with that tag.

Changes to `/release-firmware` (concept only, not edited): the tag push becomes step 5, and watching the run moves to the next morning.

Manual `v*` tags from `fork-dev` stop: they would take the nightly's date or make a build that is not `fork-nightly` the Latest. Emergency path: dispatch `nightly.yml` by hand.

## Bench plan

Bench rules apply throughout: `tools/bench/identity_guard.py` first, `lsof <port>` before each port, TX power at most 2 dBm, `--airgap on` on both nodes during the drills, DK5EN-98 untouched and never opted in. Summaries go to `tools/bench/runs/harness/nightly01-<case>-<date>.summary`.

Nodes: DK5EN-1 (Heltec V3, S3 without PSRAM, no BLE pause) and DK5EN-92 (T-Beam, classic, BLE pause). Optional third: DK5EN-14 (T-Deck Plus, PSRAM path), only with the operator's OK. Both need Safeboot v4 by web flasher first.

### Fault injection

Fault images are local INSTRUMENT builds with a fault flag, staged with `--update stagelan <url> <ilen> <tag>` (`src/command_functions.cpp:5749`) under a tag one day ahead. `--autest jump` brings the slot near (`src/command_functions.cpp:6563`).

| #   | Case                      | Fault                                                              | Pass                                                                                                                  |
| --- | ------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| F1  | Crash at boot             | `MC_FAULT_BOOT=early`: panic right after the boot counter          | 3 counted boots, park, Safeboot stays beyond 180 s, page shows the park, auto install off, record `parked`, `boots` 3 |
| F2  | Hang in the loop          | `MC_FAULT_BOOT=hang60`: loop stops after 60 s, task WDT            | Same as F1, reset reason task WDT in the record                                                                       |
| F3  | Runs but never healthy    | `MC_FAULT_BOOT=noradio`: LoRa init reported failed                 | Self-restart at 600 s, park after the third                                                                           |
| F4  | Recovery from the park    | Upload a good image in Safeboot                                    | App clears `hold`, boots normally, auto install still off, record keeps `parked`                                      |
| F5  | Release from the park     | "Release" on the Safeboot v4 page                                  | App boots, counts again, F1 image parks again after 3                                                                 |
| F6  | Power loss during apply   | Operator cuts USB power during the apply                           | `[SAFEBOOT];apply;retry`, bit 14 set, install completes                                                               |
| F7  | Gates hold                | Phone linked, battery below floor (bench supply), 15 frames queued | Skipped with `phone`, `battery`, `tx`; no handover                                                                    |
| F8  | No nightly                | `--autest release` with yesterday's date                           | Result `no_nightly`, nothing installed                                                                                |
| F9  | Long uptime (AU-24/AU-25) | 6 h uptime before the slot                                         | Bit 13 set, download ok, heap minima recorded                                                                         |

Then the real thing: both nodes opted in on the dev channel, 14 consecutive nights, each checked by RM `upl` the next morning.

## Implementation waves

Each brief targets about 25 min. File sets are exclusive within a wave; parallel briefs route `pio` through the scratchpad `piolock.sh`. Gate after every wave: the named host envs, then `tools/regression.sh --stage 1,2`, builds of the touched board envs, code review against REVIEW.md R1 to R4.

| Wave | Content                                                                                                                        | Exclusive files                                                                                                                                                                                  | Tests                                                                                                                         | Parallel with |
| ---- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- | ------------- |
| W1a  | Outcome record encode and decode, packed tag, step bits and names, result words, slot minute                                   | new `src/fw_nightly.h`, new `test/test_fw_nightly/test_fw_nightly.cpp`, `platformio.ini` (new `[env:native_fw_nightly]`)                                                                         | `native_fw_nightly`                                                                                                           | W2            |
| W1b  | Nightly gate (new skip reasons), probation step, healthy predicate, park decision                                              | same as W1a                                                                                                                                                                                      | `native_fw_nightly`, `native_fw_update`, `native_fw_update_prod`                                                              | W2            |
| W2   | Reusable `meshcom-ci.yml`, `nightly.yml`, plan and retention script, issue form                                                | `.github/workflows/meshcom-ci.yml`, new `.github/workflows/nightly.yml`, new `tools/nightly_plan.py`, new `tools/tests/test_nightly_plan.py`, new `.github/ISSUE_TEMPLATE/nightly_report.yml`    | pytest `tools/tests/test_nightly_plan.py`; proof: dispatch with `dry_run`, 68 assets                                          | W1            |
| W3   | Record the steps and heap minima in the job, NVS read and write of `out`                                                       | `src/esp32/fw_update_net.cpp`, `src/esp32/fw_update_net.h`                                                                                                                                       | `native_fw_nightly`; builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`                                                            | W4, W5        |
| W4   | Boot counter, healthy tick, park, `hold` backstop, fault flags                                                                 | new `src/esp32/fw_nightly_guard.cpp`, new `src/esp32/fw_nightly_guard.h`, `src/main.cpp` (one call), `src/esp32/esp32_main.cpp` (healthy tick call only)                                         | `native_fw_nightly`; same builds                                                                                              | W3, W5        |
| W5   | Safeboot v4: `hold` suspends the fallback, parked page, Release, `sbres`                                                       | `src/safeboot/main.cpp`, `src/safeboot/ota_state.h`, `src/safeboot/safeboot_ver.h`, `src/safeboot/ota.html`, `src/safeboot/ota.h`, `test/test_safeboot_state/`                                   | `native_safeboot`, `tools/safeboot_page_test.js`; builds `esp32-safeboot`, `esp32-S3-safeboot`                                | W3, W4        |
| W6a  | Web: `last` in `/austatus`, AU card lines, GitHub link with whitelist and preview                                              | `src/web_functions/au_ui_state.h`, `src/web_functions/web_functions.cpp`, `src/web_functions/web/mcau.js`, `test/test_au_ui_state/`, `tools/webgui_au_test.js`                                   | `native` (test_au_ui_state), `tools/webgui_au_test.js`                                                                        | W6b, W6c      |
| W6b  | Console `--update last` and `--info` line, RM read `upl`                                                                       | `src/command_functions.cpp`, `src/rm_exec_read.cpp`, `src/rm_exec_ext.h`, `src/rm_au.h`, `src/esp32/rm_au_esp32.cpp`, `src/rm_au_none.cpp`, `src/rm_commands.h`, `tools/remote_cmd_vectors.json` | `native_rm_format`, `native_remote_cmd`, `native_remote_cmd_dev`                                                              | W6a, W6c      |
| W6c  | OLED boot notice, T-Deck info line                                                                                             | `src/esp32/fw_nightly_guard.cpp`, `src/t-deck/lv_obj_functions.cpp`                                                                                                                              | builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `t_deck`, `t_deck_plus`                                                        | W6a, W6b      |
| W7   | The lift: `AU_NIGHTLY_INSTALL` fence, nightly branch in `auTick` (prep reboot, check, download, TX gate, handover at the slot) | `src/fw_update.h`, `src/esp32/esp32_main.cpp`, `tools/tests/test_au_fences.py`, `test/test_fw_update/`                                                                                           | `native_fw_update`, `native_fw_update_prod`, `native_fw_nightly`, pytest `test_au_fences.py`; every board env (shared header) | none          |
| W8   | Bench drill F1 to F9, campaign doc, `RESUME.md`, `BACKLOG.md`                                                                  | docs and `tools/bench/runs/harness/` summaries                                                                                                                                                   | bench                                                                                                                         | none          |

W7 gets an advisor review before its commit. `AU_AUTO_INSTALL` keeps its `#error`; the new fence pins that a build without `MC_AU_DEV_CHANNEL` cannot contain the nightly branch (`native_fw_update_prod`, `test_au_fences.py`).

Timeline: W1 to W7 by about 2026-11-24, W8 drill by 2026-11-26, then nights from 2026-11-27. Fourteen green nights end on 2026-12-10, which leaves a week of margin before 2026-12-18.

The nightly install path stays fork-only. Upstream builds have no dev channel, so the fence keeps it out even if code is ported; wave 6 brings notify, schedule and Safeboot, with the nightly record as evidence.

## Not verified / gaps

- **GitHub schedule timing:** how late scheduled runs start at :05 past the hour, and whether a timezone key exists for `schedule`, were not checked. The design does not rely on either.
- **Reusable workflow details:** that `ncipollo/release-action` creates the tag from `commit` when a draft is published, and how `github.ref_name` behaves in a called workflow, need the dry-run proof of W2.
- **Inactive repositories:** GitHub disables scheduled workflows after 60 days without activity in a public repository. `fork-dev` moves often; a quiet spell would stop the nightlies, which nodes then show as `no_nightly`.
- **Crash before `setup()`:** a crash in a static constructor is not counted, so the guard cannot catch it. Only the bootloader rollback (P3) would.
- **Healthy floors and timeouts** (300 s, 600 s, 16 and 24 KB) are estimates; the bench sets them.
- **Reset reasons:** a user's `--reboot` within 5 min of the install counts as a failed boot. Three are needed for a park; not tested.
- **Safeboot v4** means a USB flash of every node that opts in. Nodes on v3 keep notify only.
- **OLED and T-Deck space** for the new lines was not checked on the screens.
- **Sizes:** 44 bytes NVS is exact by layout; the RAM, flash and JS sizes in (A) are estimates, no build was run.
- **Issue form prefill:** field prefill by URL parameter is documented by GitHub but not tested on this repository.
- **AU-24 and AU-25 causes** stay unknown; the reboot before the download is a mitigation, not a fix.
- **Dev-channel notify users** see a new release every day. Not measured whether anyone besides the bench uses the dev channel.

## Open operator questions

1. **Healthy with or without WLAN (H2 or H3)?** H3 proves the node can be reached for a reinstall, but a router that reboots at night parks a good node. Recommended: H2, WLAN recorded only.
2. **Safeboot v4 required for opt-in (P1)?** It means one USB flash per opted-in node; without it a parked node cycles every 3 min. Recommended: yes, v4 required.
3. **Reboot before the download** when uptime is over 3 h: on all opted-in boards, or classic ESP32 only? AU-25 was on an S3. Recommended: all opted-in boards, once per night.
4. **EXTUDP outcome line (A4)** to the user's own LAN collector, opt-in: build it in W6b, or not now? Recommended: yes, small, stays in the user's LAN. The DM to a collector (A5) not now.
5. **Retention:** delete repeat nightlies after 14 days, keep the first release of each promoted commit? Recommended: yes.
6. **Manual `v*` tags from `fork-dev` stop**, releases only through `fork-nightly` (emergency: dispatch by hand)? Recommended: yes, else a release can steal the nightly's date.
7. **Promotion gate:** `ci-proof` with 68 assets plus 30 min healthy on DK5EN-1 and DK5EN-92 through the AU path before each fast-forward? Recommended: yes.
8. **Evidence for wave 6:** 14 consecutive green nights on DK5EN-1 and DK5EN-92 plus the passed drill F1 to F9 by 2026-12-14; the nightly install path stays fork-only? Recommended: yes to both.
