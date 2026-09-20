// Native Testsuite fuer NC-01 (BACKLOG SS3.8o): mheard-Aging ohne gueltige
// Wanduhr.
//
// Symptom: ein Knoten ohne NTP/GPS (z.B. DK5EN-90, RAK4631, nur LoRa) hoert
// Nachbarn nachweislich, meldet in seinem eigenen Positions-Beacon aber
// dauerhaft "/N0" -- der NCNT-Tag zaehlt getMheardCount() (mheard_functions.cpp),
// und der zaehlte bislang per mheardEpoch[i]+3600 > getUnixClock().
// getUnixClock() (loop_functions.cpp) laeuft mktime() auf ungesetzten
// Datumsfeldern (tm_year=-1900 bei node_date_year==0, der Boot-Default ohne
// NTP/GPS) und liefert (unsigned long)-1 statt eines echten Zeitstempels.
// Jede Addition +Fenster darauf UEBERLAEUFT (wraps modular auf den max.
// Wert des Typs, siehe unten) und jeder Eintrag sieht "abgelaufen" aus --
// selbst einer, der gerade erst gehoert wurde.
//
// Fix: mheardMillis[] (mheard_functions.cpp, parallel zu mheardEpoch[])
// altert Eintraege ueber millis() statt ueber die Wanduhr. Diese Suite
// deckt genau die drei im Auftrag verlangten Faelle ab:
//   1. ungueltige Wanduhr + frisch gehoerte Eintraege -> Count > 0
//   2. Eintrag ueber 1h alt (monoton) -> nicht mehr gezaehlt
//   3. millis()-Ueberlauf ueber die Fenstergrenze -> weiterhin korrekt
//
//   pio test -e native_parsers -f test_mheard_aging
//
// Warum getUnixClock() hier IMMER (unsigned long)-1 liefert (nicht bloss in
// Testfall 1): das ist die exakte "kaputte Wanduhr"-Situation aus dem
// Backlog-Befund, konstant über den ganzen Testlauf gehalten, um zu
// beweisen, dass KEINE der drei Situationen (frisch, >1h alt, millis-Wrap)
// noch von getUnixClock() abhaengt -- nicht nur der Bootzustand. Der
// eigentliche Wrap-Beweis braucht keine bestimmte Bitbreite: (unsigned
// long)-1 IST per Definition der Maximalwert des Typs auf jeder Plattform
// (32 Bit auf nRF52/ESP32, 64 Bit hier nativ), darum ueberlaeuft
// mheardEpoch[i]+Fenster IMMER, unabhaengig von der Breite von
// `unsigned long` auf dem jeweiligen Build.
//
// Stub-Aufbau: env:native_parsers linkt env-weit Regexp.cpp,
// regex_functions.cpp, aprs_functions.cpp, mheard_functions.cpp UND
// via_functions.cpp in JEDES Testprogramm der Env (siehe platformio.ini),
// darum braucht auch dieses Testprogramm den vollen Satz an Link-Stubs, den
// test/test_decodemheard/stubs/parser_link_stubs.h fuer die drei
// bestehenden Suiten bereits bereitstellt. Diese Datei bindet
// parser_link_stubs.h bewusst NICHT ein: getUnixClock() muss hier den
// "kaputte Wanduhr"-Wert liefern statt der dortigen Konstante 0, und ein
// TU darf ein Symbol nur einmal definieren. Der Rest des Stub-Satzes ist
// unten identisch zu parser_link_stubs.h nachgebildet. Die
// nrf52/WisBlock-API.h-Typdefinition (s_meshcom_settings) wird dagegen ECHT
// geteilt, per Include (nicht kopiert) -- der Linker braucht fuer
// "meshcom_settings" ueberall denselben Typ, siehe deren eigenen Kommentar.

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <aprs_structures.h>
#include <mc_text.h>
#include <mheard_functions.h>
#include <mheard_record.h>
#include <nrf52/WisBlock-API.h>

