// RM extended commands, contract C3: src/rm_format.h (pure reply formatters, header-only, no Arduino).
//
//   pio test -e native_rm_format
//
// Covers the exact reply examples of docs/rm-gui/extended-commands-concept.md section 3, one sentinel
// value per field (a swapped field order fails), rounding, NaN/inf handling, the MHEARD page fitting
// rules, too-small buffers, the worst-case body length per formatter (printed) and the output charset.

#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rm_format.h>

void setUp(void) {}
void tearDown(void) {}

static char g_buf[256];

static void expectStr(const char *want, size_t got)
{
    TEST_ASSERT_EQUAL_STRING(want, g_buf);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(want), (uint32_t)got);
}

static bool charsetOk(const char *s)
{
    for (; *s != '\0'; s++)
    {
        char c = *s;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
              c == '.' || c == '/' || c == '=' || c == '+' || c == '_' || c == '@' || c == '?' || c == '(' || c == ')' ||
              c == ',' || c == '*' || c == '#'))
            return false;
    }
    return true;
}

// Worst case length including the "ok " prefix, printed for comparison with the concept's table.
// `want` is the EXACT expected body, so a dropped, reordered or renamed key fails (not only length/charset).
static void reportWorst(const char *name, size_t len, const char *text, const char *want)
{
    TEST_ASSERT_EQUAL_STRING(want, text);
    printf("WORST %-7s body=%u wire_ok=%u  %s\n", name, (unsigned)len, (unsigned)len + 3u, text);
    TEST_ASSERT_TRUE(len <= RM_FMT_BODY_MAX);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(text), (uint32_t)len);
    TEST_ASSERT_TRUE(charsetOk(text));
}

// ---- count --------------------------------------------------------------------------------------------------

static void test_count(void)
{
    struct { uint32_t v; const char *want; } t[] = {
        {0, "0"}, {99999, "99999"}, {100000, "100k"}, {123456, "123k"}, {9999999, "9999k"},
        {10000000, "10M"}, {1999999999u, "1999M"}, {UINT32_MAX, "4294M"}};
    char c[6];
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    {
        size_t l = rmFmtCount(t[i].v, c);
        TEST_ASSERT_EQUAL_STRING(t[i].want, c);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(t[i].want), (uint32_t)l);
        TEST_ASSERT_TRUE(l <= 5);
    }
}

// ---- radio --------------------------------------------------------------------------------------------------

