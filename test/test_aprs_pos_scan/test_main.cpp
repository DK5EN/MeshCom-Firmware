// test_aprs_pos_scan: decodeAPRSPOS(const char *) after B-APRS-POS-01 (one tag pass, bounded value reads, CheckGroup
// without a String temporary) against the former decoder (aprs_pos_ref.h), every field of aprsPosition compared.
// Inputs: the payloads of every corpus frame, generated position beacons with random tags, group lists and digital
// bits, and raw noise. 2026-11-26.
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>
#include "repo_file.h"
#include "aprs_pos_ref.h"

s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

static uint32_t g_s = 0xA5A5A5u;
static uint32_t rnd(void)
{
    g_s = g_s * 1664525u + 1013904223u;
    return g_s >> 8;
}
static unsigned long g_cmp = 0, g_ok = 0, g_range = 0;

#define SAMEBITS(f) (memcmp(&a.f, &b.f, sizeof(a.f)) == 0)

// initAPRSPOS() leaves some fields as they were (e.g. vbus, vcurrent, grc[]); start both from the same bytes
static void prefill(aprsPosition &x)
{
    x.lat = x.lon = x.lat_d = x.lon_d = -1.5;
    x.lat_c = x.lon_c = x.aprs_group = x.aprs_symbol = '?';
    x.alt = x.bat = x.qfe = x.ncnt = x.version = x.telemetry = x.grccnt = -7;
    x.press = x.hum = x.temp = x.temp2 = x.qnh = x.gasres = x.co2 = x.vbus = x.vcurrent = -2.5f;
    memset(x.din, 'z', sizeof(x.din));
    for (int i = 0; i < 6; i++) x.grc[i] = -9;
}

static const char *firstDiff(const aprsPosition &a, const aprsPosition &b)
{
#define D(f) if (!SAMEBITS(f)) return #f;
    D(lat) D(lat_c) D(lon) D(lon_c) D(alt) D(bat) D(lat_d) D(lon_d) D(aprs_group) D(aprs_symbol) D(press) D(hum)
    D(temp) D(temp2) D(qfe) D(qnh) D(gasres) D(co2) D(ncnt) D(version) D(telemetry) D(din) D(vbus) D(vcurrent)
    D(grc) D(grccnt)
#undef D
    return 0;
}

static void both(const char *payload, const char *what)
{
    aprsPosition a, b;
    prefill(a);
    prefill(b);
    const uint16_t ra = refDecodeAPRSPOS(payload, a);
    const uint16_t rb = decodeAPRSPOS(payload, b);
    g_cmp++;
    char msg[400];
    snprintf(msg, sizeof(msg), "%s: \"%.300s\"", what, payload);
    // APRS-POS-RANGE-01: the former decoder took a lat/lon such as "1e99" (sscanf reads exponents) and then cast an
    // out-of-range double to int; the decoder now rejects that position. Everything else must stay identical.
    if (ra == 1 && !(a.lat > -2.0e11 && a.lat < 2.0e11 && a.lon > -2.0e11 && a.lon < 2.0e11))
    {
        g_range++;
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, rb, msg);
        return;
    }
    if (ra == 1) g_ok++;
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(ra, rb, msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.pos_atxt.c_str(), b.pos_atxt.c_str(), msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a.pos_name.c_str(), b.pos_name.c_str(), msg);
    const char *d = firstDiff(a, b);
    if (d)
    {
        char m2[460];
        snprintf(m2, sizeof(m2), "field %s differs -- %s", d, msg);
        TEST_FAIL_MESSAGE(m2);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The payload of every corpus frame that decodeAPRS() accepts, plus every suffix of it (other start offsets).
static unsigned corpusRun(const char *rel)
{
    FILE *f = repo_fopen(rel, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, rel);
    static char line[4096];
    unsigned n = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[32];
        static char hex[2048];
        if (sscanf(line, "%31s %2047s", name, hex) != 2) continue;
        uint8_t buf[UDP_TX_BUF_SIZE + 8];
        memset(buf, 0, sizeof(buf));
        size_t len = strlen(hex) / 2;
        if (len > UDP_TX_BUF_SIZE) len = UDP_TX_BUF_SIZE;
        bool ok = true;
        for (size_t i = 0; i < len && ok; i++)
        {
            const int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
            ok = hi >= 0 && lo >= 0;
            buf[i] = (uint8_t)(hi * 16 + lo);
        }
        if (!ok) continue;
        aprsMessage m;
        if (decodeAPRS(buf, (uint16_t)len, m) == 0) continue;
        n++;
        const size_t pl = strlen(m.msg_payload);
        for (size_t s = 0; s <= pl; s++)
            both(m.msg_payload + s, name);
    }
    fclose(f);
    return n;
}

