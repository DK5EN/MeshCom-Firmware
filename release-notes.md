> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.25` makes sure an own group text always goes on air, lets every ESP32 node on WiFi recover a dead network link by itself, stops a mailbox node from delivering an already acknowledged direct message twice, and stops the T-Deck from running out of memory after many messages; the T-Deck also keeps its backlight level and can dim at night.** Your configuration survives the update. T-Beam 1W and T-Deck Pro owners who skipped the web flasher on the last releases must use it once now.

### Action needed

> [!WARNING]
> **T-Beam 1W: run the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) once** if you did not with `v4.40a.10.20` (no erase needed).
>
> - It writes the QIO bootloader the 1W has been missing.
> - Until then Auto Update, `--ota-update` and the Safeboot update page stay blocked.
> - Do not update a 1W over the air before that: older firmware hands over to Safeboot and **the node loses its settings**.

> [!WARNING]
> **T-Deck Pro: run the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) once** if you did not with `v4.40a.10.21` (no erase needed).
>
> - The old partition table had no Safeboot slot, so the web flasher overwrote the application and the board never started.
> - The new table keeps your settings; the SPIFFS area moves and is formatted once on first boot.

**Everyone else:** update as usual (web flasher, `.bin` over USB, `tools/webflash.py`, or Auto Update on the `dev` channel).

### Key facts

- **Base:** official `v4.40a.spserver` (the latest official release, 2026-10-22), upstream `dev` up to #1236, and the fork's own work (transmit queue pool, Auto Update, store-and-forward, authenticated remote management over LoRa). It is the fork's `fork-dev` branch and **not** byte-identical to any official release.
- **Configuration:** `FLASH_STRUCT_VERSION` stays at `20260724`, so settings from `4.40a`/`4.35v` are kept. **Exception:** ten locked protocol settings are reset to their fixed value at first boot (since `v4.40a.10.19`, see below).
- **How to tell builds apart:** the version on the air still reads `4.40a`.
  - `--info` prints `...AU env=<board> tag=v4.40a.10.25` and `FLASH_VERSION 20261025`.
  - `--id` prints `MCBOARD:v1:<hwid>:<hwid>:<board>:v4.40a.10.25`.
- **Safeboot v3** ships unchanged since `v4.40a.10.21`.
  - It reaches a node only through the web flasher; over-the-air updates write the application alone.
  - Optional: the application still works with Safeboot v2 (`v4.40a.10.05` and later). v3 only adds that Safeboot names the board.
- **Auto Update channels:** `prod` reads the latest icssw-org release; `dev` (`--updchan dev`) reads the **Latest** release of this fork, which is this one. The mode is always `notify`: the node reports a new release, nothing installs by itself.
- **Built by GitHub CI** (`MeshCom CI`) from the tagged commit, 66 assets:
  - 27 ESP32 application `.bin` plus 27 `.autoupdate` files (zlib twin of each image, for Auto Update)
  - the three nRF52 boards as `.uf2` and DFU `.zip`
  - six support files

## New in `v4.40a.10.25`

- **An own group text is always sent at least once** (fork only, transmit queue, TXP-01).
  - Cause: when another gateway put the server copy of our group or `*` text on air first, our node took that copy as the echo of its own text and dropped it from the queue before its own first transmission.
  - Effect: on the gateway `DK5EN-98` 45 of 103 own group texts never left the node on LoRa (field log, 2026-10-24/25); on the bench 9 of 10.
  - Now: an own text is never released before it was sent once. The existing check stops the repeat after it was heard. Texts sent for a KISS client keep the old release.
  - Own direct messages (PN) were not affected (0 of 20 on the bench).
  - Background: [`docs/txp-own-msg-echo-release-20261024.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/txp-own-msg-echo-release-20261024.md).
- **ESP32 nodes on WiFi recover a dead network link by themselves** (TD-21, all ESP32 boards in WiFi mode).
  - Cause: a node can stay associated with its access point while the IP path is dead (no answer to ARP or ping). The old watchdog only acted when the link dropped, so the node stayed offline: 47 minutes on a T-Deck Plus in the field, more than 20 minutes on the bench.
  - Now: every 30 s the node pings its WiFi gateway (3 echoes). After 3 failed rounds it reconnects WiFi, after 3 more it restarts the network, after 10 more it reboots. Log: `[WIFI];watchdog;ipdead;<reconnect|reset|restart>`.
  - A gateway that never answered a ping does not arm the watchdog, so routers that block ping cause no action.
  - No extra task: the probe runs inside the network stack and costs about 0.6 KB of idle heap on a T-Beam (WIFI-HEAP-01).
  - Bench: an outage of 2 min instead of no recovery in 20 min.
- **A mailbox node no longer delivers an already acknowledged direct message a second time** (SNF-ACK-01, store-and-forward, ESP32-S3 and nRF52 nodes with a mailbox switched on, `--store own|list|heard`).
  - Cause: when the recipient's ack arrived before the mailbox node's own copy, the delivery's re-ack cleared the slot, and a late copy opened a new one. The recipient got the message twice plus a second store notice (field case 2026-10-25).
  - Now: an ack heard for a call the mailbox serves blocks storing that sender, recipient and message number again for 1 hour (32 entries).
  - The `MBOX` line ends with `tomb=<n>`, the count of refused stores; the mailbox web page shows it too.
  - Background: [`docs/commands-store-node.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/commands-store-node.md).
- **T-Deck: no more memory exhaustion after many received messages** (TD-20, TD-22, T-Deck and T-Deck Plus).
  - Cause: every received message was kept a second time in internal memory until reboot, and the number of message tabs had no limit. After about 250 messages the WiFi buffers ran out and the network link hung.
  - Now: the extra copy is gone. At most 16 tabs, the least recently used one is closed first. Below 40 KiB of free internal memory the oldest messages of an inactive tab are dropped (the open tab keeps at least 10).
  - Bench `DK5EN-14`: 500 messages in 5 groups held 41-62 KB free, 0 lost pings.
  - Background: [`docs/campaign-tdeck-heap-wifi-20261025.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-tdeck-heap-wifi-20261025.md).