static void test_radio_example_and_bw(void)
{
    RmRadioIn r = {433.175f, 11, 5, 250.0f, 10, 22};
    expectStr("f=433.175 sf=11 cr=5 bw=250 p=10/22 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
    r.bwKHz = 125.0f;
    expectStr("f=433.175 sf=11 cr=5 bw=125 p=10/22 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
    r.bwKHz = 62.5f;
    expectStr("f=433.175 sf=11 cr=5 bw=62.5 p=10/22 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
    r.bwKHz = 31.25f;
    expectStr("f=433.175 sf=11 cr=5 bw=31.25 p=10/22 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
    r.bwKHz = 7.8f;
    expectStr("f=433.175 sf=11 cr=5 bw=7.8 p=10/22 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
}

static void test_radio_sentinels(void)
{
    // one DISTINCT sentinel per field: any swap of two fields fails
    RmRadioIn r = {868.125f, 12, 6, 62.5f, -3, 27};
    expectStr("f=868.125 sf=12 cr=6 bw=62.5 p=-3/27 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), r));
    // non-finite freq or bw: no body, out[0] == '\0'
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < 3; i++)
    {
        RmRadioIn f = r;
        f.freqMHz = bad[i];
        strcpy(g_buf, "x");
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rmFmtRadio(g_buf, sizeof(g_buf), f));
        TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)g_buf[0]);
        RmRadioIn w = r;
        w.bwKHz = bad[i];
        strcpy(g_buf, "x");
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rmFmtRadio(g_buf, sizeof(g_buf), w));
        TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)g_buf[0]);
    }
    // extreme finite / integer values clamp to the documented domain
    RmRadioIn e = {1e30f, INT32_MAX, INT32_MIN, 1e30f, INT32_MIN, INT32_MAX};
    expectStr("f=999.999 sf=99 cr=0 bw=999.99 p=-99/99 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), e));
    RmRadioIn z = {-0.0f, INT32_MIN, INT32_MAX, -0.0f, 0, 0};
    expectStr("f=0.000 sf=0 cr=99 bw=0 p=0/0 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), z));
    RmRadioIn neg = {-1e30f, -5, 100, -3.0f, 99, -99};
    expectStr("f=0.000 sf=0 cr=99 bw=0 p=99/-99 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), neg));
}

static void test_radio_rounding(void)
{
    RmRadioIn r = {0, 11, 5, 250.0f, 10, 22};
    r.freqMHz = (float)433.1749999;
    rmFmtRadio(g_buf, sizeof(g_buf), r);
    TEST_ASSERT_EQUAL_STRING("f=433.175 sf=11 cr=5 bw=250 p=10/22 pmin=0", g_buf);
    r.freqMHz = (float)433.17549;
    rmFmtRadio(g_buf, sizeof(g_buf), r);
    TEST_ASSERT_EQUAL_STRING("f=433.175 sf=11 cr=5 bw=250 p=10/22 pmin=0", g_buf);
    r.freqMHz = (float)869.5249;
    rmFmtRadio(g_buf, sizeof(g_buf), r);
    TEST_ASSERT_EQUAL_STRING("f=869.525 sf=11 cr=5 bw=250 p=10/22 pmin=0", g_buf);
    r.freqMHz = (float)433.1744;  // rounds down, must not become .175
    rmFmtRadio(g_buf, sizeof(g_buf), r);
    TEST_ASSERT_EQUAL_STRING("f=433.174 sf=11 cr=5 bw=250 p=10/22 pmin=0", g_buf);
}

// ---- name / atxt --------------------------------------------------------------------------------------------

static void test_name_atxt(void)
{
    expectStr("n=Martin", rmFmtName(g_buf, sizeof(g_buf), "Martin"));
    expectStr("n=-", rmFmtName(g_buf, sizeof(g_buf), ""));
    expectStr("n=-", rmFmtName(g_buf, sizeof(g_buf), nullptr));
    expectStr("a=MeshCom Garten", rmFmtAtxt(g_buf, sizeof(g_buf), "MeshCom Garten"));
    expectStr("a=-", rmFmtAtxt(g_buf, sizeof(g_buf), ""));
    // longer than the documented maximum: never a truncated text
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtName(g_buf, sizeof(g_buf), "123456789012345678901"));
    // DRY-04: the formatter limits are the derived constants; literals pin the wire contract (19 / 39)
    TEST_ASSERT_EQUAL_UINT(19, RM_NAME_MAX);
    TEST_ASSERT_EQUAL_UINT(39, RM_ATXT_MAX);
    TEST_ASSERT_TRUE(rmFmtName(g_buf, sizeof(g_buf), "1234567890123456789") > 0);   // 19 chars
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtName(g_buf, sizeof(g_buf), "12345678901234567890")); // 20 chars
    TEST_ASSERT_TRUE(rmFmtAtxt(g_buf, sizeof(g_buf), "123456789012345678901234567890123456789") > 0);   // 39
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtAtxt(g_buf, sizeof(g_buf), "1234567890123456789012345678901234567890")); // 40
    TEST_ASSERT_EQUAL_CHAR('\0', g_buf[0]);
}

// ---- pos ----------------------------------------------------------------------------------------------------

