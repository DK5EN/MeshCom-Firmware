// Native Testsuite fuer die RENDER-Seite des mHeard-Moduls (BACKLOG SS3.8j-
// Folgeauftrag): decodeMHeard() (Parsing INS Table) ist bereits durch
// test_decodemheard abgedeckt, Aging/Eviction durch test_mheard_aging. Diese
// Suite pinnt stattdessen, was aus der Tabelle HERAUS gerendert wird:
//
//   * showMHeard()  -- ASCII-Tabellendump (`--mheard`-Kommando), printfdeb().
//   * showPath()    -- ASCII-Tabellendump der Pfad-Tabelle, printfdeb().
//   * sendMheard()  -- JSON-Serialisierung jedes frischen Tabelleneintrags
//                      Richtung Handy (addBLEComToOutBuffer), reales
//                      ArduinoJson + das echte (header-only, static inline)
//                      bleJsonFrame() aus src/ble_json_frame.h -- kein Stub
//                      noetig, das laeuft nativ tatsaechlich.
//   * getPayloadType()/getHardwareLong() -- die reinen Formatierungshelfer,
//                      die beide obigen Renderer fuer Typ-/Hardwarespalten
//                      aufrufen.
//
// showMHeardTDECK()/showPathTDECK() (LVGL-Tabellen fuers T-Deck-Display)
// bleiben AUSSEN VOR: sie sitzen hinter
// `#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)`, keines von
// beiden ist im nativen Testbuild definiert (siehe platformio.ini,
// build_flags dieser Env setzt nur NATIVE_BUILD/UNIT_TEST), der Praeprozessor
// schneidet den kompletten Codepfad heraus -- er ist im nativen Testbinary
// schlicht nicht vorhanden, nicht bloss ungetestet. Ihn zu pruefen bräuchte
// echte LVGL-Objekte (`mheard_ta`/`path_ta`) und `lv_table_set_cell_value()`,
// beides nicht Teil des nativen Stub-Satzes.
//
//   pio test -e native_parsers -f test_mheard_render
//
// ---------------------------------------------------------------- Stubs
//
// Bewusst KEIN #include von test/test_decodemheard/stubs/parser_link_stubs.h
// (siehe Datei-Kommentar dort): dessen printlndeb()/printdeb()/printfdeb()
// sind No-Ops, die die Ausgabe verwerfen -- fuer decodeMHeard()/Aging-Tests
// (die auf globale Arrays statt auf Textausgabe pruefen) reicht das, fuer
// diese Suite waere es das Gegenteil des Auftrags: showMHeard()/showPath()
// schreiben NUR ueber printlndeb()/printfdeb(), ein No-Op-Stub liesse nichts
// zum Pinnen uebrig. test_mheard_aging.cpp macht es exakt so vor (eigener,
// lokaler Stub-Satz statt der gemeinsamen Datei) -- hier identisch
// nachgebaut, nur dass printlndeb()/printdeb()/printfdeb() tatsaechlich in
// einen Puffer schreiben statt zu verwerfen. Jede Testsuite dieser Env ist
// ein eigenes Programm (siehe platformio.ini test_filter) mit eigener
// main(), also kein Mehrfachdefinitions-Risiko zwischen den Dateien.
//
// build_src_filter dieser Env linkt env-weit Regexp.cpp, regex_functions.cpp,
// aprs_functions.cpp, charset_filter.cpp, mheard_functions.cpp UND
// via_functions.cpp in jedes Testprogramm -- der folgende Stub-Satz deckt
// exakt die externen Symbole, die diese fuenf Uebersetzungseinheiten
// zusammen brauchen (identisch zu test_mheard_aging.cpp, das dieselbe
// Kombination bereits erfolgreich linkt).

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Arduino.h>
#include <aprs_structures.h>
#include <mc_text.h>
#include <mheard_functions.h>
#include <nrf52/WisBlock-API.h>

s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// ---- printfdeb_functions.h (src/) -- capturing statt No-Op (siehe oben).
// Ein einzelner Puffer fuer die ganze Suite: setUp() leert ihn vor jedem
// Testfall, damit Faelle einander nicht beeinflussen.
static std::string g_out;

int printlndeb(const char *buff)
{
    g_out += (buff ? buff : "");
    g_out += "\n";
    return 0;
}
int printdeb(const char *buff)
{
    g_out += (buff ? buff : "");
    return 0;
}
int printdeb(String str)
{
    g_out += str.c_str();
    return 0;
}
int printfdeb(const char *format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n > 0)
        g_out.append(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
    return n;
}

