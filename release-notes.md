> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.40a.10.02`: the official `v4.40a`, built from this fork's `fork-dev` branch.** The firmware source is byte-identical to the official [`v4.40a`](https://github.com/icssw-org/MeshCom-Firmware/releases/tag/v4.40a) (upstream `e1e2acea`, the merge of our [PR #1186](https://github.com/icssw-org/MeshCom-Firmware/pull/1186)): same `src/`, same `variants/`, same safeboot images. There are no fork-specific firmware changes in this build.

**Why this release exists:** it carries the images that the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/) serves, built and checksummed from this repository. ESP32 application builds are not byte-reproducible, so the `.bin` files here differ from upstream's in layout-shift bytes; the code is the same. If you want the official artefacts, take them from the upstream release.

**How to tell builds apart:** none on the air — the version field reads `4.40a` as in the official build. `FLASH_STRUCT_VERSION` stands at `20260724`, **your configuration survives the update from `4.35v`**. Downgrading an nRF52 node to upstream `4.35v` leaves the old settings sheet behind and starts with defaults: export your configuration first.

## What 4.40a brings

Everything below is in the official `v4.40a`; the full list is in the PR description, [`docs/archive/pr-neo-beschreibung-20261001.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.40a.10.02/docs/archive/pr-neo-beschreibung-20261001.md) (German).

1. **Store-and-forward for direct messages (new, off by default).** A mailbox node holds a DM whose target it cannot reach and delivers it when it hears the target again. ESP32-S3 and nRF52840 boards only; modes `--store off|own|list|heard`. The sender side is unchanged against upstream except for a "failed" status to the app when it gives up.
2. **Neighbourhood matrix (NBR) replaces the MHeard list and the path table.** It is the data source for `--mheard`, `--path`, the web pages, BLE and the T-Deck. This is the largest visible change: the MHeard output, the web MHeard page and the BLE MHeard frame change shape (the old 13 BLE keys stay in place, seven are appended), and the neighbour count on the air (`R<n>`, `/N`) is now the number of two-sided neighbours, so servers and maps show systematically smaller numbers for updated nodes.
3. **19 new serial/USB commands**, short forms, table-driven toggles, a grouped `--help` (`--neighbours`, `--store*`, `--nbr*`, `--msgid`, `--mbox`, `--keylock`, `--ethmtu` and others).
4. **Fixes:** a refused BLE notify no longer loses the frame (nRF52), a phone with a wrong PIN is dropped at once, a BLE settings write no longer rolls the message counter back (nRF52), the pong and ping paths over BLE, own messages without repeat keep their TX-ring priority, the T-Tracker backlight follows "display off", a settable Ethernet MTU for the RAK web server, the web message counter counts UTF-8 bytes, and `--postime 3600` survives a reboot on nRF52.
5. **RAK4631 builds with `-Os` instead of `-Ofast`:** flash use drops from 96 % to about 70 %, and NaN checks work there again. T114 and T-Echo stay on `-Ofast`.
6. **Structure:** settings, counters, BLE and UDP paths moved into their own modules; unused fonts and platform stubs removed; a new battery pipeline for all boards.

## What changes on the air

With default settings, compared with `v4.35v`:

1. The neighbour count in HEY `R<n>`, in the position `/N` and in the HEY signal report counts two-sided neighbours (at most 99), not every station heard in 60 minutes.
2. `--store` is off, so no node sends mailbox deliveries unless an operator turns it on. A store node's deliveries carry a new `msg_id` per step; a stock destination that missed the ACK shows the message again.
3. An optional HEY frame to `HN` (`--nbrreport`, default `auto`: only when mesh and gateway are off) once per 15 minutes; stock firmware discards it.
4. Older firmware floods as before: there is no new air format for the relay decision.

## Supported Hardware

### Verification for this release

- **Host suite:** 1618 test cases across 48 native environments, all green, plus the golden selftest (115 tests).
- **Build:** 32 release environments build clean (30 boards and both safeboot images). `t5_epaper` is not in this release.
- **Bench-tested before the upstream PR (2026-10-01), on the same source:** RAK4631 (`DK5EN-90`) and Heltec V3 (`DK5EN-98`) flashed with this code; callsign, power and web settings survived the update; `--postime 3600` survives a reboot on the RAK; `--mheard`, `--path`, `--neighbours`, `--nbrcheck`, `--store` and `--msgid` return plausible values on both nodes in the live mesh.
- **Soak:** five nodes ran an overnight soak on this source (2026-10-01 23:28 to 2026-10-02 08:00). The result had not been evaluated when this release was cut.

### Built and shipped, not on our bench

All other boards: E22 family, Heltec V2/V4/Wireless Stick/Tracker/T114, T-Beam (all variants), T-Deck Plus and Pro, T3-S3, T-Connect Pro, T-ETH-Elite, lora32 v2.1, E213, E290, Wireless Paper, T-Echo, LoRa-APRS boards. A T-Beam and a T-Deck Plus were part of the overnight soak but had no hardware test before the PR.

## Known gaps, stated plainly

- **MHeard and the path table are gone.** Scripts that parse the old `--mheard` table or the full relay-path string break; the first start deletes `/mheard.dat` and `/mhpath.dat`, so that history is lost once.
- **Two mailbox nodes that cannot hear each other both deliver** (up to twice the airtime, bounded by 20 actions per hour and a one-hour pause per entry).
- **A node that hears me but that I never hear does not appear in the matrix.**
- **Mailbox release has a small `gen`-check window, and the dedup tables on nRF52 gateways are unlocked:** harmless apart from one duplicate display or ACK, no memory violation.
- **The stock app does not know the DM status values `0x03` (failed) and `0x04` (held):** it leaves the message unchanged and shows "unconfirmed" after six minutes; a delivered DM is never downgraded.
- **The overnight soak of this build is not evaluated yet** (see above).
- **Carried over from earlier fork releases:** a relay on older firmware strips the ZWJ in compound emoji; multi-hop loss recovery for DM retries (`BACKLOG` PN-01); the central server's masked dedup (`msg_id & 0xFFFFF3FF`).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. Only `v4.40a.10.02` is offered there; older releases are removed from the flasher.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

## Upstream

This build contains nothing that upstream does not have. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