static void test_pos(void)
{
    RmPosIn p = {48.408124, 11.738124, 492, RM_POS_GPS};
    expectStr("48.40812 11.73812 492 gps", rmFmtPos(g_buf, sizeof(g_buf), p));
    p.lat = 48.408126;
    p.src = RM_POS_NOFIX;
    expectStr("48.40813 11.73812 492 nofix", rmFmtPos(g_buf, sizeof(g_buf), p));
    p.src = RM_POS_SET;
    p.lat = -33.5;
    p.lon = -70.25;
    p.alt = -5;
    expectStr("-33.50000 -70.25000 -5 set", rmFmtPos(g_buf, sizeof(g_buf), p));
    p.lat = -0.000001;  // rounds to zero: no "-0.00000"
    p.lon = 0.0;
    p.alt = 0;
    expectStr("0.00000 0.00000 0 set", rmFmtPos(g_buf, sizeof(g_buf), p));
    // non-finite lat or lon: no body (the dispatcher answers an error), out[0] == '\0'
    const double bad[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < 3; i++)
    {
        strcpy(g_buf, "x");
        p.lat = bad[i];
        p.lon = 11.0;
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rmFmtPos(g_buf, sizeof(g_buf), p));
        TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)g_buf[0]);
        strcpy(g_buf, "x");
        p.lat = 48.0;
        p.lon = bad[i];
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)rmFmtPos(g_buf, sizeof(g_buf), p));
        TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)g_buf[0]);
    }
}

// ---- sens ---------------------------------------------------------------------------------------------------

static void test_sens(void)
{
    RmSensIn s = {true, true, true, false, 21.4f, 45.0f, 1013.2f, 0.0f};
    expectStr("t=21.4 h=45 p=1013.2 t2=-", rmFmtSens(g_buf, sizeof(g_buf), s));
    RmSensIn q = {true, true, true, true, -5.26f, 45.5f, 998.76f, 7.04f};
    expectStr("t=-5.3 h=46 p=998.8 t2=7.0", rmFmtSens(g_buf, sizeof(g_buf), q));
    RmSensIn none = {false, false, false, false, 1, 2, 3, 4};
    expectStr("t=- h=- p=- t2=-", rmFmtSens(g_buf, sizeof(g_buf), none));
    RmSensIn bad = {true, true, true, true, NAN, INFINITY, -INFINITY, NAN};
    expectStr("t=- h=- p=- t2=-", rmFmtSens(g_buf, sizeof(g_buf), bad));
    RmSensIn zero = {true, true, true, true, 0.0f, 0.0f, 0.0f, 0.0f};  // present zero is not absent
    expectStr("t=0.0 h=0 p=0.0 t2=0.0", rmFmtSens(g_buf, sizeof(g_buf), zero));
}

// ---- mh page ------------------------------------------------------------------------------------------------

static RmMhRow g_rows[12];

static void fillRows(uint8_t cnt, uint16_t age)
{
    for (uint8_t i = 0; i < cnt; i++)
    {
        memset(&g_rows[i], 0, sizeof(g_rows[i]));
        snprintf(g_rows[i].call, sizeof(g_rows[i].call), "DK5EN-%03u", (unsigned)(i + 1));  // 9 chars
        g_rows[i].ageMin = age;
    }
}

static void test_mh_page_empty_and_one(void)
{
    uint8_t em = 99;
    RmMhPageIn in = {0, 0, nullptr, 0};
    expectStr("0 -", rmFmtMhPage(g_buf, sizeof(g_buf), in, &em));
    TEST_ASSERT_EQUAL_UINT8(0, em);
    strcpy(g_rows[0].call, "DL2JA-2");
    g_rows[0].ageMin = 0;
    RmMhPageIn one = {1, 0, g_rows, 1};
    expectStr("1 - DL2JA-2 0", rmFmtMhPage(g_buf, sizeof(g_buf), one, &em));
    TEST_ASSERT_EQUAL_UINT8(1, em);
    // concept example shape
    strcpy(g_rows[1].call, "DK5EN-98");
    g_rows[1].ageMin = 12;
    RmMhPageIn two = {23, 0, g_rows, 2};
    expectStr("23 2 DL2JA-2 0 DK5EN-98 12", rmFmtMhPage(g_buf, sizeof(g_buf), two, &em));
    TEST_ASSERT_EQUAL_UINT8(2, em);
}

