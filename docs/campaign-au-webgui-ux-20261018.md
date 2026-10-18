# Auto Update: Web GUI, modes and low-heap handling -- implementation concept

Status 2026-10-18, revision 6, **final -- ready for implementation**: operator rounds 1-5 and the fable review (6
finders, 6 Opus verifiers, 32 confirmed findings, section 11) are included; no open points. Branch `fork-dev`, code
state 85b0c457. Auto Update (#1187) is fork-only.

## 0. BLUF

- One state-driven action button in the **Auto update row** of the Info page (Check now -> Install -> Downloading
  ... % -> Install now -> Installing ... %), the row turns green when a release is available, the banner goes. The
  page reads a new `GET /austatus` (and Safeboot's `/ota/state` while the node is in Safeboot), so the button follows
  the node without a reload.
- `off` is removed, **`notify` is the default**; `auto` can be chosen, but the automatic install is **not in the
  build at all** (compiled out, three fences, 5.3). Anyone with web access may install manually.
- Low-heap messages come from the **result of a real job**, not from a live heap reading (a live reading is wrong
  on every classic ESP32). A connected phone gets a "Pause Bluetooth and check" button backed by a **BLE guard**
  that keeps McApp off for the whole job.
- There is no night window for checks today; the shedding steps run on every automatic or manual check. Two
  INSTRUMENT commands make every path testable on demand.

## 1. Facts from the code

| Topic                  | Fact                                                                                                                                                                                                                                                                                                                                                                                                  |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Web server             | `web_functions.cpp` serves both ESP32 and the RAK4631 (nRF52); AU blocks are ESP32-guarded, the CSS (`.au-banner` `:1542`) and shared JS are not. The Info page never refreshes itself (`autorefresh()` `:1207` skips it); `callfunction()` `:1306` ignores the answer.                                                                                                                               |
| `/callfunction` answer | `call_function()` sends the HTTP header only after `webFunctionCall()` returns (`web_functions.cpp:4548-4551`); body `{"<fn>":"ok"                                                                                                                                                                                                                                                                    | "failed"}` (`:4557`). |
| Web guard              | `MC_WEB_GUARD` defaults to 0 (`web_guard.h:87`), no env sets it; `/callfunction` and `/setparam` are open (`test_web_guard:652-667` pins that). `X-MC: 1` is a **request** header the page's own JS adds to every fetch (`web_functions.cpp:1199-1203`); guarded paths require it on state-changing requests. CORS `Access-Control-Allow-Origin: *` is sent on every non-RM path (`:916`, `:4326`).   |
| Banner, row, drawer    | Banner `web_functions.cpp:3900-3926`; row `:3957-3972`; Setup drawer "Firmware Update" `:3292-3330`, select `aumode` `:3305`; setters `web_setup.cpp:276-305` (aumode prod/dev runs `--autoupdate auto`, legacy `autoupdate` passes the value through), getters `:991-997`.                                                                                                                           |
| Status snapshot        | `FwNetStatus` `fw_update_net.h:150-162`; `ilen`/`zlen` are those of the **last check** (`fw_update_net.cpp:605,617`), 0 after a reboot; the staged record holds its own `ilen` (`FwStageRecord`, `fw_update.h:257`, written `:914`); `bytesDone` counts compressed bytes and is not cleared after a job; STAGELAN never sets `zlen`.                                                                  |
| Heap gate              | `heapGateOk()` (anonymous namespace, `fw_update_net.cpp:520-531`) logs `[AU];refuse;lowheap` on failure and calls `tlsAllocInstall()`. Classic ESP32: free >= 76000 B, block >= 17000 B; PSRAM-TLS: PSRAM block >= 17000 B, internal free >= 24000 B. The job pauses BLE **before** the gate (`:1077-1082`, then `:544`/`:809`); DK5EN-92 bench: about 57 KB free with BLE up, 80-85 KB paused.       |
| BLE                    | `esp32BlePause()` `esp32_main.cpp:629` runs only on classic ESP32 (`AU_BLE_PAUSE`, `fw_update_net.h:194-201`) and refuses with a phone (`deviceConnected` or `isPhoneReady`); `advertiseOnDisconnect(true)` (`:555`) restarts advertising from the NimBLE host task after a disconnect. CHECK and DOWNLOAD pause and resume BLE separately (`fw_update_net.cpp:1082`, `:1100-1112`).                  |
| Scheduler              | First check 10 min after WiFi, then 22-26 h after each check start (`esp32_main.cpp:2646-2652`); not window-gated; no check while `isPhoneReady == 1` (`fw_update.h:477`), the due check fires when the phone drops. The 03:00-05:00 window gates only the handover in mode 2. Automatic download `esp32_main.cpp:2661-2667`.                                                                         |
| Install path           | `--update apply` -> `auRebootToSafeboot()` (`command_functions.cpp:219-237`, 2 s display, `esp_restart`); no heap or phone gate. `updapply` answers OKAY unconditionally (`web_nodefunctioncalls.cpp:64-66`).                                                                                                                                                                                         |
| Safeboot               | Apply = CRC32 of the record, inflate (adler32), `esp_image_verify()`; on failure it stays in Safeboot with its own web server on port 80 for 180 s (`safeboot/main.cpp:656-674`, `ota_state.h:101`), redirects unknown paths to `/update` (`:736-739`), serves `GET /ota/state` JSON (`state`, `reason`, `fallback_in_ms`, `received`, `total`, `app_valid`; `:841-860`); registers as "MeshCom-OTA". |
| Integrity              | SHA-256 of the compressed file against the asset digest from the GitHub release JSON (`fw_update_assets.h:314,570`, `fw_update_net.cpp:873-880`); flash read-back CRC32 (`:883-898`); Safeboot as above. TLS roots pinned (`setCACert`). The release tag is stored unvalidated (`fw_update_net.cpp:599-613`).                                                                                         |
| Reinstall guard        | `fwShouldInstall()` (`fw_update.h:365-377`) refuses a tag equal to NVS `fwstage/last`, written at every stage and never removed.                                                                                                                                                                                                                                                                      |
| Settings               | `node_autoupd` default 0 in the COMMON block (`meshcom_settings.h:207`, reaches nRF52); `MC_PLATFORM_DEFAULT` exists; load sanitizer `sanitize_loaded_settings()` (`esp32_flash.cpp:221`) saves once after `preferences.end()`; JSON import range 0..2 (`config_json.h:354`) doubles as the nRF52 load clamp. Web-configured nodes are in mode 2 today (prod/dev select).                             |
| WiFi start             | `esp32_main.cpp:2244` starts WiFi if gateway, ext UDP, web server, net console or `node_autoupd > 0`; nodes without an SSID stay off (`wifiSsidMissingBlocksStart`).                                                                                                                                                                                                                                  |
| Bench install time     | Click to node answering again: DK5EN-1 about 17 s, DK5EN-98 about 25 s, DK5EN-14 about 32 s, DK5EN-92 about 33 s.                                                                                                                                                                                                                                                                                     |

## 2. Decisions (operator, 2026-10-18)

| ID  | Decision                                                                                                                                                               |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D1  | `off` is removed; `notify` is the default for new, wiped and existing nodes.                                                                                           |
| D2  | `auto` is console-only and blocked: it behaves like `notify`, the console prints `[AU];auto;blocked;notify only` where an automatic install would start.               |
| D3  | Web select: `notify (prod)`, `notify (dev)`, `auto (coming soon)` greyed with "auto: coming soon -- for now the node notifies only".                                   |
| D4  | Manual install is open to anyone with web access, in every mode (people, not foreign web pages: see 5.2).                                                              |
| D5  | One action button in the row, banner removed, no confirm dialog.                                                                                                       |
| D6  | Labels: "Check now", "Checking now ...", "Install", "Downloading now ... 17 %", "Verifying download ...", "Install now" (red), "Installing now ... 39 %".              |
| D7  | Phone block only after a real failed check (D13); on page open at most a grey hint line.                                                                               |
| D8  | Safeboot too old: "Use Webflasher to renew Auto Update aware Safeboot".                                                                                                |
| D9  | `[AU];auto;skip;<reason>` when an automatic download is due but does not start.                                                                                        |
| D10 | A phone's BLE link may be dropped for an update; the BLE guard keeps McApp from reconnecting during the job.                                                           |
| D11 | Heap measurements only for services present in the build.                                                                                                              |
| D12 | Flash permission for this campaign: every node except DK5EN-98.                                                                                                        |
| D13 | (was C1) G and H come only from a real job result; a classic ESP32 with a connected phone shows a grey hint line on page open, no block.                               |
| D14 | (was C2) `auto` stays choosable, but the automatic apply must be physically impossible in this build: no migration of stored `auto`, the code path is not compiled in. |
| D15 | (was C3) WiFi starts for auto update only when an SSID is set and the node is not in AP mode.                                                                          |
| D16 | (was C4) A node whose phone stays connected drops the phone link (BLE guard) for its automatic check, once per check period.                                           |

## 3. Resolved review points

The four points the review raised (C1-C4) are decided as D13-D16. For D13 the page-open hint looks like this:

```
|  Auto update | (i) A connected phone may block the update on this board |
|              | [ Check now ]                                            |
```

Why D13: an idle T-Beam has about 57 KB free with BLE up, below the 76000 B gate, yet every update passes because
the job pauses BLE first (80-85 KB); a live reading would show "not enough memory" on every classic ESP32.

## 4. Info page: the Auto update row

### 4.1 States

Rendered **only by the page JS** from `/austatus` (one renderer, like the RM page); the server sends the fixed lines
(mode, channel, env, running tag) and an empty action area that the JS fills on load. Green row = `#`.

| State | Condition                                                                                    | Row         | Message line                                                              | Button                                   |
| ----- | -------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------------------- | ---------------------------------------- |
| S     | `sbok == 0`                                                                                  | normal      | `! Use Webflasher to renew Auto Update aware Safeboot`                    | `[ Check now ]`; Install/Install now off |
| F     | the page remembered an "Install now" click for this node (6.2), node not yet back on the tag | green       | `Installing <tag>, the node restarts ...`                                 | `[ Installing now ... NN % ]` disabled   |
| B     | busy, job check                                                                              | normal      | --                                                                        | `[ Checking now ... ]` disabled          |
| D     | busy, job download, `pct < 100`                                                              | green       | `Firmware <tag> available`                                                | `[ Downloading now ... NN % ]` disabled  |
| D2    | busy, job download, `pct == 100` (digest and read-back running)                              | green       | --                                                                        | `[ Verifying download ... ]` disabled    |
| E     | staged, idle                                                                                 | green       | `Firmware <tag> downloaded and verified`                                  | `[ Install now ]` red                    |
| G     | last result `lowheap_phone` and `phone == 1`                                                 | normal      | `! A connected phone uses the memory the update needs`                    | `[ Pause Bluetooth and check ]`          |
| G2    | last result `lowheap_phone` and `phone == 0`                                                 | normal      | `The phone has disconnected -- check again`                               | `[ Check now ]`                          |
| H     | last result `lowheap_ble_off`, or `lowheap` on a board without the BLE pause                 | normal      | `! Not enough free memory for the update. Reboot the node and try again.` | `[ Check now ]`                          |
| C     | newer and installable, not staged, idle                                                      | green       | `Firmware <tag> available`                                                | `[ Install ]`                            |
| A     | idle, nothing newer                                                                          | normal      | `available none` (or `up to date, checked <age>`)                         | `[ Check now ]`                          |
| I     | overlay: last job failed with another code                                                   | of the base | the text of 4.3 with the code in brackets, above the button               | the button of the base state (A, C or E) |
| N     | `/austatus` answers 404 (nRF52 / no AU)                                                      | --          | the row is not shown                                                      | --                                       |

Priority: S > F > busy (B, D, D2) > E > G > G2 > H > C > A; I overlays the chosen base. `newer` but not
`installable` shows A with the line "Firmware <tag> exists, but has no image for this board".

### 4.2 Renderings

```
A |  Auto update | mode notify, channel dev                         |
  |              | env heltec_wifi_lora_32_V3, running v4.40a.10.18 |
  |              | available none                                   |
  |              | [ Check now ]                                    |

B |  Auto update | [ Checking now ... ]               (disabled)    |

C |##Auto update#|#Firmware v4.40a.10.18 available                 #|
  |#            #|#[ Install ]                                     #|

D |##Auto update#|#Firmware v4.40a.10.18 available                 #|
  |#            #|#[ Downloading now ... 17 % ]       (disabled)   #|

D2|##Auto update#|#[ Verifying download ... ]         (disabled)   #|

E |##Auto update#|#Firmware v4.40a.10.18 downloaded and verified   #|
  |#            #|#[ Install now ]   <- red                        #|

F |##Auto update#|#Installing v4.40a.10.18, the node restarts ...  #|
  |#            #|#[ Installing now ... 39 % ]        (disabled)   #|

G |  Auto update | ! A connected phone uses the memory the update needs |
  |              | [ Pause Bluetooth and check ]                    |

H |  Auto update | ! Not enough free memory for the update.         |
  |              |   Reboot the node and try again.                 |
  |              | [ Check now ]                                    |

I |##Auto update#|#! Download damaged, it was discarded (digest)   #|   (overlay on C)
  |#            #|#[ Install ]                                     #|
```

### 4.3 Error texts (keyed on job and token)

| Token(s)                                                                       | Job      | Text                                                                     |
| ------------------------------------------------------------------------------ | -------- | ------------------------------------------------------------------------ |
| `tls *`, `conn *`, `http *`, `begin`, `timeout`                                | any      | No connection to GitHub                                                  |
| `short`, `stall`, `toobig`, `chunk`, `feed`, `enc`, `json`, `noenv`, `badver`  | check    | The release list could not be read                                       |
| `short`, `stall`, `digest`, `readback`, `crc`, `write`, `enc`, `feed`, `chunk` | download | Download damaged, it was discarded -- try again                          |
| `noasset`, `nodigest`                                                          | download | This release has no image for this board                                 |
| `notota0`, `imglen`, `size`, `overlap`, `small`, `inflate`, `layout`           | download | This node's partition layout cannot take the image -- use the Webflasher |
| `erase`, `noflash`, `nvs`                                                      | download | Could not write the image to flash                                       |
| `nomem`                                                                        | any      | Not enough free memory for the update                                    |
| `lowheap`, `lowheap_phone`, `lowheap_ble_off`                                  | any      | handled by states G, G2, H                                               |
| `nocheck`, `url`, anything else                                                | any      | Update failed                                                            |

The mapping lives in the pure header of W1a; the token always follows in brackets. Tag strings are inserted with
`textContent`, never `innerHTML`.

### 4.4 Style

- Banner block `web_functions.cpp:3900-3926` and `.au-banner` CSS removed.
- Green row: a class on the row's cells with the banner's former colours.
- New CSS, ESP32 builds only: `#content_inner button:disabled{opacity:.45;cursor:not-allowed}` and a red button
  class on `--mcred` with white text.
- Every action button is disabled on click until the next `/austatus` answer (no double start).

## 5. Firmware changes

### 5.1 `GET /austatus` (ESP32 only; RAK answers 404)

Read-only, no request header needed, **`web_cors_ok = false` set explicitly** (no `Access-Control-Allow-Origin`),
built by a header-only producer `auStatusJson()` (W1a) with JSON escaping:

```json
{
  "st": "idle|busy|ok|fail",
  "job": "check|download|stagelan|none",
  "run": "v4.40a.10.17",
  "avail": "v4.40a.10.18",
  "newer": 1,
  "inst": 1,
  "staged": "v4.40a.10.18",
  "err": "",
  "pct": 0,
  "phone": 0,
  "guard": 0,
  "sbok": 1,
  "mode": "notify",
  "chan": "dev",
  "etaS": 35,
  "ifree": 57000,
  "iblk": 31000
}
```

- `pct` = `bytesDone * 100 / zlen` only while busy with a download and `zlen > 0`, else 0; STAGELAN sets `zlen`
  from Content-Length.
- `etaS` from the **staged record's** `ilen` (new `FwNetStatus.stagedIlen`, set at staging and at record load);
  `etaS = AU_ETA_HANDOVER_S + stagedIlen / AU_ETA_RATE_BPS + AU_ETA_BOOT_S`, constants per chip family fitted to
  the bench; a fixed 35 s when `stagedIlen` is 0.
- `ifree`, `iblk`: diagnostic numbers only, never a gate (D13).
- Polled every 1 s while the row is in B, D, D2, F or the guard is on; every 10 s in G (so G2 appears without a
  reload); not at idle; stops when `loadPage()` leaves the Info page.

### 5.2 Web actions (`/callfunction`)

- All `upd*` actions require the request header `X-MC: 1` (the node's page already sends it; a foreign web page
  cannot set it without a CORS preflight the node does not grant). Keeps D4 for people, blocks CSRF.
- `updcheck`: unchanged, plus a cooldown of 60 s between manual checks (GitHub allows 60 unauthenticated API calls
  per hour per public IP).
- `updcheck` with parameter `ble` ("Pause Bluetooth and check"): accepted only when the last result is
  `lowheap_phone` and a phone is connected; sets the BLE guard (section 7); the loop chains CHECK and, if the row
  was in C, DOWNLOAD under the same guard; cooldown 5 min.
- `updinstall`: when an image is already staged it answers OKAY without a job (as today); never blocked by a phone.
- `updapply`: validates the staged record (`fwNetLoadRecord`) and the Safeboot partition first, answers the **real**
  result, and the reboot runs from the loop 500 ms after the answer is sent. FAIL answers carry the reason in the
  row (I overlay).

### 5.3 Modes (D1, D2, D3, D14, D15)

- `node_autoupd` default `MC_PLATFORM_DEFAULT(1, 0)` (1 on ESP32, unchanged 0 on nRF52).
- `sanitize_loaded_settings()` (`esp32_flash.cpp:221`) maps a stored 0 to 1 (idempotent, no marker needed because
  `off` no longer exists). A stored 2 stays 2 (D14).
- JSON import range stays 0..2 (nRF52 load clamp); an imported 0 becomes 1 through the sanitizer at the next boot.
- Console `--autoupdate off` -> `[ERR];autoupdate;off removed, use notify`; `notify` and `auto` as today (Safeboot
  check for `auto` kept).
- **Automatic apply physically impossible (D14), three fences:**
  1. `fw_update.h` fixes `AU_AUTO_INSTALL 0` and stops the build with `#error` if any flag or env defines it
     otherwise, so no `platformio.ini` edit or `-D` can switch it on in this firmware line.
  2. The automatic paths are **not compiled**: the automatic download start (`esp32_main.cpp:2661-2667`) and the
     automatic handover branch (`act == FW_HANDOVER`, `:2620`) sit inside `#if AU_AUTO_INSTALL`. The shared handover
     helper (`:2442`) stays, because the manual "Install now" and RM1 `upd go` use it.
  3. `fwEffectiveMode()` in `fw_update.h` maps 2 to 1 where `in.mode` is set (`:2503`), so even the pure policy
     (`fwTick`) can never return `FW_HANDOVER` for an automatic install. Where an automatic download would have
     started, the console prints `[AU];auto;blocked;notify only` once per check.
     Manual install (web "Install"/"Install now", `--update install/apply`, RM1 `upd go`) is unchanged (D4).
- Web setters: aumode `prod`/`dev` set `notify` plus the channel; the legacy `autoupdate` param refuses `auto`;
  `manualcommand` stays the console (it can set `auto`, as the console can).
- RM1 `upd` and `--update status`/`--info` report `notify` or `auto`; never `off`.
- WiFi start rule per D15, extracted into `wifi_start_gate.h` with a truth-table test.
- New CSS/JS and the `/austatus` route inside `#if defined(ESP32)`.

### 5.4 Logging (D9)

`[AU];auto;skip;<reason>` once per check where an automatic download would be due; reasons `blocked`, `clock`,
`phone`, `battery`, `backoff`, `attempts`, `staged`. Pure function `fwAutoSkipReason()` in `fw_update.h`, host-tested.

### 5.5 Low-heap tokens

- New token `lowheap_phone`: the gate failed and the BLE pause was skipped because of a phone; it does not use an
  attempt (`fwErrConsumesAttempt()` extended).
- `lowheap` under the BLE guard becomes `lowheap_ble_off` (uses an attempt), so a node cannot cycle NimBLE every
  10 min.
- A read-only, non-logging `fwNetHeapNow(&ifree, &iblk)` for the diagnostic numbers; it does not touch the mbedTLS
  allocator.

## 6. Install progress and Safeboot (state F)

### 6.1 Flow

1. "Install now" -> `updapply` answers OKAY (5.2); the page stores `{tag, t0, etaS}` for this node in
   `sessionStorage` and enters F. A FAIL answer shows the I overlay on E.
2. The page ticks every second: `pct = min(95, floor(elapsed / etaS * 100))`.
3. From `etaS - 10 s` it polls `/austatus` every 2 s:
   - JSON with `run == tag`: 100 %, clear `sessionStorage`, reload into A.
   - JSON with the old tag and `sbok == 0`: state S (old Safeboot ignored the record).
   - JSON with the old tag otherwise: I overlay "Install failed, the old firmware is running".
   - Not JSON (Safeboot redirects to `/update`): poll `GET /ota/state` instead: `state` Applying shows real
     progress from `received`/`total`; `reason` `apply_failed` shows "Install failed -- the node returns to the old
     firmware in N s" from `fallback_in_ms`.
4. No answer at all after 180 s plus `etaS`: "The node is not back yet. Check its display and power; reload this
   page when it is up." (Safeboot may come up under "MeshCom-OTA" or on 192.168.4.1 in AP fallback.)
5. A second browser or a reload during F: `sessionStorage` restores F in the same tab; another tab sees the node
   in Safeboot (`/ota/state`) or back on the new tag.

### 6.2 After a failed apply

Safeboot keeps the `last` key, so the same tag is "not newer" afterwards and the row shows A; the I overlay says
"Install of <tag> failed". Re-offering the same tag needs a newer release or the INSTRUMENT command `--update forget`
(8.2).

## 7. BLE guard

1. A flag `s_bleGuard` owned by the loop task (the web handler runs there too); set before the CHECK starts.
2. The job task, when the guard is set, runs a **force pause** on every ESP32 board: `advertiseOnDisconnect(false)`,
   stop advertising, `deinit(true)` with the link up (NimBLE terminates the link while stopping, so the host task
   cannot restart advertising), then `bleSessionResetAll()` (clears `isPhoneReady` also on the terminate-timeout
   path).
3. `esp32BleResume()` refuses while the guard is set; only the release clears it.
4. The loop chains CHECK and DOWNLOAD under one guard and releases it on the last non-busy edge (done, failed,
   refused); the watchdog releases only when no job is busy, hard cap 40 min (check 5 min + download 30 min +
   handshake and verify).
5. While the guard is set, the phone counts as connected for the handover decisions (RM1 `upd go`, automatic path).
6. Logs `[AU];bleguard;on;phone;<0|1>`, `[AU];bleguard;off;<done|fail|refused|watchdog>`; `/austatus` `guard`; the
   row shows "Bluetooth paused for the update".
7. Users: the "Pause Bluetooth and check" button (G) and the shedding of section 8.
8. Stack deinit/reinit has never run on ESP32-S3 and only three cycles on classic: W4 benches 20 forced cycles per
   board family with heap readings before the guard is enabled on S3.

## 8. Low heap on automatic checks

### 8.1 Today and planned

There is no night window for checks (section 1, Scheduler): the automatic check runs every 22-26 h at any time and
not while a phone is ready. With `auto` blocked (D2), nothing installs at night. The shedding therefore applies to
**every check** (automatic or manual), in steps, re-checking the gate after each step and restoring everything after
the job; each step logs `[AU];shed;<step>`:

1. BLE pause (exists, classic ESP32).
2. Phone connected: BLE guard (section 7), which drops the link for the job -- on a manual "Pause Bluetooth and
   check" and on the automatic check (D16).
3. Services that the measurements (9) show as costly and that exist in the build (candidates: KISS TCP, net
   console).
4. Still `lowheap`: `[AU];shed;fail;free;..;iblk;..`, attempt used, state H.

**D16 in the scheduler:** today `fwTick` starts no check while `isPhoneReady == 1`, so a node with McApp connected
around the clock never checks. New: when a check is due and the phone has stayed connected for 30 min past the due
time, the loop sets the BLE guard and starts the check (and a manual-style download only when the operator clicks
Install -- automatic downloads stay compiled out, D14); one guarded check per 22-26 h period, logged
`[AU];bleguard;on;phone;1;reason;due`.

### 8.2 INSTRUMENT-only test commands (compiled out of release builds, RAM only)

| Command                     | Effect                                                                     |
| --------------------------- | -------------------------------------------------------------------------- |
| `--update heapgate <bytes>` | raises the free-heap threshold of the gate, forcing `lowheap` and shedding |
| `--update checknow`         | starts the automatic check path now (not the manual one), with shedding    |
| `--update forget`           | clears `fwstage/last`, so a tag can be offered again on the bench          |

## 9. Heap measurements (W3, DK5EN-92 T-Beam)

INSTRUMENT build, `[AU];heap;free;..;iblk;..;min;..` at idle and at the TLS peak; one service changed per boot;
three runs each; only services present in `ttgo_tbeam`.

| #   | Configuration                                  |
| --- | ---------------------------------------------- |
| M0  | WiFi, web server, BLE advertising, no phone    |
| M1  | phone connected (McApp)                        |
| M2  | BLE paused by AU                               |
| M3  | KISS TCP on, without and with one client       |
| M4  | net console 2323 with one session              |
| M5  | Info page open (polling `/austatus` every 1 s) |
| M6  | gateway with server link, ext UDP on           |

## 10. Tests, bench and waves

### 10.1 Host tests (W1 gate)

- `test/test_au_ui_state` (Unity, registered in `platformio.ini` `[env:native]` `test_filter`): every state and the
  priority of 4.1, the error table 4.3 per job, `etaS`, golden `auStatusJson()` strings per state.
- `tools/webgui_au_test.js` (jsdom, added to `tools/regression.sh` stage 2): reads the **same golden JSON strings**
  from the Unity test file as fake `/austatus` bodies (the `cppStrings()` pattern of `webgui_rm_test.js:456-460`),
  so a C++ key change breaks the JS test; transitions A->B->C->D->D2->E->F->A, G->G2 without reload, `/ota/state`
  branch, `sessionStorage` restore, double-click guard, polling stops on page change, no `confirm()`.
- `test_fw_update`: `fwEffectiveMode()`, `fwAutoSkipReason()`, `fwErrConsumesAttempt("lowheap_phone")`, and the D14
  fence: `fwTick` with mode 2 inside the window, staged image, every gate open, never returns `FW_HANDOVER`; the
  guarded check of D16 (due + 30 min with a phone).
- Source pin test (pytest, stage 2): the automatic download start and the `FW_HANDOVER` branch of `esp32_main.cpp`
  are inside `#if AU_AUTO_INSTALL`, and `fw_update.h` carries the `#error` fence; a build with
  `-DAU_AUTO_INSTALL=1` must fail.
- `test_esp32_flash_lifecycle`: sanitizer 0 -> 1, 2 stays 2.
- `wifi_start_gate.h` truth-table test (D15).
- `test_web_guard`: `upd*` without `X-MC` refused, `/austatus` without CORS header.
- Updated, not deleted: `test_command_setters` (`:656`, `:674`, `:702`, `:1390-1401`), `test_rm_format` (`:502`),
  `test_config_json` (`:776-808`, plus an import of 0).
- `tools/au_image_scan.sh` (`strings` on the ELF, as `bench_suite.py:143`): release ESP32 images contain
  `austatus`, `[AU];auto;skip;`, `[AU];auto;blocked;notify only`, `off removed, use notify`; contain no
  `stagelan`, `heapgate`, `checknow`, `forget`, `>off</option>`; `wiscore_rak4631` contains no `[AU]` and no
  `austatus`. INSTRUMENT images contain `heapgate`, `checknow`, `forget`. `[AU];bleguard` is expected from W4 on.
- Gate: `tools/regression.sh --stage 1,2`; builds `heltec_wifi_lora_32_V3`, `ttgo_tbeam`, `t_deck_plus`,
  `wiscore_rak4631`.

### 10.2 Bench (Chrome only: root page, nav, drawers opened by click; never DK5EN-98)

The bench nodes already run v4.40a.10.18, and the reinstall guard blocks it, so every run starts like this:
INSTRUMENT build of the wave, packed with `tools/make_autoupdate.py`, staged with `--update stagelan` under the tag
`v4.40a.10.17`, installed with "Install now" (this already exercises E and F on the new GUI), then
`--update forget`; now "Check now" finds the real Latest v4.40a.10.18.

| Run | Node(s)            | What                                                                                                                                                                                                                                                                                                           |
| --- | ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W2a | DK5EN-1            | A -> B -> C -> D (% rising) -> D2 -> E -> F (% ticking) -> A on v4.40a.10.18; settings unchanged in the page                                                                                                                                                                                                   |
| W2b | DK5EN-1            | Setup drawer opened by click: `notify (prod)`, `notify (dev)`, `auto (coming soon)` greyed with the note; a console `auto` shows greyed and selected                                                                                                                                                           |
| W2c | DK5EN-92, DK5EN-14 | as W2a; T-Beam is the tight-heap case                                                                                                                                                                                                                                                                          |
| W2d | DK5EN-1            | a forced Safeboot apply failure (INSTRUMENT: corrupt staged record) -> F reads `/ota/state`, shows "Install failed -- returns in N s", node back on old tag                                                                                                                                                    |
| W4a | DK5EN-92, -1, -14  | BLE guard with McApp (`http://mcapp.local` in Chrome): McApp connects to the node (moved off DK5EN-98), `--update heapgate` forces `lowheap_phone` -> G -> "Pause Bluetooth and check" -> McApp loses the link and stays off during check and download -> reconnects after; G2 after a manual phone disconnect |
| W4b | DK5EN-92, DK5EN-1  | 20 forced guard cycles per board family with heap readings; watchdog release with a held job                                                                                                                                                                                                                   |
| W4c | DK5EN-90 (RAK)     | McApp connected through a web session: nRF52 BLE and web page as before, no AU row                                                                                                                                                                                                                             |

At the end McApp is reconnected to DK5EN-98 and every node runs the release with mode `notify`.

### 10.3 Waves and ownership

| Wave    | Agent           | Exclusive files                                                                                                                                                                                                                                                                                                                                              | Model / effort     |
| ------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------ |
| W1a     | implementer     | new `src/web_functions/au_ui_state.h` (states, priority, error table, `auStatusJson()`), new `test/test_au_ui_state/`                                                                                                                                                                                                                                        | Sonnet 5.5, medium |
| W1b     | implementer     | `src/web_functions/web_functions.cpp`, `web_nodefunctioncalls.cpp`, `web_setup.cpp`, `web_guard.h`, `test/test_web_guard/`, new `tools/webgui_au_test.js`                                                                                                                                                                                                    | Sonnet 5.5, medium |
| W1c     | implementer     | `src/fw_update.h`, `src/esp32/esp32_main.cpp`, `src/command_functions.cpp`, `src/meshcom_settings.h`, `src/esp32/esp32_flash.cpp`, new `src/wifi_start_gate.h`, `src/rm_exec_ext.h`, `src/esp32/rm_au_esp32.cpp`, `test/test_fw_update/`, `test/test_command_setters/`, `test/test_rm_format/`, `test/test_config_json/`, `test/test_esp32_flash_lifecycle/` | Sonnet 5.5, medium |
| W1d     | implementer     | `src/esp32/fw_update_net.{h,cpp}` (`stagedIlen`, `lowheap_phone`, `fwNetHeapNow()`, STAGELAN `zlen`, `--update forget`/`heapgate`/`checknow` handlers' net side)                                                                                                                                                                                             | Sonnet 5.5, medium |
| Hotspot | orchestrator    | `platformio.ini` (test filter), `tools/regression.sh`, `docs/test-suite-map.md`, new `tools/au_image_scan.sh`                                                                                                                                                                                                                                                | --                 |
| Gate    | orchestrator    | 10.1                                                                                                                                                                                                                                                                                                                                                         | --                 |
| Advisor | general-purpose | W1 diff against this concept                                                                                                                                                                                                                                                                                                                                 | Opus 5.5, medium   |
| W2      | orchestrator    | 10.2 W2a-d                                                                                                                                                                                                                                                                                                                                                   | --                 |
| W3      | orchestrator    | section 9                                                                                                                                                                                                                                                                                                                                                    | --                 |
| W4      | implementer     | BLE guard (7), shedding (8), files mapped after W3                                                                                                                                                                                                                                                                                                           | Sonnet 5.5, medium |
| W4 gate | orchestrator    | 10.1 + 10.2 W4a-c, advisor                                                                                                                                                                                                                                                                                                                                   | Opus 5.5, medium   |

The W1 briefs fix the interfaces between the four writers: `au_ui_state.h` (W1a) is read by W1b; `fwEffectiveMode`,
`fwAutoSkipReason` (W1c) and `FwNetStatus.stagedIlen`, `fwNetHeapNow()` (W1d) are read by W1a's producer through
plain struct fields. Estimate W1 with advisor about 40 min and about $10; W2 about 1 h; W3 about 1 h.

## 11. Review record

32 confirmed findings, all built in above; 6 refuted claims. Full verdict with evidence:
`campaign-au-webgui-ux-20261018-review-verdict.md` (next to this file). The main corrections:

| Finding                                                                                           | Where now     |
| ------------------------------------------------------------------------------------------------- | ------------- |
| `updapply` cannot answer before the reboot, and answered OKAY on failure                          | 5.2, 6.1      |
| Safeboot keeps a failed node 180 s and serves `/ota/state`                                        | 6.1, 6.2      |
| Live heap reading wrong on classic ESP32                                                          | D13, 4.1, 5.5 |
| Priority hid a staged image and failed downloads                                                  | 4.1           |
| Error table incomplete and job-dependent                                                          | 4.3           |
| `/callfunction` open to foreign pages (CSRF); CORS on `/austatus`; X-MC is a request header       | 5.1, 5.2      |
| No night path; auto block must not starve checks                                                  | 5.3, 8.1, D16 |
| BLE: advertising restarts on disconnect, separate pause per job, watchdog too short, S3 unbenched | 7             |
| Mode default, migration hook, web setters still set auto, nRF52 guards                            | 5.3, D14, D15 |
| No C++/JS contract test, tests not registered, bench recipe impossible, breaking tests            | 10.1, 10.2    |
| Wave ownership gaps                                                                               | 10.3          |

## 12. Campaign status (orchestrator, updated per wave)

| Wave | State                                                                                              | Commit     |
| ---- | -------------------------------------------------------------------------------------------------- | ---------- |
| W1   | done: host gate, 5 board builds + image scan, INSTRUMENT build, 2 advisor passes                   | `581f8a0a` |
| W2   | done: bench DK5EN-1/-14/-92 (Chrome), findings fixed                                               | `5a5fc7f7` |
| W3   | done: T-Beam heap matrix, AU-17 opened                                                             | `5a5fc7f7` |
| W4   | done: BLE guard (classic ESP32 only), shedding, benches W4a-c; bench nodes restored to the release | `64cd365d` |

W1 deviations from sections 5-10:

- Sections 5.2/5.3: the X-MC gate also covers every `/setparam` value that carries an update command and the names
  `aumode`/`autoupdate`/`updchan` (advisor finding: `manualcommand` was not the only path into `commandAction()`).
- `/austatus` carries two keys beyond 5.1, appended outside `auStatusJson()`: `up` (seconds since boot) and `img` (8 hex of
  the app ELF SHA-256). The F flow of 6.1 uses them to tell a restart from a fallback to the old image when `run` is `local`
  (bench builds have no `MC_BUILD_TAG`).
- `FwNetStatus.progLen` is separate from `zlen` (STAGELAN no longer clobbers the download size).
- `--update heapgate|forget|checknow` are wired (INSTRUMENT only); `updcheck=ble` answers FAIL until W4; `FW_CHECK_GUARDED`
  logs `[AU];bleguard;pending` and runs the check unguarded until W4 (`TODO(W4)`).
- Image scan needles are the log tags (`[AU];heapgate;`, ...), not bare words (`forget` is an RM page string).
- Known flake, not ours: `tools/bench/test_ota_abort.py::StallScenarioTest` fails 1 in ~5 runs alone (timing, no safeboot change).
- Bench recipe: export `MC_BUILD_TAG` in the INSTRUMENT build, otherwise `run` reads `local`.

### W2 bench result (2026-10-18, Chrome only, INSTRUMENT builds of 581f8a0a, tag v4.40a.10.17/.19 via `MC_BUILD_TAG`)

| Run | Node     | Result                                                                                                                                                                                     |
| --- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| W2a | DK5EN-1  | A, C (real GitHub check), D (0/10/44 %), D2, E, F (14/50/89 %) all render; staged v4.40a.10.19 via `--update stagelan` -> E -> F -> reload on the new tag with `/austatus` answering       |
| W2b | DK5EN-1  | select offers notify (prod), notify (dev), `auto (coming soon)` disabled; console `auto` shows greyed + selected, row says "mode auto (notify only)"; web `autoupdate=auto` refused (rc 1) |
| W2c | DK5EN-14 | C, D 17 %, E, F, then back on the release v4.40a.10.18 (no `/austatus`): page reloaded by itself (after the F-flow fix below)                                                              |
| W2c | DK5EN-92 | H on every real check (`lowheap_ble_off`, also with net console off, also on the previous commit: pre-existing); stagelan path F -> A works                                                |
| W2d | DK5EN-1  | corrupt stream: F follows `/ota/state` ("Install failed ..."); the node stays in Safeboot (app invalid, `fallback_in_ms` -1) and needs a Webflasher upload                                 |

Findings, all from hardware:

1. F flow hung at 95 % when the node came back on a firmware without `/austatus` (Latest 10.18 predates this GUI): fixed (HTML scaffold answer counts as "node is back").
2. A stale remembered click for another tag overlaid "Install failed" on a freshly staged image: fix in flight.
3. Safeboot `apply_failed` with `app_valid:false` and `fallback_in_ms:-1` stays in Safeboot; the page claimed "returns in 0 s": fix in flight. A failed inflate after the erase destroys the single app slot (no fallback); only failures before the write fall back.
4. T-Beam: the gate refuses with BLE paused, no phone, console off, on the previous commit too (pause leaves 67-72 KB, gate wants 76000 B). The concept's "80-85 KB paused" does not hold on this build; "Reboot the node and try again" does not help there. W3 measures, W4 shedding addresses it.
5. Manual check cooldown (60 s) shows "The node did not start the job, try again in a minute" after a second click: works as designed.

### W3 heap measurements (DK5EN-92, T-Beam v1.2, `ttgo_tbeam` INSTRUMENT, 2026-10-18)

Net console (2323) as the log channel; `[AU];heap;<where>;free;..;iblk;..;min;..` lines from the new INSTRUMENT logging
(`auHeapLine`, sampler in `pumpBody`). Free = internal 8-bit heap at the gate, BLE already paused, no phone. Raw data:
`tools/bench/runs/harness/au-w3-tbeam-matrix.txt`.

| Config                                  | Gate free (3 runs) | Largest block  | Verdict |
| --------------------------------------- | ------------------ | -------------- | ------- |
| M0 base (WiFi, web, gateway, 1 console) | 67.9 / 69.0 / 70.6 | 24.6 - 38.9 KB | refuse  |
| M3 KISS TCP on                          | 67.6 / 68.5 / 68.5 | 25.6 - 28.7 KB | refuse  |
| M3b KISS TCP + TX on                    | 66.7 / 67.1 / 68.2 | 32.8 - 34.8 KB | refuse  |
| M6 gateway off                          | 66.8 / 68.0 / 69.7 | 20.5 - 38.9 KB | refuse  |
| M5 page polling `/austatus` 1 s         | 66.0 / 66.8 / 66.8 | 21.5 - 31.7 KB | refuse  |
| M4 two console sessions                 | 59.8 / 66.1 / 66.6 | 28.7 - 34.8 KB | refuse  |

- The gate wants 76000 B free and a 17000 B block: the block is fine, the free heap is 5-10 KB short in every
  configuration, and it drifts down about 0.1-0.3 KB per check.
- Cost per service (vs M0): KISS TCP 1-2 KB, KISS TX about 1 KB, page polling 1-2 KB, one extra console session up to
  8 KB, gateway off no measurable gain. Shedding services cannot close the gap on this board.
- BLE: up, idle free is about 60 KB; paused 67-72 KB (the pause frees 7-12 KB, not the 23 KB of the concept);
  after the resume 41-46 KB.
- TLS peak (gate lowered to 30000 B with `--update heapgate`): the check dips to **9.4-10.1 KB** free (sampler
  `lowest` 21 KB; the sampler is coarse); the DOWNLOAD (second TLS connection to the asset host) **reboots the node**
  right after the gate (heap exhaustion). The 76000 B gate is therefore justified; the concept's "80-85 KB paused" and
  "every update passes" for the T-Beam do not hold on this build. A previous commit (85b0c457) shows the same refusal.
- Consequence: T-Beam (classic ESP32, no PSRAM) cannot run the TLS update at this RAM level; BACKLOG AU-17. W4 still
  builds the BLE guard for phones and the shedding steps, which help boards that miss the gate by a few KB.
- INSTRUMENT `--update heapgate <bytes>` now sets (not only raises) the threshold, for such measurements.

### W4 result (BLE guard, shedding; 2026-10-18)

Code: guard state machine as pure functions in `src/fw_update.h` (`fwGuardStep` and friends, host-tested), force pause,
chain CHECK -> DOWNLOAD under one guard, watchdog 40 min, D16 guarded check, shedding (KISS first, net console only
when the 4 KB estimate can close the gap), `updcheck?ble`, INSTRUMENT `--update bleguardcycle <n>`. One advisor pass (5 items).

| Run | Node                        | Result                                                                                                                                                                                                |
| --- | --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| W4b | DK5EN-92 (classic ESP32)    | 20 forced guard cycles: every `resume;1`, no reset, free heap 60428 -> 59448 (about 50 B per cycle)                                                                                                   |
| W4b | DK5EN-1 (ESP32-S3)          | **panic** on the 2nd forced NimBLE deinit/reinit (`InstrFetchProhibited`, PC 0, in `NimBLEDevice::host_task` -> `nimble_port_run()`), no phone linked                                                 |
| W4a | DK5EN-92 + BLE client (Mac) | `phone:1` -> D13 hint on page open -> Check now -> G -> "Pause Bluetooth and check" -> row "Bluetooth paused for the update" -> link dropped, check passed the gate (C), BLE back, client reconnected |
| W4c | DK5EN-90 (RAK4631)          | flashed by serial DFU; Info page unchanged, no AU row, no `austatus` string in the page                                                                                                               |

- Decision: spec 7.8 held. The force pause is compiled only where `AU_BLE_PAUSE` is 1 (classic ESP32). On the S3 boards
  `auBleGuardRequest()` answers false, D16 runs the plain check (S3 boards have the heap), `bleguardcycle` answers
  "classic ESP32 only". Verified on DK5EN-1 after the change: no panic, `updcheck=ble` refused, `guard` 0.
- The guard gave the T-Beam more headroom than the normal pause: with the phone link released the check passed the
  76000 B gate there, where every phone-less check of W3 refused. AU-17 stays open for phone-less nodes.
- Not benched: McApp itself (`mcapp.local` not reachable from the bench host); a BLE client script
  (`tools/bench/ble_cycle.py`) stood in for the phone. Reconnect attempts during the guard were not exercised (the
  client was holding its link when the guard dropped it).

### Bench restore (2026-10-18)

DK5EN-1 (real AU path to the release), DK5EN-14 (already), DK5EN-92 (Webflasher, `ttgo_tbeam` built from tag v4.40a.10.18:
the node cannot take the update itself, AU-17) and DK5EN-90 (serial DFU from the tag build) run v4.40a.10.18 with mode
`notify`, channel `dev`; DK5EN-1 net console back off. DK5EN-98 was never touched.