// ---- loop_functions.h
unsigned long getUnixClock() { return 0; }
String getTimeString() { return String(""); }

// addBLEOutBuffer() is updateMheard()'s single-entry ingest push (already
// exercised indirectly by test_mheard_aging.cpp) -- captured here only so
// the buffer/frame_len are not left dangling; this suite does not assert on
// it, sendMheard()'s addBLEComToOutBuffer() below is the one under test.
void addBLEOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }

// ---- sendMheard()'s outward JSON frames: captured whole (type byte +
// serialized JsonDocument, produced by the REAL bleJsonFrame() in
// src/ble_json_frame.h -- header-only/static inline, no stub needed, so
// this is the actual ArduinoJson serialisation, not a fake).
struct CapturedFrame
{
    uint8_t type;
    std::string json;
};
static std::vector<CapturedFrame> g_bleFrames;

void addBLEComToOutBuffer(uint8_t *buffer, uint16_t len)
{
    CapturedFrame f;
    f.type = (len > 0) ? buffer[0] : 0;
    f.json = (len > 1) ? std::string((const char *)buffer + 1, len - 1) : std::string();
    g_bleFrames.push_back(f);
}

bool is_equ(const char *buf1, const char *buf2)
{
    return buf1 != nullptr && buf2 != nullptr && strcmp(buf1, buf2) == 0;
}

// ---- time_functions.h -- showPath() formats mheardPathEpoch[] through
// this. A fixed, recognisable non-empty stub value (real convertUNIXtoString()
// depends on wall-clock/timezone globals this suite does not set up) lets
// showPath()'s surrounding "%-19.19s"-formatting still be pinned exactly,
// without pretending to characterize convertUNIXtoString() itself (that
// belongs to time_functions, not mheard_functions).
String convertUNIXtoString(uint32_t timestamp) { (void)timestamp; return String("2026-09-12 10:00:00"); }

// ---- loop_functions_extern.h
bool bGATEWAY = false;
bool bVIA = false;

// mheard_functions.cpp's globals, inspected directly the same way
// test_mheard_aging.cpp does (mheard_functions.h deliberately does not
// extern them for production callers -- see NC-02 comment there -- but a
// test in the same conceptual unit may).
extern char mheardCalls[MAX_MHEARD][10];
extern int mheardNCount[MAX_MHEARD];
// DR-28: needed to set up mheardSortedIndex()'s direct unit tests below with
// exact, hand-picked ages instead of driving them through updateMheard()'s
// own millis()-stamping.
extern uint32_t mheardMillis[MAX_MHEARD];

void setUp(void)
{
    initMheard();
    mc_test_set_millis(0);
    g_out.clear();
    g_bleFrames.clear();
}

void tearDown(void) {}

// ------------------------------------------------------- Testaufbau-Helfer

// Baut eine wohlgeformte mheardLine mit gueltigem Datum (Jahr >= 2025, sonst
// verwirft updateMheard() den Eintrag ungeprueft -- siehe test_mheard_aging.cpp).
static void buildLine(struct mheardLine &mh, const char *callsign,
                       char ptype, uint8_t hw, uint8_t mod,
                       int16_t rssi, int8_t snr, double dist,
                       uint8_t path_len, uint8_t mesh, uint8_t ncount)
{
    initMheardLine(mh);
    mcSet(mh.mh_callsign, sizeof(mh.mh_callsign), callsign);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-09-12");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "10:00:00");
    mh.mh_payload_type = ptype;
    mh.mh_hw = hw;
    mh.mh_mod = mod;
    mh.mh_rssi = rssi;
    mh.mh_snr = snr;
    mh.mh_dist = dist;
    mh.mh_path_len = path_len;
    mh.mh_mesh = mesh;
    mh.mh_ncount = ncount;
}

// ============================================================ getPayloadType()

static void test_getPayloadType_alle_bekannten_und_ein_unbekannter_typ(void)
{
    TEST_ASSERT_EQUAL_STRING("TXT", getPayloadType(':'));
    TEST_ASSERT_EQUAL_STRING("POS", getPayloadType('!'));
    TEST_ASSERT_EQUAL_STRING("HEY", getPayloadType('@'));
    // Sentinel "unbekannt": jedes andere Byte, inkl. des initMheardLine()-
    // Defaults 0x00, faellt auf "???".
    TEST_ASSERT_EQUAL_STRING("???", getPayloadType('X'));
    TEST_ASSERT_EQUAL_STRING("???", getPayloadType('\0'));
}

// ============================================================ getHardwareLong()