static void test_mh_page_fit(void)
{
    uint8_t em = 0;
    fillRows(8, 180);  // 9-char calls, 3 h window = 3-digit ages
    // total 999 / first 100: "999 107" + 7 * 14 = exactly 105
    RmMhPageIn in = {999, 100, g_rows, 8};
    size_t l = rmFmtMhPage(g_buf, sizeof(g_buf), in, &em);
    TEST_ASSERT_EQUAL_UINT32(105, l);
    TEST_ASSERT_EQUAL_UINT8(7, em);
    TEST_ASSERT_EQUAL_STRING_LEN("999 107 DK5EN-001 180", g_buf, 21);
    TEST_ASSERT_NULL(strstr(g_buf, "DK5EN-008"));
    reportWorst("mhpage", l, g_buf,
                "999 107 DK5EN-001 180 DK5EN-002 180 DK5EN-003 180 DK5EN-004 180 DK5EN-005 180 DK5EN-006 180 DK5EN-007 180");
    // only 7 offered at the end of the list: next is '-'
    RmMhPageIn last = {999, 992, g_rows, 7};
    l = rmFmtMhPage(g_buf, sizeof(g_buf), last, &em);
    TEST_ASSERT_EQUAL_UINT8(7, em);
    TEST_ASSERT_EQUAL_STRING_LEN("999 - DK5EN-001 180", g_buf, 19);
    // 5-digit ages: fewer rows fit, never more than the budget
    fillRows(8, 65535);
    RmMhPageIn big = {65535, 60000, g_rows, 8};
    l = rmFmtMhPage(g_buf, sizeof(g_buf), big, &em);
    TEST_ASSERT_TRUE(l <= RM_FMT_BODY_MAX);
    TEST_ASSERT_EQUAL_UINT8(5, em);
    TEST_ASSERT_EQUAL_STRING_LEN("65535 60005 DK5EN-001 65535", g_buf, 27);
}

static void test_mh_page_first_beyond(void)
{
    uint8_t em = 77;
    fillRows(2, 5);
    RmMhPageIn in = {10, 10, g_rows, 2};
    expectStr("10 -", rmFmtMhPage(g_buf, sizeof(g_buf), in, &em));
    TEST_ASSERT_EQUAL_UINT8(0, em);
    RmMhPageIn in2 = {10, 40, g_rows, 2};
    expectStr("10 -", rmFmtMhPage(g_buf, sizeof(g_buf), in2, nullptr));
}

static void test_mh_page_count_zero_and_calls(void)
{
    uint8_t em = 55;
    fillRows(2, 5);
    // count 0: identical output with rows NULL or not
    RmMhPageIn a = {10, 3, g_rows, 0};
    expectStr("10 3", rmFmtMhPage(g_buf, sizeof(g_buf), a, &em));
    TEST_ASSERT_EQUAL_UINT8(0, em);
    RmMhPageIn b = {10, 3, nullptr, 0};
    expectStr("10 3", rmFmtMhPage(g_buf, sizeof(g_buf), b, &em));
    RmMhPageIn e = {0, 0, g_rows, 0};
    expectStr("0 -", rmFmtMhPage(g_buf, sizeof(g_buf), e, &em));
    RmMhPageIn x = {10, 10, g_rows, 0};
    expectStr("10 -", rmFmtMhPage(g_buf, sizeof(g_buf), x, &em));
    // row calls: fold, out-of-set bytes -> '?', > 9 chars -> '?', empty -> '-'
    memset(g_rows, 0, sizeof(g_rows));
    strcpy(g_rows[0].call, "dk5en:x");
    strcpy(g_rows[1].call, "a\nb\xc6");
    memset(g_rows[2].call, 'Q', sizeof(g_rows[2].call));  // 10 chars, no NUL
    g_rows[3].call[0] = '\0';
    for (int i = 0; i < 4; i++)
        g_rows[i].ageMin = (uint16_t)i;
    RmMhPageIn c = {4, 0, g_rows, 4};
    expectStr("4 - DK5EN?X 0 A?B? 1 ? 2 - 3", rmFmtMhPage(g_buf, sizeof(g_buf), c, &em));
    TEST_ASSERT_EQUAL_UINT8(4, em);
}

