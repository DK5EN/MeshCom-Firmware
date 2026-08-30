// Native Testsuite fuer decodeMHeard() -- PT-01 (BACKLOG SS3.8j): der
// MHeard-Binaerdatensatz-Parser hatte bislang keine eigene Suite.
//
// decodeMHeard() liest EINEN 60-Byte-Ringbuffer-Slot (mheardBuffer[i], siehe
// src/mheard_functions.cpp) zurueck in struct mheardLine. Der Slot ist ein
// pipe-getrenntes Textformat, kein Binaerformat im engeren Sinn -- der
// EINZIGE Schreiber ist updateMheard() (src/mheard_functions.cpp, aktuell
// Zeile 334f.):
//
//   snprintf(cBuffer, sizeof(cBuffer),
//            "%s|%s|%c|%i|%u|%i|%i|%.1lf|%i|%i|%i|",
//            date, time, payload_type, hw, mod, rssi, snr, dist,
//            path_len, mesh, ncount);
//
// buildMheardRecord() unten spiegelt exakt dieses Format -- das ist der
// "echte Encoder" im Sinne des Auftrags, auch wenn er hier nachgebildet statt
// direkt aufgerufen wird: updateMheard() selbst zieht getUnixClock(),
// JsonDocument/BLE-Frame-Versand und Ringbuffer-Slot-Suche mit, die fuer
// einen Parser-Test nicht noetig sind.
//
// decodeMHeard() scannt NUR die ersten 55 der 60 Puffer-Bytes (siehe
// src/mheard_functions.cpp, `for(int iset=0; iset<55; iset++)`), und trennt
// Felder ausschliesslich durch das naechste '|'. Zwei Eigenheiten daraus
// werden unten gezielt geprueft, nicht nur der Wohlfall:
//
//   * Case 1/2 (mh_date/mh_time) haengen mit concat() an -- ein fehlendes
//     '|' laesst sie ALLES bis zum Scan-Ende aufsaugen statt sauber
//     abzubrechen.
//   * Case 3 (mh_payload_type) wird bei JEDEM Byte ueberschrieben (kein
//     concat()) -- ein Byte NACH dem eigentlichen Typbyte, aber VOR dem
//     naechsten '|', ersetzt es wieder.
//
//   pio test -e native_parsers -f test_decodemheard

#include <unity.h>

#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <aprs_functions.h>
#include <aprs_structures.h>
#include <mheard_functions.h>
#include <nrf52/WisBlock-API.h>
#include <parser_link_stubs.h>

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp/mheard_functions.cpp/via_functions.cpp
// (env:native_parsers linkt alle drei Parser in jedes der drei Testprogramme,
// siehe test/test_decodemheard/stubs/parser_link_stubs.h)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631 -- int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// mheardBuffer[][60] ist ein globales Array in mheard_functions.cpp, aber
// mheard_functions.h deklariert es nicht extern (nur die Funktionen, die
// darauf zugreifen). decodeMHeard()s eigene Parametergroesse
// (sizeof(mheardBuffer[0])) braucht diese Vorwaertsdeklaration, um denselben
// Slot-Typ zu kennen, ohne die Groesse (60) hier nochmal als Magic Number zu
// wiederholen.
extern unsigned char mheardBuffer[][60];

// Baut einen 60-Byte-Slot exakt wie updateMheard() ihn schreibt (siehe
// Datei-Kommentar). buf muss sizeof(mheardBuffer[0]) == 60 Byte gross sein.
// Fuellt den Rest des Slots mit 0x00 -- das ist, was memcpy(mheardBuffer[ipos],
// cBuffer, sizeof(cBuffer)) fuer jedes ungeschriebene Byte HINTER dem von
// snprintf() terminierten String effektiv auch tut (snprintf nullterminiert;
// alles danach bleibt in der echten updateMheard() zwar Stack-Muell statt
// Nullen -- fuer einen wohlgeformten Datensatz macht das keinen Unterschied,
// weil decodeMHeard() beim letzten '|' ohnehin fertig ist).
static void buildMheardRecord(unsigned char out[sizeof(mheardBuffer[0])],
                               const char *date, const char *time, char ptype,
                               int hw, unsigned mod, int rssi, int snr,
                               double dist, int path_len, int mesh, int ncount)
{
    char buf[sizeof(mheardBuffer[0])];
    memset(buf, 0x00, sizeof(buf));
    snprintf(buf, sizeof(buf), "%s|%s|%c|%i|%u|%i|%i|%.1lf|%i|%i|%i|",
             date, time, ptype, hw, mod, rssi, snr, dist, path_len, mesh, ncount);
    memcpy(out, buf, sizeof(buf));
}

// ------------------------------------------------------------ Testfaelle

