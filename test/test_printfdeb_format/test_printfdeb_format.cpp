// Native Testsuite fuer printfdebRewriteFormat() -- den Umbau des
// Format-Strings, den printfdeb() vor vsnprintf() vornimmt.
//
// Hintergrund: der '%%'-Zweig schrieb zwei Prozentzeichen und fiel anschliessend
// in die allgemeine Kopie, die dasselbe '%' noch einmal anhaengte; der naechste
// Schleifendurchlauf fuegte das zweite '%' erneut hinzu. Aus '%%' wurden vier
// Zeichen, vsnprintf machte daraus zwei Prozentzeichen. Sichtbar an jedem
// Knoten in jeder Zeile mit Prozentzeichen -- am laufenden DK5EN-98
// (25.08.2026) mitgeschnitten:
//
//   [MC-DBG] CHANNEL_UTIL rx=178272ms tx=4936ms util=18%%
//   ...BATT 100 %%
//
// Das trifft auch die Logauswertung: tools/loganalyse.sh und die Regex in
// tools/logharvest.py lesen diese Zeilen.
//
//   pio test -e native -f test_printfdeb_format

#include <unity.h>

#include <string.h>

#include <printfdeb_format.h>

static char out[300];

static const char *rewrite(const char *in, bool csv = false)
{
    memset(out, 0x7F, sizeof(out));   // Fuellmuster: deckt fehlende NUL auf
    printfdebRewriteFormat(in, out, sizeof(out), csv);
    return out;
}

// ---------------------------------------------------------------------------
// Der eigentliche Regressionsfall
// ---------------------------------------------------------------------------

static void test_prozent_escape_bleibt_ein_paar(void)
{
    TEST_ASSERT_EQUAL_STRING("util=%u%%\n", rewrite("util=%u%%\n"));
}

static void test_prozent_escape_am_stringende(void)
{
    TEST_ASSERT_EQUAL_STRING("100 %%", rewrite("100 %%"));
}

static void test_mehrere_prozent_escapes(void)
{
    TEST_ASSERT_EQUAL_STRING("%% %d %% %s %%", rewrite("%% %d %% %s %%"));
}

static void test_einzelnes_prozent_bleibt_unveraendert(void)
{
    TEST_ASSERT_EQUAL_STRING("%s=%d", rewrite("%s=%d"));
}

static void test_prozent_am_stringende_ohne_partner(void)
{
    // uformat[in+1] ist hier die NUL -- der Zweig darf nicht darueber hinaus
    // lesen und das '%' muss stehen bleiben.
    TEST_ASSERT_EQUAL_STRING("rate %", rewrite("rate %"));
}

// ---------------------------------------------------------------------------
// Semikolon-Behandlung (bestehendes Verhalten, hier eingefroren)
// ---------------------------------------------------------------------------

static void test_semikolon_wird_im_csv_modus_behalten(void)
{
    TEST_ASSERT_EQUAL_STRING("a;b", rewrite("a;b", true));
}

static void test_semikolon_wird_ohne_csv_zu_leerzeichen(void)
{
    TEST_ASSERT_EQUAL_STRING("a b", rewrite("a;b", false));
}

static void test_semikolon_neben_leerzeichen_faellt_weg(void)
{
    TEST_ASSERT_EQUAL_STRING("a b", rewrite("a; b", false));
    TEST_ASSERT_EQUAL_STRING("a b", rewrite("a ;b", false));
}

static void test_fuehrendes_semikolon_liest_nicht_vor_den_puffer(void)
{
    // Frueher las uformat[in-1] bei in == 0 vor den Puffer. Unter dem Sanitizer
    // faellt das auf; ohne ihn bleibt der Fall wenigstens als Verhalten fixiert.
    TEST_ASSERT_EQUAL_STRING(" b", rewrite(";b", false));
}

// ---------------------------------------------------------------------------
// Randfaelle
// ---------------------------------------------------------------------------

static void test_leerer_formatstring(void)
{
    TEST_ASSERT_EQUAL_STRING("", rewrite(""));
}

static void test_nullzeiger_liefert_leeren_string(void)
{
    memset(out, 0x7F, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, printfdebRewriteFormat(NULL, out, sizeof(out), false));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_ueberlauf_wird_abgeschnitten_und_terminiert(void)
{
    char small[8];
    memset(small, 0x7F, sizeof(small));
    size_t n = printfdebRewriteFormat("ABCDEFGHIJKLMNOP", small, sizeof(small), false);

    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_EQUAL_STRING("ABCDEFG", small);
}

static void test_ueberlauf_zerreisst_kein_prozent_paar(void)
{
    // Passt das Paar nicht mehr, wird es ganz verworfen -- ein einzelnes '%'
    // am Ende waere fuer vsnprintf ein unvollstaendiger Konversionsbefehl.
    char small[4];
    memset(small, 0x7F, sizeof(small));
    printfdebRewriteFormat("AB%%", small, sizeof(small), false);

    TEST_ASSERT_EQUAL_STRING("AB", small);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_prozent_escape_bleibt_ein_paar);
    RUN_TEST(test_prozent_escape_am_stringende);
    RUN_TEST(test_mehrere_prozent_escapes);
    RUN_TEST(test_einzelnes_prozent_bleibt_unveraendert);
    RUN_TEST(test_prozent_am_stringende_ohne_partner);

    RUN_TEST(test_semikolon_wird_im_csv_modus_behalten);
    RUN_TEST(test_semikolon_wird_ohne_csv_zu_leerzeichen);
    RUN_TEST(test_semikolon_neben_leerzeichen_faellt_weg);
    RUN_TEST(test_fuehrendes_semikolon_liest_nicht_vor_den_puffer);

    RUN_TEST(test_leerer_formatstring);
    RUN_TEST(test_nullzeiger_liefert_leeren_string);
    RUN_TEST(test_ueberlauf_wird_abgeschnitten_und_terminiert);
    RUN_TEST(test_ueberlauf_zerreisst_kein_prozent_paar);

    return UNITY_END();
}
