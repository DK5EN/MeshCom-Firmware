> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**neo is `upstream/dev` plus better code. Nothing else.** This build is official `4.35t` plus everything the ICSSW team has merged into `dev` since (up to [`6cc8b552`](https://github.com/icssw-org/MeshCom-Firmware/commit/6cc8b552), PRs #1157–#1161) plus this fork's code-quality campaign: duplicated logic unified, dead files removed, and the handful of real bugs that duplication had let drift apart, fixed. It carries no new protocol feature and no new operating mode over `dev`.

**The promise: on the air, this firmware behaves like official `4.35t`. It only gets better under load** — more free heap, more flash headroom, the same frames. Where behaviour genuinely differs from upstream, it is because upstream's own duplication had let a bug through, and that is named below and in the linked changelog.

**How to tell this build apart:** not by the version letter and not by `FLASH_VERSION` — both report exactly what official `4.35t` reports (`4.35t`, `FLASH_VERSION 20260912`). The tell is the build date in `--info` / the `IS1` JSON, and the tag or filename this asset came from (`v4.35t.09.26-neo`) or the entry on the [web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/). `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.**

This release replaces `v4.35t.09.21-neo`, whose release and tag were deleted; a body for it was never committed, so this note is the first one for the neo line.

## What is new since the last neo build

The previous neo build was cut on the evening of 21 September. Everything since then is below; detail, evidence and the restructuring/bug-fix classification are in [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.26-neo/docs/CHANGELOG-neo.md) (items 118–129 plus the chapter entries named here).

**Safeboot: the complete recovery rework** (`a04d9c87`, chapter K15). The 21 September build carried only part of it. Now in: the OTA session as a state machine with a generation guard, a status page with network panel and WiFi scan, an access point after 25 s without a join, `/ota/info`, `/ota/state`, `/ota/scan`, and single-app-slot handling (`app_valid` instead of the old 180-second fallback loop). The same work is offered upstream as [PR #1162](https://github.com/icssw-org/MeshCom-Firmware/pull/1162), still open. The safeboot partition is written only by the web flasher or a serial flash, not by OTA.

**Upstream catch-up of 25 September** (`def2dc7e`, items 118–123): KISS/TCP on port 8001 (#1151, DH1FR; off by default, here also on E22_XML-DevKitC), the via settings as a second node-settings JSON `SN1` (#1155), the build date as `IS1` (#1156), and this fork's own #1152 (T-Beam-1W battery shown as USB after a settings wipe, `551f9a0b`) and #1153 (the web GUI "Voltage" switch did nothing, `248662ff`). `--via on/off` no longer stores "ON"/"OFF" as the via call sign (`0923e037`, found on the bench).

**Ping and pong over BLE** (`3b018f03`, `1a14bef5`, `b55fe5d7`): a pong addressed to the node's own call sign now also reaches the BLE client (P13); a `{ping}` typed in the app goes out once instead of being retransmitted up to three times at 40-second intervals (P14); own messages without retransmission keep their priority in the TX queue instead of being ranked like relayed traffic (P15).

**Upstream catch-up to `6cc8b552` (PRs #1157–#1161, merge `43760732`, items 124–129).**

- **#1157** is this branch's own RAM-reclaim work (byte-FIFO ring buffers, the MHeard-throttle fix, the web header held as `String`) coming back from `fork-main`, now merged into `dev` itself. Resolving the merge kept this branch's structure where it already carries the behaviour, and took four pieces from upstream's side of the same change:
  - `sendPing()` now reports a refused TX-ring entry loudly (`[PING]...not queued: TX ring refused the frame`) instead of dropping it silently — a visibility fix, nothing changes on the air since the ping was never queued either way.
  - The BLE command ring `RING_BYTES_PHONECOM` grows to 3072 B on every board class, so the whole configuration burst fits.
  - A new diagnostic marker, `[MC-DBG] RING_OVERFLOW buf=phone`.
  - The byte-ring iterator that feeds the web message page now takes its snapshot under the existing lock, instead of reading a ring that a writer could still be mutating.
- **#1158** (OE1KFR, `f73cbf9e`): `WSPWD` and `ASYM` move from the `SN` node-settings JSON to `SN1`. `SN` was already about 250 characters without a web password, over the 244-byte BLE payload limit, and the app was silently losing `ASYM` always and `GWS`/`BLED` once a password got long enough. New on this branch with this merge.
- **#1159–#1161** (OE1KBC): comments in `4.35t` preparing two MSB repeat-count bits in `msg_id`; the mask itself stays commented out upstream ("discussion ongoing"). No behaviour change.

Gate for the whole merge: 36 native host environments, 1070/1070 test cases; `test/golden/selftest.sh` green.

## What changes on the air

Today's merge changes nothing on the air: logging, RAM sizing and a BLE JSON field. Since the previous neo build, three things do:

1. **An app-originated `{ping}` is sent once** (P14), not up to three times.
2. **Own messages without retransmission (ping, pong, ACK) keep their priority** in the TX queue (P15), so on a busy node they no longer wait behind relayed traffic. Frame bytes are unchanged; send order can differ.
3. **KISS/TCP** (#1151, upstream behaviour) can put a client's frames on the air — only when switched on with `--kiss on` and `--kiss tx on`; it is off by default.

Everything else behaves as official `4.35t` plus upstream `dev`.

## Supported Hardware

### Verification for this release

- **Host suite**: 36 native environments, 1070/1070 test cases; `test/golden/selftest.sh` green.
- **Build**: all 32 release environments build clean.
- **No board has run this release image.** Nothing in today's merge (`43760732`) has been loaded onto hardware. What bench history exists belongs to earlier neo builds, not this one:
  - A 2026-09-19 differential run against `upstream/dev` on DK5EN-1 (Heltec V3) and DK5EN-92 (T-Beam): no measurable difference on the air, +11,296 B and +15,560 B free heap respectively, and a clean 12-hour soak on three nodes afterward (see "Auf der Bank gemessen" in [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.26-neo/docs/CHANGELOG-neo.md)).
  - A 2026-09-25 bench pass on DK5EN-1 covering KISS/TCP, `IS1`, `SN1` and the `--via` fix from that day's upstream catch-up (`docs/neo-upstream-merge-20260925.md`).

### Built and shipped, not on our bench

Every board in the 32 release environments builds from the same source and inherits the whole campaign, but none of them has bench time on this specific image — see "Known gaps" below.

## Known gaps, stated plainly

- **No hardware has run today's merge.** Everything above the host suite and the clean build is unverified for this image.
- **`RING_BYTES_PHONECOM` grew by roughly 1 kB RAM per board class and has not been measured on hardware.** Whether the larger BLE command ring actually stops a truncated configuration burst at the app is an inference from the ring size, not an observation.
- **The safeboot rework is ahead of upstream.** This build carries it (K15); upstream has it only as the open [PR #1162](https://github.com/icssw-org/MeshCom-Firmware/pull/1162). Its abort bench (6/6) ran on DK5EN-1 on 25 September on fork-main, not on this image.
- **E22_XML-DevKitC and the T-Beam family run with thin link-time headroom.** The campaign closed the worst of it (see chapter K18 in the changelog), but these environments are still the ones to watch after any further change.
- Everything `docs/CHANGELOG-neo.md` already lists per chapter as "kein Hardware-Nachweis" or "kompilier-verifiziert" carries over unchanged — this merge did not add bench time anywhere in the tree.

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. This is the easiest path for most boards.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelog

- **[MeshCom neo Changelog](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35t.09.26-neo/docs/CHANGELOG-neo.md)** — items 124–129 are new since the last documented catch-up (2026-09-25); the rest of the document is the full code-quality campaign this branch carries.

## Upstream

This branch tracks `upstream/dev` and offers its own work back as individual pull requests once it is proven here; #1157–#1161 above are already upstream. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