// Wohlgeformter Datensatz, per echtem Encoder-Format gebaut -- deckt alle
// elf Felder ab, die decodeMHeard() aus dem Slot liest.
static void test_wohlgeformter_datensatz_alle_elf_felder(void)
{
    unsigned char slot[sizeof(mheardBuffer[0])];
    buildMheardRecord(slot, "20260830", "101112", '!', 9, 136, -95, -3, 12.7, 3, 1, 5);

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_STRING("20260830", mh.mh_date.c_str());
    TEST_ASSERT_EQUAL_STRING("101112", mh.mh_time.c_str());
    TEST_ASSERT_EQUAL_CHAR('!', mh.mh_payload_type);
    TEST_ASSERT_EQUAL_UINT8(9, mh.mh_hw);
    TEST_ASSERT_EQUAL_UINT8(136, mh.mh_mod);
    TEST_ASSERT_EQUAL_INT16(-95, mh.mh_rssi);
    TEST_ASSERT_EQUAL_INT8(-3, mh.mh_snr);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 12.7, mh.mh_dist);
    TEST_ASSERT_EQUAL_UINT8(3, mh.mh_path_len);
    TEST_ASSERT_EQUAL_UINT8(1, mh.mh_mesh);
    TEST_ASSERT_EQUAL_INT(5, mh.mh_ncount);

    // decodeMHeard() setzt NUR die elf Felder oben -- Callsign/Pfad kommen
    // aus separaten Ringbuffer-Arrays (mheardCalls[]/mheardPathBuffer1[]),
    // nicht aus diesem Slot. initMheardLine()s Default (leerer String) bleibt.
    TEST_ASSERT_EQUAL_STRING("", mh.mh_callsign.c_str());
    TEST_ASSERT_EQUAL_STRING("", mh.mh_sourcecallsign.c_str());
}

// Negativer RSSI/SNR bleibt vorzeichenbehaftet (kein Wrap auf unsigned).
static void test_negativer_rssi_und_snr(void)
{
    unsigned char slot[sizeof(mheardBuffer[0])];
    buildMheardRecord(slot, "20260830", "120000", '@', 43, 136, -128, -20, 0.0, 0, 0, 0);

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_INT16(-128, mh.mh_rssi);
    TEST_ASSERT_EQUAL_INT8(-20, mh.mh_snr);
}

// Zweiter Payload-Type-Wert (':' = MSG_TYPE_TEXT) -- belegt, dass case 3
// nicht an '!' gebunden ist.
static void test_payload_type_text(void)
{
    unsigned char slot[sizeof(mheardBuffer[0])];
    buildMheardRecord(slot, "20260830", "130000", ':', 0, 136, -60, 5, 3.2, 4, 0, 2);

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_CHAR(':', mh.mh_payload_type);
}

// PT-01 Finding: fehlt das '|' NACH dem Typbyte (Datensatz endet mitten im
// Feld 4, wie es ein abgeschnittener/beschaedigter Ringbuffer-Slot koennte),
// bleibt itype auf 3 stehen -- und case 3 schreibt (anders als case 1/2, die
// concat()en) bei JEDEM weiteren Byte ueberschreibend auf mh_payload_type.
// Die restlichen Slot-Bytes sind bei einem echten Ringbuffer-Eintrag exakt
// dieselben Nullen wie initMheard()s memset() sie hinterlaesst -- das letzte
// davon "gewinnt": mh_payload_type landet auf 0x00 statt auf dem
// tatsaechlich gesendeten Typbyte '!'. Kein Speicherfehler (Slot ist 60 Byte,
// die Schleife scannt nur 55), aber ein plausibel aussehendes Feld wird
// durch einen abgeschnittenen Datensatz stillschweigend geloescht statt
// erkennbar leer/ungueltig zu bleiben.
static void test_fehlendes_pipe_nach_typbyte_loescht_es(void)
{
    TEST_IGNORE_MESSAGE(
        "PT-01 finding: decodeMHeard()'s case-3 handling (mh_payload_type, "
        "src/mheard_functions.cpp ~L136) overwrites on every scanned byte "
        "instead of stopping at the first one -- a record truncated right "
        "after the type byte (no closing '|') loses it to the zero padding "
        "that follows in a real ring-buffer slot. Not memory-unsafe, not "
        "fixed here.");

    unsigned char slot[sizeof(mheardBuffer[0])];
    memset(slot, 0x00, sizeof(slot));
    // "20260830|101112|!" gefolgt von Nullen -- KEIN weiteres '|' im Slot.
    memcpy(slot, "20260830|101112|!", 18);

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_CHAR(0x00, mh.mh_payload_type);   // NICHT '!'
}