static void test_getHardwareLong_direkter_index_und_legacy_remap(void)
{
    // Nativer Build definiert weder BOARD_T_DECK noch BOARD_T_DECK_PLUS ->
    // der zweite HardWare[]-Array-Zweig in mheard_functions.cpp gilt.
    TEST_ASSERT_EQUAL_STRING("no info", getHardwareLong(0).c_str());
    TEST_ASSERT_EQUAL_STRING("RAK4631", getHardwareLong(9).c_str());       // direkter Index, kein Remap
    TEST_ASSERT_EQUAL_STRING("T_WATCH_S3", getHardwareLong(35).c_str());   // hoechster direkter Index

    // Legacy-IDs (>=39): remappt auf einen frueheren Index, siehe die
    // if-Leiter in getHardwareLong(). 39 -> 13 ("EBYTE_E22"),
    // 61 -> 35 ("T_WATCH_S3", derselbe String wie der direkte Index 35 oben).
    TEST_ASSERT_EQUAL_STRING("EBYTE_E22", getHardwareLong(39).c_str());
    TEST_ASSERT_EQUAL_STRING("T_WATCH_S3", getHardwareLong(61).c_str());
}

// Sentinel "unbekannte Hardware": eine ID, die WEDER ein direkter Index
// (0..35) NOCH eine der bekannten Legacy-IDs (39..61) ist, faellt auf
// Index 0 ("no info") -- exakt denselben String wie hwid==0, ein
// Charakterisierungs-Fund: es gibt keinen eigenen "unbekannt"-String, nur
// denselben wie fuer "keine Info".
static void test_getHardwareLong_out_of_range_faellt_auf_no_info(void)
{
    TEST_ASSERT_EQUAL_STRING("no info", getHardwareLong(36).c_str());    // Luecke zwischen Index-Bereich und Legacy-Remap
    TEST_ASSERT_EQUAL_STRING("no info", getHardwareLong(38).c_str());    // ein Wert unter der Legacy-Remap-Schwelle
    TEST_ASSERT_EQUAL_STRING("no info", getHardwareLong(255).c_str());   // ueber jedem Remap-Eintrag
}

// ============================================================ showMHeard()

static void test_showMHeard_leere_tabelle_nur_rahmen(void)
{
    showMHeard();

    const char *expected =
        "\n/-----------------------------------------------------------------------------------------------------\\\n"
        "|MHeard call |    date    |   time   | typ | source hardware | mod | rssi |  snr | dist | pl | m | nc |\n"
        "\\-----------------------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected, g_out.c_str());
}

static void test_showMHeard_ein_eintrag_alle_spalten(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-1", '!', 9, 0x83, -95, -3, 12.7, 3, 1, 5);
    updateMheard(mh, 0);
    g_out.clear();   // updateMheard() selbst schreibt nichts ueber printfdeb -- Klarheit, kein Effekt

    showMHeard();

    const char *expected =
        "\n/-----------------------------------------------------------------------------------------------------\\\n"
        "|MHeard call |    date    |   time   | typ | source hardware | mod | rssi |  snr | dist | pl | m | nc |\n"
        "|------------|------------|----------|-----|-----------------|-----|------|------|------|----|---|----|\n"
        "| DK5EN-1    | 2026-09-12 | 10:00:00 | POS | RAK4631    /009 | 8/3 |  -95 |   -3 | 12.7 |  3 | 1 |  5 |\n"
        "\\-----------------------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected, g_out.c_str());
}