// ---- Link-Stubs fuer aprs_functions.cpp/mheard_functions.cpp/via_functions.cpp
// (siehe Datei-Kommentar oben -- bewusst kein #include von
// parser_link_stubs.h, weil getUnixClock() hier anders sein muss)
//
// NC-02/MH-02 (BACKLOG SS3.8o) ergaenzen diese Suite um zwei weitere Faelle
// (siehe unten): mheardCalls[]/mheardPathCalls[] werden hier per extern
// direkt inspiziert, um zu pruefen, WELCHER Slot von einer Aktion betroffen
// war (nicht bloss OB) -- mheard_functions.h externt diese Arrays bewusst
// NICHT (das ist genau der Punkt von NC-02s mheardFreshMs()/
// mheardPathFreshMs()-Helfern fuer Produktionscode in anderen Dateien), aber
// ein Test darf direkt auf die vom selben TU (mheard_functions.cpp)
// definierten globalen Arrays zugreifen.
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631 -- DK5EN-90 aus dem Backlog-Befund ist genau dieses Board
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

int printlndeb(const char *buff) { (void)buff; return 0; }
int printdeb(const char *buff) { (void)buff; return 0; }
int printdeb(String str) { (void)str; return 0; }
int printfdeb(const char *format, ...) { (void)format; return 0; }

// NC-01: die "kaputte Wanduhr" aus dem Backlog-Befund -- mktime() auf
// ungesetzten Datumsfeldern liefert (time_t)-1, hier direkt als
// (unsigned long)-1 nachgebildet (siehe Datei-Kommentar). Konstant ueber
// die ganze Suite: kein Testfall dieser Datei darf sich noch auf
// getUnixClock() verlassen.
unsigned long getUnixClock() { return (unsigned long)-1; }

String getTimeString() { return String(""); }
void addBLEOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
void addBLEComToOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
bool is_equ(const char *buf1, const char *buf2)
{
    return buf1 != nullptr && buf2 != nullptr && strcmp(buf1, buf2) == 0;
}
String convertUNIXtoString(uint32_t timestamp) { (void)timestamp; return String(""); }
bool bGATEWAY = false;
bool bVIA = false;

// mheard_functions.cpp definiert diese Arrays (siehe Datei-Kommentar oben);
// direkter extern-Zugriff hier ist testinterne Introspektion, keine
// Wiederholung des NC-02-Antipatterns aus via_functions.cpp/web_functions.cpp.
extern char mheardCalls[MAX_MHEARD][10];
extern char mheardPathCalls[MAX_MHPATH][10];
extern unsigned char mheardPathBuffer1[MAX_MHPATH][52];
extern uint8_t mheardPathLen[MAX_MHPATH];

// ------------------------------------------------------------ Testaufbau

// Baut eine wohlgeformte mheardLine mit einem gueltigen Datum (Jahr >= 2025,
// sonst verwirft updateMheard() den Eintrag ungeprueft -- das ist
// aprs_functions.cpp/getDateString()s eigenes Format, nicht Teil von NC-01).
static void buildLine(struct mheardLine &mh, const char *callsign)
{
    initMheardLine(mh);
    mcSet(mh.mh_callsign, sizeof(mh.mh_callsign), callsign);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-08-31");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "12:00:00");
    mh.mh_payload_type = '!';
    mh.mh_hw = BOARD_HARDWARE;
    mh.mh_mod = 136;
    mh.mh_rssi = -90;
    mh.mh_snr = -5;
    mh.mh_dist = 1.0;
    mh.mh_path_len = 0;
    mh.mh_mesh = 0;
    mh.mh_ncount = 0;
}

// Baut eine mheardLine fuer updateHeyPath() (NC-02 Pfad-Tabelle): ein
// gueltiges Datum (siehe buildLine oben) und ein mh_sourcepath mit genau
// einem Komma, damit updateHeyPath()s "ips = indexOf(',')+1 > 0"-Check
// durchlaeuft und der Eintrag tatsaechlich in mheardPathCalls[]/
// mheardPathEpoch[]/mheardPathMillis[] landet.
static void buildPathLine(struct mheardLine &mh, const char *sourcecall)
{
    initMheardLine(mh);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-08-31");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "12:00:00");
    mcSet(mh.mh_sourcecallsign, sizeof(mh.mh_sourcecallsign), sourcecall);
    mcSet(mh.mh_sourcepath, sizeof(mh.mh_sourcepath), sourcecall);
    mcAppend(mh.mh_sourcepath, sizeof(mh.mh_sourcepath), ",DB0XXX-12");
    mh.mh_destinationpath[0] = 0;
    mh.mh_path_len = 0;
}

