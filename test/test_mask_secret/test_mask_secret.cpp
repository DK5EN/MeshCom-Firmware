// Native Testsuite fuer maskSecret() -- die Maskierung von Passwoertern in der
// Klartextausgabe der Firmware.
//
// Hintergrund: --info druckte node_passwd, node_webpwd und node_pwd (das
// WLAN-PSK) im Klartext, und diese Ausgabe geht ueber printfdeb() auch an die
// Netzkonsole auf Port 2323. Ohne gesetztes node_passwd verlangt die keine
// Authentisierung -- am laufenden DK5EN-98 (25.08.2026) genuegte ein
// `nc dk5en-98.local 2323` und ein `--info`, um das WLAN-Passwort zu lesen.
//
//   pio test -e native -f test_mask_secret

#include <unity.h>

#include <string.h>

#include <mask_secret.h>

static void test_gesetztes_passwort_wird_maskiert(void)
{
    TEST_ASSERT_EQUAL_STRING("***", maskSecret("geheim123"));
}

static void test_maskierung_verraet_die_laenge_nicht(void)
{
    // Immer drei Sterne, unabhaengig von der Passwortlaenge.
    TEST_ASSERT_EQUAL_STRING(maskSecret("x"), maskSecret("vierzehnzeiche"));
}

static void test_leeres_passwort_bleibt_leer(void)
{
    // Die Zeile soll weiterhin zeigen, dass KEIN Passwort gesetzt ist.
    TEST_ASSERT_EQUAL_STRING("", maskSecret(""));
}

static void test_leerzeichen_gilt_als_nicht_gesetzt(void)
{
    // node_passwd wird mit "%-14.14s" gespeichert und ist damit rechts mit
    // Leerzeichen aufgefuellt; ein fuehrendes Leerzeichen heisst "nicht
    // gesetzt" -- dieselbe Pruefung wie hasPasswd im --passwd-Zweig.
    TEST_ASSERT_EQUAL_STRING("", maskSecret("              "));
}

static void test_nullzeiger_liefert_leeren_string(void)
{
    // Rueckgabe geht direkt an %s, NULL waere dort undefiniert.
    TEST_ASSERT_NOT_NULL(maskSecret(NULL));
    TEST_ASSERT_EQUAL_STRING("", maskSecret(NULL));
}

static void test_passwort_mit_innenliegendem_leerzeichen_wird_maskiert(void)
{
    TEST_ASSERT_EQUAL_STRING("***", maskSecret("zwei woerter"));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_gesetztes_passwort_wird_maskiert);
    RUN_TEST(test_maskierung_verraet_die_laenge_nicht);
    RUN_TEST(test_leeres_passwort_bleibt_leer);
    RUN_TEST(test_leerzeichen_gilt_als_nicht_gesetzt);
    RUN_TEST(test_nullzeiger_liefert_leeren_string);
    RUN_TEST(test_passwort_mit_innenliegendem_leerzeichen_wird_maskiert);

    return UNITY_END();
}