- **T-Deck: saved backlight level with an automatic day/night switch** (TD-23, T-Deck and T-Deck Plus).
  - The level survives a reboot (was 16 on every boot). SYM+B steps 16, 12, 8, 4, 2, 1 and saves the level of the current period.
  - "Auto Day/Night" in the Setup tab (Display line): the node switches between a day level (default 16) and a night level (default 4) by the sun's elevation at its position. Off by default, so nothing changes until you switch it on.
  - Without a valid clock or position the day level is used.
  - Console: `--bl`, `--bl auto on|off`, `--bl day|night 1-16`.
  - Background: [`docs/TD-23-tdeck-day-night-brightness.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/TD-23-tdeck-day-night-brightness.md).
- **Bench diagnostics only** (INSTRUMENT builds, release images unchanged): a periodic heap and WiFi line, `--heaplog`, `--wifips`, a memory starvation injector; `--wifips none` is refused while Bluetooth runs instead of crashing the node (WIFI-PS-01).

## Earlier fork releases

Everything below is already in `v4.40a.10.24` and older, and therefore in this release too. Open a section for details.

<details>
<summary><strong><code>v4.40a.10.24</code></strong> — Polish server host, Wireless Tracker Auto Update, T-Beam first boot, remote-management repeat stop</summary>

- **Polish gateway server host is now `sp.meshcom.pl`** (was `spmeshcom.dynalias.com`, server operator's request).
  - Both names resolve to 146.59.232.234; nRF52 uses the literal address, which is unchanged.
  - Only gateways with server code SP see a difference. Merged upstream as [#1236](https://github.com/icssw-org/MeshCom-Firmware/pull/1236).
- **Auto Update works on the Heltec Wireless Tracker again** (AU-21, ESP32-S3 without PSRAM).
  - Cause: the 160x80 display buffer took 25.6 KB of internal memory from boot, even with the display switched off. The update check's TLS handshake then ran out of memory (`tls -9984`, shown as "No connection to GitHub").
  - Now the buffer exists only while the display is on, and it is released for every update check and download. The display shows "Update..." meanwhile and redraws afterwards.
  - A TLS failure caused by low memory now reports `lowheap_tls` (web GUI: the low-memory state) instead of a connection error, and counts as one download attempt.
  - `--info` gains a `HEAP free/min/blk` line on every ESP32 and the display buffer size on the tracker.
  - Background: [`docs/campaign-au21-tft-heap-20261024.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-au21-tft-heap-20261024.md).
- **A remote-management command from the phone app stops repeating once the target answers** (fork only, remote management).
  - Since `v4.40a.10.11` an own `RM1` command repeats until the target replies. Only commands sent with `--rm` on the node itself had that stop.
  - A command composed in McApp (Extern-UDP or BLE), on the serial console or in the web GUI was sent with 3 repeats even after the target had answered within seconds, and the node logged `RETRANSMIT_GIVEUP` (field log `DK5EN-98` to `DK5EN-1`, 2026-10-24).
  - Now the reply to such a command stops the repeats: `[RM];stop;client;<dst>;ctr;<n>;slot;<s>`. The node holds no key for these commands, so the stop is unverified. A forged reply can only cut the repeats (the old send-once behaviour); the client still verifies the reply itself.
- **A factory-fresh T-Beam starts with GPS on, not TRACK** (TRK-03).
  - Since V4.21 the first-boot default set the TRACK bit where it meant GPS (wrong bit value), so an erased T-Beam booted with TRACK on and GPS off.
  - Only an erased node gets the new default. Existing nodes keep their stored setting: check `--pos`, and use `--track off` / `--gps on` if needed.
  - Background: [`docs/campaign-trk02-webflash-track-20261024.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-trk02-webflash-track-20261024.md).

</details>

<details>
<summary><strong><code>v4.40a.10.23</code></strong> — transmit queue pool, call blocklist, nine stability fixes</summary>

**Transmit queue (fork only, TXP)**

- **The LoRa transmit queue is a pool, not a ring.** A free slot is taken anywhere, so one waiting message no longer blocks the slots behind it ("queue full" with free slots is gone).
- **Old frames expire by priority** instead of waiting for their turn: relayed frames 300 s, positions 120 s, ACKs 90 s (HEY keeps its 180 s limit). Own texts and messages waiting for an ACK do not expire; their retry ladder handles them.
- **Time beacons (`{CET}`) older than 150 s are dropped**, at the gateway, in the queue and before relaying. A beacon from the future, or any beacon while the node has no trusted clock, is dropped too.
- **A newer frame replaces its queued twin:** HEY and position of the same originator, and a newer relayed time beacon, take the queued copy's slot instead of a second one.
- **DM retries reuse their slot,** so a DM waiting for its ACK holds one slot, not one per retry.
- **Web GUI "TX queue" gauge:** counts every occupied slot; messages waiting for an ACK are outlined, and the legend says how many.
- Background: [`docs/campaign-tx-pool.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-tx-pool.md).

**Call blocklist (fork only, BLK-01)**

