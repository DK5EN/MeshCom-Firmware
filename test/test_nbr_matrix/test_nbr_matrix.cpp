// Nachbarschaftsmatrix, Wave 1. Die Regeln (Pfadpaare, Schleifenerkennung,
// HEY-Berichtsgruppen, 12-h-Verfall mit Minuten-Ueberlauf, Verdraengung,
// die Urteile aus Konzept 4.3 und die Reichweite aus 4.4) hatten vor diesem
// Modul keinen ausfuehrbaren Test, weil sie nur zusammen mit OnRxDone in
// lora_functions.cpp existierten. env native_nbr_matrix setzt NBR_MAX_ROWS=5,
// klein genug, um Verdraengung ohne 21 Zeilen Fuellarbeit zu pruefen.
//
// test_build_src=no fuer diese Umgebung: die Quelle kommt per #include, nicht
// als eigenes Kompilat (siehe test/test_mheard_record fuer dasselbe Muster,
// dort ueber ein reines Header).

#include <unity.h>
#include <string.h>
#include <math.h>

#include "../../src/nbr_matrix.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- 1: Pfadpaar-Regel -----------------------------------------------------

void test_pair_rule_text_frame(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 100);
    TEST_ASSERT_EQUAL_INT(2, hits);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_TRUE(ibbb > 0);

    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[ibbb][0].cnt_text);
    TEST_ASSERT_EQUAL_INT8(-80, m.cells[ibbb][0].rssi);

    // Keine andere Zelle darf einen Zaehler > 0 tragen.
    int nonzero = 0;
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
            if (m.cells[x][y].cnt_text || m.cells[x][y].cnt_pos || m.cells[x][y].cnt_hey)
                nonzero++;
    TEST_ASSERT_EQUAL_INT(2, nonzero);
}

// --- 2: eigenes Echo --------------------------------------------------------

void test_own_relay_seen_directly_gives_both_cells(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 200);

    // Mein eigener Frame, einmal ueber AAA relayt, erreicht mich direkt: ich
    // hoere AAAs Kopie, UND das ist zugleich der Beweis, dass AAA mich hoert.
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", ':', NULL, false, -70, 200);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[0][iaaa].cnt_text);   // AAA hat mich gehoert
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][0].cnt_text);   // ich habe AAAs Kopie gehoert
}

void test_own_frame_returning_via_relay_is_echo_not_direct_hearing(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 200);

    // Derselbe Frame kommt ueber AAA zurueck, mit meinem eigenen Rufzeichen
    // als letztem Hop: das ist mein Echo, kein Hoerbeweis fuer "ich habe den
    // letzten Hop gehoert" (Konzept 4.1).
    nbrNoteFrame(m, "OE1AAA-1,DK5EN-93", ':', NULL, false, -70, 200);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][0].cnt_text);   // ich habe AAA gehoert
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[0][iaaa].cnt_text);   // kein Zusatztreffer aus dem Echo
}

// --- 3: HEY-Berichtsgruppen --------------------------------------------------

void test_hey_signal_report_groups_set_rssi_per_pair(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,OE1BBB-2", '@',
                             "R5;3,97,7;4,101,3;", true, -60, 300);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_INT8(-97, m.cells[0][iaaa].rssi);      // AAA hoert mich mit -97
    TEST_ASSERT_EQUAL_INT8(-101, m.cells[iaaa][ibbb].rssi);  // BBB hoert AAA mit -101
    TEST_ASSERT_EQUAL_INT8(-60, m.cells[ibbb][0].rssi);      // ich hoere BBB mit rssi_here
    TEST_ASSERT_TRUE(m.rows[0].flags & NBR_FLAG_GW);
}

void test_hey_old_format_counts_hits_without_rssi(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,OE1BBB-2", '@', "R5;", true, -60, 300);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_INT8(0, m.cells[0][iaaa].rssi);        // kein Bericht -> unbekannt
    TEST_ASSERT_EQUAL_INT8(0, m.cells[iaaa][ibbb].rssi);
    TEST_ASSERT_EQUAL_INT8(-60, m.cells[ibbb][0].rssi);      // der letzte Hop bekommt rssi_here trotzdem
}

