// Native Testsuite fuer netConsoleNetworkReady() -- das Gate in
// loopNetConsole(), das den Listening-Socket erst bei bereitem Netz oeffnet.
//
// Regressionsfall P-0111: im reinen AP-Modus ist WiFi.status() nie
// WL_CONNECTED; die Netconsole startete deshalb nie.
//
//   pio test -e native -f test_net_console_gate

#include <unity.h>

#include <net_console_gate.h>
#include <net_console_slot.h>

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

// ---- F2-10-SLOT: handshake-slot takeover (net_console_slot.h) ----

static void test_slot_preempt_exakt_bei_3000ms(void)
{
    TEST_ASSERT_TRUE(netConsoleSlotMayPreempt(true, false, 1000u + NET_CONSOLE_HS_SILENT_MS, 1000u));
}

static void test_slot_nicht_bei_2999ms(void)
{
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, false, 1000u + NET_CONSOLE_HS_SILENT_MS - 1u, 1000u));
}

static void test_slot_nie_wenn_rx_gesehen(void)
{
    // Ein sendender Halter wird nie verdraengt, auch nicht nach dem 10-s-Deadline.
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, true, 1000u + NET_CONSOLE_HS_SILENT_MS, 1000u));
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, true, 1000u + 60000u, 1000u));
}

static void test_slot_nicht_wenn_frei(void)
{
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(false, false, 1000u + 60000u, 1000u));
}

static void test_slot_millis_umlauf(void)
{
    // startMs kurz vor dem Ueberlauf von millis(): die Differenz muss modulo 2^32 gerechnet werden.
    const uint32_t start = 0xFFFFFF00u;
    TEST_ASSERT_TRUE(netConsoleSlotMayPreempt(true, false, start + NET_CONSOLE_HS_SILENT_MS, start));
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, false, start + NET_CONSOLE_HS_SILENT_MS - 1u, start));
    // Jetzt hinter dem Umlauf: nowMs < startMs numerisch.
    TEST_ASSERT_TRUE(start + NET_CONSOLE_HS_SILENT_MS < start);
}

static void test_slot_rxseen_nur_eigene_generation(void)
{
    // Der verdraengte alte Task schreibt spaet seine Generation 5; der neue Halter ist Generation 6.
    TEST_ASSERT_FALSE(netConsoleSlotRxSeen(5u, 6u));
    TEST_ASSERT_TRUE(netConsoleSlotRxSeen(6u, 6u));
    // Ende-zu-Ende: der neue stille Halter bleibt verdraengbar, obwohl der alte Task spaet "rx" meldet.
    uint32_t curGen = 6u, rxGen = 5u;
    TEST_ASSERT_TRUE(netConsoleSlotMayPreempt(true, netConsoleSlotRxSeen(rxGen, curGen), 10000u, 10000u - NET_CONSOLE_HS_SILENT_MS));
    rxGen = 6u;   // der neue Halter sendet selbst
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, netConsoleSlotRxSeen(rxGen, curGen), 10000u, 0u));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_nur_ap_ist_bereit);
    RUN_TEST(test_nur_sta_ist_bereit);
    RUN_TEST(test_nur_ethernet_ist_bereit);
    RUN_TEST(test_ap_und_sta_ist_bereit);
    RUN_TEST(test_kein_interface_ist_nicht_bereit);

    RUN_TEST(test_slot_preempt_exakt_bei_3000ms);
    RUN_TEST(test_slot_nicht_bei_2999ms);
    RUN_TEST(test_slot_nie_wenn_rx_gesehen);
    RUN_TEST(test_slot_nicht_wenn_frei);
    RUN_TEST(test_slot_millis_umlauf);
    RUN_TEST(test_slot_rxseen_nur_eigene_generation);

    return UNITY_END();
}
