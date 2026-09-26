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

// --- Zugriff (Welle 2: Kantenpool statt cells[x][y]) ----------------------
//
// Gelesen wird ueber die oeffentlichen Accessoren (nbrEdgeGet()/nbrRowGet());
// wo ein Test einen Zustand direkt aufbaut (frueher m.cells[x][y].cnt_* = n),
// setzt eset() die Kante ueber die internen Helfer aus nbr_matrix.cpp, damit
// Pool und Masken zusammen stimmen. Kante (x, y) = "y hat x gehoert", wie
// frueher cells[x][y].

static uint8_t ecnt(const NbrMatrix &m, int x, int y)
{
    NbrEdgeView v;
    return nbrEdgeGet(m, x, y, &v) ? v.cnt : 0;
}

static int8_t esnr(const NbrMatrix &m, int x, int y)
{
    NbrEdgeView v;
    return nbrEdgeGet(m, x, y, &v) ? v.snr : (int8_t)NBR_SNR_UNKNOWN;
}

static uint16_t elast(const NbrMatrix &m, int x, int y)
{
    NbrEdgeView v;
    return nbrEdgeGet(m, x, y, &v) ? v.last_min : 0;
}

static void eset(NbrMatrix &m, int x, int y, uint8_t cnt, uint16_t last, int8_t snr = NBR_SNR_UNKNOWN)
{
    int e = nbrIEdgeFind(m, x, y);
    if (e < 0)
        e = nbrIEdgeAlloc(m, x, y, last, NULL);
    m.edge[e].cnt = cnt;
    m.edge[e].last_min = last;
    m.edge[e].snr = snr;
}

static void edel(NbrMatrix &m, int x, int y)
{
    int e = nbrIEdgeFind(m, x, y);
    if (e >= 0)
        nbrIEdgeFree(m, e);
}

static NbrRowView rview(const NbrMatrix &m, int row)
{
    NbrRowView v;
    memset(&v, 0, sizeof(v));
    nbrRowGet(m, row, &v);
    return v;
}

static uint8_t rflags(const NbrMatrix &m, int row)
{
    return rview(m, row).flags;
}

// Masken-Invariante: hears[y] Bit x und heardBy[x] Bit y genau dann, wenn
// eine Kante (x, y) im Pool lebt.
static bool masks_match_pool(const NbrMatrix &m)
{
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
        {
            bool live = false;
            for (int e = 0; e < NBR_MAX_EDGES; e++)
                if (m.edge[e].x == x && m.edge[e].y == y)
                    live = true;
            if (nbrMaskTest(m.hears[y], x) != live || nbrMaskTest(m.heardBy[x], y) != live)
                return false;
        }
    return true;
}

static NbrMask mbits(int a, int b = -1, int c = -1)
{
    NbrMask r = nbrMaskNone();
    nbrMaskSet(r, a);
    nbrMaskSet(r, b);
    nbrMaskSet(r, c);
    return r;
}

#define TEST_ASSERT_MASK_EQ(exp, act) TEST_ASSERT_TRUE(nbrMaskEqual((exp), (act)))

// --- 1: Pfadpaar-Regel -----------------------------------------------------

// Trotz des Namens (aus der Zeit vor dem Text-Sonderfall) laeuft dieser Test
// jetzt mit '!': Text ('bleibt') nimmt den Pfadpaar-Zweig gar nicht mehr,
// siehe nbrNoteFrame()-Kommentar. Die eigentliche Pfadpaar-Mechanik (EDGE +
// ME aus einem 2-Token-Pfad) bleibt fuer '!'/'@' unveraendert und wird hier
// weiter geprueft; das Text-eigene Verhalten (nur ME) hat eigene Tests unten.
void test_pair_rule_text_frame(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_EQUAL_INT(2, hits);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_TRUE(ibbb > 0);

    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ibbb, 0));
    TEST_ASSERT_EQUAL_INT8(5, esnr(m, ibbb, 0));   // ME-Schritt speichert snr_here (5), nicht rssi_here (-80)

    // Keine andere Zelle darf einen Zaehler > 0 tragen.
    int nonzero = 0;
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
            if (ecnt(m, x, y))
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
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", '!', NULL, false, -70, 5, 200);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, 0, iaaa));   // AAA hat mich gehoert
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, 0));   // ich habe AAAs Kopie gehoert
}

void test_own_frame_returning_via_relay_is_echo_not_direct_hearing(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 200);

    // Derselbe Frame kommt ueber AAA zurueck, mit meinem eigenen Rufzeichen
    // als letztem Hop: das ist mein Echo, kein Hoerbeweis fuer "ich habe den
    // letzten Hop gehoert" (Konzept 4.1).
    nbrNoteFrame(m, "OE1AAA-1,DK5EN-93", '!', NULL, false, -70, 5, 200);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    // Stufe 2 (Konzept 2.3 / 5.8 Punkt 0): auch das Paar (AAA, ich) schreibt
    // Spalte 0 nicht mehr -- ob ich AAA je per Funk gehoert habe, weiss nur
    // der ME-Schritt beim Empfang. Vorher stand hier 1.
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, iaaa, 0));   // kein Hoerbeweis aus meinem eigenen Pfad
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, 0, iaaa));   // kein Zusatztreffer aus dem Echo
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
    TEST_ASSERT_EQUAL_INT8(7, esnr(m, 0, iaaa));      // AAA hoert mich mit SNR 7 dB
    TEST_ASSERT_EQUAL_INT8(-10, esnr(m, iaaa, ibbb)); // BBB hoert AAA mit SNR -10 dB
    TEST_ASSERT_EQUAL_INT8(5, esnr(m, ibbb, 0));      // ich hoere BBB: ME-Schritt speichert snr_here
    TEST_ASSERT_TRUE(rflags(m, 0) & NBR_FLAG_GW);
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
    TEST_ASSERT_EQUAL_INT8(-10, esnr(m, 0, iaaa));
}

void test_hey_old_format_counts_hits_without_snr(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);

    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,OE1BBB-2", '@', "R5;", true, -60, 5, 300);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, esnr(m, 0, iaaa));      // kein Bericht -> unbekannt
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, esnr(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_INT8(5, esnr(m, ibbb, 0));      // der letzte Hop bekommt snr_here trotzdem
}

// --- 4: Ablehnungen lassen die Matrix unangetastet --------------------------

void test_rejects_leave_matrix_byte_identical(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 50);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", ':', NULL, false, -80, 5, 50);
    memcpy(&before, &m, sizeof(m));

    // ungueltiges Rufzeichen (2 Zeichen, unter der Mindestlaenge 3). Seit
    // Stufe 2 bekommt ein verworfener Frame noch den ME-Schritt, wenn sein
    // LETZTES Token gueltig ist (test_dropped_frame_still_gets_me_step) --
    // hier ist es das ungueltige, also bleibt alles unangetastet.
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "OE1BBB-2,AB", ':', NULL, false, -80, 5, 60));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // Schleife mit mir als letztem Hop (eigenes Echo): kein ME-Schritt.
    TEST_ASSERT_EQUAL_INT(-2, nbrNoteFrame(m, "DK5EN-93,OE1AAA-1,DK5EN-93", ':', NULL, false, -80, 5, 60));
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
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");

    TEST_ASSERT_TRUE(nbrFresh(elast(m, iaaa, ibbb), 819));   // 719 min alt
    TEST_ASSERT_FALSE(nbrFresh(elast(m, iaaa, ibbb), 820));  // genau 720: nicht mehr frisch

    // Treffer auf eine inzwischen verfallene Zelle faengt bei 1 an, nicht bei 2.
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 900);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb));

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
    eset(m, iaaa, ibbb, 1, 10);
    eset(m, ibbb, iaaa, 1, 10);

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
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, ibbb, iaaa));
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
    int hits = nbrNoteFrame(m, "OE1EEE-5,OE1FFF-6", '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    int ieee = nbrFind(m, "OE1EEE-5");
    int ifff = nbrFind(m, "OE1FFF-6");
    TEST_ASSERT_TRUE(ieee >= 0);
    TEST_ASSERT_TRUE(ifff >= 0);
    TEST_ASSERT_TRUE(ieee != ifff);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ieee, ifff));
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, ieee, ieee));  // keine Diagonale
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, ifff, ifff));
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
                             '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1CCC-3"));
    int iddd = nbrFind(m, "OE1DDD-4");
    int ieee = nbrFind(m, "OE1EEE-5");
    TEST_ASSERT_TRUE(iddd > 0);
    TEST_ASSERT_TRUE(ieee > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iddd, ieee));
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
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa >= 0);            // A muss eine eigene Zeile bekommen
    TEST_ASSERT_EQUAL_INT(ibbb_before, ibbb); // B behaelt genau seinen Index
    TEST_ASSERT_TRUE(iaaa != ibbb);         // keine gemeinsame Zeile / Diagonale
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb)); // A->B normal eingetragen

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
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb));

    // Die absolute Minutenzahl seit Boot laeuft im Test ungekappt weiter;
    // nur der an nbrNoteFrame()/nbrSweep() uebergebene now_min-Wert wird --
    // wie auf dem echten Geraet -- auf uint16_t gekappt. Welle 2: den Sweep
    // ruft der Loop-Task (nicht mehr nbrNoteFrame()); hier in ~1000-min-
    // Schritten, bis die absolute Zeit 65636 erreicht -- 65636 mod 65536 ist
    // wieder 100, also genau der Umlaufpunkt, an dem der Geist ohne Sweep mit
    // Alter 0 wiederauferstehen wuerde.
    uint32_t t = 100;
    while (t < 65636)
    {
        t += (t + 1000 <= 65636) ? 1000 : (65636 - t);
        nbrNoteFrame(m, "OE1ZZZ-9,OE1YYY-8", ':', NULL, false, -80, 5, (uint16_t)t);
        nbrSweep(m, (uint16_t)t);
    }
    TEST_ASSERT_EQUAL_UINT32(65636u, t);

    uint16_t now = (uint16_t)t;
    TEST_ASSERT_EQUAL_UINT16(100, now);  // der Umlauf: 65636 mod 65536 ist wieder 100

    // Ohne den Sweep waere nbrFresh(100, 100) wahr (Alter 0) und die alte
    // Kante stuende noch im Pool -- der Geist saehe aus wie "gerade eben
    // getroffen". Der Sweep hat die Kante nach 720 min freigegeben und die
    // Zeilen nach 32768 min Stille geraeumt.
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, esnr(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));
    TEST_ASSERT_TRUE(masks_match_pool(m));
}

