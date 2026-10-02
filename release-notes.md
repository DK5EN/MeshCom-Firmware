> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.35v.09.29-neo`: official `v4.35v` plus this fork's neo line, with a BLE, battery and timing campaign on top.** It contains everything `v4.35u.09.28.3-neo` had (neighbour matrix, store-and-forward, personal-message retries, the ZWJ emoji and web UTF-8 fixes) and everything new in official `v4.35v`. It replaces all earlier releases.

**How to tell this build apart:** the version field on the air and in `--info` reads `4.35v`, the same letter as the official release. `--info` prints the flash stamp `FLASH_VERSION 20260929`; the official build does not carry it. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.**

## New in this build

Every firmware commit since `v4.35u.09.28.3-neo`, grouped.

### BLE link to the phone

1. **A frame the BLE stack refuses is no longer lost.** Both drains took a frame out of the ring before sending it and ignored the result. On ESP32, NimBLE's `notify()` without arguments cannot report a failure at all; on nRF52 `BLEUart::write()` returns 0 when the SoftDevice queue is full. Either way the frame was gone. Now a refused frame stays in the ring and is retried in the next drain window, and dropped after five retries — counted, so it is never silent. A lost connection never discards a frame. (`cf1693ca`)
2. **Frames longer than the negotiated MTU are counted.** The firmware assumes MTU 247 everywhere; it now reads the real MTU and counts every frame that does not fit. Framing is unchanged. `--info` shows the counters on the BLE line: `tx s<sent> r<retried> d<dropped> t<too long for the MTU> e<evicted> mtu<n>`.
3. **A race on nRF52 could discard the next frame unsent.** The drain read a frame and its ring position in two steps; a LoRa reception evicting in between made the drain drop the frame behind the one it had sent. Both steps now happen under one lock.
4. **nRF52 drops a phone with a wrong PIN at once.** The disconnect used to happen only on the phone's next write; until then the link stayed open (no settings were sent). (`1404e77d`)

A host test drives the real ring and drain code through the full config burst, clamps, MTU 23/185/247, refused sends and a flood during the burst; 10 of its 25 cases fail against the old code.

### Battery measurement, one pipeline for every board

5. **One shared filter and one percentage curve on all boards** (`5d0bc44a`, concept `docs/archive/concept-battery-consolidation-20260923.md`). Before, the smoothing, the sampling rate and the percent curve depended on which of two source files a board happened to compile, and two copies of the no-battery detector drifted apart. The neo line had also slowed the battery check to 30 s, which made the filter 60x slower than upstream. Now: a time-based filter (30 s time constant), fixed dividers sampled once per second, one percent curve over fractions of `--maxv` (valid for 1S and 2S packs).
6. **Heltec V3/V4/Wireless Stick without a battery: the red LED no longer stays on.** The measurement divider was switched on during one 30 s check and read during the next, so it stayed on for 30 s. It is now on for 100 ms every 30 s. Same 100 ms window on the T114, E213 and Wireless Paper.
7. **USB boards without a cell stop claiming a full battery.** On the E22 family, T-Deck, T-Deck Plus, T3-S3, T-Beam 1W, lora32 v2.1, E213 and Wireless Paper, "no cell" read 100 %; it now reads 0 % and no `/B=` value goes on the air.
8. **`--analog`** uses the same filter building block, bit-identical to before.

### Uptime counter

9. **The neighbour matrix was wiped after 49.7 days of uptime** (`c9d1fcc0`). Every matrix time stamp was `millis()/60000` truncated to 16 bits; at the `millis()` wrap that counter jumped from 6046 to 0, and the next sweep treated every row and link as ancient. Gateway flags lapsed up to 45 minutes early or late. A wrap-safe uptime-minute counter replaces all 39 call sites. Found by new "millis teleport" host tests that jump the clock to just before the wrap and across it.
10. The battery filter froze after a clock jump of 2^31 ms or more (a stalled loop); fixed in the same pass.

### Smaller fixes

11. **Soak findings of 2026-09-28** (`15fa385a`): DM `ack=`/`rtt=` statistics count once per message, whether the ACK comes over LoRa or the server; `att=` counts only own messages; several debug lines now end their line; `--info` shows why the node last rebooted and in which loop section (`...BOOT RESET_REASON=`); the neighbour matrix keeps a station in its "needed" set against a single stray hit; the gateway flag is refreshed at least every 15 minutes and lapses 45 minutes after the last gateway announcement.
12. **A deliberate reboot no longer looks like a crash** (`0ada25fb`): `--reboot`, a config import, the app's save and deep sleep clear that loop-section record first.
13. **A `:rej` reply no longer counts as an acknowledgement** in the DM statistics.
14. **`--wifiap on` announces that the node stopped being a gateway.**
15. **The web message counter counts bytes, not characters** (`93a5f547`, [#1173](https://github.com/icssw-org/MeshCom-Firmware/issues/1173)) — the gap `v4.35u.09.28.3-neo` listed is closed.
16. **RAK4631 is built with `-Os` instead of `-Ofast`** (`be8de121`): flash use drops from 95.5 % to 69.2 %, RAM unchanged. It also ends fast-math on the RAK, so NaN checks work there again.

### From official `v4.35v`

17. **`--gateway srv DL` without HAMNET uses `meshcom.hamnet.network`**, and HAMNET NTP falls back to 44.143.0.9 outside 44.148 (`641b0dd4`). Beyond upstream, the RAK4631 Ethernet path gets the same DL entry.
18. **The Track beacon carries `WIDE1-1`** (`4370326f`, [#1174](https://github.com/icssw-org/MeshCom-Firmware/issues/1174)), so standard LoRa-APRS digipeaters repeat it.
19. **A one-shot position sends the node position unless TRACK has a point** (`cc18833e`, upstream PR #1175 by makrohard).

## What changes on the air

With default settings, compared with `v4.35u.09.28.3-neo`:

1. **Track beacons** carry `,WIDE1-1` (+8 bytes).
2. **A one-shot position with TRACK off** sends the node position instead of nothing or a stale track point.
3. **A gateway** sends at least one gateway announcement (`HEY` to `HG`) per 15 minutes, and immediately after `--gateway on/off`.
4. **The battery value `/B=`** follows the new percent curve (about +5 points in the upper range and −5 at 3.7 V on the Heltec/RAK path, lower mid-range values on the E22/T-Deck path), and is left out entirely on USB boards without a cell.
5. **A gateway with `--gateway srv DL` and no HAMNET** reaches `meshcom.hamnet.network`.

## Supported Hardware

### Verification for this release

- **Host suite:** 1588 test cases across 48 native environments, all green, plus the golden selftest.
- **Build:** 32 release environments build clean.
- **Bench-tested (2026-09-29):**
  - **Heltec V3 (DK5EN-1):** BLE config burst complete 5/5, 6/6 malformed frames survived, PIN with and without code, 40/40 group-9 messages from a mock server reached a BLE client, battery reads 0.00 V / 0 % without a cell after the detection window.
  - **RAK4631 (DK5EN-90), `-Os` image:** BLE burst 5/5, malformed 6/6, PIN 3/3 (after fix 4), settings write-back unchanged over 92 fields, LoRa send and receive against the Heltec, Ethernet and NTP up.
  - **Heltec V3 (DK5EN-98):** soak on this image since 2026-09-29 10:07, not evaluated yet.
  - The bench images differ from this tag only in the one-shot position port (19) and the flash stamp.

### Built and shipped, not on our bench

All other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker/T114, T-Beam (all variants), T-Deck, T-Deck Plus, T-Deck Pro, T3-S3, T-Connect Pro, T-ETH-Elite, lora32 v2.1, E213, E290, Wireless Paper, T-Echo, LoRa-APRS boards. The battery change touches all of them; only the Heltec V3 and RAK4631 had bench time. `t5_epaper` is not in this release (pre-existing include-path breakage).

## Known gaps, stated plainly

- **Percentages change** on most boards (see "What changes on the air"). Same cell, same percentage on every board now — but not the number you saw before.
- **No-battery detection on the Heltec V3/V4/Stick takes about 3 minutes after boot** (six samples at 30 s). Until then a board without a cell shows the noise of the open divider, e.g. 3.9 V. Upstream detects it in 3 s, the previous neo release in about 6 minutes.
- **The Heltec LED fix is not yet confirmed by eye** — the 100 ms divider window is measured in a host simulation.
- **The phone gets replies to about three commands per second.** A client that fires read commands faster overruns the config buffer; the loss is now counted (`e` on the BLE line), not avoided. The app sends one command at a time.
- **At 50 server datagrams per second the ESP32 network stack drops packets** before the firmware sees them; up to 10 per second nothing was lost.
- **The message-counter fix for app settings writes on nRF52** (in `v4.35u.09.28.2-neo`) still has no hardware test.
- **A `:rej` reply marks the wrong own message** (it reads the message id at the wrong offset); logged, not fixed.
- **T114 and T-Echo** still build with `-Ofast`.
- **Carried over:** a relay on older firmware still strips the ZWJ in compound emoji, multi-hop loss recovery for DM retries (`BACKLOG` PN-01), the central server's masked dedup (`msg_id & 0xFFFFF3FF`), and the older gaps in [`release.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35v.09.29-neo/release.md).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. Only `v4.35v.09.29-neo` is offered there; older releases are removed from the flasher.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [neo code-quality campaign, battery pipeline — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35v.09.29-neo/docs/CHANGELOG-neo.md)
- [This campaign's plan, findings and bench results — `docs/ble-batt-campaign-20260929.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35v.09.29-neo/docs/ble-batt-campaign-20260929.md)
- [Personal-message retries — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35v.09.29-neo/docs/CHANGELOG-snf.md)
- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35v.09.29-neo/docs/CHANGELOG-meshcom5.md)

## Upstream

Everything official `v4.35v` contains is in this build. The BLE, battery, uptime-counter and soak fixes have not been offered upstream yet; the neighbour matrix, store-and-forward and the neo rework neither. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
