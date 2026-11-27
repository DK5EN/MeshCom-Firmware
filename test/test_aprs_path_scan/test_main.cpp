// test_aprs_path_scan: the range scan of src/aprs_path_scan.h against the former byte-by-byte path loops of
// decodeAPRS() (aprs_path_ref.h), field by field (path, last hop / destination call, source call, comma
// count, terminator found, next index), and the payload range against the former payload copy loop (length,
// bytes, NUL found, next index). B-APRS-PATH-01, 2026-11-26.
//
// Inputs: every frame of the four repository corpora (on-air captures and harvested logs) scanned from every
// start offset; every byte sequence up to 5 bytes over an alphabet holding each byte class the loops
// distinguish; 200,000 seeded random frames; and fixed edge cases (120-byte cap, field truncation).
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "repo_file.h"
#include "aprs_path_ref.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t TERMS[] = {'>', 0x3A, 0x21, 0x40};
static unsigned long g_compared = 0;

// Compare both implementations for one buffer, one start, every terminator the decoder uses, source and
// destination form. Fails the test with the input on the first difference.
static void checkAll(const uint8_t *buf, uint16_t rsize, uint16_t start, const char *what)
{
    for (size_t t = 0; t < sizeof(TERMS); t++)
    {
        for (int source = 0; source < 2; source++)
        {
            PathOut a, b;
            const uint16_t inext_in = (uint16_t)(start + 7); // any value; must survive unchanged when not found
            refScan(buf, start, rsize, TERMS[t], source != 0, inext_in, a);
            newScan(buf, start, rsize, TERMS[t], source != 0, inext_in, b);
            const char *d = pathDiff(a, b);
            g_compared++;
            if (d)
            {
                char msg[200];
                snprintf(msg, sizeof(msg), "%s: field %s differs (rsize %u, start %u, term 0x%02X, source %d)", what,
                         d, (unsigned)rsize, (unsigned)start, TERMS[t], source);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void checkPayload(const uint8_t *buf, uint16_t rsize, uint16_t start, const char *what)
{
    PayloadOut a, b;
    refPayload(buf, start, rsize, a);
    newPayload(buf, start, rsize, b);
    g_compared++;
    const char *d = payloadDiff(a, b);
    if (d)
    {
        char msg[200];
        snprintf(msg, sizeof(msg), "%s: %s differs (rsize %u, start %u)", what, d, (unsigned)rsize, (unsigned)start);
        TEST_FAIL_MESSAGE(msg);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Every "<name> <hex>" line of a corpus, scanned from every start offset 0..len.
static unsigned corpusRun(const char *rel)
{
    FILE *f = repo_fopen(rel, "r");
    char miss[160];
    snprintf(miss, sizeof(miss), "%s not found", rel);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, miss);
    static char line[4096];
    unsigned frames = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[32];
        static char hex[2048];
        if (sscanf(line, "%31s %2047s", name, hex) != 2) continue;
        uint8_t buf[400];
        size_t n = strlen(hex) / 2;
        if (n > sizeof(buf)) n = sizeof(buf);
        bool ok = true;
        for (size_t i = 0; i < n; i++)
        {
            int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
            if (hi < 0 || lo < 0) { ok = false; break; }
            buf[i] = (uint8_t)(hi * 16 + lo);
        }
        if (!ok) continue;
        frames++;
        for (uint16_t s = 0; s <= n; s++)
        {
            checkAll(buf, (uint16_t)n, s, name);
            checkPayload(buf, (uint16_t)n, s, name);
        }
    }
    fclose(f);
    return frames;
}

static void test_corpora(void)
{
    unsigned n = 0;
    n += corpusRun("test/test_aprs_corpus/corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/capture_corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/crc_corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/ack_corpus.txt");
    // The corpora hold about 1,500 frames; far fewer means a file was cut or not parsed.
    TEST_ASSERT_GREATER_THAN_UINT(1400, n);
}

// All sequences up to 5 bytes over one representative of every byte class the loops tell apart: the four
// terminators, the comma, two ordinary printable bytes, the class edges 0x20 and 0x7E, below 0x20, above 0x7E.
static void test_exhaustive_small(void)
{
    static const uint8_t A[] = {'>', ':', '!', '@', ',', 'A', '9', 0x20, 0x7E, 0x1F, 0x7F, 0xC3};
    const unsigned K = sizeof(A);
    uint8_t buf[8];
    for (unsigned len = 0; len <= 5; len++)
    {
        unsigned long total = 1;
        for (unsigned i = 0; i < len; i++) total *= K;
        for (unsigned long code = 0; code < total; code++)
        {
            unsigned long c = code;
            for (unsigned i = 0; i < len; i++) { buf[i] = A[c % K]; c /= K; }
            checkAll(buf, (uint16_t)len, 0, "exhaustive");
        }
    }
    // payload: NUL, an ordinary byte, a UTF-8 lead byte, a control byte
    static const uint8_t P[] = {0x00, 'A', 0xC3, 0x01};
    uint8_t pb[12];
    for (unsigned len = 0; len <= 10; len++)
    {
        unsigned long total = 1;
        for (unsigned i = 0; i < len; i++) total *= 4;
        for (unsigned long code = 0; code < total; code++)
        {
            unsigned long c = code;
            for (unsigned i = 0; i < len; i++) { pb[i] = P[c % 4]; c /= 4; }
            checkPayload(pb, (uint16_t)len, 0, "payload-exhaustive");
        }
    }
}

// Seeded random frames, length 0..300, bytes weighted towards path-like content.
static void test_random(void)
{
    uint32_t s = 0x1234567u;
    uint8_t buf[300];
    for (int it = 0; it < 200000; it++)
    {
        s = s * 1664525u + 1013904223u;
        const uint16_t len = (uint16_t)((s >> 8) % 301);
        for (uint16_t i = 0; i < len; i++)
        {
            s = s * 1664525u + 1013904223u;
            const uint32_t r = s >> 16;
            const uint32_t k = r % 100;
            if (k < 60) buf[i] = (uint8_t)('A' + (r / 100) % 26);
            else if (k < 70) buf[i] = ',';
            else if (k < 75) buf[i] = '-';
            else if (k < 80) buf[i] = (uint8_t)('0' + (r / 100) % 10);
            else if (k < 84) buf[i] = '>';
            else if (k < 88) buf[i] = ':';
            else if (k < 90) buf[i] = '!';
            else if (k < 92) buf[i] = '@';
            else buf[i] = (uint8_t)(r / 100); // any byte
        }
        s = s * 1664525u + 1013904223u;
        const uint16_t start = len ? (uint16_t)((s >> 8) % (len + 1)) : 0;
        checkAll(buf, len, start, "random");
        checkPayload(buf, len, start, "random");
    }
}

// Fixed edge cases: the 120-byte cap, a path longer than its field (121), calls longer than 21, 120 commas.
static void test_edges(void)
{
    uint8_t buf[300];

    memset(buf, 'A', sizeof(buf));           // no terminator: the cap at 120 bytes ends the field
    checkAll(buf, 300, 0, "cap120");
    checkAll(buf, 300, 6, "cap120@6");
    buf[120] = '>';                          // terminator right after the cap: not found
    checkAll(buf, 300, 0, "term-after-cap");
    buf[119] = '>';                          // terminator as the 120th byte: found
    checkAll(buf, 300, 0, "term-at-cap");

    memset(buf, ',', sizeof(buf));           // 120 commas: cnt 121, empty call and last
    checkAll(buf, 300, 0, "commas");

    memset(buf, 'B', sizeof(buf));           // call and last longer than 21: both cut at 20
    buf[30] = ',';
    buf[100] = '>';
    checkAll(buf, 300, 0, "long-calls");

    memset(buf, 'P', sizeof(buf));           // payload: 254 bytes cap, NUL right after the cap is not seen
    checkPayload(buf, 300, 0, "payload-cap");
    buf[254] = 0;
    checkPayload(buf, 300, 0, "payload-nul-after-cap");
    buf[253] = 0;
    checkPayload(buf, 300, 0, "payload-nul-at-cap");
    checkPayload(buf, 300, 299, "payload-tail");

    checkAll(buf, 0, 0, "empty");            // rsize 0
    checkAll(buf, 10, 10, "start=rsize");    // start at the end
    checkAll(buf, 10, 12, "start>rsize");    // start past the end (decodeAPRS: inext after a terminator at the end)
}

// The new scan on its own: the fields it reports.
static void test_scan_fields(void)
{
    const char *fr = "XXXXXXDK5EN-1,OE1ABC-12,DB0XYZ-2>*:text";
    const uint8_t *b = (const uint8_t *)fr;
    AprsPathField f = aprsPathScan(b, 6, (uint16_t)strlen(fr), '>');
    TEST_ASSERT_TRUE(f.found);
    TEST_ASSERT_EQUAL_UINT16(6, f.start);
    TEST_ASSERT_EQUAL_UINT16(32, f.end);        // index of '>'
    TEST_ASSERT_EQUAL_UINT16(13, f.first_end);  // first comma
    TEST_ASSERT_EQUAL_UINT16(24, f.last_start); // after the last comma
    TEST_ASSERT_EQUAL_UINT16(2, f.commas);
    char call[21];
    aprsPathCopy(call, sizeof(call), b, f.start, f.first_end);
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", call);
    aprsPathCopy(call, sizeof(call), b, f.last_start, f.end);
    TEST_ASSERT_EQUAL_STRING("DB0XYZ-2", call);
}

static uint32_t rnd32(void);
// aprsFcsSum() against the former inline loop of decodeAPRS() (sum of buf[0..n)).
static void test_fcs_sum(void)
{
    uint8_t buf[300];
    for (int it = 0; it < 20000; it++)
    {
        const uint16_t n = (uint16_t)(rnd32() % 300);
        for (uint16_t i = 0; i < n; i++) buf[i] = (uint8_t)rnd32();
        unsigned int ref = 0;
        for (uint16_t ib = 0; ib < n; ib++) ref += (unsigned int)buf[ib];
        TEST_ASSERT_EQUAL_UINT32(ref, aprsFcsSum(buf, n));
    }
    memset(buf, 0xFF, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(255u * 299u, aprsFcsSum(buf, 299));
    TEST_ASSERT_EQUAL_UINT32(0, aprsFcsSum(buf, 0));
}

static uint32_t g_r32 = 77u;
static uint32_t rnd32(void)
{
    g_r32 = g_r32 * 1664525u + 1013904223u;
    return g_r32 >> 8;
}

static void test_count_report(void)
{
    // Not a check of behaviour: guards against a loop that silently compares nothing.
    TEST_ASSERT_GREATER_THAN_UINT32(3000000, (uint32_t)g_compared);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_scan_fields);
    RUN_TEST(test_edges);
    RUN_TEST(test_corpora);
    RUN_TEST(test_exhaustive_small);
    RUN_TEST(test_random);
    RUN_TEST(test_fcs_sum);
    RUN_TEST(test_count_report);
    return UNITY_END();
}
