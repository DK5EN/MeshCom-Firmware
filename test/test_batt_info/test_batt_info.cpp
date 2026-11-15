// Native test for battInfoDecide(): which number --info shows after "BATTERY PIN".
// Legacy battery path (no USE_NEW_BATT) never reads fBattFaktor (0.0 at boot), so --info
// must show the real ADC_MULTIPLIER, labelled "multiplier", not "factor 0.0000".
//
//   tools/piolock.sh pio test -e native -f test_batt_info

#include <unity.h>
#include <string.h>
#include <stdio.h>

#include <batt_info.h>

void setUp(void) {}
void tearDown(void) {}

static void test_legacy_path_shows_multiplier_not_zero_factor(void)
{
    BattInfo i = battInfoDecide(false, true, 0.0f, 4.18f);
    TEST_ASSERT_TRUE(i.show);
    TEST_ASSERT_EQUAL_STRING("multiplier", i.label);
    TEST_ASSERT_EQUAL_FLOAT(4.18f, i.value);
    char b[48];
    snprintf(b, sizeof b, "%s %.4f", i.label, (double)i.value);
    TEST_ASSERT_EQUAL_STRING("multiplier 4.1800", b);
}

static void test_new_path_shows_settable_factor(void)
{
    BattInfo i = battInfoDecide(true, false, 1.0f, 0.0f);
    TEST_ASSERT_TRUE(i.show);
    TEST_ASSERT_EQUAL_STRING("factor", i.label);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, i.value);
}

static void test_new_path_ignores_multiplier_even_if_defined(void)
{
    BattInfo i = battInfoDecide(true, true, 1.0231f, 4.9245f);
    TEST_ASSERT_EQUAL_STRING("factor", i.label);
    TEST_ASSERT_EQUAL_FLOAT(1.0231f, i.value);
}

static void test_legacy_without_multiplier_omits_value(void)
{
    BattInfo i = battInfoDecide(false, false, 0.0f, 0.0f);
    TEST_ASSERT_FALSE(i.show);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_legacy_path_shows_multiplier_not_zero_factor);
    RUN_TEST(test_new_path_shows_settable_factor);
    RUN_TEST(test_new_path_ignores_multiplier_even_if_defined);
    RUN_TEST(test_legacy_without_multiplier_omits_value);
    return UNITY_END();
}