- **`--blockcall <list>` / `--blockcall none`:** frames from a blocked call, or relayed by one, are dropped and not relayed. A bare call blocks all its SSIDs.
- Shown in `--info` and on the web Info/Setup pages; settable over remote management (`RM1 blockcall`). Empty by default.
- Replaces the blocklist of the third-party DM3KS build; its stored list is taken over (ESP32). Background: [`docs/campaign-blockcall.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-blockcall.md).

**Stability fixes**

- **No crash in the daily Auto Update check on classic ESP32** (AU-19). The BLE pause before the check could hit a NimBLE timer whose callback had been cleared (2 panics in about 35 pauses on the bench); fixed by a build-time NimBLE patch.
- **No heap leak per BLE pause** (AU-20): each pause leaked one timer; now 0 in 100 pauses.
- **Auto Update checks the heap for both TLS buffers** before it connects (AU-18), so a download no longer fails with `-32512` after the check said "enough memory".
- **No lost LoRa packets while a slow browser blocks the web server** (WEB-RX-01, issue [#1229](https://github.com/icssw-org/MeshCom-Firmware/issues/1229), ESP32): received packets are parked and processed afterwards (bench: 0 of 44 lost, was 16).
- **A node can no longer go deaf for LoRa until reboot** (RX-DEAF-01 ESP32, RX-DEAF-02 nRF52): a stale "receiving" flag left by the transmit gate or the RX timeout is now cleared.
- **Gateways skip server frames they already heard on LoRa** (GWRX-01): server text frames take part in the neighbour cancel, and server frames count the gateway as a provider.
- **A gateway's two ACKs for one message carry one id on purpose** (GWACK-01): both copies are sent with the same id instead of an accidental repeat.
- **A duplicate server position no longer takes a second duplicate-filter slot** (UDP-04, ESP32).
- **Upstream merged:** [#1235](https://github.com/icssw-org/MeshCom-Firmware/pull/1235) (merged 2026-10-23) — the fork already shipped this code; the release now matches upstream `dev` for it.

</details>

<details>
<summary><strong><code>v4.40a.10.22</code></strong> — gateway server-link warning, Polish server row</summary>

- **Gateway server-link warning works again with stray LAN traffic.** A gateway now resets its "server silent" clock only on packets from its MeshCom server (ESP32 and nRF52).
  - Before, any datagram on the gateway port reset it, so a gateway with a dead server link but some LAN noise never logged "[UDP] Server not responding" or "[UDP] Heartbeat timeout".
  - Diagnostics only: recovery was never delayed, and no real server frame took this branch (0 of 102,394 in our field captures).
  - Merged upstream as [#1233](https://github.com/icssw-org/MeshCom-Firmware/pull/1233) (issue [#1231](https://github.com/icssw-org/MeshCom-Firmware/issues/1231), fork ID UDP-03).
- **Polish server in the table.** New row SP (Internet `spmeshcom.dynalias.com` 146.59.232.234; Hamnet side via the Internet server) from upstream [#1234](https://github.com/icssw-org/MeshCom-Firmware/pull/1234). OE is now labelled "MeshCom OE (Austria/WW)" (#1215). Only gateways with server code SP or OE see a difference.
- **Upstream merged:** `upstream/dev` up to `d47c34c1` (#1215, #1219/#1223 net zero, #1224 = the web send buffer the fork already had, #1225 `default_envs` order, #1233, #1234). The Safeboot images rebuilt upstream in #1215 are not taken; the fork keeps Safeboot v3.

</details>

<details>
<summary><strong><code>v4.40a.10.21</code></strong> — board identity for the web flasher, T-Deck Pro partition table</summary>

- **Every image names its board, even when the application does not run** (WF-05).
  - Why: several boards share a hardware ID (all three T-Beam v1.x images report 12), and a board in Safeboot, ROM download mode or with a hung application could not tell the web flasher which image it needs.
  - A 128-byte marker (`MCBOARD1`, hardware ID, build environment, release tag) sits at offset 0x120 of every ESP32 image; on nRF52 it is a constant in the image.
  - New command `--id` prints `MCBOARD:v1:<hwid at runtime>:<hwid at build>:<env>:<tag>` (ESP32 and nRF52).
  - **Safeboot v3** reads the marker at boot and answers `--id` and `--info` with the board name (v2 answered nothing).
  - Web flasher button **"Aus dem Flash lesen"** (ESP32): enters ROM download mode, reads about 7 KB in 1-1.3 s, names the board from the marker, then resets it into the application. Images older than this release carry no marker ("no marker").
  - `tools/pages_flasher.py` refuses to publish an ESP32 image without a matching marker.
- **Web flasher picks exactly one image** (WF-01..WF-03).
  - It reads `...AU env=<board>` from `--info`; boards with a shared hardware ID no longer leave several candidates.
  - An ESP32-S3 with native USB is left with both control lines low after listening, not in the "download" state.
  - A silent board shows what it received (last 2 KB, folded), Safeboot and the ROM banner are recognised, and **"Neustart in die App"** resets and re-detects it. A board put into ROM mode with its BOOT button needs its RST key; the page says so.
- **T-Deck Pro starts again after web flashing** (WF-04).
  - It was built with `default_16MB.csv` (no Safeboot partition), while the web flasher wrote Safeboot to 0x10000 and the application to 0xC0000 like on every other 16 MB board.
  - It now uses the 16 MB Safeboot table, and `pages_flasher.py` refuses a manifest that writes outside a board's partition table (all 27 ESP32 boards pass).
- Background: [`docs/campaign-webflasher-autodetect-20261019.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-webflasher-autodetect-20261019.md).

</details>

<details>
<summary><strong><code>v4.40a.10.20</code></strong> — no watchdog reboot from slow browsers, T-Beam 1W Safeboot fix</summary>

- **Web pages can no longer reboot the node through the watchdog** (WEB-WDT-01).
  - Before: the web server wrote straight into the TCP socket; with a slow browser one write could retry for 10 s and the node rebooted with `TASK_WDT` (reproduced on the bench).
  - Now: one 1400 B send buffer with a bounded send loop. The page is aborted after 2 s without progress, 15 s in total, or a send error; `[WEB];tx;abort;<stall|budget|error>` is logged once.
  - ESP32 sends without blocking; nRF52 (W5100S) sends at most what the chip buffer takes. Path, MHeard and neighbour pages feed the watchdog per record.
  - Normal LAN: no change (94 kB main page in about 0.5 s). Very slow link: a truncated page instead of a reboot.
- **No more "WEBServer stopped" log flood without an IP address** (WEB-STOP-01). Typical on the T-Deck with WiFi switched off. To get the web server back, switch WiFi on (`--wifi on` or the WLAN button).
- **NVS fill level in `--info`** (RM-NVS-01). Diagnostics for a field report where "remember password" failed on an E22; suspected full 20 KB NVS partition. A failed save logs `[RMNODES];write;...`. The cause is not fixed yet.
- **T-Beam 1W no longer gets stuck in Safeboot after an Auto Update** (AU-16).
  - Cause: the 1W board definition had flash mode `dio`, so it shipped the DIO bootloader. Safeboot (IDF 5) then misreads the flash, sees no settings, opens the `MeshCom-OTA` access point at 192.168.4.1, rejects the application, and writes an empty settings page: **the node loses its settings**.
  - Fix: the 1W builds with `qio` (bootloader byte-identical to the other S3 boards). Confirmed on a real 1W in the field on 2026-10-19.
  - The bootloader changes only through the web flasher. Until then every ESP32-S3 with a DIO bootloader refuses the handover to Safeboot with `[ERR];safeboot;bootloader_dio;renew with the web flasher` (no override). `--update status` and the web GUI say so.
  - Guide: [`docs/troubleshooting-tbeam-1w-autoupdate.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/troubleshooting-tbeam-1w-autoupdate.md).
- Background: [`docs/campaign-web-wdt-20261019.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-web-wdt-20261019.md).

