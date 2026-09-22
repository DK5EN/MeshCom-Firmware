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

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
    TEST_ASSERT_EQUAL_INT(2, hits);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_TRUE(ibbb > 0);

    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text);
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[ibbb][0].cnt_text);
    TEST_ASSERT_EQUAL_INT8(5, m.cells[ibbb][0].snr);   // ME-Schritt speichert snr_here (5), nicht rssi_here (-80)

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
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", ':', NULL, false, -70, 5, 200);
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
    nbrNoteFrame(m, "OE1AAA-1,DK5EN-93", ':', NULL, false, -70, 5, 200);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    // Stufe 2 (Konzept 2.3 / 5.8 Punkt 0): auch das Paar (AAA, ich) schreibt
    // Spalte 0 nicht mehr -- ob ich AAA je per Funk gehoert habe, weiss nur
    // der ME-Schritt beim Empfang. Vorher stand hier 1.
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[iaaa][0].cnt_text);   // kein Hoerbeweis aus meinem eigenen Pfad
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[0][iaaa].cnt_text);   // kein Zusatztreffer aus dem Echo
}

// --- 3: HEY-Berichtsgruppen --------------------------------------------------

void test_hey_signal_report_groups_set_snr_per_pair(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    // Drittes Feld je Gruppe ist SNR, vorzeichenbehaftet -- die zweite Gruppe
    // testet gleich den negativen Fall ("3,101,-10" statt eines positiven
    // Werts), das mittlere RSSI-Feld (97/101) wird seit der Umstellung nicht
    // mehr gespeichert.
    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,OE1BBB-2", '@',
                             "R5;3,97,7;4,101,-10;", true, -60, 5, 300);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_INT8(7, m.cells[0][iaaa].snr);      // AAA hoert mich mit SNR 7 dB
    TEST_ASSERT_EQUAL_INT8(-10, m.cells[iaaa][ibbb].snr); // BBB hoert AAA mit SNR -10 dB
    TEST_ASSERT_EQUAL_INT8(5, m.cells[ibbb][0].snr);      // ich hoere BBB: ME-Schritt speichert snr_here
    TEST_ASSERT_TRUE(m.rows[0].flags & NBR_FLAG_GW);
}

void test_hey_group_parses_negative_snr_field(void)
{
    // Eigener Test fuer den Vorzeichen-Parser (nbrParseInt in nbr_matrix.cpp):
    // eine einzelne Gruppe mit negativem SNR, isoliert von den uebrigen
    // HEY-Feldern der Nachbartests oben.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", '@', "R5;3,115,-10;", false, -60, 5, 300);
    TEST_ASSERT_TRUE(hits > 0);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_EQUAL_INT8(-10, m.cells[0][iaaa].snr);
}

void test_hey_old_format_counts_hits_without_snr(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,OE1BBB-2", '@', "R5;", true, -60, 5, 300);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.cells[0][iaaa].snr);      // kein Bericht -> unbekannt
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.cells[iaaa][ibbb].snr);
    TEST_ASSERT_EQUAL_INT8(5, m.cells[ibbb][0].snr);      // der letzte Hop bekommt snr_here trotzdem
}

// --- 4: Ablehnungen lassen die Matrix unangetastet --------------------------

void test_rejects_leave_matrix_byte_identical(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 50);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 50);
    memcpy(&before, &m, sizeof(m));

    // ungueltiges Rufzeichen (2 Zeichen, unter der Mindestlaenge 3)
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "AB,OE1BBB-2", ':', NULL, false, -80, 5, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // Schleife: dasselbe Rufzeichen zweimal im Pfad
    TEST_ASSERT_EQUAL_INT(-2, nbrNoteFrame(m, "OE1AAA-1,OE1AAA-1", ':', NULL, false, -80, 5, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // unbekannter Frame-Typ
    TEST_ASSERT_EQUAL_INT(0, nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", 'X', NULL, false, -80, 5, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

// --- 5: Alterung und Ueberlauf -----------------------------------------------

void test_aging_720_min_window_and_stale_reset(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");

    TEST_ASSERT_TRUE(nbrFresh(m.cells[iaaa][ibbb].last_min, 819));   // 719 min alt
    TEST_ASSERT_FALSE(nbrFresh(m.cells[iaaa][ibbb].last_min, 820));  // genau 720: nicht mehr frisch

    // Treffer auf eine inzwischen verfallene Zelle faengt bei 1 an, nicht bei 2.
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 900);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 20);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 30);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 5, 40);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0);

    // AAA traegt eine Beobachtung als ZEILE und als SPALTE -- die
    // Verdraengung muss beide Richtungen nullen, nicht nur eine.
    m.cells[iaaa][ibbb].cnt_text = 1; m.cells[iaaa][ibbb].last_min = 10;
    m.cells[ibbb][iaaa].cnt_text = 1; m.cells[ibbb][iaaa].last_min = 10;

    // Tabelle ist voll (Zeile 0 + 4 Fremde = NBR_MAX_ROWS). AAA hat mit
    // last_min=10 die groesste Altersluecke zu now=50 und weicht.
    nbrNoteFrame(m, "OE1EEE-5", ':', NULL, false, -80, 5, 50);

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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 100);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 100);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 100);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 5, 100);
    // Tabelle ist jetzt voll (Zeile 0 + 4 Fremde), alle vier mit derselben
    // Letztzeit. Ohne den Kollisionsschutz wuerden EEE UND FFF beide "die
    // aelteste Zeile" (Index 1) planen; der zweite Treffer faellt dann auf
    // die Diagonale statt auf ein eigenes Paar.
    int hits = nbrNoteFrame(m, "OE1EEE-5,OE1FFF-6", ':', NULL, false, -80, 5, 100);
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
                             ':', NULL, false, -80, 5, 100);
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