// --- 4: Ablehnungen lassen die Matrix unangetastet --------------------------

void test_rejects_leave_matrix_byte_identical(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 50);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 50);
    memcpy(&before, &m, sizeof(m));

    // ungueltiges Rufzeichen (2 Zeichen, unter der Mindestlaenge 3)
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "AB,OE1BBB-2", ':', NULL, false, -80, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // Schleife: dasselbe Rufzeichen zweimal im Pfad
    TEST_ASSERT_EQUAL_INT(-2, nbrNoteFrame(m, "OE1AAA-1,OE1AAA-1", ':', NULL, false, -80, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // unbekannter Frame-Typ
    TEST_ASSERT_EQUAL_INT(0, nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", 'X', NULL, false, -80, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

// --- 5: Alterung und Ueberlauf -----------------------------------------------

void test_aging_720_min_window_and_stale_reset(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 100);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");

    TEST_ASSERT_TRUE(nbrFresh(m.cells[iaaa][ibbb].last_min, 819));   // 719 min alt
    TEST_ASSERT_FALSE(nbrFresh(m.cells[iaaa][ibbb].last_min, 820));  // genau 720: nicht mehr frisch

    // Treffer auf eine inzwischen verfallene Zelle faengt bei 1 an, nicht bei 2.
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 900);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text);

    // Ueberlauf des 16-Bit-Minutenzaehlers: 65500 -> 10 sind 46 min, klar frisch.
    TEST_ASSERT_TRUE(nbrFresh(65500, 10));
}

// --- 6: Verdraengung (NBR_MAX_ROWS=5 in dieser Umgebung) --------------------

void test_eviction_replaces_oldest_row_never_row_zero(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, false, 1, 10);
    nbrNotePos(m, "OE1BBB-2", 48.1f, 11.1f, false, 1, 20);
    nbrNotePos(m, "OE1CCC-3", 48.2f, 11.2f, false, 1, 30);
    nbrNotePos(m, "OE1DDD-4", 48.3f, 11.3f, false, 1, 40);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0);

    // AAA traegt eine Beobachtung als ZEILE und als SPALTE -- die
    // Verdraengung muss beide Richtungen nullen, nicht nur eine.
    m.cells[iaaa][ibbb].cnt_text = 1; m.cells[iaaa][ibbb].last_min = 10;
    m.cells[ibbb][iaaa].cnt_text = 1; m.cells[ibbb][iaaa].last_min = 10;

    // Tabelle ist voll (Zeile 0 + 4 Fremde = NBR_MAX_ROWS). AAA hat mit
    // last_min=10 die groesste Altersluecke zu now=50 und weicht.
    nbrNotePos(m, "OE1EEE-5", 48.4f, 11.4f, false, 1, 50);

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    int ieee = nbrFind(m, "OE1EEE-5");
    TEST_ASSERT_TRUE(ieee >= 0);
    TEST_ASSERT_EQUAL_INT(iaaa, ieee);   // der Neuling landet exakt auf dem verdraengten Index
    TEST_ASSERT_TRUE(nbrFind(m, "OE1BBB-2") >= 0);
    TEST_ASSERT_TRUE(nbrFind(m, "OE1CCC-3") >= 0);
    TEST_ASSERT_TRUE(nbrFind(m, "OE1DDD-4") >= 0);
    TEST_ASSERT_EQUAL_INT(0, nbrFind(m, "DK5EN-93"));  // Zeile 0 bleibt unangetastet

    // Zeile UND Spalte des verdraengten Index sind genullt (derselbe Index
    // traegt jetzt EEE, dessen frische Zelle zu BBB noch nie gesetzt wurde --
    // stuende hier noch AAAs alter Zaehler, waere das ein Verdraengungsfehler).
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[iaaa][ibbb].cnt_text);
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[ibbb][iaaa].cnt_text);
}

