// Host-Test fuer src/rtc_offset.h -- Offset-Arithmetik zwischen dem RTC-Chip
// (UTC) und der Knotenuhr (UTC + node_utcoff) sowie das Refresh-Gate.
//
//   pio test -e native_rtc_offset
//
// RTC-1: ESP32 schrieb Knotenzeit statt UTC in den RTC.
// RTC-2: nRF52 addierte den Offset beim Lesen zweimal.
// RTC-3: das Refresh-Gate war invertiert (und der Null-Sentinel fehlte).

#include <unity.h>

#include <stdint.h>
#include <time.h>

#include <rtc_offset.h>

void setUp(void) {}
void tearDown(void) {}

static const float OFFS[] = {2.0f, -5.0f, 5.5f, 0.0f, -3.5f};

static int64_t host_timegm(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    return (int64_t)timegm(&t);
}

void test_offset_sec(void)
{
    TEST_ASSERT_EQUAL_INT32(7200, rtcOffsetSec(2.0f));
    TEST_ASSERT_EQUAL_INT32(19800, rtcOffsetSec(5.5f));
    TEST_ASSERT_EQUAL_INT32(-12600, rtcOffsetSec(-3.5f));
    TEST_ASSERT_EQUAL_INT32(-18000, rtcOffsetSec(-5.0f));
    TEST_ASSERT_EQUAL_INT32(0, rtcOffsetSec(0.0f));
}

void test_epoch_known_values(void)
{
    TEST_ASSERT_EQUAL_INT64(0, rtcEpochFromFields(1970, 1, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT64(host_timegm(2000, 2, 29, 12, 0, 0), rtcEpochFromFields(2000, 2, 29, 12, 0, 0));
    TEST_ASSERT_EQUAL_INT64(951825600LL, rtcEpochFromFields(2000, 2, 29, 12, 0, 0));
    TEST_ASSERT_EQUAL_INT64(host_timegm(2026, 10, 25, 1, 0, 0), rtcEpochFromFields(2026, 10, 25, 1, 0, 0));
    TEST_ASSERT_EQUAL_INT64(1792890000LL, rtcEpochFromFields(2026, 10, 25, 1, 0, 0));
}

void test_epoch_matches_host_timegm_over_range(void)
{
    // Jahresgrenzen, Schaltjahre, Monatsenden
    for (int y = 2001; y <= 2040; y += 3)
        for (int mo = 1; mo <= 12; mo++)
        {
            TEST_ASSERT_EQUAL_INT64(host_timegm(y, mo, 1, 0, 0, 0), rtcEpochFromFields(y, mo, 1, 0, 0, 0));
            TEST_ASSERT_EQUAL_INT64(host_timegm(y, mo, 28, 23, 59, 59), rtcEpochFromFields(y, mo, 28, 23, 59, 59));
        }
    TEST_ASSERT_EQUAL_INT64(host_timegm(2024, 2, 29, 23, 59, 59), rtcEpochFromFields(2024, 2, 29, 23, 59, 59));
    TEST_ASSERT_EQUAL_INT64(host_timegm(2100, 3, 1, 0, 0, 0), rtcEpochFromFields(2100, 3, 1, 0, 0, 0));
}

void test_write_path_is_utc(void)
{
    // off = 2: der RTC bekommt genau 7200 s weniger als die Knotenzeit
    int64_t local = rtcEpochFromFields(2026, 10, 4, 15, 30, 0);
    int64_t utc = rtcUtcFromLocalFields(2026, 10, 4, 15, 30, 0, 2.0f);
    TEST_ASSERT_EQUAL_INT64(7200, local - utc);
    TEST_ASSERT_EQUAL_INT64(rtcEpochFromFields(2026, 10, 4, 13, 30, 0), utc);
}

void test_round_trip(void)
{
    for (unsigned i = 0; i < sizeof(OFFS) / sizeof(OFFS[0]); i++)
    {
        float off = OFFS[i];
        int64_t local = rtcEpochFromFields(2026, 10, 25, 3, 15, 20);
        int64_t utc = rtcUtcFromLocalFields(2026, 10, 25, 3, 15, 20, off);
        int64_t back = rtcNodeEpochFromUtc((uint32_t)utc, off);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(local, back, "round trip off");
    }
}

void test_read_path_adds_offset_once(void)
{
    TEST_ASSERT_EQUAL_INT64(1792890000LL + 7200, rtcNodeEpochFromUtc(1792890000u, 2.0f));
    TEST_ASSERT_EQUAL_INT64(1792890000LL - 12600, rtcNodeEpochFromUtc(1792890000u, -3.5f));
}

void test_refresh_gate(void)
{
    // nie geschrieben (0) -> faellig
    TEST_ASSERT_TRUE(rtcRefreshDue(5, 0));
    TEST_ASSERT_TRUE(rtcRefreshDue(0, 0));
    TEST_ASSERT_TRUE(rtcRefreshDue(123456, 0));

    uint32_t t = 100000;
    TEST_ASSERT_FALSE(rtcRefreshDue(t, t));
    TEST_ASSERT_FALSE(rtcRefreshDue(t + 59999, t));
    TEST_ASSERT_TRUE(rtcRefreshDue(t + 60000, t));
    TEST_ASSERT_TRUE(rtcRefreshDue(t + 600000, t));
}

void test_refresh_gate_wrap(void)
{
    TEST_ASSERT_TRUE(rtcRefreshDue(0x00010000u, 0xFFFFF000u));
    // knapp vor Ablauf ueber den Wrap
    TEST_ASSERT_FALSE(rtcRefreshDue(0xFFFFF000u + 59999u, 0xFFFFF000u));
    TEST_ASSERT_TRUE(rtcRefreshDue(0xFFFFF000u + 60000u, 0xFFFFF000u));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_offset_sec);
    RUN_TEST(test_epoch_known_values);
    RUN_TEST(test_epoch_matches_host_timegm_over_range);
    RUN_TEST(test_write_path_is_utc);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_read_path_adds_offset_once);
    RUN_TEST(test_refresh_gate);
    RUN_TEST(test_refresh_gate_wrap);
    return UNITY_END();
}
