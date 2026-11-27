// test_applink_ws_range: the AppLink frame decoder after B-AL-RANGE-01 (src/applink_ws.cpp) against the former
// decoder (applink_ws_ref.h). Both get the same byte stream in the same random chunks; after every call the consumed
// count, the event and the whole decoder state must be equal. 2026-11-26.
//
//   pio test -e native_applink_ws -f test_applink_ws_range
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "applink_ws_ref.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t g_s = 0xC0FFEEu;
static uint32_t rnd(void)
{
    g_s = g_s * 1664525u + 1013904223u;
    return g_s >> 8;
}
static unsigned long g_calls = 0, g_events = 0;

// One client frame: FIN/opcode byte, masked length (7, 16 or 64 bit), mask, masked payload. Mostly well formed,
// sometimes broken on purpose (RSV bits, unmasked, wrong opcode, control frame too long or fragmented).
static size_t frame(uint8_t *out, size_t cap, uint8_t op, bool fin, size_t plen)
{
    size_t p = 0;
    uint8_t b0 = (uint8_t)((fin ? 0x80 : 0) | (op & 0x0F));
    if (rnd() % 60 == 0) b0 |= 0x40;               // RSV1
    out[p++] = b0;
    const bool masked = rnd() % 50 != 0;
    const int form = plen < 126 ? (rnd() % 8 ? 0 : 1) : (plen <= 0xFFFF ? 1 : 2);
    if (form == 0) out[p++] = (uint8_t)((masked ? 0x80 : 0) | plen);
    else if (form == 1) { out[p++] = (uint8_t)((masked ? 0x80 : 0) | 126); out[p++] = (uint8_t)(plen >> 8); out[p++] = (uint8_t)plen; }
    else { out[p++] = (uint8_t)((masked ? 0x80 : 0) | 127); for (int k = 7; k >= 0; k--) out[p++] = (uint8_t)((uint64_t)plen >> (8 * k)); }
    uint8_t m[4];
    for (int k = 0; k < 4; k++) m[k] = (uint8_t)rnd();
    if (masked) for (int k = 0; k < 4; k++) out[p++] = m[k];
    for (size_t k = 0; k < plen && p < cap; k++) out[p++] = (uint8_t)(rnd() ^ (masked ? m[k & 3] : 0));
    return p;
}

static void feedBoth(AlWsDecoder &a, AlWsDecoder &b, const uint8_t *s, size_t len)
{
    size_t off = 0;
    int guard = 0;
    while (off < len && guard++ < 100000)
    {
        size_t chunk = 1 + rnd() % 64;
        if (rnd() % 4 == 0) chunk = len - off;  // sometimes everything at once
        if (chunk > len - off) chunk = len - off;
        AlWsEvent ea = AL_WS_NONE, eb = AL_WS_NONE;
        const size_t ra = alref::feed(&a, s + off, chunk, &ea);
        const size_t rb = alWsFeed(&b, s + off, chunk, &eb);
        g_calls++;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(ra, rb, "consumed");
        TEST_ASSERT_EQUAL_INT_MESSAGE(ea, eb, "event");
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&a, &b, sizeof(a), "decoder state");
        if (ea != AL_WS_NONE) g_events++;
        if (ea == AL_WS_ERROR) return;          // both stay in error
        if (ra == 0 && chunk > 0) return;       // nothing consumed: both stuck the same way
        off += ra;
    }
}

static void test_random_streams(void)
{
    static uint8_t s[40000];
    for (int it = 0; it < 10000; it++)
    {
        size_t len = 0;
        const int frames = 1 + rnd() % 12;
        bool inFrag = false;
        for (int f = 0; f < frames && len + 400 < sizeof(s); f++)
        {
            const uint32_t k = rnd() % 20;
            uint8_t op;
            bool fin = true;
            size_t plen;
            if (k < 3) { op = 0x9 + rnd() % 2; plen = rnd() % (k == 0 ? 130 : 126); }          // ping / pong
            else if (k == 3) { op = 0x8; plen = rnd() % 4 == 0 ? 1 : 2 + rnd() % 20; }          // close
            else if (k == 4) { op = (uint8_t)(rnd() % 16); plen = rnd() % 40; }                 // any opcode
            else if (k < 9) { op = inFrag ? 0x0 : 0x1 + rnd() % 2; fin = rnd() % 2; plen = rnd() % 160; inFrag = !fin; }
            else { op = inFrag ? 0x0 : 0x1 + rnd() % 2; fin = true; inFrag = false;
                   const uint32_t z = rnd() % 10;
                   plen = z < 3 ? 295 + rnd() % 10 : z < 5 ? rnd() % 300 : z < 6 ? 60000 + rnd() % 10000 : rnd() % 64; }
            len += frame(s + len, sizeof(s) - len, op, fin, plen);
        }
        if (rnd() % 10 == 0) // raw noise tail
            for (int n = rnd() % 64; n > 0 && len < sizeof(s); n--) s[len++] = (uint8_t)rnd();
        AlWsDecoder a, b;
        alWsDecoderInit(&a);
        alWsDecoderInit(&b);
        feedBoth(a, b, s, len);
    }
}

// Byte streams with no structure at all.
static void test_noise(void)
{
    uint8_t s[512];
    for (int it = 0; it < 20000; it++)
    {
        const size_t len = rnd() % sizeof(s);
        for (size_t k = 0; k < len; k++) s[k] = (uint8_t)rnd();
        if (len && rnd() % 2) s[0] = (uint8_t)(0x80 | (1 + rnd() % 2));   // plausible first byte
        if (len > 1 && rnd() % 2) s[1] |= 0x80;                           // masked
        AlWsDecoder a, b;
        alWsDecoderInit(&a);
        alWsDecoderInit(&b);
        feedBoth(a, b, s, len);
    }
}

// The size limits exactly: messages of 299, 300, 301 bytes, single or in two fragments split at every point.
static void test_limits(void)
{
    static uint8_t s[2000];
    for (size_t total = 296; total <= 302; total++)
        for (size_t cut = 0; cut <= total; cut += (total > 10 ? 7 : 1))
        {
            size_t len = 0;
            if (cut == 0)
                len = frame(s, sizeof(s), 0x2, true, total);
            else
            {
                len = frame(s, sizeof(s), 0x2, false, cut);
                len += frame(s + len, sizeof(s) - len, 0x0, true, total - cut);
            }
            AlWsDecoder a, b;
            alWsDecoderInit(&a);
            alWsDecoderInit(&b);
            feedBoth(a, b, s, len);
        }
}

static void test_counts(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(50000, (uint32_t)g_calls); // measured 87,382 (2026-11-26)
    TEST_ASSERT_GREATER_THAN_UINT32(10000, (uint32_t)g_events);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_limits);
    RUN_TEST(test_random_streams);
    RUN_TEST(test_noise);
    RUN_TEST(test_counts);
    return UNITY_END();
}