// --- L4: weitere Randfaelle ---------------------------------------------------

void test_more_than_eight_hops_is_rejected_whole(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    memcpy(&before, &m, sizeof(m));

    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m,
        "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5,OE1FFF-6,OE1GGG-7,OE1HHH-8,DK5EN-93",
        ':', NULL, false, -80, 5, 100));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // Stufe 2: mit einem fremden letzten Hop bleibt nur dessen ME-Schritt --
    // eine Zeile fuer den letzten Hop und die Kante (letzter Hop, 0), sonst
    // nichts (keine Pfadkante, keine Zeile fuer die anderen Token).
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m,
        "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4,OE1EEE-5,OE1FFF-6,OE1GGG-7,OE1HHH-8,OE1III-9",
        '!', NULL, false, -80, 5, 100));
    int iii = nbrFind(m, "OE1III-9");
    TEST_ASSERT_TRUE(iii > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iii, 0));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1HHH-8"));
    TEST_ASSERT_EQUAL_INT(2, nbrRowsUsed(m));
    TEST_ASSERT_EQUAL_INT(1, nbrEdgesUsed(m));
    TEST_ASSERT_TRUE(masks_match_pool(m));
}


void test_reset_keeps_only_row_zero_call(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    nbrRowSetFlag(m, 0, NBR_FLAG_GW);

    nbrReset(m, 500);

    TEST_ASSERT_EQUAL_STRING("DK5EN-93", rview(m, 0).call);
    TEST_ASSERT_EQUAL_UINT8(0, rflags(m, 0));
    TEST_ASSERT_EQUAL_UINT16(0, rview(m, 0).last_min);
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        TEST_ASSERT_EQUAL_UINT8(0, rflags(m, i));
    // Kein Eintrag im Pool lebt mehr, jeder traegt NBR_SNR_UNKNOWN (nicht 0,
    // einen GUELTIGEN SNR-Wert), und keine Maske hat noch ein Bit.
    TEST_ASSERT_EQUAL_INT(0, nbrEdgesUsed(m));
    for (int e = 0; e < NBR_MAX_EDGES; e++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xFF, m.edge[e].x);
        TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, m.edge[e].snr);
    }
    for (int i = 0; i < NBR_MAX_ROWS; i++)
    {
        TEST_ASSERT_TRUE(nbrMaskEmpty(m.hears[i]));
        TEST_ASSERT_TRUE(nbrMaskEmpty(m.heardBy[i]));
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
    TEST_ASSERT_TRUE(rflags(m, iaaa) & NBR_FLAG_MESH);

    nbrNotePos(m, "OE1AAA-1", 48.0f, 11.0f, false, 1, 20);
    TEST_ASSERT_FALSE(rflags(m, iaaa) & NBR_FLAG_MESH);
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
    eset(m, iaaa, ibbb, 1, 5);
    eset(m, iaaa, iccc, 1, 5);
    eset(m, iaaa, 0, 1, 5);

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
    eset(m, iaaa, 0, 40, now);
    eset(m, iaaa, ibbb, 9, now);
    // BBB wird von 93 und AAA gehoert.
    eset(m, ibbb, 0, 7, now);
    eset(m, ibbb, iaaa, 9, now);
    // CCC wird nur von 93 gehoert.
    eset(m, iccc, 0, 5, now);

    uint8_t out[8];
    int n = nbrExclusive(m, now, out, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)iccc, out[0]);

    // BBB wiederholt jetzt auch CCC -> CCC ist nicht mehr exklusiv.
    eset(m, iccc, ibbb, 3, now);
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
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", '!', NULL, false, -80, 5, 5);
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
    eset(m, iccc, ibbb, 1, now);
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
    eset(m, iaaa, 0, 1, 5);   // ich habe AAA gehoert -> Verbindung fuer die Reichweite

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
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 10);
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
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_TRUE(hits > 0);

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1AAA-1"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1BBB-2"));
    int iccc = nbrFind(m, "OE1CCC-3");
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iccc > 0);
    TEST_ASSERT_TRUE(iddd > 0);

    // Kanten: nur C->D und D->ich, sonst keine einzige Zelle gesetzt.
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iccc, iddd));
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iddd, 0));
    int nonzero = 0;
    for (int x = 0; x < NBR_MAX_ROWS; x++)
        for (int y = 0; y < NBR_MAX_ROWS; y++)
            if (ecnt(m, x, y))
                nonzero++;
    TEST_ASSERT_EQUAL_INT(2, nonzero);
}

void test_two_token_path_is_unaffected_by_the_window(void)
{
    // ntok <= 2: das Fenster ist der GANZE Pfad, das Verhalten bleibt
    // identisch zu vor der Aenderung (Regression neben test_pair_rule_text_frame).
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);

    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    TEST_ASSERT_EQUAL_INT(2, hits);

    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_TRUE(ibbb > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb));
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ibbb, 0));
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
        if (i == 0 || (rflags(m, i) & NBR_FLAG_USED))
            rows_before++;

    // Ein spaeterer 4-Token-Pfad A,B,C,D: C,D sind das Fenster, A,B liegen
    // davor. A->B ist eine Kante zwischen zwei BESTEHENDEN Zeilen -- Regel 3
    // traegt sie trotzdem ein, OHNE eine neue Zeile fuer A oder B anzulegen.
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", '!', NULL, false, -80, 5, 20);
    TEST_ASSERT_TRUE(hits > 0);

    int rows_after = 0;
    for (int i = 0; i < NBR_MAX_ROWS; i++)
        if (i == 0 || (rflags(m, i) & NBR_FLAG_USED))
            rows_after++;

    TEST_ASSERT_EQUAL_INT(rows_before + 2, rows_after);   // nur C und D sind neu
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, ibbb));   // A->B trotzdem eingetragen
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
    TEST_ASSERT_TRUE(rflags(m, iaaa) & NBR_FLAG_POS);
    TEST_ASSERT_EQUAL_FLOAT(48.0f, rview(m, iaaa).lat);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, rview(m, iaaa).lon);
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
    TEST_ASSERT_EQUAL_INT8(7, esnr(m, iccc, iddd));   // dritte Gruppe (C->D) kam an, SNR statt RSSI
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
        nbrNoteFrame(m, "OE1XXX-9,OE1YYY-8,OE1CCC-3,OE1DDD-4", '!', NULL, false, -80, 5, t);
    }
    TEST_ASSERT_TRUE((uint16_t)(t - 10) > NBR_WINDOW_MIN); // Testaufbau: Fenster sicher ueberschritten

    // X wurde in jedem dieser Frames als Zellentreffer beruehrt, aber nie
    // als Zeile verjuengt -- die Zeile muss aus der Frische gefallen sein.
    TEST_ASSERT_FALSE(nbrFresh(rview(m, ix).last_min, t));
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

// Stufe 2 (Konzept 4.6): der ME-Schritt haengt nicht mehr an der
// Pfadpruefung. Ein wegen TOK oder LOOP verworfener Frame traegt seinen
// letzten Hop weiter ein, wenn dieses Token fuer sich gueltig und nicht ich
// ist -- MHeard zaehlt solche Rahmen. Rueckgabe und DROP-Zeile bleiben.
void test_dropped_frame_still_gets_me_step(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 50);
    test_log_reset();
    nbrLog = test_log_capture;

    // TOK: "AB" ist ungueltig, der letzte Hop OE1BBB-2 nicht.
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "AB,OE1BBB-2", '@', NULL, true, -80, 5, 60));
    int ibbb = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(ibbb > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ibbb, 0));
    TEST_ASSERT_EQUAL_INT8(5, esnr(m, ibbb, 0));
    TEST_ASSERT_EQUAL_UINT16(60, rview(m, ibbb).last_min);
    TEST_ASSERT_EQUAL_UINT8(0, rflags(m, ibbb) & NBR_FLAG_GW); // kein '@'-Schritt ausser ME
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|DROP|60|TOK|AB,OE1BBB-2\n[NBR]|ME|60|OE1BBB-2|H|-80|1|5\n"));

    // LOOP: OE1CCC-3 zweimal, letzter Hop OE1CCC-3 gueltig -> nur ME.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-2, nbrNoteFrame(m, "OE1CCC-3,OE1DDD-4,OE1CCC-3", '!', NULL, false, -70, -3, 61));
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iccc > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iccc, 0));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1DDD-4"));
    TEST_ASSERT_EQUAL_INT(2, nbrEdgesUsed(m));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|DROP|61|LOOP|OE1CCC-3,OE1DDD-4,OE1CCC-3\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ME|61|OE1CCC-3|P|-70|1|-3\n"));

    // Ein ungueltiger letzter Hop und ein ungueltiger Typ bleiben folgenlos.
    NbrMatrix before;
    memcpy(&before, &m, sizeof(m));
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "OE1BBB-2,oe1xx", ':', NULL, false, -80, 5, 62));
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteFrame(m, "", ':', NULL, false, -80, 5, 62));
    TEST_ASSERT_EQUAL_INT(0, nbrNoteFrame(m, "AB,OE1BBB-2", 'X', NULL, false, -80, 5, 62));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
    TEST_ASSERT_TRUE(masks_match_pool(m));
    nbrLog = NULL;
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
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100);
    // EDGE: <rssi> ist seit der SNR-Umstellung immer 0, trailing <snr> ist
    // NA (die Paar-Zelle bekommt nie einen SNR, nur der ME-Schritt schreibt
    // in eine Zelle). ME: <rssi> ist rssi_here durchgereicht (-80), trailing
    // <snr> ist snr_here (5), das die Zelle tatsaechlich speichert.
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|EDGE|100|OE1AAA-1|OE1BBB-2|P|0|1|NA\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ME|100|OE1BBB-2|P|-80|1|5\n"));

    // CUT: derselbe Pfad um zwei weitere Hops verlaengert.
    test_log_reset();
    nbrNoteFrame(m, "OE1AAA-1,OE1BBB-2,OE1CCC-3,OE1DDD-4", '!', NULL, false, -80, 5, 200);
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
    // '!' statt ':': alle drei Aufrufe muessen denselben Typzaehler fuellen,
    // damit "zum dritten Mal" unten stimmt -- Text haette hier ohnehin keine
    // FAR-Zeile ueber das Fenster angelegt (siehe nbrNoteFrame()-Kommentar).
    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE3FAR-9,OE1AAA-1", '!', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ifar = nbrFind(m, "OE3FAR-9");
    TEST_ASSERT_TRUE(iaaa > 0 && ifar > 0);

    // Ich (Gateway) habe FARs Frame vom Server auf LoRa gesetzt, AAA hat
    // meine Aussendung wiederholt: "FAR,ich,AAA" kommt zurueck. Das Paar
    // (FAR, ich) darf NICHT "ich habe FAR gehoert" eintragen.
    int hits = nbrNoteFrame(m, "OE3FAR-9,DK5EN-93,OE1AAA-1", '!', NULL, false, -80, 5, 6);
    TEST_ASSERT_EQUAL_INT(2, hits);                                  // (ich,AAA) + ME(AAA), nicht (FAR,ich)
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, ifar, 0));            // FAR bleibt nicht-direkt
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, 0, iaaa));            // AAA hat mich gehoert
    TEST_ASSERT_EQUAL_UINT8(3, ecnt(m, iaaa, 0));            // ich habe AAA zum dritten Mal gehoert (ME-Schritt)
    TEST_ASSERT_EQUAL_STRING("NA", nbrRowMeshNeed(m, ifar, 6));      // FAR ist kein direkter Nachbar
    TEST_ASSERT_FALSE(nbrMaskTest(nbrDirectMask(m, 6), ifar));
}

