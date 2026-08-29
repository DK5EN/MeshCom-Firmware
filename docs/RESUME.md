# RESUME — T-Deck Plus, pick up here

Last session: 2026-08-28 evening to 2026-08-29 ~01:30. Read this, then
[`tdeck-findings-20260828.md`](tdeck-findings-20260828.md) for the measurements.

## Where things stand

| Branch                        | HEAD       | Contents                                                                                                                     | Device state                 |
| ----------------------------- | ---------- | ---------------------------------------------------------------------------------------------------------------------------- | ---------------------------- |
| `v4.35p_prio`                 | `afc6a54f` | G07 fix, all serial test hooks + host harness, diagnostics (default off), docs. `full_refresh=1`, flush mitigation **off**.  | —                            |
| `tdeck-partial-refresh-trace` | `e6620133` | `v4.35p_prio` + `full_refresh=0` + flush mitigation **on** + map fixes (centring, stitching, g/h keys, G01 UAF) + SD 20 MHz. | **DK5EN-14 runs this build** |
| `tdeck-partial-refresh-wip`   | `3a7aa2f5` | The previous team's partial-refresh attempt, parked. Superseded; can be deleted.                                             |                              |

Both branches are pushed to `origin`. Working tree clean.

## What is fixed (verified on hardware)

1. **Incoming messages not drawn** — first TFT flush after an SD access on the shared SPI bus is
   lost by the panel; NOP transaction before each flush (`disp_flush`, `--flushfix`). 0/30 -> 30/30.
2. **Map: own position not centred / jumping** — viewport-sized image composed from the tiles that
   intersect it, position at the centre by construction. `center_err 0/0` at every zoom level.
3. **Map: reboot on zoom-out with other stations** — G01 use-after-free in `add_map_point()`
   (slots not NULLed after `lv_obj_del`). Reproducer crashes at step 7 without the fix, clean with it.
4. **Map: tile loading 1.6-4.5 s** — SD card was mounted at 800 kHz; now 20 MHz (0.33-0.79 s).
   40 MHz measured identical: the read path is ~1.3 MB/s either way, PNG decode dominates.
5. **Zoom keys `g`/`h`**, one `tdeck_map_zoom()` for all five entry points (handover A1).
6. **G07** draw-buffer size in pixels (prerequisite for partial refresh).
7. **Partial refresh** (`full_refresh=0`) works with 1+2 in place: idle pixel traffic 14x lower.
8. Boot fully mirrored to serial; `[AUDIO]` errors explicit; `connecttoFS()` return checked.

## What is measured but not yet fixed

- Idle repaint driver: `update_header_batt_indicator()` rewrites labels every 500 ms unconditionally.
- Message tone: `play_cw('r')` blocks the loop task 1.10 s per message (paper §6: move off loopTask,
  fix audio task priority 50->3).
- Heap: ~340 B internal per message, unbounded (`msg_list` never trimmed, `persisted_msgs` dead).
- Tone-file lookup on SD per message (remove: resolve once at boot).
- Boot: 2 s busy-wait per boot message (8 messages = 16 s).
- Exact SPI2 register clobbered by the SD library (the NOP transaction works around it).
- Handover §4.2 items not touched: G02 (`ic <= MAX_MAP`), G03, K2, F1, C3, C4, G08, G10-G16, H2.

## Decisions taken (2026-08-29 01:40)

- **PR scope:** partial refresh (`full_refresh=0`) **and** the flush mitigation together — complete
  work, not half of it. The mitigation becomes permanent code in `disp_flush()` (no switch).
- **Review flaws in the PR:** the measured/confirmed ones and the trivial ones (G01, G02, G07, A1,
  `connecttoFS()` return check, C1 audio task priority if trivially safe); everything else stays in
  `BACKLOG.md`.
- **SD at 20 MHz goes into the PR, gated to `BOARD_T_DECK_PLUS`** (the only hardware we can test;
  no other cards available). If it comes back from the field, we will see.
- **Tile file format** is the open question for tomorrow (see below); not part of this PR.

## Plan for 2026-08-29 (agreed; supersedes the earlier list)

Upstream state and branch model: `BACKLOG.md` §3.8g and §4.1. Short form:

1. Docs, commit, push. 2. Tag + delete stale branches (§4.2). 3. `/fable-review` on
   `fc83554e..upstream/dev` (UP-01..04). 4. **done** — `upstream/dev`
   merged (merge commit on this branch, resolution in its message; four targets + native green). 5. Bench-verify on DK5EN-14. 6. Layer the T-Deck fixes as small commits, then build `pr/tdeck-ui` from
   `upstream/dev` (firmware files only, one commit, German description) — include the UP-01
   `serializeJson` bound fix with a native regression test, like #1102.

Not in the PR: `d26e39d5` instrumentation, `src/t-deck/tdeck_debug.*`, `src/test_inject.*`,
`--` test commands, `lib/lvgl` hook, `tools/bench/*`, docs. #1103 is dead upstream (`FWDATE`
key removed); a build date for the app needs a new proposal within the 244-byte frame.

## Open decision: tile format on the SD card

Decoding a 256x256 PNG with lodepng costs ~95 ms per tile on the S3; a zoom step touches 2-4 tiles.
Options, cheapest first:

- **Raw RGB565 tiles** (`.565`, 131 072 B each, no decoder, `memcpy` into the composed image):
  ~0 ms decode, 128 KB/tile on the card (PNG is ~35-50 KB). The 5 909-tile Europe set would grow
  from ~150 MB to ~770 MB — fine on a 30 GB card. Converter is a 20-line Python script; the
  downloader can emit both.
- **Decoded-tile LRU cache in PSRAM** (4-9 tiles x 128 KB): zoom-back and small moves instant,
  first load unchanged. Independent of the file format; do it in any case.
- **8-bit palette PNG** (pngquant): lodepng still decodes but ~2-3x less filter/inflate work;
  smaller files. Middle ground if card space matters.
- QOI: fast decoder (~10 ms/tile), files ~PNG size; needs a new decoder in the tree.

Measure with the `[SDMAP]` `read/decode` split before choosing.

## Bench facts

- DK5EN-14 on `/dev/cu.usbmodem1101`; opening the port resets it; wait until `--uistat` answers
  (~11 s after `CLIENT STARTED`). Harness: `tools/bench/tdeck_harness.py --scenario all`.
- `--screencrc` is void (panel does not drive MISO). Panel truth = operator's eyes; LVGL truth =
  `[FLUSH]` CRC / `--framedump`.
- Eye tests: one yes/no per run. Scratch scripts of this session are in the session scratchpad
  only; the reusable ones should move into the harness (`map` scenario, crash reproducer).
