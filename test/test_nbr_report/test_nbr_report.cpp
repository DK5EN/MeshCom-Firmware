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

static char g_log[4096];
static void log_capture(const char *line)
{
    size_t used = strlen(g_log);
    if (used + strlen(line) + 2 < sizeof(g_log))
    {
        strcat(g_log, line);
        strcat(g_log, "\n");
    }
}

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
    TEST_ASSERT_FALSE(nbrRowHasFlag(r, sx, NBR_FLAG_RPT));   // gekappt: kein Veto
}

// Kantenpool voll (Welle 2, Konzept 4.1): es weicht die aelteste Kante, die
// weder Zeile 0 noch Spalte 0 beruehrt -- auch wenn eine Kante an mir selbst
// noch aelter ist. Eigene Umgebung, weil native_nbr_matrix (5 Zeilen, 20
// Kanten) jede moegliche Kante halten kann und nie voll wird.
void test_full_edge_pool_evicts_oldest_edge_not_touching_row_zero(void)
{
    static NbrMatrix p;
    nbrInit(p, "DK5EN-98", 0);
    char call[NBR_CALL_LEN];
    int idx[NBR_MAX_ROWS];
    for (int i = 1; i < NBR_MAX_ROWS; i++)
    {
        snprintf(call, sizeof(call), "DA1A%c-1", 'A' + i);
        nbrNoteFrame(p, call, ':', NULL, false, -80, 5, 1);   // Kante (i, 0) mit Minute 1
        idx[i] = nbrFind(p, call);
        TEST_ASSERT_TRUE(idx[i] > 0);
    }
    // Den Rest des Pools mit Kanten zwischen Fremden fuellen, Minute 10, 11, ...
    int used = nbrEdgesUsed(p);
    uint16_t t = 10;
    int first_x = -1, first_y = -1;
    for (int x = 1; x < NBR_MAX_ROWS && used < NBR_MAX_EDGES; x++)
        for (int y = 1; y < NBR_MAX_ROWS && used < NBR_MAX_EDGES; y++)
        {
            if (x == y)
                continue;
            int e = nbrIEdgeAlloc(p, idx[x], idx[y], t, NULL);
            p.edge[e].cnt = 1;
            if (first_x < 0)
            {
                first_x = idx[x];
                first_y = idx[y];
            }
            t++;
            used++;
        }
    TEST_ASSERT_EQUAL_INT(NBR_MAX_EDGES, nbrEdgesUsed(p));

    // Eine noch fehlende Paarkante (x, y) zwischen zwei Fremden erzwingt die
    // Verdraengung (die ME-Kante (y, 0) des Rahmens gibt es schon).
    int nx = -1, ny = -1;
    for (int x = 1; x < NBR_MAX_ROWS && nx < 0; x++)
        for (int y = 1; y < NBR_MAX_ROWS && nx < 0; y++)
            if (x != y && nbrIEdgeFind(p, idx[x], idx[y]) < 0)
            {
                nx = x;
                ny = y;
            }
    TEST_ASSERT_TRUE(nx > 0);
    char path[32], cx[NBR_CALL_LEN], cy[NBR_CALL_LEN], cfx[NBR_CALL_LEN], cfy[NBR_CALL_LEN];
    nbrCallDecode(p.call[idx[nx]], cx);
    nbrCallDecode(p.call[idx[ny]], cy);
    nbrCallDecode(p.call[first_x], cfx);
    nbrCallDecode(p.call[first_y], cfy);
    snprintf(path, sizeof(path), "%s,%s", cx, cy);

    g_log[0] = '\0';
    nbrLog = log_capture;
    nbrNoteFrame(p, path, '!', NULL, false, -80, 5, 200);
    nbrLog = NULL;

    // Die aelteste Kante ueberhaupt ist eine Kante an mir (Minute 1); weichen
    // muss aber die aelteste Fremdkante (Minute 10).
    char expect[64];
    snprintf(expect, sizeof(expect), "[NBR]|EVICT-E|200|%s|%s\n", cfx, cfy);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_log, expect), g_log);
    TEST_ASSERT_TRUE(nbrIEdgeFind(p, first_x, first_y) < 0);
    TEST_ASSERT_TRUE(nbrIEdgeFind(p, idx[nx], idx[ny]) >= 0);
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        TEST_ASSERT_TRUE(nbrIEdgeFind(p, idx[i], 0) >= 0);   // alle Kanten an mir leben noch
    TEST_ASSERT_FALSE(nbrMaskTest(p.heardBy[first_x], first_y));
    TEST_ASSERT_FALSE(nbrMaskTest(p.hears[first_y], first_x));
    TEST_ASSERT_EQUAL_INT(NBR_MAX_EDGES, nbrEdgesUsed(p));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_more_than_max_entries_truncates_with_plus);
    RUN_TEST(test_exactly_max_entries_has_no_plus);
    RUN_TEST(test_truncated_report_round_trip_clears_complete_flag);
    RUN_TEST(test_full_edge_pool_evicts_oldest_edge_not_touching_row_zero);
    return UNITY_END();
}