// --- M3: eine bestehende Zeile darf nicht Opfer eines Geschwister-Tokens ---
// --- im selben Frame werden (Advisor-Fund 2026-09-21) -----------------------

void test_existing_row_not_evicted_by_sibling_window_token(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    // Tabelle voll (Zeile 0 + 4 Fremde, NBR_MAX_ROWS=5 in dieser Umgebung).
    // OE1BBB-2 hat mit last_min=10 die groesste Altersluecke und waere ohne
    // den Fix das Verdraengungsopfer der Wahl.
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 20);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 5, 30);
    nbrNoteFrame(m, "OE1EEE-5", ':', NULL, false, -80, 5, 40);
    int ibbb_before = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(ibbb_before > 0);

    // Pfad "OE1AAA-1,OE1BBB-2": A ist unbekannt, B ist die aelteste
    // bestehende Zeile. Planung fuer A findet keine freie Zeile und waehlt
    // (ohne den Fix) die aelteste -- Bs eigene Zeile. Planung fuer B trifft
    // per Rufzeichen ebenfalls diese Zeile, weil protected_mask dort nicht
    // geprueft wurde: beide committen auf denselben Index, A verdraengt B,
    // B verdraengt A zurueck, und A hat am Ende gar keine Zeile. Mit dem Fix
    // ist B geschuetzt, sobald es aufgeloest ist -- A verdraengt stattdessen
    // die naechst-aelteste UNGESCHUETZTE Zeile (hier: OE1CCC-3). Dass dabei
    // ueberhaupt jemand weicht, ist normale Verdraengung (Tabelle voll) und
    // kein Fehler -- der Fehler war ausschliesslich, DASS B mitverdraengt
    // wurde, obwohl es im selben Frame selbst vorkommt.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa >= 0);            // A muss eine eigene Zeile bekommen
    TEST_ASSERT_EQUAL_INT(ibbb_before, ibbb); // B behaelt genau seinen Index
    TEST_ASSERT_TRUE(iaaa != ibbb);         // keine gemeinsame Zeile / Diagonale
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[iaaa][ibbb].cnt_text); // A->B normal eingetragen

    // Die naechst-aeltere, UNGESCHUETZTE Zeile (CCC) weicht wie bei jeder
    // normalen Verdraengung; D und E, juenger als CCC, bleiben unangetastet.
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1CCC-3"));
    TEST_ASSERT_TRUE(nbrFind(m, "OE1DDD-4") >= 0);
    TEST_ASSERT_TRUE(nbrFind(m, "OE1EEE-5") >= 0);
}

// --- M2: 16-Bit-Ueberlauf darf keine tote Zelle wiederbeleben ---------------

void test_sweep_clears_ghost_cell_before_it_can_wrap_fresh_again(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
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
        nbrNoteFrame(m, "OE1ZZZ-9,OE1YYY-8", ':', NULL, false, -80, 5, (uint16_t)t);
    }
    TEST_ASSERT_EQUAL_UINT32(65636u, t);

    uint16_t now = (uint16_t)t;
    TEST_ASSERT_EQUAL_UINT16(100, now);  // der Umlauf: 65636 mod 65536 ist wieder 100

    // Ohne den Sweep waere nbrFresh(100, 100) wahr (Alter 0) und die alten
    // Zaehler stuenden noch da -- der Geist saehe aus wie "gerade eben
    // getroffen". Der Sweep muss das laengst aufgeraeumt haben.
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[iaaa][ibbb].cnt_text);
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.cells[iaaa][ibbb].snr);
}

// --- L4: weitere Randfaelle ---------------------------------------------------

void test_more_than_eight_hops_is_rejected_whole(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    memcpy(&before, &m, sizeof(m));

    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m,
        "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5,OE1FFF-6,OE1GGG-7,OE1HHH-8,OE1III-9",
        ':', NULL, false, -80, 5, 100));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

void test_reset_keeps_only_row_zero_call(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
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
            // Jede Zelle traegt NBR_SNR_UNKNOWN, nicht 0 (ein memset() allein
            // wuerde 0 setzen -- einen GUELTIGEN SNR-Wert statt "unbekannt",
            // siehe nbrFillUnknownSnr() in nbr_matrix.cpp).
            TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.cells[x][y].snr);
        }
}