// ---- mh direct / route --------------------------------------------------------------------------------------

static void test_mh_direct(void)
{
    RmMhDirectIn d = {true, true, true, -95, true, 8, true, 48.4231, 11.7871, true, 4.0f, true, 499, 15, 14, 18, 0};
    expectStr("d g=1 m=1 r=-95 s=8 la=48.4231 lo=11.7871 di=4.0 a=499 n=15 x=14 h=18 t=0",
              rmFmtMhDirect(g_buf, sizeof(g_buf), d));
    RmMhDirectIn s = {false, true, true, -101, true, -7, true, -33.12345, -70.98765, true, 1234.56f, true, -12, 3, 4, 5, 12345};
    expectStr("d g=0 m=1 r=-101 s=-7 la=-33.1235 lo=-70.9877 di=1234.6 a=-12 n=3 x=4 h=5 t=12345",
              rmFmtMhDirect(g_buf, sizeof(g_buf), s));
    RmMhDirectIn none = {false, false, false, 1, false, 2, false, 1, 2, false, 3, false, 4, 0, 0, 0, 1};
    expectStr("d g=0 m=0 r=- s=- la=- lo=- di=- a=- n=0 x=0 h=0 t=1", rmFmtMhDirect(g_buf, sizeof(g_buf), none));
    RmMhDirectIn nan = {true, true, true, 0, true, 0, true, NAN, NAN, true, NAN, true, 0, 0, 0, 0, 0};
    rmFmtMhDirect(g_buf, sizeof(g_buf), nan);
    TEST_ASSERT_EQUAL_STRING("d g=1 m=1 r=0 s=0 la=- lo=- di=- a=0 n=0 x=0 h=0 t=0", g_buf);
    // one non-finite coordinate is enough: both print `-`; inf distance -> di=-
    RmMhDirectIn half = {true, true, true, 0, true, 0, true, 48.0, INFINITY, true, INFINITY, true, 0, 0, 0, 0, 0};
    rmFmtMhDirect(g_buf, sizeof(g_buf), half);
    TEST_ASSERT_EQUAL_STRING("d g=1 m=1 r=0 s=0 la=- lo=- di=- a=0 n=0 x=0 h=0 t=0", g_buf);
    half.lat = -INFINITY;
    half.lon = 11.0;
    half.distKm = 2.5f;
    rmFmtMhDirect(g_buf, sizeof(g_buf), half);
    TEST_ASSERT_EQUAL_STRING("d g=1 m=1 r=0 s=0 la=- lo=- di=2.5 a=0 n=0 x=0 h=0 t=0", g_buf);
    RmMhDirectIn w = {true, true, true, -140, true, -20, true, -89.99999, -179.99999, true, 9999.94f, true, 40000, 255, 255, 255, UINT16_MAX};
    size_t l = rmFmtMhDirect(g_buf, sizeof(g_buf), w);
    reportWorst("direct", l, g_buf,
                "d g=1 m=1 r=-140 s=-20 la=-90.0000 lo=-180.0000 di=9999.9 a=40000 n=255 x=255 h=255 t=65535");
}