// --- M1: zwei neue Rufzeichen im selben Frame duerfen nicht um dieselbe ----
// --- Opferzeile kollidieren -------------------------------------------------

void test_two_new_calls_in_one_frame_get_distinct_rows_not_the_diagonal(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "OE1AAA-1", 0.0f, 0.0f, false, 0, 100);
    nbrNotePos(m, "OE1BBB-2", 0.0f, 0.0f, false, 0, 100);
    nbrNotePos(m, "OE1CCC-3", 0.0f, 0.0f, false, 0, 100);
    nbrNotePos(m, "OE1DDD-4", 0.0f, 0.0f, false, 0, 100);
    // Tabelle ist jetzt voll (Zeile 0 + 4 Fremde), alle vier mit derselben
    // Letztzeit. Ohne den Kollisionsschutz wuerden EEE UND FFF beide "die
    // aelteste Zeile" (Index 1) planen; der zweite Treffer faellt dann auf
    // die Diagonale statt auf ein eigenes Paar.
    int hits = nbrNoteFrame(m, "OE1EEE-5,OE1FFF-6", ':', NULL, false, -80, 100);
    TEST_ASSERT_TRUE(hits > 0);

    int ieee = nbrFind(m, "OE1EEE-5");
    int ifff = nbrFind(m, "OE1FFF-6");
    TEST_ASSERT_TRUE(ieee >= 0);
    TEST_ASSERT_TRUE(ifff >= 0);
    TEST_ASSERT_TRUE(ieee != ifff);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[ieee][ifff].cnt_text);
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[ieee][ieee].cnt_text);  // keine Diagonale
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[ifff][ifff].cnt_text);
}

void test_frame_needing_more_new_rows_than_capacity_is_rejected_atomically(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    memcpy(&before, &m, sizeof(m));

    // 5 fremde, bislang unbekannte Rufzeichen, aber nur 4 Fremdzeilen frei
    // (NBR_MAX_ROWS=5 in dieser Umgebung) -- die Planung scheitert am
    // fuenften Token, BEVOR irgendeine Zeile committet wurde.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5",
                             ':', NULL, false, -80, 100);
    TEST_ASSERT_EQUAL_INT(-3, hits);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

// --- M2: 16-Bit-Ueberlauf darf keine tote Zelle wiederbeleben ---------------

void test_sweep_clears_ghost_cell_before_it_can_wrap_fresh_again(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 100);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text);

    // Die absolute Minutenzahl seit Boot laeuft im Test ungekappt weiter;
    // nur der an nbrNoteFrame() uebergebene now_min-Wert wird -- wie auf dem
    // echten Geraet -- auf uint16_t gekappt. In ~1000-min-Schritten auf ein
    // unbeteiligtes Rufzeichenpaar, damit nbrMaybeSweep() oft genug zum Zug
    // kommt (Schwelle 1024 min), bis die absolute Zeit 65636 erreicht --
    // 65636 mod 65536 ist wieder 100, also genau der Umlaufpunkt, an dem der
    // Geist ohne Sweep mit Alter 0 wiederauferstehen wuerde.
    uint32_t t = 100;
    while (t < 65636)
    {
        t += (t + 1000 <= 65636) ? 1000 : (65636 - t);
        nbrNoteFrame(m, "OE1ZZZ-9,OE1YYY-8", ':', NULL, false, -80, (uint16_t)t);
    }
    TEST_ASSERT_EQUAL_UINT32(65636u, t);

    uint16_t now = (uint16_t)t;
    TEST_ASSERT_EQUAL_UINT16(100, now);  // der Umlauf: 65636 mod 65536 ist wieder 100

    // Ohne den Sweep waere nbrFresh(100, 100) wahr (Alter 0) und die alten
    // Zaehler stuenden noch da -- der Geist saehe aus wie "gerade eben
    // getroffen". Der Sweep muss das laengst aufgeraeumt haben.
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[iaaa][ibbb].cnt_text);
    TEST_ASSERT_EQUAL_INT8(0, m.cells[iaaa][ibbb].rssi);
}