void test_note_pos_mesh_false_clears_previously_set_mesh_flag(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // nbrNotePos() legt keine Zeile mehr an -- erst per nbrNoteFrame()
    // erzeugen, dann die Position draufschreiben.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 5);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 5);
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

// --- 7b: Betreiberfrage nbrRowMeshNeed() (Advisor-Pass 2026-09-21) ----------

void test_mesh_need_answers_the_opposite_question_from_exclusive(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // AAA und BBB direkt gehoert (Ein-Token-Pfad setzt cells[X][0]).
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    // CCC bekommt eine Zeile, aber NICHT direkt: letzter Hop ist AAA (schon
    // eine bestehende Zeile), die Kante CCC->AAA traegt nur "AAA hat CCC
    // gehoert" ein, cells[CCC][0] bleibt ungesetzt.
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");

    const uint16_t now = 10;

    TEST_ASSERT_EQUAL_STRING("NA", nbrRowMeshNeed(m, 0, now));      // Zeile 0: Frage nicht gestellt
    TEST_ASSERT_EQUAL_STRING("NA", nbrRowMeshNeed(m, iccc, now));   // CCC ist kein direkter Nachbar
    TEST_ASSERT_EQUAL_STRING("MESH", nbrRowMeshNeed(m, iaaa, now)); // AAA hoert CCC exklusiv
    TEST_ASSERT_EQUAL_STRING("RED", nbrRowMeshNeed(m, ibbb, now));  // BBB hoert niemanden -> H(BBB) leer

    // BBB hoert CCC jetzt auch mit -> AAA ist nicht mehr die einzige Deckung
    // fuer CCC, AAAs Meshen ist nicht mehr noetig. Das <verdict> (Sicht auf
    // CCC als Gehoerten) bleibt davon unberuehrt -- andere Frage.
    m.cells[iccc][ibbb].cnt_text = 1; m.cells[iccc][ibbb].last_min = now;
    TEST_ASSERT_EQUAL_STRING("RED", nbrRowMeshNeed(m, iaaa, now));
}

// --- 8: Reichweite -----------------------------------------------------------

void test_reach_haversine_distance_and_partner(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNotePos(m, "DK5EN-93", 48.40f, 11.75f, false, 0, 5);   // Freising, Zeile 0 existiert immer
    // nbrNotePos() legt keine Zeile mehr an -- OE1AAA-1 erst per
    // nbrNoteFrame() erzeugen, dann die Position draufschreiben.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
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
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 10);
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
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 5, 100);
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

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 10);
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
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 5, 20);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
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
                             "R5;3,90,7;3,95,7;3,99,7;", false, -70, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    // A und B duerfen durch die HEY-Gruppen KEINE Zeile bekommen haben.
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));

    int iccc = nbrFind(m, "OE1CCC-3");
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iccc > 0 && iddd > 0);
    TEST_ASSERT_EQUAL_INT8(7, m.cells[iccc][iddd].snr);   // dritte Gruppe (C->D) kam an, SNR statt RSSI
}

// --- 14: Vor-Fenster-Kanten duerfen Zeilen nicht verjuengen -----------------
// --- (Advisor-Fund 2026-09-21) ----------------------------------------------

void test_pre_window_edge_does_not_refresh_row_age(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    // X bekommt frueh eine eigene Zeile ueber echten Direktempfang.
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 10);
    int ix = nbrFind(m, "OE1XXX-9");
    TEST_ASSERT_TRUE(ix > 0);
    nbrNoteFrame(m, "OE1YYY-8", ':', NULL, false, -80, 5, 10);

    // Ab jetzt taucht X nur noch als Vor-Fenster-Token auf: Pfad
    // X,Y,C,D mit C,D als 2-Hop-Fenster (start=2). Die Paare (X,Y) und
    // (Y,C) haben Index < start, sind also Gratis-Kanten (Regel 3) --
    // sie treffen die Zelle, duerfen aber Xs (und Ys) Zeilenalter nicht
    // anfassen. Wiederholt, bis mehr als NBR_WINDOW_MIN seit Xs letzter
    // ECHTER Zeilenberuehrung (t=10) vergangen ist.
    uint16_t t = 10;
    for (int i = 0; i < 8; i++)
    {
        t = (uint16_t)(t + 100);
        nbrNoteFrame(m, "OE1XXX-9,OE1YYY-8,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 5, t);
    }
    TEST_ASSERT_TRUE((uint16_t)(t - 10) > NBR_WINDOW_MIN); // Testaufbau: Fenster sicher ueberschritten

    // X wurde in jedem dieser Frames als Zellentreffer beruehrt, aber nie
    // als Zeile verjuengt -- die Zeile muss aus der Frische gefallen sein.
    TEST_ASSERT_FALSE(nbrFresh(m.rows[ix].last_min, t));
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
    nbrNoteFrame(m0, "OE1ZZZ-9,OE1YYY-8", ':', NULL, false, -80, 5, 10);
    nbrNotePos(m0, "OE1ZZZ-9", 1.0f, 2.0f, false, 0, 10);
    nbrLogSnapshot(m0, 10);

    test_log_reset();
    nbrLog = test_log_capture;

    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);

    // EDGE + ME ueber einen einfachen 2-Token-Pfad.
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 100);
    // EDGE: <rssi> ist seit der SNR-Umstellung immer 0, trailing <snr> ist
    // NA (die Paar-Zelle bekommt nie einen SNR, nur der ME-Schritt schreibt
    // in eine Zelle). ME: <rssi> ist rssi_here durchgereicht (-80), trailing
    // <snr> ist snr_here (5), das die Zelle tatsaechlich speichert.
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|EDGE|100|OE1AAA-1|OE1BBB-2|T|0|1|NA\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ME|100|OE1BBB-2|T|-80|1|5\n"));

    // CUT: derselbe Pfad um zwei weitere Hops verlaengert.
    test_log_reset();
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", ':', NULL, false, -80, 5, 200);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf,
        "[NBR]|CUT|200|4|2|OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4\n"));

    // DROP: ein 2-Zeichen-Token unterschreitet die Mindestlaenge 3.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "AB,OE1BBB-2", ':', NULL, false, -80, 5, 300));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|DROP|300|TOK|AB,OE1BBB-2\n"));

    // EVICT: Tabelle (NBR_MAX_ROWS=5 in dieser Umgebung) mit 0 + 4 Fremden
    // vollstopfen, ein fuenftes verdraengt das aelteste.
    NbrMatrix m2;
    nbrInit(m2, "DK5EN-93", 0);
    nbrNoteFrame(m2, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m2, "OE1BBB-2", ':', NULL, false, -80, 5, 20);
    nbrNoteFrame(m2, "OE1CCC-3", ':', NULL, false, -80, 5, 30);
    nbrNoteFrame(m2, "OE1DDD-4", ':', NULL, false, -80, 5, 40);
    int iaaa = nbrFind(m2, "OE1AAA-1");
    test_log_reset();
    nbrNoteFrame(m2, "OE1EEE-5", ':', NULL, false, -80, 5, 50);
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
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 10);

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

    // <meshneed> ist die LETZTE Spalte, nach dem unveraenderten <verdict>
    // (docs/nbr-logformat.md). AAA hoert niemanden -> H(AAA) leer -> RED,
    // obwohl <verdict> EXCL ist (ich bin AAAs einziger Hoerer) -- die
    // beiden Felder duerfen genau das auseinanderfallen. Nur die letzten
    // beiden Felder werden geprueft, um die Zeile nicht an <flags>/<age>/
    // <hearers> festzunageln.
    int iaaa = nbrFind(m, "OE1AAA-1");
    char prefix_aaa[48];
    snprintf(prefix_aaa, sizeof(prefix_aaa), "[NBR]|ROW|20|%d|OE1AAA-1|", iaaa);
    const char *row_aaa = strstr(g_log_buf, prefix_aaa);
    TEST_ASSERT_NOT_NULL(row_aaa);
    const char *eol_aaa = strchr(row_aaa, '\n');
    TEST_ASSERT_NOT_NULL(eol_aaa);
    TEST_ASSERT_EQUAL_INT(0, strncmp(eol_aaa - 9, "|EXCL|RED", 9));

    // Zeile 0: <verdict> UNK, <meshneed> NA (die Frage ist auf sich selbst
    // nicht gestellt).
    const char *row0 = strstr(g_log_buf, "[NBR]|ROW|20|0|DK5EN-93|");
    TEST_ASSERT_NOT_NULL(row0);
    const char *eol0 = strchr(row0, '\n');
    TEST_ASSERT_NOT_NULL(eol0);
    TEST_ASSERT_EQUAL_INT(0, strncmp(eol0 - 7, "|UNK|NA", 7));

    nbrLog = NULL;
}