void setUp(void)
{
    // Isoliert jeden Testfall: initMheard() nullt alle globalen
    // Ringbuffer-Arrays UND mheardWrite (mheard_functions.cpp), die sonst
    // ueber Testfaelle hinweg im selben Prozess bestehen blieben.
    initMheard();
    mc_test_set_millis(0);
}

void tearDown(void) {}

// ------------------------------------------------------------ Testfaelle

// Fall 1: ungueltige Wanduhr (getUnixClock() oben konstant kaputt) + drei
// frisch gehoerte Eintraege -> getMheardCount() > 0.
//
// VOR dem Fix: mheardEpoch[i] = getUnixClock() = ULONG_MAX bei jedem
// updateMheard(). getMheardCount() pruefte
// "(mheardEpoch[i]+3600) > getUnixClock()" == "(ULONG_MAX+3600) > ULONG_MAX".
// Die Addition ueberlaeuft (wrap auf 3599, der Maximalwert-jeder-Breite-
// Trick aus dem Datei-Kommentar), 3599 > ULONG_MAX ist falsch -- kein
// Eintrag zaehlt, exakt das NCNT-0-Symptom. Nach dem Fix zaehlt
// getMheardCount() ueber mheardMillis[] (millis()-basiert), unabhaengig von
// getUnixClock().
static void test_ungueltige_wanduhr_frische_eintraege_zaehlen(void)
{
    mc_test_set_millis(100000);

    struct mheardLine mh;
    buildLine(mh, "DK5EN-1");
    updateMheard(mh, 0);
    buildLine(mh, "DK5EN-2");
    updateMheard(mh, 0);
    buildLine(mh, "DK5EN-3");
    updateMheard(mh, 0);

    TEST_ASSERT_EQUAL_INT(3, getMheardCount());
}

// Fall 2: ein Eintrag wird > 1h (monoton) alt -> nicht mehr gezaehlt, ein
// zweiter, frisch hinzugekommener Eintrag bleibt gezaehlt. Belegt, dass die
// Unterscheidung "alt vs. frisch" wirklich ueber die verstrichene Zeit
// laeuft (nicht bloss "immer 0" oder "immer alles").
static void test_eintrag_ueber_eine_stunde_alt_zaehlt_nicht_mehr(void)
{
    mc_test_set_millis(0);

    struct mheardLine mh;
    buildLine(mh, "DK5EN-1");
    updateMheard(mh, 0);

    TEST_ASSERT_EQUAL_INT(1, getMheardCount());

    // knapp ueber 1h weiter -- der einzelne Eintrag faellt aus dem
    // Zaehlfenster, bleibt aber (< 12h) in der Tabelle stehen (showMHeard()
    // wuerde ihn noch anzeigen; das ist ein separates Fenster, siehe unten).
    mc_test_advance_millis(60UL * 60UL * 1000UL + 1000UL);
    TEST_ASSERT_EQUAL_INT(0, getMheardCount());

    // ein zweiter, jetzt frisch gehoerter Nachbar zaehlt weiterhin normal --
    // der erste (jetzt >1h alte) Eintrag zieht den Count nicht mit runter.
    buildLine(mh, "DK5EN-2");
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_INT(1, getMheardCount());
}

