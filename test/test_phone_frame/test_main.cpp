// test_phone_frame: phoneFrameDecode() (src/phone_frame.h) against the frozen parsing logic of the old
// readPhoneCommand() (phone_cmd_ref.h). FORMAL-02 wave 1.
//
// Categories, per frame (buf, len), msg_len = buf[0]:
//   A  msg_len <= len and the old code reads only buf[0..len): decoded command and arguments must be
//      identical.
//   B  the old code reads past len (a field, or the header, ends after the received bytes): the defect.
//      The test shows the old code's result depends on the bytes behind len (guard fill A5 vs 5A) and that
//      the new decode does not (non-interference); it never reads behind len.
//   C  msg_len > len: the header claims more than received. The new code must drop the frame
//      (PF_CLAIMS_MORE / PF_TOO_SHORT, command none).
// Inputs: generated well-formed frames of every command type, a sweep over every length 0..300 with every
// handled type and several header lengths, and named defect frames.
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "phone_frame.h"
#include "phone_cmd_ref.h"

void setUp(void) {}
void tearDown(void) {}

#define BUFSZ 512

static uint32_t g_rng = 0x1234567u;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static const uint8_t TYPES[] = {0x10, 0x20, 0x50, 0x70, 0x80, 0x90, 0x95, 0xA0, 0x55, 0xF0, 0x00, 0x11, 0xFF};

// Place frame bytes in a BUFSZ buffer: bytes [0,len) as given, the rest filled with guard.
static void place(uint8_t *dst, const uint8_t *frame, size_t len, uint8_t guard)
{
    memset(dst, guard, BUFSZ);
    if (len)
        memcpy(dst, frame, len);
}

// Normalise the new decode to the recorder form so both can be compared bit for bit.
static void toRec(const PhoneFrame &f, Rec &r)
{
    memset(&r, 0, sizeof(r));
    switch (f.cmd)
    {
    case PFC_NONE:
        r.cmd = RC_NONE;
        break;
    case PFC_HELLO:
        r.cmd = RC_HELLO;
        r.has_hash = f.has_hash;
        if (f.has_hash)
            memcpy(r.hash, f.u.hash, 32);
        break;
    case PFC_TIME:
        r.cmd = RC_TIME;
        r.ts = f.ts;
        break;
    case PFC_CALL:
        r.cmd = RC_CALL;
        r.dlen = f.dlen;
        memcpy(r.data, f.u.data, f.dlen);
        break;
    case PFC_LAT:
    case PFC_LON:
        r.cmd = (f.cmd == PFC_LAT) ? RC_LAT : RC_LON;
        r.fval = f.fval;
        r.save_flag = f.save_flag;
        break;
    case PFC_ALT:
        r.cmd = RC_ALT;
        r.ival = f.ival;
        r.save_flag = f.save_flag;
        break;
    case PFC_SYMBOL:
        r.cmd = RC_SYM;
        r.sym[0] = f.sym[0];
        r.sym[1] = f.sym[1];
        break;
    case PFC_TEXT:
        r.cmd = RC_TEXT;
        r.dash = f.dash;
        r.dlen = f.dlen;
        memcpy(r.data, f.u.data, f.dlen);
        break;
    case PFC_WIFI:
    {
        r.cmd = RC_WIFI;
        r.ssid_len = f.ssid_len;
        r.pwd_len = f.pwd_len;
        // the action clamps to sizeof(node_ssid) = 33 and sizeof(node_pwd) = 64
        size_t sl = f.ssid_len, pl = f.pwd_len;
        memcpy(r.ssid, f.u.wifi.ssid, sl < 32 ? sl : 32);
        memcpy(r.pwd, f.u.wifi.pwd, pl < 63 ? pl : 63);
        break;
    }
    case PFC_SAVE_RESET:
        r.cmd = RC_SAVE;
        break;
    }
}

static unsigned long g_catA[16], g_catAtotal, g_catB, g_catBrefDepends, g_catBrefExtentOver, g_catBnewOk, g_catC,
    g_catCrefExtentOver, g_catCrefAccepted, g_newDepends, g_newNotRejected;

static const char *statusName(int s)
{
    static const char *n[] = {"OK", "TOO_SHORT", "TOO_LONG", "CLAIMS_MORE", "TRUNCATED"};
    return (s >= 0 && s < 5) ? n[s] : "?";
}