// --- 12: Stufe 2 (docs/nbr-wichtigkeit-konzept.md, Abschnitt 4, 5, 5.8) ------

void test_gateway_echo_does_not_mark_server_origin_as_directly_heard(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // AAA ist mein direkter Nachbar, FAR ein Knoten, den AAA wiederholt hat
    // (2-Hop-Zeile ueber das Fenster).
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE3FAR-9,OE1AAA-1", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ifar = nbrFind(m, "OE3FAR-9");
    TEST_ASSERT_TRUE(iaaa > 0 && ifar > 0);

    // Ich (Gateway) habe FARs Frame vom Server auf LoRa gesetzt, AAA hat
    // meine Aussendung wiederholt: "FAR,ich,AAA" kommt zurueck. Das Paar
    // (FAR, ich) darf NICHT "ich habe FAR gehoert" eintragen.
    int hits = nbrNoteFrame(m, "OE3FAR-9,DK5EN-93,OE1AAA-1", ':', NULL, false, -80, 5, 6);
    TEST_ASSERT_EQUAL_INT(2, hits);                                  // (ich,AAA) + ME(AAA), nicht (FAR,ich)
    TEST_ASSERT_EQUAL_UINT8(0, m.cells[ifar][0].cnt_text);           // FAR bleibt nicht-direkt
    TEST_ASSERT_EQUAL_UINT8(1, m.cells[0][iaaa].cnt_text);           // AAA hat mich gehoert
    TEST_ASSERT_EQUAL_UINT8(3, m.cells[iaaa][0].cnt_text);           // ich habe AAA zum dritten Mal gehoert (ME-Schritt)
    TEST_ASSERT_EQUAL_STRING("NA", nbrRowMeshNeed(m, ifar, 6));      // FAR ist kein direkter Nachbar
    TEST_ASSERT_EQUAL_INT(0, (int)(nbrDirectMask(m, 6) & (1u << (unsigned)ifar)));
}

void test_eviction_prefers_two_hop_row_over_older_direct_rows(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);            // direkt, aelteste
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 20);            // direkt
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 30);            // direkt
    nbrNoteFrame(m, "OE1DDD-4,OE1CCC-3", ':', NULL, false, -80, 5, 40);   // DDD nur ueber CCC: 2-Hop-Zeile, juengste
    int iaaa = nbrFind(m, "OE1AAA-1");
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iaaa > 0 && iddd > 0);

    // Tabelle voll (NBR_MAX_ROWS = 5). Aelteste Zeile ist AAA, aber AAA ist
    // frisch direkt gehoert; die einzige 2-Hop-Zeile DDD weicht zuerst.
    nbrNoteFrame(m, "OE1EEE-5", ':', NULL, false, -80, 5, 50);
    TEST_ASSERT_TRUE(nbrFind(m, "OE1AAA-1") == iaaa);
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1DDD-4"));
    TEST_ASSERT_EQUAL_INT(iddd, nbrFind(m, "OE1EEE-5"));

    // Ohne 2-Hop-Zeile faellt die Wahl wie bisher auf die aelteste Direktzeile.
    nbrNoteFrame(m, "OE1FFF-6", ':', NULL, false, -80, 5, 60);
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(iaaa, nbrFind(m, "OE1FFF-6"));
}