// Fall 3: millis()-Ueberlauf genau ueber die Fensterschwelle hinweg.
// mheardMillis[] ist uint32_t (siehe mheard_functions.cpp-Kommentar) und
// die Vergleiche casten die Differenz explizit auf uint32_t -- das haelt
// den Ueberlauf-Trick auch auf dem nativen 64-Bit-Testhost korrekt (ohne
// den expliziten Cast wuerde die Differenz in 64-Bit-Arithmetik NICHT
// umlaufen, siehe test_millis_rollover/test_main.cpp Verdict Finding 6).
static void test_millis_ueberlauf_ueber_fensterschwelle(void)
{
    // 1000ms vor dem uint32_t-Ueberlauf gehoert.
    mc_test_set_millis((unsigned long)UINT32_MAX - 1000UL);

    struct mheardLine mh;
    buildLine(mh, "DK5EN-1");
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_INT(1, getMheardCount());

    // 2000ms weiter -- physisch nur 2s seit dem Hoeren vergangen, aber
    // millis() ist ueber UINT32_MAX gewrappt (jetzt bei 999). Ein direkter
    // (nicht ueberlaufsicherer) Vergleich wuerde den Eintrag hier faelschlich
    // als uralt/verworfen behandeln.
    mc_test_advance_millis(2000UL);
    TEST_ASSERT_EQUAL_UINT32(999UL, millis());
    TEST_ASSERT_EQUAL_INT(1, getMheardCount());   // weiterhin frisch (nur 2s alt)

    // von dort aus regulaer > 1h weiter (jetzt komplett auf der "nach dem
    // Wrap"-Seite) -- muss wie in Fall 2 sauber aus dem Fenster fallen.
    mc_test_advance_millis(60UL * 60UL * 1000UL + 1000UL);
    TEST_ASSERT_EQUAL_INT(0, getMheardCount());
}

// ---------------------------------------------------- MH-02 (BACKLOG SS3.8o)
//
// updateMheard()s Eviction-Suche initialisierte `imin` mit -1 und wies ihm
// im Vergleichs-Zweig (`if(mheardEpoch[iset] < ulmin) ulmin = ...`) nie einen
// Wert zu -- der "evict den aeltesten Eintrag"-Zweig
// (`if(imin >= 0) ipos=imin;`) war darum toter Code, Eviction fiel immer auf
// den sequentiellen `mheardWrite`-Ring zurueck (Slot 0, 1, 2, ...).
//
// Test-Aufbau, um "evict Slot 0 (Ring)" von "evict Slot MAX_MHEARD-1 (echt
// aeltester)" zu unterscheiden -- ausgenutzt wird updateMheard()s eigene
// Slot-Suche (siehe Kommentar dort): bei leeren Slots wird `inext` in der
// Scan-Schleife bei JEDEM freien Slot ueberschrieben (nicht nur beim
// ersten), die Tabelle fuellt sich also von HINTEN nach VORNE. Der erste
// Aufruf hier landet in Slot MAX_MHEARD-1 und bekommt den KLEINSTEN
// millis()-Stempel (0) -- den groessten Age-Wert, wenn spaeter geprueft
// wird. mheardWrite bleibt waehrenddessen bei 0 (der "inext"-Zweig fasst ihn
// nicht an), darum evicted der Ring-Fallback deterministisch Slot 0.
static void test_eviction_waehlt_monoton_aeltesten_eintrag(void)
{
    mc_test_set_millis(0);

    struct mheardLine mh;
    char callbuf[16];

    // Fuellt MAX_MHEARD-1 Slots, je 1000ms auseinander -- die erste Eintragung
    // ("AGE01", millis=0) landet in Slot MAX_MHEARD-1 und ist die aelteste;
    // die letzte dieser Schleife ("AGE<N>") landet in Slot 1. Slot 0 bleibt frei.
    for (int i = 1; i < MAX_MHEARD; i++)
    {
        snprintf(callbuf, sizeof(callbuf), "AGE%02d", i);
        buildLine(mh, callbuf);
        updateMheard(mh, 0);
        mc_test_advance_millis(1000UL);
    }

    TEST_ASSERT_EQUAL_STRING("AGE01", mheardCalls[MAX_MHEARD - 1]);
    TEST_ASSERT_EQUAL_STRING("", mheardCalls[0]);   // noch frei

    // Belegt den letzten freien Slot (0) -- ab hier ist die Tabelle voll,
    // dieser Eintrag ist der FRISCHESTE (groesster millis()-Stempel bisher).
    buildLine(mh, "NEWFILL");
    updateMheard(mh, 0);
    TEST_ASSERT_EQUAL_STRING("NEWFILL", mheardCalls[0]);

    // Alles bleibt weit unter dem 12h-Prune-Fenster (~30s Gesamtlaufzeit) --
    // die folgende Eviction darf NICHT ueber die "DELETE after 12h"-Schiene
    // laufen, sonst waere sie kein Beweis fuer MH-02.
    mc_test_advance_millis(500UL);

    // Tabelle voll, kein Slot frei/abgelaufen -> erzwingt den Eviction-Pfad.
    // Vor dem Fix: `imin` bleibt -1, Ring-Fallback evicted Slot 0
    // (mheardWrite==0), also "NEWFILL" -- der FRISCHESTE Eintrag, falsch.
    // Nach dem Fix: `imin` zeigt auf Slot MAX_MHEARD-1 ("AGE01", der
    // tatsaechlich aelteste), der wird evicted -- "NEWFILL" bleibt stehen.
    buildLine(mh, "EVICTME");
    updateMheard(mh, 0);

    TEST_ASSERT_EQUAL_STRING("EVICTME", mheardCalls[MAX_MHEARD - 1]);
    TEST_ASSERT_EQUAL_STRING("NEWFILL", mheardCalls[0]);
}