// --- L4: weitere Randfaelle ---------------------------------------------------

void test_more_than_eight_hops_is_rejected_whole(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    memcpy(&before, &m, sizeof(m));

    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m,
        "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5,OE1FFF-6,OE1GGG-7,OE1HHH-8,OE1III-9",
        ':', NULL, false, -80, 100));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

void test_reset_keeps_only_row_zero_call(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 100);
    m.rows[0].flags |= NBR_FLAG_GW;

    nbrReset(m, 500);

    TEST_ASSERT_EQUAL_STRING("DK5EN-93", m.rows[0].call);
    TEST_ASSERT_EQUAL_UINT8(0, m.rows[0].flags);
    TEST_ASSERT_EQUAL_UINT16(0, m.rows[0].last_min);
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        TEST_ASSERT_EQUAL_UINT8(0, m.rows[i].flags);
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
        {
            TEST_ASSERT_EQUAL_UINT8(0, m.cells[x][y].cnt_text);
            TEST_ASSERT_EQUAL_UINT8(0, m.cells[x][y].cnt_pos);
            TEST_ASSERT_EQUAL_UINT8(0, m.cells[x][y].cnt_hey);
            TEST_ASSERT_EQUAL_INT8(0, m.cells[x][y].rssi);
        }
}

void test_note_pos_mesh_false_clears_previously_set_mesh_flag(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, true, 1, 10);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(m.rows[iaaa].flags & NBR_FLAG_MESH);

    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, false, 1, 20);
    TEST_ASSERT_FALSE(m.rows[iaaa].flags & NBR_FLAG_MESH);
}

void test_hearers_returns_total_count_beyond_what_was_written(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "OE1AAA-1", 0.0f, 0.0f, false, 0, 5);
    nbrNotePos(m, "OE1BBB-2", 0.0f, 0.0f, false, 0, 5);
    nbrNotePos(m, "OE1CCC-3", 0.0f, 0.0f, false, 0, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");

    // Hoerer(AAA) = {BBB, CCC, 0}: drei Hoerer.
    m.cells[iaaa][ibbb].cnt_text = 1; m.cells[iaaa][ibbb].last_min = 5;
    m.cells[iaaa][iccc].cnt_text = 1; m.cells[iaaa][iccc].last_min = 5;
    m.cells[iaaa][0].cnt_text = 1;    m.cells[iaaa][0].last_min = 5;

    uint8_t out[2];
    uint8_t n = nbrHearers(m, iaaa, 5, out, 2);
    TEST_ASSERT_EQUAL_UINT8(3, n);   // Gesamtzahl, nicht auf max=2 gekappt
}

// --- 7: exklusiv / gedeckt, Konzept-4.3-Beispiel ----------------------------

void test_exclusive_rows_per_concept_43_example(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "OE1AAA-1", 0.0f, 0.0f, false, 0, 5);
    nbrNotePos(m, "OE1BBB-2", 0.0f, 0.0f, false, 0, 5);
    nbrNotePos(m, "OE1CCC-3", 0.0f, 0.0f, false, 0, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");

    const uint16_t now = 10;
    // AAA wird von 93 und BBB gehoert.
    m.cells[iaaa][0].cnt_text = 40;    m.cells[iaaa][0].last_min = now;
    m.cells[iaaa][ibbb].cnt_text = 9;  m.cells[iaaa][ibbb].last_min = now;
    // BBB wird von 93 und AAA gehoert.
    m.cells[ibbb][0].cnt_text = 7;     m.cells[ibbb][0].last_min = now;
    m.cells[ibbb][iaaa].cnt_text = 9;  m.cells[ibbb][iaaa].last_min = now;
    // CCC wird nur von 93 gehoert.
    m.cells[iccc][0].cnt_text = 5;     m.cells[iccc][0].last_min = now;

    uint8_t out[8];
    int n = nbrExclusive(m, now, out, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)iccc, out[0]);

    // BBB wiederholt jetzt auch CCC -> CCC ist nicht mehr exklusiv.
    m.cells[iccc][ibbb].cnt_text = 3; m.cells[iccc][ibbb].last_min = now;
    n = nbrExclusive(m, now, out, 8);
    TEST_ASSERT_EQUAL_INT(0, n);

    // Leere Matrix: nichts direkt gehoert -> -1, nicht 0.
    NbrMatrix empty;
    nbrInit(empty, "DK5EN-93", 0);
    n = nbrExclusive(empty, 10, out, 8);
    TEST_ASSERT_EQUAL_INT(-1, n);
}