void test_eviction_prefers_two_hop_row_over_older_direct_rows(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);            // direkt, aelteste
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 20);            // direkt
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 5, 30);            // direkt
    nbrNoteFrame(m, "OE1DDD-4,OE1CCC-3", '!', NULL, false, -80, 5, 40);   // DDD nur ueber CCC: 2-Hop-Zeile, juengste
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
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", '!', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1DDD-4,OE1AAA-1", '!', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_EQUAL_INT(-1, nbrRowMeshNeedCount(m, 0, 10));
    TEST_ASSERT_EQUAL_INT(-1, nbrRowMeshNeedCount(m, iccc, 10));
    TEST_ASSERT_EQUAL_INT(2, nbrRowMeshNeedCount(m, iaaa, 10));   // CCC und DDD nur ueber AAA
    TEST_ASSERT_EQUAL_INT(0, nbrRowMeshNeedCount(m, ibbb, 10));
    TEST_ASSERT_EQUAL_STRING("MESH", nbrRowMeshNeed(m, iaaa, 10));
    eset(m, iccc, ibbb, 1, 10);   // BBB hoert CCC auch
    TEST_ASSERT_EQUAL_INT(1, nbrRowMeshNeedCount(m, iaaa, 10));
    TEST_ASSERT_EQUAL_STRING("MESH", nbrRowMeshNeed(m, iaaa, 10));
}

void test_relay_need_masks_follow_concept_section_5(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    // AAA: direkt, und AAA hoert mich (hat meinen Frame wiederholt).
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", '!', NULL, false, -80, 5, 5);
    // BBB: direkt. LLL: direkt, sonst von niemandem gehoert (Blatt).
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int illl = nbrFind(m, "OE1LLL-7");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0 && illl > 0);
    const NbrMask bA = nbrMaskBit(iaaa), bB = nbrMaskBit(ibbb), bL = nbrMaskBit(illl);
    const uint16_t now = 10;

    // Frame von OE9ORG-1, letzter Hop AAA: AAA hat ihn (steht im Pfad).
    // BBB und LLL brauchen ihn; fuer beide ist niemand als Alternative
    // bekannt -> Fall A, beide "allein". sym=false: dieser Test bleibt rein
    // beobachtet, die Symmetrie-Annahme hat ihre eigenen Tests weiter unten.
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_MASK_EQ(nbrMaskOr(bB, bL), r.need);
    TEST_ASSERT_MASK_EQ(nbrMaskOr(bB, bL), r.alone);
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));

    // BBB hat AAA gehoert (cell[AAA][BBB]): BBB hat den Frame damit
    // wahrscheinlich schon (HatF) -> faellt aus dem Bedarf.
    eset(m, iaaa, ibbb, 1, now);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_MASK_EQ(bL, r.need);
    TEST_ASSERT_MASK_EQ(bL, r.alone);

    // Frame von OE9ORG-1 ueber CCC (kein Nachbar von mir, keine Zeile),
    // BBB hat CCC nicht gehoert: BBB im Bedarf; Alternative fuer BBB ist AAA
    // nur, wenn AAA den Frame hat -- AAA steht nicht im Pfad und hat CCC
    // nicht gehoert -> BBB allein. Sobald AAA CCC gehoert hat, deckt AAA BBB.
    eset(m, iaaa, ibbb, ecnt(m, iaaa, ibbb), now); // BBB hoert AAA (Deckung), s.o.
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_MASK_EQ(nbrMaskOr(bA, nbrMaskOr(bB, bL)), r.need);
    TEST_ASSERT_MASK_EQ(nbrMaskOr(bA, nbrMaskOr(bB, bL)), r.alone);
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", '!', NULL, false, -80, 5, now);   // AAA hat CCC gehoert
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iccc > 0);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_MASK_EQ(nbrMaskOr(bB, bL), r.need); // AAA hat den Frame (HatF), BBB und LLL nicht
    TEST_ASSERT_MASK_EQ(bL, r.alone);               // BBB erreicht AAA, LLL erreicht nur ich

    // Gateway-Ausnahme: ein Blatt mit GW-Flag bekommt den Frame vom Server.
    nbrRowSetFlag(m, illl, NBR_FLAG_GW);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_MASK_EQ(bB, r.need);
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.alone));           // Fall B
    m.row[illl].flags &= (uint8_t)~NBR_FLAG_GW;

    // Deckung durch eine gehoerte Wiederholung von AAA: AAAs Hoerer ist BBB
    // (cell[AAA][BBB]; dass AAA CCC gehoert hat, ist die Gegenrichtung und
    // zaehlt nicht); need & ~cover wird 0 fuer den Fall-B-Rest {BBB}. sym=false,
    // relevant=0, msg_id=0, inferred=NULL: reines Beobachtungsverhalten wie vorher.
    NbrMask cover = nbrCoverMask(m, "OE1AAA-1", now, false, nbrMaskNone(), 0, NULL);
    TEST_ASSERT_MASK_EQ(bB, cover);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAndNot(bB, cover)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrCoverMask(m, "DK5EN-93", now, false, nbrMaskNone(), 0, NULL)));   // ich selbst decke nichts
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrCoverMask(m, "OE9ZZZ-1", now, false, nbrMaskNone(), 0, NULL)));   // unbekannt

    // Ungueltiger Pfad: kein Wissen, keine Masken, known == false.
    r = nbrRelayNeed(m, "bad path!", now, false, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.need));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.alone));
    TEST_ASSERT_FALSE(r.known);
    TEST_ASSERT_EQUAL_INT(2, nbrMaskCount(nbrMaskOr(bB, bL)));

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
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.need));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.alone));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));
}

void test_exclusive_direct_ignores_two_hop_hearers(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7,OE1CCC-3", '!', NULL, false, -80, 5, 5);   // CCC (2-Hop) hoert LLL
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iaaa > 0 && illl > 0 && iccc > 0);
    // Moment: "OE1LLL-7,OE1CCC-3" macht CCC zum letzten Hop, also direkt --
    // dafuer CCCs Direktheit wieder loeschen, damit CCC ein reiner 2-Hop-Hoerer ist.
    edel(m, iccc, 0);

    uint8_t out[NBR_MAX_ROWS];
    int n_old = nbrExclusive(m, 10, out, NBR_MAX_ROWS);
    int n_new = nbrExclusiveDirect(m, 10, out, NBR_MAX_ROWS);
    TEST_ASSERT_EQUAL_INT(1, n_old);      // altes Urteil: nur AAA, LLL gilt als durch CCC gedeckt
    TEST_ASSERT_EQUAL_INT(2, n_new);      // Direktregel: AAA und LLL, CCCs Wiederholung hoere ich nie
    // AAA hoert LLL (direkte Deckung): LLL faellt aus E_self.
    eset(m, illl, iaaa, 1, 10);
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
    nbrNoteFrame(m, "OE1AAA-1,OE1PROV-1", '!', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);   // X = LLL: relayt nie
    int iaaa = nbrFind(m, "OE1AAA-1");
    int iprov = nbrFind(m, "OE1PROV-1");
    int illl = nbrFind(m, "OE1LLL-7");
    TEST_ASSERT_TRUE(iaaa > 0 && iprov > 0 && illl > 0);
    const NbrMask bL = nbrMaskBit(illl);
    const uint16_t now = 10;

    // "PROV hat LLL gehoert" (die Beobachtung fuer den ALT-Fallback): cells[LLL][PROV].
    eset(m, illl, iprov, 1, now, 6);

    test_log_reset();
    nbrLog = test_log_capture;

    // sym=false: unveraendertes Verhalten, X bleibt allein, keine Annahme.
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0x99);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));

    // sym=true: die Annahme greift, X ist nicht mehr allein.
    test_log_reset();
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0x99);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bL)));
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
    const NbrMask bL = nbrMaskBit(illl);
    const uint16_t now = 10;

    // Genau an der Schwelle (NBR_SYM_MIN_SNR == -16, Vergleich einschliesslich):
    // die Annahme greift.
    eset(m, illl, iaaa, 1, now);
    m.edge[nbrIEdgeFind(m, illl, iaaa)].snr = NBR_SYM_MIN_SNR;
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bL)));

    // Ein dB darunter: die Annahme greift NICHT mehr, X bleibt allein.
    m.edge[nbrIEdgeFind(m, illl, iaaa)].snr = (int8_t)(NBR_SYM_MIN_SNR - 1);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bL)));
}

