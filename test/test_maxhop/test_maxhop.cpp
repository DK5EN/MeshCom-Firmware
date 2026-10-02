// Native Testsuite fuer die Hop-Limit-Helfer aus src/maxhop.h (CS-01 / CS-02).
//
// Hintergrund: max_hop_text war bis 2026-08-30 faktisch eine Compile-Zeit-
// Konstante -- kein NVS-Key, und beide Plattformen haben das Feld bei jedem Boot
// mit dem Default ueberschrieben. Jetzt ist der Wert persistent, ueber
// "--maxhop 1..6" setzbar und im Web-Config als Drop-down (4/3/2 plus den
// aktuellen Wert, wenn er ausserhalb liegt) sichtbar. Serielle Pruefung und
// Drop-down muessen aus derselben Quelle kommen, sonst driften GUI und Konsole.
//
//   pio test -e native -f test_maxhop

#include <unity.h>

#include <maxhop.h>

void setUp(void)    {}
void tearDown(void) {}

static void test_gueltiger_bereich_ist_1_bis_6(void)
{
    TEST_ASSERT_FALSE(maxHopTextValid(0));
    TEST_ASSERT_TRUE(maxHopTextValid(1));
    TEST_ASSERT_TRUE(maxHopTextValid(2));
    TEST_ASSERT_TRUE(maxHopTextValid(3));
    TEST_ASSERT_TRUE(maxHopTextValid(4));
    TEST_ASSERT_TRUE(maxHopTextValid(5));
    TEST_ASSERT_TRUE(maxHopTextValid(6));
    // 7 ist MAX_HOP_LIMIT (die on-air Obergrenze), als Einstellung nicht erlaubt
    TEST_ASSERT_FALSE(maxHopTextValid(7));
    TEST_ASSERT_FALSE(maxHopTextValid(-1));
    TEST_ASSERT_FALSE(maxHopTextValid(99));
}

static void test_sanitize_haelt_gueltige_werte_fest(void)
{
    for (int v = MAXHOP_TEXT_MIN; v <= MAXHOP_TEXT_MAX; v++)
        TEST_ASSERT_EQUAL_INT(v, maxHopTextSanitize(v));
}

static void test_sanitize_faengt_alte_und_kaputte_werte(void)
{
    // 0 = "noch nichts gespeichert" (alte Settings-Datei, geloeschter NVS-Key)
    TEST_ASSERT_EQUAL_INT(MAXHOP_TEXT_FALLBACK, maxHopTextSanitize(0));
    TEST_ASSERT_EQUAL_INT(MAXHOP_TEXT_FALLBACK, maxHopTextSanitize(7));
    TEST_ASSERT_EQUAL_INT(MAXHOP_TEXT_FALLBACK, maxHopTextSanitize(-5));
    TEST_ASSERT_EQUAL_INT(MAXHOP_TEXT_FALLBACK, maxHopTextSanitize(1000000));
    TEST_ASSERT_EQUAL_INT(4, MAXHOP_TEXT_FALLBACK);   // == MAX_HOP_TEXT_DEFAULT
}

static void test_optionsliste_default_ist_4_3_2(void)
{
    int out[MAXHOP_OPTION_MAX];

    for (int cur = 2; cur <= 4; cur++)
    {
        int n = maxHopOptionList(cur, out, MAXHOP_OPTION_MAX);
        TEST_ASSERT_EQUAL_INT(3, n);
        TEST_ASSERT_EQUAL_INT(4, out[0]);
        TEST_ASSERT_EQUAL_INT(3, out[1]);
        TEST_ASSERT_EQUAL_INT(2, out[2]);
    }
}