static void test_mh_route(void)
{
    const char *via[] = {"DK5EN-98", "DL2JA-2"};
    RmMhRouteIn r = {3, 2, false, false, false, true, 17.8f, "DL2JA-2", 3, via, 2};
    expectStr("r h=3 k=2 g=0 m=- rc=17.8@DL2JA-2 t=3 v=DK5EN-98,DL2JA-2", rmFmtMhRoute(g_buf, sizeof(g_buf), r));
    RmMhRouteIn q = {7, 9, true, true, true, false, 0.0f, "", 65535, via, 0};
    expectStr("r h=7 k=9 g=1 m=1 rc=- t=65535 v=-", rmFmtMhRoute(g_buf, sizeof(g_buf), q));
    RmMhRouteIn z = {1, 1, true, true, false, true, -5.04f, "A", 0, via, 1};
    expectStr("r h=1 k=1 g=1 m=0 rc=-5.0@A t=0 v=DK5EN-98", rmFmtMhRoute(g_buf, sizeof(g_buf), z));
    // worst case: 9-char calls, long chain; only whole calls, leading ones
    const char *many[] = {"DK5EN-001", "DK5EN-002", "DK5EN-003", "DK5EN-004", "DK5EN-005", "DK5EN-006",
                          "DK5EN-007", "DK5EN-008", "DK5EN-009", "DK5EN-010"};
    RmMhRouteIn w = {255, 255, true, true, true, true, -99.9f, "DK5EN-001", UINT16_MAX, many, 10};
    size_t l = rmFmtMhRoute(g_buf, sizeof(g_buf), w);
    reportWorst("route", l, g_buf,
                "r h=255 k=255 g=1 m=1 rc=-99.9@DK5EN-001 t=65535 v=DK5EN-001,DK5EN-002,DK5EN-003,DK5EN-004,DK5EN-005");
    const char *tail = strrchr(g_buf, ',');
    TEST_ASSERT_NOT_NULL(tail);
    TEST_ASSERT_EQUAL_UINT32(9, (uint32_t)strlen(tail + 1));  // last call complete
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "v=DK5EN-001,DK5EN-002,DK5EN-003"));
    // empty rcCall with hasRc: rc is absent
    RmMhRouteIn e = {1, 1, false, true, true, true, 3.5f, "", 4, via, 1};
    expectStr("r h=1 k=1 g=0 m=1 rc=- t=4 v=DK5EN-98", rmFmtMhRoute(g_buf, sizeof(g_buf), e));
    // call rule: fold a-z, other bytes -> '?', never cut, too long ends the chain / prints '?'
    RmMhRouteIn c = {1, 1, false, true, true, true, 3.5f, "dk5en:x", 4, via, 0};
    expectStr("r h=1 k=1 g=0 m=1 rc=3.5@DK5EN?X t=4 v=-", rmFmtMhRoute(g_buf, sizeof(g_buf), c));
    const char *odd[] = {"dl2ja-2", "a\nb\xc6", "OK-1", "TOOLONGCALL12", "AFTER-1"};
    RmMhRouteIn o = {1, 1, false, true, true, false, 0.0f, "", 4, odd, 5};
    expectStr("r h=1 k=1 g=0 m=1 rc=- t=4 v=DL2JA-2,A?B?,OK-1", rmFmtMhRoute(g_buf, sizeof(g_buf), o));
    const char *first[] = {"TOOLONGCALL12", "DK5EN-98"};
    RmMhRouteIn f = {1, 1, false, true, true, false, 0.0f, "", 4, first, 2};
    expectStr("r h=1 k=1 g=0 m=1 rc=- t=4 v=-", rmFmtMhRoute(g_buf, sizeof(g_buf), f));
    RmMhRouteIn un = {1, 1, false, true, true, true, 1.0f, "", 4, via, 0};
    memcpy(un.rcCall, "ABCDEFGHIJ", 10);  // 10 chars, no NUL
    expectStr("r h=1 k=1 g=0 m=1 rc=1.0@? t=4 v=-", rmFmtMhRoute(g_buf, sizeof(g_buf), un));
    RmMhRouteIn ex = {1, 1, false, true, true, true, 1.0f, "ABCDEFGHI", 4, via, 0}; // exactly 9: kept
    expectStr("r h=1 k=1 g=0 m=1 rc=1.0@ABCDEFGHI t=4 v=-", rmFmtMhRoute(g_buf, sizeof(g_buf), ex));
}

// ---- txq / mbox / maxhop -----------------------------------------------------------------------------------

