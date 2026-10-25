// Host test for src/daylight.h -- sun elevation, day/night with hysteresis and the T-Deck
// backlight policy (TD-23).
//
//   pio test -e native_daylight
//
// Reference times: NOAA algorithm as implemented by the Python package astral 3.x
// (sunrise()/sunset(), sun centre at -0.833 deg), cross-checked against PyEphem
// (VSOP87, use_center=True, horizon -0:50, pressure 0): the two agree within 30 s for every row.
// Tolerance here is +-3 min.

#include <unity.h>

#include <stdint.h>
#include <math.h>

#include <daylight.h>

void setUp(void) {}
void tearDown(void) {}

#define SUNRISE_DEG (-0.833)
#define TOL_S 180

struct SunEvent
{
    const char *what;
    double lat, lon;
    uint32_t utc;  // reference event time
    bool rising;
};

static const SunEvent EVENTS[] = {
    // Munich 48.14 N 11.58 E
    {"munich jun rise 03:13:44Z", 48.14, 11.58, 1782011624u, true},
    {"munich jun set 19:17:14Z", 48.14, 11.58, 1782069434u, false},
    {"munich dec rise 07:01:32Z", 48.14, 11.58, 1797836492u, true},
    {"munich dec set 15:21:55Z", 48.14, 11.58, 1797866515u, false},
    {"munich mar rise 05:17:09Z", 48.14, 11.58, 1773983829u, true},
    {"munich mar set 17:25:58Z", 48.14, 11.58, 1774027558u, false},
    // Sydney 33.87 S 151.21 E (local winter in June)
    {"sydney jun set 06:53:35Z", -33.87, 151.21, 1782024815u, false},
    {"sydney jun rise 21:00:24Z", -33.87, 151.21, 1782075624u, true},
    {"sydney dec set 09:05:11Z", -33.87, 151.21, 1797843911u, false},
    {"sydney dec rise 18:41:21Z", -33.87, 151.21, 1797878481u, true},
    // New York 40.71 N 74.01 W (west of Greenwich)
    {"newyork jun rise 09:25:19Z", 40.71, -74.01, 1782033919u, true},
    {"newyork dec rise 12:16:51Z", 40.71, -74.01, 1797855411u, true},
    {"newyork dec set 21:31:34Z", 40.71, -74.01, 1797888694u, false},
    {"newyork mar set 23:07:58Z", 40.71, -74.01, 1774048078u, false},
    // Tromsoe 69.65 N 18.96 E at the equinox
    {"tromso mar rise 04:44:21Z", 69.65, 18.96, 1773981861u, true},
    {"tromso mar set 17:01:01Z", 69.65, 18.96, 1774026061u, false},
};

// First crossing of `deg` in the requested direction within +-2 h of `ref`, 10 s resolution.
static long find_crossing(double lat, double lon, uint32_t ref, bool rising, double deg)
{
    uint32_t t = ref - 7200;
    double prev = sun_elevation_deg(t, lat, lon);
    for (; t <= ref + 7200; t += 10)
    {
        const double e = sun_elevation_deg(t, lat, lon);
        if (rising ? (prev <= deg && e > deg) : (prev >= deg && e < deg))
            return (long)t;
        prev = e;
    }
    return -1;
}

static void test_sunrise_sunset_reference_sites(void)
{
    for (unsigned i = 0; i < sizeof(EVENTS) / sizeof(EVENTS[0]); i++)
    {
        const SunEvent &ev = EVENTS[i];
        const long t = find_crossing(ev.lat, ev.lon, ev.utc, ev.rising, SUNRISE_DEG);
        TEST_ASSERT_TRUE_MESSAGE(t > 0, ev.what);
        const long d = t - (long)ev.utc;
        TEST_ASSERT_TRUE_MESSAGE(d <= TOL_S && d >= -TOL_S, ev.what);
    }
}

// 2026-06-21 12:00Z (1782043200), astral elevation without refraction.
static void test_elevation_at_noon_utc(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.5, 63.765, sun_elevation_deg(1782043200u, 48.14, 11.58));
    TEST_ASSERT_FLOAT_WITHIN(0.5, -62.42, sun_elevation_deg(1782043200u, -33.87, 151.21));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 26.467, sun_elevation_deg(1782043200u, 40.71, -74.01));
    TEST_ASSERT_FLOAT_WITHIN(0.5, 42.493, sun_elevation_deg(1782043200u, 69.65, 18.96));
}

