// Host test for src/netmode_label.h (RAK-NETMODE-01, #1289): --info NETWORK Mode label.
//
//   pio test -e native_netmode_label

#include <unity.h>

#include "netmode_label.h"

void setUp(void) {}
void tearDown(void) {}

static void test_esp32_wifi_and_eth(void)
{
    TEST_ASSERT_EQUAL_STRING("WiFi", infoNetModeLabel(0, false, false));
    TEST_ASSERT_EQUAL_STRING("ETH", infoNetModeLabel(1, false, false));
}

static void test_rak_on_lan_is_eth_even_with_netmode_0(void)
{
    TEST_ASSERT_EQUAL_STRING("ETH", infoNetModeLabel(0, true, true));
    TEST_ASSERT_EQUAL_STRING("ETH", infoNetModeLabel(1, true, true));
}

static void test_nrf52_without_eth_is_none(void)
{
    TEST_ASSERT_EQUAL_STRING("none", infoNetModeLabel(0, true, false));
    TEST_ASSERT_EQUAL_STRING("none", infoNetModeLabel(1, true, false));
}

static void test_out_of_range_netmode_is_wifi(void)
{
    TEST_ASSERT_EQUAL_STRING("WiFi", infoNetModeLabel(5, false, false));
    TEST_ASSERT_EQUAL_STRING("WiFi", infoNetModeLabel(-1, false, false));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_esp32_wifi_and_eth);
    RUN_TEST(test_rak_on_lan_is_eth_even_with_netmode_0);
    RUN_TEST(test_nrf52_without_eth_is_none);
    RUN_TEST(test_out_of_range_netmode_is_wifi);
    return UNITY_END();
}