// ---------------------------------------------------- NC-02 (BACKLOG SS3.8o)
//
// mheardPathEpoch[]/getUnixClock() hat im PATH-Ringpuffer (updateHeyPath()s
// "PATH DELETE after 12 Hours"-Check, showPath()) denselben Wrap-Hazard wie
// NC-01 bei mheardEpoch[]/getMheardCount() -- behoben durch das parallele
// mheardPathMillis[] + die exportierten mheardPathFreshMs()-Helfer. Deckt,
// analog zu den beiden obigen NC-01-Faellen, "frisch trotz kaputter Wanduhr"
// und "wird trotzdem korrekt aelter/geloescht" ab -- diesmal fuer die
// PATH-Tabelle und ueber den echten updateHeyPath()-Code, nicht nur den
// Helfer isoliert.
// VOR NC-02 war der Delete-Check
// `(mheardPathEpoch[iset]+(60*60*12)) < getUnixClock()`: bei der (hier
// konstant kaputten) Wanduhr `mheardPathEpoch[iset]=getUnixClock()=ULONG_MAX`,
// also `(ULONG_MAX+43200) < ULONG_MAX` == `43199 < ULONG_MAX` == IMMER wahr --
// ein taufrischer Pfad-Eintrag wurde beim naechsten updateHeyPath()-Aufruf
// geloescht, unabhaengig von der real seit dem Hoeren verstrichenen Zeit.
static void test_pfad_frischer_eintrag_ueberlebt_naechsten_aufruf_trotz_kaputter_wanduhr(void)
{
    mc_test_set_millis(100000);

    struct mheardLine mh;
    buildPathLine(mh, "DK5EN-9");
    updateHeyPath(mh);

    // updateHeyPath()s eigene Slot-Suche (siehe dortiger Kommentar) setzt
    // `inext` nur beim ERSTEN freien Slot -- der erste Aufruf landet also in
    // Slot 0, deterministisch.
    TEST_ASSERT_EQUAL_STRING("DK5EN-9", mheardPathCalls[0]);
    TEST_ASSERT_TRUE(mheardPathFreshMs(0, 60UL * 60UL * 12UL * 1000UL));   // 12h-Fenster

    // Nur 1s spaeter, weit unter dem 12h-Fenster -- ein zweiter, ANDERER
    // Eintrag loest updateHeyPath()s Scan-Schleife (die "PATH DELETE after
    // 12 Hours"-Pruefung) aus. Vor NC-02 haette diese den obigen Eintrag
    // JETZT geloescht (s.o.), obwohl real nur 1s vergangen ist.
    mc_test_advance_millis(1000UL);
    buildPathLine(mh, "OE1XYZ-1");
    updateHeyPath(mh);

    TEST_ASSERT_EQUAL_STRING("DK5EN-9", mheardPathCalls[0]);   // ueberlebt
}

