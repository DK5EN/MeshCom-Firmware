// test_rm_range: the range-based RM1 code of B-RM-RANGE-01 (src/remote_cmd.cpp) against the former byte-by-byte
// code (rm_ref.h): rmParse() on frame-shaped and free text, rmCanonical() and rmReplyCanonical() on every buffer
// size around the fit limit, rmCommandAllowed() on every command name. 2026-11-26.
//
//   pio test -e native_remote_cmd -f test_rm_range
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <remote_cmd.h>
#include <rm_commands.h>
#include "rm_ref.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t g_s = 0x9E3779B9u;
static uint32_t rnd(void)
{
    g_s = g_s * 1664525u + 1013904223u;
    return g_s >> 8;
}
static unsigned long g_parse = 0, g_canon = 0;

static void parseBoth(const char *text, const char *what)
{
    RmCmd a, b;
    const bool ra = rmref::parse(text, a);
    const bool rb = rmParse(text, b);
    g_parse++;
    char msg[300];
    snprintf(msg, sizeof(msg), "%s: rmParse differs for \"%.200s\"", what, text);
    TEST_ASSERT_EQUAL_MESSAGE(ra, rb, msg);
    if (ra)
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&a, &b, sizeof(a), msg); // the whole struct, padding included (both memset)
}

static const char *const NAMES[] = {"sync", "info", "led", "txpower", "setout", "pins", "gpio", "upd", "mh",
                                    "name", "pos", "Info", "x", "abcdefghijklmno", "abcdefghijklmnop"};

// Frame-shaped text: "RM1 <ctr> <cmd>[ <args>] <tag>" with every field drawn near its limits.
static void buildFrame(char *t, size_t cap)
{
    size_t p = 0;
    auto put = [&](char ch) {
        if (p + 1 < cap)
            t[p++] = ch;
    };
    const char *pre = (rnd() % 20) ? "RM1 " : "RM1x";
    for (const char *q = pre; *q; q++) put(*q);
    const unsigned cd = rnd() % 12; // 0..11 digits
    for (unsigned i = 0; i < cd; i++) put((char)((i == 0 && rnd() % 4 == 0) ? '0' : '0' + rnd() % 10));
    put(' ');
    if (rnd() % 3 == 0)
        for (const char *q = NAMES[rnd() % (sizeof(NAMES) / sizeof(NAMES[0]))]; *q; q++) put(*q);
    else
    {
        const unsigned cl = rnd() % 18;
        for (unsigned i = 0; i < cl; i++) put((char)(rnd() % 8 == 0 ? 'A' + rnd() % 26 : 'a' + rnd() % 26));
    }
    const unsigned mode = rnd() % 4;
    if (mode != 0)
    {
        put(' ');
        const unsigned al = mode == 3 ? 36 + rnd() % 8 : rnd() % 42; // around RM_MAX_ARGS
        for (unsigned i = 0; i < al; i++)
        {
            const unsigned k = rnd() % 10;
            put(k == 0 ? ' ' : k == 1 ? (char)(0x20 + rnd() % 95) : (char)('a' + rnd() % 26));
        }
    }
    put(' ');
    const unsigned tl = (rnd() % 10) ? 16 : 14 + rnd() % 5;
    for (unsigned i = 0; i < tl; i++) put((char)(rnd() % 12 == 0 ? 'G' : "0123456789abcdef"[rnd() % 16]));
    if (rnd() % 30 == 0) put(' ');
    t[p] = 0;
}

static void test_parse_frames(void)
{
    char t[220];
    for (int it = 0; it < 300000; it++)
    {
        buildFrame(t, sizeof(t));
        parseBoth(t, "frame");
    }
}

static void test_parse_free_text(void)
{
    char t[220];
    for (int it = 0; it < 100000; it++)
    {
        const size_t len = rnd() % 210;
        for (size_t i = 0; i < len; i++) t[i] = (char)(1 + rnd() % 255);
        if (len >= 4 && rnd() % 2) memcpy(t, "RM1 ", 4);
        t[len] = 0;
        parseBoth(t, "free");
    }
}

static void test_parse_edges(void)
{
    const char *tag = "0123456789abcdef";
    char t[220];
    // cmd of 14, 15, 16 bytes; args of 38, 39, 40 bytes; double space at the end of args; trailing args space
    for (int cl = 13; cl <= 17; cl++)
        for (int al = 0; al <= 41; al++)
        {
            char cmd[20], args[50];
            memset(cmd, 'c', cl); cmd[cl] = 0;
            memset(args, 'a', al); args[al] = 0;
            snprintf(t, sizeof(t), "RM1 7 %s%s%s %s", cmd, al ? " " : "", args, tag);
            parseBoth(t, "edge");
            if (al >= 2)
            {
                args[al - 1] = ' ';
                snprintf(t, sizeof(t), "RM1 7 %s %s %s", cmd, args, tag);
                parseBoth(t, "edge-trailing-space");
                args[al - 2] = ' ';
                snprintf(t, sizeof(t), "RM1 7 %s %s %s", cmd, args, tag);
                parseBoth(t, "edge-double-space");
            }
        }
    parseBoth("RM1 0 sync 0123456789abcdef", "sync");
    parseBoth("RM1 0 info 0123456789abcdef", "ctr0-not-sync");
    parseBoth("RM1 4294967295 info 0123456789abcdef", "ctr-max");
    parseBoth("RM1 4294967296 info 0123456789abcdef", "ctr-overflow");
}

static void randStr(char *s, size_t maxlen)
{
    const size_t n = rnd() % (maxlen + 1);
    for (size_t i = 0; i < n; i++) s[i] = (char)(0x20 + rnd() % 95);
    s[n] = 0;
}

