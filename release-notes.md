> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.11`: the official `v4.40a` plus everything the fork has built since: Auto Update, store-and-forward from the server, authenticated remote management over LoRa (now with extended commands and a Remote page), MTU setting, BLE reconnect hardening, the web GUI for these, the charge/board LED work, an Ethernet DHCP fix for the RAK4631 and a TLS heap gate for Auto Update on classic ESP32.** It is the fork's `fork-dev` branch on top of the official `v4.40a` and **not** byte-identical to it. The design for #1187-#1191 is in [`docs/concept-open-issues-20261004.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.11/docs/concept-open-issues-20261004.md); the fold-in of eleven upstream commits and the LED work are in [`docs/campaign-upstream-fold-20261011.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.11/docs/campaign-upstream-fold-20261011.md).

**About the earlier tags:** `v4.40a.10.05` and `v4.40a.10.06` exist as tags in this repository, but they are no longer available as GitHub releases. This release supersedes both: everything they contained is described below (section "Already in the tags `v4.40a.10.05` / `v4.40a.10.06`"), and the new work since the `v4.40a.10.06` tag is listed first. The last GitHub release before this one is `v4.40a.10.02`, which is the official `v4.40a` rebuilt without own changes.

**First release built by CI.** The images of this release were built and published by the GitHub workflow `MeshCom CI` (re-enabled on the fork, triggered by the tag), not by hand. 66 assets: 39 (27 ESP32 application `.bin` including `T-ETH-ELITE_1262`, the three nRF52 boards as `.uf2` and DFU `.zip`, six support files) plus 27 `.autoupdate` files (the zlib twin of each ESP32 image, used by Auto Update). The [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) serves the application images of this release.

**How to tell builds apart:** the version field on the air still reads `4.40a`. `--info` prints `...AU env=<board> tag=v4.40a.10.11` and the flash stamp `FLASH_VERSION 20261011`. `FLASH_STRUCT_VERSION` stays at `20260724`, **your configuration survives the update from `4.40a`/`4.35v`**.

**Safeboot:** the Safeboot image is the one of `v4.40a.10.05`/`10.06` (versioned, with the `/ota/state` heap fields and `.bin` / `.bin.zz` uploads). A node that already runs it can take this release over the air, including through Auto Update on the dev channel. A node still on an older Safeboot needs the web flasher or USB once.

**Auto Update channels (read this):** the `prod` channel reads the latest icssw-org release, the `dev` channel (`--updchan dev`) reads the **Latest** release of this fork, which is this one. Every fork release that becomes "Latest" is what dev-channel nodes install.

## New since the `v4.40a.10.06` tag

1. **Remote management, extended (#1189).** Nine new allowlisted commands: `radio`, `name`, `atxt`, `pos`, `sens`, `mh`, `txq`, `mbox`, `maxhop`, plus the LED commands `led on|off|flash` and `chgled auto|on|off` (see LEDs below). Replies are capped (result 108, args 39 bytes), sanitised and escaped in the web page; `status` and `radio` replies carry the TX power as `p=<cur>/<min>/<max>`; the sender clamps `txpower` per target and the GUI words refusals; a valid-tag but blocked command is answered (`err range`, `err blocked`) and costs no strike. `sync` has its own 60 s limiter and a replayed `sync` no longer blocks commands. The nRF52 `radio` reply converts the stored units (Hz, coding-rate index). One command table and one message table replace several copies (review waves 1-3); the RM compatibility layer was removed because remote management never shipped before. The sender budget is 2 commands without a verified reply, 10 after one verified (RM-PROOF: two sends in a row without a verified reply suspend the proven budget). RM1 commands are send-once texts (`addTxRingEntryOnce`, no `{NNN` suffix) and repeat on the PN ladder until a verified reply; the same wrong guess counts as one strike.
2. **Remote page and settings pages in the web GUI.** A Remote page with node tiles (key state, padlock, colour), a key store with three saved nodes, state tiles, heard list as a table with lookup row, TX-queue and mailbox bars, five settings cards with label | input | Set rows, stepper range from the target's minimum power, Actions as a tile grid and switches in one aligned column, Identity and Position cards in the Settings idiom, TX power as a number input with the board range, a request guard (`MC_WEB_GUARD`: default off except for the RM endpoints, same-host Origin or `X-MC` header), a status token and a "Server:" line on the info page (Internet/Hamnet server and whether it answers). Web API: `src/web_functions/Web-API_documentation.txt`.
3. **Remote management over the server path and per callsign.** A gateway relays RM1 command DMs that reach it through the server, and a node accepts them (RM-GWRELAY). Reject counter and lockout run per sender callsign-SSID, never per path. The second copy of one frame is dropped before the check (RM-DUP), so a junk frame costs one strike, not two; the stopped-set holds only live ring slots and is sized to the ring.
4. **LEDs.**
   - **Charge LED (CHG-01..04):** on AXP192/AXP2101 boards the charge LED follows charging unless you configure it (`--charge led on` returns to this automatic mode, `--charge led off` darkens it); the configured state is marked by `node_sset2` bit `0x8000`; the boot initialisation also runs on AXP2101 boards; the BLE config export carries the setting as `CHGLED`. `--gateway pos` and `--netconsole off` no longer clear bit 15.
   - **Board LED flashing** (upstream #1202) and a **LED polarity** guard: LED active-low logic is a build macro (upstream #1195), and the remote LED is never driven on boards that share the pin with OneWire.
   - **RM `led on|off|flash`**, **RM `chgled auto|on|off`** (the override is gone after a reboot) and **web switches** "Light flash" and "Charge LED". `led` works on Heltec V3 (GPIO35) and boards with `BOARD_LED`; **Heltec V4 gets the remote LED too (GPIO35, built, untested)**. T-Beam (OneWire pin) answers `err unsupported` to `led`, Heltec V3 answers it to `chgled`.
   - **Upstream fold-in:** eleven commits from icssw-org/dev were cherry-picked: #1193 (duplicate RadioLib build flags removed, sorted), #1195 (LED active-low macro), #1196 (charge indicator LED), #1202 (board LED flashing) and their follow-ups; #1200/#1201 (temperature offset) were already in the fork. The T-Deck position view lost a dead hemisphere flip (the real swap was already fixed).
5. **Sensors and inputs.** The configured temperature offsets now apply to AHT20, SHT21 and BME680 (#1200); the web temperature offset accepts a leading zero and a decimal comma (#1201); `--bme|--bmp|--bmx off` clears only the BMP390 bit of `node_sset3`.
6. **HEY echo upload.** A gateway uploads the echoes of its own `HG` HEY to the server, as received, without an own signal report.
7. **Store-and-forward:** DMs to or from service calls are never stored; the `WLNK-1` acknowledgement form is recognised.
8. **Ethernet DHCP on the RAK4631 (ETH-04).** After a reboot or an IP loss the RAK could stay without a lease for hours (seen at DB0AIS): recovery now keeps a held lease and only reopens UDP, escalates to a full DHCP when the reopen saw no UDP RX, hardware-resets the W5100S after two DHCP failures with link up, and seeds the RNG from the SoftDevice. The decision logic is host-tested.
9. **Auto Update:** the release asset is now `<board>.autoupdate` (was `<board>.bin.zz`); Safeboot is versioned and Auto Update stays locked while Safeboot is too old (AU-12); Safeboot reports the real `Update.end()` failure and re-validates the app after an upload; `/ota/state` shows heap values; `ota_abort.py` takes a compressed image. **Classic ESP32 TLS heap gate (AU-D13):** with a phone connected over Bluetooth the TLS handshake used to fail with -9984/-10368 while the free heap fell to 268 B; the check now refuses cleanly with `lowheap` (76 KB total-free gate) and does not use up an attempt.
10. **nRF52 builds:** `heltec_t114` and `t_echo` build with `-Os`, and the gate links every nRF52 board.

## Already in the tags `v4.40a.10.05` / `v4.40a.10.06` (compared with the official `v4.40a`)

1. **Firmware Auto Update (#1187), ESP32 only, off by default.** `--autoupdate off|notify|auto`, `--updchan prod|dev`, `--update check|install|status|apply`, one selector in the web GUI, info row and an "update available" banner. The node checks GitHub once a day over TLS (five built-in root certificates), downloads the compressed image into the free end of its own application slot, verifies SHA-256 and CRC; in `auto` mode it hands over to Safeboot between 03:00 and 05:00 local time, which unpacks and verifies it offline and boots it. Classic ESP32 boards pause Bluetooth for the download (no phone connected).
2. **Store-and-forward for PMs from the central server (#1188).** A store-mode gateway also holds PMs that arrive through the server for calls of its store set, purges them on a server-side `:ack` and uploads its `:sto` notice. `--stor on|off` (**off by default**). Accepted by the server operator on 2026-10-11 and running live.
3. **Authenticated remote management over LoRa (#1189), off by default.** `--remotemgmt on` plus a `--passwd`: a direct message `RM1 <counter> <command> <tag>` with a 64-bit HMAC-SHA256 tag runs one allowlisted command and answers with a tagged reply. Plaintext plus tag, no encryption. Persisted strictly increasing counter, rate limit, lockout after three bad frames. Reference sender `tools/remote_cmd.py`, design `docs/adr-remote-hmac.md`. The console command `--rm` was renamed `--remotemgmt` (setting `node_rm` and web key `rm` unchanged).
4. **MTU setting on every board (#1190).** `--mtu 1280..1500` (alias `--ethmtu`), **new default 1280** for nodes without a stored value; applied to WiFi STA/AP, Ethernet and Safeboot, MTU card in the web GUI. Nodes that already store 1500 keep it.
5. **BLE reconnect hardening (#1191).** No inherited authorisation after a quick reconnect, a wrong-PIN disconnect no longer hits the next phone, disconnect counters in `--info`, advertising self-check every 5 s.
6. **Web:** header reads "Meshcom 4.40a"; the web upload readers stop after an absolute deadline, so a trickling client can no longer stall the node into a watchdog reset.
7. **Since `v4.40a.10.02`, also in those tags:** POSIX time-zone rules (`--settz`, `node_tz`, web field, BLE `TZ`) with the RTC holding UTC; web GUI delivery ticks survive ring churn; Extern-UDP refuses broadcast/multicast targets and `--extudpip` without argument clears the target; T-Deck: ACK status in the bubble, map markers survive a reboot, tiles cached in PSRAM; Poland track frequency per platform; a fresh ESP32 without stored SSID starts its access point; CSMA back-off arithmetic.

## What changes on the air

With default settings, compared with the official `v4.40a`:

1. Nothing new is transmitted by default: Auto Update, STOR and remote management are off.
2. A node with `--remotemgmt on` and a password treats a LoRa DM to its own exact call that starts with `RM1 ` as a command (not shown as chat) and answers with a DM; with RM off such a DM is ordinary text. RM1 DMs go on air once, without `{NNN`, and a sender repeats them on the PN ladder until a verified reply.
3. A store-mode gateway also stores PMs that reach it through the server and uploads its `:sto` notice. A gateway uploads the echoes of its own `HG` HEY. **A gateway also uploads RM1 command messages to the public server** (see the gaps).
4. On AXP192/AXP2101 boards the charge LED now follows charging by default.

## Supported Hardware

### Verification for this release

- **Host suite:** 2371 host test cases in 71 native environments, all green, plus the golden selftest and the host tool suites.
- **Build:** the release environments are built by CI (27 ESP32 images, 3 nRF52 images, Safeboot).
- **Bench-tested for this release (2026-10-11):**
  - **T-Beam v1.2 classic (`DK5EN-92`):** charge LED follows charging after the update, `--charge led off|on` with reboots, RM `chgled on|off|auto` sent from `DK5EN-1`, `led` answers `err unsupported` (OneWire pin); Auto Update CHECK with and without a phone on Bluetooth (phone on: clean `lowheap` refusal, minimum heap 58 KB; phone off: CHECK ok).
  - **Heltec V3 (`DK5EN-1`):** RM `led on`, `led flash`, `led off` (steady, short flash every 2 s, dark), `chgled` answers `err unsupported`.
  - **Auto Update of this very release** on both nodes (dev channel): CHECK, TLS download, staging, Safeboot apply and boot; `--info` reports `tag=v4.40a.10.11`.
- **Bench-tested earlier, code paths unchanged in this release:** the RM extended commands and gateway path on Heltec V3, T-Beam and RAK4631 (2026-10-06/07, RM-DUP not part of it), the `v4.40a.10.05`/`10.06` features (MTU, BLE cycles, Auto Update end to end on Heltec V3, web cards).

### Built and shipped, not on our bench

Everything else, including the ETH-04 DHCP fix as shipped here (benched on `DK5EN-90` during development, not re-run for this release; no field confirmation yet), the Heltec V4 remote LED, the Safeboot changes on every board, and all other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker/T114, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, T-ETH-Elite (including `T-ETH-ELITE_1262`), lora32 v2.1, E213, E290, Wireless Paper, T-Echo, LoRa-APRS boards. `t5_epaper` is not shipped.

## Known gaps, stated plainly

- **Auto Update on classic ESP32: thin heap margin for the download.** Verified after publishing (2026-10-11): `DK5EN-92` (T-Beam, classic) and `DK5EN-1` (Heltec V3) installed this release through Auto Update on the dev channel (TLS download 13 s / 43 s, Safeboot apply, boot). On the T-Beam the download dips to 13,952 B free (about 73.6 KB used), only about 2.4 KB below the 76 KB pre-TLS gate; a busier classic node may refuse with `lowheap` and retry later.
- **Auto Update on the prod channel cannot install yet:** official icssw-org releases carry no `.autoupdate` assets, so a prod node reports "no installable image for this board". The dev channel (this fork) carries them.
- **Auto Update and same-day releases:** the version guard understands `v<VER>.MM.DD` only; a `.N` suffix is invisible to it, so same-day re-releases are dated to the next day.
- **Auto Update needs the new Safeboot**, delivered only by web flasher or USB. With an old Safeboot the first handover is wasted, then automatic handovers stop (`[AU];refuse;old_safeboot`). The classic Safeboot image has about 1.6 KB of headroom.
- **Heltec V4 remote LED:** built, not tested on hardware. Other `BOARD_LED` boards compile only.
- **Remote management:**
  - The sync limiter and the duplicate ring (RM-DUP) were not benched on hardware; RM-GWRELAY was benched against a mock server only.
  - A late echo of a command can still execute after the sender already shows "No answer" (171 s).
  - **Gateways upload RM1 command messages to the public server.** The tag is visible there; the command text is not secret.
  - Three junk `RM1` frames within 90 s lock RM for 5 minutes (fails closed, by design). The key is `node_passwd`, shared with the net console and KISS and exported in clear in the configuration JSON. After a flash wipe on nRF52 change the password (the counter is gone). The web send panel carries the target's password once over plain HTTP; use it only on a trusted network. A rejected command with a wrong password gets no answer. T-Echo and T114 are untested as RM targets.
- **BLE, by design:** messages queued while no phone is connected stay queued and are delivered after the next connect, also to a different phone (BLC-D1, accepted).
- **MTU:** the access-point path, the classic Safeboot and T-ETH are not benched (accepted).
- **ETH-04:** field confirmation at DB0AIS is pending; the router option `always-broadcast=yes` is unanswered.
- **Carried over:** a relay on older firmware strips the ZWJ in compound emoji; multi-hop loss recovery for DM retries (`BACKLOG` PN-01); the central server's masked dedup.
- **Not in this release:** `t5_epaper` (the T5 fixes of earlier cycles are blind, no T5 hardware).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, board detection built in, includes the Safeboot. Only the images of the latest release are offered there.

Otherwise pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py` (application only, Safeboot unchanged). The `<board>.autoupdate` files are for Auto Update and need no manual handling. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

## Upstream

#1187-#1191 are upstream candidates in icssw-org/MeshCom-Firmware; no pull request has been opened for this release. Eleven commits in this release come from upstream `dev`. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