static void test_pfad_eintrag_ueber_zwoelf_stunden_gilt_nicht_mehr_als_frisch_und_wird_geloescht(void)
{
    mc_test_set_millis(0);

    struct mheardLine mh;
    buildPathLine(mh, "DK5EN-9");
    updateHeyPath(mh);
    TEST_ASSERT_EQUAL_STRING("DK5EN-9", mheardPathCalls[0]);

    // Direkter Beweis am exportierten Helfer (NC-02s Kern-Fix): die
    // 12h-Grenze wirkt monoton, unabhaengig von getUnixClock() (hier
    // konstant kaputt, siehe Datei-Kommentar).
    mc_test_advance_millis(60UL * 60UL * 12UL * 1000UL + 1000UL);
    TEST_ASSERT_FALSE(mheardPathFreshMs(0, 60UL * 60UL * 12UL * 1000UL));

    // ... und der echte updateHeyPath()-Code nutzt das: ein zweiter,
    // ANDERER Eintrag loest die Scan-Schleife aus, die den jetzt >12h alten
    // Slot 0 als abgelaufen erkennt und leert -- vor NC-02 war dieser Check
    // an mheardPathEpoch[]+12h vs. der kaputten getUnixClock() gebunden und
    // haette (wrap-bedingt) selbst einen taufrischen Eintrag geloescht.
    buildPathLine(mh, "OE1XYZ-1");
    updateHeyPath(mh);
    TEST_ASSERT_EQUAL_UINT8(0x00, (uint8_t)mheardPathCalls[0][0]);
}

// ---------------------------------------------- R2-04 (mheardLine-Haelfte)
//
// mheardLine.mh_callsign/mh_date/mh_time/mh_sourcecallsign/mh_sourcepath/
// mh_destinationpath/mh_path_payload sind ab hier feste char[] statt
// Arduino-String (aprs_structures.h). Die drei Faelle unten pruefen genau
// die Stellen, an denen das schiefgehen koennte: eine Kuerzung, die
// unbemerkt ueberlaeuft statt sauber an der Feldgrenze zu stoppen; ein
// Datensatz-Rundlauf, der nicht mehr Byte fuer Byte ankommt; und -- der
// eigentliche Anlass fuer R3-13s Ablehnung (BACKLOG SS3.8aj) -- ein
// 6-Hop-Pfad (--maxhop erlaubt 6), der durch den Typwechsel eine NEUE
// Kuerzung vor der bereits bestehenden 51-Zeichen-Grenze von
// mheardPathBuffer1[][52] bekommt.

// Fall (a): ein zu langes mh_sourcepath wird an der Strukturgrenze
// (MC_PATH_LEN, aprs_structures.h) GEKUERZT -- mcSet() (mc_text.h)
// terminiert immer und ueberschreibt nie mehr als dstsz. Das direkt danach
// deklarierte Feld (mh_destinationpath) traegt einen Sentinel, der einen
// Ueberlauf sichtbar machen wuerde.
static void test_ueberlanger_sourcepath_wird_an_der_strukturgrenze_gekuerzt(void)
{
    struct mheardLine mh;
    initMheardLine(mh);

    mcSet(mh.mh_destinationpath, sizeof(mh.mh_destinationpath), "SENTINEL");

    char longpath[300];
    memset(longpath, 'A', sizeof(longpath) - 1);
    longpath[sizeof(longpath) - 1] = 0;

    bool ok = mcSet(mh.mh_sourcepath, sizeof(mh.mh_sourcepath), longpath);

    TEST_ASSERT_FALSE(ok);   // mcSet() meldet die Kuerzung
    TEST_ASSERT_EQUAL_size_t((size_t)MC_PATH_LEN - 1, strlen(mh.mh_sourcepath));
    TEST_ASSERT_EQUAL_STRING("SENTINEL", mh.mh_destinationpath);   // Nachbarfeld unberuehrt
}

