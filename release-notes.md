> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.35u.09.28.2-neo`: a same-day second cut of `v4.35u.09.28-neo`, adding one bench-verified bug fix.** Everything in `v4.35u.09.28-neo` (official `v4.35u`, plus this fork's neo line, neighbour matrix, store-and-forward and personal-message retries) is unchanged; this build adds a single fix for a message-text bug.

**How to tell this build apart:** the version field on the air and in `--info` still reads `4.35u` — the letter does not change for a same-day cut. The flash stamp stays `FLASH_VERSION 20260928`, identical to `v4.35u.09.28-neo`, so the version field and flash stamp cannot tell the two apart; only the git commit (`8de8d3da` on top of `fed0eb0e`) does. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.**

## New in this build

1. **A compound emoji that uses a ZERO WIDTH JOINER (ZWJ, U+200D) no longer falls apart into two glyphs when sent as a text message.** The firmware's own character filter (`charset_filter.cpp`) treated U+200D as part of a blanket "bidi/zero-width format character" range (U+200B–U+200F) and stripped it from every message before transmission. Unlike its neighbours in that range, U+200D carries no glyph of its own but binds the codepoints either side of it into one grapheme — for example shrug (U+1F937) + ZWJ + male sign (U+2642) + variation selector (U+FE0F) renders as "person shrugging" only if the ZWJ survives. Stripping it does not remove an invisible character, it splits the sequence: the receiver sees the shrug emoji and a separate male-sign glyph instead of one composed icon.

   Found via a live bench test on a RAK4631 (`--loradebug on` / `--txcapture on`): the `TX_FRAME` capture of the actual on-air bytes was missing the 3-byte ZWJ sequence (`E2 80 8D`) between the two emoji halves. Fixed by carving U+200D out of the stripped range; everything else in that block (zero-width space, zero-width non-joiner, the bidi override/embedding characters, the BOM) is still stripped exactly as before.

   The identical exception already existed in this project's separate web-proxy component (MCProxy, `text_decode.py`, added there for the same failure mode on 2026-08-30) — this fix ports that same, already-proven exception into the firmware's own filter, which runs independently on every text-message path (serial, BLE, web, T-Deck).

   Also offered upstream as [PR #1177](https://github.com/icssw-org/MeshCom-Firmware/pull/1177) (source-only, no test changes, since upstream does not carry this fork's native test suite).

## What changes on the air

With default settings, compared with `v4.35u.09.28-neo`:

1. **A text message containing a ZWJ-joined compound emoji now transmits with the joiner intact**, so it renders as one composed glyph at the receiver instead of two separate ones. Every other message is transmitted byte-for-byte identically; the fix only stops one specific codepoint from being removed.

Nothing else changes on the air.

## Supported Hardware

### Verification for this release

- **Host suite:** 875 test cases across the 12 native gate environments, all green, including 2 new regression tests for this fix (`test_charset_filter`): one pins the ZWJ passing through a real compound-emoji byte sequence, the other pins that its immediate neighbours in the same Unicode block (zero-width space, zero-width non-joiner) still get stripped.
- **Build:** 32 release environments build clean.
- **On hardware.** The fix was bench-verified end to end on a RAK4631 (DK5EN-90): flashed the pre-fix build, sent the shrug emoji from the serial console, captured the on-air `TX_FRAME` bytes and confirmed the ZWJ was missing; flashed this fix, repeated the same send, and confirmed the ZWJ bytes are present in the capture and the console's decoded line renders one composed glyph.

### Built and shipped, not on our bench

All 30 boards in this release except the RAK4631, which had bench time for this specific fix.

## Known gaps, stated plainly

- **Only the RAK4631 had bench time for this fix.** The charset filter is shared, board-independent code, so the fix applies identically everywhere, but no other board sent a ZWJ emoji on this build.
- **A relay running older firmware still strips the ZWJ when it re-encodes and forwards a message** — observed during the same bench session (a neighbouring node still on `v4.35u.09.28-neo` re-stripped the joiner on relay). This is expected until that node is reflashed, not a defect in this fix.
- **Everything `v4.35u.09.28-neo` listed as open carries over unchanged:** multi-hop loss recovery for DM retries (`BACKLOG` PN-01), the central server's masked dedup (`msg_id & 0xFFFFF3FF`), and the older gaps listed in [`release.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.2-neo/release.md) and [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.2-neo/docs/CHANGELOG-neo.md).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. Only `v4.35u.09.28.2-neo` is offered there; older releases are removed from the flasher (their GitHub release objects stay).

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [Personal-message retries — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.2-neo/docs/CHANGELOG-snf.md)
- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.2-neo/docs/CHANGELOG-meshcom5.md)
- [neo code-quality campaign — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.2-neo/docs/CHANGELOG-neo.md)

## Upstream

Everything official `v4.35u` contains is in this build. The ZWJ fix is offered as [PR #1177](https://github.com/icssw-org/MeshCom-Firmware/pull/1177); the earlier ACK and server-echo fixes remain offered as [PR #1176](https://github.com/icssw-org/MeshCom-Firmware/pull/1176). The neighbour matrix, store-and-forward and the neo rework have not been offered upstream yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
