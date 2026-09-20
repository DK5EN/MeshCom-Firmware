// Native Testsuite fuer coordMatches() aus src/coord_compare.h (PR #1150).
//
// Hintergrund: node_lat/node_lon sind double, der nRF52-Core hat kein
// String::toDouble(). Der vor PR #1150 gemergte #else-Zweig verglich deshalb
// gegen paramValue.toFloat() -- ein float kann 49.997 nicht exakt darstellen
// (er wird zu 49.9970016f), also scheiterte der Vergleich fuer praktisch
// jede reale Koordinate und die Web-GUI zeigte "Value could not be set.",
// obwohl der Knoten den Wert laengst gespeichert hatte. Diese Suite pinnt die
// gemergte Regel (double statt float, mit fabs() und 1e-7 Toleranz), damit
// ein zukuenftiger core-spezifischer #ifdef den Fehler nicht wieder einfuehrt.
//
//   pio test -e native -f test_web_setcoord

#include <unity.h>

#include <coord_compare.h>

void setUp(void)    {}
void tearDown(void) {}

// Der eigentliche Regressionsfall: 49.997 wurde vor dem Fix als
// 49.9970016f (float) mit dem gespeicherten double 49.997 verglichen und
// scheiterte. Mit double muss das matchen.
static void test_49_997_matcht_sich_selbst(void)
{
    TEST_ASSERT_TRUE(coordMatches(49.997, "49.997"));
}

// Werte, die ein float zufaellig exakt darstellen kann -- die liefen schon
// vor dem Fix durch. Duerfen nicht die einzige Abdeckung sein.
static void test_float_darstellbare_werte_matchen(void)
{
    TEST_ASSERT_TRUE(coordMatches(48.5, "48.5"));
    TEST_ASSERT_TRUE(coordMatches(49.25, "49.25"));
    TEST_ASSERT_TRUE(coordMatches(50.0, "50.0"));
}

// --setlat/--setlon legen negative Eingaben als Betrag ab, die Himmelsrichtung
// steckt in node_lat_c/node_lon_c -- coordMatches() muss das per fabs() ignorieren.
static void test_negative_eingabe_matcht_gespeicherten_betrag(void)
{
    TEST_ASSERT_TRUE(coordMatches(49.997, "-49.997"));
}

// Eine wirklich andere Koordinate darf nicht matchen.
static void test_unterschiedliche_koordinate_matcht_nicht(void)
{
    TEST_ASSERT_FALSE(coordMatches(49.997, "49.998"));
}

// Toleranzgrenze 1e-7 von beiden Seiten pinnen.
static void test_toleranzgrenze_knapp_innerhalb_matcht(void)
{
    TEST_ASSERT_TRUE(coordMatches(49.997, "49.9970000999"));
}

static void test_toleranzgrenze_knapp_ausserhalb_matcht_nicht(void)
{
    TEST_ASSERT_FALSE(coordMatches(49.997, "49.9970002"));
}

// Das Eingabefeld liefert bis zu acht Nachkommastellen.
static void test_acht_nachkommastellen_matchen(void)
{
    TEST_ASSERT_TRUE(coordMatches(49.997, "49.99700000"));
}

// Laengengrad funktioniert identisch.
static void test_laengengrad_matcht(void)
{
    TEST_ASSERT_TRUE(coordMatches(8.59221, "8.59221"));
}

// Eigenschaft, die diese Suite absichert: wuerde coordMatches() 'typed' ueber
// einen float statt double fuehren, muesste dieser Fall scheitern (siehe
// Docstring oben). float(49.997) == 49.9970016..., dessen Abstand zum
// double-Original liegt weit ueber 1e-7.
static void test_pinnt_double_nicht_float(void)
{
    TEST_ASSERT_TRUE(coordMatches(49.997, "49.997"));
    TEST_ASSERT_TRUE(fabs((double)(float)49.997 - 49.997) > 1e-7);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_49_997_matcht_sich_selbst);
    RUN_TEST(test_float_darstellbare_werte_matchen);
    RUN_TEST(test_negative_eingabe_matcht_gespeicherten_betrag);
    RUN_TEST(test_unterschiedliche_koordinate_matcht_nicht);
    RUN_TEST(test_toleranzgrenze_knapp_innerhalb_matcht);
    RUN_TEST(test_toleranzgrenze_knapp_ausserhalb_matcht_nicht);
    RUN_TEST(test_acht_nachkommastellen_matchen);
    RUN_TEST(test_laengengrad_matcht);
    RUN_TEST(test_pinnt_double_nicht_float);
    return UNITY_END();
}
