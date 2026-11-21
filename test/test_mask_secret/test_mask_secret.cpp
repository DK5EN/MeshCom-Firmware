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
#include <string>

#include <mask_secret.h>
#include <settings_dump_mask.h>

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

static std::string mask_line(const char *line, size_t cap = 256)
{
    char out[256];
    size_t n = settingsDumpMaskLine(line, strlen(line), out, cap);
    TEST_ASSERT_EQUAL_UINT32(strlen(out), n);
    return std::string(out);
}

static void test_dump_mask_secret_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("node_passwd=***", mask_line("node_passwd=geheim").c_str());
    TEST_ASSERT_EQUAL_STRING("node_webpwd=***", mask_line("node_webpwd=x").c_str());
    TEST_ASSERT_EQUAL_STRING("node_lpwd=***", mask_line("node_lpwd=wlan psk").c_str());
    TEST_ASSERT_EQUAL_STRING("node_pwd=***", mask_line("node_pwd=srv").c_str());
}

static void test_dump_mask_unset_stays_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("node_webpwd=", mask_line("node_webpwd=").c_str());
    TEST_ASSERT_EQUAL_STRING("node_passwd=", mask_line("node_passwd=              ").c_str());
}

static void test_dump_mask_bt_code(void)
{
    TEST_ASSERT_EQUAL_STRING("bt_code=0", mask_line("bt_code=0").c_str());
    TEST_ASSERT_EQUAL_STRING("bt_code=***", mask_line("bt_code=731942").c_str());
    TEST_ASSERT_EQUAL_STRING("bt_code=***", mask_line("bt_code=00").c_str());
}

static void test_dump_mask_non_secret_unchanged(void)
{
    TEST_ASSERT_EQUAL_STRING("node_call=DK5EN-90", mask_line("node_call=DK5EN-90").c_str());
    TEST_ASSERT_EQUAL_STRING("no equals sign", mask_line("no equals sign").c_str());
    TEST_ASSERT_EQUAL_STRING("node_passwd_x=abc", mask_line("node_passwd_x=abc").c_str());
}

static void test_dump_mask_long_line(void)
{
    std::string v(150, 'p');
    std::string in = "node_lpwd=" + v;
    TEST_ASSERT_EQUAL_STRING("node_lpwd=***", mask_line(in.c_str()).c_str());
    std::string ns = "node_eqns=" + v;
    TEST_ASSERT_EQUAL_STRING(ns.c_str(), mask_line(ns.c_str()).c_str());
}

static void test_dump_mask_small_outcap_returns_zero(void)
{
    char out[8];
    memset(out, 'Z', sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(0, settingsDumpMaskLine("node_webpwd=abc", 15, out, 8));
    TEST_ASSERT_EQUAL_UINT32(0, settingsDumpMaskLine("node_call=DK5EN-90", 18, out, 8));
    TEST_ASSERT_EQUAL_UINT32(0, settingsDumpMaskLine("a=b", 3, out, 0));
    TEST_ASSERT_EQUAL_CHAR('Z', out[7]);
}

static void test_dump_key_table(void)
{
    TEST_ASSERT_TRUE(settingsDumpKeyIsSecret("bt_code", 7));
    TEST_ASSERT_TRUE(settingsDumpKeyIsSecret("node_aprsispas", 14));
    TEST_ASSERT_FALSE(settingsDumpKeyIsSecret("node_pwdx", 9));
    TEST_ASSERT_FALSE(settingsDumpKeyIsSecret("node_call", 9));
    TEST_ASSERT_FALSE(settingsDumpKeyIsSecret(NULL, 0));
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

    RUN_TEST(test_dump_mask_secret_strings);
    RUN_TEST(test_dump_mask_unset_stays_empty);
    RUN_TEST(test_dump_mask_bt_code);
    RUN_TEST(test_dump_mask_non_secret_unchanged);
    RUN_TEST(test_dump_mask_long_line);
    RUN_TEST(test_dump_mask_small_outcap_returns_zero);
    RUN_TEST(test_dump_key_table);

    return UNITY_END();
}
