> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.06`: `v4.40a.10.05` plus the web GUI for the three new features: one Auto Update selector, an MTU card and a remote-management card with a send panel.** Like `v4.40a.10.05` it is the fork's `fork-dev` branch on top of the official `v4.40a` and **not** byte-identical to it. The design for #1187-#1191 is in [`docs/concept-open-issues-20261004.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.06/docs/concept-open-issues-20261004.md).

**How to tell builds apart:** the version field on the air still reads `4.40a`. `--info` prints `...AU env=<board> tag=v4.40a.10.06` and the flash stamp `FLASH_VERSION 20261006`. `FLASH_STRUCT_VERSION` stays at `20260724`, **your configuration survives the update from `4.40a`/`4.35v`**.

**The Safeboot image is unchanged from `v4.40a.10.05`.** A node that already got `v4.40a.10.05` through the web flasher or USB can take this release over the air, including through Auto Update on the dev channel. A node still on an older Safeboot needs the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) or USB once (see `v4.40a.10.05`).

## What is new

1. **Auto Update in the web GUI (#1187), ESP32 only.** The Firmware Update card has one selector: `off`, `prod` (official icssw-org releases) or `dev` (this fork). `prod` and `dev` install automatically in the 03:00-05:00 window. The console mode `--autoupdate notify` stays available and is shown as such.
2. **MTU card (#1190), every board with a network.** Choose 1280, 1400, 1500 or a custom value (1280..1500); the card shows the resulting TCP MSS.
3. **Remote management card (#1189).** Shows this node's state (switch, password set, accepted and rejected commands, lockout, counter, last commands) and a "manage another node" panel: target call, that node's password and one allowlisted command. This node signs the command, sends it as a LoRa DM and shows the reply once its tag verifies. The password goes through its own endpoint (`POST /rmsend`), is never logged or stored, and is held in RAM for 10 minutes to verify the reply. Replies that come back through the server are accepted (they carry the tag); commands still go out over LoRa only. Web API: `/rmsend`, `/rmstatus` (see `src/web_functions/Web-API_documentation.txt`).
4. **Console command renamed: `--rm on|off` is now `--remotemgmt on|off`.** `--rm` no longer exists. The setting itself (`node_rm`) and its value are unchanged, so a node that had RM on keeps it on. The web setparam key stays `rm`.
5. **Smaller fixes:** the web header reads "Meshcom 4.40a" (was "Meshcom 4.0 4.40a"); the web upload readers now stop after an absolute deadline, so a client that trickles its request body can no longer stall the node into a watchdog reset.

## Already in `v4.40a.10.05` (compared with the official `v4.40a`)

1. **Firmware Auto Update (#1187), ESP32 only, off by default.** `--autoupdate off|notify|auto`, `--updchan prod|dev` (prod = icssw-org releases, dev = this fork), `--update check|install|status|apply`, setup card, info row and an "update available" banner in the web GUI. The node checks GitHub once a day over TLS (five built-in root certificates, nothing else), downloads the compressed image (`<board>.bin.zz`, a new asset of every release from now on) into the free end of its own application slot and verifies SHA-256 and CRC; in `auto` mode it hands over to Safeboot between 03:00 and 05:00 local time, which unpacks and verifies the image offline and boots it. Classic ESP32 boards pause Bluetooth for the few seconds of the download (no phone connected) because TLS does not fit next to it otherwise. A node whose Safeboot predates this release is detected after one handover and stops handing over automatically.
2. **Store-and-forward for PMs from the central server (#1188).** A store-mode gateway now also holds PMs that arrive through the server for calls of its store set, purges them on a server-side `:ack`, and uploads its `:sto` notice to the server. New `--stor on|off` (**off by default**, pending the server operator's approval) announces to the server which callsigns this gateway holds PMs for (store set AND heard directly within 12 h, never the own exact call).
3. **Authenticated remote management over LoRa (#1189), off by default.** `--remotemgmt on` (was `--rm on`) plus a `--passwd`: a direct message `RM1 <counter> <command> <tag>` with a 64-bit HMAC-SHA256 tag runs one allowlisted command (`reboot`, `status`, `sendpos`, `sendtrack`, `gps|track|display|gateway|mesh on|off`, `txpower <n>`, `setout <a0..b7> <on|off>`) and answers with a tagged reply. Plaintext plus tag, no encryption. Strictly increasing counter persisted across reboots, rate limit, lockout after three bad frames. Reference sender: `tools/remote_cmd.py`. Web switch, info row and (new in `v4.40a.10.06`) the management card. Design: `docs/adr-remote-hmac.md`.
4. **MTU setting on every board (#1190).** `--mtu 1280..1500` (alias `--ethmtu`), **new default 1280** for nodes without a stored value; applied to WiFi STA/AP, Ethernet and Safeboot, so the web GUI works through HAMNET/VPN tunnels. Nodes that already store 1500 keep it: set `--mtu 1280` if you are behind a tunnel.
5. **BLE reconnect hardening (#1191).** A phone that drops and reconnects between two loop passes no longer inherits the previous session's authorisation; a wrong-PIN disconnect request no longer hits the next phone; disconnect reasons and counters (`--info` `BLE: con= dis= ...`) on both platforms; advertising self-check every 5 s.
6. **Since `v4.40a.10.02`, also in that build:** POSIX time-zone rules (`--settz`, `node_tz`, web field, BLE `TZ`) with the onboard RTC holding UTC; web GUI delivery ticks survive ring churn; Extern-UDP refuses broadcast/multicast targets and `--extudpip` without argument clears the target; T-Deck: ACK status in the message bubble, map markers survive a reboot, decoded tiles cached in PSRAM; Poland track frequency per platform; a fresh ESP32 without stored SSID starts its access point; CSMA back-off arithmetic and the T5 UI fixes (T5 is not in this release).

## What changes on the air

With default settings, compared with `v4.40a` (nothing changes compared with `v4.40a.10.05`):

1. Nothing new is transmitted: Auto Update, STOR and remote management are off by default.
2. A node with `--remotemgmt on` and a password treats a LoRa DM to its own exact call that starts with `RM1 ` as a command (not shown as chat) and answers with a DM; with RM off such a DM is ordinary text.
3. A store-mode gateway now also stores PMs that reach it through the server and uploads its `:sto` notice to the server.

## Supported Hardware

### Verification for this release

- **Host suite:** 1983 test cases across 60 native environments, all green, plus the golden selftest (48 commands) and the host tool suites.
- **Build:** 32 release environments build clean (30 boards and both Safeboot images). `t5_epaper` is not in this release.
- **Bench-tested for `v4.40a.10.06` (2026-10-05):**
  - **Heltec V3 (`DK5EN-1`):** the new web cards: Auto Update selector off/prod/dev with read-back, MTU card, header; remote-management send panel driving `DK5EN-90` over LoRa: `status`, `sync`, `display off`, `display on` all answered with a verified reply (8 sent, 7 answered; the one miss was a single lost frame, its retry was answered); the web RM switch routes through `--remotemgmt`.
  - **RAK4631 (`DK5EN-90`):** `--remotemgmt on|off` and its help line on the console; executed all commands from the panel, counters and command log in `/rmstatus`.
- **Bench-tested for `v4.40a.10.05` (2026-10-04/05), unchanged code paths:** Heltec V3 (MTU in app and Safeboot, BLE reconnect cycles, Auto Update end to end including a real GitHub download of `v4.40a.10.05` on the dev channel and the Safeboot apply), RAK4631 (BLE cycles, store node and STOR against a mock server, RM counter persistence and replay rejection), T-Beam v1.2 (Auto Update check with the Bluetooth pause).

### Built and shipped, not on our bench

All other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker/T114, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, T-ETH-Elite, lora32 v2.1, E213, E290, Wireless Paper, T-Echo, LoRa-APRS boards.

## Known gaps, stated plainly

- **Auto Update on the prod channel cannot install yet:** official icssw-org releases carry no `.bin.zz` assets, so a prod node only reports "no installable image for this board". The dev channel (this fork) carries them since `v4.40a.10.05`.
- **Auto Update and same-day releases:** the version guard understands `v<VER>.MM.DD` only. A same-day re-release with a `.2` suffix would be invisible to Auto Update, which is why this release is dated 10.06.
- **Auto Update needs the new Safeboot**, which only the web flasher or USB delivers. With an old Safeboot the first handover is wasted, then automatic handovers stop (`[AU];refuse;old_safeboot`).
- **Auto Update on classic ESP32:** a check started while a phone is connected skips the Bluetooth pause and can fail with a TLS verify error. The classic Safeboot image has 1.6 KB of headroom left.
- **STOR is off and unapproved:** the central server does not know the datagram yet; how the server routes PMs to store gateways (measurements M1-M4) is still unmeasured.
- **Remote management:** three junk `RM1` frames within 90 s lock RM for 5 minutes (accepted by design: it fails closed). The key is `node_passwd`, which is shared with the net console and KISS and exported in clear in the configuration JSON. After a flash wipe on nRF52 change the password (the replay counter is gone). The web send panel carries the target's password once over plain HTTP inside your LAN; use it only on a network you trust. A rejected command (wrong password) gets no answer, so the panel cannot tell it from a lost frame.
- **BLE:** a hello frame of the previous phone still queued when a new phone connects is processed for the new session (backlog B1).
- **MTU:** the access-point path, the classic Safeboot and T-ETH were not measured on the bench.
- **Carried over:** a relay on older firmware strips the ZWJ in compound emoji; multi-hop loss recovery for DM retries (`BACKLOG` PN-01); the central server's masked dedup.

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in, includes the Safeboot of `v4.40a.10.05`. Only `v4.40a.10.06` is offered there.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py` (application only, Safeboot unchanged). The `<board>.bin.zz` files are for Auto Update and need no manual handling. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

## Upstream

The five features are upstream candidates for icssw-org/MeshCom-Firmware issues #1187-#1191; no pull request has been opened yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
