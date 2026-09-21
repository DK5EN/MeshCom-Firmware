// Nachbarschaftsmatrix, Wave 1 + 2-Hop-Fenster. Die Regeln (Pfadpaare,
// Schleifenerkennung, HEY-Berichtsgruppen, 12-h-Verfall mit Minuten-
// Ueberlauf, Verdraengung, die Urteile aus Konzept 4.3, die Reichweite aus
// 4.4, das 2-Hop-Fenster und die Log-Instrumentierung aus
// docs/nbr-logformat.md) hatten vor diesem Modul keinen ausfuehrbaren Test,
// weil sie nur zusammen mit OnRxDone in lora_functions.cpp existierten.
// env native_nbr_matrix setzt NBR_MAX_ROWS=5, klein genug, um Verdraengung
// ohne 21 Zeilen Fuellarbeit zu pruefen.
//
// Seit dem 2-Hop-Fenster legt nbrNoteFrame() nur noch fuer die letzten zwei
// Pfad-Token eine Zeile an, und nbrNotePos() legt GAR KEINE Zeile mehr an
// (siehe src/nbr_matrix.h). Mehrere hier vorher per nbrNotePos() gefuellte
// Tests wurden deshalb auf eine Zeilen-Neuanlage per nbrNoteFrame() (Ein-
// Token-Pfad, "direkt gehoert") vor dem eigentlichen nbrNotePos()-Aufruf
// umgestellt; die einzelnen Anpassungen stehen jeweils als Kommentar an der
// betroffenen Stelle.
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

    // Zeilen entstehen jetzt nur noch ueber nbrNoteFrame() (2-Hop-Fenster);
    // ein Ein-Token-Pfad ist der minimale Fall "ich habe X direkt gehoert"
    // und legt genau eine Zeile an (siehe nbrNoteFrame()-Kommentar).
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 20);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 30);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 40);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0);

    // AAA traegt eine Beobachtung als ZEILE und als SPALTE -- die
    // Verdraengung muss beide Richtungen nullen, nicht nur eine.
    m.cells[iaaa][ibbb].cnt_text = 1; m.cells[iaaa][ibbb].last_min = 10;
    m.cells[ibbb][iaaa].cnt_text = 1; m.cells[ibbb][iaaa].last_min = 10;

    // Tabelle ist voll (Zeile 0 + 4 Fremde = NBR_MAX_ROWS). AAA hat mit
    // last_min=10 die groesste Altersluecke zu now=50 und weicht.
    nbrNoteFrame(m, "OE1EEE-5", ':', NULL, false, -80, 50);

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
    // Zeilen entstehen nur noch ueber nbrNoteFrame() (2-Hop-Fenster); ein
    // Ein-Token-Pfad legt hier je eine Zeile fuer AAA..DDD an.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 100);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 100);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 100);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 100);
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

// War vor dem 2-Hop-Fenster ein -3 (FULL): 5 neue Rufzeichen brauchten 5 neue
// Zeilen, aber nur 4 Fremdzeilen sind frei (NBR_MAX_ROWS=5 in dieser
// Umgebung) -- das Frame wurde komplett verworfen. Die neue Regel plant nur
// noch die letzten zwei Pfad-Token, braucht also nie mehr als 2 neue Zeilen;
// FULL ist bei NBR_MAX_ROWS >= 3 (jede reale Board-Konfiguration) praktisch
// unerreichbar geworden (siehe nbrNoteFrame()-Kommentar). Dieser Test haelt
// die neue Erwartung fest: der lange Pfad wird akzeptiert, nur die letzten
// zwei Token bekommen eine Zeile, die drei davor bleiben ohne.
void test_long_path_beyond_two_hop_window_is_accepted_not_rejected(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5",
                             ':', NULL, false, -80, 100);
    TEST_ASSERT_TRUE(hits > 0);

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1CCC-3"));
    int iddd = nbrFind(m, "OE1DDD-4");
    int ieee = nbrFind(m, "OE1EEE-5");
    TEST_ASSERT_TRUE(iddd > 0);
    TEST_ASSERT_TRUE(ieee > 0);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iddd][ieee].cnt_text);
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
    // nbrNotePos() legt keine Zeile mehr an -- erst per nbrNoteFrame()
    // erzeugen, dann die Position draufschreiben.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10);
    int iaaa = nbrFind(m, "OE1AAA-1");
    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, true, 1, 10);
    TEST_ASSERT_TRUE(m.rows[iaaa].flags & NBR_FLAG_MESH);

    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, false, 1, 20);
    TEST_ASSERT_FALSE(m.rows[iaaa].flags & NBR_FLAG_MESH);
}