static void test_optionsliste_nimmt_aktuellen_wert_auf(void)
{
    int out[MAXHOP_OPTION_MAX];

    // 5 und 6 kann nur die serielle Konsole setzen -- die Seite muss sie zeigen
    int n = maxHopOptionList(5, out, MAXHOP_OPTION_MAX);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(5, out[0]);
    TEST_ASSERT_EQUAL_INT(4, out[1]);
    TEST_ASSERT_EQUAL_INT(3, out[2]);
    TEST_ASSERT_EQUAL_INT(2, out[3]);

    n = maxHopOptionList(6, out, MAXHOP_OPTION_MAX);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(6, out[0]);
    TEST_ASSERT_EQUAL_INT(4, out[1]);

    // 1 ist kleiner als jeder angebotene Wert -> haengt hinten an
    n = maxHopOptionList(1, out, MAXHOP_OPTION_MAX);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(4, out[0]);
    TEST_ASSERT_EQUAL_INT(3, out[1]);
    TEST_ASSERT_EQUAL_INT(2, out[2]);
    TEST_ASSERT_EQUAL_INT(1, out[3]);
}

static void test_optionsliste_absteigend_und_ohne_duplikat(void)
{
    int out[MAXHOP_OPTION_MAX];

    for (int cur = -3; cur <= 12; cur++)
    {
        int n = maxHopOptionList(cur, out, MAXHOP_OPTION_MAX);
        TEST_ASSERT_TRUE(n >= 3 && n <= MAXHOP_OPTION_MAX);

        for (int i = 1; i < n; i++)
            TEST_ASSERT_TRUE_MESSAGE(out[i - 1] > out[i], "options must be strictly descending");

        // der (bereinigte) aktuelle Wert steht immer drin, sonst koennte die
        // Seite ihn nicht als "selected" markieren
        int want = maxHopTextSanitize(cur);
        bool found = false;
        for (int i = 0; i < n; i++)
            if (out[i] == want)
                found = true;
        TEST_ASSERT_TRUE_MESSAGE(found, "current value missing from the option list");

        // 4, 3 und 2 sind immer dabei
        for (int fixed = 2; fixed <= 4; fixed++)
        {
            bool has = false;
            for (int i = 0; i < n; i++)
                if (out[i] == fixed)
                    has = true;
            TEST_ASSERT_TRUE_MESSAGE(has, "a default option went missing");
        }
    }
}

static void test_optionsliste_schutz_gegen_kleine_puffer(void)
{
    int out[MAXHOP_OPTION_MAX];

    TEST_ASSERT_EQUAL_INT(0, maxHopOptionList(4, nullptr, MAXHOP_OPTION_MAX));
    TEST_ASSERT_EQUAL_INT(0, maxHopOptionList(4, out, 0));
    TEST_ASSERT_EQUAL_INT(0, maxHopOptionList(4, out, -1));

    // schreibt nie ueber cap hinaus
    for (int cap = 1; cap <= MAXHOP_OPTION_MAX; cap++)
    {
        int guard[MAXHOP_OPTION_MAX + 1];
        for (int i = 0; i < MAXHOP_OPTION_MAX + 1; i++)
            guard[i] = -777;
        int n = maxHopOptionList(6, guard, cap);
        TEST_ASSERT_EQUAL_INT(cap, n);
        TEST_ASSERT_EQUAL_INT(-777, guard[cap]);
    }

    // Anfang der gekuerzten Liste bleibt der Anfang der vollen Liste
    int two[2];
    TEST_ASSERT_EQUAL_INT(2, maxHopOptionList(5, two, 2));
    TEST_ASSERT_EQUAL_INT(5, two[0]);
    TEST_ASSERT_EQUAL_INT(4, two[1]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_gueltiger_bereich_ist_1_bis_6);
    RUN_TEST(test_sanitize_haelt_gueltige_werte_fest);
    RUN_TEST(test_sanitize_faengt_alte_und_kaputte_werte);
    RUN_TEST(test_optionsliste_default_ist_4_3_2);
    RUN_TEST(test_optionsliste_nimmt_aktuellen_wert_auf);
    RUN_TEST(test_optionsliste_absteigend_und_ohne_duplikat);
    RUN_TEST(test_optionsliste_schutz_gegen_kleine_puffer);
    return UNITY_END();
}
