> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.11.25` makes Auto Update work on ESP32 boards without PSRAM after long uptime and stops the T-Beam from rebooting during the download. RAK4631, T114 and T-Echo owners should read the first box: these boards now send the same LoRa sync word as every ESP32 SX1262 board.** Your configuration survives the update.

### Action needed

> [!WARNING]
> **RAK4631, T114 and T-Echo: the LoRa sync word changes from `0x242B` to `0x24B4`** (SYNC-NRF-01, upstream #1320).
>
> - Why: the old value put the second sync symbol where no other board sends it; a LoRa chip of the newest generation (LR2012) drops such frames. With `0x24B4` an nRF52 board sends exactly like a Heltec V3.
> - Bench 2026-11-24: a RAK4631 with the old and with the new value against Heltec V3, T-Deck Plus and T-Beam, both directions: no difference.
> - **Not measured:** an nRF52 node with the old value against one with the new value (we have one RAK). Update all nRF52 nodes of a route that runs only over nRF52 boards together, and report it if such a route loses frames after the update.

> [!WARNING]
> **Coming from before `v4.40a.11.20`, T-Beam: holding the user button for 3 s puts the node into deep sleep** (TBEAM-SLEEP-01). A short press still toggles the display.

> [!WARNING]
> **T-Beam 1W and T-Deck Pro:** run the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) once if you skipped it with `v4.40a.10.20` (1W) or `v4.40a.10.21` (Pro). Details: "Known gaps" below.

**Everyone else:** update as usual (web flasher, `.bin` over USB, `tools/webflash.py`, or Auto Update on the `dev` channel).

### Key facts

- **Base:** the official `v4.40a.11.22` plus upstream `dev` up to #1320 (2026-11-23, `68286e07`), which includes our wave-3 PR [#1316](https://github.com/icssw-org/MeshCom-Firmware/pull/1316) (merged 2026-11-22). Upstream has released #1316 and #1320 in no official release yet.
- **Fork-only firmware:** Auto Update memory work (OTA-LOWHEAP-01, AU-29, AU-30), library patches (FORMAL-01), INA226 calibration, everything listed under "New in", the fork-only items of `v4.40a.11.22` and older, and the `dev` Auto Update channel. Not byte-identical to any official release.
- **How to tell builds apart:** `--info` prints `...AU env=<board> tag=v4.40a.11.25` and `FLASH_VERSION 20261125`; `--id` ends in `:v4.40a.11.25`.
- **Configuration:** `FLASH_STRUCT_VERSION` stays at `20260724`, so settings are kept.
- **Safeboot** stays at capability v3.
- **Auto Update channels:** `prod` reads the latest icssw-org release, `dev` (`--updchan dev`) the Latest release of this fork. Mode always `notify`.
- **Bench status:** Auto Update on Heltec V3, T-Beam and T-Deck Plus, the sync word, the gateway hold, the register query and remote management ran on our bench on 2026-11-24, plus one night of Auto Update runs. "Verification" says what was checked.
- **Assets (69):** 28 ESP32 application `.bin` plus 28 `.autoupdate` files, the three nRF52 boards as `.uf2` and DFU `.zip`, and seven support files.

## New in `v4.40a.11.25`

**Auto Update**

- **Auto Update no longer fails with "Not enough free memory" after long uptime** on ESP32 boards without PSRAM (OTA-LOWHEAP-01, fork issue #3).
  - Cause: the download needs 66-69 KB of internal memory at its peak; half of it are two TLS buffers of 16,717 bytes that must exist at the same time. After hours of uptime the memory is split into pieces and the update failed, sometimes after the check had let it pass.
  - Now: the check before the download measures what the run needs; while a run is active the web server only serves the update page; a lean TLS client of its own; one buffer of 33,440 bytes taken at the start of each run holds both TLS buffers and then serves the image check.
  - No vendored library code: the TLS library is the framework's own (an earlier fork build carried a copy of 23,000 lines, removed before this release).
- **The T-Beam no longer reboots while the update is written** (AU-29). Erasing the staging area held one CPU core for up to 900 ms per block and tripped the watchdog. Also in the official 11.21 and 11.22.
- **The T-Beam installs reliably from the web page** (AU-30): the image check runs in a separate start before WiFi when memory is short.
- **Bench 2026-11-24:** check and download with staging on DK5EN-1 (Heltec V3, 1,180,497 bytes in 11.2 s), DK5EN-92 (T-Beam, 1,240,686 bytes in 13.4 s, lowest free memory 11,332 bytes) and DK5EN-14 (T-Deck Plus, 1,617,579 bytes in 20.7 s). Night 2026-11-24/25, three runs per board three hours apart, up to 6.5 hours of uptime: 9 of 9 staged, no restart.
  - Background: [`docs/campaign-ota-lowheap-01-20261122.md`](docs/campaign-ota-lowheap-01-20261122.md).

**Radio**

- **nRF52 sync word `0x24B4`** (SYNC-NRF-01, upstream #1320): see "Action needed". Background: [`docs/syncword-ab-bench-20261124.md`](docs/syncword-ab-bench-20261124.md).
- **Position beacons go out on time** (POSINFO-LAG-01, #1315). Cause: a beacon was sent about 30 s late. Now: on time, with a 60 s floor between automatic beacons; manual beacons keep 30 s.

**Fixes from the official wave-3 PR #1316** (merged upstream, released here first)

- **nRF52 `--dumpsettings` no longer prints passwords and the BLE PIN** (SEC-DUMP-01, #1291).
- **RAK4631 Ethernet uses the configured DNS server, and `--info` shows the real network mode** (RAK-ETH-DNS-01, RAK-NETMODE-01, #1289).
- **A gateway no longer hands server texts for foreign groups, third-party DMs, or `*` texts under `--nomsgall on` to its display and phone** (GW-GROUP-01, GW-DM-01, #1290); relay, dedup and the gateway ACK are unchanged. `--setcall`, `--setudpcall` and `--setlog` explain why a call with `/` is refused (GW-CALL-01).
- **Web setup reads `tempoffsetoutdoor`** (WEB-TEMPOFF-01, #1296).
- **T-Beam 1W sets the PA the same way at boot and at runtime** (TBEAM1W-PWR-01, #1301).
- **GPS autosymbol and the NEXT position counter** (#1295): nRF52 restores the autosymbol setting at boot (GPS-AUTOSYM-01), a stationary node still updates its symbol (GPS-AUTOSYM-02), the first two minutes after boot no longer count as car speed (GPS-AUTOSYM-03), and the NEXT countdown no longer wraps to a huge number (POSINFO-NEXT-01).
- **Manual OTA onto an old Safeboot warns and proceeds** (OTA-SAFEBOOT-01, #1300).
- **DHT is reported "found" only after the sensor answered a probe** (DHT-PROBE-01, #1302).
- **APRS text** (#1293): the track frame keeps 25 bytes of comment instead of 16; text is cut once and never inside a UTF-8 character; `--atxt` tells you when the text is longer than what goes on air (APRS-TXT-01..03).
- **T-Deck header battery label follows `--volt`** (TDECK-VOLT-01, #1312).
- **Display paging and the user button** (#1294): OLED and web show the same messages (DISP-PAGE-01), a browse click is no longer lost when the display turns off right after it (DISP-PAGE-02), the Wireless Paper's first press no longer redraws the newest message (DISP-PAGE-03), and a user button switched off in the web UI stops working at once, not after a reboot (BTN-OFF-01).
- **Heltec T114 dark theme; `--msgwake` keeps a switched-off display dark** (T114-DARK-01, #1314).
- **`--aprswx on|off` real fix** for the settings reply to the phone (#1288; replaces the temporary fork fix).

**Sensors and web**

- **INA226: the calibration is written for common shunt values** (INA226-CAL-01).
  - Cause: the library refuses a shunt and maximum current pair above 81.9 mV full scale and then writes no calibration; with 0.1 Ohm and the default 20 A the sensor read 0 mA.
  - Now: `--shunt` lowers the maximum current to fit, `--imax` refuses a pair that does not fit, a stored pair is clamped at boot.
- **The INA226 bus-voltage label lost its stray `<`** (WEB-INA-01).
- **The WX table lists AHT20, SHT21, BMP390 and INA226** (WEB-WX-01, ticket P-0394).

**T-Deck and T-Deck Plus**

- **Conversation tabs no longer disappear while messages arrive** (TD-35, from a user report on 11.22).
  - Cause: with WiFi on the T-Deck runs at 25-41 KB free internal memory (DK5EN-14, 2026-11-22 to 25), below the 40 KB floor of the memory trim. Each new message trimmed old bubbles and then deleted the tab it had emptied, so the public tab vanished while the last DM tab stayed.
  - Now: the trim removes bubbles only. Tabs are removed only by the 16-tab limit and by the user.
- **Light theme: no grey edges on buttons** (TD-36). The default LVGL light theme drew a grey shadow under the segment options in the settings and the MESH and `No * MSG` buttons. The shadow is off; the dark theme is unchanged. Bench 2026-11-25: screenshots on DK5EN-14.

**Library patches** (FORMAL-01, fork-only)

- **Patched copies of library code fix findings of a code review:** NimBLE (NB-01..07), W5100S Ethernet (ETH-07), the arduino-esp32 HTTP client (AE-01..03), mDNS and the DHCP server of the access point, an lwIP MTU guard, a certificate date check and TLS 1.2 as minimum for Auto Update, DNS through the network thread, and fixes in the UDP drain, the Extern-UDP queue, the net console slot and APRS. See "Known gaps": these were not benched one by one.
  - Background: [`docs/campaign-formal-01-20261123.md`](docs/campaign-formal-01-20261123.md).

## Earlier fork releases

Everything below is already in `v4.40a.11.22` and older, and therefore in this release too. Open a section for details.

<details>
<summary><strong><code>v4.40a.11.22</code></strong> — RAK4631 Ethernet gateway no longer goes silent after a chip reset, AppLink (phone app over the LAN), dash commands, unsigned pong id</summary>

**Ethernet gateway (RAK4631 with RAK13800)**

- **A RAK4631 gateway no longer goes silent after a hardware reset of its Ethernet chip** (ETH-06, fork-only).
  - Cause: after two failed DHCP attempts the firmware resets the W5100S Ethernet chip, which deletes every network socket in it. The socket to an Extern-UDP peer (for example MeshDash) stayed marked as open; the next datagram waited forever for the chip, and the main loop stopped. The node kept receiving but sent nothing, not on LoRa and not to the server. Field log DB0AIS-12 (DG3FBL): 52 hours silent from 2026-11-14 05:43; the map server shows the same window offline.
  - Now: before the chip reset the firmware drops the Extern-UDP and AppLink sockets without touching the chip and opens them again once the node has an address. On top of that, the library's wait for a sent datagram ends after 3 s (build script `tools/w5100s_patch.py`; the chip's own timeout is 1.6 s).
  - New log lines: `[EXT];event;chip_reset;socket;dropped` and `[EXT];event;reopened_after_chip_reset`.
  - Bench 2026-11-21, DK5EN-90: the forced chip reset (bench command `--ethdrop hw`, INSTRUMENT builds only) ran three times; keepalives continued every 30 s, and Extern-UDP datagrams arrived before and after the reset.
  - Background: [`docs/architecture/08-defect-catalogue.md`](docs/architecture/08-defect-catalogue.md), section 2n.

**AppLink: the phone app over the LAN** (APPLINK-01, fork-only)

- **The MeshCom phone protocol now also runs over a WebSocket on TCP port 8002**, for apps on the same network as the node. Off by default.
  - `--applink on` needs a console password (`--passwd`) first: the login is an HMAC challenge keyed with it. `--applink off` and `--applink` (status) round it off; the web setup page has a switch.
  - ESP32 boards over WiFi or W5500 Ethernet, RAK4631 with RAK13800. On the RAK, AppLink and Extern-UDP exclude each other (the W5100S has four sockets).
  - One session at a time; the newest login wins, also against a Bluetooth phone.
  - Heltec T114 and T-Echo have no network: `--applink` answers "AppLink not available on this board".
  - Bench 2026-11-21: login and configuration frames on DK5EN-1 (Heltec V3, WiFi) and DK5EN-90 (RAK4631, Ethernet), and a chip reset on the RAK with a session up.
  - No app speaks AppLink yet; the interface is agreed with an app maintainer. Background: [`docs/applink-api-contract.md`](docs/applink-api-contract.md).

**Console, ping and web**

- **Commands written with a minus sign or a dash work** (CMD-DASH-01, discussion #1042).
  - Cause: the command web page showed `−−` (minus signs) and phone keyboards turn `--` into an en or em dash; the node answered "wrong command".
  - Now: one or two such dashes at the start of a command are read as `--`. Dashes in names and text stay.
- **The pong id is printed unsigned** (PONG-SIGN-01).
  - Cause: ids with the top bit set went on air as a negative number, for example `{pong}{-427409305}`, and the display showed nothing useful.
  - Now: the id is unsigned and the display shows its last three digits.
  - On air: ids below 2^31 are unchanged.
- **The web navigation buttons take 75 % of the bar on every system** (WEB-NAV-02). Since `v4.40a.11.20` they filled the whole bar on Linux with a classic scrollbar; the reporter chose the smaller look.

</details>

<details>
<summary><strong><code>v4.40a.11.21</code></strong> — Auto Update after a downgrade, W/S hemisphere in TRACK beacons, APRS-IS weather over Wi-Fi</summary>

**Auto Update**

- **A node downgraded by hand is offered the newer release again** (AU-27, fork-only).
  - Cause: Auto Update stores the tag it last installed and never offers that tag again. After an Auto Update to `v4.40a.11.20` and a downgrade to `v4.40a.11.08` with an OTA flasher, the node said "current" and hid 11.20 for good (field report, Heltec V3).
  - Now: the block applies only to an image without a date in its version (a local build without `MC_BUILD_TAG`), where it stops a reinstall loop. A release image reports its own date and cannot loop.
  - Nothing installs on its own: automatic install is not built into this firmware line, every download and install needs a click or a command.
  - Takes effect on nodes that run this release or later. A node on `v4.40a.11.08` that hides 11.20 sees `v4.40a.11.21` as new, because the tag differs.

**GPS and TRACK** (upstream #1284, merged 2026-11-21, closes upstream issues #1279 and #1278)

- **The TRACK turn beacon keeps W and S** (#1279).
  - Cause: the turn beacon got the position as magnitudes, so a node west of Greenwich sent E and one south of the equator sent N.
  - Now: signed coordinates reach the smart-beaconing code on ESP32 and the T-Deck Pro; the TRACK line on the OLED prints the magnitude with its letter.
- **Own position as signed degrees for course, distance and map** (found while fixing #1279).
  - RAK4631 stored the own GPS position signed where everything else stores magnitude with a letter; now it stores the magnitude too.
  - Eight readers of the own position (display course and distance, `--mheard`, web, app distance, neighbour row 0) and five T-Deck map fallbacks now compare signed with signed.
- **`compile_commands.json` is ignored by git upstream as well** (#1278); no firmware change.

**APRS-IS weather over Wi-Fi** (upstream #1269 by Mateusz-Krajewski, merged 2026-11-20)

- **A node with a weather sensor can report to APRS-IS directly over Wi-Fi.** Off by default.
  - `--aprswx on/off`, `--aprswxint <5..1440 min>`, `--aprswxcall <CALL-SSID>`, `--aprsis server <host> <port>`, `--aprsis pass <n>`; `--aprswx` alone prints the state.
  - The WX call must be set and must differ from the node call, because the MeshCom server already reports the node call to APRS-IS.
  - The client does not block the loop and makes one attempt per interval.
- **Fork fix on top, temporary:** upstream's `--aprswx on` and `--aprswx off` answered with the status line and skipped the settings reply, so a phone over BLE did not see the new state. The fork answers like `--via on` (`632aa9b3`); it goes when upstream fixes it (reported on #1269).

**Known and accepted with this release**

- **Wider navigation buttons on Linux** (WEB-NAV-01, since `v4.40a.11.20`): browsers with a classic scrollbar showed the left button bar 15 px narrower than designed. Since 11.20 the scrollbar is hidden and the buttons have their design size of 60 px, which looks almost twice as large to anyone used to the narrow bar. The bar still scrolls with the mouse wheel.

</details>

<details>
<summary><strong><code>v4.40a.11.20</code></strong> — clock ranks NTP and GPS before LoRa beacons, ping display and nRF52 budget, web GUI with Pins page, faster relay ACKs, T-Beam deep-sleep press, Safeboot checks</summary>

Items marked "upstream #1280" or "upstream #1281" are merged in icssw-org `dev` but not released there. Items marked "official `v4.40a.11.14`" are in that release and reach fork users here for the first time since `v4.40a.11.08`.

**Clock**

- **A `{CET}` beacon over LoRa may only step the clock forward** (CET-SCATTER-01 Part A).
  - Cause: beacons of one origin arrive with different ages, so a node that took its time only from `{CET}` was stepped 2-6 s both ways (night run 2026-11-16: T-Beam 8 of 11 beacons).
  - Now: a beacon steps the clock forward by 1 s or more; one 0-5 s behind is held; a step back needs 3 stale beacons in a row within 30 min. The first beacon after boot sets the clock. Gateways with NTP are unchanged. Log `[CET];apply`, `hold` and `skip`.
  - Bench on `DK5EN-1` (no WLAN, GPS off, `--timesrc cet`, 60 min, 2026-11-16): one initial set, 4 beacons held, the clock stayed 1.38-1.71 s behind the host. Replay of the night run: the old rule stepped the T-Beam 10 of 11 times, the new one 3 times.
  - Background: [`docs/concept-cet-scatter-01-20261116.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.20/docs/concept-cet-scatter-01-20261116.md).
