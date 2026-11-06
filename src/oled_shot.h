/**
 * OLED bench screenshot: layout of the u8g2 full-frame buffer (_F_ constructors, 128x64) and the
 * reference conversion to a row-major image. Pure, no Arduino, host-testable.
 *
 * The firmware does NOT convert: GET /screen.oled serves the u8g2 buffer as it is (no copy, the
 * T-Beam heap is tight) and tools/bench/oled_screenshot.py converts on the host. The functions here
 * are the executable specification of that layout; the Python tool implements the same mapping and
 * its --self-test uses the same known pattern as test/test_oled_shot.
 *
 * u8g2_ll_hvline_vertical_top_lsb (every SSD1306/SH1106 128x64 _F_ buffer): the buffer is 8 pages of
 * 128 bytes. Byte index = (y / 8) * width + x, bit (y % 8) of that byte = pixel (x, y), LSB at the top.
 * 1 = pixel lit.
 *
 * Wire format: X-Screen-Format: oled1-<w>x<h>, body = w * (h / 8) bytes of that buffer.
 */
#ifndef MC_OLEDSHOT_H
#define MC_OLEDSHOT_H

#include <stddef.h>
#include <stdint.h>

#define OLED_SHOT_W 128
#define OLED_SHOT_H 64
#define OLED_SHOT_BYTES ((OLED_SHOT_W) * (OLED_SHOT_H) / 8)   // 1024

/** Pixel (x, y) of a vertical-byte buffer of the given width; 0 outside the 8-row pages. */
static inline uint8_t oledShotPixel(const uint8_t *buf, int width, int x, int y)
{
    return (uint8_t)((buf[(y >> 3) * width + x] >> (y & 7)) & 1u);
}

/** Row-major 8 bpp: out[y * width + x] = 0 (dark) or 255 (lit). out holds width * height bytes. */
static inline void oledShotTo8bpp(const uint8_t *buf, int width, int height, uint8_t *out)
{
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            out[y * width + x] = oledShotPixel(buf, width, x, y) ? 255 : 0;
}

#if !defined(UNIT_TEST) && !defined(NATIVE_BUILD)
// loop_functions.cpp, INSTRUMENT_ENABLED && MC_HAS_U8G2 only (no other TU may call these).
// Status of the frame source: 0 = frame available (buf/len set), 1 = no display (u8g2 NULL),
// 2 = display off or sleeping, 3 = buffer is not a full frame (page mode).
int oledShotSource(const uint8_t **buf, size_t *len, int *w, int *h);
// --oledtext: draw UTF-8 text through the message fold; font 0 = 6x10, 1 = 10x20; hold_s on screen.
// Returns 0 ok, 1 no display, 2 empty text.
int oledTextTest(const char *utf8, int font, int hold_s);
#endif

#endif