static void test_txq(void)
{
    RmTxqIn t = {3, 20, 0, 12345, 678, 9, 12};
    expectStr("q=3/20 bp=quiet tx=12345 rt=678 dr=9 u=12", rmFmtTxq(g_buf, sizeof(g_buf), t));
    RmTxqIn u = {7, 21, 1, 100000, 2000000, 99999999, 99};
    expectStr("q=7/21 bp=qrs tx=100k rt=2000k dr=99M u=99", rmFmtTxq(g_buf, sizeof(g_buf), u));
    u.bp = 2;
    u.util = 250;  // clamped
    expectStr("q=7/21 bp=qrt tx=100k rt=2000k dr=99M u=100", rmFmtTxq(g_buf, sizeof(g_buf), u));
    u.bp = 9;
    rmFmtTxq(g_buf, sizeof(g_buf), u);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "bp=?"));
    RmTxqIn w = {UINT16_MAX, UINT16_MAX, 2, UINT32_MAX, UINT32_MAX, UINT32_MAX, 100};
    reportWorst("txq", rmFmtTxq(g_buf, sizeof(g_buf), w), g_buf, "q=65535/65535 bp=qrt tx=4294M rt=4294M dr=4294M u=100");
}

static void test_mbox(void)
{
    RmMboxIn m = {3, 12, 50, 1834, 3, 20, 111, 222, 333, 4, 5, 6, 777, 888};
    expectStr("m=heard u=12/50 b=1834 a=3/20 st=111 dl=222 ak=333 dr=15 bl=777 nt=888",
              rmFmtMbox(g_buf, sizeof(g_buf), m));
    m.mode = 0;
    rmFmtMbox(g_buf, sizeof(g_buf), m);
    TEST_ASSERT_EQUAL_STRING_LEN("m=off ", g_buf, 6);
    m.mode = 1;
    rmFmtMbox(g_buf, sizeof(g_buf), m);
    TEST_ASSERT_EQUAL_STRING_LEN("m=own ", g_buf, 6);
    m.mode = 2;
    rmFmtMbox(g_buf, sizeof(g_buf), m);
    TEST_ASSERT_EQUAL_STRING_LEN("m=list ", g_buf, 7);
    RmMboxIn w = {3, UINT16_MAX, UINT16_MAX, UINT32_MAX, UINT16_MAX, UINT16_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                  UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    reportWorst("mbox", rmFmtMbox(g_buf, sizeof(g_buf), w), g_buf,
                "m=heard u=65535/65535 b=999999 a=65535/65535 st=4294M dl=4294M ak=4294M dr=4294M bl=4294M nt=4294M");
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "dr=4294M"));  // sum saturates, no wrap
}

static void test_maxhop(void)
{
    expectStr("t=4 p=2", rmFmtMaxhop(g_buf, sizeof(g_buf), 4, 2));
    expectStr("t=0 p=99", rmFmtMaxhop(g_buf, sizeof(g_buf), -3, 1000));
    expectStr("t=99 p=0", rmFmtMaxhop(g_buf, sizeof(g_buf), INT32_MAX, INT32_MIN));
    expectStr("t=0 p=99", rmFmtMaxhop(g_buf, sizeof(g_buf), INT32_MIN, INT32_MAX));
    expectStr("t=0 p=0", rmFmtMaxhop(g_buf, sizeof(g_buf), 0, 0));
    reportWorst("maxhop", rmFmtMaxhop(g_buf, sizeof(g_buf), 99, 99), g_buf, "t=99 p=99");
}

// ---- worst cases of the remaining formatters and the charset ------------------------------------------------