void test_sym_observed_edge_present_skips_inference_and_log(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1LLL-7", ':', NULL, false, -80, 5, 5);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int illl = nbrFind(m, "OE1LLL-7");
    const NbrMask bL = nbrMaskBit(illl);
    const uint16_t now = 10;

    // LLL hat AAA BEOBACHTET gehoert (cell[AAA][LLL]) -- das reicht schon
    // fuer eine Alternative, unabhaengig von sym; die Annahme darf hier gar
    // nicht erst gebraucht werden.
    eset(m, iaaa, illl, 1, now);

    test_log_reset();
    nbrLog = test_log_capture;
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));   // keine Annahme, es war Beobachtung
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
    const NbrMask bL = nbrMaskBit(illl);
    const uint16_t now = 10;

    // "AAA hat LLL gehoert" ist gesetzt und frisch (ein Text-Frame ohne
    // Signalbericht), aber ohne SNR -- NBR_SNR_UNKNOWN bleibt stehen (siehe
    // nbrInit()/nbrFillUnknownSnr() und nbrHitCell(), das die Zaehler
    // erhoeht, ohne snr anzufassen). Die Annahme darf daraus NICHTS
    // folgern, auch mit sym=true nicht.
    eset(m, illl, iaaa, 1, now);
    TEST_ASSERT_EQUAL_INT8(NBR_SNR_UNKNOWN, esnr(m, illl, iaaa));

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, true, 0);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.alone, bL)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));
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
    const NbrMask bX = nbrMaskBit(ixxx);
    const uint16_t now = 10;

    // "P hat X gehoert" (Beobachtung fuer den HASF-Fallback): cells[X][P].
    eset(m, ixxx, ippp, 1, now, -5);

    test_log_reset();
    nbrLog = test_log_capture;

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, false, 0x77);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));         // sym=false: X bleibt im Bedarf
    TEST_ASSERT_TRUE(nbrMaskEmpty(r.inferred));

    test_log_reset();
    r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0x77);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));     // sym=true: die Annahme deckt X, need faellt weg
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));
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
    const NbrMask bX = nbrMaskBit(ixxx);
    const NbrMask bY = nbrMaskBit(iyyy);
    const uint16_t now = 10;

    eset(m, ixxx, ippp, 1, now, -5);  // P hat X gehoert
    eset(m, ixxx, iyyy, 1, now);                               // Y hat X gehoert

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0x55);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));      // X per HASF angenommen
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.need, bY)));
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.alone, bY)));         // X versorgt Y NICHT
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bY)));
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
    eset(m, ix, im, 1, now, -12);

    // sym=false: keine Erweiterung, *inferred bleibt 0.
    NbrMask inferred;
    memset(&inferred, 0xFF, sizeof(inferred));
    NbrMask cover = nbrCoverMask(m, "OE1MMM-1", now, false, nbrMaskNone(), 0, &inferred);
    TEST_ASSERT_FALSE(nbrMaskTest(cover, ix));
    TEST_ASSERT_TRUE(nbrMaskEmpty(inferred));

    // sym=true, relevant OHNE X: die Maske wird trotzdem erweitert (der
    // Aufrufer braucht sie fuer die eigentliche Abzugsrechnung), aber es
    // erscheint KEINE SYM-COVER-Zeile -- relevant filtert nur das Log.
    test_log_reset();
    nbrLog = test_log_capture;
    inferred = nbrMaskNone();
    cover = nbrCoverMask(m, "OE1MMM-1", now, true, nbrMaskNone(), 0xAB, &inferred);
    TEST_ASSERT_TRUE(nbrMaskTest(cover, ix));
    TEST_ASSERT_TRUE(nbrMaskTest(inferred, ix));
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));

    // sym=true, relevant MIT X: dieselbe Maske, jetzt mit SYM-COVER-Zeile.
    test_log_reset();
    inferred = nbrMaskNone();
    cover = nbrCoverMask(m, "OE1MMM-1", now, true, nbrMaskBit(ix), 0xAB, &inferred);
    TEST_ASSERT_TRUE(nbrMaskTest(cover, ix));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|SYM|10|000000AB|COVER|OE1XXX-9|OE1MMM-1|-12\n"));

    nbrLog = NULL;
}

// --- HN-Nachbarschaftsbericht (Report), Abschnitt E, src/nbr_matrix.h ------

void test_nbr_row_size_and_sym_threshold_unchanged_by_the_new_field(void)
{
    // Welle 2 (Konzept 4.1): Zeilenkern 12 Byte, Kante 6 Byte, beide ohne
    // Fuellung; das Rufzeichen liegt als eigenes 64-Bit-Wort daneben. Wird
    // hier gemessen statt nur behauptet (static_assert in nbr_matrix.cpp).
    TEST_ASSERT_EQUAL_UINT32(12, (unsigned)sizeof(NbrRow));
    TEST_ASSERT_EQUAL_UINT32(6, (unsigned)sizeof(NbrEdge));

    // NBR_SYM_MIN_SNR zieht seinen Wert jetzt von LORA_SNR_STABLE_MIN_DB
    // (configuration_default.h) statt einer eigenen Kopie der -16.
    TEST_ASSERT_EQUAL_INT(LORA_SNR_STABLE_MIN_DB, NBR_SYM_MIN_SNR);
    TEST_ASSERT_EQUAL_INT(-16, NBR_SYM_MIN_SNR);
}

// --- Builder: nbrBuildReport() ----------------------------------------------

void test_build_report_orders_by_snr_desc_tie_by_callsign(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 100);
    // Reihenfolge der nbrNoteFrame()-Aufrufe bewusst NICHT sortiert, damit
    // der Test die Sortierung der Funktion prueft, nicht die Anlagereihenfolge.
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 100);    // SNR 5
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 100);    // SNR 5, Gleichstand mit BBB
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 9, 100);    // SNR 9, hoechster
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, -10, 100);  // SNR -10, niedrigster (noch >= -16)

    char buf[160];
    int n = nbrBuildReport(m, 100, 12, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    // CCC (9) > AAA/BBB (5, Gleichstand: AAA vor BBB alphabetisch) > DDD (-10).
    TEST_ASSERT_EQUAL_STRING("R12;N4;OE1CCC-3,9;OE1AAA-1,5;OE1BBB-2,5;OE1DDD-4,-10;", buf);
}

void test_build_report_threshold_inclusive_at_sym_min_snr_and_excluded_below(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, (int8_t)NBR_SYM_MIN_SNR, 0);        // genau an der Schwelle: drin
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, (int8_t)(NBR_SYM_MIN_SNR - 1), 0);  // 1 dB drunter: raus

    char buf[160];
    int n = nbrBuildReport(m, 0, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "OE1AAA-1"));
    TEST_ASSERT_NULL(strstr(buf, "OE1BBB-2"));
}

void test_build_report_freshness_cut_at_60_min(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 100);

    char buf[160];
    // 59 min alt (NBR_REPORT_FRESH_MIN == 60): noch frisch.
    int n = nbrBuildReport(m, 159, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "OE1AAA-1"));

    // genau 60 min alt: die Grenze ist exklusiv, wie bei nbrFresh()/NBR_WINDOW_MIN.
    n = nbrBuildReport(m, 160, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "OE1AAA-1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "N0;"));
}

void test_build_report_unknown_snr_excluded(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 0);
    int iaaa = nbrFind(m, "OE1AAA-1");
    // Zelle bleibt "gesetzt" (Zaehler > 0), SNR wird gezielt auf unbekannt
    // zurueckgesetzt -- eine Zelle mit Beobachtung, aber ohne Signalbericht.
    m.edge[nbrIEdgeFind(m, iaaa, 0)].snr = NBR_SNR_UNKNOWN;

    char buf[160];
    int n = nbrBuildReport(m, 0, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "OE1AAA-1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "N0;"));
}

void test_build_report_n0_is_a_valid_empty_list(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    char buf[160];
    int n = nbrBuildReport(m, 0, 3, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("R3;N0;", buf);
}

void test_build_report_buffer_too_small_returns_minus1(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 0);

    char buf[8]; // "R1;N1;OE1AAA-1,5;" braucht 18+1 Byte, passt nicht
    int n = nbrBuildReport(m, 0, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, n);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// NBR_MAX_ROWS=5 in dieser Umgebung erlaubt hoechstens NBR_MAX_ROWS-1 = 4
// gleichzeitig direkt gehoerte Nachbarn. Die Kappung bei
// NBR_REPORT_MAX_ENTRIES == 8 ("+" bei mehr Kandidaten als Eintraegen, kein
// "+" bei genau 8) kann darum in DIESER Testumgebung nicht end-to-end mit
// echten Zeilen durchexerziert werden -- das braeuchte
// NBR_MAX_ROWS >= 9 (bzw. 10 fuer den >8-Fall), eine platformio.ini-Aenderung
// ausserhalb des Dateisatzes dieses Wave-Agenten (siehe dessen
// Abschluss-Report). Dieser Test haelt fest, was OHNE diese Grenze schon
// geprueft werden kann: die Konstante selbst, und dass unterhalb des Limits
// nie abgeschnitten wird.
void test_build_report_cap_constant_and_no_truncation_below_it(void)
{
    TEST_ASSERT_EQUAL_INT(8, NBR_REPORT_MAX_ENTRIES);

    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 0);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 4, 0);
    nbrNoteFrame(m, "OE1CCC-3", ':', NULL, false, -80, 3, 0);
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 2, 0); // NBR_MAX_ROWS-1: die Obergrenze dieser Umgebung

    char buf[160];
    int n = nbrBuildReport(m, 0, 4, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "N4;"));
    TEST_ASSERT_NULL(strstr(buf, "+"));
}

// --- Empfaenger: nbrNoteReport() --------------------------------------------

void test_note_report_applies_full_spec_example(void)
{
    // Eigenes Rufzeichen ist absichtlich einer der Bericht-Eintraege
    // (DK5EN-98), um "self" im selben Aufruf wie "ok" und "norow" zu pruefen.
    NbrMatrix m;
    nbrInit(m, "DK5EN-98", 0);
    nbrNoteFrame(m, "OE1SEND-1", ':', NULL, false, -80, 5, 0);   // Absender, braucht eine Zeile
    nbrNoteFrame(m, "DL2JA-2", ':', NULL, false, -80, 3, 0);     // bekannter Nachbar -> "ok"
    int isend = nbrFind(m, "OE1SEND-1");
    int idja = nbrFind(m, "DL2JA-2");
    TEST_ASSERT_TRUE(isend > 0 && idja > 0);

    test_log_reset();
    nbrLog = test_log_capture;

    int applied = nbrNoteReport(m, "OE1SEND-1",
        "R12;N5;DL2JA-2,7;DB0ISM-1,5;DK5EN-98,-8;DB0ED-99,-11;DL2UD-1,-12;", 50);
    TEST_ASSERT_EQUAL_INT(2, applied);   // DL2JA-2 (ok) + DK5EN-98 (self); die drei uebrigen sind norow

    TEST_ASSERT_EQUAL_INT8(7, esnr(m, idja, isend));    // "OE1SEND-1 hat DL2JA-2 gehoert"
    TEST_ASSERT_EQUAL_INT8(-8, esnr(m, 0, isend));      // "OE1SEND-1 hat mich gehoert"
    TEST_ASSERT_TRUE(rflags(m, isend) & NBR_FLAG_RPT);   // kein '+' -> vollstaendig
    TEST_ASSERT_EQUAL_UINT16(50, rview(m, isend).rpt_min);

    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPT|50|OE1SEND-1|DL2JA-2|7|ok\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPT|50|OE1SEND-1|DB0ISM-1|5|norow\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPT|50|OE1SEND-1|DK5EN-98|-8|self\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPT|50|OE1SEND-1|DB0ED-99|-11|norow\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPT|50|OE1SEND-1|DL2UD-1|-12|norow\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|RPTSUM|50|OE1SEND-1|12|5|1|2\n"));

    nbrLog = NULL;
}

