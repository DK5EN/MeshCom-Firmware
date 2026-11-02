# National characters on node displays (CHARS-01)

Status: implemented on `fork-dev` (2026-11-02), E290 bench and upstream PRs open. Backlog row `CHARS-01` in
[`BACKLOG.md`](BACKLOG.md) section 3.2.

## Result (2026-11-02)

- **Part A done as planned:** sp2ong's patch applied with his authorship (`6eda5f8a`), env block placed by hand,
  ini baseline regenerated (`08041fda`). Host gate green, both E290 envs build. **E290 bench still open:** no E290
  in the fleet.
- **Part B done, with two deviations from the plan below:**
  - **Fallback fonts instead of full fonts.** `src/t-deck/fonts/mc_font_latin_N.c` carry only U+00A0-U+017F plus
    `– — ‘ ’ “ ” „ • … €`; `.fallback` points at the built-in `lv_font_montserrat_N` for ASCII and `LV_SYMBOL_*`.
    Flash +70 KB in total (sizes 12-18 linked, 20 unused), not 30-60 KB per size.
  - **Phone punctuation added.** Phones replace `'` and `"` with `’` and `“ ”`. Without these glyphs French text would
    show boxes, which looks worse than before. U+202F (narrow no-break space, French) is not in Montserrat; the
    T-Deck message path maps it to a space.
- **Bench DK5EN-14:** outgoing bubble (`::{TEST}` over 2323) and a DM received from DK5EN-98 via McApp render
  German, Polish, French, Spanish, Portuguese, the punctuation and `€` correctly.
- **Pitfall found by the advisor:** `lv_font_conv --lcd` makes a sub-pixel font, which LVGL with
  `LV_USE_FONT_SUBPX 0` draws as blank gaps. Guarded by `tools/tests/test_tdeck_fonts.py`.
