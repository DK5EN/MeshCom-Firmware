// TZ-01 (docs/ntp-tz-rtc-wave-plan.md W2): tz_rule parser and offset-at-UTC.
//
// Oracle: host libc. setenv("TZ") + tzset() + localtime_r() is used HERE ONLY;
// the firmware never touches libc TZ (it would double-apply the offset in
// Clock::setCurrentTime()).
//
//   pio test -e native_tz_rule

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tz_rule.h"

void setUp(void) {}
void tearDown(void) {}

static time_t utcAt(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    return timegm(&t);
}

// compare one instant against libc; returns false and prints on mismatch
static bool sameAsLibc(const TzRule *r, const char *tz, time_t t)
{
    struct tm tm;
    time_t tt = t;
    localtime_r(&tt, &tm);
    int32_t off = tzOffsetSec(r, (uint32_t)t);
    const char *ab = tzAbbrev(r, (uint32_t)t);
    if (off != (int32_t)tm.tm_gmtoff || strcmp(ab, tm.tm_zone) != 0)
    {
        printf("MISMATCH tz=%s t=%ld: ours %d/%s libc %ld/%s\n", tz, (long)t, (int)off, ab,
               (long)tm.tm_gmtoff, tm.tm_zone);
        return false;
    }
    return true;
}

