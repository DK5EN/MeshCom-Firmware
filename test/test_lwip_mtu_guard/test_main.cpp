#include <unity.h>
#include "esp32/lwip_mtu_guard.h"

void setUp(void) {}
void tearDown(void) {}

void test_zero_stays_zero() { TEST_ASSERT_EQUAL_UINT16(0, mcMtuFloor(0)); }
void test_rogue_range_raised()
{
    TEST_ASSERT_EQUAL_UINT16(576, mcMtuFloor(20));
    TEST_ASSERT_EQUAL_UINT16(576, mcMtuFloor(27));
    TEST_ASSERT_EQUAL_UINT16(576, mcMtuFloor(1));
    TEST_ASSERT_EQUAL_UINT16(576, mcMtuFloor(575));
}
void test_floor_and_above_pass()
{
    TEST_ASSERT_EQUAL_UINT16(576, mcMtuFloor(576));
    TEST_ASSERT_EQUAL_UINT16(577, mcMtuFloor(577));
    TEST_ASSERT_EQUAL_UINT16(1500, mcMtuFloor(1500));
    TEST_ASSERT_EQUAL_UINT16(65535, mcMtuFloor(65535));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_stays_zero);
    RUN_TEST(test_rogue_range_raised);
    RUN_TEST(test_floor_and_above_pass);
    return UNITY_END();
}
