// Kappung der HN-Nachbarschaftsmeldung (nbrBuildReport(), Abschnitt E in
// src/nbr_matrix.h) mit genug Zeilen fuer mehr als NBR_REPORT_MAX_ENTRIES
// direkte Nachbarn. Eigene Umgebung (native_nbr_report, NBR_MAX_ROWS=12), weil
// native_nbr_matrix mit 5 Zeilen die Kappung nicht erreicht und dessen
// Verdraengungstests genau 5 Zeilen voraussetzen. Ein fehlendes '+' an einer
// gekappten Liste liesse den Empfaenger die fehlenden Stationen als "nicht
// gehoert" lesen -- deshalb hier Ende-zu-Ende.
#include <unity.h>
#include <string.h>
#include "../../src/nbr_matrix.cpp"

static NbrMatrix m;

static void add_direct(const char *call, int snr)
{
    nbrNoteFrame(m, call, ':', NULL, false, -80, (int8_t)snr, 10);
}

void setUp(void) { nbrInit(m, "DK5EN-98", 0); }
void tearDown(void) {}

void test_more_than_max_entries_truncates_with_plus(void)
{
    const char *calls[10] = {"DA1AA-1", "DA1AB-1", "DA1AC-1", "DA1AD-1", "DA1AE-1",
                             "DA1AF-1", "DA1AG-1", "DA1AH-1", "DA1AI-1", "DA1AJ-1"};
    for (int i = 0; i < 10; i++)
        add_direct(calls[i], 9 - i);            // SNR 9 .. 0, alle ueber der Schwelle
    char out[160];
    int n = nbrBuildReport(m, 11, 17, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("R17;N8+;DA1AA-1,9;DA1AB-1,8;DA1AC-1,7;DA1AD-1,6;DA1AE-1,5;"
                             "DA1AF-1,4;DA1AG-1,3;DA1AH-1,2;", out);
    TEST_ASSERT_TRUE(n < 128);                  // dokumentierte Obergrenze
}

void test_exactly_max_entries_has_no_plus(void)
{
    const char *calls[8] = {"DA1AA-1", "DA1AB-1", "DA1AC-1", "DA1AD-1",
                            "DA1AE-1", "DA1AF-1", "DA1AG-1", "DA1AH-1"};
    for (int i = 0; i < 8; i++)
        add_direct(calls[i], -i);
    add_direct("DA1AZ-1", -17);                 // unter der Schwelle: zaehlt nicht als "gekappt"
    char out[160];
    TEST_ASSERT_TRUE(nbrBuildReport(m, 11, 9, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "R9;N8;"));
    TEST_ASSERT_NULL(strchr(out, '+'));
    TEST_ASSERT_NULL(strstr(out, "DA1AZ-1"));
}

void test_truncated_report_round_trip_clears_complete_flag(void)
{
    const char *calls[9] = {"DA1AA-1", "DA1AB-1", "DA1AC-1", "DA1AD-1", "DA1AE-1",
                            "DA1AF-1", "DA1AG-1", "DA1AH-1", "DA1AI-1"};
    for (int i = 0; i < 9; i++)
        add_direct(calls[i], 5);
    char out[160];
    TEST_ASSERT_TRUE(nbrBuildReport(m, 11, 9, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "N8+;"));

    // Empfaenger kennt den Absender DK5EN-98 als direkten Nachbarn.
    NbrMatrix r;
    nbrInit(r, "DK5EN-1", 0);
    nbrNoteFrame(r, "DK5EN-98", '@', NULL, false, -60, 6, 11);
    int applied = nbrNoteReport(r, "DK5EN-98", out, 11);
    TEST_ASSERT_TRUE(applied >= 0);
    int sx = nbrFind(r, "DK5EN-98");
    TEST_ASSERT_TRUE(sx > 0);
    TEST_ASSERT_EQUAL_UINT8(0, r.rows[sx].flags & NBR_FLAG_RPT);   // gekappt: kein Veto
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_more_than_max_entries_truncates_with_plus);
    RUN_TEST(test_exactly_max_entries_has_no_plus);
    RUN_TEST(test_truncated_report_round_trip_clears_complete_flag);
    return UNITY_END();
}