void test_hearers_returns_total_count_beyond_what_was_written(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // nbrNotePos() legt keine Zeile mehr an -- die drei Zeilen entstehen
    // hier ueber nbrNoteFrame() (Ein-Token-Pfad = "direkt gehoert").
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5);
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
    // nbrNotePos() legt keine Zeile mehr an -- ueber nbrNoteFrame() erzeugen.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5);
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
    nbrNotePos(m, "DK5EN-93", 48.40f, 11.75f, false, 0, 5);   // Freising, Zeile 0 existiert immer
    // nbrNotePos() legt keine Zeile mehr an -- OE1AAA-1 erst per
    // nbrNoteFrame() erzeugen, dann die Position draufschreiben.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5);
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

// --- 10: 2-Hop-Fenster (Betreiber-Vorgabe) -----------------------------------

void test_two_hop_window_only_creates_rows_for_last_two_tokens(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    // A,B liegen VOR dem Fenster (start = 4-2 = 2) -- sie bekommen keine
    // Zeile. Nur C,D (das Fenster) und Zeile 0 existieren danach.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 100);
    TEST_ASSERT_TRUE(hits > 0);

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));
    int iccc = nbrFind(m, "OE1CCC-3");
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iccc > 0);
    TEST_ASSERT_TRUE(iddd > 0);

    // Kanten: nur C->D und D->ich, sonst keine einzige Zelle gesetzt.
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iccc][iddd].cnt_text);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iddd][0].cnt_text);
    int nonzero = 0;
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
            if (m.cells[x][y].cnt_text || m.cells[x][y].cnt_pos || m.cells[x][y].cnt_hey)
                nonzero++;
    TEST_ASSERT_EQUAL_INT(2, nonzero);
}

void test_two_token_path_is_unaffected_by_the_window(void)
{
    // ntok <= 2: das Fenster ist der GANZE Pfad, das Verhalten bleibt
    // identisch zu vor der Aenderung (Regression neben test_pair_rule_text_frame).
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
}

void test_rule3_edge_between_two_existing_rows_creates_no_new_row(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    // A und B bekommen ueber kurze Pfade je eine eigene Zeile.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 10);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0);

    int rows_before = 0;
    for (int i = 0; i < NBR_MAX_ROWS; i++)
        if (i == 0 || (m.rows[i].flags & NBR_FLAG_USED))
            rows_before++;

    // Ein spaeterer 4-Token-Pfad A,B,C,D: C,D sind das Fenster, A,B liegen
    // davor. A->B ist eine Kante zwischen zwei BESTEHENDEN Zeilen -- Regel 3
    // traegt sie trotzdem ein, OHNE eine neue Zeile fuer A oder B anzulegen.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 20);
    TEST_ASSERT_TRUE(hits > 0);

    int rows_after = 0;
    for (int i = 0; i < NBR_MAX_ROWS; i++)
        if (i == 0 || (m.rows[i].flags & NBR_FLAG_USED))
            rows_after++;

    TEST_ASSERT_EQUAL_INT(rows_before + 2, rows_after);   // nur C und D sind neu
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text);   // A->B trotzdem eingetragen
}

// --- 11: nbrNotePos() legt keine Zeile mehr an -------------------------------

void test_note_pos_unknown_call_leaves_matrix_untouched_known_call_writes(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10);
    memcpy(&before, &m, sizeof(m));

    // OE1BBB-2 hat keine Zeile -> nbrNotePos() darf die Matrix nicht anfassen.
    nbrNotePos(m, "OE1BBB-2", 48.0f, 11.0f, true, 1, 20);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // OE1AAA-1 hat eine Zeile -> nbrNotePos() schreibt.
    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, true, 1, 20);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(m.rows[iaaa].flags & NBR_FLAG_POS);
    TEST_ASSERT_EQUAL_FLOAT(48.0f, m.rows[iaaa].lat);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, m.rows[iaaa].lon);
    TEST_ASSERT_TRUE(memcmp(&before, &m, sizeof(m)) != 0);
}

// --- 12: HEY-Berichtsgruppen nur innerhalb des Fensters -----------------------

void test_hey_groups_only_apply_within_window_or_existing_rows(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    // Vier Token: C,D sind das Fenster, A,B liegen davor und haben keine
    // Zeile. R hat drei Gruppen fuer die drei Pfadpaare A-B, B-C, C-D --
    // nur die dritte (C-D, beide Enden im Fenster) darf ankommen.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", '@',
                             "R5;3,90,7;3,95,7;3,99,7;", false, -70, 100);
    TEST_ASSERT_TRUE(hits > 0);

    // A und B duerfen durch die HEY-Gruppen KEINE Zeile bekommen haben.
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));

    int iccc = nbrFind(m, "OE1CCC-3");
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iccc > 0 && iddd > 0);
    TEST_ASSERT_EQUAL_INT8(-99, m.cells[iccc][iddd].rssi);   // dritte Gruppe (C->D) kam an
}

// --- 13: Log-Emitter (docs/nbr-logformat.md) ---------------------------------

static char g_log_buf[4096];