// Fall (b): mheardRecordFromLine()/mheardLineFromRecord() muessen einen
// Datensatz unveraendert hin- und zurueckreichen -- genau der Rundweg, den
// updateMheard()s REP-Zweig (mheardLine_save) und sendMheard()/showMHeard()
// bei jedem Lesen nutzen.
static void test_mheardline_record_round_trip_unveraendert(void)
{
    struct mheardLine mh;
    initMheardLine(mh);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-09-16");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "23:59:01");
    mh.mh_payload_type = '@';
    mh.mh_hw = 14;
    mh.mh_mod = 0x83;
    mh.mh_rssi = -97;
    mh.mh_snr = -4;
    mh.mh_dist = 3.4;
    mh.mh_path_len = 5;
    mh.mh_mesh = 1;
    mh.mh_ncount = 7;

    MheardRecord rec;
    mheardRecordFromLine(mh, rec);

    struct mheardLine mh2;
    mheardLineFromRecord(rec, mh2);

    TEST_ASSERT_EQUAL_STRING("2026-09-16", mh2.mh_date);
    TEST_ASSERT_EQUAL_STRING("23:59:01", mh2.mh_time);
    TEST_ASSERT_EQUAL_CHAR('@', mh2.mh_payload_type);
    TEST_ASSERT_EQUAL_UINT8(14, mh2.mh_hw);
    TEST_ASSERT_EQUAL_UINT8(0x83, mh2.mh_mod);
    TEST_ASSERT_EQUAL_INT16(-97, mh2.mh_rssi);
    TEST_ASSERT_EQUAL_INT8(-4, mh2.mh_snr);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.4f, mh2.mh_dist);
    TEST_ASSERT_EQUAL_UINT8(5, mh2.mh_path_len);
    TEST_ASSERT_EQUAL_UINT8(1, mh2.mh_mesh);
    TEST_ASSERT_EQUAL_UINT8(7, mh2.mh_ncount);
}

// Fall (c): ein 6-Hop-Pfad (--maxhop erlaubt 6) uebersteht updateHeyPath()
// unveraendert. mh_sourcepath ist MC_PATH_LEN (121) breit -- weit ueber
// jeder realen Pfadlaenge --, die einzige Kuerzung, die greifen darf, ist
// die bereits bestehende 51-Zeichen-Grenze von mheardPathBuffer1[][52]
// (R3-13 wurde bewusst NICHT umgesetzt, siehe BACKLOG SS3.8aj); dieser
// 6-Hop-Pfad bleibt mit 34 Zeichen deutlich darunter, belegt also, dass der
// Typwechsel selbst KEINE neue, engere Kuerzung einfuehrt.
static void test_sechs_hop_pfad_ueberlebt_updateheypath_unveraendert(void)
{
    mc_test_set_millis(0);

    struct mheardLine mh;
    initMheardLine(mh);
    mcSet(mh.mh_date, sizeof(mh.mh_date), "2026-09-16");
    mcSet(mh.mh_time, sizeof(mh.mh_time), "12:00:00");
    mcSet(mh.mh_sourcecallsign, sizeof(mh.mh_sourcecallsign), "DK5EN-9");

    // 6 Hops insgesamt: DK5EN-9 (Quelle) + 5 Relais.
    mcSet(mh.mh_sourcepath, sizeof(mh.mh_sourcepath),
          "DK5EN-9,OE3A-1,OE3B-2,OE3C-3,OE3D-4,OE3E-5");
    mh.mh_destinationpath[0] = 0;
    mh.mh_path_len = 6;

    updateHeyPath(mh);

    TEST_ASSERT_EQUAL_STRING("DK5EN-9", mheardPathCalls[0]);
    TEST_ASSERT_EQUAL_UINT8(6, mheardPathLen[0] & 0x7F);

    const char *expected_tail = "OE3A-1,OE3B-2,OE3C-3,OE3D-4,OE3E-5";
    TEST_ASSERT_TRUE(strlen(expected_tail) <= 51);   // Testaufbau bleibt selbst unter der bestehenden Grenze
    TEST_ASSERT_EQUAL_STRING(expected_tail, (const char *)mheardPathBuffer1[0]);
}

// ---- Die beiden Textvergleiche, die der Typwechsel still gekippt haette --
//
// Vor R2-04 waren mh_sourcecallsign und mh_destinationpath Arduino-String,
// und `mheardLine.mh_sourcecallsign == meshcom_settings.node_call` bzw.
// `mheardLine.mh_destinationpath == "HG"` verglichen INHALTE (String hat
// dafuer einen operator==). Als char[] waeren beide zu ZEIGERvergleichen
// geworden: zwei verschiedene Arrays sind nie derselbe Zeiger, also immer
// false -- ohne Warnung, ohne Absturz, auf jeder Plattform.
//
// Beide Stellen sind jetzt is_equ() (mheard_functions.cpp:469,638). Diese
// zwei Faelle fallen um, wenn jemand sie wieder auf == zurueckdreht; ohne
// sie merkt es niemand, weil beide Defekte nur STILL falsch sind:
// der Knoten traegt sich selbst in die Pfadtabelle ein, und die
// Gateway-Markierung im GUI verschwindet.