</details>

<details>
<summary><strong><code>v4.40a.10.19</code></strong> — LoRa queue relief, locked protocol settings, Auto Update web row</summary>

- **A full LoRa transmit queue no longer blocks for minutes.** Field captures showed QRT for up to 8 minutes while the channel was idle three quarters of the time: every received frame re-armed the full hold, and the queue filled with redundant copies.
  - **`--nbrrelay on` is fixed in release builds:** a relay is cancelled when every neighbour that needs the frame is served; a relay waiting 8 s gets a 150 ms hold.
  - An echo of an own group text releases its queued copy; a heard or acknowledged text stops retrying; no retries during QRT.
  - A server frame carrying the node's own call as source is not queued for LoRa again.
  - A full queue answers with a refusal (QTA) at any depth. QRT ends below 16 entries with one QRV, at most every 60 s.
  - **Age-escalated hold:** a priority 1-3 frame waiting 20 s gets a short hold after a received frame, after 60 s the rapid hold. Never after own TX or a busy CAD.
  - Logs: the TX log `cad=` shows the real CAD attempt; `[MC-HWM]` gains `aged1`/`aged2`/`wait_hwm` (ESP32). The web queue card says "Your messages: accepted / slow down / refused".
  - Background: [analysis](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/lora-qrt-dk5en98-db0taw14-20261018.md), [plan](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-lora-qrt-20261018.md).
- **Ten protocol-tuning settings are fixed in release builds.**
  - Locked: `--nbrrelay` (on), `--nbrsym` (on), `--nbrreport` (auto), `--shortpath` (off), `--pingtime`/`--pingmax`, the four `--spec*` parameters, and `--stor on`. A change is refused with `[ERR];locked;<name>` on every path (USB, 2323 console, web, BLE, T-Deck, remote management).
  - **A node that had any of these changed is set to the fixed value at first boot.**
  - `{SET}` over LoRa accepts a text hop of 1..6 and ignores the position hop.
  - New range limits: `--postime` 300..86400 s, `--ptime` 5..120 min. Setter fixes: `--ptime` fallback 15 min (was 30 h), `--pingtime` floor 30 s, `--pingmax max` is 5 (was 100).
  - SF, bandwidth, coding rate, TX power, gateway, mesh, via and network mode stay free.
  - Spec: [`docs/production-locked-settings.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/production-locked-settings.md).
- **Auto Update: `notify` only, one button in the web GUI.**
  - `off` is gone and loads as `notify`: **every ESP32 node with WiFi checks for a new release about once a day and reports it; nothing installs by itself.** `auto` is refused in every build.
  - The Info page's Auto update row runs one button through every state (Check now, Install, Downloading %, Install now, Installing %) and follows the node through Safeboot and back without a reload.
  - Classic ESP32 with a phone connected: "Pause Bluetooth and check" drops the phone link for the job. Before giving up on low heap the check sheds KISS TCP and, if that is enough, the net console.
  - Background: [`docs/campaign-au-webgui-ux-20261018.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-au-webgui-ux-20261018.md).