// Both builders on every buffer size 0..n_full+2, so the fit limit is hit exactly at every append.
static void test_canonical_sizes(void)
{
    for (int it = 0; it < 20000; it++)
    {
        RmCmd c;
        memset(&c, 0, sizeof(c));
        c.ctr = (rnd() % 4 == 0) ? 0 : (rnd() % 2 ? rnd() : 0xFFFFFFFFu - rnd() % 3);
        randStr(c.cmd, sizeof(c.cmd) - 1);
        randStr(c.args, sizeof(c.args) - 1);
        char dst[16], src[16], result[RM_MAX_RESULT + 1];
        randStr(dst, 12);
        randStr(src, 12);
        randStr(result, RM_MAX_RESULT);
        for (size_t n = 0; n <= 200; n++)
        {
            char a[210], b[210];
            memset(a, 'A', sizeof(a));
            memset(b, 'B', sizeof(b));
            size_t ra = rmref::canonical(c, dst, src, a, n);
            size_t rb = rmCanonical(c, dst, src, b, n);
            g_canon++;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(ra, rb, "rmCanonical length");
            if (ra)
                TEST_ASSERT_EQUAL_STRING_MESSAGE(a, b, "rmCanonical text");
            else if (n)
                TEST_ASSERT_EQUAL_CHAR_MESSAGE(0, b[0], "rmCanonical failure clears out[0]");
            ra = rmref::replyCanonical(c.ctr, dst, src, result, a, n);
            rb = rmReplyCanonical(c.ctr, dst, src, result, b, n);
            g_canon++;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(ra, rb, "rmReplyCanonical length");
            if (ra)
                TEST_ASSERT_EQUAL_STRING_MESSAGE(a, b, "rmReplyCanonical text");
            else if (n)
                TEST_ASSERT_EQUAL_CHAR_MESSAGE(0, b[0], "rmReplyCanonical failure clears out[0]");
            if (ra && n > ra + 2)
                break; // every larger buffer fits as well
        }
    }
    // null arguments: both fail the same way
    RmCmd c;
    memset(&c, 0, sizeof(c));
    char a[64], b[64];
    TEST_ASSERT_EQUAL_UINT32(rmref::canonical(c, nullptr, "S", a, sizeof(a)), rmCanonical(c, nullptr, "S", b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT32(rmref::canonical(c, "D", "S", nullptr, 0), rmCanonical(c, "D", "S", nullptr, 0));
}

// rmCommandAllowed() looks a name up by length and bytes now: every listed name is still found (with the args its
// shape accepts for a read), and names that differ by one byte, a missing or an extra character are not.
static void test_allowlist_lookup(void)
{
#define RM_NAME_ROW(n, k, shape) #n,
    static const char *const names[] = {RM_COMMAND_LIST(RM_NAME_ROW)};
#undef RM_NAME_ROW
    int found = 0;
    for (const char *n : names)
    {
        const bool any = rmCommandAllowed(n, "", 0, 99) || rmCommandAllowed(n, "on", 0, 99) ||
                         rmCommandAllowed(n, "5", 0, 99) || rmCommandAllowed(n, "a1 on", 0, 99);
        if (any)
            found++;
        char v[24];
        snprintf(v, sizeof(v), "%sx", n);
        TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(v, "", 0, 99) && rmCommandAllowed(v, "on", 0, 99), v);
        const size_t l = strlen(n);
        if (l > 1)
        {
            snprintf(v, sizeof(v), "%.*s", (int)(l - 1), n);
            bool clash = false;
            for (const char *m : names) clash = clash || strcmp(m, v) == 0;
            if (!clash)
                TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(v, "", 0, 99), v);
        }
    }
    // every name of the list resolves with at least one of the probe arguments
    TEST_ASSERT_EQUAL_INT((int)(sizeof(names) / sizeof(names[0])), found);
}

// outU32() finds the digits by subtraction now: the counter field of rmCanonical() must equal printf("%u") for every
// power of ten and its neighbours, the extremes, and two million random counters.
static void ctrBoth(uint32_t ctr)
{
    RmCmd c;
    memset(&c, 0, sizeof(c));
    c.ctr = ctr;
    strcpy(c.cmd, "info");
    char got[96], want[96];
    const size_t l = rmCanonical(c, "D", "S", got, sizeof(got));
    snprintf(want, sizeof(want), "RM1|D|S|%u|info", (unsigned)ctr);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, want);
    TEST_ASSERT_EQUAL_UINT32(strlen(want), l);
}

static void test_counter_digits(void)
{
    ctrBoth(0);
    ctrBoth(0xFFFFFFFFu);
    ctrBoth(0xFFFFFFFEu);
    ctrBoth(0x80000000u);
    ctrBoth(4000000000u);
    ctrBoth(3999999999u);
    for (uint64_t p = 1; p <= 1000000000u; p *= 10)
        for (int d = -2; d <= 2; d++)
            for (uint64_t m = 1; m <= 9; m++)
            {
                const int64_t v = (int64_t)(p * m) + d;
                if (v >= 0 && v <= 0xFFFFFFFFll)
                    ctrBoth((uint32_t)v);
            }
    for (uint32_t v = 0; v < 100000; v++)
        ctrBoth(v);
    for (int it = 0; it < 2000000; it++)
        ctrBoth((rnd() << 8) ^ rnd());
}

static void test_counts(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(400000, (uint32_t)g_parse);
    TEST_ASSERT_GREATER_THAN_UINT32(100000, (uint32_t)g_canon);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_edges);
    RUN_TEST(test_parse_frames);
    RUN_TEST(test_parse_free_text);
    RUN_TEST(test_canonical_sizes);
    RUN_TEST(test_allowlist_lookup);
    RUN_TEST(test_counter_digits);
    RUN_TEST(test_counts);
    return UNITY_END();
}