void test_mesh_need_count_is_the_number_behind_the_word(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1DDD-4,OE1AAA-1", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_EQUAL_INT(-1, nbrRowMeshNeedCount(m, 0, 10));
    TEST_ASSERT_EQUAL_INT(-1, nbrRowMeshNeedCount(m, iccc, 10));
    TEST_ASSERT_EQUAL_INT(2, nbrRowMeshNeedCount(m, iaaa, 10));   // CCC und DDD nur ueber AAA
    TEST_ASSERT_EQUAL_INT(0, nbrRowMeshNeedCount(m, ibbb, 10));
    TEST_ASSERT_EQUAL_STRING("MESH", nbrRowMeshNeed(m, iaaa, 10));
    m.cells[iccc][ibbb].cnt_text = 1; m.cells[iccc][ibbb].last_min = 10;   // BBB hoert CCC auch
    TEST_ASSERT_EQUAL_INT(1, nbrRowMeshNeedCount(m, iaaa, 10));
    TEST_ASSERT_EQUAL_STRING("MESH", nbrRowMeshNeed(m, iaaa, 10));
}

void test_relay_need_masks_follow_concept_section_5(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // AAA: direkt, und AAA hoert mich (hat meinen Frame wiederholt).
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", ':', NULL, false, -80, 5, 5);
    // BBB: direkt. LLL: direkt, sonst von niemandem gehoert (Blatt).
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int illl = nbrFind(m, "OE1LLL-7");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0 && illl > 0);
    const uint32_t bA = 1u << (unsigned)iaaa, bB = 1u << (unsigned)ibbb, bL = 1u << (unsigned)illl;
    const uint16_t now = 10;

    // Frame von OE9ORG-1, letzter Hop AAA: AAA hat ihn (steht im Pfad).
    // BBB und LLL brauchen ihn; fuer beide ist niemand als Alternative
    // bekannt -> Fall A, beide "allein". sym=false: dieser Test bleibt rein
    // beobachtet, die Symmetrie-Annahme hat ihre eigenen Tests weiter unten.
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(bB | bL, r.need);
    TEST_ASSERT_EQUAL_UINT32(bB | bL, r.alone);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);

    // BBB hat AAA gehoert (cell[AAA][BBB]): BBB hat den Frame damit
    // wahrscheinlich schon (HatF) -> faellt aus dem Bedarf.
    m.cells[iaaa][ibbb].cnt_text = 1; m.cells[iaaa][ibbb].last_min = now;
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(bL, r.need);
    TEST_ASSERT_EQUAL_UINT32(bL, r.alone);

    // Frame von OE9ORG-1 ueber CCC (kein Nachbar von mir, keine Zeile),
    // BBB hat CCC nicht gehoert: BBB im Bedarf; Alternative fuer BBB ist AAA
    // nur, wenn AAA den Frame hat -- AAA steht nicht im Pfad und hat CCC
    // nicht gehoert -> BBB allein. Sobald AAA CCC gehoert hat, deckt AAA BBB.
    m.cells[iaaa][ibbb].last_min = now; // BBB hoert AAA (Deckung), s.o.
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(bA | bB | bL, r.need);
    TEST_ASSERT_EQUAL_UINT32(bA | bB | bL, r.alone);
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", ':', NULL, false, -80, 5, now);   // AAA hat CCC gehoert
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iccc > 0);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(bB | bL, r.need);      // AAA hat den Frame (HatF), BBB und LLL nicht
    TEST_ASSERT_EQUAL_UINT32(bL, r.alone);          // BBB erreicht AAA, LLL erreicht nur ich

    // Gateway-Ausnahme: ein Blatt mit GW-Flag bekommt den Frame vom Server.
    m.rows[illl].flags |= NBR_FLAG_GW;
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(bB, r.need);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone);           // Fall B
    m.rows[illl].flags &= (uint8_t)~NBR_FLAG_GW;

    // Deckung durch eine gehoerte Wiederholung von AAA: AAAs Hoerer ist BBB
    // (cell[AAA][BBB]; dass AAA CCC gehoert hat, ist die Gegenrichtung und
    // zaehlt nicht); need & ~cover wird 0 fuer den Fall-B-Rest {BBB}. sym=false,
    // relevant=0, msg_id=0, inferred=NULL: reines Beobachtungsverhalten wie vorher.
    uint32_t cover = nbrCoverMask(m, "OE1AAA-1", now, false, 0, 0, NULL);
    TEST_ASSERT_EQUAL_UINT32(bB, cover);
    TEST_ASSERT_EQUAL_UINT32(0, bB & ~cover);
    TEST_ASSERT_EQUAL_UINT32(0, nbrCoverMask(m, "DK5EN-93", now, false, 0, 0, NULL));   // ich selbst decke nichts
    TEST_ASSERT_EQUAL_UINT32(0, nbrCoverMask(m, "OE9ZZZ-1", now, false, 0, 0, NULL));   // unbekannt

    // Ungueltiger Pfad: kein Wissen, keine Masken, known == false.
    r = nbrRelayNeed(m, "bad path!", now, false, 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.need);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone);
    TEST_ASSERT_FALSE(r.known);
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(bB | bL));

    // Gueltiger Pfad, gueltige Matrix: known == true, auch wenn need == 0
    // ("alle Abhaengigen haben den Frame") -- das ist Fall B, kein Unwissen.
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_TRUE(r.known);
}

void test_relay_need_on_empty_matrix_is_no_knowledge_not_case_b(void)
{
    NbrMatrix e;
    nbrInit(e, "DK5EN-93", 0);
    // Leere Matrix (nur Zeile 0): keine abhaengige Zeile, also kein Wissen.
    // Der Aufrufer muss wie heute fluten, statt das Relay als Fall B mit
    // leerem Bedarf einzureihen und beim ersten fremden Echo abzubrechen
    // (Advisor-Fund 2026-09-22).
    NbrNeed r = nbrRelayNeed(e, "OE9ORG-1,OE1AAA-1", 10, false, 0);
    TEST_ASSERT_FALSE(r.known);
    TEST_ASSERT_EQUAL_UINT32(0, r.need);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);
}

