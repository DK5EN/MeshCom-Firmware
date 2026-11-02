# Campaign CHARS-01 end-to-end (2026-11-02)

National characters on every display path and in the two back ends, as one feature instead of the
board-specific parts A (E290, sp2ong) and B (T-Deck). Concept:
[concept-national-chars-20261101.md](concept-national-chars-20261101.md). Upstream issue #1221.

**PR rule:** the upstream PR goes out only when every path below is done. Its German body credits sp2ong (#1221,
patch `0004`, commit `6eda5f8a`) for the idea and the first implementation, then describes our broadening.

## Operator decisions (2026-11-02)

- E290 / Wireless Paper: add French, Spanish and Portuguese glyphs ourselves (sp2ong's tool), mention it on #1221.
- MCProxy and mc-chat are part of the campaign.
- Fallback for missing glyphs: German umlauts `ae oe ue ss` (`Ae Oe Ue`), every other letter without its accent,
  `€` as `EUR`, typographic quotes and dashes as ASCII.
- OLED: real Latin-1 letters from the u8g2 `_f` fonts, fallback only for the rest.
- Web GUI: fix the `{` cut (any `{` in a message cuts the rest).

## Path map (scout, 2026-11-02)

| Path                                 | Before                                         | Owner wave |
| ------------------------------------ | ---------------------------------------------- | ---------- |
| E290 / Wireless Paper                | PL, DE, `°`, `€`; FR/ES/PT dropped             | W1-B       |
| T-Deck / T-Deck Plus                 | done (`23e03fed`)                              | -          |
| T-Deck Pro RX and own TX             | `utf8ascii()` / blanks                         | W2-E       |
| T-Echo, Tracker, T114, T-Connect Pro | `utf8ascii()` drops PL/FR                      | W2-E       |
| T5 e-paper                           | Latin-1 ok; PL and `€` missing                 | W2-E       |
| OLED (Heltec, RAK, T-Beam)           | raw UTF-8, mixed drawStr/drawUTF8, wrap splits | W2-E       |
| Web GUI                              | UTF-8 ok; `{` cuts the text                    | W1-E       |
| MCProxy                              | TX unfiltered; `\n` `\t` deleted               | W1-C       |
| mc-chat backend                      | no 150-byte TX check, no filter                | W1-D       |

## Waves

| Wave | Agent                   | Exclusive files                                                                                                                  | State                     |
| ---- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------- | ------------------------- |
| W1   | A fallback module       | `src/text_fold.{h,cpp}`, `test/test_text_fold/`                                                                                  | done (firmware W1 commit) |
| W1   | B E290 glyphs           | `src/Fonts/FreeSans9pt7b.h`, `FreeSansBold9pt7b.h`, `src/gfx_charmap.h`, `test/test_gfx_charmap/`, `tools/gfxfont_add_glyphs.py` | done (firmware W1 commit) |
| W1   | C MCProxy               | `../MCProxy` text filter, TX path, tests, README char section                                                                    | done (MCProxy `6df61da`)  |
| W1   | D mc-chat               | `../mc-chat` send path, meteo gate, decoder filter, tests                                                                        | done (mc-chat `bb2ae3b`)  |
| W1   | E web `{` cut           | `src/msg_body.h`, `web_functions.cpp` render site, `lv_obj_functions.cpp` `tdeck_add_MSG`, `test/test_msg_body/`                 | done (firmware W1 commit) |
| W2   | E display paths         | `src/loop_functions.cpp`, `src/tft_display_functions.cpp`, T5/T-Deck Pro display files                                           | done (firmware W2 commit) |
| W3   | bench + docs + PR draft | orchestrator                                                                                                                     | pending                   |

Orchestrator-owned: `platformio.ini` (`[env:native_text_fold]` added before W1),
`test/golden/native/variant-ini-effective.json` (regenerated at the gate).

Flash target after the commit: DK5EN-14 (T-Deck Plus), operator request 2026-11-02.

## Wave 1 result (2026-11-02)

- **Firmware:** `text_fold` (4 targets, 20 host tests incl. an independent CP437 reference and a 0..0x2FFFF sweep),
  `msg_body.h` (trailing `{` + 1..5 digits, the PN parser's rule; also on the T-Deck, where `tdeck_add_MSG` cut at the
  first `{` too), E290 +69 glyphs at 0x9B..0xDF (sp2ong's 0x20..0x9A byte-identical; new glyphs rendered with FreeFont
  20120503 / freetype 2.13.2, a few of his glyphs re-render one pixel different, so his freetype differed).
  Gate stage 1,2 PASS (87 envs, 3308/3308, run `regression-20261102-053430`); `t_deck_plus` and
  `vision-master-e290` build. Two Opus advisor rounds, APPROVED after one rework.
- **Trade-off:** a user text ending in `{2026` is hidden as an ACK marker (pinned by a test); before, everything
  after the first `{` was cut.
- **For W2:** `TF_CP437` assumes Adafruit `setCp437(true)`; a caller must set it, otherwise `ß` and `°` print shifted.
- **Back ends:** mc-chat moved the shared weather contract to UTF-8 bytes (`command_contract.json` sha256
  `b666b2da...`); MCProxy takes it by subtree pull with its byte-based `meteo.py`.

## Wave 2 result (2026-11-02)

- **Every display path folds through `text_fold`; `utf8ascii()` is gone.** T-Deck Pro (RX, own TX, GUI echo),
  T-Echo, Tracker, T114, T-Connect Pro: `TF_ASCII`. T5: `TF_UTF8_LATIN1`. E290 / Wireless Paper: unmapped
  codepoints fall back to `text_fold` inside `gfx_utf8_to_font` (one `?` per ZWJ emoji sequence). Generic OLED
  (Heltec, RAK4631, T-Beam, E22, Stick): message folded to Latin-1 bytes before the byte-wise wrap, every page drawn
  with `drawStr`; the TEMP page and the user-set SSID on the info page are Latin-1 too. T-Deck / T-Deck Plus:
  codepoints outside the font set (Romanian `ș`, emoji, combining marks) folded instead of placeholder boxes
  (`src/t-deck/tdeck_text_cover.h`, coverage pinned against all five font cmaps by `tools/tests/test_tdeck_fonts.py`).
- **Behaviour change for ASCII:** `\t \r \n` runs become one space on the non-LVGL displays (the T-Deck keeps `\n`);
  C0 controls are dropped.
- Buffers static on nRF52 (N-22 pattern). RAK4631 flash 87.7 %, RAM 39.9 %.
- Gate stage 1,2 PASS (88 envs, 3324/3324) before the last advisor fix (SSID fold, board code only; V3, RAK4631,
  T-Beam rebuilt). Advisor: two rounds of rework, the second taken by the orchestrator.
- **Back ends, second advisor round:** mc-chat refuses empty-after-filter and over-long text on the MCP path before
  a msg id is consumed; MCProxy skips an empty BLE send.

## Open after wave 2

- W3: bench (DK5EN-14 T-Deck Plus flashed first; OLED board with the test sentence; E290 not in the fleet),
  concept doc and PR draft (German, sp2ong credit first; see memory `chars01-pr-attribution-and-scope`).
- Follow-ups (BACKLOG CHARS-01): E290/WP status lines (TEMP `°C`, a UTF-8 SSID) print raw UTF-8 bytes into the
  extended GFX font and show two wrong glyphs; `onebutton_functions.cpp:199` prints E290 glyph codes to serial;
  firmware `charset_filter.h` does not strip bidi isolates U+2066-2069 (mc-chat and MCProxy do); MCProxy publishes
  an inbound msg that is empty after the filter; VT/FF are stripped instead of becoming a space in both back ends.
