> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.18`: `v4.40a.10.17` plus a position filter for ESP32 GPS boards (a still node no longer hops between grid cells on the air), three fixes for the network debug console, and two new remote-management commands, `net` and `upd` (read the network state, check for and install a release over LoRa).** Like `v4.40a.10.17` it is the official `v4.40a.hbserver` plus everything the fork has built on top: one central gateway server table for ESP32 and nRF52, the network mode `--setnet auto|inet|hamnet`, a static-IP fix for the RAK4631 Ethernet recovery, and the fork features of `v4.40a.10.11` (Auto Update, store-and-forward from the server, authenticated remote management over LoRa with its web page). It is the fork's `fork-dev` branch on top of the official `v4.40a.hbserver` and **not** byte-identical to it. The design of the server table is in [`docs/gw-servers-unification.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/gw-servers-unification.md), the bench run in [`docs/campaign-gw-net-matrix.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/campaign-gw-net-matrix.md). The server table, the network mode and the static-IP fix ([icssw-org/MeshCom-Firmware#1213](https://github.com/icssw-org/MeshCom-Firmware/pull/1213)) and the T-Deck reply button and web firmware column ([#1214](https://github.com/icssw-org/MeshCom-Firmware/pull/1214)) were merged into upstream `dev` on 2026-10-18, after this release was tagged; no official release contains them yet. The position filter is specified in [`docs/gps-11-position-kalman-filter.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/gps-11-position-kalman-filter.md) with the bench run in [`docs/bench-gps11-dk5en1-20261017.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/bench-gps11-dk5en1-20261017.md); the remote commands in [`docs/campaign-rm1-net-upd.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/campaign-rm1-net-upd.md). The Auto Update fix of `v4.40a.10.17` is documented in [`docs/campaign-au-psram-tls-20261016.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/campaign-au-psram-tls-20261016.md).

**Built by CI.** The images were built and published by the GitHub workflow `MeshCom CI` from this tag. 66 assets: 27 ESP32 application `.bin`, the three nRF52 boards as `.uf2` and DFU `.zip`, six support files, and 27 `.autoupdate` files (the zlib twin of each ESP32 image, used by Auto Update). The [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) serves this release.

**How to tell builds apart:** the version field on the air still reads `4.40a`. `--info` prints `...AU env=<board> tag=v4.40a.10.18` and the flash stamp `FLASH_VERSION 20261018`. `FLASH_STRUCT_VERSION` stays at `20260724`, **your configuration survives the update from `4.40a`/`4.35v`**.

**Safeboot:** unchanged since `v4.40a.10.05`. A node that already runs it can take this release over the air, including through Auto Update on the dev channel.

**Auto Update channels:** the `prod` channel reads the latest icssw-org release, the `dev` channel (`--updchan dev`) reads the **Latest** release of this fork, which is this one.

## New since `v4.40a.10.17`

1. **GPS position filter (GPS-11), on by default on ESP32 GPS boards.** A still node used to send the raw GPS fix, so multipath noise of 10-40 m moved its on-air position between `DDMM.MM` grid cells many times an hour. The fix now goes through an HDOP-weighted filter before it is stored and sent: with `--track off` a 30-minute average while the node is still, with automatic motion detection (10 s of speed evidence or a displacement beyond a 40-60 m radius) that switches to a constant-velocity model; with `--track on` a constant-velocity model fed by the GPS Doppler speed, with a zero-velocity update. While still, a 0.3-cell hysteresis holds the on-air grid cell. A manual position set resets the filter only when it changes the position. `--gpsfilter off` restores the old path exactly (stored position and APRS text are bit-identical, host-tested); the switch is runtime only and not saved, so the filter is on again after a reboot. `--gpsdebug on` prints one line per fix with raw, filtered and held position, speed and course. RAK4631 and T-Deck Pro read GPS on their own paths and are not filtered.
2. **Network debug console (port 2323) fixes.** The console now also starts in pure WiFi AP mode (P-0111). `--netconsole off` closes the listener even when the console is the only network service (P-0169). A client that vanished without closing its connection no longer blocks every new login until a power cycle: new connections are accepted while a session is up, a successful HMAC login takes over the old session, a failed one leaves it alone, and TCP keepalive (60/10/3 s) drops dead peers (P-0168).
3. **Remote management: `net` and `upd` (RM1).** `net` answers MTU, MeshCom server, configured and effective network (Internet or Hamnet), on ESP32 and nRF52. `upd` reads the Auto Update state; `upd check` starts a check and `upd go <tag>` installs the reported or staged release and hands over to Safeboot (at least 8 s after the reply). `upd` works whatever `--autoupdate` is set to, and keeps every Auto Update gate except the night window (valid clock, no phone connected, battery floor, Safeboot able to apply). Boards without Auto Update answer `err unsupported`. `status` gains `rs=0|1` (strict 10 s spacing enforced or not). The remote web page shows `net` and `upd` in the Radio card, with Check and a two-tap Update button. The age of the last check (`ck=`) is now right on nodes whose system clock was never set (GPS clock), which the bench run found.

## Already in `v4.40a.10.17` (new since `v4.40a.10.16`)

1. **Auto Update works on PSRAM boards (AU-13).** The prebuilt mbedTLS allocated internal RAM only; on the T-Deck Plus a TLS handshake took about 56 KB of the 57-84 KB internal heap while 4.6 MB PSRAM sat unused, so the update check failed (`[AU];tls;-32512` in the field, `tls -1` and a WiFi drop on our bench) right after boot. mbedTLS now allocates from PSRAM first (internal fallback) on boards built with `BOARD_HAS_PSRAM`; `[AU];tlsalloc;psram` marks it. The heap gate and the `[AU];heap` line now measure internal RAM, which they did not before (PSRAM was counted, so logs looked healthy).
2. **`--via` validates the list before storing it (VIA-01).** Any string used to be accepted; a field node set `DK8VW-P`, no relay matched the resulting path and nothing was meshed. Each comma token must now be a valid callsign with a numeric SSID 0..99 without a leading zero; empty tokens, `*`, `H`, `HG` and lists over 39 characters are rejected with `[ERR]..Via <...> not valid`, and the stored value stays unchanged. `NONE` or an empty value still clear it. The web `setvia` field no longer reports FAIL for lowercase input or `none`.
3. **T-Deck: DM reply button (TD-21).** Every incoming bubble gets a reply button that puts the sender into "To" and opens the compose tab. Opening compose from a conversation tab prefills "To" with that callsign or group; a typed value is never overwritten.
4. **Web GUI: firmware version per station.** The Path Information table gets a Firmware column and every MHeard card a Firmware field (for example `v4.40a`), learned from every decoded frame, direct or relayed. Costs 2 B RAM per neighbour row.
5. **Clean-up:** a duplicated flag check before the firmware note in `OnRxDone` is gone (no behaviour change).

## Already in `v4.40a.10.16` (new since `v4.40a.10.11`)

1. **One central gateway server table (`src/gw_servers.cpp`).** ESP32 and nRF52 (dynamic and static IP) now pick the MeshCom server and the time server from the same table `GW_SERVERS[]`, one row per country code, with an Internet and a Hamnet side each. Before, the choice was spread over three hand-written copies that had drifted apart. Rows: OE, DL, IT, HC (Hamcloud), HB (Switzerland, new upstream in #1209-#1212, now also on nRF52); France and USA are prepared as commented rows (no addresses yet). Named behaviour changes:
   - DL is served on the Internet by `meshcom.hamnet.network` only; DL on the Hamnet goes to the Hamcloud host (`44.148.230.197`), as the DL server operator asked for Internet-only.
   - HC on the Internet goes to the DL server; IT with Hamnet forced keeps the Italian Internet server (Italy has no Hamnet server).
   - nRF52 now selects DL and HC like the ESP32 does, and a static nRF52 node gets HC and HB as well (before only OE, DL, IT).
   - The Hamnet time server is taken per server row; `--setownntp` now applies to DHCP nodes too.
2. **Network mode `--setnet auto|inet|hamnet`.** `node_honly` becomes a 4-state value: 0 unset (old behaviour per path), 1 Hamnet forced, 2 auto (own address `44.x` → Hamnet, otherwise Internet), 3 Internet forced. **Auto is the factory default for new or wiped nodes only**; existing nodes keep their stored value and behave as before. Bare `--setnet` prints the mode and the network in use. `--setinet on|off|auto` and `--sethamnet on|off` stay as aliases. `--gateway srv` validates against the table and its help lists the table's codes. The `NET MODE` line in `--info` (gateway mode only) shows the same, and the time server on the web info page follows the same selection.
3. **Static IP on the RAK4631 stays static (ETH-05).** A RAK4631 with a configured static address used to fall back to DHCP during Ethernet recovery. Recovery now re-applies the static configuration (reopen UDP, re-apply without reset, or a hardware reset with a 5-minute gap), and after a W5100S hardware reset it waits for the PHY before it reconfigures.
4. **Console and logs.** ESP32 boards with a UART console (USB-UART bridge; native-USB CDC boards excluded) get a 2048 B serial transmit ring, so debug prints no longer block the receive path at 115200 baud (Heltec V3 with all debug streams on: OnRxDone p50 77.9 ms → 4.4 ms; costs about 2.4 kB heap). The `--setlog` STAT line reports OnRxDone timing (avg/p95/p99/max) and counts every exit.
5. **Upstream merged:** official `v4.40a.hbserver` (#1209-#1212: HB and HC servers, HB server for nRF52, esptool upload for E213/E290/Wireless Paper). The fork's temporary OE Internet fix is reverted; the table carries OE.

## Already in `v4.40a.10.11` (compared with the official firmware)

1. **Firmware Auto Update (#1187), ESP32 only, off by default.** `--autoupdate off|notify|auto`, `--updchan prod|dev`, `--update check|install|status|apply`. Daily TLS check against GitHub, compressed `<board>.autoupdate` image, SHA-256 and CRC verified, applied by the versioned Safeboot between 03:00 and 05:00. Classic ESP32 TLS heap gate (`lowheap`).
2. **Store-and-forward for PMs from the central server (#1188).** `--stor on|off`, **off by default**.
3. **Authenticated remote management over LoRa (#1189), off by default.** `--remotemgmt on` plus `--passwd`; `RM1` DMs with a 64-bit HMAC-SHA256 tag run allowlisted commands (extended set incl. `radio`, `name`, `pos`, `mh`, `txq`, `led`, `chgled`), gateway relay through the server, lockout per sender callsign. Remote page and settings cards in the web GUI, request guard `MC_WEB_GUARD`.
4. **Auto Update asset and Safeboot:** `<board>.autoupdate`, versioned Safeboot, AU locked while Safeboot is too old.

Everything else of `v4.40a.10.11` (MTU, time zones, BLE reconnect hardening, ETH-04, LED work, HTML escaping, small web features) is in the official firmware since #1207/#1208.

## What changes on the air

With default settings, compared with the official `v4.40a.hbserver`:

1. Nothing new is transmitted by default: Auto Update, STOR and remote management are off.
2. A **gateway** with server code DL or HC, or an nRF52 gateway with a static address, may talk to a different server host than before (see the server table item above). Non-gateway nodes are not affected.
3. **T-Deck:** composing a message while a callsign or group conversation tab is open now sends a DM or group message to that tab; before, the empty "To" field sent it to `*`.
4. **ESP32 boards with GPS** send a filtered position: a still node stays on one `DDMM.MM` grid cell instead of following the multipath noise, and a moving node follows a smoothed track. `--gpsfilter off` gives the old raw position until the next reboot.
5. A node with `--remotemgmt on` and a password treats a LoRa DM to its own call that starts with `RM1 ` as a command; a gateway uploads its own HEY echoes and RM1 command messages to the server (as in `v4.40a.10.11`).

## Supported Hardware

### Verification for this release

- **Host suite:** 2521 host test cases in 72 native environments, all green, plus the golden selftest (48 commands) and the host tool suites (pytest 1100).
- **Server selection:** a golden comparison of the old and the new code over 700 grid rows (code × own address × mode × platform × own NTP); every differing row is a named, intended change.
- **Build:** the release environments are built by CI (27 ESP32 images, 3 nRF52 images, Safeboot).
- **Bench-tested for this release (2026-10-17), new items:**
  - **GPS filter on Heltec V3 (`DK5EN-1`, external u-blox, indoor with strong multipath):** 2 h stationary with `--track off` and 2 h with `--track on`, no reboot, no rejected sample. The held on-air position changed cell 11 times in 2 h against 119 for the raw fix (51 % of the time on the modal cell, 85 % before one false motion trigger caused by a 30 s multipath step of about 30 m, which the filter followed). Stationary speed noise max 0.44 m/s at 3 s fixes. No drive test.
  - **Net console on Heltec V3 (`DK5EN-1`):** before/after runs for P-0168 (dead session blocks new logins) and P-0169 (`--netconsole off` leaves the listener open); both fixed on the bench.
  - **RM1 `net` / `upd` over LoRa** (`DK5EN-98` sending to `DK5EN-1`, 2 dBm): `status` with `rs=0`/`rs=1`, `net`, `upd`, `upd check`, `upd go` with a stale and with the current tag; a real `upd go v4.40a.10.17` of a staged release handed over to Safeboot and came back on the release.
  - **`--via` validation on Heltec V3 (`DK5EN-1`):** USB console and web `setvia`, every reject, accept and clear case passes.
- **Bench-tested for `v4.40a.10.17` (2026-10-16/17), unchanged since:**
  - **AU-13 on T-Deck Plus (`DK5EN-14`):** 10 of 10 dev-channel checks right after reboot pass (no allocation failure, no WiFi drop, internal heap minimum 55.7 KB, was 2.7 KB); `--update install` downloaded and staged a 1.57 MB image in 22 s. Control runs on T-Beam v1.2 (`DK5EN-92`, classic ESP32: check and download pass, internal minimum 11.6 KB during the download) and Heltec V3 (`DK5EN-1`, minimum 25.3 KB).
  - **TD-21 on T-Deck Plus (`DK5EN-14`):** reply button and "To" prefill checked by hand.
- **Bench-tested for `v4.40a.10.16` (2026-10-15/16), unchanged since:**
  - **Network matrix on RAK4631 (`DK5EN-90`, Ethernet) and Heltec V3 (`DK5EN-1`, WiFi):** 45 cases per node, 3 networks (DHCP; static via the home router; static via a Linux router that carries the Hamnet VPN) × `auto`/`inet`/`hamnet` × OE/DL/IT/HC/HB. Server choice right in all 90 cases; KEEP, server BEAT and NTP in every case that has a return path (the Linux router NATs only into the Hamnet, so only its Hamnet cases count).
  - **ETH-05 on `DK5EN-90`** (static address, INSTRUMENT build): link drop re-applies the static configuration without DHCP; a stalled second drop escalates to a W5100S hardware reset and comes back with the static address in 1.9 s; inside 5 minutes no second reset.
  - After the run both nodes were left on this firmware as gateway (`DK5EN-1`, OE, BEAT) and non-gateway (`DK5EN-90`) with DHCP.
- **Bench-tested earlier, code paths unchanged in this release:** remote management (2026-10-06/07), Auto Update end to end (2026-10-11), the `v4.40a.10.05`/`10.06` features.

### Built and shipped, not on our bench

The web firmware column is covered by host tests only. The GPS filter was benched stationary on one board only; T-Beam, T-Beam Supreme, T-Deck and the other GPS boards run the same code untested. Everything else, including the network mode and the server table on every other board, ESP32 boards with Ethernet (T-ETH-Elite), the nRF52 boards T114 and T-Echo, and all other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, lora32 v2.1, E213, E290, Wireless Paper, LoRa-APRS boards. `t5_epaper` is not shipped.

## Known gaps, stated plainly

- **Network mode and older firmware:** a configuration exported with `node_honly` 2 or 3 cannot be imported into older firmware (it accepts 0..1 only), and a downgrade with 2 stored behaves like Internet on ESP32/nRF52 DHCP and like Hamnet on a static nRF52.
- **Status line for IT with Hamnet forced:** `--setnet` and `--info` say `in use: hamnet` (the selected network) while the Italian server is reached over the Internet; the `[GW];srv` line shows the real path. Intended.
- **nRF52 uses literal server addresses** (no DNS resolver); the HB address follows a dynamic-DNS name upstream and can go stale.
- **ETH-05 not benched:** cable pull and a real UDP transmit error streak; ESP32 Ethernet boards not benched with the new table.
- **NTP on a node with a GPS fix:** the ESP32 asks NTP only without a GPS fix (GPS time has priority, unchanged behaviour).
- **`--via`:** a bad value already stored before the update survives it (re-enter it), and a JSON configuration import does not validate `node_via`.
- **Firmware column after store-and-forward:** a message delivered from the server's store is re-encoded with the store node's version, so the column can show that version for the sender until the sender's next own frame.
- **T-Beam Supreme:** its 8 MB PSRAM is not enabled by the build, so Auto Update there still uses internal RAM only (as before).
- **AU-13 field confirmation** on the reporting T-Deck Plus is pending.
- **GPS filter:** not drive-tested (lag at the start of a drive, smart-beacon shots while moving are unmeasured); a multipath step of 30-45 m held for 15 s can still be taken for motion and move the on-air position by one cell (accepted rate in the host tests: at most 9 per day). Not on RAK4631 and T-Deck Pro. Smart beaconing with `--track on` still calls a still node stationary on speed alone.
- **Net console:** the AP-mode start (P-0111) and the keepalive drop of a half-open peer are not benched. In pure AP mode the 5-minute offline recovery recreates the AP and drops the console session (will not be fixed).
- **RM1 `upd go`:** the download path (`installing`) is host-tested only; on the bench the reinstall guard already knew the release, so only the staged path ran. `upd go` installs even with `--autoupdate off`; remote management itself stays off by default and needs the password.
- **AU-14:** a classic-ESP32 T-Beam rebooted once (`TASK_WDT`) 13 s into an Auto Update download; the retry passed. Open. **AU-16:** a T-Beam 1W in the field stayed in Safeboot after an Auto Update; cause open, see [`docs/troubleshooting-tbeam-1w-autoupdate.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.18/docs/troubleshooting-tbeam-1w-autoupdate.md).
- **Carried over from `v4.40a.10.11`:** Auto Update on classic ESP32 has a thin heap margin; the prod channel cannot install yet (official releases carry no `.autoupdate`); Auto Update needs the new Safeboot; RM sync limiter and duplicate ring not benched on hardware; gateways upload RM1 command messages to the public server; BLE queued messages go to the next phone; ETH-04 field confirmation pending; relays on older firmware strip the ZWJ in compound emoji.
- **Not in this release:** `t5_epaper`, France and USA servers (no addresses yet).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, board detection built in, includes the Safeboot. Only the images of the latest release are offered there.

Otherwise pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py` (application only, Safeboot unchanged). The `<board>.autoupdate` files are for Auto Update and need no manual handling. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

## Upstream

The gateway server table, the network mode and ETH-05 ([#1213](https://github.com/icssw-org/MeshCom-Firmware/pull/1213)) and the T-Deck reply button and web firmware column ([#1214](https://github.com/icssw-org/MeshCom-Firmware/pull/1214)) are merged into upstream `dev` since 2026-10-18 (not in an official release yet). This release carries the same code. AU-13 and the RM1 `net`/`upd` commands belong to the Auto Update and remote-management code (#1187, #1189); `--via` validation, the GPS filter and the net console fixes are not offered upstream yet. #1187-#1189 (Auto Update, STOR, remote management) remain fork features. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