- **More heap for the update check on classic ESP32.** The 5 KB phone byte rings exist only while BLE runs; the check now starts with 86 KB free instead of 76-81 KB. Side effect: the web message history is empty after an update check on these boards.
- **Board LED and charge LED.** RAK4631, T114 and T-Echo get the LED heartbeat and remote `led`; LEDs go dark in sleep and with BLE off. More variant pins; `--info` prints `BOARD LED` and `CHARGE LED`. `--board led on` is refused while OneWire owns the pad (T-Beam GPIO4).
- **Remote `sens` reports only detected sensors** (BME280 humidity was reported as not fitted).
- **The neighbour page no longer trips the watchdog** (field reboots on a 47-row table).
- Background: [`docs/campaign-fork-hardening-20261018.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/campaign-fork-hardening-20261018.md).

</details>

<details>
<summary><strong><code>v4.40a.10.18</code></strong> — GPS position filter, net console fixes, remote <code>net</code>/<code>upd</code></summary>

- **GPS position filter, on by default on ESP32 GPS boards** (GPS-11).
  - Before: a still node sent the raw fix, and 10-40 m of multipath noise moved its on-air `DDMM.MM` cell many times an hour.
  - Now: still nodes average over 30 minutes and hold their grid cell; motion is detected automatically and switches to a smoothed track. With `--track on` the filter uses the GPS Doppler speed.
  - `--gpsfilter off` restores the old path exactly until the next reboot. `--gpsdebug on` prints raw, filtered and held position per fix.
  - Not on RAK4631 and T-Deck Pro (own GPS paths).
- **Network debug console (port 2323):** starts in pure WiFi AP mode too; `--netconsole off` really closes the listener; a vanished client no longer blocks every new login until a power cycle (TCP keepalive drops dead peers, a valid login takes over).
- **Remote management gains `net` and `upd`.**
  - `net`: MTU, MeshCom server, configured and effective network.
  - `upd`: Auto Update state; `upd check` starts a check; `upd go <tag>` installs and hands over to Safeboot. Keeps every Auto Update gate except the night window.
  - `status` gains `rs=0|1`; the remote web page shows `net` and `upd` with a two-tap Update button.

</details>

<details>
<summary><strong><code>v4.40a.10.17</code></strong> — Auto Update on PSRAM boards, <code>--via</code> validation, T-Deck reply button</summary>

- **Auto Update works on PSRAM boards** (AU-13). The TLS handshake took about 56 KB of the T-Deck Plus' internal heap while 4.6 MB PSRAM sat unused, so the check failed. mbedTLS now allocates from PSRAM first; the heap gate measures internal RAM only.
- **`--via` validates before storing** (VIA-01). Each token must be a valid callsign with SSID 0..99; bad lists are rejected and the stored value stays. `NONE` or empty still clears it.
- **T-Deck: reply button on every incoming message** (TD-21). Opening compose from a conversation tab prefills "To".
- **Web GUI: firmware version per station** in the Path table and on every MHeard card (2 B RAM per neighbour).
- Clean-up: a duplicated flag check in `OnRxDone` removed (no behaviour change).

</details>

<details>
<summary><strong><code>v4.40a.10.16</code></strong> — one gateway server table, <code>--setnet</code>, RAK4631 static IP</summary>

- **One central gateway server table** (`src/gw_servers.cpp`). ESP32 and nRF52 pick server and time server from the same table, one row per country code (OE, DL, IT, HC, HB), Internet and Hamnet side each. Before, three hand-written copies had drifted apart. Named changes:
  - DL on the Internet: `meshcom.hamnet.network` only; DL on the Hamnet: the Hamcloud host `44.148.230.197` (server operator's request).
  - HC on the Internet goes to the DL server; IT with Hamnet forced keeps the Italian Internet server (no Italian Hamnet server).
  - nRF52 selects DL and HC like ESP32; static nRF52 nodes also get HC and HB.
  - The Hamnet time server follows the server row; `--setownntp` now applies to DHCP nodes too.
- **Network mode `--setnet auto|inet|hamnet`.** `auto` (own address `44.x` means Hamnet) is the default **for new or wiped nodes only**; existing nodes keep their setting. Bare `--setnet` prints mode and network in use; `--setinet`/`--sethamnet` stay as aliases.
- **RAK4631 with a static IP stays static** during Ethernet recovery (ETH-05); after a W5100S hardware reset it waits for the PHY.
- **Console and logs:** ESP32 boards with a USB-UART bridge get a 2048 B serial transmit ring, so debug output no longer stalls receiving (OnRxDone p50 77.9 ms to 4.4 ms; about 2.4 kB heap). `--setlog` STAT reports OnRxDone timing.
- **Upstream merged:** official `v4.40a.hbserver` (#1209-#1212: HB and HC servers, HB for nRF52, esptool upload for E213/E290/Wireless Paper).

</details>

<details>
<summary><strong><code>v4.40a.10.11</code></strong> — fork features compared with the official firmware</summary>

- **Firmware Auto Update** (#1187), ESP32 only. Daily TLS check against GitHub, compressed `<board>.autoupdate` image, SHA-256 and CRC verified, applied by the versioned Safeboot between 03:00 and 05:00. Mode is `notify` since `v4.40a.10.19`.
- **Store-and-forward for PMs from the central server** (#1188), `--stor on|off`. Locked off in release builds since `v4.40a.10.19`.
- **Authenticated remote management over LoRa** (#1189), off by default. `--remotemgmt on` plus `--passwd`; `RM1` DMs with a 64-bit HMAC-SHA256 tag run allowlisted commands; lockout per sender callsign; web GUI remote page.
- Everything else of `v4.40a.10.11` (MTU, time zones, BLE reconnect hardening, ETH-04, LED work, HTML escaping, small web features) is in the official firmware since #1207/#1208.

</details>

Full list of differences from upstream: [`docs/fork-dev-vs-upstream-20261018.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.25/docs/fork-dev-vs-upstream-20261018.md).

## What changes on the air

With default settings, compared with the official `v4.40a.spserver`:

- **Nothing new on LoRa by default.** STOR and remote management are off. Auto Update is a daily Internet check over WiFi, never on LoRa.
- **Every release node relays with `--nbrrelay on`** and cannot switch it off. Relays are cancelled when every neighbour is served; expect fewer redundant copies and shorter queues. Field evidence so far: one gateway (`DK5EN-98`).
- **Own group texts go out at least once:** another gateway's server copy no longer cancels an own text before its first transmission (new in this release). Expect more own texts on air from gateways near other gateways.
- **ESP32 nodes on WiFi ping their WiFi gateway** (3 echoes every 30 s, local network only, never on LoRa).
- **Fewer own retries and copies:** echoed group texts cancel their queued copy once it was sent, heard or acknowledged texts stop retrying, no retries during QRT, and a gateway no longer re-sends its own message when the server reflects it.
- **Stale frames leave the queue:** relayed frames after 300 s, positions after 120 s, ACKs after 90 s; time beacons older than 150 s are not sent or relayed at all. A newer HEY, position or time beacon replaces its queued twin.
- **Gateways send fewer server copies:** a server frame already heard on LoRa is not transmitted again.
- **Old frames wait less:** a priority 1-3 frame queued for 20 s (60 s) goes out after a short (rapid) hold, so a busy node no longer stalls for minutes.
- **QRT ends earlier** (queue below 16, one QRV); a full queue answers with a refusal at any depth.
- **`{SET}` over LoRa sets the text hop only** (1..6). Changed `--pingtime`/`--pingmax`, `--nbrreport` or `--shortpath` go back to the fixed value.
- **Gateways with server code SP** connect to `sp.meshcom.pl` (same address as before).
- **A factory-fresh T-Beam** runs GPS and no TRACK beaconing; configured T-Beams are unchanged.
- **ESP32 boards with GPS send a filtered position:** a still node stays on one grid cell; a moving node follows a smoothed track.
- **Remote management** (if enabled with a password): a LoRa DM to the own call starting with `RM1 ` is a command; a gateway uploads its own HEY echoes and RM1 messages to the server.

## Supported Hardware

### Verification for this release

- **Host suite:** 2951 test cases in 82 native environments, all green, plus the golden selftest (48 commands), nRF52 board links and the host tool suites (pytest 1410, web GUI, web flasher). New: suites for the own-text release, the WiFi liveness watchdog, the T-Deck message store and tab limit, and the day/night backlight (sun elevation against reference times).
- **Build:** all release images built by CI (27 ESP32, 3 nRF52, Safeboot), and the 27 `.autoupdate` files re-inflated byte-exact.
- **Idle heap on T-Beam v1.2 (`DK5EN-92`):** 68728 B against the 69332 B baseline (-604 B, the gate allows 3072 B).
  - A first build of the WiFi watchdog with its own ping task measured 63884 B and failed the gate; the release ships the probe without a task.
