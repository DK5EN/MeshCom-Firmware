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
#include <mheard_functions.h>
#include <nrf52/WisBlock-API.h>

// ---- Link-Stubs fuer aprs_functions.cpp/mheard_functions.cpp/via_functions.cpp
// (siehe Datei-Kommentar oben -- bewusst kein #include von
// parser_link_stubs.h, weil getUnixClock() hier anders sein muss)
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

// ------------------------------------------------------------ Testaufbau

// Baut eine wohlgeformte mheardLine mit einem gueltigen Datum (Jahr >= 2025,
// sonst verwirft updateMheard() den Eintrag ungeprueft -- das ist
// aprs_functions.cpp/getDateString()s eigenes Format, nicht Teil von NC-01).
static void buildLine(struct mheardLine &mh, const char *callsign)
{
    initMheardLine(mh);
    mh.mh_callsign = callsign;
    mh.mh_date = "2026-08-31";
    mh.mh_time = "12:00:00";
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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ungueltige_wanduhr_frische_eintraege_zaehlen);
    RUN_TEST(test_eintrag_ueber_eine_stunde_alt_zaehlt_nicht_mehr);
    RUN_TEST(test_millis_ueberlauf_ueber_fensterschwelle);
    return UNITY_END();
}