static void test_log_capture(const char *line)
{
    size_t used = strlen(g_log_buf);
    if (used + 2 >= sizeof(g_log_buf))
        return;
    strncat(g_log_buf, line, sizeof(g_log_buf) - used - 2);
    strncat(g_log_buf, "\n", sizeof(g_log_buf) - strlen(g_log_buf) - 1);
}

static void test_log_reset(void)
{
    g_log_buf[0] = '\0';
}

void test_log_emitter_field_sequence_matches_format_doc(void)
{
    // nbrLog == NULL (Ausgangszustand): keine Ausgabe, kein Absturz.
    TEST_ASSERT_NULL(nbrLog);
    NbrMatrix m0;
    nbrInit(m0, "DK5EN-93", 0);
    nbrNoteFrame(m0, "OE1ZZZ-9,OE1YYY-8", ':', NULL, false, -80, 10);
    nbrNotePos(m0, "OE1ZZZ-9", 1.0f, 2.0f, false, 0, 10);
    nbrLogSnapshot(m0, 10);

    test_log_reset();
    nbrLog = test_log_capture;

    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);

    // EDGE + ME ueber einen einfachen 2-Token-Pfad.
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 100);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|EDGE|100|OE1AAA-1|OE1BBB-2|T|0|1\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ME|100|OE1BBB-2|T|-80|1\n"));

    // CUT: derselbe Pfad um zwei weitere Hops verlaengert.
    test_log_reset();
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 200);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf,
        "[NBR]|CUT|200|4|2|OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4\n"));

    // DROP: ein 2-Zeichen-Token unterschreitet die Mindestlaenge 3.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "AB,OE1BBB-2", ':', NULL, false, -80, 300));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|DROP|300|TOK|AB,OE1BBB-2\n"));

    // EVICT: Tabelle (NBR_MAX_ROWS=5 in dieser Umgebung) mit 0 + 4 Fremden
    // vollstopfen, ein fuenftes verdraengt das aelteste.
    NbrMatrix m2;
    nbrInit(m2, "DK5EN-93", 0);
    nbrNoteFrame(m2, "OE1AAA-1", ':', NULL, false, -80, 10);
    nbrNoteFrame(m2, "OE1BBB-2", ':', NULL, false, -80, 20);
    nbrNoteFrame(m2, "OE1CCC-3", ':', NULL, false, -80, 30);
    nbrNoteFrame(m2, "OE1DDD-4", ':', NULL, false, -80, 40);
    int iaaa = nbrFind(m2, "OE1AAA-1");
    test_log_reset();
    nbrNoteFrame(m2, "OE1EEE-5", ':', NULL, false, -80, 50);
    char expect[64];
    snprintf(expect, sizeof(expect), "[NBR]|EVICT|50|%d|OE1AAA-1|OE1EEE-5\n", iaaa);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, expect));

    nbrLog = NULL;
}

void test_log_snapshot_emits_snap_row_per_used_row_and_endsnap(void)
{
    test_log_reset();
    nbrLog = test_log_capture;

    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 10);

    nbrLogSnapshot(m, 20);

    char expect_snap[64];
    snprintf(expect_snap, sizeof(expect_snap), "[NBR]|SNAP|20|DK5EN-93|3|%d|", (int)NBR_MAX_ROWS);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, expect_snap));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ENDSNAP|20\n"));

    int row_lines = 0;
    const char *p = g_log_buf;
    while ((p = strstr(p, "[NBR]|ROW|")) != NULL)
    {
        row_lines++;
        p += 1;
    }
    TEST_ASSERT_EQUAL_INT(3, row_lines);   // Zeile 0 + AAA + BBB

    nbrLog = NULL;
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
    RUN_TEST(test_long_path_beyond_two_hop_window_is_accepted_not_rejected);
    RUN_TEST(test_sweep_clears_ghost_cell_before_it_can_wrap_fresh_again);
    RUN_TEST(test_more_than_eight_hops_is_rejected_whole);
    RUN_TEST(test_reset_keeps_only_row_zero_call);
    RUN_TEST(test_note_pos_mesh_false_clears_previously_set_mesh_flag);
    RUN_TEST(test_hearers_returns_total_count_beyond_what_was_written);
    RUN_TEST(test_exclusive_rows_per_concept_43_example);
    RUN_TEST(test_reach_haversine_distance_and_partner);
    RUN_TEST(test_format_row_contains_callsign_and_hearers);
    RUN_TEST(test_two_hop_window_only_creates_rows_for_last_two_tokens);
    RUN_TEST(test_two_token_path_is_unaffected_by_the_window);
    RUN_TEST(test_rule3_edge_between_two_existing_rows_creates_no_new_row);
    RUN_TEST(test_note_pos_unknown_call_leaves_matrix_untouched_known_call_writes);
    RUN_TEST(test_hey_groups_only_apply_within_window_or_existing_rows);
    RUN_TEST(test_log_emitter_field_sequence_matches_format_doc);
    RUN_TEST(test_log_snapshot_emits_snap_row_per_used_row_and_endsnap);
    return UNITY_END();
}