// Classify and check one frame. frame has `len` bytes.
static void check(const uint8_t *frame, size_t len, const char *what)
{
    uint8_t bufA[BUFSZ], bufB[BUFSZ];
    place(bufA, frame, len, 0xA5);
    place(bufB, frame, len, 0x5A);

    Rec ra, rb;
    size_t extA = refReadPhoneCommand(bufA, ra);
    size_t extB = refReadPhoneCommand(bufB, rb);
    (void)extB;

    PhoneFrame fa, fb;
    phoneFrameDecode(bufA, len, &fa);
    phoneFrameDecode(bufB, len, &fb);
    Rec na, nb;
    toRec(fa, na);
    toRec(fb, nb);

    char msg[160];
    snprintf(msg, sizeof(msg), "%s: len %u msg_len %u type 0x%02X: new decode depends on bytes behind len", what,
             (unsigned)len, (unsigned)bufA[0], (unsigned)bufA[1]);
    if (fa.status != fb.status || memcmp(&na, &nb, sizeof(na)) != 0)
    {
        g_newDepends++;
        TEST_FAIL_MESSAGE(msg);
    }

    const bool over = extA > len;
    const bool claims = len >= 1 && (size_t)frame[0] > len;
    if (len < 2 || claims)
    {
        // C (and the header-less tiny frames): must be dropped
        g_catC++;
        if (over)
            g_catCrefExtentOver++;
        else
            g_catCrefAccepted++;
        snprintf(msg, sizeof(msg), "%s: len %u msg_len %u status %s, expected drop", what, (unsigned)len,
                 len ? (unsigned)frame[0] : 0u, statusName(fa.status));
        if (fa.status == PF_OK || fa.cmd != PFC_NONE)
        {
            g_newNotRejected++;
            TEST_FAIL_MESSAGE(msg);
        }
        if (over && memcmp(&ra, &rb, sizeof(ra)) != 0)
            g_catBrefDepends++;
        return;
    }

    if (over)
    {
        g_catB++;
        g_catBrefExtentOver++;
        if (memcmp(&ra, &rb, sizeof(ra)) != 0)
            g_catBrefDepends++;
        if (fa.status == PF_OK)
            g_catBnewOk++; // decoded without the missing bytes: allowed (e.g. text shorter than 2), not reading them
        return;
    }

    // category A
    g_catAtotal++;
    if (ra.cmd >= 0 && ra.cmd < 16)
        g_catA[ra.cmd]++;
    snprintf(msg, sizeof(msg), "%s: len %u msg_len %u type 0x%02X: decode differs from reference (status %s)", what,
             (unsigned)len, (unsigned)bufA[0], (unsigned)bufA[1], statusName(fa.status));
    TEST_ASSERT_EQUAL_MESSAGE(PF_OK, fa.status, msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(&ra, &na, sizeof(ra)), msg);
}

static void resetStats(void)
{
    memset(g_catA, 0, sizeof(g_catA));
    g_catAtotal = g_catB = g_catBrefDepends = g_catBrefExtentOver = g_catBnewOk = g_catC = g_catCrefExtentOver =
        g_catCrefAccepted = g_newDepends = g_newNotRejected = 0;
}

// --- generated well-formed frames --------------------------------------------------------------------------

static void fill(uint8_t *f, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++)
        f[i] = (uint8_t)rnd();
}