// SORT ORDER (DR-28, BACKLOG OPT-D16): showMHeard() muss most-recent-first
// anzeigen -- NICHT die physische Ringpuffer-Slot-Reihenfolge (die Storage-
// Arrays selbst werden dafuer nicht umsortiert, siehe mheardSortedIndex()
// in mheard_functions.cpp).
//
// Beweisaufbau: updateMheard()s eigene Slot-Suche (siehe dortiger Kommentar
// und test_mheard_aging.cpp) fuellt eine leere Tabelle von HINTEN nach VORNE
// -- der erste neue Eintrag landet in Slot MAX_MHEARD-1, der zweite in
// MAX_MHEARD-2, usw. Drei Eintraege in der Hoer-Reihenfolge C, A, B landen
// also physisch bei Slot 79 (C), 78 (A), 77 (B). Ein rein physischer,
// aufsteigender iset-Lauf zeigte sie in der Reihenfolge B, A, C; DR-28
// verlangt stattdessen die Anzeigereihenfolge nach Aktualitaet.
//
// C wird danach per gleichem Rufzeichen erneut gehoert (bOld-Pfad in
// updateMheard() -- der Slot bleibt derselbe, nur mheardMillis[] wird
// aktualisiert) und ist damit der zeitlich JUENGSTE Eintrag, obwohl es
// physisch zuerst (Slot 79) gehoert wurde. Erwartete Anzeigereihenfolge
// most-recent-first: C (gerade erneut gehoert), B (zuletzt vor C gehoert),
// A (am laengsten her).
static void test_showMHeard_zeigt_zuletzt_gehoert_zuerst(void)
{
    struct mheardLine mh;

    // Hoer-Reihenfolge: C, A, B (bewusst weder alphabetisch noch dessen
    // Umkehrung) -- landet physisch bei Slot 79/78/77.
    buildLine(mh, "DK5EN-C", '!', 9, 0, -60, 1, 1.0, 0, 0, 0);
    updateMheard(mh, 0);
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-A", '!', 9, 0, -61, 2, 2.0, 0, 0, 0);
    updateMheard(mh, 0);
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-B", '!', 9, 0, -62, 3, 3.0, 0, 0, 0);
    updateMheard(mh, 0);

    // Slot-Positionen wie im Kommentar oben hergeleitet -- direkte
    // Introspektion belegt den Aufbau, bevor der Renderer geprueft wird.
    // Diese physische Lage ist ein Implementierungsdetail der Speicherung
    // (siehe mheard_functions.h/mheardSortedIndex()-Kommentar), nicht die
    // Anzeigereihenfolge.
    TEST_ASSERT_EQUAL_STRING("DK5EN-C", mheardCalls[79]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-A", mheardCalls[78]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-B", mheardCalls[77]);

    // DK5EN-C erneut hoeren: bleibt in Slot 79 (bOld-Pfad), wird aber jetzt
    // der zeitlich JUENGSTE Eintrag.
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-C", '!', 9, 0, -63, 4, 4.0, 0, 0, 0);
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN-C", mheardCalls[79]);   // derselbe Slot, kein Verschieben

    g_out.clear();
    showMHeard();

    // Most-recent-first: C (gerade erneut gehoert) zuerst, dann B (vor C
    // zuletzt gehoert), dann A (am laengsten her) -- NICHT die Hoer-
    // Reihenfolge (C,A,B), NICHT alphabetisch (A,B,C), NICHT die physische
    // Slot-Reihenfolge (B,A,C).
    size_t posA = g_out.find("DK5EN-A");
    size_t posB = g_out.find("DK5EN-B");
    size_t posC = g_out.find("DK5EN-C");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, posA);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, posB);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, posC);
    TEST_ASSERT_TRUE(posC < posB);
    TEST_ASSERT_TRUE(posB < posA);
}

// Feldformatierung: negativer RSSI/SNR, dist==0.0, hop/mesh/ncount==0 --
// alle "%4i"/"%2i"/"%3i"-Felder mit dem kleinstmoeglichen sinnvollen Wert,
// keine Sonderbehandlung fuer 0 (kein Platzhalter-Text, einfach "0").
static void test_showMHeard_negative_und_null_felder(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-0", ':', 0, 0, -128, -20, 0.0, 0, 0, 0);
    updateMheard(mh, 0);
    g_out.clear();

    showMHeard();

    const char *expected =
        "\n/-----------------------------------------------------------------------------------------------------\\\n"
        "|MHeard call |    date    |   time   | typ | source hardware | mod | rssi |  snr | dist | pl | m | nc |\n"
        "|------------|------------|----------|-----|-----------------|-----|------|------|------|----|---|----|\n"
        "| DK5EN-0    | 2026-09-12 | 10:00:00 | TXT | no info    /000 | 0/0 | -128 |  -20 |  0.0 |  0 | 0 |  0 |\n"
        "\\-----------------------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected, g_out.c_str());
}

