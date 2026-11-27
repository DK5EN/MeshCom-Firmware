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

// FORMAL-NETCON-MARK (#1339): the trace of netconsole_today_kickmarked.cfg. Holder gen 1 is preempted, gen 2
// takes the slot and receives a byte (mark = 2); then task 1 wakes late (EOF) and marks. The mark must stay 2,
// so gen 2 is not silent and a third connection may not take its slot.
static void test_mark_spaeter_alter_task_ueberschreibt_nicht(void)
{
    uint32_t rx = 0, gen = 1;
    rx = netConsoleSlotMark(rx, gen, 1);           // task 1 receives a byte
    gen = 2;                                       // connection 2 takes the slot over
    TEST_ASSERT_FALSE(netConsoleSlotRxSeen(rx, gen));
    rx = netConsoleSlotMark(rx, gen, 2);           // task 2 receives a byte
    TEST_ASSERT_TRUE(netConsoleSlotRxSeen(rx, gen));
    rx = netConsoleSlotMark(rx, gen, 1);           // task 1 wakes late and marks
    TEST_ASSERT_TRUE_MESSAGE(netConsoleSlotRxSeen(rx, gen), "late mark of the preempted task made the holder look silent");
    TEST_ASSERT_FALSE(netConsoleSlotMayPreempt(true, netConsoleSlotRxSeen(rx, gen), 10000, 0));
}

static void test_mark_aktueller_halter_setzt(void)
{
    TEST_ASSERT_EQUAL_UINT32(7, netConsoleSlotMark(3, 7, 7));
    TEST_ASSERT_EQUAL_UINT32(3, netConsoleSlotMark(3, 7, 6));
    TEST_ASSERT_EQUAL_UINT32(3, netConsoleSlotMark(3, 7, 8));
}

static void test_free_nur_eigener_slot(void)
{
    TEST_ASSERT_TRUE(netConsoleSlotMayFree(5, 5));
    TEST_ASSERT_FALSE(netConsoleSlotMayFree(6, 5));
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
    RUN_TEST(test_mark_spaeter_alter_task_ueberschreibt_nicht);
    RUN_TEST(test_mark_aktueller_halter_setzt);
    RUN_TEST(test_free_nur_eigener_slot);

    return UNITY_END();
}