static void test_corpora(void)
{
    unsigned n = 0;
    n += corpusRun("test/test_aprs_corpus/corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/capture_corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/crc_corpus.txt");
    n += corpusRun("test/test_aprs_fuzz/ack_corpus.txt");
    TEST_ASSERT_GREATER_THAN_UINT(500, n);
}

static void put(char *s, size_t &p, size_t cap, const char *t)
{
    while (*t && p + 1 < cap) s[p++] = *t++;
    s[p] = 0;
}

// Generated position beacons: lat/lon in the APRS form, symbol, comment/name, then a random mix of tags.
static void test_generated(void)
{
    static const char TAGS[] = "BAPHTOFQGCVUIYRDNXZ";
    char s[300];
    char tmp[64];
    for (int it = 0; it < 150000; it++)
    {
        size_t p = 0;
        s[0] = 0;
        if (rnd() % 20)
        {
            snprintf(tmp, sizeof(tmp), "%04u.%02u%c%c%05u.%02u%c%c", rnd() % 9100, rnd() % 100, rnd() % 2 ? 'N' : 'S',
                     rnd() % 2 ? '/' : '\\', rnd() % 18100, rnd() % 100, rnd() % 2 ? 'E' : 'W', "&#>-"[rnd() % 4]);
            if (rnd() % 15 == 0) tmp[rnd() % strlen(tmp)] = (char)(0x21 + rnd() % 90); // corrupt one byte
            put(s, p, sizeof(s), tmp);
        }
        if (rnd() % 3 == 0) // comment / name
        {
            const int n = rnd() % 30;
            for (int i = 0; i < n && p + 1 < sizeof(s); i++) s[p++] = (char)(rnd() % 6 == 0 ? '#' : 'a' + rnd() % 26);
            s[p] = 0;
        }
        const int ntags = rnd() % 8;
        for (int k = 0; k < ntags; k++)
        {
            const char tg = TAGS[rnd() % (sizeof(TAGS) - 1)];
            if (tg == 'N') { snprintf(tmp, sizeof(tmp), "/N%u", rnd() % 2 ? 1 + rnd() % 9999 : rnd() % 10); put(s, p, sizeof(s), tmp); continue; }
            snprintf(tmp, sizeof(tmp), "/%c=", tg);
            put(s, p, sizeof(s), tmp);
            const uint32_t v = rnd() % 10;
            if (tg == 'R')
            {
                const int ng = rnd() % 9;
                for (int g = 0; g < ng; g++)
                {
                    snprintf(tmp, sizeof(tmp), "%s%u", g ? ";" : "", rnd() % 4 == 0 ? rnd() % 1000000 : 1 + rnd() % 99999);
                    put(s, p, sizeof(s), tmp);
                }
                if (rnd() % 8 == 0) put(s, p, sizeof(s), ";x");
            }
            else if (tg == 'D')
            {
                const int nb = 6 + rnd() % 5;
                for (int i = 0; i < nb && p + 1 < sizeof(s); i++) s[p++] = (char)(rnd() % 12 == 0 ? '2' : '0' + rnd() % 2);
                s[p] = 0;
            }
            else if (v < 4) { snprintf(tmp, sizeof(tmp), "%d", (int)(rnd() % 200000) - 1000); put(s, p, sizeof(s), tmp); }
            else if (v < 7) { snprintf(tmp, sizeof(tmp), "%.3f", (double)(rnd() % 100000) / 7.0 - 50.0); put(s, p, sizeof(s), tmp); }
            else if (v < 8) put(s, p, sizeof(s), rnd() % 2 ? "0x1A" : "010");
            else if (v < 9) put(s, p, sizeof(s), "123456789012");
            // v == 9: empty value
            if (rnd() % 4 == 0) put(s, p, sizeof(s), " ");
        }
        if (rnd() % 10 == 0 && p > 0) s[rnd() % p] = (char)(0x20 + rnd() % 95);   // stray byte anywhere
        if (rnd() % 10 == 0 && p > 0) s[rnd() % (p + 1)] = 0;                      // cut anywhere
        both(s, "generated");
    }
}

static void test_noise(void)
{
    char s[300];
    for (int it = 0; it < 50000; it++)
    {
        const size_t n = rnd() % 290;
        for (size_t i = 0; i < n; i++) s[i] = (char)(1 + rnd() % 255);
        s[n] = 0;
        both(s, "noise");
    }
    both("", "empty");
}

static void test_counts(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(200000, (uint32_t)g_cmp);
    TEST_ASSERT_GREATER_THAN_UINT32(50000, (uint32_t)g_ok); // accepted positions, so the tag paths ran
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)g_range);  // the generator's corrupted bytes do produce exponents
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_corpora);
    RUN_TEST(test_generated);
    RUN_TEST(test_noise);
    RUN_TEST(test_counts);
    return UNITY_END();
}