// Truncation: Callsign an der Speichergrenze. mheardCalls[][10] speichert
// hoechstens 9 Nutzzeichen (updateMheard() deckelt icsize auf
// sizeof(...)-1); ein 9-Zeichen-Rufzeichen passt komplett, ein
// 10-Zeichen-Rufzeichen verliert das letzte Zeichen SCHON BEIM SPEICHERN --
// nicht erst bei der Anzeige. showMHeard()s "%-10.10s"-Format wuerde selbst
// bis zu 10 Zeichen anzeigen, sieht wegen der Speichergrenze aber nie mehr
// als 9. Diese Grenze wird hier genau getroffen, nicht ueberschritten (ein
// laengeres Rufzeichen waere fuer den Testprozess unschaedlich -- memcpy()
// kopiert ohnehin nur icsize Bytes -- aber ausserhalb der Charakterisierung
// dieser Grenze selbst nicht mehr aussagekraeftig).
static void test_showMHeard_callsign_an_speichergrenze_wird_gekappt(void)
{
    struct mheardLine mh;

    // 9 Zeichen: passt vollstaendig (sizeof(mheardCalls[i])-1 == 9).
    buildLine(mh, "DK5EN-999", '!', 9, 0, -70, 0, 1.0, 0, 0, 0);
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN-999", mheardCalls[79]);

    // 10 Zeichen: das zehnte ('9' am Ende) wird beim Speichern verworfen,
    // im Ringpuffer steht nur noch "DK5EN-1234" gekuerzt auf 9 Zeichen ->
    // "DK5EN-123".
    buildLine(mh, "DK5EN-1234", '!', 9, 0, -71, 0, 1.0, 0, 0, 0);
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_STRING("DK5EN-123", mheardCalls[78]);

    g_out.clear();
    showMHeard();

    // Beide Zeilen der Anzeige zeigen die (ggf. schon gekappten)
    // Speicherwerte, links-buendig auf 10 Zeichen Feldbreite aufgefuellt --
    // "DK5EN-999 " (ein Fuellzeichen) bzw. "DK5EN-123 " (ein Fuellzeichen,
    // NICHT das verworfene "4").
    TEST_ASSERT_NOT_EQUAL(std::string::npos, g_out.find("| DK5EN-999  | "));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, g_out.find("| DK5EN-123  | "));
    TEST_ASSERT_EQUAL(std::string::npos, g_out.find("DK5EN-1234"));
    TEST_ASSERT_EQUAL(std::string::npos, g_out.find("DK5EN-1233"));
}

// Aging-Grenze: ein Eintrag, der (monoton, siehe NC-01) laenger als 12h
// zurueckliegt, wird von showMHeard() uebersprungen -- dasselbe
// MHEARD_PRUNE_WINDOW_MS-Fenster wie in updateMheard()s eigenem
// "DELETE after 12h"-Zweig, hier aber am Anzeige-Pfad selbst geprueft statt
// nur an getMheardCount()s separatem 1h-Fenster (test_mheard_aging.cpp
// deckt nur Letzteres ab).
static void test_showMHeard_eintrag_ueber_zwoelf_stunden_wird_nicht_angezeigt(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-OLD", '!', 9, 0, -90, 0, 1.0, 0, 0, 0);
    updateMheard(mh, 0);

    mc_test_advance_millis(60UL * 60UL * 12UL * 1000UL + 1000UL);
    g_out.clear();

    showMHeard();

    const char *expected =
        "\n/-----------------------------------------------------------------------------------------------------\\\n"
        "|MHeard call |    date    |   time   | typ | source hardware | mod | rssi |  snr | dist | pl | m | nc |\n"
        "\\-----------------------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected, g_out.c_str());
}

// ============================================================ sendMheard()

// JSON-Feldreihenfolge/-Formate fuer einen einzelnen frischen Eintrag.
// Bemerkenswerter Formatierungs-Fund: PLT ist NICHT der von
// getPayloadType() dekodierte Text ("POS"/"TXT"/"HEY"), sondern der rohe
// (uint8_t)-ASCII-Code des Typbytes -- showMHeard() und sendMheard() zeigen
// denselben Rohwert also unterschiedlich an (Text vs. Zahl).
static void test_sendMheard_ein_frischer_eintrag_json_feldreihenfolge(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-1", '!', 9, 0x83, -95, -3, 12.7, 3, 1, 5);
    updateMheard(mh, 0);
    g_bleFrames.clear();

    sendMheard();

    TEST_ASSERT_EQUAL_INT(1, (int)g_bleFrames.size());
    TEST_ASSERT_EQUAL_UINT8(0x44, g_bleFrames[0].type);
    const char *expected_json =
        "{\"TYP\":\"MH\",\"CALL\":\"DK5EN-1\",\"DATE\":\"2026-09-12\",\"TIME\":\"10:00:00\","
        "\"PLT\":33,\"HW\":9,\"MOD\":131,\"RSSI\":-95,\"SNR\":-3,\"DIST\":12.7,\"PL\":3,"
        "\"MESH\":1,\"NCNT\":5}";
    TEST_ASSERT_EQUAL_STRING(expected_json, g_bleFrames[0].json.c_str());
}