void test_exclusive_direct_ignores_two_hop_hearers(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7,OE1CCC-3", ':', NULL, false, -80, 5, 5);   // CCC (2-Hop) hoert LLL
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iaaa > 0 && illl > 0 && iccc > 0);
    // Moment: "OE1LLL-7,OE1CCC-3" macht CCC zum letzten Hop, also direkt --
    // dafuer CCCs Direktheit wieder loeschen, damit CCC ein reiner 2-Hop-Hoerer ist.
    memset(&m.cells[iccc][0], 0, sizeof(NbrCell));

    uint8_t out[NBR_MAX_ROWS];
    int n_old = nbrExclusive(m, 10, out, NBR_MAX_ROWS);
    int n_new = nbrExclusiveDirect(m, 10, out, NBR_MAX_ROWS);
    TEST_ASSERT_EQUAL_INT(1, n_old);      // altes Urteil: nur AAA, LLL gilt als durch CCC gedeckt
    TEST_ASSERT_EQUAL_INT(2, n_new);      // Direktregel: AAA und LLL, CCCs Wiederholung hoere ich nie
    // AAA hoert LLL (direkte Deckung): LLL faellt aus E_self.
    m.cells[illl][iaaa].cnt_text = 1; m.cells[illl][iaaa].last_min = 10;
    TEST_ASSERT_EQUAL_INT(1, nbrExclusiveDirect(m, 10, out, NBR_MAX_ROWS));
    TEST_ASSERT_EQUAL_INT(iaaa, out[0]);
    NbrMatrix e;
    nbrInit(e, "DK5EN-93", 0);
    TEST_ASSERT_EQUAL_INT(-1, nbrExclusiveDirect(e, 10, out, NBR_MAX_ROWS));
}

// --- Symmetrie-Annahme (--nbrsym, nbr_matrix.h NBR_SYM_MIN_SNR) -------------
//
// Cell-Rollen ueberall unten: cells[A][B] = "B hat A gehoert". Die
// Beobachtung, die den Fallback traegt, ist immer die UMGEKEHRTE Richtung
// der Annahme -- "M hat X gehoert" (cells[X][M]) traegt die Annahme "X hoert
// M".

void test_sym_alt_fallback_makes_lone_dependent_not_alone(void)
{
    // Regression: X (LLL) ist ein Nachbar, der NIE relayt -- kein
    // beobachteter Hoerer irgendeines Anbieters (cell[M][X] nie gesetzt) --
    // aber der direkte Anbieter PROV (nicht selbst im Pfad, sondern nur
    // HASF ueber AAA) hat X mit SNR +6 dB gehoert. PROV bewusst NICHT im
    // Pfad, damit die Annahme ausschliesslich ueber den ALT-Fallback laeuft
    // und nicht schon vorher ueber HASF greift (HASF prueft nur gegen
    // Pfadteilnehmer, hier also nur AAA). Die alte (Vor-Symmetrie-)Logik
    // kennt nur cell[M][X] und haette X hier in JEDEM Fall als "allein"
    // (Fall A) eingestuft; TEST_ASSERT_EQUAL_UINT32(0, r.alone & bL) unten
    // waere gegen sie ein Fehlschlag, weil r.alone & bL bei ihr immer != 0
    // bliebe -- genau das haelt der sym=false-Teil hier fest (das alte
    // Verhalten bleibt unveraendert bestehen), der sym=true-Teil beweist
    // die neue Ausnahme.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // AAA: Pfadteilnehmer, direkt gehoert. PROV hat AAA beobachtet gehoert
    // (cell[AAA][PROV], das Paar im Pfad "AAA,PROV") -- das macht PROV zu
    // einem zweiten, unabhaengigen Anbieter (HatF ueber Beobachtung) UND zum
    // letzten Hop, also ebenfalls direkt gehoert.
    nbrNoteFrame(m, "OE1AAA-1,OE1PROV-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);   // X = LLL: relayt nie
    int iaaa = nbrFind(m, "OE1AAA-1");
    int iprov = nbrFind(m, "OE1PROV-1");
    int illl = nbrFind(m, "OE1LLL-7");
    TEST_ASSERT_TRUE(iaaa > 0 && iprov > 0 && illl > 0);
    const uint32_t bL = 1u << (unsigned)illl;
    const uint16_t now = 10;

    // "PROV hat LLL gehoert" (die Beobachtung fuer den ALT-Fallback): cells[LLL][PROV].
    m.cells[illl][iprov].cnt_hey = 1; m.cells[illl][iprov].last_min = now; m.cells[illl][iprov].snr = 6;

    test_log_reset();
    nbrLog = test_log_capture;

    // sym=false: unveraendertes Verhalten, X bleibt allein, keine Annahme.
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0x99);
    TEST_ASSERT_TRUE((r.alone & bL) != 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));

    // sym=true: die Annahme greift, X ist nicht mehr allein.
    test_log_reset();
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0x99);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone & bL);
    TEST_ASSERT_TRUE((r.inferred & bL) != 0);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|SYM|10|00000099|ALT|OE1LLL-7|OE1PROV-1|6\n"));

    nbrLog = NULL;
}

