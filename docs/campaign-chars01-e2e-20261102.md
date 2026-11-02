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
| W1   | C MCProxy               | `../MCProxy` text filter, TX path, tests, README char section                                                                    | running                   |
| W1   | D mc-chat               | `../mc-chat` send path, meteo gate, decoder filter, tests                                                                        | running                   |
| W1   | E web `{` cut           | `src/msg_body.h`, `web_functions.cpp` render site, `lv_obj_functions.cpp` `tdeck_add_MSG`, `test/test_msg_body/`                 | done (firmware W1 commit) |
| W2   | E display paths         | `src/loop_functions.cpp`, `src/tft_display_functions.cpp`, T5/T-Deck Pro display files                                           | pending                   |
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
