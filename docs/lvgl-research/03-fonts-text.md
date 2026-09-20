# Track 3 — Fonts, font sizes and text rendering in LVGL 8.3

See `00-CONTEXT.md` for shared hardware/config facts; not repeated here.

## TL;DR for the coding agent

1. Only `LV_FONT_MONTSERRAT_28` (bpp=4, uncompressed) is compiled in; it costs ~18.4 KB of glyph
   bitmap data + ~1.3 KB glyph-dsc table + small cmap/kern tables in **flash**, **zero RAM**. This is
   the right kind of cost for a 16 MB-flash device — do not switch it to `_COMPRESSED` and do not
   move fonts to runtime loading (`lv_font_load`) on this board.
2. **German umlauts (ä, ö, ü, ß) are NOT in Montserrat-28's compiled character set.** Its cmap only
   covers ASCII 0x20–0x7F, degree sign (0xB0), bullet (0x2022), and ~61 FontAwesome symbol
   codepoints (0xF0xx–0xF7xx). Any German string containing ä/ö/ü/ß currently renders each such
   character as a **hollow rectangle outline** (LVGL's placeholder-glyph box), silently, because
   `LV_USE_LOG` is `0` in this build (no warning printed). Fix: regenerate the font with an extended
   range (`-r 0x20-0xFF` or explicit `0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF`) via `lv_font_conv`.
3. `lv_label_set_text()` in LVGL 8.3 does **not** skip work when the new text equals the old text —
   it unconditionally calls `lv_obj_invalidate()` and (for a different string) frees+reallocs the
   text buffer, every call. The caller must add its own `if (strcmp(old, new) != 0)` guard for a
   frequently-updated label (e.g. a header clock/battery/GPS label redrawn on every timer tick).
4. Use `lv_label_set_text_static()` for labels whose string is a `const char*` literal or a
   long-lived buffer (menu captions, static UI chrome) to skip the malloc/copy entirely.
   `lv_label_set_text_fmt()` always allocates (it's `lv_label_set_text()` + `snprintf`) — do not
   call it in a hot loop.
5. `LV_LABEL_LONG_SCROLL` / `LV_LABEL_LONG_SCROLL_CIRCULAR` install an **infinite** (`LV_ANIM_REPEAT_INFINITE`)
   offset animation that calls `lv_obj_invalidate()` on every animation timer tick — which in this
   project's config (`LV_DISP_DEF_REFR_PERIOD 10`) fires up to 100×/second, forever, for as long as
   that label exists, even if nothing else on screen changes. **The repo does not currently use
   either mode** (grep confirms only `LV_LABEL_LONG_CLIP` and `LV_LABEL_LONG_WRAP` are used) — keep
   it that way; do not introduce scrolling long-text labels for a battery-relevant, blocking-flush
   device.
6. The message list (`lv_obj_functions.cpp`, `msg_list_append_bubble`) does not use `set_text` on
   reused labels at all — it builds a fresh multi-widget "chat bubble" (wrapper, header row, header
   label, body label, footer row, timestamp label, plus 2 heap-allocated event-data structs) per
   message. A live append to the active tab is incremental (cheap-ish), but switching tabs calls
   `msg_list_clear()` + rebuilds **all** bubbles in that tab from scratch — up to `MSG_TAB_MAX_MESSAGES
= 50` bubbles × ~6 widgets = ~300 widget creations + flex-layout passes in one burst. This is
   already capped per-tab at 50; do not remove that cap, and do not "fix" the live-append path by
   converting it to a data-vector diff/rebuild — the current incremental-append-on-active-tab
   behavior is correct and should be preserved.
7. `LV_TXT_LINE_BREAK_LONG_LEN` is `0` in this config, i.e. LVGL will not force-break inside a long
   unbreakable word — with `LV_LABEL_LONG_WRAP` a long German compound word (common — German has no
   spaces inside compounds) can overflow its box horizontally instead of wrapping. Prefer
   `LV_LABEL_LONG_DOT` or set a non-zero `LV_TXT_LINE_BREAK_LONG_LEN` if this becomes visible.
8. `LV_USE_FONT_SUBPX` is `0` and should stay `0`. This is a ST7789 driven over SPI by TFT_eSPI —
   subpixel font rendering only helps when the panel's physical RGB stripe order and orientation are
   known and fixed (its entire purpose is aligning glyph edges to individual R/G/B subpixel columns);
   for an SPI TFT accessed as a generic 16-bit RGB565 framebuffer there is no software-visible
   subpixel geometry to align to, so it would only add ~3× font flash size and CPU cost for no visual
   benefit. Confirmed pointless for this hardware.
9. `LV_USE_BIDI 0` and `LV_USE_ARABIC_PERSIAN_CHARS 0` — correct, keep both off; German/Latin text
   needs neither, and both add CPU cost per `lv_label_set_text` call if enabled.
10. Recommended font set for this 320×240 2.8" panel: keep Montserrat-28 as the large/legible size for
    message bodies and headers, and add **one** smaller size (Montserrat-16 or -18) for secondary
    chrome (timestamps, status bar, list metadata) — see Finding 3 for sizing rationale. Do not enable
    more than 2–3 sizes; each additional size is another ~10–20 KB of flash and, more importantly,
    another font pointer to keep track of consistently across styles.

## Findings

### 1. LVGL 8.3 font architecture: `lv_font_t`, `lv_font_fmt_txt_dsc_t`, bpp, compression

**Claim.** An `lv_font_t` (`lib/lvgl/src/font/lv_font.h:64`) is a vtable: `get_glyph_dsc`,
`get_glyph_bitmap`, `line_height`, `base_line`, an optional `subpx` flag, an opaque `dsc` pointer to
format-specific data, and an optional `fallback` font pointer (chain of fonts LVGL walks until one
resolves the glyph). LVGL's built-in "fmt_txt" format (used by all Montserrat fonts) stores its data
in `lv_font_fmt_txt_dsc_t` (`lib/lvgl/src/font/lv_font_fmt_txt.h:159`): a `glyph_bitmap` byte array,
a `glyph_dsc[]` array of per-glyph metrics, one or more `cmaps[]` (Unicode range → glyph-id maps),
optional kerning tables, and a `bpp` field (1/2/4/8 bits per pixel — the value stored per pixel is an
_opacity/anti-aliasing level_, not a color; the label's actual color comes from the style).
`bitmap_format` is `LV_FONT_FMT_TXT_PLAIN` (0, uncompressed, each glyph's bitmap decoded straight
from the array) or `LV_FONT_FMT_TXT_COMPRESSED` (1, RLE-compressed, decoded on demand).

**Why.** Higher bpp gives smoother anti-aliased edges at the cost of near-linear size growth — LVGL's
own docs state bpp=4 makes a font "nearly four times larger" than bpp=1. Compression trades ~30%
slower rendering (LVGL docs, confirmed by source: see Finding 5) for smaller flash.

**Symptom if violated.** Choosing bpp=8 "for quality" on a 16-color-shade font blows flash budget for
imperceptible visual gain at 320×240; choosing bpp=1 on body text looks visibly jagged/pixelated.

**Fix / current state.** This repo's only compiled font, `lv_font_montserrat_28.c`, is generated with
`--bpp 4 --no-compress` (verified from its embedded generator-command comment at the top of the
file). bpp=4 (16 shades) is the correct default choice for a 16-bit-color SPI panel — enough
anti-aliasing quality, moderate size. Keep bpp=4 for any custom font added to this project unless a
specific size is flash-constrained.

**Source.** `lib/lvgl/src/font/lv_font.h`, `lib/lvgl/src/font/lv_font_fmt_txt.h`,
`lib/lvgl/src/font/lv_font_montserrat_28.c:1-4` (generator comment),
https://lvgl.io/docs/open/8.3/overview/font (redirect target of docs.lvgl.io/8.3/overview/font.html).

### 2. `LV_FONT_FMT_TXT_LARGE`

**Claim.** With `LV_FONT_FMT_TXT_LARGE == 0` (this project's setting, `lv_conf.h:407`), each
`lv_font_fmt_txt_glyph_dsc_t` entry packs into **8 bytes**: `bitmap_index` (20 bits, max 1 MB of
bitmap data addressable) + `adv_w` (12 bits, 8.4 fixed-point) in a `uint32_t`, plus `box_w`, `box_h`
(each `uint8_t`, max glyph 255×255 px) and `ofs_x`, `ofs_y` (each `int8_t`). With
`LV_FONT_FMT_TXT_LARGE == 1`, the same struct switches to full 32/16/16-bit fields — **16 bytes per
glyph**, i.e. double the glyph-descriptor table size, and it lifts the 1 MB bitmap-index and 255 px
glyph-size ceilings.

**Why.** `LARGE` only exists to support fonts whose _compiled bitmap blob_ exceeds 1 MB (e.g. huge
CJK fonts) or whose individual glyphs exceed 255×255 px. Montserrat-28's bitmap is ~18 KB — nowhere
near the ceiling.

**Symptom if violated.** Turning `LV_FONT_FMT_TXT_LARGE` on for a small Latin font doubles every
glyph-dsc table for zero benefit (e.g. Montserrat-28's 158-glyph table would go from ~1.3 KB to
~2.5 KB).

**Fix.** Leave `LV_FONT_FMT_TXT_LARGE 0`. Only set it to `1` if a custom font's `lv_font_conv` output
warns about exceeding the 1 MB bitmap-index range (it will refuse/warn at conversion time).

**Source.** `lib/lvgl/src/font/lv_font_fmt_txt.h:28-42`, `src/t-deck/lv_conf.h:407`.

### 3. Real flash cost per built-in Montserrat size (measured from the vendored `.c` sources)

**Claim.** Counted directly from the glyph-bitmap arrays actually compiled in
`lib/lvgl/src/font/lv_font_montserrat_*.c` (default character range: ASCII 0x20–0x7F + degree +
bullet + the standard ~61-symbol FontAwesome subset, bpp=4, uncompressed):

| Size                 | glyph_bitmap bytes | glyph count | glyph_dsc table (8B × count) | approx total flash |
| -------------------- | ------------------ | ----------- | ---------------------------- | ------------------ |
| 8                    | 1,995              | ~158        | ~1.26 KB                     | ~3.5 KB            |
| 12                   | 4,169              | ~158        | ~1.26 KB                     | ~5.9 KB            |
| 16                   | 6,635              | ~158        | ~1.26 KB                     | ~8.5 KB            |
| 20                   | 9,886              | ~158        | ~1.26 KB                     | ~12 KB             |
| 24                   | 13,847             | ~158        | ~1.26 KB                     | ~16 KB             |
| **28 (compiled in)** | **18,443**         | **158**     | **~1.26 KB**                 | **~21 KB**         |
| 32                   | 22,726             | ~158        | ~1.26 KB                     | ~25 KB             |
| 40                   | 36,019             | ~158        | ~1.26 KB                     | ~39 KB             |
| 48                   | 49,523             | ~158        | ~1.26 KB                     | ~53 KB             |

("approx total flash" adds bitmap + glyph_dsc + a few hundred bytes of cmap/kern tables; kern tables
for Montserrat are non-trivial — Montserrat-28's `kern_left_class_mapping`/`kern_right_class_mapping`/
`kern_class_values` arrays add roughly 1–2 KB on top, not broken out per-size here.) Roughly: **cost
scales close to linearly with point size** for the same character range (each size is generated
independently from the TTF outline, not derived from another bitmap size).

**Why it matters for range.** These numbers are for the _default_ ASCII+symbol range only. Extending
a font's range costs roughly proportional to glyph count × average glyph area:

- Adding full Latin-1 Supplement (umlauts, accented Western-European letters, ~96 codepoints
  0xA0–0xFF) to a 28px font: order of another ~60–70 extra glyphs (many of 0xA0-0xFF are unused
  control/punctuation, so realistically ~40-50 visible extra letters incl. Ä/Ö/Ü/ä/ö/ü/ß) → roughly
  another **+25–35%** on top of the ASCII-only bitmap size, i.e. a few more KB for Montserrat-28.
- Adding Cyrillic (another ~64 glyphs) is a similar order of magnitude to Latin-1.
- Adding CJK is categorically different: thousands of glyphs, each much taller/wider than Latin —
  LVGL ships `LV_FONT_SIMSUN_16_CJK` (1000 most-common CJK radicals only, at a _fixed_ 16px) as a
  demonstration of how much even a _reduced_ CJK set costs; a full CJK font is tens of MB and is
  never compiled in as a single monolithic `lv_font_t` in real projects — it's loaded as a runtime
  binary font from external storage instead. **Not relevant for this project** (no CJK requirement)
  but included for completeness per the brief.

**Fix / recommendation for this device.** 16 MB flash makes 21–30 KB per font size a non-issue —
budget is not the constraint here. The actual right lever is _not_ to enable more built-in sizes
speculatively, but to regenerate the _one or two_ sizes actually used with the _exact_ character
range the UI needs (ASCII + German Latin-1 letters + only the symbols actually referenced by
`LV_SYMBOL_*` in the codebase), via `lv_font_conv` (Finding 6). This keeps both flash _and_, more
importantly, the RLE/opacity-table CPU work bounded to glyphs that exist.

**Source.** Byte counts computed directly from `lib/lvgl/src/font/lv_font_montserrat_*.c` in this
repo (`awk`/`grep` counts of the `glyph_bitmap[]` array contents — this is exactly what the linker
places in `.rodata`/flash, not a doc estimate). Struct sizes from `lv_font_fmt_txt.h:28-42`. CJK
comment: `lv_conf.h:390` (`LV_FONT_SIMSUN_16_CJK`).

### 4. Built-in (compile-time) fonts cost zero RAM; runtime-loaded fonts cost heap — and runtime loading is the wrong choice here

**Claim.** A built-in font like `lv_font_montserrat_28` is a `static`/`const` C array compiled into
`.rodata`/flash. `LV_ATTRIBUTE_LARGE_CONST` and `LV_ATTRIBUTE_FAST_MEM` are both defined **empty** in
this project's `lv_conf.h` (lines 340, 346) — i.e. no special linker-section placement is requested,
so the glyph data sits in ordinary flash, memory-mapped through the ESP32's flash cache like any other
`const` data. It costs **0 bytes of PSRAM and 0 bytes of internal SRAM**, ever.

The alternative — `lv_font_load(const char * fontName)` / `lv_font_free()` (LVGL 8.3 API, declared in
`lib/lvgl/src/font/lv_font_loader.h`) — loads a `.bin` font (produced by `lv_font_conv --format bin`)
at runtime **from a filesystem path through LVGL's `lv_fs` abstraction**, and allocates the resulting
`lv_font_t`, its glyph-dsc/cmap/kern tables, and (for compressed fonts) a decompression buffer, all
on the **heap** — which in this project is `ps_malloc`-backed PSRAM (`LV_MEM_CUSTOM_ALLOC ps_malloc`,
`lv_conf.h`).

**Why runtime loading is the wrong choice for this device specifically.** Three independent reasons,
all sourced from this repo's actual config:

1. **No filesystem driver is wired to LVGL at all.** Every `LV_USE_FS_*` option
   (`LV_USE_FS_STDIO`, `LV_USE_FS_POSIX`, `LV_USE_FS_WIN32`, `LV_USE_FS_FATFS`) is `0` in
   `lv_conf.h`. `lv_font_load()` would need one of these (or a custom `lv_fs_drv_t`) registered
   before it could open anything — this is not "flip a flag", it's new integration work.
2. **The SD card shares the SPI bus with the TFT** behind a single semaphore (`xSemaphore`, per
   shared context). Loading a font from SD at any point after boot — e.g. a user-triggered language
   switch — would contend with `disp_flush()` for that same semaphore, adding latency/jank exactly
   where the project is already fighting redraw-latency bugs.
3. It moves font cost from "free" flash into PSRAM heap that LVGL's object tree already lives in,
   for no benefit — with 16 MB of flash available, there is no capacity pressure that runtime loading
   would relieve.

**Right choice for this device.** Compile every font variant needed (including any German-Latin-1
extended font) in at build time as a `LV_FONT_DECLARE`d `const` `.c` file, exactly like the built-in
Montserrat fonts. Reserve `lv_font_load`/binary fonts only for a hypothetical future feature like
user-installable/downloadable fonts — not needed today.

**Symptom if violated.** If someone adds `lv_font_load()` from SD without wiring an `LV_USE_FS_*`
driver first, it will fail to open the file at the `lv_fs` layer and return `NULL` — silently
falling back to whatever font was already set (or crashing on a null-font dereference if the caller
doesn't check).

**Source.** `lib/lvgl/src/font/lv_font_loader.h`, `src/t-deck/lv_conf.h:340,346,407,612-637`,
shared context (`00-CONTEXT.md`) re: SD/TFT SPI semaphore. Web: search results on `lv_font_load` /
`lv_binfont_create` (the latter is the LVGL **9.x** rename — 8.3 uses `lv_font_load`, confirmed by
reading this repo's own vendored header, not by trusting the web results, which mixed 8.x/9.x
terminology).

### 5. Sizing for a 320×240 2.8" panel, and `LV_DPI_DEF`/`LV_DPX()` interaction

**Claim.** `LV_DPI_DEF` is `130` in this project (`lv_conf.h:99`). `lv_disp_get_dpi(disp)` returns
`disp->driver->dpi`, and `lv_disp_drv_init()` defaults `driver->dpi = LV_DPI_DEF` — this project does
not appear to override `disp_drv.dpi` in `tdeck_main.cpp`, so the display effectively reports 130 DPI
to LVGL's sizing logic regardless of the ST7789's true physical DPI (~140 DPI at 2.8"/320×240 — close
enough that the default is a reasonable approximation and not worth overriding). `LV_DPX(n)` computes
`n * dpi / 160` (`_LV_DPX_CALC`, `lib/lvgl/src/hal/lv_hal.h:30`), i.e. 130 DPI scales all `LV_DPX()`-
derived sizes down slightly from the 160-DPI reference (`LV_DPX(10)` ≈ 8 px). This is used directly by
LVGL core even with **no theme active**: `LV_OBJ_DEF_WIDTH`/`LV_OBJ_DEF_HEIGHT` (`lv_obj.c:49-50`) and
`SCROLLBAR_MIN_SIZE` (`lv_obj_scroll.c:21`) are both `LV_DPX(...)`. Note: `LV_USE_THEME_DEFAULT` is
`0` in this project's `lv_conf.h:576` — the widget default-theme padding logic (`PAD_DEF` etc., which
also uses `LV_DPX`/`lv_disp_dpx` and buckets displays into `DISP_SMALL`/`MEDIUM`/`LARGE` by
`LV_HOR_RES`) is **not compiled in and not applied**; only the theme-independent `LV_DPX` uses above
(object default size, scrollbar) actually run in this codebase. All widget padding in this UI comes
from explicit `lv_obj_set_style_pad_*` calls in `lv_obj_functions.cpp`, not from a theme.

**Sizing guidance for this 320×240 panel.** At 130 DPI a 28 px glyph cap-height is roughly 5.2 mm —
comfortably readable at arm's length, appropriate for a primary message/body font. A readable
_minimum_ on this class of panel is generally accepted as ~14–16 px (below that, 4bpp anti-aliasing on
a 320-wide panel starts to look mushy and letters lose distinguishability at normal viewing distance);
go no smaller than 14px for anything a user must read, and reserve anything below that (10/12px) for
disposable metadata only if screen real estate is desperately tight. Recommended concrete set for this
device:

- **28px (already compiled in)** — message bodies, primary content, dialog text.
- **16px** — secondary chrome: header clock/battery/GPS labels, message timestamps, tab labels,
  list metadata. (16px keeps a full status row legible in the ~24-32px header strip typical for a
  240px-tall landscape screen while leaving most of the vertical budget to content.)
- Optionally **20px** as a middle size for button labels / settings-form labels if 28px is visually
  too large in a compact settings form and 16px too small for primary interactive controls — but
  only add this third size if 16/28 genuinely don't cover the UI; each size is another
  `LV_FONT_DECLARE` + another `~10-15KB` and another thing to keep visually consistent.
- **Do not go to 4+ sizes.** Every additional size increases the chance of visually inconsistent
  screens (a maintenance/consistency cost, not just a flash cost) and gives diminishing returns once
  you have "large" and "small" covered.

**Symptom if violated.** Using `LV_FONT_DEFAULT` (`&lv_font_montserrat_28`, `lv_conf.h:402`) for
every label including small metadata makes timestamps/status text visually dominate over content;
going below ~12px on this panel makes text illegible at bpp=4 4-bit anti-aliasing resolution.

**Source.** `lib/lvgl/src/hal/lv_hal.h:30`, `lib/lvgl/src/hal/lv_hal_disp.c:96,508-512`,
`lib/lvgl/src/core/lv_obj.c:49-50,77-78`, `lib/lvgl/src/core/lv_obj_scroll.c:21`,
`src/t-deck/lv_conf.h:99,402,576`, `lib/lvgl/src/extra/themes/default/lv_theme_default.c:36,661-663`
(confirms theme code exists but is not compiled in here).

### 6. Custom font workflow: `lv_font_conv`, exact CLI, `LV_FONT_DECLARE`

**Claim.** LVGL's official offline converter is the `lv_font_conv` npm package
(https://github.com/lvgl/lv_font_conv), the same tool that generated every vendored Montserrat `.c`
file in this repo (confirmed — see the generator-command comment at the top of
`lib/lvgl/src/font/lv_font_montserrat_28.c`, reproduced in Finding 9). The equivalent online tool is
https://lvgl.io/tools/fontconverter.

**Exact invocation used by this repo's own Montserrat-28** (from the file header, so this is a
_confirmed-working_ command against this LVGL version, not a guess):

```
lv_font_conv --font Montserrat-Medium.ttf -r 0x20-0x7F,0xB0,0x2022 \
  --font FontAwesome5-Solid+Brands+Regular.woff \
  -r 61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650 \
  --size 28 --format lvgl --bpp 4 --no-compress --no-prefilter \
  -o lv_font_montserrat_28.c --force-fast-kern-format
```

(That symbol range is the standard LVGL FontAwesome-symbol codepoint set — same numeric list as the
`LV_SYMBOL_*` macros in `lib/lvgl/src/font/lv_symbol_def.h`, cross-checked and confirmed matching.)

**Copy-pasteable example for adding a German-safe range to a NEW custom size (e.g. a 16px UI font
covering ASCII + German umlauts + only the symbols this project actually uses):**

```
npx lv_font_conv --font Montserrat-Medium.ttf \
  -r 0x20-0x7F,0xA7,0xB0,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0x2022 \
  --font FontAwesome5-Solid+Brands+Regular.woff \
  -r 61441,61452,61453,61461,61478,61479,61480,61502,61931,62212 \
  --size 16 --format lvgl --bpp 4 --no-compress \
  -o lv_font_montserrat_16_de.c
```

(`0xC4,0xD6,0xDC` = Ä Ö Ü; `0xE4,0xF6,0xFC` = ä ö ü; `0xDF` = ß; `0xA7` = § which German legal/UI
text sometimes needs; adjust the FontAwesome codepoint list to only the `LV_SYMBOL_*` values this
project's `.cpp` files actually reference, found via `grep -o 'LV_SYMBOL_[A-Z_]*' src/t-deck/*.cpp
| sort -u` and cross-referencing `lv_symbol_def.h`.)

**`--no-compress` and why it matters.** Compressed fonts (`LV_FONT_FMT_TXT_COMPRESSED`) are
RLE-decoded per glyph at draw time (see Finding 8) — LVGL's own docs state compressed rendering is
"about 30% slower". Since this project already has a blocking, non-DMA SPI flush and CPU-bound
software rasterization (`LV_DRAW_COMPLEX 1`, all GPU paths off, per shared context), adding decode
CPU work on top is the wrong trade for a device with 16 MB of free flash. **Always pass
`--no-compress`** for new fonts on this project, matching what the vendored Montserrat-28 already
does.

**Registering the result.**

```c
// custom_fonts.h
LV_FONT_DECLARE(lv_font_montserrat_16_de)

// lv_conf.h
#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(lv_font_montserrat_16_de)

// usage
extern const lv_font_t lv_font_montserrat_16_de;
lv_obj_set_style_text_font(label, &lv_font_montserrat_16_de, LV_PART_MAIN);
```

`LV_FONT_DECLARE(name)` simply expands to `extern const lv_font_t name;` — it's a convenience macro
for header declarations, functionally identical to writing the `extern` by hand.

**Source.** `lib/lvgl/src/font/lv_font_montserrat_28.c:1-4` (in-repo, ground truth for exact working
CLI syntax against this exact LVGL build), `lib/lvgl/src/font/lv_symbol_def.h` (codepoint
cross-check), https://github.com/lvgl/lv_font_conv (README, fetched), https://lvgl.io/tools/fontconverter,
https://lvgl.io/docs/open/8.3/overview/font.

### 7. Rendering cost breakdown: glyph lookup vs. blending vs. flush — and where the real bottleneck is on this device

**Claim.** Three distinct costs happen per drawn glyph:

1. **Glyph lookup** (`lv_font_get_glyph_dsc` → cmap search): for Montserrat-28's format
   (`LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY` for the dense ASCII range, `..._SPARSE_TINY` for the symbol
   set), this is an O(1) array-index or small sparse-array binary search — negligible cost per
   character, regardless of how many characters are on screen.
2. **Glyph blending** (`lv_draw_sw_letter.c: draw_letter_normal`): for an **uncompressed** font this
   is a straight per-pixel loop over the glyph's `box_w × box_h` pixels, using a precomputed 16-entry
   (bpp=4) opacity lookup table to blend into the framebuffer — cost scales with _glyph pixel area ×
   number of visible characters_, entirely on the ESP32-S3 CPU (no GPU path is compiled in;
   `LV_DRAW_COMPLEX 1` with all GPU backends off, per shared context).
3. **The flush** (`disp_flush()` in `tdeck_main.cpp`): `tft.pushColors(..., w*h, false)` is a
   **blocking, non-DMA** SPI write of the entire invalidated area, serialized behind the TFT/SD
   `xSemaphore`.

On this hardware, **(3) dominates for anything beyond a handful of characters**, and it dominates by
_invalidated-area size_, not glyph count — this is why `lv_obj_invalidate()` scope matters more than
which font/bpp is used. **(2) is real but secondary**: compressed fonts add a 4th, extra cost — RLE
decompression of the glyph bitmap into a **shared, `lv_mem_realloc`'d** decode buffer
(`LV_GC_ROOT(_lv_font_decompr_buf)`, `lib/lvgl/src/font/lv_font_fmt_txt.c:102-127`) on **every single
draw call for every glyph**, not once per unique character — i.e. re-decoded from scratch each time a
label is redrawn, even if the same glyph was just decoded a moment ago for the previous character.
In this project that realloc lands in **PSRAM** (`LV_MEM_CUSTOM_ALLOC ps_malloc`), and PSRAM
malloc/realloc is measurably slower than internal-SRAM heap ops on ESP32-S3. This is an additional,
concrete reason (beyond LVGL's own "~30% slower" figure) not to use `LV_FONT_MONTSERRAT_28_COMPRESSED`
or any `--compress`ed custom font on this board.

**Why `lv_label_set_text` invalidates more than "just the text area".** `lv_label_set_text()` calls
`lv_obj_invalidate(obj)` unconditionally at entry (`lv_label.c:90`) — the _whole label object's_
current coordinates, before the new text/size is even known — and the label's `LV_EVENT_REFR_EXT_DRAW_SIZE`
handler additionally extends the invalidated/draw area by `font_h / 4` px beyond the label's box
(`lv_label.c:757-762`) to accommodate italic/overhanging glyphs. After the new text is set,
`lv_label_refr_text()` triggers `lv_obj_refresh_self_size()`, which — if the label's size actually
changed (e.g. `LV_SIZE_CONTENT` width/height, used by several labels in this project's message
bubbles, e.g. `lv_obj_functions.cpp:2926`) — invalidates _again_ at the new size, and can also
propagate a layout recompute up through parent flex containers (the message bubble's flex rows).

**Symptom if violated / cost quantification for `LV_LABEL_LONG_SCROLL`.** Not currently used in this
codebase (verified: only `CLIP`/`WRAP` appear in `lv_obj_functions.cpp`), but if ever added: the
scroll animation's `set_ofs_x_anim`/`set_ofs_y_anim` callbacks call `lv_obj_invalidate(obj)` on
**every single animation timer tick** (`lv_label.c:1259-1270`), and that timer runs at
`LV_DISP_DEF_REFR_PERIOD` (this project's `_lv_anim_core_init` uses that same 10 ms period,
`lib/lvgl/src/misc/lv_anim.c:60`) — i.e. up to **100 invalidate+redraw+flush cycles per second,
forever, for the lifetime of that single label**, each one going through the same blocking non-DMA
SPI flush described above. On a device already fighting "full-screen repaints are slow, UI feels
laggy" (shared-context symptom #3) and where "playing audio blocks the whole device" is a known issue
competing for the same CPU, a single scrolling label would be a permanent, silent battery/CPU/latency
drain that never stops on its own (`LV_ANIM_REPEAT_INFINITE`). **Do not introduce `LONG_SCROLL` /
`LONG_SCROLL_CIRCULAR` on this project.**

**`LV_LABEL_LONG_DOT` / `CLIP` / `WRAP` cost comparison.**

- `CLIP` (used for header time/battery/sat/locator labels, `lv_obj_functions.cpp:548,555,567,591`,
  and message timestamps) is the cheapest: no animation, no dot manipulation, `lv_draw_label` is
  simply clipped to the label's content area — no ongoing cost after the one-time draw.
- `WRAP` (used for message headers/bodies and the "no data" map label) is a one-time text-layout cost
  proportional to text length at `set_text` time (line-break search), then static — no ongoing
  animation cost, comparable to `CLIP` after layout.
- `DOT` is not used in this codebase, but note it **mutates the label's text buffer in place** to
  splice in the "…" — if ever used with `lv_label_set_text_static()` on a buffer in true read-only
  flash/ROM, this would corrupt memory or fault; only use `DOT` with a writable (dynamically
  allocated, i.e. plain `lv_label_set_text()`) buffer.

**Source.** `lib/lvgl/src/draw/sw/lv_draw_sw_letter.c:85-230`, `lib/lvgl/src/font/lv_font_fmt_txt.c:41,102-127`,
`lib/lvgl/src/widgets/lv_label.c:85-230,750-1101,1259-1270`, `lib/lvgl/src/misc/lv_anim.c:57-60`,
`src/t-deck/lv_conf.h` (`LV_MEM_CUSTOM_ALLOC`, `LV_DISP_DEF_REFR_PERIOD`, `LV_DRAW_COMPLEX`),
`src/t-deck/lv_obj_functions.cpp:548,555,567,591,1483,2925,2989,3009` (grep confirming current
long-mode usage), LVGL docs (label page, ~30%-slower compressed-font figure).

### 8. `lv_label_set_text` vs `_static` vs `_fmt` — allocation/invalidation and the message-list pattern

**Claim.** All three functions call `lv_obj_invalidate()`/trigger `lv_label_refr_text()`
unconditionally — **none of them, in LVGL 8.3, check whether the new text content differs from the
old text before doing the invalidate + realloc/layout work.** Confirmed directly in
`lib/lvgl/src/widgets/lv_label.c`:

- `lv_label_set_text(obj, text)` (line 85): the only "skip" case is when the caller passes the
  label's _own current pointer_ back in (`label->text == text`), which is a self-refresh path (used
  when style/font changes and you want to re-flow the same string) — it still reallocs and still
  invalidates. Passing a _different pointer to identical content_ does full work: free old buffer,
  `strlen` + `lv_mem_alloc` + `strcpy` the new one, invalidate, re-layout.
- `lv_label_set_text_static(obj, text)` (line 175): stores the `const char*` pointer directly, no
  copy, no free/alloc — cheapest possible update, but the caller must guarantee `text` outlives the
  label (string literal or a buffer with equal-or-longer lifetime than the label).
- `lv_label_set_text_fmt(obj, fmt, ...)` (line 147): always calls `_lv_txt_set_text_vfmt` (an
  internal `vsnprintf`-style allocator) — always heap-allocates a fresh buffer, every call, same cost
  class as `lv_label_set_text` plus the `printf`-formatting overhead itself. (Note: this project's
  `Print::printf`/heap-per-line issue is independently documented in the user's memory —
  `printf-malloc-starves-nimble.md` — the same class of cost applies here on the LVGL side.)

**Correct pattern for a message list / any frequently-updated label.** Since LVGL does not
content-diff for you, the caller must:

```c
static char last_batt_text[16] = "";
void update_battery_label(lv_obj_t *label, int pct) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    if (strcmp(buf, last_batt_text) != 0) {
        strcpy(last_batt_text, buf);
        lv_label_set_text(label, buf);   // only invalidate+realloc when it actually changed
    }
}
```

For labels whose text is a fixed string set once (button captions, static headers), prefer
`lv_label_set_text_static()` with a `static const char*` or string literal — zero allocation ever.

For the **message list specifically**: the current implementation (`msg_list_append_bubble`,
Finding 6/TL;DR item 6) does not call `set_text` repeatedly on a reused label at all — every message
creates brand-new label widgets. That is a heavier-weight but _simpler-to-reason-about_ pattern than
label reuse + diffing, and is already bounded by `MSG_TAB_MAX_MESSAGES = 50` per tab
(`lv_obj_functions.cpp:182`, trimmed via `msg_tabs_trim_history`, `:2513`). **Do not "optimize" this
into a label-recycling/virtual-list pattern unless profiling actually shows the per-tab-switch
50-bubble rebuild (Finding 6) is a measured bottleneck** — that would be a much larger, riskier change
than anything else in this track, and is out of scope for a font/text-focused fix.

**Source.** `lib/lvgl/src/widgets/lv_label.c:85-191`, `src/t-deck/lv_obj_functions.cpp:182,2513,2842-3015`
(grep + read), LVGL 8.3 label docs (confirms the same 3-function distinction from the doc side).

### 9. Text areas (`lv_textarea`): append vs. re-set, cursor blink, growth

**Claim.** `lv_textarea_add_text()`/`lv_textarea_add_char()` insert into the existing label buffer via
`lv_label_ins_text()` (`lib/lvgl/src/widgets/lv_textarea.c:139,190`) — an incremental in-place
insertion, cheaper than `lv_textarea_set_text()` which discards and rebuilds the whole text. The
cursor blinks via an `lv_anim` (`cursor_blink_anim_cb`, `lv_textarea.c:932`, default
`LV_TEXTAREA_DEF_CURSOR_BLINK_TIME = 400 ms`), started by `start_cursor_blink()` on focus/edit
(`:391,837,874,921`) — same infinite-animation-invalidate mechanism as label scrolling (Finding 7),
but scoped correctly (only while a textarea has focus/cursor visible, not always-on), and at 400 ms
period rather than 10 ms — a non-issue.

**Growth problem.** `lv_textarea` has no automatic trimming; `lv_textarea_set_max_length()`
(`:501`) exists but is opt-in and, per its own logic (`:1013`), only **blocks further typed input**
once the limit is hit — it does not trim from the front. A textarea used as an ever-growing log
would grow its backing label buffer without bound; the label's `lv_txt_get_size()` re-layout cost on
every append then also grows with total accumulated text length, not just the newly appended part
(the whole string is re-measured for wrapping on every `refr_text`).

**Current repo usage.** Verified via grep: every `lv_textarea_*` call in this codebase
(`event_functions.cpp`, `lv_obj_functions.cpp`) is a **settings-form input field** (`setup_callsign`,
`setup_name`, `setup_comment`, `setup_lat`, `setup_lon`, `setup_wifissid`, etc.), always cleared with
`lv_textarea_set_text(x, "")`, never appended to as a log. **The message list is not a textarea** —
it's the label/flex-bubble pattern in Finding 8/TL;DR item 6. There is currently no
ever-growing-textarea bug in this codebase; this finding is preventive guidance in case a debug/log
textarea is added later.

**Fix if a log-style textarea is ever added.** Cap it explicitly (track line count / char count
yourself, since `set_max_length` won't trim), and periodically replace the whole buffer with a
truncated tail via `lv_textarea_set_text()` rather than letting `add_text()` grow unbounded.

**Source.** `lib/lvgl/src/widgets/lv_textarea.c:28-29,100-190,268-297,501-507,932,1013,1029-1051`,
grep of `src/t-deck/event_functions.cpp` and `lv_obj_functions.cpp` for all `lv_textarea_*` call
sites.

### 10. UTF-8, German umlauts, and BiDi/Arabic settings

**Claim.** `LV_TXT_ENC` is `LV_TXT_ENC_UTF8` (`lv_conf.h:432`) — correct and required, since German
strings containing ä/ö/ü/ß are multi-byte UTF-8 sequences and LVGL must decode them to Unicode
codepoints before doing a font cmap lookup. **This decoding step works correctly today** — the
problem (TL;DR item 2) is downstream of decoding: the _font_ Montserrat-28 as currently compiled
simply has no glyph for U+00E4 (ä), U+00F6 (ö), U+00FC (ü), U+00DF (ß), U+00C4 (Ä), U+00D6 (Ö),
U+00DC (Ü) — confirmed by reading `lv_font_montserrat_28.c`'s cmap tables directly: the only cmap
entries are `range 32..126` (ASCII) and a sparse list of ~63 codepoints (degree sign, bullet, and
FontAwesome symbols in the 0xF0xx-0xF7xx range) — no Latin-1 Supplement block at all.

**What happens on a missing glyph, exactly** (traced through source, `lib/lvgl/src/font/lv_font.c:71-125`
→ `lib/lvgl/src/draw/sw/lv_draw_sw_letter.c:95-119`): `lv_font_get_glyph_dsc()` fails to find the
codepoint in any cmap, falls through the (empty, in this project) fallback-font chain, and — because
`LV_USE_FONT_PLACEHOLDER 1` (`lv_conf.h:420`) — synthesizes a placeholder descriptor
(`box_w = line_height/2`, `box_h = line_height`) and returns `false`. The draw code then draws **a
1px-wide hollow rectangle outline** in the label's text color (`lv_draw_rect_dsc` with
`bg_opa`/`outline_opa`/`shadow_opa` all `LV_OPA_MIN`, `border_width = 1`) at that position — i.e. a
visible "tofu box" per missing character, advancing the cursor by half the line height. It also calls
`LV_LOG_WARN(...)`, but `LV_USE_LOG` is `0` (`lv_conf.h:233`) in this build, so **this happens with
zero diagnostic output** — a build with logging enabled would at least print `glyph dsc. not found
for U+00E4` to help debug it; as configured today it fails completely silently on device.

**Fix.** Regenerate Montserrat (or whichever custom font) with an extended range including at minimum
`0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC` (Finding 6 gives the exact CLI). This is very likely an actual
live bug in this codebase given "the repo has German strings" (shared context) and PR descriptions
must be written in German (project CLAUDE.md) — any German UI string containing an umlaut will show
boxes today.

**BiDi/Arabic — should stay off.** `LV_USE_BIDI 0` and `LV_USE_ARABIC_PERSIAN_CHARS 0`
(`lv_conf.h:455,466`) are both correctly disabled: German/Latin text is left-to-right with no
contextual letter shaping, so both would add per-`set_text` CPU cost (BiDi run computation,
Arabic-Persian contextual glyph substitution) for zero benefit. Leave both off.

**Source.** `lib/lvgl/src/font/lv_font.c:71-125`, `lib/lvgl/src/draw/sw/lv_draw_sw_letter.c:95-119`,
`lib/lvgl/src/font/lv_font_montserrat_28.c` (cmap section, read directly), `src/t-deck/lv_conf.h:233,420,432,455,466`.

### 11. Symbols (`LV_SYMBOL_*`) and how they're merged into Montserrat

**Claim.** `LV_SYMBOL_*` macros (`lib/lvgl/src/font/lv_symbol_def.h`) are UTF-8-encoded string
literals for codepoints in the FontAwesome Private Use Area (0xF000-0xF2FF range, plus
`LV_SYMBOL_BULLET` at 0x2022). They are **not a separate font** at runtime — LVGL's official
Montserrat `.c` files are generated by merging two font sources in one `lv_font_conv` invocation: the
Montserrat TTF for the Latin range, and `FontAwesome5-Solid+Brands+Regular.woff` for a specific,
hand-picked codepoint list (Finding 6), all baked into the _same_ `lv_font_t`/cmap/bitmap-array. A
label just needs `lv_label_set_text(label, LV_SYMBOL_TRASH)` (used at
`lv_obj_functions.cpp:2952` for the message-delete button) with a font that has that codepoint merged
in — no special widget or image needed.

**Availability depends on which Montserrat size + which of its cmap ranges were generated with.**
This repo's Montserrat-28 was verified (by cross-referencing the embedded generator-command decimal
codepoint list against `lv_symbol_def.h`'s hex codepoints) to include the _standard_ ~61-symbol
FontAwesome set that LVGL bundles with every enabled Montserrat size by default — so
`LV_SYMBOL_TRASH`, `LV_SYMBOL_OK`, `LV_SYMBOL_CLOSE`, `LV_SYMBOL_SETTINGS`, `LV_SYMBOL_WIFI`,
`LV_SYMBOL_GPS`, `LV_SYMBOL_BATTERY_FULL`, etc. should all render correctly at 28px in this project.
**If this project ever enables a different Montserrat size that does not carry the same symbol set**
(e.g. `LV_FONT_MONTSERRAT_8`, which is small enough that some builds ship it Latin-only), or points a
style at a **custom-converted** font that wasn't generated with the FontAwesome merge, any
`LV_SYMBOL_*` used with that font/size falls into the exact same missing-glyph "hollow box" failure
mode as Finding 10 — verify with `grep -o 'LV_SYMBOL_[A-Z_]*' src/t-deck/*.cpp | sort -u` against
whatever codepoint list was passed to `lv_font_conv` for any new custom font.

**Adding custom icon glyphs (handoff to Track 4).** The same merge mechanism used for FontAwesome
symbols is the correct way to add _new_ custom icons (e.g. project-specific status icons) as text
glyphs rather than bitmap images: convert the icon set (e.g. an SVG-derived icon font, or a curated
subset of Material Symbols) with `lv_font_conv --font your-icons.ttf -r <codepoints> --symbols`
merged alongside the Latin font, exactly like Montserrat+FontAwesome. This keeps icons cheap
(sub-KB per glyph, part of the existing font-blend draw path, no separate image-decode step) versus
bitmap/PNG icons, which is **Track 4's territory** (image handling) — this track only confirms the
font-merge mechanism exists and is the right default choice for icon-style glyphs, deferring the
bitmap-vs-font-icon tradeoff writeup to Track 4.

**Source.** `lib/lvgl/src/font/lv_symbol_def.h`, `lib/lvgl/src/font/lv_font_montserrat_28.c:4-4,4633-4661`
(generator comment + cmap, cross-checked against symbol_def.h codepoints),
`src/t-deck/lv_obj_functions.cpp:2952` (repo usage example).

### 12. Failure-symptom checklist

| Symptom                                                      | Likely cause                                                                                                                                                                                                                                                                                                                                                                                                                        | Where to check                                                                                                                                                                                                                                                                                                                                      |
| ------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Text invisible** (label exists, nothing drawn)             | Font's line color == background color; or `LV_OPA_TRANSP` text opacity; or label positioned outside its parent's clip area — **not usually a font issue**                                                                                                                                                                                                                                                                           | Check `lv_obj_set_style_text_color`/`text_opa`; unrelated to fonts per se                                                                                                                                                                                                                                                                           |
| **Hollow boxes / "tofu" instead of a character**             | Character's codepoint is not in the active font's cmap (classic case here: German umlauts in Montserrat-28, Finding 10)                                                                                                                                                                                                                                                                                                             | Cross-check the codepoint against the font's cmap ranges (read the `.c` file's `cmaps[]`/`unicode_list_*` — remember `SPARSE` entries are offsets from `range_start`, not absolute codepoints, see Finding 11's derivation)                                                                                                                         |
| **Garbage / scrambled glyph pixels**                         | Wrong `bpp` assumed by the renderer vs. what the font was compiled with (e.g. manually editing a font's `.bpp` field, or mixing a `--lcd`/subpx font with `LV_USE_FONT_SUBPX 0`)                                                                                                                                                                                                                                                    | `draw_letter_normal` switches on `g->bpp` (`lv_draw_sw_letter.c:193-213`) — a mismatched/corrupted font struct falls into `default: LV_LOG_WARN("invalid bpp")` and simply skips drawing (won't actually show garbage in 8.3 — it silently no-ops instead; "garbage" is more likely a corrupted/truncated glyph_bitmap array from a bad conversion) |
| **Wrong size rendered** (e.g. code asks for 16px, gets 28px) | Requested `LV_FONT_MONTSERRAT_16` etc. is `0`/not compiled in this build (`lv_conf.h:364-384` — only `_28` is `1`); LVGL silently falls back to `LV_FONT_DEFAULT` (`&lv_font_montserrat_28`) rather than erroring                                                                                                                                                                                                                   | Check the specific `LV_FONT_MONTSERRAT_n` macro is `1` in `lv_conf.h` before referencing `&lv_font_montserrat_n` anywhere                                                                                                                                                                                                                           |
| **Text visibly clipped mid-character**                       | `LV_LABEL_LONG_CLIP` on a label sized too small for even one line, or a fixed-height container with `LV_LABEL_LONG_WRAP` and `LV_TXT_LINE_BREAK_LONG_LEN 0` failing to break a long unspaced token (Finding "TL;DR" item 7 — relevant for German compounds)                                                                                                                                                                         | Check the label's long-mode and its parent's fixed height/width                                                                                                                                                                                                                                                                                     |
| **`LV_ASSERT` / crash referencing a font pointer**           | Referencing `&lv_font_montserrat_N` for an `N` whose `LV_FONT_MONTSERRAT_N` macro is `0` — the symbol won't even exist at that name in some configurations, or (if it does resolve via a stale header) points at an all-zero/garbage struct, and `LV_ASSERT_NULL(font_p)` in `lv_font_get_glyph_bitmap`/`get_glyph_dsc` (`lv_font.c:48,64`) fires on a `NULL` font pointer if a style never had its font set and no fallback exists | Grep `lv_conf.h` for the exact `LV_FONT_MONTSERRAT_N 1` before using that font anywhere; ensure every style with `lv_obj_set_style_text_font` gets a real, non-NULL font pointer                                                                                                                                                                    |
| **Symbol characters (LV_SYMBOL\_*) render as boxes**         | Symbol codepoint not merged into the active font at that size/variant (Finding 11)                                                                                                                                                                                                                                                                                                                                                  | Cross-reference the symbol's hex value against the font's cmap, same technique as the umlaut case                                                                                                                                                                                                                                                   |

**Source.** Synthesized from Findings 1-11 above (all individually sourced against this repo's
vendored LVGL 8.3.11 code).

## Rules to hand the coding agent

1. Before shipping any German-language string through a label, verify its font's cmap covers
   Ä/Ö/Ü/ä/ö/ü/ß — Montserrat-28 as currently compiled in this repo does **not** (Finding 10). This
   is a real, verified, currently-live defect if any German UI string contains an umlaut.
2. Regenerate any font needing the German range with `lv_font_conv --bpp 4 --no-compress`, mirroring
   the exact flags already used for `lv_font_montserrat_28.c` (Finding 6). Never enable
   `LV_FONT_MONTSERRAT_28_COMPRESSED` or convert a custom font with compression on this board
   (Finding 7 — PSRAM realloc-per-glyph cost, ~30% slower per LVGL's own figures).
3. Do not add `lv_font_load()`/runtime binary fonts. No `LV_USE_FS_*` driver is wired up, and doing
   so would contend with the TFT for the shared SPI semaphore (Finding 4). Compile every needed font
   variant in at build time via `LV_FONT_DECLARE`.
4. Keep `LV_USE_FONT_SUBPX 0`. It is architecturally pointless on this SPI/TFT_eSPI panel (Finding
   "TL;DR" item 8) and would cost ~3× font flash for zero visible benefit.
5. Never introduce `LV_LABEL_LONG_SCROLL` or `LV_LABEL_LONG_SCROLL_CIRCULAR` on this project — both
   install a permanent 10ms-period invalidate loop (Finding 7) that never stops, compounding this
   device's existing redraw-latency and battery problems. This codebase currently uses only `CLIP`
   and `WRAP` — keep it that way.
6. Add an explicit `strcmp`-before-`lv_label_set_text` guard on any label updated from a periodic
   timer/poll loop (clock, battery %, GPS status, satellite count) — LVGL 8.3 does not skip
   unnecessary invalidation/reallocation for you (Finding 8). Use
   `lv_label_set_text_static()` for constant/never-changing label text instead of
   `lv_label_set_text()`.
7. Do not restructure the message-bubble list into a label-recycling/virtual-list pattern without
   first profiling the actual tab-switch full-rebuild cost (Finding 6/8) — it's already capped at 50
   messages/tab, and the live-append-to-active-tab path is already incremental and cheap.
8. Do not add a growing/unbounded `lv_textarea` for logging without an explicit external cap and
   periodic truncation — `lv_textarea_set_max_length()` only blocks further typed input, it does not
   trim (Finding 9). Not currently a live bug (no such textarea exists today), but a trap if added.
9. Keep `LV_USE_BIDI` and `LV_USE_ARABIC_PERSIAN_CHARS` at `0`. Both are correctly off for
   German/Latin text and would add per-`set_text` CPU cost if enabled.
10. When adding any new font size, generate it with exactly the character range needed (ASCII +
    German Latin-1 + only the `LV_SYMBOL_*` codepoints actually referenced in `.cpp` files) —
    don't blanket-import a huge Unicode range "to be safe"; every extra glyph is extra flash and,
    for compressed fonts (which you should not be using anyway per rule 2), extra draw-time CPU.
11. Prefer 2 font sizes for this UI (28px content, 16px chrome/metadata), 3 at most. Do not enable
    more Montserrat sizes than are actually assigned to a style somewhere in the code.

## Open questions / UNVERIFIED

- Exact flash byte totals per font size including kerning-table overhead were not fully broken out
  per size (only measured precisely for Montserrat-28's glyph_bitmap + glyph_dsc). The bitmap-array
  byte counts in Finding 3's table are exact (counted directly from the vendored source arrays); the
  "approx total flash" column is an estimate that includes an assumed constant ~1-2KB kern-table
  overhead not individually re-measured for every size in the table — treat the "approx total" column
  as UNVERIFIED beyond the 28px row, which was checked in detail.
- The claim that "a full Latin-1 range adds roughly +25-35%" to a font's bitmap size is a reasoned
  estimate (glyph-count ratio × typical accented-letter bitmap area vs. base-Latin average), not a
  measurement against an actually-generated extended font in this repo — mark this figure
  UNVERIFIED and treat it as ballpark only; if precision matters, generate the actual extended font
  and measure it directly with the same `awk`/`grep` technique used in Finding 3.
- Whether `disp_drv.dpi` is left at the `LV_DPI_DEF` default (130) versus overridden somewhere in
  `tdeck_main.cpp` was checked via targeted grep for `disp_drv.dpi` / `.dpi =` and found no override —
  but this was not an exhaustive read of the entire `setupLvgl()` function, so treat "confirmed no
  override" as high-confidence but not 100% exhaustively verified.
- LVGL 9.x behavior notes (subpixel macro rename to `LV_DRAW_SW_FONT_SUBPX`, `lv_binfont_create`
  replacing `lv_font_load`) come from web search summaries of GitHub issues/forum threads, not from
  reading LVGL 9.x source directly (out of scope — this repo is pinned to 8.3.11). Treat the _v9_
  side of those two notes as UNVERIFIED-but-plausible; the _v8.3_ side of both was confirmed directly
  against this repo's vendored source.

## Sources

- `lib/lvgl/src/font/lv_font.h`, `lv_font.c`, `lv_font_fmt_txt.h`, `lv_font_fmt_txt.c`,
  `lv_font_loader.h`, `lv_symbol_def.h` — vendored LVGL 8.3.11 source, read directly in this repo.
- `lib/lvgl/src/font/lv_font_montserrat_*.c` (all sizes) — vendored source, byte-counted directly for
  Finding 3, cmap/generator-comment read directly for Findings 6, 10, 11.
- `lib/lvgl/src/widgets/lv_label.c`, `lv_textarea.c` — vendored source, read in full for the relevant
  sections (Findings 7, 8, 9).
- `lib/lvgl/src/draw/sw/lv_draw_sw_letter.c` — vendored source, read for Findings 7, 10, 12.
- `lib/lvgl/src/misc/lv_anim.c` — vendored source, confirms anim timer period (Finding 7).
- `lib/lvgl/src/hal/lv_hal.h`, `lv_hal_disp.c`, `core/lv_obj.c`, `core/lv_obj_scroll.c`,
  `extra/themes/default/lv_theme_default.c` — vendored source, read for Finding 5 (DPI/DPX).
- `src/t-deck/lv_conf.h` — this project's LVGL config, read/grepped extensively throughout.
- `src/t-deck/tdeck_main.cpp`, `lv_obj_functions.cpp`, `event_functions.cpp` — this project's UI
  code, read/grepped for actual usage patterns (message list, textareas, long-modes).
- https://lvgl.io/docs/open/8.3/overview/font — LVGL 8.3 font docs (fetched; redirect target of
  docs.lvgl.io/8.3/overview/font.html), bpp/compression/subpx/LV_FONT_DECLARE confirmation.
- https://lvgl.io/docs/open/8.3/widgets/core/label — LVGL 8.3 label docs (fetched), confirms
  set_text/set_text_static/set_text_fmt distinction and long-mode descriptions.
- https://github.com/lvgl/lv_font_conv — lv_font_conv README (fetched), CLI flag reference
  (`--font`, `-r`/`--range`, `--symbols`, `--bpp`, `--no-compress`, `--lcd`, `-o`).
- https://lvgl.io/tools/fontconverter — online converter (named per brief, not independently
  fetched — CLI tool covers the same functionality and was verified instead).
- Web search: "LVGL 8.3 LV_USE_FONT_LOADER lv_font_load binary font runtime heap" — used only to
  cross-check terminology (`lv_font_load` vs. 9.x `lv_binfont_create`); the load-bearing 8.3 API
  confirmation came from reading `lv_font_loader.h` directly, not from this search.
- Web search: "LVGL v9 removed subpixel font rendering LV_USE_FONT_SUBPX deprecated" — used only for
  the v8.3-vs-v9 macro-rename side note in the Open Questions section; not load-bearing for any v8.3
  claim.