// SORT ORDER, JSON-Serialisierer (DR-28): sendMheard() muss dieselbe
// most-recent-first-Reihenfolge liefern wie showMHeard() (siehe dortiger
// Beweis) -- ueber mheardSortedIndex(), nicht ueber einen physischen
// "for(iset=0..MAX_MHEARD)"-Scan.
static void test_sendMheard_zuletzt_gehoert_zuerst(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-C", '!', 9, 0, -60, 1, 1.0, 0, 0, 0);
    updateMheard(mh, 0);
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-A", '!', 9, 0, -61, 2, 2.0, 0, 0, 0);
    updateMheard(mh, 0);
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-B", '!', 9, 0, -62, 3, 3.0, 0, 0, 0);
    updateMheard(mh, 0);

    // DK5EN-C erneut hoeren (bOld-Pfad, gleicher Slot) -- jetzt der
    // zeitlich juengste Eintrag, siehe test_showMHeard_zeigt_zuletzt_...
    mc_test_advance_millis(1000UL);
    buildLine(mh, "DK5EN-C", '!', 9, 0, -63, 4, 4.0, 0, 0, 0);
    updateMheard(mh, 0);

    g_bleFrames.clear();
    sendMheard();

    TEST_ASSERT_EQUAL_INT(3, (int)g_bleFrames.size());
    // Most-recent-first: C (gerade erneut gehoert), B (vor C zuletzt
    // gehoert), A (am laengsten her) -- NICHT die Hoer-Reihenfolge
    // (C,A,B) und NICHT die physische Slot-Reihenfolge (B,A,C).
    TEST_ASSERT_TRUE(g_bleFrames[0].json.find("\"CALL\":\"DK5EN-C\"") != std::string::npos);
    TEST_ASSERT_TRUE(g_bleFrames[1].json.find("\"CALL\":\"DK5EN-B\"") != std::string::npos);
    TEST_ASSERT_TRUE(g_bleFrames[2].json.find("\"CALL\":\"DK5EN-A\"") != std::string::npos);
}

