// Erste native Testsuite: Rufzeichen-Validierung (checkRegexCall).
//
// Warum diese zuerst: checkRegexCall entscheidet, welche Absender- und
// Zielrufzeichen im Mesh akzeptiert werden. Sie ist reine Ein-/Ausgabe-Logik,
// haengt nur an Regexp.cpp und ist damit ohne jede Quelltextaenderung nativ
// pruefbar.
//
// Die Erwartungswerte stammen aus dem Amateurfunk-Rufzeichenschema und aus den
// im Code explizit gelisteten Sonderfaellen — nicht aus der Ausgabe der
// Funktion selbst. Ein Test, dessen Sollwert vom Pruefling erzeugt wurde,
// kann nur Regressionen finden, keine bestehenden Fehler.
//
//   pio test -e native

#include <unity.h>

#include <Arduino.h>
#include <regex_functions.h>

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------ gueltige Rufzeichen

static void test_akzeptiert_gaengige_rufzeichen(void)
{
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("OE1KBC"), "OE1KBC");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DK5EN"), "DK5EN");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DL2JA"), "DL2JA");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DB0ED"), "DB0ED");
}

static void test_akzeptiert_ssid_suffix(void)
{
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DK5EN-1"), "einstellige SSID");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DK5EN-98"), "zweistellige SSID");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("DL2JA-2"), "SSID 2");
}

// ------------------------------------------------------- dokumentierte Sonderfaelle
// Diese Werte stehen als explizite Vergleiche in regex_functions.cpp.

static void test_akzeptiert_dienstkennungen(void)
{
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("*"), "TOALL");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("H"), "HEY");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("HG"), "HEY vom Gateway");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("BOT GATE"), "BOT GATE");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("WLNK-1"), "WLNK-1");
    TEST_ASSERT_TRUE_MESSAGE(checkRegexCall("APRS2SOTA"), "APRS2SOTA");
}

static void test_lehnt_swl_kennung_de_ab(void)
{
    // "DE" ist eine SWL-Kennung, kein sendeberechtigtes Rufzeichen.
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall("DE"), "DE muss abgelehnt werden");
}

// ---------------------------------------------------------------- Randfaelle

static void test_lehnt_leeres_rufzeichen_ab(void)
{
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall(""), "leerer String");
}

static void test_lehnt_offensichtlichen_unsinn_ab(void)
{
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall("ABCDEFGH"), "nur Buchstaben, keine Ziffer");
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall("12345"), "nur Ziffern");
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall("OE1KBC!"), "Sonderzeichen am Ende");
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall(" OE1KBC"), "fuehrendes Leerzeichen");
}

// Sehr langes Rufzeichen: darf nicht abstuerzen und muss abgelehnt werden.
// Regexp.cpp arbeitet mit setjmp/longjmp — ein Ueberlauf waere hier sichtbar.
static void test_ueberlanges_rufzeichen_stuerzt_nicht_ab(void)
{
    String lang;
    for (int i = 0; i < 300; i++)
        lang += "A";
    TEST_ASSERT_FALSE_MESSAGE(checkRegexCall(lang), "300 Zeichen muessen abgelehnt werden");
}

// ------------------------------------------------------------- Shim-Selbsttest
// Wenn der Shim falsch ist, sind alle anderen Ergebnisse wertlos.

static void test_shim_string_verhaelt_sich_wie_erwartet(void)
{
    String s = "DK5EN-98";
    TEST_ASSERT_EQUAL_UINT(8, s.length());
    TEST_ASSERT_EQUAL_INT(5, s.indexOf('-'));
    TEST_ASSERT_EQUAL_STRING("DK5EN", s.substring(0, 5).c_str());
    TEST_ASSERT_EQUAL_STRING("98", s.substring(6).c_str());
    TEST_ASSERT_TRUE(s.startsWith("DK5"));
    TEST_ASSERT_EQUAL_INT(0, s.compareTo(String("DK5EN-98")));
}

static void test_shim_uhr_ist_steuerbar(void)
{
    mc_test_set_millis(1000);
    TEST_ASSERT_EQUAL_UINT32(1000, millis());
    mc_test_advance_millis(500);
    TEST_ASSERT_EQUAL_UINT32(1500, millis());
    // Verdict Finding 8a: Shim-Uhr auf 0 zuruecksetzen -- dieser Test liess
    // sie zuvor bei 1500 stehen und beeinflusste damit jeden spaeter in
    // dieser Suite laufenden Test, der sich (unbewusst) auf millis()==0 beim
    // Start verlaesst.
    mc_test_set_millis(0);
}

static void test_shim_zufall_ist_deterministisch(void)
{
    randomSeed(12345);
    long a1 = random(0, 11), a2 = random(0, 11), a3 = random(0, 11);
    randomSeed(12345);
    TEST_ASSERT_EQUAL_INT32(a1, random(0, 11));
    TEST_ASSERT_EQUAL_INT32(a2, random(0, 11));
    TEST_ASSERT_EQUAL_INT32(a3, random(0, 11));
}

// ------------------------------------------------------------ Board-Profil
// Sichert, dass der native Build dieselben Ringgroessen sieht wie die Boards,
// gegen die wir testen. Ohne explizites Profil faellt configuration_global.h
// in den #else-Zweig (MAX_MHEARD 30 / MAX_DEDUP_RING 70) und die Tests wuerden
// eine Konfiguration pruefen, die auf keiner Hardware existiert.

static void test_board_profil_ist_gepinnt(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, MAX_MHEARD, "MAX_MHEARD (ESP32-S3/RAK-Profil)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, MAX_MHPATH, "MAX_MHPATH");
    TEST_ASSERT_EQUAL_INT_MESSAGE(20, MAX_RING, "MAX_RING");
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, MAX_DEDUP_RING, "MAX_DEDUP_RING");
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_shim_string_verhaelt_sich_wie_erwartet);
    RUN_TEST(test_shim_uhr_ist_steuerbar);
    RUN_TEST(test_shim_zufall_ist_deterministisch);
    RUN_TEST(test_board_profil_ist_gepinnt);

    RUN_TEST(test_akzeptiert_gaengige_rufzeichen);
    RUN_TEST(test_akzeptiert_ssid_suffix);
    RUN_TEST(test_akzeptiert_dienstkennungen);
    RUN_TEST(test_lehnt_swl_kennung_de_ab);
    RUN_TEST(test_lehnt_leeres_rufzeichen_ab);
    RUN_TEST(test_lehnt_offensichtlichen_unsinn_ab);
    RUN_TEST(test_ueberlanges_rufzeichen_stuerzt_nicht_ab);

    return UNITY_END();
}
