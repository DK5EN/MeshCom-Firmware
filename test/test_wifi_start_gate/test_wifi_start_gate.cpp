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

// D15 (AU web GUI): Wahrheitstabelle autoupd x AP-Modus x SSID.
static void test_au_wahrheitstabelle(void)
{
    const char *ssids[] = {NULL, "", "none", "MyWLAN"};
    const bool ssidSet[] = {false, false, false, true};
    for(int au = 0; au <= 2; au++)
        for(int ap = 0; ap <= 1; ap++)
            for(int i = 0; i < 4; i++)
            {
                const bool want = au > 0 && !ap && ssidSet[i];
                TEST_ASSERT_EQUAL_MESSAGE(want, wifiAutoUpdateWantsStart(au, ap != 0, ssids[i]),
                                          ssids[i] ? ssids[i] : "(null)");
                // AU allein entscheidet, wenn kein anderer Dienst WiFi will
                TEST_ASSERT_EQUAL(want, wifiStartWanted(false, false, false, false, au, ap != 0, ssids[i]));
            }
}

static void test_au_negative_autoupd_startet_nicht(void)
{
    TEST_ASSERT_FALSE(wifiAutoUpdateWantsStart(-1, false, "MyWLAN"));
}

static void test_andere_dienste_starten_wifi_unabhaengig_von_au(void)
{
    // Gateway, Ext-UDP, Web, Net-Konsole: wie bisher, auch ohne SSID / im AP-Modus / mit AU 0
    TEST_ASSERT_TRUE(wifiStartWanted(true, false, false, false, 0, false, NULL));
    TEST_ASSERT_TRUE(wifiStartWanted(false, true, false, false, 0, true, "none"));
    TEST_ASSERT_TRUE(wifiStartWanted(false, false, true, false, 1, true, ""));
    TEST_ASSERT_TRUE(wifiStartWanted(false, false, false, true, 0, false, "MyWLAN"));
    TEST_ASSERT_FALSE(wifiStartWanted(false, false, false, false, 0, false, "MyWLAN"));
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
    RUN_TEST(test_au_wahrheitstabelle);
    RUN_TEST(test_au_negative_autoupd_startet_nicht);
    RUN_TEST(test_andere_dienste_starten_wifi_unabhaengig_von_au);

    return UNITY_END();
}