static void test_eigenes_rufzeichen_kommt_nicht_in_die_pfadtabelle(void)
{
    // updateHeyPath() schliesst das eigene Rufzeichen aus. Wird der Vergleich
    // zum Zeigervergleich, greift der Ausschluss nie und der Knoten landet in
    // seiner eigenen PATH-Tabelle.
    mcSet(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "DK5EN-9");

    struct mheardLine mh;
    buildPathLine(mh, "DK5EN-9");          // Quelle == eigenes Rufzeichen
    updateHeyPath(mh);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, mheardPathCalls[0][0],
        "updateHeyPath() hat das EIGENE Rufzeichen in die Pfadtabelle "
        "geschrieben -- der Ausschluss (mheard_functions.cpp:469) vergleicht "
        "wieder Zeiger statt Inhalte");

    // Gegenprobe: ein fremdes Rufzeichen MUSS eingetragen werden, sonst
    // besteht der Test auch dann, wenn updateHeyPath() gar nichts mehr tut.
    struct mheardLine other;
    buildPathLine(other, "OE1XYZ-1");
    updateHeyPath(other);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(0x00, mheardPathCalls[0][0],
        "Gegenprobe gescheitert: ein FREMDES Rufzeichen wurde ebenfalls nicht "
        "eingetragen -- dann prueft der Fall oben nichts");
}

static void test_hg_ziel_setzt_das_gateway_bit_im_pfadlaengenfeld(void)
{
    // mh_destinationpath == "HG" markiert einen ueber ein Gateway gehoerten
    // Pfad, indem mheardPathLen das oberste Bit bekommt. Als Zeigervergleich
    // waere die Markierung lautlos verschwunden.
    mcSet(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "DK5EN-1");

    struct mheardLine mh;
    buildPathLine(mh, "OE1XYZ-1");
    mcSet(mh.mh_destinationpath, sizeof(mh.mh_destinationpath), "HG");
    mh.mh_path_len = 3;
    updateHeyPath(mh);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(3 | 0x80), mheardPathLen[0],
        "Ziel \"HG\" hat das Gateway-Bit nicht gesetzt -- der Vergleich in "
        "mheard_functions.cpp:638 prueft wieder Zeiger statt Inhalte");

    // Gegenprobe mit einem anderen Ziel: dasselbe Feld OHNE das Bit.
    initMheard();
    struct mheardLine plain;
    buildPathLine(plain, "OE1XYZ-2");
    mcSet(plain.mh_destinationpath, sizeof(plain.mh_destinationpath), "*");
    plain.mh_path_len = 3;
    updateHeyPath(plain);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, mheardPathLen[0],
        "Ein Ziel, das NICHT \"HG\" ist, hat trotzdem das Gateway-Bit "
        "bekommen -- dann sagt der Fall oben nichts ueber den Vergleich aus");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ungueltige_wanduhr_frische_eintraege_zaehlen);
    RUN_TEST(test_eintrag_ueber_eine_stunde_alt_zaehlt_nicht_mehr);
    RUN_TEST(test_millis_ueberlauf_ueber_fensterschwelle);
    RUN_TEST(test_eviction_waehlt_monoton_aeltesten_eintrag);
    RUN_TEST(test_pfad_frischer_eintrag_ueberlebt_naechsten_aufruf_trotz_kaputter_wanduhr);
    RUN_TEST(test_pfad_eintrag_ueber_zwoelf_stunden_gilt_nicht_mehr_als_frisch_und_wird_geloescht);
    RUN_TEST(test_ueberlanger_sourcepath_wird_an_der_strukturgrenze_gekuerzt);
    RUN_TEST(test_eigenes_rufzeichen_kommt_nicht_in_die_pfadtabelle);
    RUN_TEST(test_hg_ziel_setzt_das_gateway_bit_im_pfadlaengenfeld);
    RUN_TEST(test_mheardline_record_round_trip_unveraendert);
    RUN_TEST(test_sechs_hop_pfad_ueberlebt_updateheypath_unveraendert);
    return UNITY_END();
}
