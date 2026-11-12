// GPS sky plot core: GSV parser, freshness, SNR levels, projection, AGE formatter.
#include <stdio.h>
#include <string.h>
#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>
#include "gps_skyplot.h"

void setUp(void) { gpsSkySetLock(NULL, NULL); gpsSkyReset(); }
void tearDown(void) {}

// build "$<body>*hh" with a correct checksum
static void sentence(char *out, size_t n, const char *body)
{
    uint8_t x = 0;
    for(const char *p = body; *p; p++) x ^= (uint8_t)*p;
    snprintf(out, n, "$%s*%02X", body, x);
}

static bool parseBody(const char *body, uint32_t now)
{
    char s[128];
    sentence(s, sizeof(s), body);
    return gpsSkyParseLine(s, now);
}

static const GpsSkySat *find(const GpsSkySat *a, int n, char sys, int prn)
{
    for(int i = 0; i < n; i++)
        if(a[i].sys == sys && a[i].prn == prn) return &a[i];
    return NULL;
}

void test_checksum_good(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,05,40,120,30", 1000));
    TEST_ASSERT_EQUAL_INT(1, gpsSkySatsInView(1000));
}

void test_checksum_bad(void)
{
    char s[128];
    sentence(s, sizeof(s), "GPGSV,1,1,01,05,40,120,30");
    s[strlen(s) - 1] = (s[strlen(s) - 1] == '0') ? '1' : '0';
    TEST_ASSERT_FALSE(gpsSkyParseLine(s, 1000));
    TEST_ASSERT_FALSE(gpsSkyParseLine("$GPGSV,1,1,01,05,40,120,30*", 1000));   // digits missing
    TEST_ASSERT_FALSE(gpsSkyParseLine("$GPGSV,1,1,01,05,40,120,30", 1000));    // no checksum
    TEST_ASSERT_EQUAL_INT(0, gpsSkySatsInView(1000));
}

void test_non_gsv_rejected(void)
{
    TEST_ASSERT_FALSE(parseBody("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 1000));
    TEST_ASSERT_EQUAL_INT(0, gpsSkySatsInView(1000));
}

void test_empty_fields(void)
{
    // sat 07 tracked-less: elev/az/snr empty; sat 09 has elev+az only
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,02,07,,,,09,33,201,", 1000));
    GpsSkySat a[GPS_SKY_MAX_SATS];
    int n = gpsSkyCopy(a, GPS_SKY_MAX_SATS, 1000);
    TEST_ASSERT_EQUAL_INT(2, n);
    const GpsSkySat *s7 = find(a, n, 'P', 7);
    const GpsSkySat *s9 = find(a, n, 'P', 9);
    TEST_ASSERT_NOT_NULL(s7);
    TEST_ASSERT_NOT_NULL(s9);
    TEST_ASSERT_EQUAL_INT(-1, s7->elev);
    TEST_ASSERT_EQUAL_INT(-1, s7->az);
    TEST_ASSERT_EQUAL_INT(0, s7->snr);
    TEST_ASSERT_EQUAL_INT(33, s9->elev);
    TEST_ASSERT_EQUAL_INT(201, s9->az);
    TEST_ASSERT_EQUAL_INT(0, s9->snr);
}

void test_multi_sentence_group(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,3,1,09,01,10,010,20,02,20,020,25,03,30,030,30,04,40,040,35", 1000));
    TEST_ASSERT_TRUE(parseBody("GPGSV,3,2,09,05,50,050,40,06,60,060,41,07,70,070,42,08,80,080,43", 1000));
    TEST_ASSERT_TRUE(parseBody("GPGSV,3,3,09,09,85,090,44", 1000));
    TEST_ASSERT_EQUAL_INT(9, gpsSkySatsInView(1000));
}

void test_signal_id_trailing_field(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,02,05,40,120,30,06,50,130,31,1", 1000));
    GpsSkySat a[GPS_SKY_MAX_SATS];
    int n = gpsSkyCopy(a, GPS_SKY_MAX_SATS, 1000);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 5));
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 6));
}

void test_talker_mapping(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,01,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("GLGSV,1,1,01,02,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("GAGSV,1,1,01,03,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("GBGSV,1,1,01,04,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("BDGSV,1,1,01,05,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("GNGSV,1,1,01,06,10,010,20", 1000));
    GpsSkySat a[GPS_SKY_MAX_SATS];
    int n = gpsSkyCopy(a, GPS_SKY_MAX_SATS, 1000);
    TEST_ASSERT_EQUAL_INT(6, n);
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 1));
    TEST_ASSERT_NOT_NULL(find(a, n, 'L', 2));
    TEST_ASSERT_NOT_NULL(find(a, n, 'A', 3));
    TEST_ASSERT_NOT_NULL(find(a, n, 'B', 4));
    TEST_ASSERT_NOT_NULL(find(a, n, 'B', 5));
    TEST_ASSERT_NOT_NULL(find(a, n, 'N', 6));
}

