#!/usr/bin/env python3
"""Append national-character glyphs to an Adafruit_GFX style font header (src/Fonts/*.h).

Two modes, picked from the font's current 'last' value:
  - original font (last == 0x7E): the ASCII glyphs (0x20-0x7E) are left untouched, an empty
    placeholder glyph is added at 0x7F and the given characters get 0x80, 0x81, ...
  - already extended font (last > 0x7E): the given characters are appended at last+1, last+2, ...
    (no placeholder); all existing glyph and bitmap data stay byte-identical.
The font's 'last' value is raised accordingly (must stay <= 0xFF). The code order MUST match
kGfxCharmap in src/gfx_charmap.h (append the same characters, in the same order, to the string
documented there).

Rendering follows Adafruit's fontconvert (1 bit, 141 dpi, same pt size as the font name).
Needs freetype-py:  uv run --with freetype-py python3 tools/gfxfont_add_glyphs.py ...
TTF: GNU FreeFont (FreeSans.ttf / FreeSansBold.ttf, ftp.gnu.org/gnu/freefont).

  uv run --with freetype-py python3 tools/gfxfont_add_glyphs.py src/Fonts/FreeSans9pt7b.h FreeSans.ttf 9 "<chars>"
"""
import re, sys
import freetype

path, ttf, pt, chars = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
t = open(path, encoding="utf-8").read()

m_font = re.search(r"const GFXfont (\w+) PROGMEM = \{.*?0x20,\s*0x([0-9A-Fa-f]{2}),\s*(\d+)\};", t, re.S)
if not m_font:
    sys.exit("font struct not found")
name = m_font.group(1)
old_last = int(m_font.group(2), 16)
extended = old_last > 0x7E
first_code = old_last + 1

face = freetype.Face(ttf)
face.set_char_size(pt << 6, 0, 141, 0)

bm_re = re.compile(r"(const uint8_t %sBitmaps\[\] PROGMEM = \{.*?)(\};)" % name, re.S)
m_bm = bm_re.search(t)
old_bytes = re.findall(r"0x[0-9A-Fa-f]{2}", m_bm.group(1))
offset = len(old_bytes)

new_bytes = []
glyph_lines = []
acc = []
def flush_bits():
    global acc
    while len(acc) % 8:
        acc.append(0)
    for i in range(0, len(acc), 8):
        v = 0
        for b in acc[i:i + 8]:
            v = (v << 1) | b
        new_bytes.append(v)
    acc = []

if not extended:
    glyph_lines.append((offset + len(new_bytes), 0, 0, 0, 0, 0, "0x7F (unused placeholder)"))
    first_code = 0x80
for i, ch in enumerate(chars):
    face.load_char(ch, freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_RENDER)
    g = face.glyph; b = g.bitmap
    off = offset + len(new_bytes)
    for y in range(b.rows):
        for x in range(b.width):
            acc.append(1 if b.buffer[y * b.pitch + (x >> 3)] & (0x80 >> (x & 7)) else 0)
    flush_bits()
    glyph_lines.append((off, b.width, b.rows, g.advance.x >> 6, g.bitmap_left, 1 - g.bitmap_top,
                        "0x%02X '%s' U+%04X" % (first_code + i, ch, ord(ch))))
last = first_code + len(chars) - 1
if last > 0xFF:
    sys.exit("last code 0x%X exceeds 0xFF" % last)
if offset + len(new_bytes) > 0xFFFF:
    sys.exit("bitmapOffset (uint16) overflow")

# 1) bitmap bytes
rows = []
for i in range(0, len(new_bytes), 12):
    rows.append("    " + ", ".join("0x%02X" % v for v in new_bytes[i:i + 12]))
body = m_bm.group(1).rstrip()
if not body.endswith(","):
    body += ","
t = t[:m_bm.start()] + body + "\n" + ",\n".join(rows) + "\n" + m_bm.group(2) + t[m_bm.end():]

# 2) glyph table: the last entry ends with "}};" (0x7E in the original font, the last added glyph otherwise)
gl_start = re.search(r"const GFXglyph %sGlyphs\[\] PROGMEM = \{" % name, t)
tail = re.compile(r"(\{[^{}]*\})\};([^\n]*)").search(t, gl_start.end() if gl_start else 0)
if not gl_start or not tail:
    sys.exit("glyph table / last glyph entry not found")
add = ""
for k, gl in enumerate(glyph_lines):
    end = "}};" if k == len(glyph_lines) - 1 else "},"
    add += "\n    {%d, %d, %d, %d, %d, %d%s   // %s" % (gl[0], gl[1], gl[2], gl[3], gl[4], gl[5], end, gl[6])
t = t[:tail.start()] + tail.group(1) + "," + tail.group(2) + add + t[tail.end():]

# 3) font struct: last
t = re.sub(r"(0x20,\s*)0x%02X(,\s*%s\};)" % (old_last, m_font.group(3)), r"\g<1>0x%02X\2" % last, t, count=1)
open(path, "w", encoding="utf-8").write(t)
print("%s: +%d glyphs (last=0x%02X), +%d bitmap bytes" % (name, len(glyph_lines), last, len(new_bytes)))
