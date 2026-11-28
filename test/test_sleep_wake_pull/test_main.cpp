// Upstream issue #1341: the deep sleep wake pin gets the internal pull-up also with --button off.
//
//   pio test -e native_sleep_wake_pull

#include <unity.h>

#include <string>

#include <sleep_wake_pull.h>

#include "../support/repo_file.h"

void setUp(void) {}
void tearDown(void) {}

static void test_plain_pin_gets_pullup(void)
{
    TEST_ASSERT_TRUE(sleepWakeNeedsPullup(0, false, true));    // Heltec V3 style GPIO0
    TEST_ASSERT_TRUE(sleepWakeNeedsPullup(38, false, true));   // T-Beam
    TEST_ASSERT_TRUE(sleepWakeNeedsPullup(12, false, false));  // GPIO12 is no strapping hazard on S3
}

static void test_no_pin_no_pullup(void)
{
    TEST_ASSERT_FALSE(sleepWakeNeedsPullup(99, false, true));
    TEST_ASSERT_FALSE(sleepWakeNeedsPullup(-1, false, false));
}

static void test_external_pullup_boards_untouched(void)
{
    TEST_ASSERT_FALSE(sleepWakeNeedsPullup(0, true, false));
    TEST_ASSERT_FALSE(sleepWakeNeedsPullup(21, true, true));
}

static void test_mtdi_strapping_pin_untouched_on_classic_esp32(void)
{
    TEST_ASSERT_FALSE(sleepWakeNeedsPullup(12, false, true));
}

// The pull-up is set inside the ext1 block, before esp_sleep_enable_ext1_wakeup().
static void test_pin_sleep_sets_pullup_before_ext1(void)
{
    std::string src = repo_slurp("src/esp32/esp32_sleep.cpp");
    TEST_ASSERT_FALSE(src.empty());
    size_t guard = src.find("if (iButtonPin != 99)");
    TEST_ASSERT_TRUE(guard != std::string::npos);
    size_t helper = src.find("sleepWakeNeedsPullup(iButtonPin,", guard);
    size_t pull = src.find("rtc_gpio_pullup_en((gpio_num_t)iButtonPin)", guard);
    size_t ext1 = src.find("esp_sleep_enable_ext1_wakeup(", guard);
    TEST_ASSERT_TRUE_MESSAGE(helper != std::string::npos, "helper call in the ext1 block");
    TEST_ASSERT_TRUE_MESSAGE(pull != std::string::npos, "RTC pull-up on the wake pin");
    TEST_ASSERT_TRUE(ext1 != std::string::npos);
    TEST_ASSERT_TRUE(helper < pull);
    TEST_ASSERT_TRUE(pull < ext1);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_pin_gets_pullup);
    RUN_TEST(test_no_pin_no_pullup);
    RUN_TEST(test_external_pullup_boards_untouched);
    RUN_TEST(test_mtdi_strapping_pin_untouched_on_classic_esp32);
    RUN_TEST(test_pin_sleep_sets_pullup_before_ext1);
    return UNITY_END();
}
