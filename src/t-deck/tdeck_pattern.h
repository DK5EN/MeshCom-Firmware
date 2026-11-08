// DISP-PAT-01: T-Deck display test pattern (--tdpattern, INSTRUMENT builds only).
// The pure part (colour generators, argument parser) is LVGL-free and host-tested (test_tdeck_pattern).
// The LVGL part is declared at the bottom and lives in tdeck_pattern.cpp.
#ifndef TDECK_PATTERN_H
#define TDECK_PATTERN_H

#include <stdint.h>
#include <stddef.h>
#include "tdeck_palette.h"

// 0xRRGGBB of an RGB565 colour from 5/6/5-bit channel values (bits replicated, so it is on the grid).
inline uint32_t tdeck_pat_from565(uint32_t r5, uint32_t g6, uint32_t b5)
{
    return tdeck_rgb565((r5 << 19) | (g6 << 10) | (b5 << 3));
}

// Neutral grey step i = 0..31 (out of range clamps to 31): R5 = B5 = i, G6 = 2*i + (i >> 4).
inline uint32_t tdeck_pat_grey(uint8_t i)
{
    if(i > 31)
        i = 31;
    return tdeck_pat_from565(i, (uint32_t)(2 * i + (i >> 4)), i);
}

// Hue 0..5 = R, G, B, C, M, Y as channel mask (bit 0 = R, 1 = G, 2 = B).
inline uint8_t tdeck_pat_hue_mask(uint8_t hue)
{
    static const uint8_t m[6] = {1, 2, 4, 6, 5, 3};
    return m[hue > 5 ? 5 : hue];
}

// Dark ramp step k = 0..7: active channels = 255 * (k + 1) / 8 (k = 7 is full saturation), others 0.
inline uint32_t tdeck_pat_ramp_dark(uint8_t hue, uint8_t k)
{
    if(k > 7)
        k = 7;
    const uint32_t v = 255u * (k + 1u) / 8u, mk = tdeck_pat_hue_mask(hue);
    return tdeck_rgb565(((mk & 1 ? v : 0) << 16) | ((mk & 2 ? v : 0) << 8) | (mk & 4 ? v : 0));
}

// Light ramp step k = 0..7: full saturation (k = 0) toward white (k = 7); inactive channels = 255 * k / 7.
inline uint32_t tdeck_pat_ramp_light(uint8_t hue, uint8_t k)
{
    if(k > 7)
        k = 7;
    const uint32_t v = 255u * k / 7u, mk = tdeck_pat_hue_mask(hue);
    return tdeck_rgb565(((mk & 1 ? 255u : v) << 16) | ((mk & 2 ? 255u : v) << 8) | (mk & 4 ? 255u : v));
}

// "<page> [bl <level>]" or "off". Leading/trailing blanks, CR and LF are ignored; tokens are exact and
// lower case. page 0..4, bl 1..16 (*bl = 0 when absent). Returns 0 ok, 1 = off, -1 error.
inline int tdeck_pat_parse(const char* args, int* page, int* bl)
{
    if(args == NULL || page == NULL || bl == NULL)
        return -1;
    char tok[4][12];
    int n = 0;
    const char* p = args;
    for(;;)
    {
        while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if(*p == 0)
            break;
        if(n == 4)
            return -1;
        size_t l = 0;
        while(*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        {
            if(l >= sizeof(tok[0]) - 1)
                return -1;
            tok[n][l++] = *p++;
        }
        tok[n][l] = 0;
        n++;
    }
    auto num = [](const char* s, int lo, int hi, int* out) {
        if(s[0] == 0)
            return false;
        int v = 0;
        for(const char* c = s; *c; c++)
        {
            if(*c < '0' || *c > '9')
                return false;
            v = v * 10 + (*c - '0');
        }
        if(v < lo || v > hi)
            return false;
        *out = v;
        return true;
    };
    auto eq = [](const char* a, const char* b) {
        while(*a && *a == *b) { a++; b++; }
        return *a == 0 && *b == 0;
    };
    if(n == 1 && eq(tok[0], "off"))
        return 1;
    if(n != 1 && n != 3)
        return -1;
    int pg = 0, lv = 0;
    if(!num(tok[0], 0, 4, &pg))
        return -1;
    if(n == 3 && (!eq(tok[1], "bl") || !num(tok[2], 1, 16, &lv)))
        return -1;
    *page = pg;
    *bl = lv;
    return 0;
}

#ifndef NATIVE_BUILD
#if INSTRUMENT_ENABLED && (defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS))
// --tdpattern argument string (everything after the command token); prints the [TDPAT] lines.
void tdeck_pattern_cmd(const char* args);
// True while a pattern is shown; brightnessTick() (tdeck_helpers.cpp) then feeds the TFT sleep timer.
bool tdeck_pattern_active();
#endif
#endif

#endif