void test_note_report_malformed_variants_are_rejected_wholesale(void)
{
    NbrMatrix m, before;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1SEND-1", ':', NULL, false, -80, 5, 0);
    memcpy(&before, &m, sizeof(m));

    test_log_reset();
    nbrLog = test_log_capture;

    // k-Diskrepanz: Header behauptet N2, nur ein Eintrag folgt.
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N2;OE1AAA-1,5;", 10));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|DROP|10|RPT|OE1SEND-1\n"));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // k-Diskrepanz umgekehrt: Header behauptet N1, ein zweiter Eintrag folgt trotzdem.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N1;OE1AAA-1,5;OE1BBB-2,3;", 10));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // k > NBR_REPORT_MAX_ENTRIES (8).
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N9;", 10));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // fehlendes Komma im Eintrag.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N1;OE1AAA-1 5;", 10));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // nicht-numerischer SNR.
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N1;OE1AAA-1,x;", 10));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    // ueberlanges Rufzeichen (>= NBR_CALL_LEN Zeichen).
    test_log_reset();
    TEST_ASSERT_EQUAL_INT(-1, nbrNoteReport(m, "OE1SEND-1", "R1;N1;TOOLONGCALL1,5;", 10));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));

    nbrLog = NULL;
}

void test_note_report_sender_without_row_returns_zero(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    int applied = nbrNoteReport(m, "OE1UNKNOWN-9", "R1;N1;OE1AAA-1,5;", 10);
    TEST_ASSERT_EQUAL_INT(0, applied);
}

// --- Symmetrie-Veto durch einen gueltigen HN-Bericht ------------------------

void test_sym_veto_blocks_inference_when_report_is_complete_and_fresh(void)
{
    // Regression: derselbe Aufbau wie test_sym_hasf_fallback_removes_dependent_from_need,
    // ZUSAETZLICH mit einem frischen, vollstaendigen HN-Bericht auf X, der P
    // NICHT enthielt. Ohne die VETO-Pruefung in nbrHearsSym() wuerde die
    // Annahme weiterhin ziehen: TEST_ASSERT_EQUAL_UINT32(0, r.inferred & bX)
    // ist genau die Assertion, die gegen den Code VOR diesem Fix fehlschlaegt
    // (r.inferred wuerde bX enthalten, r.need nicht).
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    TEST_ASSERT_TRUE(ippp > 0 && ixxx > 0);
    const NbrMask bX = nbrMaskBit(ixxx);
    const uint16_t now = 100;

    // "P hat X gehoert" (die Beobachtung, die sonst den HASF-Fallback traegt).
    eset(m, ixxx, ippp, 1, now, -5);

    // X hat vor 10 min einen VOLLSTAENDIGEN HN-Bericht gesendet, der P nicht
    // enthielt (kein cells[ippp][ixxx] gesetzt -- sonst waere es der direkte
    // Beobachtungszweig, keine Annahme noetig).
    nbrRowSetFlag(m, ixxx, NBR_FLAG_RPT);
    m.row[ixxx].rpt_min = (uint16_t)(now - 10);

    test_log_reset();
    nbrLog = test_log_capture;

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0x42);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));          // X bleibt im Bedarf: keine Annahme
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));  // <-- schlaegt ohne den Fix fehl
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|SYM|100|00000042|VETO|OE1XXX-9|OE1PPP-1|-5\n"));
    TEST_ASSERT_NULL(strstr(g_log_buf, "|HASF|"));

    nbrLog = NULL;
}

void test_sym_veto_does_not_apply_to_a_truncated_report(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    const NbrMask bX = nbrMaskBit(ixxx);
    const uint16_t now = 100;

    eset(m, ixxx, ippp, 1, now, -5);

    // Bericht war ABGESCHNITTEN ('+'): NBR_FLAG_RPT bewusst NICHT gesetzt.
    m.row[ixxx].rpt_min = (uint16_t)(now - 10);

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));
}

void test_sym_veto_expires_after_valid_window(void)
{
    NbrMatrix m;
    const uint16_t now = 1000;
    nbrInit(m, "DK5EN-93", 0);
    // Zeilen bei "now" anlegen, nicht bei einer weit zurueckliegenden Minute
    // -- sonst faellt cells[X][0] schon vor dem eigentlichen Testfenster aus
    // dem allgemeinen NBR_WINDOW_MIN (720 min), und X ist gar nicht mehr
    // "direkt", unabhaengig vom Report-Veto.
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, now);
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, now);
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    const NbrMask bX = nbrMaskBit(ixxx);

    eset(m, ixxx, ippp, 1, now, -5);
    nbrRowSetFlag(m, ixxx, NBR_FLAG_RPT);

    // Genau innerhalb der Grenze (NBR_REPORT_VALID_MIN == 45): Veto greift noch.
    m.row[ixxx].rpt_min = (uint16_t)(now - (NBR_REPORT_VALID_MIN - 1));
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0);
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));

    // Genau NBR_REPORT_VALID_MIN alt: Grenze exklusiv (ueberlaufsicherer
    // Vergleich wie nbrFresh()) -- die Annahme darf wieder greifen.
    m.row[ixxx].rpt_min = (uint16_t)(now - NBR_REPORT_VALID_MIN);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));
    TEST_ASSERT_FALSE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));
}

void test_sym_veto_report_listing_m_is_observed_not_inferred(void)
{
    // X meldet P direkt in seinem HN-Bericht -> cells[P][X] wird zur echten
    // Beobachtung (dieselbe Zelle, die nbrHearsSym() als "observed" prueft);
    // der Rueckschluss wird dafuer gar nicht erst gebraucht, kein SYM-Log.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1PPP-1", ':', NULL, false, -80, 5, 5);
    nbrNoteFrame(m, "OE1XXX-9", ':', NULL, false, -80, 5, 5);
    int ippp = nbrFind(m, "OE1PPP-1");
    int ixxx = nbrFind(m, "OE1XXX-9");
    TEST_ASSERT_TRUE(ippp > 0 && ixxx > 0);
    const NbrMask bX = nbrMaskBit(ixxx);
    const uint16_t now = 100;

    int applied = nbrNoteReport(m, "OE1XXX-9", "R1;N1;OE1PPP-1,6;", now);
    TEST_ASSERT_EQUAL_INT(1, applied);
    TEST_ASSERT_TRUE(rflags(m, ixxx) & NBR_FLAG_RPT);

    test_log_reset();
    nbrLog = test_log_capture;

    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1PPP-1", now, true, 0);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.need, bX)));      // X gilt als versorgt: HatF via echter Beobachtung
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrMaskAnd(r.inferred, bX)));  // keine Annahme -- es war Beobachtung
    TEST_ASSERT_NULL(strstr(g_log_buf, "|SYM|"));  // weder HASF/ALT noch VETO

    nbrLog = NULL;
}

// --- Rundlauf: Baustein und Empfaenger auf zwei getrennten Matrizen --------

void test_report_round_trip_build_then_note(void)
{
    // Sende-Knoten A hoert direkt zwei Nachbarn mit unterschiedlichem SNR.
    NbrMatrix a;
    nbrInit(a, "OE1SEND-1", 0);
    nbrNoteFrame(a, "OE1AAA-1", ':', NULL, false, -80, 7, 0);
    nbrNoteFrame(a, "OE1BBB-2", ':', NULL, false, -80, -3, 0);

    char payload[160];
    int n = nbrBuildReport(a, 0, 2, payload, sizeof(payload));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("R2;N2;OE1AAA-1,7;OE1BBB-2,-3;", payload);

    // Empfangs-Knoten B kennt den Absender UND beide gemeldeten Rufzeichen.
    NbrMatrix b;
    nbrInit(b, "DK5EN-93", 0);
    nbrNoteFrame(b, "OE1SEND-1", ':', NULL, false, -80, 4, 100);
    nbrNoteFrame(b, "OE1AAA-1", ':', NULL, false, -80, 1, 100);
    nbrNoteFrame(b, "OE1BBB-2", ':', NULL, false, -80, 1, 100);
    int isend = nbrFind(b, "OE1SEND-1");
    int iaaa = nbrFind(b, "OE1AAA-1");
    int ibbb = nbrFind(b, "OE1BBB-2");
    TEST_ASSERT_TRUE(isend > 0 && iaaa > 0 && ibbb > 0);

    int applied = nbrNoteReport(b, "OE1SEND-1", payload, 100);
    TEST_ASSERT_EQUAL_INT(2, applied);
    TEST_ASSERT_EQUAL_INT8(7, esnr(b, iaaa, isend));
    TEST_ASSERT_EQUAL_INT8(-3, esnr(b, ibbb, isend));
    TEST_ASSERT_TRUE(rflags(b, isend) & NBR_FLAG_RPT);
}

// --- 15: Text nimmt keinen Pfadpaar-Pfad mehr (Gateway-Einspeisung) --------
//
// Feldlog DK5EN-98, 22.-23.09.2026, 34h: der Server haengt seinen eigenen
// Pfad vor den einspeisenden Gateway-Namen ("<Server-Pfad>,<Gateway>");
// das Paar (letztes Server-Token, Gateway) war nie ein Funkempfang. Text
// (':') liefert seither NUR noch den ME-Schritt (siehe nbrNoteFrame()-
// Kommentar); '!'/'@' bleiben unveraendert.

