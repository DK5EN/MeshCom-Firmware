#ifndef SCREEN_SHADOW_H
#define SCREEN_SHADOW_H

#include <stdint.h>
#include <string.h>

/*
 * Shadow copy of the T-Deck screen (INSTRUMENT builds). The panel does not drive MISO, so the
 * picture cannot be read back from it. Every LVGL flush is copied here instead, 2 bytes per
 * pixel in flush order (RGB565 big endian, LV_COLOR_16_SWAP), and clipped to the frame.
 */
inline void screenShadowBlit(uint8_t *fb, int fbW, int fbH, int x1, int y1, int w, int h,
                             const uint8_t *src)
{
    if (fb == NULL || src == NULL || w <= 0 || h <= 0)
        return;
    const int srcStride = w * 2;
    int skipCols = 0;
    int skipRows = 0;
    if (x1 < 0)
    {
        skipCols = -x1;
        w += x1;
        x1 = 0;
    }
    if (y1 < 0)
    {
        skipRows = -y1;
        h += y1;
        y1 = 0;
    }
    if (x1 >= fbW || y1 >= fbH || w <= 0 || h <= 0)
        return;
    if (x1 + w > fbW)
        w = fbW - x1;
    if (y1 + h > fbH)
        h = fbH - y1;
    for (int row = 0; row < h; row++)
    {
        memcpy(fb + ((y1 + row) * fbW + x1) * 2,
               src + (skipRows + row) * srcStride + skipCols * 2,
               (size_t)w * 2);
    }
}

#endif