void test_same_prn_other_system_distinct(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,05,10,010,20", 1000));
    TEST_ASSERT_TRUE(parseBody("GLGSV,1,1,01,05,20,020,30", 1000));
    TEST_ASSERT_EQUAL_INT(2, gpsSkySatsInView(1000));
}

void test_fresh_boundary(void)
{
    TEST_ASSERT_TRUE(gpsSkySatFresh(1000, 1000));
    TEST_ASSERT_TRUE(gpsSkySatFresh(1000, 1000 + GPS_SKY_FRESH_MS - 1));
    TEST_ASSERT_FALSE(gpsSkySatFresh(1000, 1000 + GPS_SKY_FRESH_MS));
}

void test_fresh_rollover(void)
{
    uint32_t seen = 0xFFFFFFFFUL - 1000UL;
    TEST_ASSERT_TRUE(gpsSkySatFresh(seen, 2000));            // 3001 ms later, across the wrap
    TEST_ASSERT_FALSE(gpsSkySatFresh(seen, 6000));           // 7001 ms later
}

void test_expiry_in_table(void)
{
    uint32_t seen = 0xFFFFFFFFUL - 1000UL;
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,05,40,120,30", seen));
    TEST_ASSERT_EQUAL_INT(1, gpsSkySatsInView(seen + 5999UL));
    TEST_ASSERT_EQUAL_INT(0, gpsSkySatsInView(seen + 6000UL));
    GpsSkySat a[4];
    TEST_ASSERT_EQUAL_INT(0, gpsSkyCopy(a, 4, seen + 6000UL));
}

void test_slot_reuse_expired(void)
{
    // fill 40 sats at t=0, then at t=7000 a new one takes an expired slot
    for(int i = 1; i <= GPS_SKY_MAX_SATS; i++)
    {
        char b[64];
        snprintf(b, sizeof(b), "GPGSV,1,1,01,%02d,10,010,20", i);
        TEST_ASSERT_TRUE(parseBody(b, 0));
    }
    TEST_ASSERT_EQUAL_INT(GPS_SKY_MAX_SATS, gpsSkySatsInView(100));
    TEST_ASSERT_TRUE(parseBody("GLGSV,1,1,01,70,10,010,20", 7000));
    TEST_ASSERT_EQUAL_INT(1, gpsSkySatsInView(7000));
}

void test_overflow_evicts_oldest(void)
{
    for(int i = 1; i <= GPS_SKY_MAX_SATS; i++)
    {
        char b[64];
        snprintf(b, sizeof(b), "GPGSV,1,1,01,%02d,10,010,20", i);
        TEST_ASSERT_TRUE(parseBody(b, (uint32_t)(i * 10)));   // all fresh at t=1000
    }
    // sat 1 is the oldest; a 41st sat evicts it, the count stays at the cap
    TEST_ASSERT_TRUE(parseBody("GLGSV,1,1,01,99,10,010,20", 1000));
    GpsSkySat a[GPS_SKY_MAX_SATS + 5];
    int n = gpsSkyCopy(a, GPS_SKY_MAX_SATS + 5, 1000);
    TEST_ASSERT_EQUAL_INT(GPS_SKY_MAX_SATS, n);
    TEST_ASSERT_NULL(find(a, n, 'P', 1));
    TEST_ASSERT_NOT_NULL(find(a, n, 'L', 99));
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 2));
}

void test_update_in_place(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,05,40,120,30", 1000));
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,01,05,41,121,31", 2000));
    GpsSkySat a[4];
    int n = gpsSkyCopy(a, 4, 2000);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(41, a[0].elev);
    TEST_ASSERT_EQUAL_INT(121, a[0].az);
    TEST_ASSERT_EQUAL_INT(31, a[0].snr);
    TEST_ASSERT_EQUAL_UINT32(2000, a[0].seen_ms);
}

void test_copy_respects_max(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,04,01,10,010,20,02,10,010,20,03,10,010,20,04,10,010,20", 1000));
    GpsSkySat a[2];
    TEST_ASSERT_EQUAL_INT(2, gpsSkyCopy(a, 2, 1000));
}

void test_feed_bytewise(void)
{
    char s[128];
    sentence(s, sizeof(s), "GPGSV,1,1,01,05,40,120,30");
    strcat(s, "\r\n");
    for(const char *p = s; *p; p++) gpsSkyFeed(*p, 1000);
    TEST_ASSERT_EQUAL_INT(1, gpsSkySatsInView(1000));
}