void test_text_frame_creates_only_me_row_not_the_injecting_path(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    int hits = nbrNoteFrame(m, "OE1XAR-33,DL2JA-2", ':', NULL, false, -80, 5, 10);
    TEST_ASSERT_EQUAL_INT(1, hits);   // nur ME(DL2JA-2), keine Kante

    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE1XAR-33"));   // Server-Token bekommt keine Zeile
    int idja = nbrFind(m, "DL2JA-2");
    TEST_ASSERT_TRUE(idja > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, idja, 0));
}

void test_pos_frame_same_path_still_creates_the_pair_edge(void)
{
    // Gegenprobe: derselbe Pfad bleibt fuer '!' unveraendert -- die
    // Pfadpaar-Kante entsteht weiterhin, POS ist von der Text-Ausnahme
    // nicht betroffen.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    int hits = nbrNoteFrame(m, "OE1XAR-33,DL2JA-2", '!', NULL, false, -80, 5, 10);
    TEST_ASSERT_EQUAL_INT(2, hits);   // Kante + ME

    int ixar = nbrFind(m, "OE1XAR-33");
    int idja = nbrFind(m, "DL2JA-2");
    TEST_ASSERT_TRUE(ixar > 0);
    TEST_ASSERT_TRUE(idja > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ixar, idja));
}

void test_text_frame_with_three_tokens_touches_only_the_last_hop(void)
{
    // A und GW existieren schon (ueber POS-Frames angelegt), mit einer
    // fruehen last_min. Ein spaeterer 3-Token-Text-Frame "A,GW,B" darf
    // weder die Fenster-Kante (GW,B) noch die Regel-3-Gratis-Kante (A,GW)
    // eintragen -- unter der alten Regel haette (A,GW) getroffen, weil
    // beide Enden schon Zeilen hatten. Nur B (letzter Hop) bekommt den
    // ME-Treffer; A und GW duerfen dabei nicht verjuengt werden.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);

    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1GWY-1", '!', NULL, false, -80, 5, 10);
    int ia = nbrFind(m, "OE1AAA-1");
    int igw = nbrFind(m, "OE1GWY-1");
    TEST_ASSERT_TRUE(ia > 0 && igw > 0);

    g_log_buf[0] = '\0';
    nbrLog = test_log_capture;
    int hits = nbrNoteFrame(m, "OE1AAA-1,OE1GWY-1,OE1BBB-2", ':', NULL, false, -80, 5, 200);
    nbrLog = NULL;
    TEST_ASSERT_EQUAL_INT(1, hits);   // nur ME(B)
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "[NBR]|ME|200|OE1BBB-2|T|-80|1|5\n"));
    TEST_ASSERT_NULL(strstr(g_log_buf, "|CUT|"));    // Text kennt kein 2-Hop-Fenster
    TEST_ASSERT_NULL(strstr(g_log_buf, "|EDGE|"));   // und keine Pfadkante

    int ib = nbrFind(m, "OE1BBB-2");
    TEST_ASSERT_TRUE(ib > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ib, 0));

    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, ia, igw));   // keine Regel-3-Gratis-Kante
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, igw, ib));   // keine Fenster-Kante

    TEST_ASSERT_EQUAL_UINT16(10, rview(m, ia).last_min);       // A nicht verjuengt
    TEST_ASSERT_EQUAL_UINT16(10, rview(m, igw).last_min);      // GW nicht verjuengt
}

void test_text_frame_with_own_call_as_last_hop_is_still_a_pure_echo(void)
{
    // Pin: das eigene Echo bleibt fuer Text folgenlos, auch nach der
    // Text-Sonderregel (unveraenderte Erwartung, siehe
    // test_own_frame_returning_via_relay_is_echo_not_direct_hearing).
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, 5, 10);
    TEST_ASSERT_TRUE(nbrFind(m, "OE1AAA-1") > 0);

    NbrMatrix before;
    memcpy(&before, &m, sizeof(m));

    int hits = nbrNoteFrame(m, "OE1AAA-1,DK5EN-93", ':', NULL, false, -80, 5, 20);
    TEST_ASSERT_EQUAL_INT(0, hits);
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &m, sizeof(m)));
}

void test_text_echo_of_my_own_frame_marks_no_hears_me_edge(void)
{
    // Bewusster Verlust (nbr_matrix.h): "<ich>,X" als Text traegt nur
    // "ich habe X gehoert" ein, nicht "X hoert mich" -- ein anderes Gateway
    // kann meinen hochgeladenen Text genauso als "<ich>,<Gateway>" senden.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    int hits = nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", ':', NULL, false, -70, 5, 200);
    TEST_ASSERT_EQUAL_INT(1, hits);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, 0));   // ich habe AAA gehoert
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, 0, iaaa));   // keine "AAA hoert mich"-Kante aus Text
}

void test_gateway_echo_text_gives_far_origin_no_row(void)
{
    // Text-Zwilling von test_gateway_echo_does_not_mark_server_origin_as_directly_heard:
    // der Feldfall, der die Text-Regel ausgeloest hat. Ich (Gateway) setze
    // FARs Server-Text auf LoRa, AAA wiederholt: "FAR,ich,AAA". FAR bekommt
    // keine Zeile, nur AAA den ME-Treffer.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, 5, 5);
    int hits = nbrNoteFrame(m, "OE3FAR-9,DK5EN-93,OE1AAA-1", ':', NULL, false, -80, 5, 6);
    TEST_ASSERT_EQUAL_INT(1, hits);
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "OE3FAR-9"));
    int iaaa = nbrFind(m, "OE1AAA-1");
    // Welle 2: ein Zaehler ueber alle Typen -- der POS-Treffer von oben plus
    // dieser ME-Treffer (frueher cnt_text == 1 neben cnt_pos == 1).
    TEST_ASSERT_EQUAL_UINT8(2, ecnt(m, iaaa, 0));
    TEST_ASSERT_EQUAL_UINT8(0, ecnt(m, 0, iaaa));
}

// --- Welle 2: Kantenpool, Rufzeichenwort, Anteilsregel, Halbierung, SNR ----

void test_call_word_round_trip_and_invalid_calls(void)
{
    const char *ok[] = {"DK5EN-98", "OE1XAR-62", "ABC", "DB0ED-99", "9A1-0"};
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
    {
        uint64_t w = nbrCallEncode(ok[i]);
        TEST_ASSERT_TRUE(w != 0);
        char back[NBR_CALL_LEN];
        nbrCallDecode(w, back);
        TEST_ASSERT_EQUAL_STRING(ok[i], back);
    }
    // Zeichen 1 in Bit 0..5, 'A' = 11, '0' = 1, '-' = 37.
    uint64_t w = nbrCallEncode("A0-");
    TEST_ASSERT_EQUAL_UINT32(11u, (unsigned)(w & 63u));
    TEST_ASSERT_EQUAL_UINT32(1u, (unsigned)((w >> 6) & 63u));
    TEST_ASSERT_EQUAL_UINT32(37u, (unsigned)((w >> 12) & 63u));
    TEST_ASSERT_EQUAL_UINT32(0u, (unsigned)(w >> 18));

    TEST_ASSERT_TRUE(nbrCallEncode("AB") == 0);            // 2 Zeichen
    TEST_ASSERT_TRUE(nbrCallEncode("ABCDEFGHIJ") == 0);    // 10 Zeichen
    TEST_ASSERT_TRUE(nbrCallEncode("DK5EN-98X9") == 0);    // 10 Zeichen
    TEST_ASSERT_TRUE(nbrCallEncode("dk5en-98") == 0);      // Kleinbuchstaben
    TEST_ASSERT_TRUE(nbrCallEncode("DK5EN/P") == 0);       // '/'
    TEST_ASSERT_TRUE(nbrCallEncode("") == 0);
    TEST_ASSERT_TRUE(nbrCallEncode(NULL) == 0);
    char empty[NBR_CALL_LEN];
    nbrCallDecode(0, empty);
    TEST_ASSERT_EQUAL_STRING("", empty);

    // Die Suche ist ein Wortvergleich: ungueltige Rufzeichen finden nichts.
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    TEST_ASSERT_EQUAL_INT(0, nbrFind(m, "DK5EN-93"));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(m, "dk5en-93"));
    TEST_ASSERT_TRUE(nbrOwnCallIs(m, "DK5EN-93"));
    TEST_ASSERT_FALSE(nbrOwnCallIs(m, "DK5EN-94"));
}

void test_mask_hex_is_fixed_width_most_significant_first(void)
{
    NbrMask mk = nbrMaskNone();
    nbrMaskSet(mk, 1);
    nbrMaskSet(mk, 4);
    char buf[NBR_MASK_HEX_LEN + 1];
    TEST_ASSERT_EQUAL_INT(NBR_MASK_HEX_LEN, nbrMaskHex(mk, buf, sizeof(buf)));
    char expect[NBR_MASK_HEX_LEN + 1];
    memset(expect, '0', NBR_MASK_HEX_LEN);
    expect[NBR_MASK_HEX_LEN] = '\0';
    expect[NBR_MASK_HEX_LEN - 1] = '2';
    expect[NBR_MASK_HEX_LEN - 2] = '1';
    TEST_ASSERT_EQUAL_STRING(expect, buf);
}

