# Earlier fork releases v4.40a.10.11 to v4.40a.10.30

Moved out of `release-notes.md` on 2026-11-25: GitHub limits a release body to 125,000 characters. Each section is unchanged.

<details>
<summary><strong><code>v4.40a.10.30</code></strong> — web login without plain-text password, Extern-UDP echo in web chat, gateway names itself in HEY upload, E290 OTP image</summary>

- **The web GUI password no longer crosses the network** (fork, upstream [#1245](https://github.com/icssw-org/MeshCom-Firmware/pull/1245), issue [#1241](https://github.com/icssw-org/MeshCom-Firmware/issues/1241), WEB-AUTH-01, every board with a web GUI).
  - Cause: the login sent the password unencoded in the URL, and a login unlocked the client's IP address for 4 h, so every device behind the same router was logged in too.
  - Now: the browser proves the password with a one-time challenge (HMAC-SHA256 over a 30 s nonce). A successful login sets a random session cookie; sessions end after 4 h without a request.
  - Enter submits the login, and the password field has the focus when the page opens.
  - Five failed logins within a minute block the login for 60 s. Changing the web password (`--webpwd`) ends every open session.
  - Background: [`docs/campaign-webui-login.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.30/docs/campaign-webui-login.md).
- **Messages sent through an external app appear in the node's own web chat** (fork, upstream [#1244](https://github.com/icssw-org/MeshCom-Firmware/pull/1244), UDP-05).
  - Cause: a message sent as JSON over UDP port 1799 (Extern-UDP, used by McApp) went out on LoRa, but only messages from the web GUI or phone app were copied into the chat history.
  - Now: Extern-UDP sends are copied there too. The gateway's "server reached" note stays limited to web and phone sends.
  - Background: [`docs/campaign-extudp-webui-echo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.30/docs/campaign-extudp-webui-echo.md).
- **A gateway names itself in the HEY reports it uploads** (fork, upstream [#1243](https://github.com/icssw-org/MeshCom-Firmware/pull/1243), HEY-UP-01, all gateways).
  - Cause: a gateway that heard another station's HEY appended its signal report but not its own call. About 44 % of HEY reports on the server named no receiver, so maps could not draw the link; gateway-to-gateway links were almost never shown.
  - Now: the gateway's call is the last element of the uploaded path, paired with its signal report.
  - A path stays within the 119 characters the decoder accepts. A relay whose own call no longer fits now skips the frame (log reason `pathfull`) instead of sending a path the next node cannot read.
  - Background: [`docs/bug-hey-upload-no-receiver-20261030.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.30/docs/bug-hey-upload-no-receiver-20261030.md).
- **Vision Master E290: second image for the newer display panel** (upstream [#1240](https://github.com/icssw-org/MeshCom-Firmware/pull/1240) by sp2ong, in the official `v4.40a.10.28`).
  - `vision-master-e290-otp.bin` is for panels whose flex cable reads `FPC-7519revb`; the stock image ghosts on them. Panels marked `15-22243` keep `vision-master-e290.bin`.
  - The OTP image uses partial refresh with a full refresh every 10 page updates.
  - Both images: a single click browses up to 9 stored messages, newest first, then the status page.
  - The web flasher offers both and asks for the flex-cable marking, because board detection cannot tell the panels apart.

</details>

<details>
<summary><strong><code>v4.40a.10.25</code></strong> — own group text always sent, WiFi watchdog, mailbox double delivery, T-Deck memory and backlight</summary>

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
- **Phone app sees more settings** (upstream [#1237](https://github.com/icssw-org/MeshCom-Firmware/pull/1237), merged 2026-10-25, taken as is).
  - The settings frame to the app (SN1) gains the text hop limit, MTU, voltage or percent display, net console and the KISS bits.
  - `--netconsole`, `--kiss`, `--volt`/`--proz` and `--mtu` now push the new state to the phone; `--maxv` sends the stored value.
  - The INA226 keys are only sent by builds with an INA226 driver, so the app hides that card on other boards.
- **Bench diagnostics only** (INSTRUMENT builds, release images unchanged): a periodic heap and WiFi line, `--heaplog`, `--wifips`, a memory starvation injector; `--wifips none` is refused while Bluetooth runs instead of crashing the node (WIFI-PS-01).

</details>

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