// Randabdeckung Slot 0: alle Faelle oben belegen ausschliesslich Slot 77-79
// (updateMheard()s Rueckwaerts-Fuellung bei einer FRISCH initialisierten
// Tabelle, siehe test_showMHeard_zeigt_physische_slot_reihenfolge...). Ein
// Mutant, der sendMheard()s Scan-Schleife auf "iset=1..MAX_MHEARD-1"
// verkuerzt (Slot 0 wird nie geprueft/gesendet), waere von KEINEM der
// obigen Faelle gefangen worden -- Slot 0 kommt dort nie vor. Dieser Test
// fuellt die Tabelle bewusst VOLLSTAENDIG (MAX_MHEARD Eintraege), damit
// Slot 0 garantiert belegt ist (die letzte Neueinfuegung in eine sich von
// hinten nach vorne fuellende Tabelle landet in Slot 0, siehe Kommentar
// oben), und beweist damit zwei Dinge zugleich: dass sendMheard() wirklich
// JEDEN Slot inklusive Slot 0 scannt (Framezahl == MAX_MHEARD, nicht
// MAX_MHEARD-1), und dass die physische Reihenfolge (nicht Sortierung) sich
// auch ueber die volle Tabellenbreite haelt.
static void test_sendMheard_deckt_slot_null_ab(void)
{
    struct mheardLine mh;
    char callbuf[16];

    for (int i = 0; i < MAX_MHEARD; i++)
    {
        snprintf(callbuf, sizeof(callbuf), "DK5EN-%02d", i);
        buildLine(mh, callbuf, '!', 9, 0, -70, 0, 1.0, 0, 0, 0);
        updateMheard(mh, 0);
    }

    // Letzte Neueinfuegung (i==MAX_MHEARD-1) landet in Slot 0 (Rueckwaerts-
    // Fuellung, siehe Datei-Kommentar oben) -- direkte Introspektion vor dem
    // eigentlichen Renderer-Aufruf.
    TEST_ASSERT_EQUAL_STRING("DK5EN-79", mheardCalls[0]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-00", mheardCalls[79]);

    g_bleFrames.clear();
    sendMheard();

    // Framezahl selbst ist der Beweis gegen "Slot 0 wird uebersprungen":
    // MAX_MHEARD, nicht MAX_MHEARD-1.
    TEST_ASSERT_EQUAL_INT(MAX_MHEARD, (int)g_bleFrames.size());
    // Aufsteigender iset-Scan -> erster Frame ist Slot 0 (DK5EN-79, die
    // zuletzt gehoerte, physisch aber zuerst gerenderte Station), letzter
    // Frame ist Slot 79 (DK5EN-00, zuerst gehoert, zuletzt gerendert).
    TEST_ASSERT_TRUE(g_bleFrames.front().json.find("\"CALL\":\"DK5EN-79\"") != std::string::npos);
    TEST_ASSERT_TRUE(g_bleFrames.back().json.find("\"CALL\":\"DK5EN-00\"") != std::string::npos);
}

// Aging-Grenze fuer den JSON-Pfad: identisch zu showMHeard()s Fund oben,
// separat belegt, weil sendMheard() einen eigenen Codepfad (kein
// gemeinsamer Helfer) mit demselben Fenster ist.
static void test_sendMheard_eintrag_ueber_zwoelf_stunden_wird_nicht_gesendet(void)
{
    struct mheardLine mh;
    buildLine(mh, "DK5EN-OLD", '!', 9, 0, -90, 0, 1.0, 0, 0, 0);
    updateMheard(mh, 0);

    mc_test_advance_millis(60UL * 60UL * 12UL * 1000UL + 1000UL);
    g_bleFrames.clear();

    sendMheard();

    TEST_ASSERT_EQUAL_INT(0, (int)g_bleFrames.size());
}

// ============================================================ mheardSortedIndex()
//
// Direct unit tests for the DR-28 helper itself, bypassing updateMheard()'s
// own millis()-stamping so each case can pick exact, hand-crafted ages.
// setUp() has already run initMheard(), so every slot starts unoccupied.

static void test_mheardSortedIndex_leere_tabelle(void)
{
    uint8_t idx[MAX_MHEARD];

    uint8_t n = mheardSortedIndex(idx, (uint32_t)millis());

    TEST_ASSERT_EQUAL_UINT8(0, n);
}

static void test_mheardSortedIndex_sortiert_nach_alter_aufsteigend(void)
{
    // Slot-Positionen absichtlich nicht sortiert und nicht benachbart, damit
    // ein Mutant, der einfach die physische Reihenfolge durchreicht, nicht
    // zufaellig durchrutscht.
    mheardCalls[10][0] = 'A'; mheardMillis[10] = 100;
    mheardCalls[5][0]  = 'B'; mheardMillis[5]  = 300;
    mheardCalls[20][0] = 'C'; mheardMillis[20] = 200;

    uint8_t idx[MAX_MHEARD];
    uint8_t n = mheardSortedIndex(idx, 1000);

    TEST_ASSERT_EQUAL_UINT8(3, n);
    // millis 300 -> kleinstes Alter -> zuerst; millis 100 -> groesstes
    // Alter -> zuletzt.
    TEST_ASSERT_EQUAL_UINT8(5,  idx[0]);
    TEST_ASSERT_EQUAL_UINT8(20, idx[1]);
    TEST_ASSERT_EQUAL_UINT8(10, idx[2]);
}

// Rollover-Sicherheit: dieselbe unsigned-Subtraktion wie die bestehenden
// Aging-Filter in diesem File (NC-01) -- ein Eintrag, dessen mheardMillis[]
// kurz VOR einem uint32_t-Ueberlauf liegt, muss trotzdem als AELTER gelten
// als einer kurz NACH `now`s Nulldurchgang, nicht juenger.
static void test_mheardSortedIndex_rollover_sicher(void)
{
    mheardCalls[3][0] = 'X'; mheardMillis[3] = 0xFFFFFF00UL;   // Alter bei now=50: 306
    mheardCalls[7][0] = 'Y'; mheardMillis[7] = 10UL;           // Alter bei now=50: 40

    uint8_t idx[MAX_MHEARD];
    uint8_t n = mheardSortedIndex(idx, 50);

    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT8(7, idx[0]);   // Alter 40 -> juenger -> zuerst
    TEST_ASSERT_EQUAL_UINT8(3, idx[1]);   // Alter 306 -> aelter -> zuletzt
}

// Gleichstand (identisches Alter): haelt den niedrigeren Slot zuerst -- die
// stabile Sortierung schiebt ein Element nur an Eintraegen mit STRENG
// groesserem Alter vorbei (siehe mheardSortedIndex()-Kommentar).
static void test_mheardSortedIndex_gleichstand_haelt_niedrigeren_slot_zuerst(void)
{
    mheardCalls[15][0] = 'A'; mheardMillis[15] = 100;
    mheardCalls[8][0]  = 'B'; mheardMillis[8]  = 100;   // gleiches Alter wie Slot 15

    uint8_t idx[MAX_MHEARD];
    uint8_t n = mheardSortedIndex(idx, 1000);

    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_UINT8(8,  idx[0]);
    TEST_ASSERT_EQUAL_UINT8(15, idx[1]);
}

// Volle Tabelle: MAX_MHEARD belegte Slots -> n == MAX_MHEARD, und jeder Slot
// kommt in idx[] genau einmal vor (kein Slot doppelt, keiner fehlt).
static void test_mheardSortedIndex_volle_tabelle_jeder_slot_genau_einmal(void)
{
    for(int i = 0; i < MAX_MHEARD; i++)
    {
        mheardCalls[i][0] = (char)('A' + (i % 26));
        mheardMillis[i] = (uint32_t)i;   // paarweise verschiedenes Alter
    }

    uint8_t idx[MAX_MHEARD];
    uint8_t n = mheardSortedIndex(idx, 1000);

    TEST_ASSERT_EQUAL_UINT8(MAX_MHEARD, n);

    bool seen[MAX_MHEARD] = {false};
    for(uint8_t k = 0; k < n; k++)
    {
        TEST_ASSERT_FALSE(seen[idx[k]]);
        seen[idx[k]] = true;
    }
    for(int i = 0; i < MAX_MHEARD; i++)
        TEST_ASSERT_TRUE(seen[i]);
}

// ============================================================ showPath()

static void test_showPath_leere_tabelle_nur_rahmen(void)
{
    showPath();

    const char *expected =
        "\n/-----------------------------------------------------------------------------------------\\\n"
        "|       date          | lng/Gate/Path                                                     |\n"
        "\\-----------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected, g_out.c_str());
}

static void test_showPath_ein_eintrag(void)
{
    struct mheardLine mh;
    initMheardLine(mh);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-09-12");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "10:00:00");
    mcSet(mh.mh_sourcecallsign, sizeof(mh.mh_sourcecallsign), "DK5EN-9");
    mcSet(mh.mh_sourcepath, sizeof(mh.mh_sourcepath), "DK5EN-9,DB0XXX-12");
    mh.mh_destinationpath[0] = 0;
    mh.mh_path_len = 3;
    updateHeyPath(mh);
    g_out.clear();

    showPath();

    // mheardPathLen bit7 (Gateway-Flag) ist hier 0 -> "G"-Platz bleibt ein
    // Leerzeichen; convertUNIXtoString() ist hier fest gestubbt (siehe
    // Stub-Kommentar oben) -- der Zeitstempel-Wert selbst ist damit nicht
    // Teil dieser Charakterisierung, nur seine "%-19.19s"-Einbettung.
    std::string pad42(42, ' ');
    std::string expected =
        std::string("\n/-----------------------------------------------------------------------------------------\\\n") +
        "|       date          | lng/Gate/Path                                                     |\n" +
        "|---------------------|-------------------------------------------------------------------|\n" +
        "| 2026-09-12 10:00:00 | 3 /DK5EN-9    DB0XXX-12" + pad42 + " |\n" +
        "\\-----------------------------------------------------------------------------------------/\n"
        "\n";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), g_out.c_str());
}