// Nie initialisierte Matrix (BSS, alles 0): jede Funktion sieht "leer" --
// kein Eintrag (0,0) gilt als Kante, der Sweep tut nichts, nichts wird
// geschrieben (Orchestrator-Hinweis Welle 2: Sweep und Leser laufen ab Boot,
// nbrInit() erst beim ersten Empfang).
void test_never_initialised_matrix_reads_as_empty(void)
{
    static NbrMatrix z;
    memset(&z, 0, sizeof(z));
    static NbrMatrix before;
    memcpy(&before, &z, sizeof(z));

    test_log_reset();
    nbrLog = test_log_capture;

    nbrSweep(z, 100);
    nbrSweep(z, 900);
    NbrRowView v;
    TEST_ASSERT_FALSE(nbrRowGet(z, 0, &v));
    TEST_ASSERT_FALSE(nbrRowGet(z, 1, &v));
    TEST_ASSERT_EQUAL_INT(1, nbrRowsUsed(z));
    TEST_ASSERT_EQUAL_INT(0, nbrEdgesUsed(z));
    NbrEdgeView ev;
    TEST_ASSERT_FALSE(nbrEdgeGet(z, 0, 0, &ev));
    TEST_ASSERT_FALSE(nbrEdgeGet(z, 1, 0, &ev));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrDirectMask(z, 100)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrHeardMeMask(z, 100)));
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrHearersMask(z, 0, 100)));
    TEST_ASSERT_EQUAL_UINT8(0, nbrHearers(z, 0, 100, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, nbrExclusive(z, 100, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, nbrExclusiveDirect(z, 100, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, nbrFind(z, "DK5EN-93"));
    TEST_ASSERT_FALSE(nbrOwnCallIs(z, "DK5EN-93"));
    NbrNeed r = nbrRelayNeed(z, "OE1AAA-1,OE1BBB-2", 100, true, 1);
    TEST_ASSERT_FALSE(r.known);
    TEST_ASSERT_TRUE(nbrMaskEmpty(nbrCoverMask(z, "OE1AAA-1", 100, true, nbrMaskNone(), 1, NULL)));
    char buf[160];
    TEST_ASSERT_EQUAL_INT(0, nbrFormatRow(z, 0, 100, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(nbrBuildReport(z, 100, 0, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("R0;N0;", buf);
    TEST_ASSERT_EQUAL_INT(0, nbrNoteFrame(z, "OE1AAA-1,OE1BBB-2", '!', NULL, false, -80, 5, 100));
    nbrNotePos(z, "OE1AAA-1", 48.0f, 11.0f, true, 1, 100);
    TEST_ASSERT_EQUAL_INT(0, nbrNoteReport(z, "OE1AAA-1", "R1;N0;", 100));

    nbrLogSnapshot(z, 100);
    char expect[64];
    snprintf(expect, sizeof(expect), "[NBR]|SNAP|100||1|%d|0\n", (int)NBR_MAX_ROWS);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, expect));
    TEST_ASSERT_NULL(strstr(g_log_buf, "EVICT"));
    TEST_ASSERT_NULL(strstr(g_log_buf, "|EDGE|"));
    nbrLog = NULL;

    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &z, sizeof(z)));   // nichts geschrieben
}

void test_row_eviction_frees_edges_and_clears_mask_bits(void)
{
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, 5, 10);            // aelteste
    nbrNoteFrame(m, "OE1BBB-2,OE1AAA-1", '!', NULL, false, -80, 5, 11);   // Kante (BBB, AAA)
    nbrNoteFrame(m, "OE1AAA-1,OE1CCC-3", '!', NULL, false, -80, 5, 12);   // Kante (AAA, CCC)
    nbrNoteFrame(m, "DK5EN-93,OE1DDD-4", '!', NULL, false, -80, 5, 13);   // Kante (0, DDD)
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(ecnt(m, ibbb, iaaa) > 0 && ecnt(m, iaaa, iccc) > 0 && ecnt(m, iaaa, 0) > 0);
    TEST_ASSERT_TRUE(nbrMaskTest(m.heardBy[ibbb], iaaa));
    TEST_ASSERT_TRUE(nbrMaskTest(m.hears[iccc], iaaa));
    int before = nbrEdgesUsed(m);

    // Alle vier direkt gehoert, darum waehlt erst der zweite Durchgang: die
    // aelteste Zeile weicht. AAA ist hier juenger als BBB -- das Opfer ist
    // die Zeile mit der groessten Altersluecke.
    nbrNoteFrame(m, "OE1EEE-5", ':', NULL, false, -80, 5, 20);
    int ieee = nbrFind(m, "OE1EEE-5");
    TEST_ASSERT_TRUE(ieee > 0);
    int victim = ieee;
    // Keine Kante beruehrt mehr den alten Inhaber; seine Bits sind weg.
    for (int r = 0; r < NBR_MAX_ROWS; r++)
    {
        if (r == 0)
            continue;   // (EEE, 0) ist die neue ME-Kante
        TEST_ASSERT_FALSE(nbrMaskTest(m.hears[r], victim));
        TEST_ASSERT_FALSE(nbrMaskTest(m.heardBy[victim], r));
        TEST_ASSERT_FALSE(nbrMaskTest(m.heardBy[r], victim));
        TEST_ASSERT_FALSE(nbrMaskTest(m.hears[victim], r));
    }
    TEST_ASSERT_TRUE(masks_match_pool(m));
    TEST_ASSERT_TRUE(nbrEdgesUsed(m) < before + 1);
}

// Anteilsregel (nbr_matrix.h, Konzept 4.3): "M deckt x" erst ab
// NBR_SHARE_PCT Prozent des staerksten direkten Hoerers von x, Grenze
// einschliesslich. In dieser Umgebung gilt der Produktionswert 10.
void test_share_rule_one_hit_against_fifty_does_not_cover(void)
{
#if NBR_SHARE_PCT == 10
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    const uint16_t now = 10;
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, now);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, now);
    nbrNoteFrame(m, "OE1CCC-3,OE1AAA-1", '!', NULL, false, -80, 5, now);   // CCC: 2-Hop ueber AAA
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    int iccc = nbrFind(m, "OE1CCC-3");
    TEST_ASSERT_TRUE(iaaa > 0 && ibbb > 0 && iccc > 0);

    eset(m, iccc, iaaa, 50, now);   // AAA hoert CCC 50-mal
    eset(m, iccc, ibbb, 1, now);    // BBB hoert CCC einmal (Streutreffer)
    NbrMask set;
    TEST_ASSERT_EQUAL_INT(1, nbrRowMeshNeedCount(m, iaaa, now));   // CCC gehoert AAA allein
    TEST_ASSERT_EQUAL_INT(1, nbrRowMeshNeedSet(m, iaaa, now, &set));
    TEST_ASSERT_MASK_EQ(nbrMaskBit(iccc), set);
    TEST_ASSERT_EQUAL_INT(0, nbrRowMeshNeedCount(m, ibbb, now));   // BBBs 1 von 50 ist keine Deckung

    eset(m, iccc, ibbb, 4, now);    // 4 von 50 = 8 %: noch keine Deckung
    TEST_ASSERT_EQUAL_INT(1, nbrRowMeshNeedCount(m, iaaa, now));
    eset(m, iccc, ibbb, 5, now);    // 5 von 50 = 10 %: Grenze einschliesslich
    TEST_ASSERT_EQUAL_INT(0, nbrRowMeshNeedCount(m, iaaa, now));
    TEST_ASSERT_EQUAL_INT(0, nbrRowMeshNeedCount(m, ibbb, now));   // BBB deckt CCC jetzt, AAA aber auch

    // Deckung durch eine gehoerte Wiederholung von AAA: Kante (AAA, Y) = "Y
    // hat AAA gehoert", normiert auf AAAs staerksten direkten Hoerer.
    nbrNoteFrame(m, "OE1DDD-4", ':', NULL, false, -80, 5, now);
    int iddd = nbrFind(m, "OE1DDD-4");
    TEST_ASSERT_TRUE(iddd > 0);
    eset(m, iaaa, ibbb, 50, now);
    eset(m, iaaa, iddd, 1, now);
    NbrMask cover = nbrCoverMask(m, "OE1AAA-1", now, false, nbrMaskNone(), 0, NULL);
    TEST_ASSERT_MASK_EQ(nbrMaskBit(ibbb), cover);
    // Allein-Maske: Frame ueber CCC. AAA hat CCC gehoert (HatF) und ist
    // damit Versorger; DDD braucht den Frame, hoert AAA aber nur 1-mal gegen
    // AAAs staerksten direkten Hoerer BBB mit 50 -> keine Alternative, DDD
    // bleibt allein (Fall A).
    NbrNeed r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_TRUE(nbrMaskTest(r.need, iddd));
    TEST_ASSERT_TRUE(nbrMaskTest(r.alone, iddd));
    eset(m, iaaa, iddd, 5, now);    // 10 %: AAA versorgt DDD
    cover = nbrCoverMask(m, "OE1AAA-1", now, false, nbrMaskNone(), 0, NULL);
    TEST_ASSERT_MASK_EQ(mbits(ibbb, iddd), cover);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1CCC-3", now, false, 0);
    TEST_ASSERT_TRUE(nbrMaskTest(r.need, iddd));
    TEST_ASSERT_FALSE(nbrMaskTest(r.alone, iddd));
    // HatF bleibt die blosse Kante: mit AAA im Pfad hat DDD den Frame schon
    // mit einem einzigen Treffer (need, nicht Anteil).
    eset(m, iaaa, iddd, 1, now);
    r = nbrRelayNeed(m, "OE9ORG-1,OE1AAA-1", now, false, 0);
    TEST_ASSERT_FALSE(nbrMaskTest(r.need, iddd));
#else
    TEST_IGNORE_MESSAGE("NBR_SHARE_PCT != 10 in dieser Umgebung");
#endif
}

void test_counter_halving_in_sweep(void)
{
#if NBR_CNT_HALVE_MIN == 90
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 5, 10);
    nbrNoteFrame(m, "OE1BBB-2", ':', NULL, false, -80, 5, 10);
    int iaaa = nbrFind(m, "OE1AAA-1");
    int ibbb = nbrFind(m, "OE1BBB-2");
    eset(m, iaaa, ibbb, 255, 80);
    eset(m, ibbb, iaaa, 3, 80);
    eset(m, iaaa, 0, 1, 80);
    eset(m, ibbb, 0, 2, 80);

    nbrSweep(m, 89);   // 89 min seit nbrInit(): noch nicht
    TEST_ASSERT_EQUAL_UINT8(255, ecnt(m, iaaa, ibbb));
    nbrSweep(m, 90);
    TEST_ASSERT_EQUAL_UINT8(128, ecnt(m, iaaa, ibbb));   // (255+1)/2
    TEST_ASSERT_EQUAL_UINT8(2, ecnt(m, ibbb, iaaa));     // (3+1)/2
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, iaaa, 0));        // faellt nie auf 0
    TEST_ASSERT_EQUAL_UINT8(1, ecnt(m, ibbb, 0));
    nbrSweep(m, 90);   // zweiter Aufruf in derselben Minute: nichts
    TEST_ASSERT_EQUAL_UINT8(128, ecnt(m, iaaa, ibbb));
    nbrSweep(m, 179);
    TEST_ASSERT_EQUAL_UINT8(128, ecnt(m, iaaa, ibbb));
    nbrSweep(m, 180);
    TEST_ASSERT_EQUAL_UINT8(64, ecnt(m, iaaa, ibbb));
    // Der Sweep gibt ausserdem Kanten ueber NBR_WINDOW_MIN frei.
    nbrSweep(m, 80 + NBR_WINDOW_MIN);
    TEST_ASSERT_EQUAL_INT(0, nbrEdgesUsed(m));
    TEST_ASSERT_TRUE(masks_match_pool(m));