static void test_wellformed_every_command(void)
{
    resetStats();
    uint8_t f[BUFSZ];
    size_t n = 0;

    // hello, with and without hash, trailing bytes after msg_len
    for (int rep = 0; rep < 20; rep++)
        for (size_t ml = 4; ml <= 40; ml++)
            for (size_t extra = 0; extra < 3; extra++)
            {
                size_t len = ml + extra;
                fill(f, 0, len);
                f[0] = (uint8_t)ml; f[1] = 0x10; f[2] = 0x20; f[3] = 0x30;
                if (ml == 4 && rep == 0) { f[2] = 0; } // hello mismatch is ignored by both
                if (ml >= 35 && len < 36) continue;   // would read past len: category B
                check(f, len, "hello"); n++;
            }
    // time
    for (int rep = 0; rep < 50; rep++)
        for (size_t len = 6; len <= 9; len++) { fill(f, 0, len); f[0] = (uint8_t)len; f[1] = 0x20; check(f, len, "time"); n++; }
    // callsign: every payload length 0..255
    for (size_t pl = 0; pl <= 255; pl++)
        for (int rep = 0; rep < 3; rep++)
        {
            size_t len = 3 + pl;
            fill(f, 0, len);
            f[0] = (uint8_t)(len > 255 ? 255 : len); f[1] = 0x50; f[2] = (uint8_t)pl;
            if (pl == 0) { /* n = 0: old VLA stays uninitialised, no bytes to compare */ }
            check(f, len, "call"); n++;
        }
    // lat / lon / alt, every save flag value, with and without trailing bytes
    const uint8_t types3[] = {0x70, 0x80, 0x90};
    const uint8_t flags[] = {0x0A, 0x0B, 0x00, 0xFF};
    for (size_t t = 0; t < 3; t++)
        for (size_t fl = 0; fl < 4; fl++)
            for (int rep = 0; rep < 30; rep++)
                for (size_t extra = 0; extra < 2; extra++)
                {
                    size_t len = 7 + extra;
                    fill(f, 0, len);
                    f[0] = 7; f[1] = types3[t]; f[6] = flags[fl];
                    check(f, len, "position"); n++;
                }
    // symbol
    for (int rep = 0; rep < 100; rep++)
    {
        fill(f, 0, 5); f[0] = 4; f[1] = 0x95; f[2] = (rep & 1) ? 0x2f : 0x5c;
        check(f, 4, "symbol"); n++;
    }
    // text: every length 0..253, plain and "--"
    for (size_t tl = 0; tl <= 253; tl++)
        for (int dash = 0; dash < 2; dash++)
        {
            size_t len = 2 + tl;
            fill(f, 0, len);
            f[0] = (uint8_t)len; f[1] = 0xA0;
            if (dash && tl >= 2) { f[2] = '-'; f[3] = '-'; }
            check(f, len, "text"); n++;
        }
    // wifi: ssid 1..40, pwd 1..70, plus zero lengths
    for (size_t sl = 0; sl <= 40; sl += (sl < 5 ? 1 : 7))
        for (size_t pl = 0; pl <= 70; pl += (pl < 5 ? 1 : 11))
        {
            size_t len = 4 + sl + pl;
            fill(f, 0, len);
            f[0] = (uint8_t)len; f[1] = 0x55; f[2] = (uint8_t)sl; f[3 + sl] = (uint8_t)pl;
            check(f, len, "wifi"); n++;
        }
    // save+reset and unknown types
    for (size_t len = 2; len <= 5; len++) { fill(f, 0, len); f[0] = (uint8_t)len; f[1] = 0xF0; check(f, len, "save"); n++; }
    for (int t = 0; t < 256; t++)
        if (t != 0x10 && t != 0x20 && t != 0x50 && t != 0x55 && t != 0x70 && t != 0x80 && t != 0x90 && t != 0x95 && t != 0xA0 && t != 0xF0)
        {
            fill(f, 0, 12); f[0] = 12; f[1] = (uint8_t)t; check(f, 12, "unknown"); n++;
        }

    printf("wellformed: %lu frames, A=%lu (hello %lu time %lu call %lu lat %lu lon %lu alt %lu sym %lu text %lu wifi %lu save %lu none %lu)\n",
           (unsigned long)n, g_catAtotal, g_catA[RC_HELLO], g_catA[RC_TIME], g_catA[RC_CALL], g_catA[RC_LAT],
           g_catA[RC_LON], g_catA[RC_ALT], g_catA[RC_SYM], g_catA[RC_TEXT], g_catA[RC_WIFI], g_catA[RC_SAVE], g_catA[RC_NONE]);
    printf("wellformed: old code reads past len in %lu of them (text shorter than 2 bytes: the dash check reads [3]; a 2-byte frame: [2] is read unconditionally)\n", g_catB);
    TEST_ASSERT_EQUAL_UINT(0, g_catC);
    TEST_ASSERT_EQUAL_UINT(n, g_catAtotal + g_catB);
    TEST_ASSERT_TRUE(g_catB >= 1 && g_catB <= 8);
    TEST_ASSERT_TRUE(g_catA[RC_HELLO] >= 600);
    TEST_ASSERT_TRUE(g_catA[RC_TIME] >= 200);
    TEST_ASSERT_TRUE(g_catA[RC_CALL] >= 768);
    TEST_ASSERT_TRUE(g_catA[RC_LAT] >= 240);
    TEST_ASSERT_TRUE(g_catA[RC_LON] >= 240);
    TEST_ASSERT_TRUE(g_catA[RC_ALT] >= 240);
    TEST_ASSERT_TRUE(g_catA[RC_SYM] >= 100);
    TEST_ASSERT_TRUE(g_catA[RC_TEXT] >= 500);
    TEST_ASSERT_TRUE(g_catA[RC_WIFI] >= 50);
    TEST_ASSERT_TRUE(g_catA[RC_SAVE] >= 3);
    TEST_ASSERT_TRUE(g_catA[RC_NONE] >= 240);
}

