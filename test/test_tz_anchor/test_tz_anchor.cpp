// TZ-01 (docs/ntp-tz-rtc-wave-plan.md W3): tzReanchor(), the arithmetic of the
// Clock::SetClock(time_t, bool) funnel (src/tz_anchor.h). Epochs are literal
// UTC instants; node epoch = UTC + offset.
//
//   pio test -e native_tz_anchor

#include <unity.h>

#include "tz_anchor.h"

void setUp(void) {}
void tearDown(void) {}

static TzRule cet(void)
{
    TzRule r;
    TEST_ASSERT_TRUE(tzParse("CET-1CEST,M3.5.0,M10.5.0/3", &r));
    return r;
}

// 2026-10-25 01:00:00 UTC: CEST -> CET, node epoch steps back one hour
static void test_cest_to_cet(void)
{
    const TzRule r = cet();
    const int64_t sw = 1792890000;
    int32_t off;

    // last second of summer time: unchanged
    int64_t out = tzReanchor(sw - 1 + 7200, 7200, &r, &off);
    TEST_ASSERT_EQUAL_INT32(7200, off);
    TEST_ASSERT_EQUAL_INT64(sw - 1 + 7200, out);

    // first second of winter time, still carrying the CEST offset
    out = tzReanchor(sw + 7200, 7200, &r, &off);
    TEST_ASSERT_EQUAL_INT32(3600, off);
    TEST_ASSERT_EQUAL_INT64(sw + 3600, out);
}

// 2026-03-29 01:00:00 UTC: CET -> CEST, node epoch steps forward one hour
static void test_cet_to_cest(void)
{
    const TzRule r = cet();
    const int64_t sw = 1774746000;
    int32_t off;

    int64_t out = tzReanchor(sw - 1 + 3600, 3600, &r, &off);
    TEST_ASSERT_EQUAL_INT32(3600, off);
    TEST_ASSERT_EQUAL_INT64(sw - 1 + 3600, out);

    out = tzReanchor(sw + 3600, 3600, &r, &off);
    TEST_ASSERT_EQUAL_INT32(7200, off);
    TEST_ASSERT_EQUAL_INT64(sw + 7200, out);
}

static void test_mid_season_unchanged(void)
{
    const TzRule r = cet();
    int32_t off;

    int64_t out = tzReanchor(1782000000 + 7200, 7200, &r, &off);   // June
    TEST_ASSERT_EQUAL_INT32(7200, off);
    TEST_ASSERT_EQUAL_INT64(1782000000 + 7200, out);

    out = tzReanchor(1767225600 + 3600, 3600, &r, &off);           // January
    TEST_ASSERT_EQUAL_INT32(3600, off);
    TEST_ASSERT_EQUAL_INT64(1767225600 + 3600, out);
}

// first clock set after boot: node_utcoff still 0, the rule supplies the offset
static void test_stale_offset_corrected(void)
{
    const TzRule r = cet();
    int32_t off;
    int64_t out = tzReanchor(1782000000, 0, &r, &off);
    TEST_ASSERT_EQUAL_INT32(7200, off);
    TEST_ASSERT_EQUAL_INT64(1782000000 + 7200, out);
}

static void test_idempotent(void)
{
    const TzRule r = cet();
    int32_t off1, off2;
    int64_t out1 = tzReanchor(1792890000 + 7200, 7200, &r, &off1);
    int64_t out2 = tzReanchor(out1, off1, &r, &off2);
    TEST_ASSERT_EQUAL_INT32(off1, off2);
    TEST_ASSERT_EQUAL_INT64(out1, out2);
}

static void test_half_hour_zone(void)
{
    TzRule r;
    TEST_ASSERT_TRUE(tzParse("<+0530>-5:30", &r));
    int32_t off;

    int64_t out = tzReanchor(1782000000, 0, &r, &off);
    TEST_ASSERT_EQUAL_INT32(19800, off);
    TEST_ASSERT_EQUAL_INT64(1782000000 + 19800, out);

    out = tzReanchor(1782000000 + 19800, 19800, &r, &off);
    TEST_ASSERT_EQUAL_INT32(19800, off);
    TEST_ASSERT_EQUAL_INT64(1782000000 + 19800, out);
}

static void test_negative_zone(void)
{
    TzRule r;
    TEST_ASSERT_TRUE(tzParse("EST5EDT,M3.2.0,M11.1.0", &r));
    int32_t off;
    int64_t out = tzReanchor(1782000000 - 18000, -18000, &r, &off);   // June: EDT
    TEST_ASSERT_EQUAL_INT32(-14400, off);
    TEST_ASSERT_EQUAL_INT64(1782000000 - 14400, out);
}

// utc < 0 (clock before 1970): the rule is not consulted
static void test_before_epoch_untouched(void)
{
    const TzRule r = cet();
    int32_t off;
    int64_t out = tzReanchor(100, 7200, &r, &off);
    TEST_ASSERT_EQUAL_INT32(7200, off);
    TEST_ASSERT_EQUAL_INT64(100, out);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_cest_to_cet);
    RUN_TEST(test_cet_to_cest);
    RUN_TEST(test_mid_season_unchanged);
    RUN_TEST(test_stale_offset_corrected);
    RUN_TEST(test_idempotent);
    RUN_TEST(test_half_hour_zone);
    RUN_TEST(test_negative_zone);
    RUN_TEST(test_before_epoch_untouched);
    return UNITY_END();
}