#else
    TEST_IGNORE_MESSAGE("NBR_CNT_HALVE_MIN != 90 in dieser Umgebung");
#endif
}

void test_me_snr_is_a_running_mean(void)
{
#if NBR_SNR_AVG_N == 8
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 0);
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 0, 10);   // erster Wert: der Wert
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_EQUAL_INT8(0, esnr(m, iaaa, 0));
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 8, 10);   // n = 2: (0 + 8) / 2
    TEST_ASSERT_EQUAL_INT8(4, esnr(m, iaaa, 0));
    nbrNoteFrame(m, "OE1AAA-1", '!', NULL, false, -80, -8, 11);  // n = 3: 4 - 12/3
    TEST_ASSERT_EQUAL_INT8(0, esnr(m, iaaa, 0));
    for (int i = 0; i < 30; i++)
        nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, 10, 12);
    TEST_ASSERT_EQUAL_INT8(10, esnr(m, iaaa, 0));                  // ein gleichbleibender Wert wird erreicht
    // Nicht (x, 0): der zuletzt gemeldete Wert, kein Mittel.
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", '@', "R1;3,90,7;", false, -80, 5, 13);
    TEST_ASSERT_EQUAL_INT8(7, esnr(m, 0, iaaa));
    nbrNoteFrame(m, "DK5EN-93,OE1AAA-1", '@', "R1;3,90,-3;", false, -80, 5, 13);
    TEST_ASSERT_EQUAL_INT8(-3, esnr(m, 0, iaaa));
    // Ein verfallener Wert faengt neu an: erster Wert = der Wert.
    nbrNoteFrame(m, "OE1AAA-1", ':', NULL, false, -80, -20, (uint16_t)(13 + NBR_WINDOW_MIN));
    TEST_ASSERT_EQUAL_INT8(-20, esnr(m, iaaa, 0));
#else
    TEST_IGNORE_MESSAGE("NBR_SNR_AVG_N != 8 in dieser Umgebung");
#endif
}

// Frische kommt aus der Kantenminute, nicht aus dem Sweep: zwei Matrizen mit
// denselben Rahmen, eine jede Minute gesweept, eine nie, liefern dieselben
// Urteile. Die Halbierung (die NUR der Sweep ausfuehrt) ist hier per
// last_halve festgehalten; ohne sie unterscheidet die beiden nur, ob
// verfallene Kanten noch im Pool stehen.
void test_decisions_do_not_depend_on_the_sweep(void)
{
    static NbrMatrix a, b;
    nbrInit(a, "DK5EN-93", 0);
    nbrInit(b, "DK5EN-93", 0);
    const char *frames[] = {"OE1AAA-1", "OE1BBB-2,OE1AAA-1", "DK5EN-93,OE1BBB-2", "OE1CCC-3,OE1BBB-2",
                            "OE1AAA-1,OE1CCC-3", "OE1DDD-4"};
    const char types[] = {'!', '!', '@', '!', '!', ':'};
    const int nf = 6;
    for (uint16_t t = 1; t < 1600; t++)
    {
        a.last_halve = t;   // Halbierung aus (siehe Kommentar)
        nbrSweep(a, t);
        int f = -1;
        if (t < 200 && t % 7 == 0)
            f = (t / 7) % nf;               // alle Kanten frisch
        else if (t >= 1000 && t % 50 == 0)
            f = (t / 50) % 3;               // nach > 720 min Pause nur noch ein Teil
        if (f >= 0)
        {
            nbrNoteFrame(a, frames[f], types[f], "R1;3,90,-5;", false, -80, (int8_t)(t % 11 - 5), t);
            nbrNoteFrame(b, frames[f], types[f], "R1;3,90,-5;", false, -80, (int8_t)(t % 11 - 5), t);
        }
        if (t % 37 == 0)
        {
            TEST_ASSERT_MASK_EQ(nbrDirectMask(b, t), nbrDirectMask(a, t));
            TEST_ASSERT_MASK_EQ(nbrHeardMeMask(b, t), nbrHeardMeMask(a, t));
            for (int row = 0; row < NBR_MAX_ROWS; row++)
            {
                TEST_ASSERT_EQUAL_INT(nbrRowMeshNeedCount(b, row, t), nbrRowMeshNeedCount(a, row, t));
                TEST_ASSERT_MASK_EQ(nbrHearersMask(b, row, t), nbrHearersMask(a, row, t));
                TEST_ASSERT_EQUAL_UINT8(nbrHearers(b, row, t, NULL, 0), nbrHearers(a, row, t, NULL, 0));
            }
            uint8_t oa[NBR_MAX_ROWS], ob[NBR_MAX_ROWS];
            TEST_ASSERT_EQUAL_INT(nbrExclusive(b, t, ob, NBR_MAX_ROWS), nbrExclusive(a, t, oa, NBR_MAX_ROWS));
            TEST_ASSERT_EQUAL_INT(nbrExclusiveDirect(b, t, ob, NBR_MAX_ROWS), nbrExclusiveDirect(a, t, oa, NBR_MAX_ROWS));
            for (int f2 = 0; f2 < nf; f2++)
            {
                NbrNeed ra = nbrRelayNeed(a, frames[f2], t, true, 0);
                NbrNeed rb = nbrRelayNeed(b, frames[f2], t, true, 0);
                TEST_ASSERT_EQUAL(rb.known, ra.known);
                TEST_ASSERT_MASK_EQ(rb.need, ra.need);
                TEST_ASSERT_MASK_EQ(rb.alone, ra.alone);
                TEST_ASSERT_MASK_EQ(rb.inferred, ra.inferred);
            }
            char ba[160], bb[160];
            TEST_ASSERT_EQUAL_INT(nbrBuildReport(b, t, 1, bb, sizeof(bb)), nbrBuildReport(a, t, 1, ba, sizeof(ba)));
            TEST_ASSERT_EQUAL_STRING(bb, ba);
        }
    }
    // Der Sweep hat tatsaechlich aufgeraeumt, ohne ihn stehen verfallene Kanten.
    TEST_ASSERT_TRUE(nbrEdgesUsed(a) < nbrEdgesUsed(b));
    TEST_ASSERT_TRUE(masks_match_pool(a));
    TEST_ASSERT_TRUE(masks_match_pool(b));
}

// Advisor Welle 2, Befund A: eine HEY-Gruppe fuer das Paar (x, ich) ist mein
// eigener frueherer Empfang von x, als Echo zurueck. Sie darf das laufende
// SNR-Mittel der Kante (x, 0) nicht ueberschreiben. Vor dem Fix: -20.
static void test_echoed_hey_group_leaves_me_snr_mean_alone(void)
{
#if NBR_SNR_AVG_N > 1
    NbrMatrix m;
    nbrInit(m, "DK5EN-93", 300);
    for (int i = 0; i < 8; i++)
        nbrNoteFrame(m, "OE1AAA-1", '!', "", false, -70, 10, 300);
    int iaaa = nbrFind(m, "OE1AAA-1");
    TEST_ASSERT_TRUE(iaaa > 0);
    TEST_ASSERT_EQUAL_INT8(10, esnr(m, iaaa, 0));

    nbrNoteFrame(m, "OE1AAA-1,DK5EN-93,OE1BBB-2", '@', "R5;3,97,-20;4,101,3;", false, -60, 5, 301);
    TEST_ASSERT_EQUAL_INT8(10, esnr(m, iaaa, 0));
#else
    TEST_IGNORE_MESSAGE("NBR_SNR_AVG_N == 1: Gruppe ueberschreibt wie in der dichten Matrix");
#endif
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
    RUN_TEST(test_dropped_frame_still_gets_me_step);
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
    RUN_TEST(test_nbr_row_size_and_sym_threshold_unchanged_by_the_new_field);
    RUN_TEST(test_build_report_orders_by_snr_desc_tie_by_callsign);
    RUN_TEST(test_build_report_threshold_inclusive_at_sym_min_snr_and_excluded_below);
    RUN_TEST(test_build_report_freshness_cut_at_60_min);
    RUN_TEST(test_build_report_unknown_snr_excluded);
    RUN_TEST(test_build_report_n0_is_a_valid_empty_list);
    RUN_TEST(test_build_report_buffer_too_small_returns_minus1);
    RUN_TEST(test_build_report_cap_constant_and_no_truncation_below_it);
    RUN_TEST(test_note_report_applies_full_spec_example);
    RUN_TEST(test_note_report_malformed_variants_are_rejected_wholesale);
    RUN_TEST(test_note_report_sender_without_row_returns_zero);
    RUN_TEST(test_sym_veto_blocks_inference_when_report_is_complete_and_fresh);
    RUN_TEST(test_sym_veto_does_not_apply_to_a_truncated_report);
    RUN_TEST(test_sym_veto_expires_after_valid_window);
    RUN_TEST(test_sym_veto_report_listing_m_is_observed_not_inferred);
    RUN_TEST(test_report_round_trip_build_then_note);
    RUN_TEST(test_text_frame_creates_only_me_row_not_the_injecting_path);
    RUN_TEST(test_pos_frame_same_path_still_creates_the_pair_edge);
    RUN_TEST(test_text_frame_with_three_tokens_touches_only_the_last_hop);
    RUN_TEST(test_text_frame_with_own_call_as_last_hop_is_still_a_pure_echo);
    RUN_TEST(test_text_echo_of_my_own_frame_marks_no_hears_me_edge);
    RUN_TEST(test_gateway_echo_text_gives_far_origin_no_row);
    RUN_TEST(test_call_word_round_trip_and_invalid_calls);
    RUN_TEST(test_mask_hex_is_fixed_width_most_significant_first);
    RUN_TEST(test_never_initialised_matrix_reads_as_empty);
    RUN_TEST(test_row_eviction_frees_edges_and_clears_mask_bits);
    RUN_TEST(test_share_rule_one_hit_against_fifty_does_not_cover);
    RUN_TEST(test_counter_halving_in_sweep);
    RUN_TEST(test_me_snr_is_a_running_mean);
    RUN_TEST(test_decisions_do_not_depend_on_the_sweep);
    RUN_TEST(test_echoed_hey_group_leaves_me_snr_mean_alone);
    return UNITY_END();
}