// --- sweep: every length 0..300, every handled type, several header lengths -----------------------------------

static void test_sweep_all_lengths(void)
{
    resetStats();
    uint8_t f[BUFSZ];
    unsigned long n = 0;
    for (size_t len = 0; len <= 300; len++)
        for (size_t t = 0; t < sizeof(TYPES); t++)
            for (int ml = 0; ml < 7; ml++)
                for (int rep = 0; rep < 4; rep++)
                {
                    fill(f, 0, len);
                    if (len > 1)
                    {
                        uint8_t m;
                        switch (ml)
                        {
                        case 0: m = (uint8_t)(len > 255 ? 255 : len); break;
                        case 1: m = 0; break;
                        case 2: m = 1; break;
                        case 3: m = (uint8_t)(len - 1); break;
                        case 4: m = (uint8_t)(len + 1 > 255 ? 255 : len + 1); break;
                        case 5: m = 255; break;
                        default: m = (uint8_t)rnd(); break;
                        }
                        f[0] = m;
                        f[1] = TYPES[t];
                        // keep lengths inside the frame most of the time so fields are reachable
                        if (rep & 1) f[2] = (uint8_t)(rnd() % 40);
                        if (len > 3 && rep == 2) { f[2] = 0x20; f[3] = 0x30; }
                        if (len > 6 && rep == 3) f[6] = 0x0A;
                        if (len > 3 && TYPES[t] == 0xA0 && rep == 3) { f[2] = '-'; f[3] = '-'; }
                    }
                    else if (len == 1)
                        f[0] = (uint8_t)rnd();
                    check(f, len, "sweep");
                    n++;
                }
    printf("sweep: %lu frames: A=%lu B(old reads past len, header consistent)=%lu C(header claims more or len<2)=%lu; "
           "old code read past len in %lu frames, its result changed with the bytes behind len in %lu; new decode depends on them in %lu\n",
           n, g_catAtotal, g_catB, g_catC, g_catBrefExtentOver + g_catCrefExtentOver, g_catBrefDepends, g_newDepends);
    TEST_ASSERT_EQUAL_UINT(0, g_newDepends);
    TEST_ASSERT_EQUAL_UINT(0, g_newNotRejected);
    TEST_ASSERT_TRUE(g_catAtotal >= 5000);
    TEST_ASSERT_TRUE(g_catB >= 200);
    TEST_ASSERT_TRUE(g_catC >= 10000);
    TEST_ASSERT_TRUE(g_catCrefExtentOver >= 1000); // the old code reads past len when the header claims more
    TEST_ASSERT_TRUE(g_catBrefDepends >= 100);     // ... and its result then depends on bytes it was never sent
    TEST_ASSERT_TRUE(g_catBnewOk <= g_catB);
    for (int c = RC_HELLO; c <= RC_SAVE; c++)
        TEST_ASSERT_TRUE_MESSAGE(g_catA[c] >= 50, "sweep category A minimum per command");
}

// --- named defect frames: header claims more than received -------------------------------------------------

static void defect(const uint8_t *frame, size_t len, const char *what, bool reject = true)
{
    uint8_t buf[BUFSZ];
    place(buf, frame, len, 0xA5);
    Rec r;
    size_t ext = refReadPhoneCommand(buf, r);
    char msg[120];
    snprintf(msg, sizeof(msg), "%s: old code read %u bytes of %u received", what, (unsigned)ext, (unsigned)len);
    printf("defect %-28s received %3u, old code read %3u (%s)\n", what, (unsigned)len, (unsigned)ext, ext > len ? "OVERREAD" : "in range");
    TEST_ASSERT_TRUE_MESSAGE(ext > len, msg);
    PhoneFrame d;
    phoneFrameDecode(buf, len, &d);
    if (reject)
    {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(PF_OK, d.status, what);
        TEST_ASSERT_EQUAL_MESSAGE(PFC_NONE, d.cmd, what);
    }
    else
    {
        // the value fits, only the optional trailing field (flag byte / hash) is missing: decoded without it
        TEST_ASSERT_EQUAL_MESSAGE(PF_OK, d.status, what);
        TEST_ASSERT_FALSE_MESSAGE(d.save_flag || d.has_hash, what);
    }
}