static void test_munich_day_and_night(void)
{
    // 2026-06-21 12:00Z day, 2026-06-21 23:00Z (1782082800) night
    TEST_ASSERT_TRUE(daylight_is_day(1782043200u, 48.14, 11.58, false));
    TEST_ASSERT_FALSE(daylight_is_day(1782082800u, 48.14, 11.58, true));
}

// Polar day / polar night: a whole day in steps of 5 min, never a switch.
static void test_polar_day_and_night(void)
{
    const uint32_t jun21 = 1782000000u - (1782000000u % 86400u);  // 2026-06-21 00:00Z
    const uint32_t dec21 = 1797811200u;                            // 2026-12-21 00:00Z
    bool day = true;
    for (uint32_t t = jun21; t < jun21 + 86400u; t += 300)
    {
        day = daylight_is_day(t, 69.65, 18.96, day);
        TEST_ASSERT_TRUE(day);
        TEST_ASSERT_FALSE(isnan(sun_elevation_deg(t, 69.65, 18.96)));
    }
    // December at 69.65 N is still twilight around noon (above -6 deg); 78 N stays night.
    bool night_day = false;
    for (uint32_t t = dec21; t < dec21 + 86400u; t += 300)
    {
        night_day = daylight_is_day(t, 78.22, 15.65, night_day);
        TEST_ASSERT_FALSE(night_day);
    }
    // The poles themselves: no NaN, constant period.
    TEST_ASSERT_TRUE(daylight_is_day(jun21, 90.0, 0.0, true));
    TEST_ASSERT_FALSE(daylight_is_day(dec21, 90.0, 0.0, false));
    TEST_ASSERT_FALSE(isnan(sun_elevation_deg(dec21, -90.0, 0.0)));
}

static void test_hysteresis(void)
{
    const double thr = DAYLIGHT_THRESHOLD_DEG;
    const double band = DAYLIGHT_HYSTERESIS_DEG;
    // Oscillating within the band never flips, whichever side we start on.
    const double wobble[] = {thr - 0.5 * band, thr + 0.5 * band, thr, thr - 0.9 * band, thr + 0.9 * band};
    bool day = true;
    for (unsigned i = 0; i < 5; i++)
    {
        day = daylight_is_day_elev(wobble[i], day);
        TEST_ASSERT_TRUE(day);
    }
    day = false;
    for (unsigned i = 0; i < 5; i++)
    {
        day = daylight_is_day_elev(wobble[i], day);
        TEST_ASSERT_FALSE(day);
    }
    // Leaving the band switches.
    TEST_ASSERT_FALSE(daylight_is_day_elev(thr - 1.1 * band, true));
    TEST_ASSERT_TRUE(daylight_is_day_elev(thr + 1.1 * band, false));
}

static void test_dusk_switches_once(void)
{
    // Munich 2026-03-20: from 16:00Z to 20:00Z in 1 min steps, exactly one day -> night flip.
    const uint32_t start = 1774022400u;  // 2026-03-20 16:00Z
    bool day = true;
    int flips = 0;
    for (uint32_t t = start; t < start + 4 * 3600u; t += 60)
    {
        const bool now = daylight_is_day(t, 48.14, 11.58, day);
        if (now != day)
            flips++;
        day = now;
    }
    TEST_ASSERT_EQUAL_INT(1, flips);
    TEST_ASSERT_FALSE(day);
}

static void test_position_valid(void)
{
    TEST_ASSERT_TRUE(daylight_position_valid('N', 'E', 48.14, 11.58));
    TEST_ASSERT_TRUE(daylight_position_valid('S', 'W', -33.0, -70.0));
    TEST_ASSERT_FALSE(daylight_position_valid(0, 'E', 48.14, 11.58));
    TEST_ASSERT_FALSE(daylight_position_valid('N', 'X', 48.14, 11.58));
    TEST_ASSERT_FALSE(daylight_position_valid('N', 'E', 0.0, 0.0));
    TEST_ASSERT_FALSE(daylight_position_valid('N', 'E', 91.0, 11.0));
    TEST_ASSERT_FALSE(daylight_position_valid('N', 'E', NAN, 11.0));
}

