// Upstream issue #1327: with GPS on but no position, the position interval falls back to --postime.
//
//   pio test -e native_gps_interval

#include <unity.h>

#include <string>

#include <gps_interval.h>

#include "../support/repo_file.h"

void setUp(void) {}
void tearDown(void) {}

static void test_postime_set_wins(void)
{
    TEST_ASSERT_EQUAL_UINT(300u, gpsNoFixInterval(300, 1800u));
    TEST_ASSERT_EQUAL_UINT(86400u, gpsNoFixInterval(86400, 1800u));
}

static void test_postime_unset_gives_default(void)
{
    TEST_ASSERT_EQUAL_UINT(1800u, gpsNoFixInterval(0, 1800u));
    TEST_ASSERT_EQUAL_UINT(1800u, gpsNoFixInterval(-5, 1800u));
}

static size_t countOf(const std::string &s, const std::string &needle)
{
    size_t n = 0;
    for (size_t p = s.find(needle); p != std::string::npos; p = s.find(needle, p + 1))
        n++;
    return n;
}

// Every "ten empty polls" fallback uses the helper; none sets POSINFO_INTERVAL directly any more.
static void pinFile(const char *path, size_t expectedSites)
{
    std::string src = repo_slurp(path);
    TEST_ASSERT_FALSE_MESSAGE(src.empty(), path);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expectedSites, countOf(src, "no_gps_reset_counter > 10"), path);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expectedSites,
        countOf(src, "posinfo_interval = gpsNoFixInterval(meshcom_settings.node_postime, POSINFO_INTERVAL);"), path);
    TEST_ASSERT_TRUE_MESSAGE(src.find("#include \"gps_interval.h\"") != std::string::npos, path);
}

static void test_pin_esp32_fallback_uses_postime(void) { pinFile("src/esp32/esp32_main.cpp", 1); }
static void test_pin_nrf52_fallbacks_use_postime(void) { pinFile("src/nrf52/nrf52_main.cpp", 2); }

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_postime_set_wins);
    RUN_TEST(test_postime_unset_gives_default);
    RUN_TEST(test_pin_esp32_fallback_uses_postime);
    RUN_TEST(test_pin_nrf52_fallbacks_use_postime);
    return UNITY_END();
}