- **Time sources rank NTP, GPS, phone or command, RTC, then `{CET}`** (CET-SCATTER-01 Part B).
  - Once NTP or GPS has set the clock since boot, `{CET}` never steps it again until a reboot. A phone holds `{CET}` off for 1 h, the RTC for 6 h. GPS module time without a fix blocks it too.
  - A payload whose date does not convert is skipped (`[CET];skip;why;bad_payload`); a held beacon logs `[CET];hold;why;rank;by;<source>`.
  - Bench on `DK5EN-92` (WLAN on, GPS off, `--timesrc all`, 60 min, 2026-11-16): NTP set the clock at +7 s, 6 of 6 beacons were held by rank, no `{CET}` set.
- **GPS time without a fix no longer overrides valid NTP time** (`a3a2e69e`, `f515aaa1`). Official `v4.40a.11.14` has it.
  - Cause: both set the clock; the GPS module clock can be a second off, so the per-second clock line flickered (T-Beam `DK5EN-92` after boot, :12 :13 :12).
  - Now: GPS with a fix, then NTP, then GPS without a fix. Bench 2026-11-14: before, NTP set 12 and GPS set 13 within 50 ms every few seconds.
- **The shown time never runs back by up to 10 s** (`5616c8a1`, `f515aaa1`). Official `v4.40a.11.14` has it.
  - Now: the clock line, the page main line and the e-paper status line hold the second they already showed. A larger step back (summer/winter time, a real correction) shows at once. The clock itself, logs and frames keep the real time.
- **A dropped stale `{CET}` frame logs a correct age** (CET-AGE-LOG-01): `RING_DROP_STALE reason=cet` printed a wrapped `age_s` (4294448 seen on `DK5EN-98`).