- **Still open:** French, Spanish and Portuguese letters on E290 / Wireless Paper (about 40 glyphs into sp2ong's
  fonts with `tools/gfxfont_add_glyphs.py`; ask him on #1221 first), upstream PRs (A by sp2ong or with his authorship,
  B as our own).
  Upstream issue: [icssw-org/MeshCom-Firmware#1221](https://github.com/icssw-org/MeshCom-Firmware/issues/1221).

## Verdict

**Both parts can be done. E290 / Wireless Paper can take sp2ong's patch almost as is; the T-Deck
Plus needs its own fix because it renders through LVGL, which the patch does not touch.**

| Part | Boards                        | Display stack                    | Work                                       |
| ---- | ----------------------------- | -------------------------------- | ------------------------------------------ |
| A    | E290, Wireless Paper (E213)   | GxEPD2 / Adafruit GFX, `GFXfont` | Port sp2ong's patch `0004`, bench on E290  |
| B    | T-Deck, T-Deck Plus           | LVGL 8, built-in Montserrat      | Generate Latin fonts, drop `utf8ascii()`   |
| -    | OLED boards (SSD1306, SH1106) | `utf8ascii()` to CP437           | Out of scope; keep today's transliteration |

## Background

- Messages travel as UTF-8 (Android app, WebDesk, web GUI). Polish letters are two-byte sequences
  `C4xx` / `C5xx` (plus `ó` = `C3 B3`); German umlauts are `C3xx`.
- `utf8ascii()` (`src/loop_functions.cpp`, around line 6531) knows only `äöüÄÖÜß`, `°` and `€`. Every
  other non-ASCII character returns 0 and is dropped. "duszków" shows as "duszkw".
- The `String` overload of `utf8ascii()` spells umlauts out as `ae`, `oe`, `ue`, `ss`; the byte and
  `char*` overloads map them to CP437 codes (`0x94`, `0x92`, ...).

## Part A: E290 and Wireless Paper (issue #1221)

**sp2ong's patch fixes it; his screenshot shows it working.** The E290 photo attached to the issue
(2026-10-30) displays "Zażółć gęślą jaźń Łódź i Ściana Żaba. Ärzte trinken Öl über den Fluss. Süße
Mäuse essen." with every Polish and German character correct.

What the patch (`0004-e290-national-chars.patch`, attached to the issue, author sp2ong) does:

- **Fonts:** `src/Fonts/FreeSans9pt7b.h` and `FreeSansBold9pt7b.h` get 27 glyphs at codes
  `0x80..0x9A`: `ąćęłńóśźż`, `ĄĆĘŁŃÓŚŹŻ`, `äöüÄÖÜß`, `°`, `€`. ASCII glyphs unchanged, about
  0.7 KB per font. About 100 free codes remain for more languages.
- **Mapping:** `src/gfx_charmap.h`, a sorted UTF-8 codepoint to glyph-code table plus decoder, pure
  C++. Unknown characters are dropped.
- **Call site:** `sendDisplayText()` uses `gfx_utf8_to_font()` under
  `#if defined(BOARD_E290) || defined(WP_DISP)`; all other boards keep `utf8ascii()`.
- **Test:** `test/test_gfx_charmap/test_main.cpp`, new host env `[env:native_gfx_charmap]`.
- **Tool:** `tools/gfxfont_add_glyphs.py` (freetype) adds further glyphs the same way.

State against `fork-dev` (`16a65966`):

- Everything except `platformio.ini` applies cleanly (`git apply --check --exclude=platformio.ini`).
- `platformio.ini` conflicts only on context: the new env block must be placed by hand.

Open points:

- **Ghosting:** our OTP build no longer runs a periodic full refresh by default
  (`96e16e6e`, `E290_FULL_EVERY=0`). sp2ong reports leftover "ghosts" of earlier messages. Bench on
  our own E290 with that setting before trusting his test.
- **Wireless Paper / E213:** same fonts and code path, not tested by sp2ong. No bench board here.
- **Upstream:** sp2ong offered to open the PR himself. If he does not, submit it with him as author
  (`git am` keeps the authorship).

Steps:

1. `git am` the patch on `fork-dev`, resolve the `platformio.ini` block by hand.
2. Host gate `tools/regression.sh --stage 1,2` (includes the new `native_gfx_charmap`).
3. Build `vision-master-e290` and the OTP env, flash our E290, send a DM to group `TEST` with the
   test sentence above. Check glyphs and ghosting.

## Part B: T-Deck Plus

**Why it shows no umlauts:** the T-Deck renders with LVGL and only the built-in Montserrat fonts
(`variants/t_deck_plus/lv_conf.h`: sizes 12, 14, 16, 18, 20; `LV_TXT_ENC_UTF8`). Those fonts cover
ASCII `0x20..0x7F` plus symbols only: no `äöüß`, no Polish letters.

To avoid missing glyphs, received messages go through `utf8ascii()` before display
(`src/t-deck/lv_obj_functions.cpp:4673` in `tdeck_add_MSG()` and `:4750` for the bubble body,
added upstream in `babac781`). That turns umlauts into `ae`/`oe`/`ue`/`ss` and drops every other
non-ASCII character.

Open question for the operator: from the code the T-Deck should show "Maeuse", not blanks. If it
shows blanks or boxes, some text reaches LVGL past `utf8ascii()` and that path must be found too.

Fix:

1. Generate Montserrat in the sizes in use (12-20, `bpp 4`) with `lv_font_conv`, range ASCII plus
   `U+00A0-U+017F` (Latin-1 Supplement and Latin Extended-A: German, Polish, Czech, French, ...),
   plus the FontAwesome symbols the UI uses (`LV_SYMBOL_*`). Montserrat is OFL-licensed.
2. Declare them through `LV_FONT_CUSTOM_DECLARE` and replace the `lv_font_montserrat_*` references
   in `src/t-deck/` (and `LV_FONT_DEFAULT`).
3. Remove `utf8ascii()` from the T-Deck message path; LVGL decodes UTF-8 itself.
4. Check the outgoing path, the message preview (`last.body`, around line 3113) and the
   `/messages.json` reload, so stored messages render the same.

Cost: estimated 30-60 KB flash per font size (not measured); the T-Deck has 16 MB flash
(`partitions-16MB-safeboot.csv`). Fonts live in flash, no RAM cost beyond LVGL's glyph cache.

Test: T-Deck Plus on the bench, DM to group `TEST` with the test sentence, check all sizes in use
(bubble, header, preview).

## Order

1. Part A first: port the patch with sp2ong as author, host gate, E290 bench.
2. Part B as a separate wave, tested on the T-Deck Plus.
3. Upstream: Part A via sp2ong's PR (or ours with his authorship), Part B as our own PR.