- **Benched for this release (2026-10-25):**
  - Own group text fix on Heltec V3 (`DK5EN-1`) and RAK4631 (`DK5EN-90`), same setup before and after: without the fix 6 of 10 own texts released before their first transmission, with it 0 of 20; 20 of 20 sent, 19 exactly once (one repeat of a text nobody heard, by design).
  - WiFi watchdog on T-Deck Plus (`DK5EN-14`), final probe: internal memory starved until the IP path hung (588 B free), memory released, the path stayed dead; the watchdog reconnected after 3 failed rounds, outage 2 min 0 s, no reboot (before the fix: no recovery in 20 min).
  - The probe gets answers from a real access point: `[WIFI];probe;armed` 60 s after boot on T-Beam v1.2 and T-Deck Plus.
  - T-Deck memory on `DK5EN-14`: 400 messages in one tab plateau at 41.6 KB free (before: hang at about 250). 20 groups round robin hold 16 tabs with 0 lost pings; 500 messages in 5 groups keep 41.0-61.9 KB free, no failed allocation.
- **Host-tested only:** the mailbox fix (SNF-ACK-01: 9 new cases, each failing against the unfixed code), the day/night backlight (TD-23: no hardware test yet), the watchdog on any board except the T-Deck Plus, and the watchdog's network reset and reboot stages.

<details>
<summary>Bench results of earlier releases (unchanged code since)</summary>

- **`v4.40a.10.24` (2026-10-24):**
  - Auto Update check on Heltec V3 (`DK5EN-1`) with the AU-21 code: the check passes, no allocation failure, `--info` shows the heap line.
  - TRK-03 on T-Beam v1.2 (`DK5EN-92`): erased and reflashed, the bug confirmed (TRACK on, GPS off on first boot); the fix is covered by a regression test that fails on the old value.
  - Host-tested only: the tracker display-buffer release and re-acquire, the TRK-03 fix itself, the remote-management repeat stop. Idle heap gate skipped (operator decision).
- **`v4.40a.10.23` (2026-10-23/24):**
  - Transmit queue on Heltec V3 (`DK5EN-1`), RAK4631 (`DK5EN-90`) and T-Beam v1.2 (`DK5EN-92`): gauge and legend, ACK path, three DM retries in one slot on ESP32 and nRF52, time-beacon age gate and replacement at a gateway fed by a mock server.
  - Call blocklist on `DK5EN-1`, `DK5EN-90`, `DK5EN-92`.
  - AU-19/AU-20: 100 BLE pauses on T-Beam v1.2 without a panic, no leaked timer; AU-18: three update checks on the release build, no allocation failure.
  - WEB-RX-01 on `DK5EN-1`: 0 of 44 packets lost during web stalls (was up to 16). RX-DEAF-01 forced on `DK5EN-92`: flag cleared within 1 s, reception continuous.
  - GWRX-01 on ESP32 and nRF52; GWACK-01 28 of 28 ACK pairs with one id.
  - The release code runs on all five of our nodes, including the production gateway `DK5EN-98`, since 2026-10-24 16:20.
- **`v4.40a.10.22` (2026-10-22):** server-link warning fix on Heltec V3 (`DK5EN-1`): with 13 stray datagrams during 130 s of server silence no warning without the fix; with it the first after 59 s. Idle heap 69068 B.
- **`v4.40a.10.21` (2026-10-19):**
  - `--id` and the marker on T-Deck Plus (`DK5EN-14`), Heltec V3 (`DK5EN-1`), T-Beam v1.2 (`DK5EN-92`, alias 12 resolves to `ttgo_tbeam`) and RAK4631 (`DK5EN-90`). Bench builds of the release code.
  - Safeboot v3 on T-Beam v1.2 (UART) and T-Deck Plus (native USB): marker at boot, `--id` and `--info` answered, settings kept.
  - Web flasher (local copy, Chrome on macOS): detection right on all four boards. "Aus dem Flash lesen" names T-Deck Plus and T-Beam, also silent in ROM mode, and the restart returns to the application. Flash read in 1.0-1.3 s. RAK4631 detected via `--id` (no flash read on nRF52).
- **`v4.40a.10.20` (2026-10-19):**
  - Web send buffer: the `TASK_WDT` reboot reproduced on Heltec V3 without the fix. With it no reboot on Heltec V3, T-Beam v1.2, T-Deck Plus and RAK4631 at 2.5 s and 1.2 s reply delays; pages cut by `stall`/`budget` as designed. Without delay all 14 pages complete (main page 94 kB in 501 ms).
  - "WEBServer stopped" fix and NVS line: host tests and board builds only.
  - T-Beam 1W Safeboot fix reproduced on T-Deck Plus (same chip family, bootloader swapped): DIO showed the field symptoms; QIO came up and returned to the application; the guard refused DIO on every path, settings intact.
- **`v4.40a.10.19` (2026-10-18):**
  - Heap on T-Beam v1.2: idle 69332 B with BLE up; free heap at the update check 86276-86576 B (was 76.3-81.5 KB).
  - Setting lock refused on Heltec V3 and RAK4631; on `DK5EN-98` NBRRELAY is on after the flash.
  - LEDs and `--info` LED lines on `DK5EN-1`, `DK5EN-90`, `DK5EN-92`; T-Deck Plus shows "not fitted".
  - Auto Update web row in Chrome on Heltec V3, T-Deck Plus and T-Beam: every state, a full install through Safeboot and back, a corrupt stream shows "Install failed". BLE guard: 20 forced cycles without a reset.
  - Queue work: host tests and board builds only (operator decision); no field capture evaluated yet. Neighbour-page fix: host-tested only.