**Ping** (upstream #1280)

- **The last ping of a `--ping start` run gets its PONG FAILED** (LAST-PONG-01), on ESP32 and nRF52.
  - Cause: the fail check ran only at the next send tick, and the budget had closed by then. Found on `DK5EN-90` on 2026-11-15.
  - Now: one interval after the send, only for a queued send; no verdict after stop, pong or a cleared target.
- **One fail verdict per unanswered ping** (PING-FAIL-REPEAT-01): after a send that queued no new ping (refused ring, TRACK, invalid target) the stale flag is cleared, so the verdict is not repeated on every tick. The clear sits after `sendPing()` so the PONG FAILED frame stays on OLED boards.
- **TRACK display: the track page replaces a ping frame after 10 s** (TRACK-PING-01). Bench on `DK5EN-1` (2026-11-15): pong image at +1.7 s and +8.0 s, track page at +12.1 s.
- **Benched since `v4.40a.11.08`, code unchanged:** the nRF52 ping budget (PING-NRF-01) on `DK5EN-90`: 5 pings 60 s apart, then none for 3 min; `--ping stop` held for 140 s. Also PING-STOP-01, PING-HOLD-02 and a `{ping}` DM answered by `{pong}`.

**Radio and channel access**

- **Relayed ACKs leave sooner at a busy node** (ACK-AGE-01).
  - Cause: every received frame restarts the 3.0-3.35 s CSMA wait, and only the 20 s age floor ended it. Relay ACKs waited a median of 6.1 s and up to 24.8 s (SOAK-1108 on `DK5EN-98`).
  - Now: ACK frames reach the first age stage after 6 s (`CSMA_AGE_STAGE1_ACK_MS`); personal DMs keep 20 s. The `CSMA_AGED` log line gains `ack=0|1`; the age statistic uses the same stage.
  - Bench on `DK5EN-98`, 14.2 h from 2026-11-14 21:45: 17 relay ACKs, median 3.3 s, maximum 13.6 s. It was a quiet night (1.9 % channel use), so this is no proof under load.
  - Background: [`docs/ackage-auswertung-20261115.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.20/docs/ackage-auswertung-20261115.md).
- **Repeated `RX_DEDUP_NEW` with `OWN:e` in a log are echoes of own beacons and HEY** (DEDUP-OWN-01): a rule in the log guide, no code change.
- **Benched since `v4.40a.11.08`, code unchanged:** the CSMA-01 shortened wait on a mock server, 14 of 14 replies with the first CAD at 3035-3350 ms.

**Remote management**

- **An RM reply on a full TX ring is no longer lost** (RM-LOSTREPLY-01).
  - Cause: an own RM1 reply on a ring full of priority-1 frames was dropped for good (`RING_DROP_NEW`); the sender saw `noanswer` after 171 s.
  - Now: the reply may take the slot of the oldest queued ACK, text or binary, only when nothing of lower priority can be evicted. A KISS client's frame never gets this. One `RING_DROP_PRIO` line per eviction; no retry.
  - Bench 2026-11-16 on a local mock, ring 20 of 20 with priority-1 ACKs: the reply evicted the oldest ACK, the sender got `ok` after 15.2 s (before: `noanswer` after 171 s).
  - Cost: one queued ACK is dropped for the reply. Background: [`docs/concept-rm-lostreply-01-20261116.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.20/docs/concept-rm-lostreply-01-20261116.md).

**T-Deck and T-Deck Pro**

- **Position page with SKY and TEXT side tabs** (TD-SKYTAB-01); the sky plot itself is in the official `v4.40a.11.14`.
  - The sky plot shows horizon and 30/60 degree rings, up to 40 satellites coloured by signal, the satellite count and the fix age. After boot the sky view is shown.
  - SKY and TEXT sit on the right edge of the card like tabs; SEND POS moved to the bottom with a two-line label, clear of the tabs.
  - Checked on `DK5EN-14` by screenshot on 2026-11-16: light and dark, sky and text view.
- **The GPS page text fits** (TD-29): 12 px table font, 9 lines in a 196 x 163 field, SEND POS right of the text. Official `v4.40a.11.14` has it; the operator checked the page by eye on 2026-11-15.
- **The header GPS icon opens the GPS tab** (TD-NAV-01); the GPS tab button is hidden and the icon stays visible, grey with GPS off.
  - The operator saw the icons on `DK5EN-14` (2026-11-15); the tap path has no separate record.
- **The key-lock glyph sits inside the keyboard button** (TD-LOCK-01) and no longer covers the Bluetooth icon. Bench on `DK5EN-14` 2026-11-16: light and dark, locked and unlocked.
- **WLAN and BLE header icons show their state:** white when the link is up, yellow when on without a link, grey and struck through when off. Official `v4.40a.11.14` has it.
  - Cause: the BLE icon stayed white after a phone disconnected; the WLAN icon vanished when WLAN was off.
- **T-Deck Pro: GPS sky plot on screen 3, and the GPS screen no longer freezes.**
  - Cause: the loop suspended the GPS task every 3 s on the Pro, because the GPS flag is forced off at boot.
  - Not tested: no T-Deck Pro on our bench.
- **`--disptest` in INSTRUMENT builds** (TD-34): `--disptest square 8` ran 20 steps in 1.4 s on `DK5EN-14` (2026-11-16) with no reset and the console session kept.

**T114 and GPS** (official `v4.40a.11.14`, upstream #1264 to #1267, DH1FR)

- **The T114 status page shows GPS, time source and radio:** fix state, satellites and HDOP, the time source, RSSI and SNR of the last frame, and the neighbour count. A clock stuck at 00:00:00 is now explainable at a glance.
- **A graphic GPS page with sky plot follows the status page,** with fix state, satellites used and in view, HDOP, altitude, speed, course, fix age and position. Signal colours turn green from 35 dB-Hz and orange from 20; AGE shows `--` without a fix.
- **T114 button:** the loop polls it every 10 ms so double and triple clicks register; a click wakes a dark display, also in track mode; a long press shows "DEEP SLEEP" for 1.5 s first; waking from deep sleep with the button no longer starts the bootloader.
- **One sky-plot core for T114, T-Deck and T-Deck Pro:** the GSV parser, satellite table, signal levels, projection and AGE formatter live in one file with 26 host cases.
  - Not on our bench: no T114.

**Boards and builds**

- **T-Beam: hold the user button 3 s for deep sleep** (TBEAM-SLEEP-01). The display toggle of that press is undone first; the task watchdog is fed while the button is held. Bench on `DK5EN-92` (operator, 2026-11-15): 3 s press, deep sleep, wake by button.
- **INA226 is enabled on Heltec V3, Wireless Stick and Wireless Tracker** (INA226-HELTEC-01, upstream #1272, official `v4.40a.11.14`). It had been off since V4.34c with the note "I2C fault"; the cause is not documented. Built only, no field test yet.
- **`BOARD_COUNTRY` is a default, not an override** (upstream #1273): a stored country survives on boards that define one. Official `v4.40a.11.14` has it.
- **Spectral scan:** the frequency split is at 500 MHz, and the task watchdog is fed during the scan (upstream #1268). The web Info page says "Gateway:" instead of "Server:" (upstream #1258). Both in the official `v4.40a.11.14`.
- **The `--info` battery line shows the value the board really uses:** `factor` on `USE_NEW_BATT` boards, `multiplier` on the Heltec switched family, no number elsewhere (was a misleading `factor 0.0000`).
- **nRF52: the empty BLE service F0A0 is gone** (BLE-SET-01 leftover). Its UUID stays in the advertising packet so scan filters still find the node. Bench on `DK5EN-90` (2026-11-16): the scan shows F0A0 only in the advertising; GATT holds DFU, Device Information and Nordic UART.
- **Smaller ESP32 images and T-Deck builds** (since `v4.40a.11.06`, in the official `v4.40a.11.14`): `-fno-exceptions`, `CORE_DEBUG_LEVEL=0` and NimBLE peripheral-only give images 125-175 KB smaller per environment; unused T-Deck LVGL widgets and TFT_eSPI fonts are off, about 60 KB less flash.

**Safeboot**

- **Safeboot refuses an image that is no application** (SB-06-DESC).
  - Cause: `Update.end()` had already moved the boot pointer to the application slot when the identity check ran, so a structurally valid non-app image (the release `bootloader.bin`) was activated.
  - Now: the image head is checked first. On refusal the pointer goes back to Safeboot, the first sector of the slot is erased and `app_valid` is cleared. Every route to the slot re-checks the image.
  - Bench on `DK5EN-92` (2026-11-15, Safeboot rewritten over USB): `bootloader.bin` refused with `why;desc` and HTTP 400 `not_bootable`, Safeboot web stayed up; a wrong-chip image was refused too.
  - Reach: only the web flasher installs the new Safeboot. `DK5EN-92` still carried a Safeboot without any SB-06 check before the bench.

**Web GUI** (upstream #1281 carries the first two)

- **The web page loads completely on the RAK4631** (WEB-LOAD-01).
  - Cause: the W5100S has 4 sockets; it refused part of the parallel asset requests of a cold page load (2 of 4 per round), so the page lacked `mc.js`: no theme switch, card toggles or queue chart. Present since the gzip asset split of `v4.40a.11.06`.
  - Now: every asset tag retries after 250 ms times the attempt, at most 8 times, and catches up the clock tick and the Auto Update row.
  - Bench 2026-11-19 on `DK5EN-90` and `DK5EN-1` (operator, Safari and Chrome, cold cache, wide and narrow): PASS. Test `tools/webgui_load_test.js` has 18 checks, 5 failed before.
- **The nav bar hides its scrollbar** (WEB-NAV-01): a shown bar took up to 15 of its 42-60 px and squeezed the icons to dots. Operator PASS 2026-11-19.
- **The setup parameter table moved from RAM to flash** (RAM-WSP-01).
  - Cause: the table used captureless lambdas as getters; on nRF52 (`-std=gnu++11`) that is no constant expression, so the table sat in RAM on every board.
  - Now: type-tagged addresses and a `constexpr` table; a lint refuses a table that is not `constexpr`. Measured at build: RAM -880 B on RAK4631, -1,008 B on Heltec V3.
  - This is the unintended part of the +3.6 KB RAK RAM growth of #1281; 2.7 KB are two deliberate static JSON buffers.
- **The Auto Update card installs the tag it shows** (AU-26): it re-reads `/austatus` at the click and cancels a pending retry poll, so a second click cannot send a second `updapply`. JS test only.
- **Smaller:** the T-Deck brightness value is centred with its slider; `src/web_functions/web/README.md` explains how to regenerate `web_assets.h`; the setlat/setlon read-back is a host-testable helper (P-0124).

**Tools and tests**

- **`clock_drift.py` records `[CET]` lines** and an offline boot marker (4 new pytest cases).
- **New host tests:** the time-source rank (`native_cet_age`, 36 cases), the T-Deck sky geometry (`native_gps_sky_layout`), the RM reply eviction (7 cases, 3 failed before), the shared sky core, and the `wsParams` lint with a self-test.

**Compared with the official `v4.40a.11.14`**

- **Ping display and budget** (upstream #1280, merged 2026-11-19): frames held 10 s, message page back after a ping frame, nRF52 honours `--ping start`, `--ping stop` and `--pingmax`, the nRF52 loop polls every 1 ms while a channel check runs (CSMA-03), one fail verdict per ping. The official release has none of it.
- **Web GUI, Pins page, GPIO control and board LED** (upstream #1281, merged 2026-11-19): gzip assets, dark mode, pin assignments, pin policy, `--gpiocontrol`, `--gpio`, LED off, flash or steady.
  - The Remote page of #1281 already has cards for commands that upstream nodes do not know yet (`pins`, `gpio`); such a node refuses them.
- **Fork-only, no PR yet:** the CET rank and filter, ACK age, RM reply eviction, CSMA-01 and CSMA-05, hop clamp, Safeboot checks, `--nbrreport` removal, T-Deck side tabs and GPS icon, T-Beam deep-sleep press, and the `dev` Auto Update channel.

</details>

<details>
<summary><strong><code>v4.40a.11.08</code></strong> — display clock fix, ping display and nRF52 ping budget, T-Deck light and dark theme, Pins page with GPIO control, Heltec V4 GNSS, RAK4631 L76K probe, DHT22</summary>

**`v4.40a.11.08` kept the display clock right and stopped ping frames from hiding it, gave the T-Deck a light and dark theme with a readable light mode, added a GPIO section to the Pins page, and built on the official `v4.40a.11.07`.** Your configuration survived the update. nRF52 users who ping, and Heltec V4 owners with sensors on GPIO 41/42, had to act (see the warning above).

Base: the official `v4.40a.11.07` (2026-11-08, `upstream/dev` 5d76238b). Configuration: `FLASH_STRUCT_VERSION` stayed at `20260724`; `--nbrreport` was removed (BL-01).
**Clock**

- **The display clock no longer jumps back by 1-2 minutes** (CET-AGE-01, CLK-01).
  - Cause: a second server stream delivers `{CET}` time beacons that are 20-115 s old, and each one stepped the clock back. Seen before the fix on three bench nodes, up to -116 s.
  - Now: a beacon more than 5 s behind, within 30 min of the last accepted one, is ignored (log `[CET];skip;back_s`). Forward steps, small steps and the first beacon after boot are accepted as before.
  - Bench: stale steps of up to 116 s seen on three nodes before the fix; with the guard every `{CET}` set over 1 h stayed within 0.8 s (Heltec V3 and T-Beam).
  - Background: [`docs/rca-cet-age-01-20261108.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.08/docs/rca-cet-age-01-20261108.md).
- **The clock is drawn every second** on all clock pages, including the track page (CLK-01).
  - Cause: the free-running clock lost ticks (CLK-01 F3), and the OLED clock line stayed frozen on pages other than the main page (F1).
  - Measured 2026-11-08 on DK5EN-1 and DK5EN-92: the clock is drawn every second, a free-running clock drifted +0.1 s over 2 h, one OLED frame takes 37.6 ms, 3.8 % of loop time.
  - By decision the message page keeps its sender header: the clock is frozen there and redrawn when the page changes.
- **A configured ping no longer freezes the head-page clock** on ESP32 (CLK-PING-01).

**Ping display and ping budget**

- **Ping and pong frames stay for 10 s, then the clock returns** (PING-HOLD-01). DK5EN-1 bench: longest clock gap 16 s, was 158 s.
- **The message page is redrawn after a ping frame** (PING-HOLD-02).
- **`--ping start` and `--ping stop` no longer cause a false PONG FAILED** (PING-STOP-01).
- **nRF52 honours the ping budget** (PING-NRF-01). See the warning above.

**T-Deck display and theme**

- **Theme: Auto, Dark or Light** (TD-THEME-01), console `--theme auto|dark|light`, kept across reboots.
  - Auto follows the day and night period of the backlight. Header icons, tab bar, bubbles, toggles and the boot logo follow the theme live.
  - A keyboard-lock glyph shows in the header while the lock is on. Map colours are unchanged.
  - Bench: theme dark, light and auto switched from the web page and seen on the device (DK5EN-14).
- **Readable light mode** (TD-CHAT-01, DISP-PAT-01): black text and outline, 3 px outlined bubbles, distinct fills.
  - Cause: on the T-Deck panel the light bubbles vanished (operator photos, 2026-11-08).
  - The sent bubble is green, and was seen with its ACK on the device. Header red is unchanged.
  - The palette comes from operator photos of the panel, with host tests on the RGB565 steps between fills.
- **Setup page: font size, theme, display mode and brightness** on the device (WEB-TD-01).
  - Chat text S, M or L (`--chatfont s|m|l`), also applied to the compose box.
  - Display: Auto (day and night switching), Light (fixed day level) or Dark (fixed night level), `--bl mode auto|day|night`. The slider edits the level the mode selects.
- **Test pattern for panel photos** `--tdpattern` in INSTRUMENT builds only.

**Web GUI**

- **Setup page: label and control share one line,** with automatic light and dark mode and a green selection and slider.
  - The T-Deck theme, font size, display and brightness are also set on this page (WEB-TD-01). Toggles keep their outline; T-Deck labels are shorter. Operator reviewed the page in Chrome on 2026-11-08.
- **The web GUI has its own theme and text size,** kept in the browser. It shows a ticking clock, synced at page load and again after a laptop sleep.
- **Contrast lifted to stray-light targets** for panels in sunlight: two colours changed (light `--mcok`, dark `--mcredbtn`); body text 7:1, secondary 5.5:1, controls 3:1.
- **Board LED control lives on the Pins page only;** the Setup page keeps the charge LED switch.
- **Menu order:** Pins right after Weather, Remote right after Position.

**Pins and GPIO**

- **Pin assignments on the Pins page** (GPIO-01): see which pin has which function, and set it where the board allows.
  - A pin policy refuses radio, flash, display, GPS and similar pins in the four pin setters (1-Wire, button, analog, wind) and at load, with the reason.
  - A button on GPIO 0 (Heltec V3 PRG) is shown as a pin, not "none". `--button gpio` takes effect after a restart (GPIO-02).
- **Free GPIO control, off by default** (GPIO-01 Part B): `--gpiocontrol on|off` (console and a switch on the Pins page) and `--gpio list|<n> on|off|in|release`.
  - Only reviewed boards offer free pins: T-Beam (13, 25) and Heltec V3 (2, 4, 5, 6). Others refuse, nRF52 answers "not on this board".
  - A pin used by an enabled function is not offered. A config import or remote management cannot switch the master on.
  - Over web, `/pinassign` and `/gpiostatus` report the state; a write needs the `X-MC` header.
- **Remote management reads and sets pins** (RM1 `pins` and `gpio`, RM page). The button pin is read-only over RM: a remote move needs a restart and a pin held low could strand the node.
- **T-Deck Pro pins** (GPIO-06, GPIO-04, GPIO-08, GPIO-09): keyboard IRQ moved from 46 to 15, radio power enable 46 reserved, SD CS 48 and touch INT 12 set, eight T-Deck copies removed from the Pro variant.
  - Not tested: no T-Deck Pro on our bench.
- **T5 and RAK4631 radio pins are reserved** (GPIO-03).

**Heltec V4 GNSS**

- **GNSS pins, power and I2C corrected** (GPSV4-01): RX/TX were swapped; the kit gets its enable (GPIO 34, active low), wake and reset lines. Deep sleep and `--gps off` release the TX and reset lines and keep the kit off.
  - I2C moved to SDA 4 and SCL 3 (see the warning). R2 PSRAM only: no runtime guard against an R8 module.
  - Not tested on hardware: no Heltec V4 on our bench. A regression block in `pin_board.cpp` fails on the old pins.

**RAK4631 GPS**

- **L76K detection works at 38400 and falls back to u-blox** (OE1KBC #1259, improved here).
  - Now: the baud loop closes the port before the next try so the 38400 probe runs; with no answer the u-blox setup at 9600 runs as before; `--setublox` and `--setl76k` survive a reboot on nRF52.
  - Not tested: no L76K module on our bench. Four source-pin tests fail before and pass after.

**nRF52 and radio**

- **The nRF52 loop polls every 1 ms while a channel check (CAD) is pending** (CSMA-03).
  - Cause: the loop paused 100 ms per pass, so every free CAD result was 60-97 ms old when used.
  - Now: the loop polls at 1 ms while a CAD runs or its result waits. A stale result is rechecked once before sending (threshold 5 ms until the re-bench).
  - Bench on `DK5EN-90`: the CAD result is now 1-2 ms old when the send uses it (was 60-97 ms); CAD to send 2-3 ms (was 63.5 ms). The 5 ms recheck threshold stays as a safety net and did not fire.
  - Background: [`docs/rca-csma03-cad-age-20261108.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.08/docs/rca-csma03-cad-age-20261108.md).
- **A higher-priority frame shortens a running CSMA wait** (CSMA-01), and the idle re-anchor, relay jitter, busy back-off timeout and ACK retry were tightened (CSMA-05). Host tests only.
- **A relay clamps a hop counter of 8 to 15 to the maximum before it decrements** (IDEE-04), so frames of old firmware stop circling. Host test only.

**Settings, sensors and safeboot**

- **DHT22 works** (SENS-04, SENS-05): `--dht 11|22`, kept across reboots, default 11. The sensor now uses the pin set with `--owgpio`; before it ran on the compile-time pin. A bad stored type decodes to 11.
- **Safeboot checks an update image more strictly** (SB-06): header, chip id, segment count and app-descriptor magic after the image check. No SHA-256 and no project name check, by decision. A dead boot-partition path was removed (SB-07).
- **The dead nRF52 BLE settings characteristic `0xF0A1` is gone** (BLE-SET-01, BLE-SET-01-FU). It never appeared on the air.

**Mesh and remote management**

- **A mailbox node no longer re-sends a direct message for hours** (SNF-ECHO-01, upstream #1257). Already in `v4.40a.11.06`; it arrives here again through the official `v4.40a.11.07`.
- **The RM page Queues card reads per row** (RM-Q-01): each row (TX queue, mailbox, max hop) has its own Read, and the next command goes out only after the previous answer arrived (report from DB0AAT-3). Tests only.
- **RM page texts match the real timeout:** no answer after 171 s, waiting text "up to 3 minutes" (RM-NOANS-01, RM-TXT-02).

**Tools and diagnostics**

- **INSTRUMENT builds log the clock** (`[CLOCK]`, `[DISPCLK]`, `--timesrc all|cet|none`, `--clockstat`, `--oledshot`, `--tdshot`). Release images are unchanged.
- **Bench scripts:** `clock_drift.py` (clock drift over time), `--offline-ok` for `identity_guard` and `clock_drift` on a node without server link, and a corrected fake in the OTA abort test (TEST-OTA-01).

</details>

<details>
<summary><strong><code>v4.40a.11.06</code></strong> — national characters, UV and wind, web password, live Pins page, message hold, T-Beam update gate, RAK pin guard</summary>

**`v4.40a.11.06` brings national characters on every display, UV and wind sensors, the battery charge state, a web password you can manage on the page, a live Pins page, a message hold during server outages, and fixes that stop a T-Beam refusing updates and a RAK being bricked by a pin setting.** Your configuration survives the update. Only scripts that reboot a node over HTTP must act (below).

**Action needed**

> [!WARNING]
> **Scripts that call `/callfunction/?otaupdate` or `?reboot` must send the header `X-MC: 1`** (web guard).
>
> - Without the header the node answers 403 and does nothing. The web GUI and `tools/webflash.py` already send it.
> - The web guard is now on by default, and cross-origin headers are gone, so pages on other sites can no longer drive a node.

> [!WARNING]
> **Coming from before `v4.40a.11.01`:** a stored ping target that is not a callsign is cleared at boot. Coming from before `v4.40a.10.30`: `/?nodepassword=<password>` logins answer 403, use the three-step login in [`Web-API_documentation.txt`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.06/src/web_functions/Web-API_documentation.txt), section "Login".

> [!WARNING]
> **T-Beam 1W and T-Deck Pro:** run the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) once if you skipped it with `v4.40a.10.20` (1W) or `v4.40a.10.21` (Pro). Details: "Known gaps" below.

**Everyone else:** update as usual (web flasher, `.bin` over USB, `tools/webflash.py`, or Auto Update on the `dev` channel).

**Key facts**

- **Base:** the official `v4.40a.10.28` (2026-10-28) plus upstream `dev` up to our merged [#1251](https://github.com/icssw-org/MeshCom-Firmware/pull/1251) (4f60051c, 2026-11-02). National characters, the charge state and UV and wind sensors are upstream through #1249, #1250 and #1251.
- **Fork-only firmware:** everything else below and the `dev` Auto Update channel. The release is **not** byte-identical to any official release.
- **Configuration:** `FLASH_STRUCT_VERSION` stays at `20260724`, so settings from `4.40a`/`4.35v` are kept. New settings start at their defaults.
- **How to tell builds apart:** the version on the air still reads `4.40a`.
  - `--info` prints `...AU env=<board> tag=v4.40a.11.06` and `FLASH_VERSION 20261106`.
  - `--id` prints `MCBOARD:v1:<hwid>:<hwid>:<board>:v4.40a.11.06`.
- **Safeboot** stays at capability v3 with a refreshed image (smaller page, stale stage dropped on a web upload). It reaches a node only through the web flasher.
- **Auto Update channels:** `prod` reads the latest icssw-org release, `dev` (`--updchan dev`) the **Latest** release of this fork, which is this one. The mode is always `notify`: nothing installs by itself.
- **T-Beam idle memory is lower:** 64.0 KB against 68.7 KB on `v4.40a.11.01`, accepted by the operator. It holds flat, so this is a lower level and not a leak. Details: "Known gaps".
- **Assets (68):** 28 ESP32 application `.bin` plus 28 `.autoupdate` files, the three nRF52 boards as `.uf2` and DFU `.zip`, and six support files.

**New in `v4.40a.11.06`**

**Displays and characters**

- **National characters on every display** (CHARS-01, upstream #1249, idea by sp2ong in #1221).
  - E290, Wireless Paper and E213 fonts carry Polish and German letters; E290 also French, Spanish and Portuguese. T-Deck has Latin fonts.
  - A display that lacks a letter folds it (`ä` to `ae`, `ł` to `l`, `€` to `EUR`, emoji to `?`); nothing printable is dropped any more.
  - T-Deck input now caps the message in bytes, so a long UTF-8 text is no longer cut in the middle of a letter (TD-25). Latin-2 on the OLED fonts stays folded by decision (CHARS-02).
- **Boot screens and the web logo show MeshCom 4.4** (BOOT-01).

**Sensors, battery and telemetry**

- **UV and wind sensors on ESP32** (WX-01, upstream #1251): optional LTR390 and reed anemometer, as telemetry values `uvindex`, `wind` and `gust`, off by default.
- **Battery charge state on PMU boards** (BAT-04, upstream #1250): charging, full, discharging or absent, plus current where the PMU measures it (AXP192 only). Over BLE in a new `IS2` register.
- **Own `T#` telemetry goes on air again** without the soft-serial app (TLM-03).
  - Cause: since upstream `v4.34x.05.16` the periodic timer asked for the soft-serial app only.

**Web GUI**

- **Web password on the page** (WEB-PWD-01): set, change and clear it on Setup. A forgotten password still needs `--webpwd none` on the console.
- **Live Pins page:** board LED, charge LED and MCP23017 pins, refreshed every 5 s while open. An input pin or bad port now answers 422 instead of a false success (WEB-PIN-01).
- **Board LED off, flash or steady,** kept across reboots (WEB-LED-01).
- **Auto Update install plan** on Setup and Info: delay, weekday, time. It is a notice only; automatic install stays off.
- **Smaller and tidier:** CSS and JS are served compressed, dark mode and accessibility fixes (WEB-DARK-01, WEB-DARK-02, WEB-A11Y-01), Firmware Update card aligned (WEB-GRID-01), unique field ids (WEB-DUP-01).

**Auto Update**

- **T-Beam no longer refuses Check now and Download after web use** (AU-23).
  - Cause: closed web sockets held memory for two minutes, and the job's own stack could split the big memory block.
  - Now: on a refusal the job clears those sockets, reads the gate again and, on a split heap, restarts once.
- **The update image is checked more strictly before handover,** with a battery floor on apply and a staging gate.
- **Check time stamps use the node clock** (AU-22). The update button reads Download, not Install.

**Network and gateway**

- **EXTUDP peer changes take effect at once** on console and web (EXT-PEER-01). Accepted: a RAK cannot resolve hostnames, so a hostname peer now leaves EXTUDP off in both directions, with one log line. Before, it still received but sent to the old address. Set `--extudpip` to a dotted IP.
- **EXTUDP can answer register queries** (`{"type":"reg","q":"SN"}`): read-only, secrets filtered, only to the configured peer, one per second.
- **Messages are held during a server outage:** up to 20 minutes, text only, 1 KB on classic ESP32, 6 KB on S3 and RAK, replayed with the same id (`--udphold`).
- **Gateway stability:** UDP drain, server name resolved again, EXTUDP name lookup no longer blocks.
- **Net console:** handshake deadline and rate-limited accept errors.

**RAK4631 and nRF52**

- **A pin setting can no longer brick a RAK** (RAK-PIN-01).
  - Cause: `--button gpio 1` took the 32 kHz crystal pin, the node hung in its first delay, and a reflash kept it.
  - Now: crystal and reset pins are refused at the console and repaired at load.
- **Ethernet is a build flag per board,** off on T114 and T-Echo, where `--gateway on` is refused.
- **Secrets are masked in the BLE settings path** (QA-NET-02). See "Known gaps": that path does not exist on the air (BLE-SET-01).

**T-Deck**

- **BLE and WiFi header icons toggle the radio,** stay across reboots and follow the switch (TD-30, TD-33). The Path column wraps long paths, Setup field padding fixed (TD-31).
- **T-Deck Pro:** a destination of 1 to 9 characters is a DM, an invalid one is refused and never sent as a broadcast (TD-26). Also TD-27 to TD-29 and TD-32.

**Stability and mesh**

- **A mailbox node no longer keeps re-sending a direct message for hours** (SNF-ECHO-01, ESP32-S3 and nRF52 nodes with a mailbox switched on, upstream PR [#1257](https://github.com/icssw-org/MeshCom-Firmware/pull/1257)).
  - Cause: the node's own delivery came back through another gateway and looked like a new message. That restarted the hold time and skipped the pause, so the message never expired.
  - Now: a copy that arrives after delivery began changes nothing, the delivery's id enters the duplicate check, and a frame whose path holds the node's own call is not stored.
  - Unchanged: the limits themselves. A message nobody acknowledges still goes out up to 9 times, then at most once per hour until the 24-hour hold ends.
- **Mesh:** the echo scan runs only after decoding, `{SET}` saves are rate limited, special calls are handled as source (QA-RX-03).
- **Stability:** BLE callbacks no longer log on the host task, less `String` churn on hot paths, safe argument copies in commands, cleared RX copy tail (BL-02).
- **Smaller builds:** no exceptions, debug level 0, unused T-Deck widgets and fonts dropped, table-driven web settings, shared code for hashing and clock sync.
- **Also:** BMP390 guard fixed, battery and queue details (BAT-05, TXP-02), time zone and sensor fixes (TZ-02, SENS-03), CAD timing log (CSMA-04), OLED and T-Deck bench screenshots.

</details>

<details>
<summary><strong><code>v4.40a.11.01</code></strong> — remote Auto Update channel switch, ping target lock, own beacons at priority 2, receive-silence logging</summary>

- **Remote management can switch the Auto Update channel** (fork, flag builds only, RM-UPDCHAN, RM1 `upd chan prod|dev`).
  - Use: an authenticated `RM1` DM `upd chan dev` or `upd chan prod` sets the channel; answers are `ok chan <x>`, `err aubusy` (a check or download runs, or a remote `upd go` is armed) and `err unsupported`.
  - Console `--updchan`, `/setparam updchan` and the remote web page use the same setter. A switch clears the last check result, keeps a staged image and starts no check.
  - Fixed on the way: after a console or web switch the stale "update available" line stayed; a console switch during a check now answers `[ERR];updchan;busy`.
  - The remote web page shows the active channel as selected (it looked unavailable before) and asks for confirmation before a switch.
  - Builds without the `dev` channel refuse the command with `err blocked`; remote management itself stays off unless enabled with a password.
  - Background: [`docs/campaign-rm1-updchan.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.01/docs/campaign-rm1-updchan.md).
- **The ping target must be a callsign** (fork, LRF item 1, every board).
  - Cause: a group, `*`, junk or a malformed value could be stored by a JSON import, a BLE settings write or an older build, and then pinged every interval. A group ping drew ACKs from every gateway: in the 2026-10-31 log analysis 97.9 % of all ACK forwards in one cluster belonged to such pings.
  - Now: groups, special targets, the node's own call and leading-zero SSIDs are refused by the console setter (with the reason), the nRF52 BLE write, boot-time clean-up, the ping timer and a manual `{ping}` text. A refused ping does not use up `pingcount`.
  - Background: [`docs/campaign-logreport-fixes-20261101.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.01/docs/campaign-logreport-fixes-20261101.md).
- **A node's own position, weather and HEY reports go out with the priority of an own group message** (fork, LRF item 2, every board).
  - Cause: own beacons had the low priority of relayed beacons and starved at busy nodes.
  - Now: own beacons are priority 2; relayed beacons stay at 4/5. Expiry times by frame type are unchanged (own position 120 s, own HEY 180 s).
- **Receive-silence logging for field diagnosis** (fork, LRF item 5, ESP32 and nRF52; no change in radio behaviour).
  - `--loradebug on` prints `RX_SILENCE` snapshots (chip mode, IRQ flags, device errors, instantaneous RSSI) after 60 s without a reception, and `RX_SILENCE_END` with what ended it and the ages of the last CAD, own TX and RX restart.
  - The RX restart line gains `mode_before=`; `--info` shows `RXSIL` counters (silences, longest, total). Counters run always; chip reads and prints only under `--loradebug`.
  - Purpose: tell a stuck receiver chip from a quiet channel on nodes that go deaf (OE3XIA-12 in the 24-h analysis).
- **`--gateway lab <host[:port]>` for a lab server** (fork, developers).
  - A gateway can register at a lab hub (code `LB`, new setting `node_gwlab`) instead of the public server. ESP32 takes a host name or IPv4, nRF52 IPv4 only; an unusable lab falls back to `OE` with a logged reason.
  - It is not offered in the web dropdown, the BLE list or `--gateway srv`. `--gateway srv OE` restores the normal server. The setting reboots the node.
- **Vision Master E290 OTP image: no periodic full refresh by default** (fork, `vision-master-e290-otp`).
  - Testing on panels marked `FPC-7519revb` showed no ghosting with partial refresh alone, so the full refresh every 10 updates is off (`E290_FULL_EVERY=0`). `-D E290_FULL_EVERY=N` brings it back.

</details>

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

Full list of differences from upstream: [`docs/fork-dev-vs-upstream-20261018.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.11.22/docs/fork-dev-vs-upstream-20261018.md) (state of 2026-10-18).

## What changes on the air

With default settings, compared with the official `v4.40a.11.22`:

- **RAK4631, T114 and T-Echo send sync word `0x24B4`** (SYNC-NRF-01, upstream #1320): the second sync symbol now sits where every ESP32 SX1262 board puts it. In TRACK mode the APRS sync word is unchanged.
- **Track frames carry up to 25 bytes of comment** instead of 16, without a `#name` (APRS-TXT-01, #1293).
- **Automatic position beacons go out on time**, at least 60 s apart; manual beacons keep 30 s (POSINFO-LAG-01, #1315).

Since the official `v4.40a.11.14`, unchanged from the previous fork release:

- **Pong ids with the top bit set go out unsigned** (PONG-SIGN-01): `{pong}{3867557991}` instead of `{pong}{-427409305}`. Ids below 2^31 are unchanged.
- **A RAK4631 gateway keeps sending after an Ethernet chip reset** (ETH-06) instead of falling silent until a reboot.
- **TRACK turn beacons west of Greenwich or south of the equator carry W and S again** (upstream #1284). Nodes in the north-east send what they sent before.
- **APRS-IS weather goes over Wi-Fi, not LoRa** (upstream #1269), and only after `--aprswx on`.
- **Relayed ACKs leave after 6 s of waiting instead of 20 s** (ACK-AGE-01). Expect shorter ACK delays at a busy relay. Frame format and length are unchanged. Seen on one gateway in a quiet night only.
- **An RM1 reply on a full TX ring may evict the oldest queued ACK** (RM-LOSTREPLY-01). One ACK is lost for the reply; nothing changes while the ring has room.
- **A higher-priority frame shortens a CSMA wait that is already running** (CSMA-01). Expect a reply to a received frame to go out earlier at a busy node. Benched on a mock server only.
- **nRF52 nodes check the channel with a shorter blind window** (CSMA-03, CSMA-05; CSMA-03 is in upstream #1280): the loop polls every 1 ms while a CAD is pending. Frame format and length are unchanged.
- **A relay clamps a hop counter of 8 to 15 to 7 before it decrements** (IDEE-04): frames of old firmware with such a counter stop circling.
- **nRF52 nodes ping only after `--ping start`** and stop at `--pingmax` (upstream #1280): fewer pings than before on RAK4631, T114 and T-Echo.
- **The neighbour report is automatic** (BL-01): sent only when neither `--mesh` nor `--gateway` is on; the `--nbrreport` setting is gone.
- **Auto Update `dev` channel:** fork builds can follow this fork's releases (`--updchan dev`, also by remote management); official builds know `prod` only.
- **Nothing else new on LoRa by default.** The clock ranking (a node with NTP or GPS ignores `{CET}`), the ping display, the T-Deck and T114 pages and the Pins page are local. Free GPIO control is off until the master switch is turned on at the console or on the Pins page.

<details>
<summary>Shared with the official <code>v4.40a.11.07</code>: fork behaviour merged upstream in #1247 and #1257</summary>

- **Own position, weather and HEY reports leave the queue sooner** at a busy node: priority 2 instead of 4/5. Expect these beacons to win against relayed traffic; expiry times are unchanged.
- **No more pings to groups or special targets:** a stored non-callsign ping target is cleared at boot. Fewer ACK floods from gateways answering group pings.
- **Receive-silence logging** reads the radio chip only under `--loradebug`.
- **Since `v4.40a.10.30`:** gateways upload HEY reports with their own call (server side, not LoRa), and a relay skips a frame whose path is already too long for its own call (more than 119 characters, log `pathfull`).
- **Mailbox nodes send less** (mailbox switched on, off by default): the node's own returning delivery no longer restarts its delivery. Frame format and length are unchanged.

</details>

<details>
<summary>Shared with the official <code>v4.40a.10.28</code>: fork behaviour merged upstream in #1238 (compared with <code>v4.40a.spserver</code>)</summary>

- **Nothing new on LoRa by default.** STOR and remote management are off. Auto Update is a daily Internet check over WiFi, never on LoRa.
- **Every release node relays with `--nbrrelay on`** and cannot switch it off. Relays are cancelled when every neighbour is served; expect fewer redundant copies and shorter queues. Field evidence so far: one gateway (`DK5EN-98`).
- **Own group texts go out at least once:** another gateway's server copy no longer cancels an own text before its first transmission. Expect more own texts on air from gateways near other gateways.
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

</details>

## Supported Hardware

### Verification for this release

- **Host suite:** `tools/regression.sh --stage 1,2` PASS on the tagged tree (2026-11-25): 4,237 Unity test cases in 129 native environments (one skipped), the golden selftest (48 commands), five board link builds, 1,842 pytest cases plus the Node, nbrlog and tool self-test suites. The new Auto Update buffer size, the `hmac_connect.py` fixes and the INA226 calibration each have tests that fail on the old code.
- **Build:** all 36 board environments built from the release candidate on 2026-11-24 (35 green; `esp32-external-radio` needs its own build overlay and is not shipped). CI builds all shipped environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download.
- **Auto Update, bench 2026-11-24** (release-candidate images, check and download with staging, never installed): DK5EN-1 Heltec V3, DK5EN-92 T-Beam v1.2, DK5EN-14 T-Deck Plus, numbers under "New in". Night 2026-11-24/25, three runs per board three hours apart (19:26 to 01:49, uptime up to 6.5 hours): 9 of 9 downloads staged, no restart, no refusal. DK5EN-1 10.9 to 11.4 s, lowest free memory after a run 21,260 bytes; DK5EN-92 13.6 to 13.7 s, 11,700 bytes, the image check ran in place on the first run and was moved to the check start at install on runs two and three (AU-30, memory short after the download); DK5EN-14 20.6 to 22.0 s, 23,444 bytes. Production gateway DK5EN-98 on the release candidate: no restart for 15.8 hours (2026-11-24 15:38 to 2026-11-25 07:28), free memory steady at 100 to 104 KB, no failed transmission.
- **Idle heap on the T-Beam release image (`DK5EN-92`), 2026-11-25:** 62,992 B free at idle, 1,024 B below the baseline of 64,016 B (limit 3,072 B): pass.
- **Sync word, bench 2026-11-24:** RAK4631 with the old and the new value against Heltec V3, T-Deck Plus and T-Beam, both directions, at 2 and -9 dBm; TRACK on and off. Result: [`docs/syncword-ab-bench-20261124.md`](docs/syncword-ab-bench-20261124.md).
- **Gateway and remote management, bench 2026-11-24:** message hold in a simulated server outage on DK5EN-1 (held, replayed once with the same id); Extern-UDP register query on DK5EN-92 (16 registers, no secret, rate limit); RM1 `pins`, `gpio`, `txq`, `mbox`, `maxhop` over LoRa from DK5EN-92 to DK5EN-1, refusals for the button pin and without the GPIO master switch.
- **Production gateway:** `DK5EN-98` runs the release candidate since 2026-11-24 15:38 (OTA), settings unchanged.
- **Not on a node:** INA226 calibration and the WX table (no sensor on the bench), and most wave-3 fixes listed under "Not on our bench" below.

<details>
<summary>Bench results of earlier releases (unchanged code since)</summary>

**`v4.40a.11.22`:**

- **Host suite:** `tools/regression.sh --stage 1,2` PASS on the tagged tree: 4012 Unity test cases in 116 native environments, plus the golden selftest and 1632 host tool tests. The Ethernet fix, the AppLink socket rules and the AppLink memory rule each have tests that fail on the old code.
- **Build:** all 36 board environments built from the AppLink merge on 2026-11-21 (35 green; `esp32-external-radio` needs its own build overlay and is not shipped); after the last code change T-Beam, Heltec V3 and RAK4631. CI builds all shipped environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download.
- **Review:** the AppLink merge, its socket fix and the memory change each passed an independent review before commit.
- **Idle heap on the T-Beam release image (`DK5EN-92`), 2026-11-21:** 65,860 B free at idle with BLE up, 1,844 B above the baseline of 64,016 B. The first image of this release failed this check (3,360 B below): AppLink held 5.4 kB of static RAM even when off. Since then AppLink takes its memory from the heap only while it is on; its static share on the T-Beam is 112 B.
- **Bench 2026-11-21:** Ethernet chip reset on `DK5EN-90` (RAK4631, INSTRUMENT image) three times; AppLink on `DK5EN-1` (Heltec V3, WiFi) and `DK5EN-90`, including a chip reset with a session up. Identity guard ok, 2 dBm.
- **Production gateway:** `DK5EN-98` runs `v4.40a.11.20`; it does not run this release yet.
- **Smoke test after the memory change, 2026-11-21:** release-candidate images on `DK5EN-1` and `DK5EN-90`; AppLink on, login, off, on, login again on both.
- **Not on a node:** the dash commands, the unsigned pong id and the navigation buttons.

**`v4.40a.11.21`:**

- **Host suite:** `tools/regression.sh --stage 1,2` PASS on the tagged tree: 3900 Unity test cases in 111 native environments, plus the golden selftest and the host tool suites. The Auto Update fix has a test that failed on the old code; the W/S fix has eight pins that fail on the old source.
- **Build:** CI builds all environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download. Heltec V3 built locally after the last code change.
- **Review:** the Auto Update fix and the W/S fix each passed an independent review before commit.
- **Idle heap on the T-Beam release image (`DK5EN-92`):** not run, waived by the operator. The APRS-IS client allocates nothing while it is off.
- **Production gateway:** `DK5EN-98` runs `v4.40a.11.20` since 2026-11-21 by Auto Update from the web page, operator OK; the gateway reconnected after the reboot. It does not run this release yet.
- **Not on a node:** the downgrade case, a TRACK beacon from west or south, and APRS-IS weather.

**`v4.40a.11.20`:**

- **Host suite:** `tools/regression.sh --stage 1,2` PASS on `fork-dev` `938f0031`: 3871 Unity test cases in 110 native environments, plus the golden selftest and the host tool suites. Most fixes of this release have a test that failed on the old code.
- **Build:** RAK4631, Heltec V3, T114, T-Echo, T-Deck and T-Beam build green on `938f0031`. CI builds all environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download.
- **Idle heap on the T-Beam release image (`DK5EN-92`):** not run for this release. The T-Beam was not reachable on the bench, and the operator waived the check. The `wsParams` table moved to flash in this release, which frees about 1 KB on every ESP32 board.
- **F-2 bench 2026-11-16** on `DK5EN-1`, `DK5EN-90` and `DK5EN-92` with an INSTRUMENT image of `dc08974f`, identity guard ok, 2 dBm:
  - **RM reply on a full ring:** the reply evicted the oldest ACK, sender `ok` after 15.2 s (was `noanswer` after 171 s).
  - **Clock:** `DK5EN-1` held 4 beacons and stayed within 1.71 s for 60 min; `DK5EN-92` held 6 of 6 beacons by NTP rank.
  - **BLE:** the scan on `DK5EN-90` shows F0A0 only in the advertising packet.
  - **Web assets, second browser:** Firefox 160 with a fresh profile on the RAK; an ESP32 confirmed by the operator.
- **B-1 bench 2026-11-15** on bulk images of `f64ea131` and `afaa2f40` (`DK5EN-1`, `DK5EN-14`, `DK5EN-90`, `DK5EN-92`, 2 dBm):
  - **Safeboot:** `bootloader.bin` refused with `why;desc` after the SB-06-DESC fix (`DK5EN-92`).
  - **ping and nRF52 budget:** PING-NRF-01, PING-STOP-01, PING-HOLD-02 and TRACK-PING-01 PASS on `DK5EN-1` and `DK5EN-90`.
  - **Mock server:** RM-DUP-01 PASS (one strike per junk frame), CSMA-01 14 of 14, server path Heltec to RAK.
  - **BLE:** BLE-SET-01 with the phone app and NRF-BLEDFU with nRF Connect on `DK5EN-90`.
  - **Web and T-Deck (operator):** web password set, change and delete, dark mode on every page, chat text sizes, tab cap, backlight, header icons, boot screen.
  - **T-Beam:** 3 s press to deep sleep and wake by button (operator, `DK5EN-92`).
  - **Night clock run** (2 h): the node clock held, no step back below -5 s; the scatter of LoRa beacons led to CET-SCATTER-01.
- **T-Deck bench 2026-11-16** on `DK5EN-14`: TD-THEME-01 on every tab in light and dark by screenshot, TD-34 `--disptest square 8` and TD-LOCK-01 PASS, SKY/TEXT tabs by screenshot.
- **Web PR bench 2026-11-19** on `DK5EN-1` and `DK5EN-90`: WEB-LOAD-01 and WEB-NAV-01 found and fixed, then cold-cache PASS in Safari and Chrome (operator).
- **Production gateway:** `DK5EN-98` runs `029eb2f5` (this release without RAM-WSP-01) since 2026-11-19 15:35 CET by OTA, operator OK; the gateway reconnected after the flash. Earlier, `aa8e1b45` ran 14.2 h there for ACK-AGE-01.
- **Still pending, to be filled in after the tag:**
  - **Release image smoke test** on the bench nodes and the CI asset check (68 assets, 28 `.autoupdate`).
  - **RAM-WSP-01** has only build measurements and the host gate; no node runs it yet.

**`v4.40a.11.08`:**

- **Host suite:** 3765 Unity test cases in 107 host envs and 1593 pytest cases, all green, plus the golden selftest, nRF52 board links and the host tool suites.
- **Bench 2026-11-08** on INSTRUMENT builds of that day (not the release images), on `DK5EN-1`, `DK5EN-92`, `DK5EN-14` and `DK5EN-90`:
  - **Clock:** drawn every second; a free-running clock held +0.1 s over 2 h; one OLED frame takes 3.8 % of loop time (limit 5 %).
  - **Stale `{CET}` beacons:** backward steps up to -116 s on three nodes before the fix; with the guard every set over 1 h stayed within 0.8 s (Heltec V3 and T-Beam).
  - **Ping frames:** held 10 s; longest clock gap 16 s on `DK5EN-1`, was 158 s.
  - **nRF52 CAD age:** 60-97 ms before the fix, 1-2 ms after (`DK5EN-90`); CAD to send 2-3 ms, was 63.5 ms.
  - **T-Deck theme:** dark, light and auto seen on the device; the green sent bubble seen with its ACK. Web setup page reviewed by the operator in Chrome and Safari.
- **Idle heap, T-Beam release image:** 65024 B free at 600 s with BLE up, 1008 B above the baseline of 64016 B (the gate allows 3072 B).

**`v4.40a.11.06`:**

- **Host suite:** 3607 test cases in 101 native environments, all green, plus the golden selftest, nRF52 board links and the host tool suites (pytest 1503, web GUI, web flasher).
- **Build:** CI builds all environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download.
- **Test image:** the bench and soak below ran on the CI build of `44075ef3`. The release adds a one-line Setup layout fix, the mailbox fix (SNF-ECHO-01) and the `FLASH_VERSION` stamp; the release images get a smoke test on the four nodes. The soak did not cover the mailbox fix.
- **Mailbox fix, benched on a Heltec V3 (2026-11-06), before and after, on local builds:** returning copies injected through the real receive path at 2 dBm. Before, a copy counted as a refresh and put the slot back to held. After, the refresh count stays put and the delivery continues.
- **Soak, 1 hour (2026-11-06):** T-Beam, Heltec V3, T-Deck Plus, RAK4631: no reboot. T-Beam flat within 112 B over the last 40 minutes at 64016 B, new baseline; the gate allows 3072 B.
- **Benched on those images (2026-11-06):**
  - Web GUI: every page on RAK4631, T-Beam and T-Deck Plus in Chrome, no console errors. Layout fix seen on a T-Deck Plus.
  - Update safety: six manipulated `.autoupdate` uploads refused on a T-Beam, each with its reason, clean upload recovered the node; Safeboot page in Chrome on a T-Deck Plus.
  - EXTUDP peer change on all four boards, also during a WiFi drop on a T-Deck Plus.
  - Slow client on a T-Beam at 2.5 s reply delay: no reboot.
  - Remote management round trip Heltec V3 to T-Beam, 4 of 4 replies verified, relayed by a gateway over the server.
  - Ethernet on a RAK4631: 20 inbound EXTUDP datagrams at 0.5 s and 1.2 s, no reset.
  - Phone (operator): DM and ACK on T-Beam and RAK4631, reconnect without pairing; with a phone held the T-Beam offers "Pause Bluetooth and check", the check ends well and BLE returns.
- **Benched on earlier images the same day:** reserved RAK pin refused, hold ring with one simulated outage, 60 of 60 T-Beam pages complete, OLED screenshots, 0 % RX loss on a T-Beam under web load.

**`v4.40a.11.01`:**

- **Host suite:** 3262 test cases in 85 native environments, all green, plus the golden selftest (48 commands), nRF52 board links and the host tool suites (pytest 1473, web GUI, web flasher). New: suites for the ping target, the receive-silence tracker, the remote channel switch and the lab gateway.
- **Build:** CI builds all 33 environments from the tagged tree; the 28 `.autoupdate` files are re-inflated byte-exact after download (checked on the published assets).
- **Idle heap on T-Beam v1.2 (`DK5EN-92`):** 68688 B against the 69332 B baseline (-644 B, the gate allows 3072 B), measured 2026-11-01 as the 600 s STAT heap with Bluetooth up on a local build of this commit.
- **Benched for this release (2026-10-31 and 11-01):**
  - Remote channel switch (RM1 `upd chan`): T-Beam `DK5EN-92` to Heltec V3 `DK5EN-1` over LoRa and over the 2323 console, both on the fix commit; the remote web page checked in Chrome on `DK5EN-92`.
  - Ping target: refusals of group, special, own-call and malformed targets at the console setter on `DK5EN-1`.
  - Receive-silence logging: nRF52 on RAK4631 `DK5EN-90`; ESP32 on Heltec V3 `DK5EN-1` for 18 minutes at 2 dBm (six snapshots, six end lines, 178 timed RX restarts all in receive mode, no reset). On the bench a silence is a quiet channel, not a deaf chip.
- **Host-tested only:** own beacons at priority 2 (queue order, expiry), boot-time clean-up of a stored ping target, the lab gateway, the E290 refresh default (built only, no E290 on our bench).

- **`v4.40a.10.30` (2026-10-30):**
  - Web login on Heltec V3 (`DK5EN-1`, USB): 19 of 19 HTTP checks including the login block, plus Chrome (Enter, focus, wrong-password message, logout). On the gateway `DK5EN-98` (over the air): 17 of 17 plus Chrome.
  - Extern-UDP echo on Heltec V3 (`DK5EN-1`): a JSON message to UDP 1799 shows once as an own message in the web chat.
  - HEY upload on the gateway `DK5EN-98` in the field since 2026-10-30 17:22: `DL2JA-2,DK5EN-98` published on the INTERLINK feed, link drawn on the map, publish rate unchanged.
- **`v4.40a.10.25` (2026-10-25):**
  - **Idle heap on T-Beam v1.2 (`DK5EN-92`):** 68728 B against the 69332 B baseline (-604 B, the gate allows 3072 B).
    - A first build of the WiFi watchdog with its own ping task measured 63884 B and failed the gate; the release ships the probe without a task.
  - **Benched for this release (2026-10-25):**
    - Own group text fix on Heltec V3 (`DK5EN-1`) and RAK4631 (`DK5EN-90`), same setup before and after: without the fix 6 of 10 own texts released before their first transmission, with it 0 of 20; 20 of 20 sent, 19 exactly once (one repeat of a text nobody heard, by design).
    - WiFi watchdog on T-Deck Plus (`DK5EN-14`), final probe: internal memory starved until the IP path hung (588 B free), memory released, the path stayed dead; the watchdog reconnected after 3 failed rounds, outage 2 min 0 s, no reboot (before the fix: no recovery in 20 min).
    - The probe gets answers from a real access point: `[WIFI];probe;armed` 60 s after boot on T-Beam v1.2 and T-Deck Plus.
    - T-Deck memory on `DK5EN-14`: 400 messages in one tab plateau at 41.6 KB free (before: hang at about 250). 20 groups round robin hold 16 tabs with 0 lost pings; 500 messages in 5 groups keep 41.0-61.9 KB free, no failed allocation.
  - **Host-tested only:** the app settings from #1237 (builds and the host suite only, not tried with the McApp phone app), the mailbox fix (SNF-ACK-01: 9 new cases, each failing against the unfixed code), the day/night backlight (TD-23: no hardware test yet), the watchdog on any board except the T-Deck Plus, and the watchdog's network reset and reboot stages.
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

- **Heltec V4 GNSS (GPSV4-01):** no Heltec V4 on our bench. Pins, power lines and the I2C move are checked by the build and a failing-before test only.
- **RAK4631 L76K detection:** no L76K module on our bench. The probe and the saved module choice are source-pin tests and builds only.
- **Pins page, pin policy and free GPIO control (GPIO-01 to GPIO-04):** host tests and builds only, no jumper tests; the pages load in the browser on RAK4631 and Heltec V3 (2026-11-19). OMs test the pins in the field after the release (GPIO-07).
- **T-Deck Pro pins and sky plot (GPIO-06, GPIO-08, GPIO-09, GPS screen 3):** no T-Deck Pro on our bench; keyboard, SD card, touch pins and the sky plot are not tried on the device.
- **T114 pages and buttons** (status page, GPS page, 10 ms button polling, deep-sleep screen, bootloader skip): upstream code of DH1FR, merged and built here; no T114 on our bench.
- **INA226 on Heltec V3, Wireless Stick and Wireless Tracker** (INA226-HELTEC-01): build only, no INA226 module on our bench.
- **T-Beam deep-sleep press (TBEAM-SLEEP-01):** benched on the T-Beam v1.2 only; AXP192, T-Beam Supreme and 1W variants are built and host-tested.
- **Spectral scan and `BOARD_COUNTRY` default** (upstream #1268, #1273): built; not run.
- **T-Beam 1W:** none on our bench. A field operator confirmed on 2026-10-19 that a 1W with the QIO bootloader completed an Auto Update. Marker and flash read not tried on a 1W.
- **T-Deck Pro (partition table):** none on our bench. The new partition table is checked by the build only (Safeboot at 0x10000, application at 0xC0000, NVS unchanged at 0x9000).
- **New LED pins** on T-Echo, T114, T-ETH-Elite, Heltec V4 and the S3 DevKitC come from board documentation; no LED was watched blinking.
- **GPS filter:** benched stationary on one board only; T-Beam, T-Beam Supreme, T-Deck and other GPS boards run the same code untested.
- **Web firmware column:** host tests only.
- **All other boards:** E22 family, Heltec V2/V4/Wireless Stick/Tracker, T-Beam Supreme and SX1262/SX1268 variants, T-Deck, T-Deck Plus and Pro, T3-S3, T-Connect Pro, lora32 v2.1, E213, E290 (both images), Wireless Paper, LoRa-APRS boards, T-ETH-Elite (Ethernet), T114 and T-Echo, including network mode and server table.
- `t5_epaper` is not shipped.

## Known gaps, stated plainly

**Could cost you settings or a working node**

- **T-Beam 1W in the field** keeps its DIO bootloader and blocked Auto Update until its owner runs the web flasher once (no erase needed). Do not update it over the air before that: older firmware hands over to Safeboot and the node loses its settings. A 1W already stuck in Safeboot needs the web flasher too.
- **T-Deck Pro not web-flashed since `v4.40a.10.21`** still has the old partition table without a Safeboot slot. Run the web flasher once (no erase needed); the SPIFFS area moves and is formatted once on first boot.
- **A failed image after the flash erase** leaves the node in Safeboot until a web-flasher upload (single app slot).
- **Locked settings are reset at boot without asking.** INSTRUMENT bench builds keep them changeable. `--stor on` is refused, so store-and-forward (#1188) is effectively off.

**New in `v4.40a.11.25`**

- **Library patches (FORMAL-01) were not benched one by one.** They ran on the bench nodes the whole day (WiFi, web, BLE advertising, Auto Update download over TLS, Ethernet on the RAK), but no patch has a targeted test on hardware. One of them broke the Auto Update download on 2026-11-23 (AU-28, fixed before this release). Report anything new around BLE, mDNS, the access point, Ethernet or downloads.
- **Sync word:** an nRF52 node with the old value against one with the new value is not measured (one RAK on our bench), and the margin near the sensitivity limit is not testable on our bench table. See "Action needed".
- **Auto Update not measured on** T-Beam Supreme, Wireless Tracker, the E22 boards and Heltec V4. The T-Beam stays tight: the largest free block at the start of a run was between 34.8 KB and 57.3 KB on our board, for a 33.4 KB buffer.
- **A node can miss the frame that makes its own channel check busy** (CAD-RXLOSS-01, open, found during the sync-word test): a node with frames queued runs CAD before sending; if a frame starts during that check, the node does not decode it and gets it only as a relay copy. Seen mostly on a RAK4631 gateway with ACKs from the server queued. Not new in this release.
- **Wave-3 fixes not on our bench:** phone path of the gateway rule (GW-DM-01), the `/` hint (GW-CALL-01), the OTA warning on an old Safeboot (OTA-SAFEBOOT-01), the user-button switch (BTN-OFF-01), Wireless Paper paging (DISP-PAGE-03), APRS frames on air, DHT probe, GPS autosymbol in motion, T-Beam 1W PA. Host tests and builds only.
- **INA226 calibration and the WX table:** host tests only, no sensor on the bench.
- **T-Deck tabs (TD-35):** the fix has a host test but was not forced on the bench, because the memory must fall below the floor first. Old messages are still trimmed at the current floor; re-tuning it is open (TD-37).
- **Remote management Queues card:** the three requests of its Read buttons were answered over LoRa; the button click itself was not tried in a browser.

**AppLink (new in `v4.40a.11.22`)**

- **A node can start with AppLink on by accident** if it ever had both `--gpsdebug on` (removed in 2026-04) and `--checkmesh on` (v4.35p, May to June 2026) set, and has a console password. The boot log then says `[APPLINK] on (port 8002)`; `--applink off` ends it. Accepted by the operator; the login still needs the password.
- **No encryption:** like the web GUI, AppLink runs over plain TCP. The password never travels, but the session content does.
- **A session that dies with the RAK's Ethernet chip sends no close:** the app learns it from its own heartbeat timeout.
- **RAK4631:** after `--extudp off`, AppLink stays off until `--applink on` or a reboot, although the saved setting says on.
- **T-Deck Plus** names itself `TDECK+` in the login, not `TDECK_PLUS` (short-name table of the T-Deck builds).
- **Not measured:** heap use of an AppLink session on ESP32, and a frame-by-frame comparison with a Bluetooth capture.

**Not on the bench in this release**

- **Dash commands, unsigned pong id and the 75 % navigation buttons:** host tests and builds only; the button size was measured in Chrome on a saved node page.

**Web login (since `v4.40a.10.30`)**

- **No encryption:** the web GUI still runs over plain HTTP. Someone who can sniff the network can take over an open session cookie, and settings changed after login travel in plain text.
- **Use a long random web password:** a sniffed login challenge can be attacked offline.
- **Any device on the LAN can lock the login for a minute** with five wrong guesses; open sessions stay valid.
- **A login page cached by the browser from older firmware** sends the old plain-text login and gets an error; reload the page.

**New in this release (not benched, or limits found)**

- **APRS-IS weather** (upstream #1269) is host-tested and built, not run on our bench. The passcode is stored on the node in plain form; it is never printed or exported.
- **`--aprswx on/off` carries a temporary fork fix** for the reply to a phone over BLE; it is reverted when upstream fixes it.
- **W/S fixes, not done yet:** setting a negative position from the T-Deck position entry or a config import, and lowercase hemisphere letters from the T-Deck setup.
- **Auto Update downgrade fix** helps from this release on; the downgrade itself was not repeated on a node.
- **Heltec V4 GNSS, RAK4631 L76K, Pins page, T-Deck Pro pins and sky plot, T114 pages, INA226 on Heltec** are built and host-tested only: see "Built and shipped, not on our bench".
- **ACK-AGE-01:** the 14.2 h night on `DK5EN-98` was quiet (1.9 % channel use), so the shorter ACK wait is not proven under load. Four long waits came from one burst of four ACKs queued behind each other.
  - **ACK-CANCEL-01 (open):** a queued relay ACK is not cancelled when a neighbour forwards the same ACK first. Needs a concept; deferred by the operator.
- **Clock sources:** `{CET}` can no longer step a clock that NTP or GPS has set, but a node with neither still follows LoRa beacons that can differ by seconds; part A holds those within about 1.7 s on the bench.
  - **CET-RESTAMP-01 (concept only):** a gateway with NTP would write its own time into the `{CET}` beacon right before transmit, so receivers lose the server and queue delay. That changes what goes on air; no code exists.
- **CSMA-01, CSMA-05 and the relay hop clamp (IDEE-04):** CSMA-01 ran against a mock server (14 of 14), CSMA-05 and the clamp are host tests only; the shorter CSMA wait was not repeated on air.
  - **CSMA-03 threshold:** `CSMA_CAD_MAX_AGE_US` stays at 5 ms; CAD age was measured at 1-2 ms on `DK5EN-90`.
- **RM1 `pins` and `gpio`, RM Queues card (RM-Q-01):** benched over LoRa on 2026-11-24 (see "Verification"); a real pin change with a jumper and the card's button click were not tried.
- **DHT22 (SENS-04):** no DHT22 on our bench; the pin and type handling is host-tested.
- **Safeboot (SB-06-DESC):** benched on one T-Beam. Nodes that never took a web-flasher install keep an older Safeboot without these checks.
- **Clock every second:** measured on Heltec V3 and T-Beam OLEDs. No separate record for the T-Deck, e-paper, T114 and T-Echo displays, which run the same logic.
- **T-Deck:** the sky plot and SKY/TEXT tabs were checked by screenshot; the tap path of the header GPS icon has no separate record.
  - **TD-34 (open, cosmetic):** switching WLAN off or on at the console may not repaint the header icon.
  - **TD-NAV-01 nits:** the hidden GPS button leaves a stub of about 3 px in the tab bar; while the GPS page is active no tab is highlighted; the 8 px click area of the GPS icon overlaps the WLAN icon.
- **TRACK display:** after the 10 s hold the track page stays frozen until the ping time ends (no countdown, no clock); widening the gate would break the 30 s text-message hold (TRACK-PING-01 follow-up).
- **Web contrast:** the light-theme label on green is 5.08:1, below the 7:1 body-text target (operator informed).
- **RAM-WSP-01:** measured at build, not on a running node.

**Open since `v4.40a.11.06`**

- **Less memory on classic ESP32:** T-Beam idle heap 64.0 KB against 68.7 KB on `v4.40a.11.01` (measured 2026-11-06). After 150 web requests the T-Beam holds 53.3 KB (TBEAM-HEAP-01); the `v4.40a.11.08` release image shows the same, so this is no regression since then.
  - About 2.5 KB are new static buffers (hold ring, web settings table, register query); the rest is not itemised. Measured with about 1 KB of the drop possibly caused by the first web request after boot.
  - Check now passed 20 of 20 on a T-Beam. Accepted by the operator.
- **Web password:** no prompt for the current one, so an open session can change or clear it. It crosses the network in plain text.
- **Auto Update restart on a split heap with a phone held:** the phone path works, but this run may not have hit a split heap (AU-23).
- **Message hold:** one simulated outage on the bench, no real server failure. A single lost `BEAT` may replay the few held frames (about 1 per 4 h, measured), by decision.
- **MCP23017 pins on the Pins page:** host tests only, no expander on our bench.
- **AU-24 and AU-25 are resolved in `v4.40a.11.25`:** both were the low-memory failure (OTA-LOWHEAP-01) and the T-Beam erase watchdog (AU-29); see "New in".
- **Mailbox echo fix (SNF-ECHO-01):** benched on one Heltec V3 with injected copies, not with a real second gateway and the server, and not on nRF52 (built only). The counters of the node from the field report were not read, so the link to that report rests on timing and code.
  - **Still open:** two mailbox nodes serving the same call can re-open a slot after it expires, about one run per hold time and node. A node that keeps hearing the recipient without an ack can therefore start over.
  - **Accepted:** after the first pause a message gets one frame per pause instead of nine. This differs from the concept but keeps airtime down (about 30 instead of up to 216 frames in 24 hours).
- **UV and wind sensors, charge state over BLE:** see the upstream PRs; not repeated on the bench since `v4.40a.11.06`.
- **Display fixes on E290, Wireless Paper, E213, T-Echo and T114:** no such board on our bench unless listed under "Verification".

**Not benched (host tests or builds only)**

- **Vision Master E290 OTP image:** no E290 on our bench; built only. Its display behaviour, including the new default without periodic full refresh, is the upstream author's test; ghosting after long partial-only runs is not ruled out.
- **Own beacons at priority 2:** host tests only; no field capture yet shows the effect on a busy node. Own beacons now share priority 2 with own group texts.
- **Ping target clean-up at boot:** host tests only; a stored `255.` or group value was not loaded on hardware.
- **Receive-silence logging:** the chip-state fields were read on a quiet channel only; a real deaf phase (OE3XIA-12 type) has not been caught with it yet. The cause of those silences stays open until it is.
- **Remote channel switch:** benched between two nodes; not tried with McApp, which still needs its own command.
- **`--gateway lab`:** host tests and a build check; not run against a lab server on hardware.
- **HEY path cap and relay `pathfull` skip:** host tests only; a 119-character path was not produced on air.

- **Remote-management repeat stop for client commands:** host tests and builds only; not yet run over the air between two nodes.
- **Wireless Tracker memory fix (AU-21):** no tracker on our bench; display-buffer release and re-acquire are host tests and builds only until the first field report. Still open: the ESP32-S3 memory floor for TLS and a Bluetooth pause before the check on S3 boards.
- **Mailbox double delivery (SNF-ACK-01):** host tests and builds only; not run over the air. Classic ESP32 boards have no mailbox and are not affected.
- **WiFi watchdog:** benched on one T-Deck Plus only. The network reset and reboot stages ran in host tests only. After a heavy memory shortage the link can also drop completely; then the older watchdog reconnects after its 180 s grace time (seen on the bench, not shortened).
- **T-Deck memory fixes:** no 24 h soak after the fixes yet; the message tabs were checked by eye (operator, 2026-11-15).
- **Transmit queue in the field:** the 24 h capture on the gateway `DK5EN-98` (2026-10-24/25) found the own-text bug fixed in `v4.40a.10.25`; a 93 h soak of `v4.40a.10.25` on the same gateway (2026-10-25 to 10-29) then showed 0 queue drops and every frame sent well within its expiry time; the expiry times (300/120/90 s) are not re-tuned yet. In a small older sample (27 relayed frames, 2026-10-18) the slowest 10 % waited longer than 300 s and would now be dropped. The relay-side time-beacon gate was not seen on air (host tests only).
- **Call blocklist:** taking over the DM3KS build's stored list was not benched on hardware.
- **Queue work of `v4.40a.10.19`:** host tests (including the golden corpus) and builds only. The locked `--nbrrelay on` rests on one gateway's field data. The age-escalated hold is counted only since boot (`[MC-HWM]`), not per 5-minute window.
- **Auto Update:** the BLE guard runs on classic ESP32 only (a forced NimBLE restart crashed an ESP32-S3) and was tested with a BLE client script, not the McApp phone app. Stale-click and stuck-in-Safeboot handling in the web row is host-tested only.
- **RM1 `upd go`:** the download path is host-tested only. `upd go` installs even with `--autoupdate off`; remote management itself stays off by default and needs the password.
- **RAK4631 static IP:** cable pull and a real UDP error streak not benched; ESP32 Ethernet boards not benched with the new table.
- **LEDs:** deep-sleep LED, remote `chgled` over the air and the phone app view not benched.
- **Net console:** the AP-mode start and the keepalive drop of a half-open peer not benched. In pure AP mode the 5-minute offline recovery drops the console session (will not be fixed).
- **GPS filter:** no drive test. A 30-45 m multipath step held for 15 s can still look like motion and move the position by one cell (at most 9 per day in host tests). Smart beaconing with `--track on` still calls a still node stationary on speed alone.
- **Web flasher:** not tested on Windows, nor with a CP210x bridge for the restart path. The Safeboot v3 hint is covered by unit tests and a script harness only.

**Open defects**

- **Remember-password failure on an E22** (RM-NVS-01): only instrumented; waits for the reporter's `--info` and log.
- **One T-Beam reboot during an Auto Update download** (AU-14, `TASK_WDT` 13 s in, classic ESP32); the retry passed. Open.
- **No marker check in CI:** only the web flasher publish step refuses an image without a marker.
- **Heltec V4 Vext in deep sleep** (HV4-VEXT-01): the pad hold should keep it off; no current measurement yet.

**Accepted behaviour**

- **Ping id repeats after reboots** (DEDUP-LAP-01, won't fix, operator 2026-11-15): the message id grows by 100 per boot, so frequent reboots can repeat an id inside a neighbour's duplicate ring. One bench ping was dropped as a duplicate.
- **A `{CET}` beacon never corrects a clock that NTP or GPS has set:** the clock follows those sources until a reboot.
- **T-Deck under memory pressure:** the oldest messages of an inactive tab disappear from the display, and a 17th conversation closes the least recently used tab.
- **WiFi round trip of 80-100 ms** on ESP32 nodes with Bluetooth on: WiFi modem sleep, not a memory problem (WIFI-PS-01). Switching modem sleep off needs Bluetooth off.
- **T-Beams first booted on V4.21 or later keep TRACK on** if the old default set it. Not migrated: a TRACK the owner wanted cannot be told apart. Use `--track off` / `--gps on`.
- **The message page shows no clock line:** it is frozen by design and redrawn when the page changes (CLK-01).
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
- The `prod` channel had nothing to install until the official `v4.40a.10.28`; installing from it is not tried by us yet.
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

- **In upstream `dev`, not in an official release yet:** our wave-3 PR [#1316](https://github.com/icssw-org/MeshCom-Firmware/pull/1316) (21 issues, merged 2026-11-22, synced here as `dc5ea81c`) and #1320 (nRF52 sync word and `hmac_connect.py`, merged 2026-11-23, synced as `4fe57374`). This release carries all of upstream `dev` as of 2026-11-23.
- **Official `v4.40a.11.22`** (upstream tag on `5db0a4f5`) shares only its name with our `v4.40a.11.22`; the two are different builds.
- **Next upstream PR (planned):** one combined PR with the Auto Update memory work, the small fixes (#1305, #1306, INA226), the gateway hold (#1307), the register query (#1308), the remote-management node side (#1303) and the `hmac_connect.py` fix. The library patches, Safeboot, AppLink and the `dev` channel stay fork-only for now.
- **In the official release `v4.40a.11.14`** (2026-11-14, `upstream/dev` 70ae124d): our PR [#1277](https://github.com/icssw-org/MeshCom-Firmware/pull/1277) with 20 commits. Merged into this tree as `d4384eac`.
  - **Clock and display:** stale `{CET}` beacons, clock line every second, GPS without a fix against NTP, shown time held.
  - **T-Deck:** theme and readable light mode, setup controls, header icons, GPS layout and sky plot, smaller builds.
  - **Boards:** Heltec V4 GNSS, DHT22, T-Deck Pro pins and sky plot.
  - **From others:** DH1FR's T114 pages (#1264 to #1267), INA226 on Heltec (#1272), and the OE1KBC fixes #1274 and #1275 (`BOARD_COUNTRY` default, spectral scan, Info page label). Merged as `f05ead76`, `ac763f66` and `173b1e3c`.
- **Merged in upstream `dev`, not in an official release:** [#1280](https://github.com/icssw-org/MeshCom-Firmware/pull/1280) (ping, `4cfa2ac2`) and [#1281](https://github.com/icssw-org/MeshCom-Firmware/pull/1281) (web GUI, Pins page, GPIO control, board LED, `45f66dc7`), both by oe1kbc on 2026-11-19; [#1269](https://github.com/icssw-org/MeshCom-Firmware/pull/1269) (APRS-IS weather, Mateusz-Krajewski) on 2026-11-20, merged here as `6731af6c`; our [#1284](https://github.com/icssw-org/MeshCom-Firmware/pull/1284) (W/S hemisphere) on 2026-11-21, synced here as `0110fd3c` with the tree unchanged; our [#1285](https://github.com/icssw-org/MeshCom-Firmware/pull/1285) (`REVIEW.md` only) on 2026-11-21, synced as `29d9d65c`. This release carries all of upstream `dev` as of 2026-11-21.
- **Not byte-identical:** this release adds the fork-only firmware listed under "New in" to the official `v4.40a.11.14`. The flash stamp in `--info` (`FLASH_VERSION 20261122`, `tag=v4.40a.11.22`) is the distinguishing mark.
- **Open upstream:** our [#1286](https://github.com/icssw-org/MeshCom-Firmware/pull/1286) (navigation buttons WEB-NAV-02 and unsigned pong id PONG-SIGN-01, since 2026-11-21). The Ethernet fix ETH-06, the dash commands CMD-DASH-01 and AppLink have no PR yet. The Auto Update downgrade fix (AU-27) goes with the Auto Update wave. The next waves (remote management and mesh, gateway, Auto Update and Safeboot, channel access) follow; RAM-WSP-01 goes with the next web PR.
- **Earlier official releases:** `v4.40a.10.28` (2026-10-28) with all fork firmware up to `v4.40a.10.25` as one commit ([#1238](https://github.com/icssw-org/MeshCom-Firmware/pull/1238)), `.autoupdate` assets in the official CI ([#1239](https://github.com/icssw-org/MeshCom-Firmware/pull/1239)) and the E290 OTP image ([#1240](https://github.com/icssw-org/MeshCom-Firmware/pull/1240), sp2ong). Before it `v4.40a.spserver` (2026-10-22) with the gateway server table, network mode and RAK4631 static IP ([#1213](https://github.com/icssw-org/MeshCom-Firmware/pull/1213)), T-Deck reply button and web firmware column (#1214), web send buffer (#1224) and the gateway server-link warning fix (#1233).
- **In the official release `v4.40a.11.07`** (2026-11-08, `upstream/dev` 5d76238b): our PRs [#1247](https://github.com/icssw-org/MeshCom-Firmware/pull/1247) to #1251 (ping target lock, own beacons at priority 2, receive-silence logging, national characters, charge state, UV and wind) and [#1257](https://github.com/icssw-org/MeshCom-Firmware/pull/1257) (mailbox echo), plus OE1KBC's RAK4631 L76K GPS probe (#1259) and the T-Deck Pro / T3 S3 bootloader mapping in `tools/download_meshcom.py` (#1260 to #1262). Merged into this tree as `da76033b`.
- **Merged upstream 2026-10-31** (shipped here since `v4.40a.10.30`, in the official `v4.40a.11.07`): HEY upload names the gateway and path cap ([#1243](https://github.com/icssw-org/MeshCom-Firmware/pull/1243)), Extern-UDP sends in the web chat ([#1244](https://github.com/icssw-org/MeshCom-Firmware/pull/1244)), web login without plain-text password ([#1245](https://github.com/icssw-org/MeshCom-Firmware/pull/1245)).
- **Fork-only, no PR yet:** the list "Compared with the official `v4.40a.11.14`" under "New in", remote channel switch (`upd chan`), `--gateway lab`, E290 refresh default.
- **Fork-only by design:** the `dev` Auto Update channel (built only with `MC_AU_DEV_CHANNEL`) and the INSTRUMENT bench diagnostics (not in release images).
- Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