void test_feed_ignores_noise_and_overlong(void)
{
    const char *noise = "garbage no dollar\r\n";
    for(const char *p = noise; *p; p++) gpsSkyFeed(*p, 1000);
    gpsSkyFeed('$', 1000);
    for(int i = 0; i < 200; i++) gpsSkyFeed('A', 1000);
    gpsSkyFeed('\n', 1000);
    TEST_ASSERT_EQUAL_INT(0, gpsSkySatsInView(1000));
    // and the parser recovers on the next sentence
    char s[128];
    sentence(s, sizeof(s), "GPGSV,1,1,01,05,40,120,30");
    strcat(s, "\n");
    for(const char *p = s; *p; p++) gpsSkyFeed(*p, 1000);
    TEST_ASSERT_EQUAL_INT(1, gpsSkySatsInView(1000));
}

void test_snr_levels(void)
{
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_LOW, gpsSkySnrLevel(0));
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_LOW, gpsSkySnrLevel(19));
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_MID, gpsSkySnrLevel(20));
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_MID, gpsSkySnrLevel(34));
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_HIGH, gpsSkySnrLevel(35));
    TEST_ASSERT_EQUAL_INT(GPS_SKY_SNR_HIGH, gpsSkySnrLevel(60));
}

void test_projection(void)
{
    int x, y;
    gpsSkyProject(90, 123, 54, 86, 44, &x, &y);   // zenith -> centre, any azimuth
    TEST_ASSERT_EQUAL_INT(54, x);
    TEST_ASSERT_EQUAL_INT(86, y);
    gpsSkyProject(0, 0, 54, 86, 44, &x, &y);      // horizon north -> top
    TEST_ASSERT_EQUAL_INT(54, x);
    TEST_ASSERT_EQUAL_INT(86 - 44, y);
    gpsSkyProject(0, 90, 54, 86, 44, &x, &y);     // horizon east -> right
    TEST_ASSERT_EQUAL_INT(54 + 44, x);
    TEST_ASSERT_EQUAL_INT(86, y);
    gpsSkyProject(0, 180, 54, 86, 44, &x, &y);    // south -> bottom
    TEST_ASSERT_EQUAL_INT(54, x);
    TEST_ASSERT_EQUAL_INT(86 + 44, y);
    gpsSkyProject(45, 270, 54, 86, 44, &x, &y);   // half radius west
    TEST_ASSERT_EQUAL_INT(54 - 22, x);
    TEST_ASSERT_EQUAL_INT(86, y);
}

void test_projection_clamps_elevation(void)
{
    int x, y;
    gpsSkyProject(120, 0, 54, 86, 44, &x, &y);
    TEST_ASSERT_EQUAL_INT(54, x);
    TEST_ASSERT_EQUAL_INT(86, y);
    gpsSkyProject(-5, 0, 54, 86, 44, &x, &y);
    TEST_ASSERT_EQUAL_INT(54, x);
    TEST_ASSERT_EQUAL_INT(86 - 44, y);
}

// ---- AGE formatter (regression: no fix -> TinyGPS++ age() = ULONG_MAX -> "AGE 4294967s")

void test_age_valid(void)
{
    char b[32];
    gpsSkyFormatAge(b, sizeof(b), true, 0);
    TEST_ASSERT_EQUAL_STRING("AGE   0s", b);
    gpsSkyFormatAge(b, sizeof(b), true, 2500);
    TEST_ASSERT_EQUAL_STRING("AGE   2s", b);
    gpsSkyFormatAge(b, sizeof(b), true, 123000);
    TEST_ASSERT_EQUAL_STRING("AGE 123s", b);
}

void test_age_invalid_fix(void)
{
    char b[32];
    gpsSkyFormatAge(b, sizeof(b), false, 2500);
    TEST_ASSERT_EQUAL_STRING("AGE   --s", b);
}

void test_age_uint32_max(void)
{
    char b[32];
    gpsSkyFormatAge(b, sizeof(b), true, UINT32_MAX);
    TEST_ASSERT_EQUAL_STRING("AGE   --s", b);
    gpsSkyFormatAge(b, sizeof(b), false, UINT32_MAX);
    TEST_ASSERT_EQUAL_STRING("AGE   --s", b);
}

void test_nmea41_four_groups_plus_signal_id(void)
{
    // NMEA 4.1: four full groups and the trailing signalId (21 fields) -> 4 satellites
    TEST_ASSERT_TRUE(parseBody("GPGSV,3,1,10,01,10,020,21,02,20,040,22,03,30,060,23,04,40,080,24,1", 1000));
    TEST_ASSERT_EQUAL_INT(4, gpsSkySatsInView(1000));
}

void test_empty_padded_group_skipped(void)
{
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,02,07,10,020,21,,,,,08,20,040,22", 1000));
    GpsSkySat a[GPS_SKY_MAX_SATS];
    int n = gpsSkyCopy(a, GPS_SKY_MAX_SATS, 1000);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 7));
    TEST_ASSERT_NOT_NULL(find(a, n, 'P', 8));
}

