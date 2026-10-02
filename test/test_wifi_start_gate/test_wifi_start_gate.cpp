// Native Testsuite fuer wifiSsidMissingBlocksStart() -- das Gate in
// startNetwork(), das den WiFi-Start bei fehlender SSID abbricht.
//
// Hintergrund: Das Gate lag vor dem bWIFIAP-Zweig. Ein frischer oder geloeschter
// ESP32-Knoten hat node_ssid = "none"; nach `--wifiap on` kam der softAP deshalb
// nie hoch (Display: "AP : <call>" ohne IP). Im AP-Modus ist die SSID
// irrelevant, das Gate gilt nur fuer STA.
//
//   pio test -e native -f test_wifi_start_gate

#include <unity.h>

#include <stddef.h>

#include <wifi_start_gate.h>

static void test_ap_modus_ssid_none_blockiert_nicht(void)
{
    // Regressionsfall: frischer Knoten, --wifiap on.
    TEST_ASSERT_FALSE(wifiSsidMissingBlocksStart(true, "none"));
}

static void test_ap_modus_leere_ssid_blockiert_nicht(void)
{
    TEST_ASSERT_FALSE(wifiSsidMissingBlocksStart(true, ""));
}

static void test_ap_modus_null_ssid_blockiert_nicht(void)
{
    TEST_ASSERT_FALSE(wifiSsidMissingBlocksStart(true, NULL));
}

static void test_sta_modus_ssid_none_blockiert(void)
{
    TEST_ASSERT_TRUE(wifiSsidMissingBlocksStart(false, "none"));
}

static void test_sta_modus_leere_ssid_blockiert(void)
{
    TEST_ASSERT_TRUE(wifiSsidMissingBlocksStart(false, ""));
}

static void test_sta_modus_null_ssid_blockiert(void)
{
    TEST_ASSERT_TRUE(wifiSsidMissingBlocksStart(false, NULL));
}

static void test_sta_modus_gesetzte_ssid_blockiert_nicht(void)
{
    TEST_ASSERT_FALSE(wifiSsidMissingBlocksStart(false, "MyWLAN"));
}

static void test_sta_modus_none_gross_geschrieben_ist_eine_ssid(void)
{
    // Exakter Vergleich wie is_equ(): "None" ist ein gueltiger SSID-Name.
    TEST_ASSERT_FALSE(wifiSsidMissingBlocksStart(false, "None"));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_ap_modus_ssid_none_blockiert_nicht);
    RUN_TEST(test_ap_modus_leere_ssid_blockiert_nicht);
    RUN_TEST(test_ap_modus_null_ssid_blockiert_nicht);
    RUN_TEST(test_sta_modus_ssid_none_blockiert);
    RUN_TEST(test_sta_modus_leere_ssid_blockiert);
    RUN_TEST(test_sta_modus_null_ssid_blockiert);
    RUN_TEST(test_sta_modus_gesetzte_ssid_blockiert_nicht);
    RUN_TEST(test_sta_modus_none_gross_geschrieben_ist_eine_ssid);

    return UNITY_END();
}
