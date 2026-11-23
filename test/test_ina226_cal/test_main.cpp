// INA226-CAL-01: INA226 calibration pair imax x shunt (src/ina226_cal.h, header-only).
//
//   pio test -e native_ina226_cal
//
// Field case 2026-11-22 (Heltec V3, 0.1 Ohm module): with the default 0.002 Ohm the current read 50x
// high; the obvious fix `--shunt 0.1` kept the default imax 20 A, 20 A x 0.1 Ohm = 2 V > 81.9 mV, and
// RobTillaart INA226::setMaxCurrentShunt refused the pair without writing a calibration (0 mA).
// libAccepts() is the library's own check; oldSetupPair() is the pre-fix setup rule kept as comparison.

#include <unity.h>

#include "ina226_cal.h"

using namespace ina226cal;

void setUp(void) {}
void tearDown(void) {}

// INA226 0.6.6 setMaxCurrentShunt guard, in its own order
static bool libAccepts(float maxCurrent, float shunt)
{
    if(maxCurrent * shunt > 0.08190f) return false;
    if(maxCurrent < 0.001f) return false;
    if(shunt < 0.001f) return false;
    return true;
}

// pre-fix setupINA226: stored pair used as is when 0.001 < shunt < 0.5, else (20, 0.002)
static float oldSetupImax(float imax, float shunt) { return (shunt > 0.001f && shunt < 0.5f) ? imax : 20.0f; }

static void test_field_case_old_rule_was_rejected(void)
{
    // documents the bug: the old setup handed (20 A, 0.1 Ohm) to the library, which refused it
    TEST_ASSERT_FALSE(libAccepts(oldSetupImax(20.0f, 0.1f), 0.1f));
}

static void test_field_case_new_rule_is_accepted(void)
{
    float imax = effectiveImax(20.0f, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.81f, imax);
    TEST_ASSERT_TRUE(libAccepts(imax, 0.1f));
    TEST_ASSERT_TRUE(pairOk(imax, 0.1f));
}

static void test_default_pair_unchanged(void)
{
    TEST_ASSERT_TRUE(pairOk(20.0f, 0.002f));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, effectiveImax(20.0f, 0.002f));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, imaxLimit(0.002f));   // 40.95 A capped at 20
}

static void test_fitting_imax_is_kept(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.5f, effectiveImax(0.5f, 0.1f));
    TEST_ASSERT_EQUAL_FLOAT(0.8f, effectiveImax(0.8f, 0.1f));
}

static void test_limit_always_accepted_by_library(void)
{
    // every shunt the command allows, in 1 mOhm steps: the limit and the effective imax pass the library
    for(int m = 1; m <= 500; m++)
    {
        float shunt = m / 1000.0f;
        float lim = imaxLimit(shunt);
        TEST_ASSERT_TRUE_MESSAGE(lim >= kMinImaxA, "limit below 10 mA");
        TEST_ASSERT_TRUE_MESSAGE(libAccepts(lim, shunt), "limit rejected by library");
        TEST_ASSERT_TRUE_MESSAGE(libAccepts(effectiveImax(20.0f, shunt), shunt), "effective imax rejected");
        TEST_ASSERT_TRUE_MESSAGE(libAccepts(effectiveImax(0.0f, shunt), shunt), "imax 0 not repaired");
    }
}

static void test_over_limit_pairs_refused(void)
{
    TEST_ASSERT_FALSE(pairOk(0.82f, 0.1f));
    TEST_ASSERT_FALSE(pairOk(20.0f, 0.01f));   // 200 mV
    TEST_ASSERT_TRUE(pairOk(8.19f, 0.01f));
}

static void test_out_of_range_values(void)
{
    TEST_ASSERT_FALSE(shuntInRange(0.0005f));
    TEST_ASSERT_FALSE(shuntInRange(0.6f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, imaxLimit(0.6f));
    TEST_ASSERT_FALSE(pairOk(0.0f, 0.002f));    // old --imax accepted 0, the library does not
    TEST_ASSERT_FALSE(pairOk(21.0f, 0.002f));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_field_case_old_rule_was_rejected);
    RUN_TEST(test_field_case_new_rule_is_accepted);
    RUN_TEST(test_default_pair_unchanged);
    RUN_TEST(test_fitting_imax_is_kept);
    RUN_TEST(test_limit_always_accepted_by_library);
    RUN_TEST(test_over_limit_pairs_refused);
    RUN_TEST(test_out_of_range_values);
    return UNITY_END();
}