void test_projection_off_axis(void)
{
    int x, y;
    gpsSkyProject(30, 45, 54, 86, 44, &x, &y);   // r = 29.33, sin45 = cos45 -> 20.74 -> 21
    TEST_ASSERT_EQUAL_INT(54 + 21, x);
    TEST_ASSERT_EQUAL_INT(86 - 21, y);
}

static int s_lockDepth = 0, s_lockCalls = 0, s_maxDepth = 0;
static void tLock(void)   { s_lockCalls++; s_lockDepth++; if(s_lockDepth > s_maxDepth) s_maxDepth = s_lockDepth; }
static void tUnlock(void) { s_lockDepth--; }

void test_lock_wraps_table_access(void)
{
    s_lockDepth = s_lockCalls = s_maxDepth = 0;
    gpsSkySetLock(tLock, tUnlock);
    TEST_ASSERT_TRUE(parseBody("GPGSV,1,1,02,05,40,120,30,06,50,130,31", 1000));
    TEST_ASSERT_EQUAL_INT(1, s_lockCalls);          // one lock for the whole sentence
    GpsSkySat a[GPS_SKY_MAX_SATS];
    TEST_ASSERT_EQUAL_INT(2, gpsSkyCopy(a, GPS_SKY_MAX_SATS, 1000));
    TEST_ASSERT_EQUAL_INT(2, gpsSkySatsInView(1000));
    TEST_ASSERT_EQUAL_INT(3, s_lockCalls);
    TEST_ASSERT_EQUAL_INT(0, s_lockDepth);          // balanced
    TEST_ASSERT_EQUAL_INT(1, s_maxDepth);           // never nested (a non-recursive mutex is enough)
    TEST_ASSERT_FALSE(parseBody("GPGGA,1,2,3", 1000));
    TEST_ASSERT_FALSE(gpsSkyParseLine("$GPGSV,1,1,01,05,40,120,30*48", 1000));   // bad checksum (good: 49)
    TEST_ASSERT_EQUAL_INT(3, s_lockCalls);          // rejected sentences take no lock
    TEST_ASSERT_EQUAL_INT(0, s_lockDepth);
    gpsSkyReset();
    TEST_ASSERT_EQUAL_INT(4, s_lockCalls);          // reset clears the table under the lock
    TEST_ASSERT_EQUAL_INT(0, s_lockDepth);
    gpsSkySetLock(NULL, NULL);
}

// GPS-SKY W4: on the T-Deck Pro bGPSON is forced false at boot (esp32_main.cpp), so a loop-side
// tdeck_set_gps(false) suspended gps_task every 3 s and froze GPS screen 3 and the sky feed.
// Only screen 3 (entry3/exit3) may start and stop that task.
void test_tdeck_pro_loop_does_not_suspend_gps_task(void)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_gps_skyplot/test_main.cpp";
    std::string path = file.substr(0, file.size() - suffix.size()) + "src/esp32/esp32_main.cpp";
    std::ifstream f(path.c_str(), std::ios::binary);
    TEST_ASSERT_TRUE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    TEST_ASSERT_TRUE(ss.str().find("tdeck_set_gps(false);") == std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum_good);
    RUN_TEST(test_checksum_bad);
    RUN_TEST(test_non_gsv_rejected);
    RUN_TEST(test_empty_fields);
    RUN_TEST(test_multi_sentence_group);
    RUN_TEST(test_signal_id_trailing_field);
    RUN_TEST(test_nmea41_four_groups_plus_signal_id);
    RUN_TEST(test_empty_padded_group_skipped);
    RUN_TEST(test_talker_mapping);
    RUN_TEST(test_same_prn_other_system_distinct);
    RUN_TEST(test_fresh_boundary);
    RUN_TEST(test_fresh_rollover);
    RUN_TEST(test_expiry_in_table);
    RUN_TEST(test_slot_reuse_expired);
    RUN_TEST(test_overflow_evicts_oldest);
    RUN_TEST(test_update_in_place);
    RUN_TEST(test_copy_respects_max);
    RUN_TEST(test_feed_bytewise);
    RUN_TEST(test_feed_ignores_noise_and_overlong);
    RUN_TEST(test_snr_levels);
    RUN_TEST(test_projection);
    RUN_TEST(test_projection_off_axis);
    RUN_TEST(test_lock_wraps_table_access);
    RUN_TEST(test_tdeck_pro_loop_does_not_suspend_gps_task);
    RUN_TEST(test_projection_clamps_elevation);
    RUN_TEST(test_age_valid);
    RUN_TEST(test_age_invalid_fix);
    RUN_TEST(test_age_uint32_max);
    return UNITY_END();
}