// PT-01 Finding: fehlt das '|' NACH dem Datum (case 1 haengt per concat()
// an), saugt mh_date jedes weitere Byte im 55-Byte-Scanfenster auf --
// einschliesslich eingebetteter NUL-Bytes aus der Nullpolsterung. Ein String
// mit eingebetteten NUL ist fuer die Arduino-String-Klasse selbst
// unproblematisch (laengenbasiert, kein C-String), aber jeder Aufrufer, der
// mh_date.c_str() spaeter als C-String behandelt (z.B. fuer eine feste
// Breite formatiert, siehe showMHeard()s "%-10.10s"), sieht nur das erste
// Zeichen bis zum ersten eingebetteten NUL.
static void test_fehlendes_pipe_nach_datum_haengt_alles_an(void)
{
    TEST_IGNORE_MESSAGE(
        "PT-01 finding: decodeMHeard()'s case-1 handling (mh_date, "
        "src/mheard_functions.cpp ~L133) has no closing delimiter to stop "
        "at if the record has no '|' at all -- it concatenates every byte "
        "in the 55-byte scan window, including embedded NUL padding, into "
        "mh_date instead of leaving it at the intended value. Not "
        "memory-unsafe, not fixed here.");

    unsigned char slot[sizeof(mheardBuffer[0])];
    memset(slot, 0x00, sizeof(slot));
    memcpy(slot, "20260830", 8);   // kein '|' im ganzen Slot

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_size_t(55u, mh.mh_date.length());   // haette 8 sein sollen
}

// Datensatz mit fehlenden Endfeldern: date/time/type UND die ersten paar
// Zahlenfelder sind sauber durch '|' abgeschlossen, aber der Datensatz
// bricht danach ab (kein weiteres '|' fuer path_len/mesh/ncount). Anders als
// bei case 1-3 ist das fuer die Zahlenfelder SICHER: die case-4..11-Zweige
// haengen nur an eine temporaere String-Variable (strdec) an und weisen sie
// dem mheardLine-Feld erst BEIM naechsten '|' zu -- ohne das '|' bleibt der
// initMheardLine()-Default (0) stehen, statt einen Teil-/Muellwert zu uebernehmen.
static void test_datensatz_mit_fehlenden_endfeldern(void)
{
    unsigned char slot[sizeof(mheardBuffer[0])];
    memset(slot, 0x00, sizeof(slot));
    // date|time|type|hw|mod|rssi| -- danach bricht der Datensatz ab (kein
    // snr/dist/path_len/mesh/ncount mehr).
    memcpy(slot, "20260830|101112|!|9|136|-95|", 29);

    struct mheardLine mh;
    decodeMHeard(slot, mh);

    TEST_ASSERT_EQUAL_STRING("20260830", mh.mh_date.c_str());
    TEST_ASSERT_EQUAL_STRING("101112", mh.mh_time.c_str());
    TEST_ASSERT_EQUAL_CHAR('!', mh.mh_payload_type);
    TEST_ASSERT_EQUAL_UINT8(9, mh.mh_hw);
    TEST_ASSERT_EQUAL_UINT8(136, mh.mh_mod);
    // rssi selbst fehlt sein abschliessendes '|' NICHT (der Slot endet exakt
    // nach "-95|"), ist also vollstaendig gelesen:
    TEST_ASSERT_EQUAL_INT16(-95, mh.mh_rssi);
    // snr/dist/path_len/mesh/ncount wurden nie durch ein '|' abgeschlossen
    // -- initMheardLine()-Default (0) bleibt stehen, kein Muellwert:
    TEST_ASSERT_EQUAL_INT8(0, mh.mh_snr);
    TEST_ASSERT_EQUAL_UINT8(0, mh.mh_path_len);
    TEST_ASSERT_EQUAL_UINT8(0, mh.mh_mesh);
    TEST_ASSERT_EQUAL_INT(0, mh.mh_ncount);
}

// Garbage-Bytes: ein Slot aus lauter 0xFF (kein einziges '|', keine
// druckbaren ASCII-Zeichen) darf nicht abstuerzen. Dieselbe Konsequenz wie
// beim fehlenden Pipe nach dem Datum (case 1 saugt alles auf), hier mit
// nicht-druckbaren statt Null-Bytes.
static void test_garbage_bytes_stuerzt_nicht_ab(void)
{
    unsigned char slot[sizeof(mheardBuffer[0])];
    memset(slot, 0xFF, sizeof(slot));

    struct mheardLine mh;
    decodeMHeard(slot, mh);   // darf nicht abstuerzen/haengen

    TEST_ASSERT_EQUAL_size_t(55u, mh.mh_date.length());   // siehe Finding oben: alles landet in mh_date
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_wohlgeformter_datensatz_alle_elf_felder);
    RUN_TEST(test_negativer_rssi_und_snr);
    RUN_TEST(test_payload_type_text);
    RUN_TEST(test_fehlendes_pipe_nach_typbyte_loescht_es);
    RUN_TEST(test_fehlendes_pipe_nach_datum_haengt_alles_an);
    RUN_TEST(test_datensatz_mit_fehlenden_endfeldern);
    RUN_TEST(test_garbage_bytes_stuerzt_nicht_ab);
    return UNITY_END();
}