// ============================================================ main()

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_getPayloadType_alle_bekannten_und_ein_unbekannter_typ);
    RUN_TEST(test_getHardwareLong_direkter_index_und_legacy_remap);
    RUN_TEST(test_getHardwareLong_out_of_range_faellt_auf_no_info);
    RUN_TEST(test_showMHeard_leere_tabelle_nur_rahmen);
    RUN_TEST(test_showMHeard_ein_eintrag_alle_spalten);
    RUN_TEST(test_showMHeard_zeigt_zuletzt_gehoert_zuerst);
    RUN_TEST(test_showMHeard_negative_und_null_felder);
    RUN_TEST(test_showMHeard_callsign_an_speichergrenze_wird_gekappt);
    RUN_TEST(test_showMHeard_eintrag_ueber_zwoelf_stunden_wird_nicht_angezeigt);
    RUN_TEST(test_sendMheard_ein_frischer_eintrag_json_feldreihenfolge);
    RUN_TEST(test_sendMheard_zuletzt_gehoert_zuerst);
    RUN_TEST(test_sendMheard_deckt_slot_null_ab);
    RUN_TEST(test_sendMheard_eintrag_ueber_zwoelf_stunden_wird_nicht_gesendet);
    RUN_TEST(test_mheardSortedIndex_leere_tabelle);
    RUN_TEST(test_mheardSortedIndex_sortiert_nach_alter_aufsteigend);
    RUN_TEST(test_mheardSortedIndex_rollover_sicher);
    RUN_TEST(test_mheardSortedIndex_gleichstand_haelt_niedrigeren_slot_zuerst);
    RUN_TEST(test_mheardSortedIndex_volle_tabelle_jeder_slot_genau_einmal);
    RUN_TEST(test_showPath_leere_tabelle_nur_rahmen);
    RUN_TEST(test_showPath_ein_eintrag);
    return UNITY_END();
}