static void test_named_defects(void)
{
    uint8_t f[BUFSZ];
    memset(f, 0, sizeof(f));
    // callsign: payload len 200, three bytes sent
    f[0] = 3; f[1] = 0x50; f[2] = 200; defect(f, 3, "callsign n=200, 3 bytes sent");
    // callsign with a consistent msg_len but n beyond len
    memset(f, 0, sizeof(f)); f[0] = 6; f[1] = 0x50; f[2] = 200; defect(f, 6, "callsign n=200, msg_len 6");
    // position frame without the save flag byte
    memset(f, 0, sizeof(f)); f[0] = 6; f[1] = 0x70; defect(f, 6, "latitude, no save flag byte", false);
    // position frame cut at the type byte
    memset(f, 0, sizeof(f)); f[0] = 2; f[1] = 0x90; defect(f, 2, "altitude, 2 bytes sent");
    // timestamp cut
    memset(f, 0, sizeof(f)); f[0] = 4; f[1] = 0x20; defect(f, 4, "timestamp, 4 bytes sent");
    // hello claiming a hash, 35 bytes sent
    memset(f, 0, sizeof(f)); f[0] = 35; f[1] = 0x10; f[2] = 0x20; f[3] = 0x30; defect(f, 35, "hello hash, 35 bytes sent", false);
    // hello cut after the type byte
    memset(f, 0, sizeof(f)); f[0] = 2; f[1] = 0x10; f[2] = 0x20; defect(f, 2, "hello, 2 bytes sent");
    // text claiming 250 bytes
    memset(f, 0, sizeof(f)); f[0] = 250; f[1] = 0xA0; defect(f, 5, "text msg_len=250, 5 bytes sent");
    // symbol cut
    memset(f, 0, sizeof(f)); f[0] = 3; f[1] = 0x95; defect(f, 3, "symbol, 3 bytes sent");
    // one byte only: header type is read behind len
    f[0] = 1; defect(f, 1, "1 byte sent");
    // wifi: msg_len says 40, 10 sent
    memset(f, 0, sizeof(f)); f[0] = 40; f[1] = 0x55; f[2] = 20; f[23] = 10; defect(f, 10, "wifi msg_len=40, 10 sent");
}

static void test_len_limits(void)
{
    uint8_t f[BUFSZ];
    memset(f, 0, sizeof(f));
    PhoneFrame d;
    phoneFrameDecode(f, 0, &d);
    TEST_ASSERT_EQUAL(PF_TOO_SHORT, d.status);
    phoneFrameDecode(f, 1, &d);
    TEST_ASSERT_EQUAL(PF_TOO_SHORT, d.status);
    f[0] = 2; f[1] = 0xF0;
    phoneFrameDecode(f, 301, &d);
    TEST_ASSERT_EQUAL(PF_TOO_LONG, d.status);
    phoneFrameDecode(f, 300, &d);
    TEST_ASSERT_EQUAL(PF_OK, d.status);
    TEST_ASSERT_EQUAL(PFC_SAVE_RESET, d.cmd);
    // text field: out buffer is NUL-terminated at the longest text
    memset(f, 'x', sizeof(f)); f[0] = 255; f[1] = 0xA0;
    phoneFrameDecode(f, 300, &d);
    TEST_ASSERT_EQUAL(253, d.dlen);
    TEST_ASSERT_EQUAL(0, d.u.data[253]);
    // callsign n = 255
    memset(f, 'y', sizeof(f)); f[0] = 255; f[1] = 0x50; f[2] = 255;
    phoneFrameDecode(f, 258, &d);
    TEST_ASSERT_EQUAL(255, d.dlen);
    TEST_ASSERT_EQUAL(0, d.u.data[255]);
    phoneFrameDecode(f, 257, &d);
    TEST_ASSERT_EQUAL(PF_TRUNCATED, d.status);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_wellformed_every_command);
    RUN_TEST(test_sweep_all_lengths);
    RUN_TEST(test_named_defects);
    RUN_TEST(test_len_limits);
    return UNITY_END();
}
