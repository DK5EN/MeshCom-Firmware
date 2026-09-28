> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.35u.09.28-neo`: this fork's neo line moved to the version letter `u`, after upstream released the official `v4.35u` on 27 September 2026.** It carries everything in official `v4.35u` (the upstream pull requests #1162 and #1165–#1172), plus neo, the neighbour matrix, store-and-forward and the personal-message retries this fork already shipped in `v4.35t.09.28-neo`, plus four fixes that are not in any official release yet.

**How to tell this build apart:** the version field on the air and in `--info` reads `4.35u` (it read `4.35t` in `v4.35t.09.28-neo`). The flash stamp stays `FLASH_VERSION 20260928`, the same as `v4.35t.09.28-neo`, so the letter is the only difference between the two. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.**

## Relation to official `v4.35u`

Official `v4.35u` is upstream `dev` at `61a58daa`: the base this fork builds on (`6cc8b552`, 26 September) plus nine merged pull requests. This build carries the code of all of them:

- **#1162** safeboot OTA as a state machine, status page, auto-AP, one app slot — in this fork since `v4.35t.09.26-neo`.
- **#1168** personal-message retries with XOR `msg_id` — in this fork since `v4.35t.09.28-neo`.
- **#1169** web RX log lists the oldest line first (this fork's own PR, issue #1154) — new in this build.
- **#1171** (OE1KFR) BLE: 4 s supervision timeout instead of 1.8 s, which iOS rejected and which cut Android's 5 s default short; plus `[BLE ]` connection diagnostics behind `--bledebug` — new in this build.
- **#1165** (makrohard) `--setcall` with the callsign already set confirms it without writing flash or rebooting — new in this build.
- **#1166** (makrohard) the net console on TCP 2323 also starts in Ethernet mode (T-ETH-ELITE), and two opt-in build flags, `DISABLE_BLE` and `DISABLE_BATTERY`, for boards without a usable BLE controller or battery divider. No board in this release sets either flag. — new in this build.
- **#1167, #1170, #1172** documentation (`docs/wiederholungen.md`, not carried here) and the version letter.

Upstream's pull request #1164 (settings saved where they change) is still open and not in this build.

## New in this build, not in any official release

1. **The BLE command ring now holds the whole configuration burst.** After an app connects, the node sends its configuration in one go — up to 12 frames of up to 246 bytes. The command ring was 1,536 bytes on the E22_XML boards and 2,048 bytes on classic ESP32, ESP32-S3 and RAK4631, so the first frames could be pushed out before the app read them; the I register was among them, and the app showed empty node settings. The ring is 3,072 bytes on every board now, and a compile-time check refuses any board whose ring cannot hold the burst. Upstream has this since #1157; the neo byte ring had never received it.
2. **The web messages page no longer renders garbage on the RAK4631 when the ring moves under it.** The history iterator read three ring fields without the lock (same origin as item 1).
3. **A relay or gateway ACK for a retried personal message now reaches the app and stops the retries.** A retry copy carries its own `msg_id` (bits 10–11 flipped); the node looked the ACK up under that id, found nothing, sent the app no tick, kept retrying and even relayed its own ACK as a foreign one. It now folds the id back to the original. Offered upstream as [PR #1176](https://github.com/icssw-org/MeshCom-Firmware/pull/1176) — official `v4.35u` has this bug.
4. **A gateway no longer transmits its own retry copy again when the server echoes it back** after it has left the dedup ring. Same cause, same fix, also in PR #1176.

## What changes on the air

With default settings, compared with `v4.35t.09.28-neo`:

1. **The version field reads `4.35u`.**
2. **A relay/gateway ACK for a DM retry stops the remaining retries** (item 3 above), so fewer retry copies go out.
3. **A gateway sends no second copy of its own DM retry after a server echo** (item 4 above).

Nothing else changes on the air. The BLE timeout, the RX log, `--setcall`, the net console and the command ring act on the phone link, the web page or the node itself.

## Supported Hardware

### Verification for this release

- **Host suite:** 46 native environments, 1,408 test cases, 1,407 passed and 1 skipped. New regression tests, each failing without its fix: the command-ring burst and the iterator lock (`test_byte_fifo`), the ACK fold (`test_pn_retry`), the server-echo fold for both platforms (`test_udp_frame_twin`).
- **Build:** 32 release environments build clean. RAK4631 flash usage 96.1 % (783,532 of 815,104 bytes).
- **Independent review:** an advisor pass over the ported pull requests and the ACK fix found no rework; its one finding became item 4.
- **Not on hardware.** No board had bench time on this build. The 24 h soak of DK5EN-1 and DK5EN-98 that runs until 28 September 17:39 tests the XOR retry build of `v4.35t.09.28-neo` (`ce9bf157`), not this one.

### Built and shipped, not on our bench

All 30 boards in this release.

## Known gaps, stated plainly

- **Nothing in this build ran on hardware.** The four upstream ports are behaviourally the same as upstream's code, but only the build and a scan of the firmware images for their strings prove they are in; no host test reaches them.
- **The ACK tick in the app for a retried DM (item 3) is not proven end to end** — the host test covers the lookup, not a real relay ACK.
- **`DISABLE_BLE` and `DISABLE_BATTERY` were built once with both flags set** (BLE init gone from the image, the disabled marker present) but no board ships with them.
- **Everything `v4.35t.09.28-neo` listed as open carries over:** multi-hop loss recovery for DM retries (`BACKLOG` PN-01), the central server's masked dedup (`msg_id & 0xFFFFF3FF`), the final 24 h soak verdict, and the older gaps listed in [`release.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28-neo/release.md) and [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28-neo/docs/CHANGELOG-neo.md).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. Only `v4.35u.09.28-neo` is offered there; older releases are removed from the flasher (their GitHub release objects stay).

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [This release and the personal-message retries — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28-neo/docs/CHANGELOG-snf.md)
- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28-neo/docs/CHANGELOG-meshcom5.md)
- [neo code-quality campaign — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28-neo/docs/CHANGELOG-neo.md)

## Upstream

Everything official `v4.35u` contains is in this build. The ACK and server-echo fixes are offered as [PR #1176](https://github.com/icssw-org/MeshCom-Firmware/pull/1176). The neighbour matrix, store-and-forward and the neo rework have not been offered upstream yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
