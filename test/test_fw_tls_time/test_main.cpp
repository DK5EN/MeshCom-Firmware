// Host test of src/fw_tls_time.h (FORMAL-01 wave 3b, mbedTLS #1).
#include <unity.h>
#include <stdint.h>
#include "fw_tls_time.h"

static int64_t ep(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{
    int64_t e = -1;
    TEST_ASSERT_TRUE(fwTlsEpochFromX509(y, mo, d, h, mi, s, &e));
    return e;
}

static uint32_t flags(int64_t from, int64_t to, bool clk, int64_t now, int64_t floor)
{
    return fwTlsCertFlags(true, from, true, to, clk, now, floor);
}

void setUp(void) {}
void tearDown(void) {}

static const int64_t FLOOR = 1790000000; // about 2026-09-21

void test_valid_cert(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2026, 1, 1), ep(2027, 1, 1), true, ep(2026, 11, 23), FLOOR));
}
void test_expired_with_clock(void)
{
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED,
                             flags(ep(2020, 1, 1), ep(2026, 11, 22), true, ep(2026, 11, 23), FLOOR));
}
void test_expired_by_floor_without_clock(void)
{
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, flags(ep(2020, 1, 1), ep(2025, 6, 1), false, 0, FLOOR));
}
void test_floor_wins_over_wrong_old_clock(void)
{
    // a clock behind the build must not revive a cert that expired before the build
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, flags(ep(2020, 1, 1), ep(2025, 6, 1), true, ep(2024, 1, 1), FLOOR));
}
void test_not_yet_valid_with_clock(void)
{
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_FUTURE, flags(ep(2027, 1, 1), ep(2028, 1, 1), true, ep(2026, 11, 23), FLOOR));
}
void test_not_yet_valid_ignored_without_clock(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2027, 1, 1), ep(2028, 1, 1), false, 0, FLOOR));
}
void test_stale_rtc_2014_valid_flag_no_future(void)
{
    // RTC lost power and reset to 2014 but bRTCON is set: clock_valid=true, node_utc < build floor.
    // The clock counts as not valid: a cert valid since 2026-01 must not get FUTURE, the floor decides.
    const int64_t stale = ep(2014, 1, 1);
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2026, 1, 1), ep(2027, 1, 1), true, stale, FLOOR));
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2027, 1, 1), ep(2028, 1, 1), true, stale, FLOOR)); // no FUTURE either
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, flags(ep(2020, 1, 1), ep(2025, 6, 1), true, stale, FLOOR));
}
void test_clock_at_floor_is_valid_one_below_is_not(void)
{
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_FUTURE, flags(FLOOR + 1, FLOOR + 1000, true, FLOOR, FLOOR));
    TEST_ASSERT_EQUAL_UINT32(0, flags(FLOOR - 10, FLOOR + 1000, true, FLOOR, FLOOR));
    TEST_ASSERT_EQUAL_UINT32(0, flags(FLOOR + 1, FLOOR + 1000, true, FLOOR - 1, FLOOR));
}
void test_clock_far_in_future_cannot_be_caught(void)
{
    // documented limit: no upper bound, a clock wrong into the future expires certs whose valid_to lies before it
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED,
                             flags(ep(2026, 1, 1), ep(2027, 1, 1), true, ep(2035, 1, 1), FLOOR));
}
void test_boundary_seconds(void)
{
    const int64_t now = ep(2026, 11, 23, 12, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2026, 1, 1), now, true, now, FLOOR));                    // to == now: valid
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, flags(ep(2026, 1, 1), now - 1, true, now, FLOOR)); // to == now-1
    TEST_ASSERT_EQUAL_UINT32(0, flags(now, now + 100, true, now, FLOOR));                         // from == now: valid
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_FUTURE, flags(now + 1, now + 100, true, now, FLOOR)); // from == now+1
    TEST_ASSERT_EQUAL_UINT32(0, flags(ep(2020, 1, 1), FLOOR, false, 0, FLOOR));                   // to == floor: valid
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, flags(ep(2020, 1, 1), FLOOR - 1, false, 0, FLOOR));
}
void test_unconvertible_to_fails_closed(void)
{
    TEST_ASSERT_EQUAL_UINT32(FW_TLS_BADCERT_EXPIRED, fwTlsCertFlags(true, 0, false, 0, false, 0, FLOOR));
}
void test_flags_are_date(void)
{
    TEST_ASSERT_TRUE(fwTlsFlagsAreDate(FW_TLS_BADCERT_EXPIRED));
    TEST_ASSERT_TRUE(fwTlsFlagsAreDate(FW_TLS_BADCERT_FUTURE | 0x08u));
    TEST_ASSERT_FALSE(fwTlsFlagsAreDate(0x08u)); // NOT_TRUSTED
    TEST_ASSERT_FALSE(fwTlsFlagsAreDate(0));
}
void test_epoch_conversion(void)
{
    TEST_ASSERT_EQUAL_INT64(0, ep(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(951782400, ep(2000, 2, 29));             // leap year, 400 rule
    TEST_ASSERT_EQUAL_INT64(951868800, ep(2000, 3, 1));
    TEST_ASSERT_EQUAL_INT64(1709164800, ep(2024, 2, 29));            // leap year
    TEST_ASSERT_EQUAL_INT64(1709251200, ep(2024, 3, 1));
    TEST_ASSERT_EQUAL_INT64(1704067199, ep(2023, 12, 31, 23, 59, 59)); // end of year
    TEST_ASSERT_EQUAL_INT64(4102444800LL, ep(2100, 1, 1));           // past 2038, 100 rule not leap
    TEST_ASSERT_EQUAL_INT64(ep(2100, 3, 1) - ep(2100, 2, 28), 86400); // 2100 has no Feb 29
    TEST_ASSERT_EQUAL_INT64(2147483648LL, ep(2038, 1, 19, 3, 14, 8));
    int64_t e;
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(2100, 2, 29, 0, 0, 0, &e));
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(2023, 2, 29, 0, 0, 0, &e));
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(2026, 4, 31, 0, 0, 0, &e));
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(2026, 13, 1, 0, 0, 0, &e));
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(2026, 1, 1, 24, 0, 0, &e));
    TEST_ASSERT_FALSE(fwTlsEpochFromX509(1969, 12, 31, 0, 0, 0, &e));
    TEST_ASSERT_TRUE(fwTlsEpochFromX509(9999, 12, 31, 23, 59, 59, &e)); // GeneralizedTime "no expiry"
}
void test_build_floor(void)
{
    TEST_ASSERT_EQUAL_INT64(ep(2026, 9, 25, 11, 6, 3), fwTlsBuildFloor("Sep 25 2026", "11:06:03"));
    TEST_ASSERT_EQUAL_INT64(ep(2026, 1, 5, 0, 0, 0), fwTlsBuildFloor("Jan  5 2026", "00:00:00"));
    TEST_ASSERT_EQUAL_INT64(ep(2026, 12, 31, 23, 59, 59), fwTlsBuildFloor("Dec 31 2026", "23:59:59"));
    TEST_ASSERT_EQUAL_INT64(0, fwTlsBuildFloor("Xxx 25 2026", "11:06:03"));
    TEST_ASSERT_EQUAL_INT64(0, fwTlsBuildFloor("Sep 25 2026", "11-06-03"));
    TEST_ASSERT_EQUAL_INT64(0, fwTlsBuildFloor(NULL, "11:06:03"));
    TEST_ASSERT_TRUE(fwTlsBuildFloor(__DATE__, __TIME__) > 1790000000); // this very build parses
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_cert);
    RUN_TEST(test_expired_with_clock);
    RUN_TEST(test_expired_by_floor_without_clock);
    RUN_TEST(test_floor_wins_over_wrong_old_clock);
    RUN_TEST(test_not_yet_valid_with_clock);
    RUN_TEST(test_not_yet_valid_ignored_without_clock);
    RUN_TEST(test_stale_rtc_2014_valid_flag_no_future);
    RUN_TEST(test_clock_at_floor_is_valid_one_below_is_not);
    RUN_TEST(test_clock_far_in_future_cannot_be_caught);
    RUN_TEST(test_boundary_seconds);
    RUN_TEST(test_unconvertible_to_fails_closed);
    RUN_TEST(test_flags_are_date);
    RUN_TEST(test_epoch_conversion);
    RUN_TEST(test_build_floor);
    return UNITY_END();
}