void test_sym_threshold_is_inclusive_at_minus_16(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    const uint32_t bL = 1u << (unsigned)illl;
    const uint16_t now = 10;

    // Genau an der Schwelle (NBR_SYM_MIN_SNR == -16, Vergleich einschliesslich):
    // die Annahme greift.
    m.cells[illl][iaaa].cnt_hey = 1; m.cells[illl][iaaa].last_min = now;
    m.cells[illl][iaaa].snr = NBR_SYM_MIN_SNR;
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone & bL);
    TEST_ASSERT_TRUE((r.inferred & bL) != 0);

    // Ein dB darunter: die Annahme greift NICHT mehr, X bleibt allein.
    m.cells[illl][iaaa].snr = (int8_t)(NBR_SYM_MIN_SNR - 1);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_TRUE((r.alone & bL) != 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred & bL);
}

void test_sym_observed_edge_present_skips_inference_and_log(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    const uint32_t bL = 1u << (unsigned)illl;
    const uint16_t now = 10;

    // LLL hat AAA BEOBACHTET gehoert (cell[AAA][LLL]) -- das reicht schon
    // fuer eine Alternative, unabhaengig von sym; die Annahme darf hier gar
    // nicht erst gebraucht werden.
    m.cells[iaaa][illl].cnt_text = 1; m.cells[iaaa][illl].last_min = now;

    test_log_reset();
    nbrLog = test_log_capture;
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.alone & bL);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);   // keine Annahme, es war Beobachtung
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));
    nbrLog = NULL;
}

void test_sym_unknown_snr_on_reverse_cell_gives_no_inference(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    const uint32_t bL = 1u << (unsigned)illl;
    const uint16_t now = 10;

    // "AAA hat LLL gehoert" ist gesetzt und frisch (ein Text-Frame ohne
    // Signalbericht), aber ohne SNR -- NBR_SNR_UNKNOWN bleibt stehen (siehe
    // nbrInit()/nbrFillUnknownSnr() und nbrHitCell(), das die Zaehler
    // erhoeht, ohne snr anzufassen). Die Annahme darf daraus NICHTS
    // folgern, auch mit sym=true nicht.
    m.cells[illl][iaaa].cnt_text = 1; m.cells[illl][iaaa].last_min = now;
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.cells[illl][iaaa].snr);

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_TRUE((r.alone & bL) != 0);
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);
}

void test_sym_hasf_fallback_removes_dependent_from_need(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, 5);   // P: Pfadteilnehmer, eigene Zeile
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);   // X: Abhaengiger, sonst unbeteiligt
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    TEST_ASSERT_TRUE(ippp > 0 && ixxx > 0);
    const uint32_t bX = 1u << (unsigned)ixxx;
    const uint16_t now = 10;

    // "P hat X gehoert" (Beobachtung fuer den HASF-Fallback): cells[X][P].
    m.cells[ixxx][ippp].cnt_hey = 1; m.cells[ixxx][ippp].last_min = now; m.cells[ixxx][ippp].snr = -5;

    test_log_reset();
    nbrLog = test_log_capture;

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, false, 0x77);
    TEST_ASSERT_TRUE((r.need & bX) != 0);         // sym=false: X bleibt im Bedarf
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred);

    test_log_reset();
    r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0x77);
    TEST_ASSERT_EQUAL_UINT32(0, r.need & bX);     // sym=true: die Annahme deckt X, need faellt weg
    TEST_ASSERT_TRUE((r.inferred & bX) != 0);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|SYM|10|00000077|HASF|OE1XXX-9|OE1PPP-1|-5\n"));

    nbrLog = NULL;
}

void test_sym_hasf_inferred_node_is_no_provider(void)
{
    // Advisor 2026-09-22: X steht nur per HASF-Annahme in hasf ("P hat X
    // gehoert", also angenommen "X hoert P"). Y hat X BEOBACHTET gehoert
    // (cells[X][Y]), sonst niemanden. Vor dem Fix machte die angenommene
    // HatF von X ihn zum Versorger, Y verlor seinen Allein-Status ohne eigene
    // SYM-Zeile (zwei gestapelte Annahmen). Jetzt: hoechstens eine Annahme,
    // Y bleibt allein.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, 5);   // P: Pfadteilnehmer
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);   // X: direkt, HatF nur per Annahme
    nbrNoteFrame(m, "OE1YYY-3", ':', NULL, false, -80, 5, 5);   // Y: direkt, hoert nur X
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    int iyyy = nbrFind(m, "OE1YYY-3");
    TEST_ASSERT_TRUE(ippp > 0 && ixxx > 0 && iyyy > 0);
    const uint32_t bX = 1u << (unsigned)ixxx;
    const uint32_t bY = 1u << (unsigned)iyyy;
    const uint16_t now = 10;

    m.cells[ixxx][ippp].cnt_hey = 1; m.cells[ixxx][ippp].last_min = now; m.cells[ixxx][ippp].snr = -5;  // P hat X gehoert
    m.cells[ixxx][iyyy].cnt_text = 1; m.cells[ixxx][iyyy].last_min = now;                               // Y hat X gehoert

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0x55);
    TEST_ASSERT_TRUE((r.inferred & bX) != 0);      // X per HASF angenommen
    TEST_ASSERT_TRUE((r.need & bY) != 0);
    TEST_ASSERT_TRUE((r.alone & bY) != 0);         // X versorgt Y NICHT
    TEST_ASSERT_EQUAL_UINT32(0, r.inferred & bY);
}