static void test_policy(void)
{
    // auto off -> day, whatever else holds
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_DAY, brightness_period(false, true, true, false));
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_DAY, brightness_period(false, false, false, false));
    // auto on without a trusted clock or a position -> fallback (day slot)
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_FALLBACK, brightness_period(true, false, true, false));
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_FALLBACK, brightness_period(true, true, false, false));
    // auto on, clock and position fine
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_NIGHT, brightness_period(true, true, true, false));
    TEST_ASSERT_EQUAL_INT(BL_PERIOD_DAY, brightness_period(true, true, true, true));

    TEST_ASSERT_EQUAL_UINT8(12, brightness_level_for(BL_PERIOD_DAY, 12, 2));
    TEST_ASSERT_EQUAL_UINT8(12, brightness_level_for(BL_PERIOD_FALLBACK, 12, 2));
    TEST_ASSERT_EQUAL_UINT8(2, brightness_level_for(BL_PERIOD_NIGHT, 12, 2));
    // invalid stored levels fall back to the defaults, never to 0 (panel off)
    TEST_ASSERT_EQUAL_UINT8(BL_DAY_DEFAULT, brightness_level_for(BL_PERIOD_DAY, 0, 2));
    TEST_ASSERT_EQUAL_UINT8(BL_NIGHT_DEFAULT, brightness_level_for(BL_PERIOD_NIGHT, 12, 17));

    TEST_ASSERT_EQUAL_STRING("day", brightness_period_name(BL_PERIOD_DAY));
    TEST_ASSERT_EQUAL_STRING("night", brightness_period_name(BL_PERIOD_NIGHT));
    TEST_ASSERT_EQUAL_STRING("fallback", brightness_period_name(BL_PERIOD_FALLBACK));
}

static void test_symb_writes_active_slot(void)
{
    int day = 16, night = 4;
    brightness_store_level(BL_PERIOD_NIGHT, 1, &day, &night);
    TEST_ASSERT_EQUAL_INT(16, day);
    TEST_ASSERT_EQUAL_INT(1, night);
    brightness_store_level(BL_PERIOD_DAY, 8, &day, &night);
    TEST_ASSERT_EQUAL_INT(8, day);
    TEST_ASSERT_EQUAL_INT(1, night);
    brightness_store_level(BL_PERIOD_FALLBACK, 12, &day, &night);
    TEST_ASSERT_EQUAL_INT(12, day);
    TEST_ASSERT_EQUAL_INT(1, night);
}

static void test_level_valid(void)
{
    TEST_ASSERT_FALSE(brightness_level_valid(0));
    TEST_ASSERT_TRUE(brightness_level_valid(1));
    TEST_ASSERT_TRUE(brightness_level_valid(16));
    TEST_ASSERT_FALSE(brightness_level_valid(17));
    TEST_ASSERT_FALSE(brightness_level_valid(-1));
}

static void test_step_table(void)
{
    const uint8_t cycle[] = {16, 12, 8, 4, 2, 1, 16};
    for (unsigned i = 0; i + 1 < sizeof(cycle); i++)
        TEST_ASSERT_EQUAL_UINT8(cycle[i + 1], brightness_next_step(cycle[i]));
    // off-table values step to the next lower entry
    TEST_ASSERT_EQUAL_UINT8(8, brightness_next_step(10));
    TEST_ASSERT_EQUAL_UINT8(2, brightness_next_step(3));
    TEST_ASSERT_EQUAL_UINT8(12, brightness_next_step(15));
    TEST_ASSERT_EQUAL_UINT8(4, brightness_next_step(5));
    // panel off and out of range -> brightest
    TEST_ASSERT_EQUAL_UINT8(16, brightness_next_step(0));
    TEST_ASSERT_EQUAL_UINT8(16, brightness_next_step(17));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sunrise_sunset_reference_sites);
    RUN_TEST(test_elevation_at_noon_utc);
    RUN_TEST(test_munich_day_and_night);
    RUN_TEST(test_polar_day_and_night);
    RUN_TEST(test_hysteresis);
    RUN_TEST(test_dusk_switches_once);
    RUN_TEST(test_position_valid);
    RUN_TEST(test_policy);
    RUN_TEST(test_symb_writes_active_slot);
    RUN_TEST(test_level_valid);
    RUN_TEST(test_step_table);
    return UNITY_END();
}
