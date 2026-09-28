> [!IMPORTANT]
> **This is not the official MeshCom firmware.** The official firmware is developed and released by the ICSSW team at [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) — please look there first, and support the project there.

## What this release is

**`v4.35u.09.28.3-neo`: a same-day third cut of `v4.35u.09.28-neo`, adding one bench-verified bug fix on top of `v4.35u.09.28.2-neo`.** Everything in `v4.35u.09.28.2-neo` (official `v4.35u`, plus this fork's neo line, neighbour matrix, store-and-forward, personal-message retries and the ZWJ emoji fix) is unchanged; this build adds a single fix for text sent from the web interface. It replaces all earlier releases.

**How to tell this build apart:** the version field on the air and in `--info` still reads `4.35u` — the letter does not change for a same-day cut. The flash stamp stays `FLASH_VERSION 20260928`, identical to the two earlier 09.28 cuts, so only the git commit (the fix is `ea16bfef`, on top of `v4.35u.09.28.2-neo`) and the build time in `--info` tell them apart. `FLASH_STRUCT_VERSION` stands at `20260724`, unchanged — **your configuration survives this update.**

## New in this build

1. **Text typed into the web interface now goes on the air as proper UTF-8, whatever the language** ([#1173](https://github.com/icssw-org/MeshCom-Firmware/issues/1173)). The browser sends every form value percent-encoded (`ą` travels as `%C4%85`). The web server's decoder, `decodeURLPercentCoding()`, did not really decode: it replaced a fixed list of about 60 codes (German umlauts, `ß`, a few Italian vowels, ASCII punctuation) and left every other code in the text verbatim. Polish, Czech, Cyrillic, emoji and every other script therefore went out as literal `%C4%85`-style text. Reproduced on a Heltec V3 with the previous build: `ąęś äö` sent to group 9 was stored and sent as `%C4%85%C4%99%C5%9B äö`.

   The fix is a real single-pass decoder (`src/url_decode.cpp`): every `%XX` becomes its byte, so any UTF-8 character arrives byte for byte. The old quirks are kept (`+` is a space, a line break becomes `-`, `"` is dropped); decoded control characters are now dropped too, so a `%00` cannot cut a value short. The same decoder serves the settings page, function calls and the login, so they gain the same fix. Three smaller changes ride along:

   - The web message is still capped at 150 bytes, but the cut now falls between characters instead of inside a multi-byte one.
   - The message panel ran `decodeURIComponent()` on the raw HTML it polls. That froze the panel as soon as any message contained a bare `%` (e.g. `100% sure`), and it masked this bug: the sender's own screen turned `%C4%85` back into `ą` while every other node showed the code. It is removed.
   - HTML responses declare `charset=utf-8` in the HTTP header, not only in the page's `<meta>` tag.

   Offered upstream as [PR #1178](https://github.com/icssw-org/MeshCom-Firmware/pull/1178) (source only; the 14-case native regression test `test_url_decode` stays in this fork, since upstream does not carry the native test suite).

## What changes on the air

With default settings, compared with `v4.35u.09.28.2-neo`:

1. **A message sent from the web interface that contains characters outside the old list now transmits as UTF-8** instead of as percent-code text. Messages that only use ASCII, German umlauts or the handful of Italian vowels the old list knew are transmitted byte-for-byte identically. Messages sent over serial, BLE or the T-Deck keyboard never went through this decoder and do not change.

Nothing else changes on the air.

## Supported Hardware

### Verification for this release

- **Host suite:** 889 test cases across the 12 native gate environments, all green, including 14 new regression tests for this fix (`test_url_decode`). Against a C port of the old decoder, 6 of the 14 fail, the Polish case among them.
- **Build:** 32 release environments build clean.
- **On hardware.** Bench-verified on a Heltec V3 (DK5EN-1) through its web interface: with the previous build, `ąęś äö` sent to group 9 was stored as `%C4%85%C4%99%C5%9B äö`; after an OTA update to this build, `ąęśćłóżź äö 100% ok` was stored exactly as typed, and the response header carried `charset=utf-8`.

### Built and shipped, not on our bench

All 30 boards in this release except the Heltec V3, which had bench time for this specific fix.

## Known gaps, stated plainly

- **Only the Heltec V3 had bench time for this fix, and no second node received the test message.** The check read the sending node's own message store, which holds the exact text handed to the radio. The decoder is shared, board-independent code; the nRF52 builds (RAK4631, T114, T-Echo) compile it but were not exercised.
- **The character counter under the message field still counts characters, not bytes.** Most Polish letters take two bytes, so a long message with many of them is cut before the counter reaches zero — cleanly between characters now, but earlier than the counter suggests.
- **Everything `v4.35u.09.28.2-neo` listed as open carries over unchanged:** a relay on older firmware still strips the ZWJ in compound emoji, multi-hop loss recovery for DM retries (`BACKLOG` PN-01), the central server's masked dedup (`msg_id & 0xFFFFF3FF`), and the older gaps listed in [`release.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.3-neo/release.md) and [`docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.3-neo/docs/CHANGELOG-neo.md).

## Installing

**[Web flasher](https://dk5en.github.io/MeshCom-Firmware/flash/)** — flash over USB from the browser, 30 boards, board detection built in. Only `v4.35u.09.28.3-neo` is offered there; older releases are removed from the flasher.

Otherwise, pick the asset for your board below. ESP32 boards take the `.bin` over USB or, if the node is already reachable, over WiFi with `tools/webflash.py`. The three nRF52 boards (`wiscore_rak4631`, `heltec_t114`, `t_echo`) ship both a `.uf2` — double-tap reset, copy the file to the volume that appears — and a DFU `.zip` for `adafruit-nrfutil` over serial.

`FLASH_STRUCT_VERSION` is unchanged, so your settings survive.

## Changelogs

- [Personal-message retries — `docs/CHANGELOG-snf.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.3-neo/docs/CHANGELOG-snf.md)
- [Neighbour matrix — `docs/CHANGELOG-meshcom5.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.3-neo/docs/CHANGELOG-meshcom5.md)
- [neo code-quality campaign — `docs/CHANGELOG-neo.md`](https://github.com/DK5EN/MeshCom-Firmware/blob/v4.35u.09.28.3-neo/docs/CHANGELOG-neo.md)

## Upstream

Everything official `v4.35u` contains is in this build. The web-interface UTF-8 fix is offered as [PR #1178](https://github.com/icssw-org/MeshCom-Firmware/pull/1178); the ZWJ fix ([PR #1177](https://github.com/icssw-org/MeshCom-Firmware/pull/1177)) and the ACK and server-echo fixes ([PR #1176](https://github.com/icssw-org/MeshCom-Firmware/pull/1176)) have been merged upstream. The neighbour matrix, store-and-forward and the neo rework have not been offered upstream yet. Please report bugs that also exist in the official firmware to [icssw-org/MeshCom-Firmware](https://github.com/icssw-org/MeshCom-Firmware) directly.