- **`v4.40a.10.18` (2026-10-17):**
  - GPS filter on Heltec V3 (indoor, strong multipath): 2 h each with `--track off` and `on`, no reboot. The held cell changed 11 times in 2 h against 119 for the raw fix. No drive test.
  - Net console fixes before/after on Heltec V3.
  - RM1 `net`/`upd` over LoRa at 2 dBm, including a real `upd go` through Safeboot and back.
  - `--via` validation: every reject, accept and clear case.
- **`v4.40a.10.17` (2026-10-16/17):**
  - Auto Update on T-Deck Plus: 10 of 10 checks right after reboot pass (internal heap minimum 55.7 KB, was 2.7 KB); a 1.57 MB image staged in 22 s. Control runs on T-Beam v1.2 and Heltec V3.
  - T-Deck reply button checked by hand.
- **`v4.40a.10.16` (2026-10-15/16):**
  - Network matrix on RAK4631 (Ethernet) and Heltec V3 (WiFi): 45 cases per node (3 networks × `auto`/`inet`/`hamnet` × OE/DL/IT/HC/HB). Server choice right in all 90; KEEP, BEAT and NTP in every case with a return path.
  - RAK4631 static IP: a link drop re-applies the static configuration; a stalled second drop escalates to a W5100S reset and recovers in 1.9 s.
- **Earlier:** remote management (2026-10-06/07), Auto Update end to end (2026-10-11), the `v4.40a.10.05`/`10.06` features.

</details>

### Built and shipped, not on our bench

- **T-Beam 1W:** none on our bench. A field operator confirmed on 2026-10-19 that a 1W with the QIO bootloader completed an Auto Update. Marker and flash read not tried on a 1W.
- **T-Deck Pro:** none on our bench. The new partition table is checked by the build only (Safeboot at 0x10000, application at 0xC0000, NVS unchanged at 0x9000).
- **New LED pins** on T-Echo, T114, T-ETH-Elite, Heltec V4 and the S3 DevKitC come from board documentation; no LED was watched blinking.
- **GPS filter:** benched stationary on one board only; T-Beam, T-Beam Supreme, T-Deck and other GPS boards run the same code untested.
- **Web firmware column:** host tests only.
- **All other boards:** E22 family, Heltec V2/V4/Wireless Stick/Tracker, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, lora32 v2.1, E213, E290, Wireless Paper, LoRa-APRS boards, T-ETH-Elite (Ethernet), T114 and T-Echo, including network mode and server table.
- `t5_epaper` is not shipped.

## Known gaps, stated plainly

**Could cost you settings or a working node**