void test_cover_mask_symmetry_fallback_respects_relevant_for_logging(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1MMM-1", ':', NULL, false, -80, 5, 5);   // M: Relayer, eigene Zeile
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);   // X: Kandidat, eigene Zeile
    int im = nbrFind(m, "OE1MMM-1");
    int ix = nbrFind(m, "OE1XXX-9");
    TEST_ASSERT_TRUE(im > 0 && ix > 0);
    const uint16_t now = 10;

    // "M hat X gehoert" (Beobachtung fuer den Fallback): cells[X][M].
    m.cells[ix][im].cnt_hey = 1; m.cells[ix][im].last_min = now; m.cells[ix][im].snr = -12;

    // sym=false: keine Erweiterung, *inferred bleibt 0.
    uint32_t inferred = 0xFFFFFFFF;
    uint32_t cover = nbrCoverMask(m, "OE1MMM-1", now, false, 0, 0, &inferred);
    TEST_ASSERT_EQUAL_UINT32(0, cover & (1u << (unsigned)ix));
    TEST_ASSERT_EQUAL_UINT32(0, inferred);

    // sym=true, relevant OHNE X: die Maske wird trotzdem erweitert (der
    // Aufrufer braucht sie fuer die eigentliche Abzugsrechnung), aber es
    // erscheint KEINE SYM-COVER-Zeile -- relevant filtert nur das Log.
    test_log_reset();
    nbrLog = test_log_capture;
    inferred = 0;
    cover = nbrCoverMask(m, "OE1MMM-1", now, true, 0, 0xAB, &inferred);
    TEST_ASSERT_TRUE((cover & (1u << (unsigned)ix)) != 0);
    TEST_ASSERT_TRUE((inferred & (1u << (unsigned)ix)) != 0);
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));

    // sym=true, relevant MIT X: dieselbe Maske, jetzt mit SYM-COVER-Zeile.
    test_log_reset();
    inferred = 0;
    cover = nbrCoverMask(m, "OE1MMM-1", now, true, (1u << (unsigned)ix), 0xAB, &inferred);
    TEST_ASSERT_TRUE((cover & (1u << (unsigned)ix)) != 0);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|SYM|10|000000AB|COVER|OE1XXX-9|OE1MMM-1|-12\n"));

    nbrLog = NULL;
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_pair_rule_text_frame);
    RUN_TEST(test_own_relay_seen_directly_gives_both_cells);
    RUN_TEST(test_own_frame_returning_via_relay_is_echo_not_direct_hearing);
    RUN_TEST(test_hey_signal_report_groups_set_snr_per_pair);
    RUN_TEST(test_hey_group_parses_negative_snr_field);
    RUN_TEST(test_hey_old_format_counts_hits_without_snr);
    RUN_TEST(test_rejects_leave_matrix_byte_identical);
    RUN_TEST(test_aging_720_min_window_and_stale_reset);
    RUN_TEST(test_eviction_replaces_oldest_row_never_row_zero);
    RUN_TEST(test_existing_row_not_evicted_by_sibling_window_token);
    RUN_TEST(test_two_new_calls_in_one_frame_get_distinct_rows_not_the_diagonal);
    RUN_TEST(test_long_path_beyond_two_hop_window_is_accepted_not_rejected);
    RUN_TEST(test_sweep_clears_ghost_cell_before_it_can_wrap_fresh_again);
    RUN_TEST(test_more_than_eight_hops_is_rejected_whole);
    RUN_TEST(test_reset_keeps_only_row_zero_call);
    RUN_TEST(test_note_pos_mesh_false_clears_previously_set_mesh_flag);
    RUN_TEST(test_hearers_returns_total_count_beyond_what_was_written);
    RUN_TEST(test_exclusive_rows_per_concept_43_example);
    RUN_TEST(test_mesh_need_answers_the_opposite_question_from_exclusive);
    RUN_TEST(test_reach_haversine_distance_and_partner);
    RUN_TEST(test_format_row_contains_callsign_and_hearers);
    RUN_TEST(test_two_hop_window_only_creates_rows_for_last_two_tokens);
    RUN_TEST(test_two_token_path_is_unaffected_by_the_window);
    RUN_TEST(test_rule3_edge_between_two_existing_rows_creates_no_new_row);
    RUN_TEST(test_note_pos_unknown_call_leaves_matrix_untouched_known_call_writes);
    RUN_TEST(test_hey_groups_only_apply_within_window_or_existing_rows);
    RUN_TEST(test_pre_window_edge_does_not_refresh_row_age);
    RUN_TEST(test_log_emitter_field_sequence_matches_format_doc);
    RUN_TEST(test_log_snapshot_emits_snap_row_per_used_row_and_endsnap);
    RUN_TEST(test_gateway_echo_does_not_mark_server_origin_as_directly_heard);
    RUN_TEST(test_eviction_prefers_two_hop_row_over_older_direct_rows);
    RUN_TEST(test_mesh_need_count_is_the_number_behind_the_word);
    RUN_TEST(test_relay_need_masks_follow_concept_section_5);
    RUN_TEST(test_relay_need_on_empty_matrix_is_no_knowledge_not_case_b);
    RUN_TEST(test_exclusive_direct_ignores_two_hop_hearers);
    RUN_TEST(test_sym_alt_fallback_makes_lone_dependent_not_alone);
    RUN_TEST(test_sym_threshold_is_inclusive_at_minus_16);
    RUN_TEST(test_sym_observed_edge_present_skips_inference_and_log);
    RUN_TEST(test_sym_unknown_snr_on_reverse_cell_gives_no_inference);
    RUN_TEST(test_sym_hasf_fallback_removes_dependent_from_need);
    RUN_TEST(test_sym_hasf_inferred_node_is_no_provider);
    RUN_TEST(test_cover_mask_symmetry_fallback_respects_relevant_for_logging);
    return UNITY_END();
}
