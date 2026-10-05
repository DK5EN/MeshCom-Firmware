> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.05`: the fork's `fork-dev` branch on top of the official `v4.40a`, with five new features for the open upstream issues #1187-#1191 and the fork's backlog fixes since `v4.40a.10.02`.** Unlike `v4.40a.10.02`, this build is **not** byte-identical to the official `v4.40a`: it carries firmware that upstream does not have (yet). The design for the five features is in [`docs/concept-open-issues-20261004.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.05/docs/concept-open-issues-20261004.md).

**How to tell builds apart:** the version field on the air still reads `4.40a`. `--info` prints `...AU env=<board> tag=v4.40a.10.05` and the flash stamp `FLASH_VERSION 20261005`. `FLASH_STRUCT_VERSION` stays at `20260724`, **your configuration survives the update from `4.40a`/`4.35v`**.

**This release also ships a new Safeboot image** (staged-update apply step and the MTU setting). Over-the-air updates (web upload, `tools/webflash.py`, Auto Update itself) only rewrite the application; the new Safeboot reaches a node only through the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) or a USB flash. Auto Update needs the new Safeboot to install anything (see below).

## What is new

1. **Firmware Auto Update (#1187), ESP32 only, off by default.** `--autoupdate off|notify|auto`, `--updchan prod|dev` (prod = icssw-org releases, dev = this fork), `--update check|install|status|apply`, setup card, info row and an "update available" banner in the web GUI. The node checks GitHub once a day over TLS (five built-in root certificates, nothing else), downloads the compressed image (`<board>.bin.zz`, a new asset of every release from now on) into the free end of its own application slot and verifies SHA-256 and CRC; in `auto` mode it hands over to Safeboot between 03:00 and 05:00 local time, which unpacks and verifies the image offline and boots it. Classic ESP32 boards pause Bluetooth for the few seconds of the download (no phone connected) because TLS does not fit next to it otherwise. A node whose Safeboot predates this release is detected after one handover and stops handing over automatically.
2. **Store-and-forward for PMs from the central server (#1188).** A store-mode gateway now also holds PMs that arrive through the server for calls of its store set, purges them on a server-side `:ack`, and uploads its `:sto` notice to the server. New `--stor on|off` (**off by default**, pending the server operator's approval) announces to the server which callsigns this gateway holds PMs for (store set AND heard directly within 12 h, never the own exact call).
3. **Authenticated remote management over LoRa (#1189), off by default.** `--rm on` plus a `--passwd`: a direct message `RM1 <counter> <command> <tag>` with a 64-bit HMAC-SHA256 tag runs one allowlisted command (`reboot`, `status`, `sendpos`, `sendtrack`, `gps|track|display|gateway|mesh on|off`, `txpower <n>`, `setout <a0..b7> <on|off>`) and answers with a tagged reply. Plaintext plus tag, no encryption. Strictly increasing counter persisted across reboots, rate limit, lockout after three bad frames. Reference sender: `tools/remote_cmd.py`. Web switch and info row. Design: `docs/adr-remote-hmac.md`.
4. **MTU setting on every board (#1190).** `--mtu 1280..1500` (alias `--ethmtu`), **new default 1280** for nodes without a stored value; applied to WiFi STA/AP, Ethernet and Safeboot, so the web GUI works through HAMNET/VPN tunnels. Nodes that already store 1500 keep it: set `--mtu 1280` if you are behind a tunnel.
5. **BLE reconnect hardening (#1191).** A phone that drops and reconnects between two loop passes no longer inherits the previous session's authorisation; a wrong-PIN disconnect request no longer hits the next phone; disconnect reasons and counters (`--info` `BLE: con= dis= ...`) on both platforms; advertising self-check every 5 s.
6. **Since `v4.40a.10.02`, also in this build:** POSIX time-zone rules (`--settz`, `node_tz`, web field, BLE `TZ`) with the onboard RTC holding UTC; web GUI delivery ticks survive ring churn; Extern-UDP refuses broadcast/multicast targets and `--extudpip` without argument clears the target; T-Deck: ACK status in the message bubble, map markers survive a reboot, decoded tiles cached in PSRAM; Poland track frequency per platform; a fresh ESP32 without stored SSID starts its access point; CSMA back-off arithmetic and the T5 UI fixes (T5 is not in this release).

## What changes on the air

With default settings, compared with `v4.40a`:

1. Nothing new is transmitted: Auto Update, STOR and remote management are off by default.
2. A node with `--rm on` and a password treats a LoRa DM to its own exact call that starts with `RM1 ` as a command (not shown as chat) and answers with a DM; with RM off such a DM is ordinary text.
3. A store-mode gateway now also stores PMs that reach it through the server and uploads its `:sto` notice to the server.

## Supported Hardware

### Verification for this release

- **Host suite:** 1977 test cases across 60 native environments, all green, plus the golden selftest (48 commands) and the host tool suites (764 pytest cases).
- **Build:** 32 release environments build clean (30 boards and both Safeboot images). `t5_epaper` is not in this release.
- **Bench-tested this cycle (2026-10-04/05):**
  - **Heltec V3 (`DK5EN-1`):** MTU 1240/1436 MSS in app and Safeboot; BLE 50 fast reconnects + 20 unclean drops; remote management in both directions; Auto Update end to end (compressed image staged over the bench LAN, Safeboot unpacked 1.72 MB in 10.3 s and booted the new version), GitHub check on both channels, web GUI.
  - **RAK4631 (`DK5EN-90`):** BLE 50 + 20 cycles with matching counters; store node against a mock server (server PM held, `:sto` uploaded, server `:ack` purged); STOR on/off against the mock; remote management incl. counter persistence across a reboot and replay rejection.
  - **T-Beam v1.2 (`DK5EN-92`, `ttgo_tbeam`):** Auto Update GitHub check with the Bluetooth pause, BLE connects afterwards.

### Built and shipped, not on our bench

All other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker/T114, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, T-ETH-Elite, lora32 v2.1, E213, E290, Wireless Paper, T-Echo, LoRa-APRS boards.

## Known gaps, stated plainly

- **Auto Update on the prod channel cannot install yet:** official icssw-org releases carry no `.bin.zz` assets, so a prod node only reports "no installable image for this board". The dev channel (this fork) carries them from this release on. The GitHub download of a real release is being tested with this release itself; the full install path so far ran from the bench LAN.
- **Auto Update needs the new Safeboot**, which only the web flasher or USB delivers. With an old Safeboot the first handover is wasted, then automatic handovers stop (`[AU];refuse;old_safeboot`).
- **Auto Update on classic ESP32:** a check started while a phone is connected skips the Bluetooth pause and can fail with a TLS verify error. The classic Safeboot image has 1.6 KB of headroom left.
- **STOR is off and unapproved:** the central server does not know the datagram yet; how the server routes PMs to store gateways (measurements M1-M4) is still unmeasured.
- **Remote management:** three junk `RM1` frames within 90 s lock RM for 5 minutes (accepted by design: it fails closed). The key is `node_passwd`, which is shared with the net console and KISS and exported in clear in the configuration JSON. After a flash wipe on nRF52 change the password (the replay counter is gone).
- **BLE:** a hello frame of the previous phone still queued when a new phone connects is processed for the new session (backlog B1).
- **MTU:** the access-point path, the classic Safeboot and T-ETH were not measured on the bench.
- **Carried over:** a relay on older firmware strips the ZWJ in compound emoji; multi-hop loss recovery for DM retries (`BACKLOG` PN-01); the central server's masked dedup.

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in, **includes the new Safeboot**. Only `v4.40a.10.05` is offered there.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py` (application only, Safeboot unchanged). The `<board>.bin.zz` files are for Auto Update and need no manual handling. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

## Upstream

The five features are upstream candidates for icssw-org/MeshCom-Firmware issues #1187-#1191; no pull request has been opened yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