static void setOracle(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

// hourly (plus half-hour) sweep over [from, to)
static void sweep(const char *tz, int y0, int y1)
{
    TzRule r;
    TEST_ASSERT_TRUE_MESSAGE(tzParse(tz, &r), tz);
    setOracle(tz);
    int bad = 0;
    long n = 0;
    for (time_t t = utcAt(y0, 1, 1, 0, 0, 0); t < utcAt(y1, 1, 1, 0, 0, 0); t += 1800, n++)
        if (!sameAsLibc(&r, tz, t))
            bad++;
    TEST_ASSERT_GREATER_THAN(1000, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, tz);
}

static void test_cet_sweep_2026_2027(void)
{
    sweep("CET-1CEST,M3.5.0,M10.5.0/3", 2026, 2028);
}

static void test_cet_edges(void)
{
    const char *tz = "CET-1CEST,M3.5.0,M10.5.0/3";
    TzRule r;
    TEST_ASSERT_TRUE(tzParse(tz, &r));
    setOracle(tz);

    // {y, month, day} of the last Sunday of March / October at 01:00:00 UTC
    const int edges[4][3] = {{2026, 3, 29}, {2026, 10, 25}, {2027, 3, 28}, {2027, 10, 31}};
    for (int i = 0; i < 4; i++)
    {
        time_t e = utcAt(edges[i][0], edges[i][1], edges[i][2], 1, 0, 0);
        TEST_ASSERT_TRUE(sameAsLibc(&r, tz, e - 1));
        TEST_ASSERT_TRUE(sameAsLibc(&r, tz, e));
        TEST_ASSERT_TRUE(sameAsLibc(&r, tz, e + 1));
        bool spring = (i % 2 == 0);
        TEST_ASSERT_EQUAL_INT32(spring ? 3600 : 7200, tzOffsetSec(&r, (uint32_t)(e - 1)));
        TEST_ASSERT_EQUAL_INT32(spring ? 7200 : 3600, tzOffsetSec(&r, (uint32_t)e));
    }
    TEST_ASSERT_EQUAL_STRING("CET", tzAbbrev(&r, (uint32_t)utcAt(2026, 1, 15, 12, 0, 0)));
    TEST_ASSERT_EQUAL_STRING("CEST", tzAbbrev(&r, (uint32_t)utcAt(2026, 7, 15, 12, 0, 0)));
}

static void test_us_eastern(void) { sweep("EST5EDT,M3.2.0,M11.1.0", 2026, 2027); }
static void test_utc0(void) { sweep("UTC0", 2026, 2027); }
static void test_half_hour_quoted(void) { sweep("<+0530>-5:30", 2026, 2027); }
static void test_new_zealand(void) { sweep("NZST-12NZDT,M9.5.0,M4.1.0/3", 2026, 2028); }
static void test_london(void) { sweep("GMT0BST,M3.5.0/1,M10.5.0", 2026, 2028); }

static void test_fields(void)
{
    TzRule r;
    TEST_ASSERT_TRUE(tzParse("<+0530>-5:30", &r));
    TEST_ASSERT_EQUAL_INT32(19800, r.stdOffSec);
    TEST_ASSERT_FALSE(r.hasDst);
    TEST_ASSERT_EQUAL_STRING("+0530", r.stdName);

    TEST_ASSERT_TRUE(tzParse("CET-1CEST,M3.5.0,M10.5.0/3", &r));
    TEST_ASSERT_TRUE(r.hasDst);
    TEST_ASSERT_EQUAL_INT32(3600, r.stdOffSec);
    TEST_ASSERT_EQUAL_INT32(7200, r.dstOffSec);   // default std + 1 h
    TEST_ASSERT_EQUAL_INT32(7200, r.startTimeSec);
    TEST_ASSERT_EQUAL_INT32(10800, r.endTimeSec);

    // explicit dst offset, long name truncated to 7 chars
    TEST_ASSERT_TRUE(tzParse("<ABCDEFGHIJ>3<XYZ>1:30,M3.1.0,M10.1.0", &r));
    TEST_ASSERT_EQUAL_STRING("ABCDEFG", r.stdName);
    TEST_ASSERT_EQUAL_INT32(-10800, r.stdOffSec);
    TEST_ASSERT_EQUAL_INT32(-5400, r.dstOffSec);
}

static void test_rejects(void)
{
    static const char *const bad[] = {
        "",
        "CET",
        "CET-1CEST",
        "CET-1CEST,J60,J300",
        "CET-1CEST,60,300",
        "CET-1CEST,M13.5.0,M10.5.0",
        "CET-1CEST,M0.5.0,M10.5.0",
        "CET-1CEST,M3.6.0,M10.5.0",
        "CET-1CEST,M3.0.0,M10.5.0",
        "CET-1CEST,M3.5.7,M10.5.0",
        "CET-1CEST,M3.5.0",
        "CET-1CEST,M3.5.0,",
        "CET-1CEST,M3.5.0,M10.5.0,",
        "CET-1CEST,M3.5.0,M10.5.0x",
        "UTC0x",
        "<+0530-5:30",
        "<+05 30>-5:30",
        "AB-1",
        "CET-25",
        "CET-1:60",
        "CET-1CEST,M3.5.0/168,M10.5.0",
    };
    TzRule r;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        TEST_ASSERT_FALSE_MESSAGE(tzParse(bad[i], &r), bad[i]);

    TEST_ASSERT_FALSE(tzParse(NULL, &r));
    TEST_ASSERT_FALSE(tzParse("UTC0", NULL));

    // 40+ characters
    TEST_ASSERT_FALSE(tzParse("CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:00x", &r));
    TEST_ASSERT_FALSE(tzParse("<AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA>0", &r));
}

static void test_length_limit(void)
{
    TzRule r;
    // exactly 39 characters is fine, 40 is not
    const char *ok39 = "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00";
    TEST_ASSERT_EQUAL_INT(39, (int)strlen(ok39));
    TEST_ASSERT_TRUE(tzParse(ok39, &r));
    const char *bad40 = "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:";
    TEST_ASSERT_EQUAL_INT(40, (int)strlen(bad40));
    TEST_ASSERT_FALSE(tzParse(bad40, &r));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cet_sweep_2026_2027);
    RUN_TEST(test_cet_edges);
    RUN_TEST(test_us_eastern);
    RUN_TEST(test_utc0);
    RUN_TEST(test_half_hour_quoted);
    RUN_TEST(test_new_zealand);
    RUN_TEST(test_london);
    RUN_TEST(test_fields);
    RUN_TEST(test_rejects);
    RUN_TEST(test_length_limit);
    return UNITY_END();
}