// --- 8: Reichweite -----------------------------------------------------------

void test_reach_haversine_distance_and_partner(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "DK5EN-93", 48.40f, 11.75f, false, 0, 5);   // Freising
    nbrNotePos(m, "OE1AAA-1", 48.14f, 11.58f, false, 0, 5);   // Muenchen
    int iaaa = nbrFind(m, "OE1AAA-1");
    m.cells[iaaa][0].cnt_text = 1;
    m.cells[iaaa][0].last_min = 5;   // ich habe AAA gehoert -> Verbindung fuer die Reichweite

    int partner = -1;
    float d = nbrReach(m, 0, 10, &partner);
    TEST_ASSERT_TRUE(fabsf(d - 31.0f) < 1.0f);
    TEST_ASSERT_EQUAL_INT(iaaa, partner);

    // Keine Position gesetzt -> keine Reichweite.
    NbrMatrix m2;
    nbrInit(m2, "DK5EN-93", 0);
    int partner2 = 0;
    float d2 = nbrReach(m2, 0, 10, &partner2);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, d2);
    TEST_ASSERT_EQUAL_INT(-1, partner2);
}

// --- 9: Zeilenformatierer -----------------------------------------------------

void test_format_row_contains_callsign_and_hearers(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 10);
    int iaaa = nbrFind(m, "OE1AAA-1");

    char buf[128];
    int n = nbrFormatRow(m, iaaa, 10, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "OE1AAA-1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "OE1BBB-2"));  // BBB hat AAA gehoert -> steht in hearers
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_pair_rule_text_frame);
    RUN_TEST(test_own_relay_seen_directly_gives_both_cells);
    RUN_TEST(test_own_frame_returning_via_relay_is_echo_not_direct_hearing);
    RUN_TEST(test_hey_signal_report_groups_set_rssi_per_pair);
    RUN_TEST(test_hey_old_format_counts_hits_without_rssi);
    RUN_TEST(test_rejects_leave_matrix_byte_identical);
    RUN_TEST(test_aging_720_min_window_and_stale_reset);
    RUN_TEST(test_eviction_replaces_oldest_row_never_row_zero);
    RUN_TEST(test_two_new_calls_in_one_frame_get_distinct_rows_not_the_diagonal);
    RUN_TEST(test_frame_needing_more_new_rows_than_capacity_is_rejected_atomically);
    RUN_TEST(test_sweep_clears_ghost_cell_before_it_can_wrap_fresh_again);
    RUN_TEST(test_more_than_eight_hops_is_rejected_whole);
    RUN_TEST(test_reset_keeps_only_row_zero_call);
    RUN_TEST(test_note_pos_mesh_false_clears_previously_set_mesh_flag);
    RUN_TEST(test_hearers_returns_total_count_beyond_what_was_written);
    RUN_TEST(test_exclusive_rows_per_concept_43_example);
    RUN_TEST(test_reach_haversine_distance_and_partner);
    RUN_TEST(test_format_row_contains_callsign_and_hearers);
    return UNITY_END();
}
