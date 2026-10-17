// Native Testsuite fuer netConsoleNetworkReady() -- das Gate in
// loopNetConsole(), das den Listening-Socket erst bei bereitem Netz oeffnet.
//
// Regressionsfall P-0111: im reinen AP-Modus ist WiFi.status() nie
// WL_CONNECTED; die Netconsole startete deshalb nie.
//
//   pio test -e native -f test_net_console_gate

#include <unity.h>

#include <net_console_gate.h>

static void test_nur_ap_ist_bereit(void)
{
    // Regressionsfall P-0111: reiner Access Point, kein STA, kein Ethernet.
    TEST_ASSERT_TRUE(netConsoleNetworkReady(false, true, false));
}

static void test_nur_sta_ist_bereit(void)
{
    TEST_ASSERT_TRUE(netConsoleNetworkReady(true, false, false));
}

static void test_nur_ethernet_ist_bereit(void)
{
    TEST_ASSERT_TRUE(netConsoleNetworkReady(false, false, true));
}

static void test_ap_und_sta_ist_bereit(void)
{
    TEST_ASSERT_TRUE(netConsoleNetworkReady(true, true, false));
}

static void test_kein_interface_ist_nicht_bereit(void)
{
    // Ohne Interface wuerde ::socket() im lwIP-Assert enden.
    TEST_ASSERT_FALSE(netConsoleNetworkReady(false, false, false));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_nur_ap_ist_bereit);
    RUN_TEST(test_nur_sta_ist_bereit);
    RUN_TEST(test_nur_ethernet_ist_bereit);
    RUN_TEST(test_ap_und_sta_ist_bereit);
    RUN_TEST(test_kein_interface_ist_nicht_bereit);

    return UNITY_END();
}
