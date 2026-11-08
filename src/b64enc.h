// CLK-01 (docs/campaign-clk-01-20261107.md) -- RFC 4648 base64 encoder (with padding) for the
// INSTRUMENT serial screenshots [OLEDSHOT] / [TDSHOT]. Pure, header-only, host-testable
// (test/test_oled_shot). No decoder: the host side (tools/bench/clock_drift.py) decodes.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Length of the base64 text (without NUL) for n input bytes.
static inline size_t b64EncodedLen(size_t n)
{
    return ((n + 2) / 3) * 4;
}

// Encode n bytes into out (NUL terminated). Returns the text length, or 0 if cap is smaller than
// b64EncodedLen(n) + 1 (out[0] is then set to NUL when cap > 0). n == 0 gives "" and returns 0.
static inline size_t b64Encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t need = b64EncodedLen(n);
    if (cap == 0)
        return 0;
    if (cap < need + 1)
    {
        out[0] = '\0';
        return 0;
    }
    size_t o = 0;
    size_t i = 0;
    for (; i + 3 <= n; i += 3)
    {
        const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];
        out[o++] = tab[v & 63];
    }
    if (n - i == 1)
    {
        const uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if (n - i == 2)
    {
        const uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = tab[(v >> 18) & 63];
        out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}