static void test_worst_cases_rest(void)
{
    RmRadioIn r = {999.999f, 99, 99, 999.99f, -99, -99};
    reportWorst("radio", rmFmtRadio(g_buf, sizeof(g_buf), r), g_buf, "f=999.999 sf=99 cr=99 bw=999.99 p=-99/-99 pmin=0");
    char name[RM_NAME_MAX + 1], atxt[RM_ATXT_MAX + 1];
    memset(name, 'N', RM_NAME_MAX);
    name[RM_NAME_MAX] = '\0';
    memset(atxt, 'A', RM_ATXT_MAX);
    atxt[RM_ATXT_MAX] = '\0';
    reportWorst("name", rmFmtName(g_buf, sizeof(g_buf), name), g_buf, "n=NNNNNNNNNNNNNNNNNNN");
    reportWorst("atxt", rmFmtAtxt(g_buf, sizeof(g_buf), atxt), g_buf, "a=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    RmPosIn p = {-89.99999, -179.99999, 40000, RM_POS_NOFIX};
    reportWorst("pos", rmFmtPos(g_buf, sizeof(g_buf), p), g_buf, "-89.99999 -179.99999 40000 nofix");
    RmSensIn s = {true, true, true, true, -99.9f, 100.0f, 1099.9f, -99.9f};
    reportWorst("sens", rmFmtSens(g_buf, sizeof(g_buf), s), g_buf, "t=-99.9 h=100 p=1099.9 t2=-99.9");
    // absurd inputs stay inside the budget (values are clamped)
    RmSensIn ab = {true, true, true, true, -1e30f, 1e30f, 1e30f, -1e30f};
    TEST_ASSERT_TRUE(rmFmtSens(g_buf, sizeof(g_buf), ab) <= RM_FMT_BODY_MAX);
    TEST_ASSERT_TRUE(charsetOk(g_buf));
    RmRadioIn abr = {1e30f, INT32_MAX, INT32_MIN, 1e30f, INT32_MIN, INT32_MAX};
    expectStr("f=999.999 sf=99 cr=0 bw=999.99 p=-99/99 pmin=0", rmFmtRadio(g_buf, sizeof(g_buf), abr));
    TEST_ASSERT_TRUE(charsetOk(g_buf));
}

// ---- too-small buffers --------------------------------------------------------------------------------------

static void test_small_buffer(void)
{
    char b[8];
    RmRadioIn r = {433.175f, 11, 5, 250.0f, 10, 22};
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtRadio(b, sizeof(b), r));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    // exact fit needs strlen+1
    char e[9];
    TEST_ASSERT_EQUAL_UINT32(7, rmFmtMaxhop(e, 8, 4, 2));
    memset(e, 'x', sizeof(e));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtMaxhop(e, 7, 4, 2));
    TEST_ASSERT_EQUAL_CHAR('\0', e[0]);
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtName(b, sizeof(b), "ABCDEFGH"));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    RmPosIn p = {48.4, 11.7, 492, RM_POS_GPS};
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtPos(b, sizeof(b), p));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    RmSensIn s = {true, true, true, true, 1, 2, 3, 4};
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtSens(b, sizeof(b), s));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    fillRows(1, 1);
    RmMhPageIn in = {5, 0, g_rows, 1};
    memset(b, 'x', sizeof(b));
    uint8_t em = 9;
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtMhPage(b, sizeof(b), in, &em));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    TEST_ASSERT_EQUAL_UINT8(0, em);
    RmTxqIn t = {1, 2, 0, 3, 4, 5, 6};
    memset(b, 'x', sizeof(b));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtTxq(b, sizeof(b), t));
    TEST_ASSERT_EQUAL_CHAR('\0', b[0]);
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtMaxhop(nullptr, 8, 1, 1));
    TEST_ASSERT_EQUAL_UINT32(0, rmFmtMaxhop(b, 0, 1, 1));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_count);
    RUN_TEST(test_radio_example_and_bw);
    RUN_TEST(test_radio_sentinels);
    RUN_TEST(test_radio_rounding);
    RUN_TEST(test_name_atxt);
    RUN_TEST(test_pos);
    RUN_TEST(test_sens);
    RUN_TEST(test_mh_page_empty_and_one);
    RUN_TEST(test_mh_page_fit);
    RUN_TEST(test_mh_page_first_beyond);
    RUN_TEST(test_mh_page_count_zero_and_calls);
    RUN_TEST(test_mh_direct);
    RUN_TEST(test_mh_route);
    RUN_TEST(test_txq);
    RUN_TEST(test_mbox);
    RUN_TEST(test_maxhop);
    RUN_TEST(test_worst_cases_rest);
    RUN_TEST(test_small_buffer);
    return UNITY_END();
}