- **T-Beam 1W in the field** keeps its DIO bootloader and blocked Auto Update until its owner runs the web flasher once. A 1W already stuck in Safeboot needs the web flasher too, and its settings may be gone.
- **A failed image after the flash erase** leaves the node in Safeboot until a web-flasher upload (single app slot).
- **Locked settings are reset at boot without asking.** INSTRUMENT bench builds keep them changeable. `--stor on` is refused, so store-and-forward (#1188) is effectively off.

**Not benched (host tests or builds only)**

- **Remote-management repeat stop for client commands:** host tests and builds only; not yet run over the air between two nodes.
- **Wireless Tracker memory fix (AU-21):** no tracker on our bench; display-buffer release and re-acquire are host tests and builds only until the first field report. Still open: the ESP32-S3 memory floor for TLS and a Bluetooth pause before the check on S3 boards.
- **Mailbox double delivery (SNF-ACK-01):** host tests and builds only; not run over the air. Classic ESP32 boards have no mailbox and are not affected.
- **Day/night backlight (TD-23):** host tests and builds only; not yet checked on a T-Deck.
- **WiFi watchdog:** benched on one T-Deck Plus only. The network reset and reboot stages ran in host tests only. After a heavy memory shortage the link can also drop completely; then the older watchdog reconnects after its 180 s grace time (seen on the bench, not shortened).
- **T-Deck memory fixes:** no 24 h soak after the fixes yet, and no visual check of the message tabs on the device.
- **Own group text fix in the field:** benched only; the gateway `DK5EN-98` does not run it yet.
- **Transmit queue in the field:** the 24 h capture on the gateway `DK5EN-98` (2026-10-24/25) found the own-text bug fixed in this release; the expiry times (300/120/90 s) are not re-tuned yet. In a small older sample (27 relayed frames, 2026-10-18) the slowest 10 % waited longer than 300 s and would now be dropped. The relay-side time-beacon gate was not seen on air (host tests only).
- **Call blocklist:** taking over the DM3KS build's stored list was not benched on hardware.
- **Queue work of `v4.40a.10.19`:** host tests (including the golden corpus) and builds only. The locked `--nbrrelay on` rests on one gateway's field data. The age-escalated hold is counted only since boot (`[MC-HWM]`), not per 5-minute window.
- **Auto Update:** the BLE guard runs on classic ESP32 only (a forced NimBLE restart crashed an ESP32-S3) and was tested with a BLE client script, not the McApp phone app. Stale-click and stuck-in-Safeboot handling in the web row is host-tested only. The T-Beam download after the heap work was not re-run.
- **RM1 `upd go`:** the download path is host-tested only. `upd go` installs even with `--autoupdate off`; remote management itself stays off by default and needs the password.
- **RAK4631 static IP:** cable pull and a real UDP error streak not benched; ESP32 Ethernet boards not benched with the new table.
- **LEDs:** deep-sleep LED, remote `led`/`chgled` over the air and the phone app view not benched.
- **Net console:** the AP-mode start and the keepalive drop of a half-open peer not benched. In pure AP mode the 5-minute offline recovery drops the console session (will not be fixed).
- **GPS filter:** no drive test. A 30-45 m multipath step held for 15 s can still look like motion and move the position by one cell (at most 9 per day in host tests). Smart beaconing with `--track on` still calls a still node stationary on speed alone.
- **Web flasher:** not tested on Windows, nor with a CP210x bridge for the restart path. The Safeboot v3 hint is covered by unit tests and a script harness only.

**Open defects**

- **Remember-password failure on an E22** (RM-NVS-01): only instrumented; waits for the reporter's `--info` and log.
- **One T-Beam reboot during an Auto Update download** (AU-14, `TASK_WDT` 13 s in, classic ESP32); the retry passed. Open.
- **Auto Update fix on the reporting T-Deck Plus** (AU-13): field confirmation pending.
- **No marker check in CI:** only the web flasher publish step refuses an image without a marker.

**Accepted behaviour**

- **T-Deck under memory pressure:** the oldest messages of an inactive tab disappear from the display, and a 17th conversation closes the least recently used tab.
- **WiFi round trip of 80-100 ms** on ESP32 nodes with Bluetooth on: WiFi modem sleep, not a memory problem (WIFI-PS-01). Switching modem sleep off needs Bluetooth off.
- **T-Beams first booted on V4.21 or later keep TRACK on** if the old default set it. Not migrated: a TRACK the owner wanted cannot be told apart. Use `--track off` / `--gps on`.
- **Wireless Tracker:** a message page held on the display returns to the main page after an update check.
- **Slow browser links get truncated pages** (2 s without progress or 15 s total; at 1.2 s reply delay the 94 kB main page is cut at about 73-87 kB). Not checked in Chrome on the bench. On the RAK4631 the send library can still wait without a timeout for a chip acknowledgement; no hang seen.
- **Web message history is empty** after each update check on classic ESP32 (phone rings freed during the BLE pause).
- **Server frames with the own call** are no longer queued for LoRa; the phone copy is lost too when the node does not follow that group.
- **Status line for IT with Hamnet forced** says `in use: hamnet` while the Italian server is reached over the Internet; `[GW];srv` shows the real path.
- **nRF52 uses literal server addresses** (no DNS); the HB address follows a dynamic-DNS name upstream and can go stale.
- **NTP with a GPS fix:** ESP32 asks NTP only without a GPS fix (unchanged).
- **Firmware column after store-and-forward** can show the store node's version until the sender's next own frame.
- **T-Beam Supreme:** its 8 MB PSRAM is not enabled by the build, so Auto Update uses internal RAM only (as before).
- **Safeboot v3 only via web flasher:** nodes updated over the air keep v2 and stay unnamed in Safeboot; harmless.
- **Web flasher offers only this release:** images up to `v4.40a.10.20` carry no marker and cannot be published there.

**Downgrade and migration**

- A configuration exported with network mode 2 or 3 cannot be imported into older firmware; a downgrade with mode 2 stored behaves like Internet (ESP32, nRF52 DHCP) or Hamnet (static nRF52).
- A bad `--via` value stored before the update survives it (re-enter it); a JSON import does not validate `node_via`.

<details>
<summary>Carried over from <code>v4.40a.10.11</code></summary>

- Auto Update on classic ESP32 has a thin heap margin.
- The `prod` channel cannot install yet (official releases carry no `.autoupdate`).
- Auto Update needs the new Safeboot.
- RM sync limiter and duplicate ring not benched on hardware; gateways upload RM1 command messages to the public server.
- BLE queued messages go to the next phone.
- ETH-04 field confirmation pending.
- Relays on older firmware strip the ZWJ in compound emoji.

</details>

**Not in this release:** `t5_epaper`, France and USA servers (no addresses yet).

## Installing

- **[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** (recommended): flash over USB from the browser, board detection built in, includes Safeboot v3 and the bootloader. For a silent ESP32 board use "Aus dem Flash lesen". Only the latest release is offered.
  - **Required once for T-Beam 1W** (if not web-flashed with `v4.40a.10.20`) and **T-Deck Pro** (if not web-flashed with `v4.40a.10.21`).
- **ESP32, manual:** the `.bin` for your board over USB, or over WiFi with `tools/webflash.py` (application only, Safeboot unchanged).
- **nRF52** (`wiscore_rak4631`, `heltec_t114`, `t_echo`): `.uf2` (double-tap reset, copy the file to the volume that appears) or the DFU `.zip` with `adafruit-nrfutil` over serial.
- **`<board>.autoupdate`** files are for Auto Update; no manual handling.

## Upstream

- **In the official release `v4.40a.spserver`** (2026-10-22): gateway server table, network mode, RAK4631 static IP ([#1213](https://github.com/icssw-org/MeshCom-Firmware/pull/1213)), T-Deck reply button and web firmware column ([#1214](https://github.com/icssw-org/MeshCom-Firmware/pull/1214)), web send buffer ([#1224](https://github.com/icssw-org/MeshCom-Firmware/pull/1224)), gateway server-link warning fix ([#1233](https://github.com/icssw-org/MeshCom-Firmware/pull/1233)).
- **Merged into upstream `dev`, not in an official release yet** (this release carries the same code):
  - `--via` validation, GPS filter, net console fixes, console transmit ring, OnRxDone STAT figures, `[HEAP]` boot marks, NVS line, board LED work ([#1235](https://github.com/icssw-org/MeshCom-Firmware/pull/1235), merged 2026-10-23)
  - SP server host `sp.meshcom.pl` ([#1236](https://github.com/icssw-org/MeshCom-Firmware/pull/1236), merged 2026-10-24)
- **No PR of ours is open upstream.** The board marker and `--id` are planned as a PR.
- **Fork-only so far:**
  - the "WEBServer stopped" fix (the bug is in upstream `dev` too) and the NVS diagnostics in remote management
  - everything of this release: the own group text fix (part of the transmit queue pool), the mailbox fix (part of store-and-forward), the WiFi watchdog, the T-Deck memory fixes and the day/night backlight
  - the Wireless Tracker memory fix (AU-21), the T-Beam first-boot fix (TRK-03, planned as a PR) and the remote-management repeat stop of `v4.40a.10.24`
  - the transmit queue pool and the call blocklist of `v4.40a.10.23`; its fixes (AU-18/19/20, WEB-RX-01, RX-DEAF-01/02, GWRX-01, GWACK-01, UDP-04) are planned as PRs
  - everything of `v4.40a.10.19`: queue work, setting lock, Auto Update web row, heap and LED work, `sens` fix, neighbour-page fix
  - Safeboot v3 and the web flasher changes
  - the T-Beam 1W Safeboot fix (part of the fork's Auto Update; the `dio` board definition comes from upstream)
  - Auto Update, store-and-forward and remote management (#1187-#1189), including AU-13 and RM1 `net`/`upd`
- Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
