// Native Testsuite fuer decodeAPRSPOS() -- PT-01 (BACKLOG SS3.8j): der
// Positions-Payload-Parser hatte bislang keine eigene Suite.
//
// decodeAPRSPOS() liest den Teil einer APRS-Payload, der NACH dem
// Typbyte/Zeitstempel steht -- z.B. "4825.35N\01147.19E-Marzling#Werner/R=9;"
// -- und zerlegt ihn in struct aprsPosition: Breite/Laenge (roh im NMEA-
// artigen DDMM.mm-Format UND als Dezimalgrad lat_d/lon_d), das
// APRS-Symbolpaar (aprs_group/aprs_symbol), den Freitextkommentar (pos_atxt)
// und die optionalen Erweiterungsfelder /B= (Batterie), /A= (Hoehe) etc.
//
// Drei der Vektoren unten (VEC_F001/F003/F011) sind echte, im Korpus
// eingefrorene decodeAPRS()-Payloads (test/test_aprs_corpus/golden.txt,
// Frames f001/f003/f011) -- keine Erfindung dieser Suite. f003 ist der
// einzige Korpus-Frame mit sowohl /B= als auch /A=, f011 der einzige mit
// leerem Kommentartext (Symbol direkt gefolgt von '/'). Erwartungswerte
// fuer lat_d/lon_d sind von Hand aus der DDMM.mm-Umrechnung nachgerechnet
// (siehe Kommentar je Testfall).
//
//   pio test -e native_parsers -f test_decodeaprspos

#include <unity.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <aprs_structures.h>
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

// ---------------------------------------------------------------- Vektoren

// f001 (golden.txt): Positionsbake DL2JA-1 -- Gruppe '\', Symbol '-', KEIN
// /B=/A=. lat_d = 48 + 25.35/60 = 48.4225, lon_d = 11 + 47.19/60 = 11.786500
static const char *VEC_F001 = "4825.35N\\01147.19E-Marzling#Werner/R=9;";

// f003 (golden.txt): Positionsbake DK5EN-90 -- der einzige Korpus-Frame mit
// sowohl /B= als auch /A=. Gruppe '/', Symbol '#'.
// lat_d = 48 + 24.43/60 = 48.4071666..., lon_d = 11 + 44.40/60 = 11.740000
// pos_atxt endet am ERSTEN '/' nach dem Symbol: "github.com" (danach
// "/dk5en/mcapp#Martin/B=099/A=001657/...").
static const char *VEC_F003 =
    "4824.43N/01144.40E#github.com/dk5en/mcapp#Martin/B=099/A=001657/N1/R=20;232;262;9;26244;26244;";

// f011 (golden.txt): Positionsbake DK5EN-91 -- Symbol direkt gefolgt von
// '/', also LEERER Kommentartext. lat_d = 48 + 25.00/60 = 48.41666...,
// lon_d = 11 + 45.46/60 = 11.757666...
static const char *VEC_F011 = "4825.00N/01145.46E#/B=057/N1";

// ------------------------------------------------------------ Testfaelle

static void test_f003_battery_und_altitude(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_F003, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 4824.43, pos.lat);
    TEST_ASSERT_EQUAL_CHAR('N', pos.lat_c);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 1144.40, pos.lon);
    TEST_ASSERT_EQUAL_CHAR('E', pos.lon_c);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);
    TEST_ASSERT_EQUAL_CHAR('#', pos.aprs_symbol);
    TEST_ASSERT_EQUAL_STRING("github.com", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_INT(99, pos.bat);
    TEST_ASSERT_EQUAL_INT(1657, pos.alt);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 48.407166, pos.lat_d);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 11.740000, pos.lon_d);
}

static void test_f011_kein_battery_leerer_kommentar(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_F011, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);
    TEST_ASSERT_EQUAL_CHAR('#', pos.aprs_symbol);
    TEST_ASSERT_EQUAL_STRING("", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_INT(57, pos.bat);
    TEST_ASSERT_EQUAL_INT(0, pos.alt);   // kein /A= im Frame -- initAPRSPOS-Default bleibt
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 48.416666, pos.lat_d);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 11.757666, pos.lon_d);
}

static void test_f001_gruppe_backslash_kein_ba(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_F001, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('\\', pos.aprs_group);
    TEST_ASSERT_EQUAL_CHAR('-', pos.aprs_symbol);
    TEST_ASSERT_EQUAL_STRING("Marzling#Werner", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_INT(0, pos.bat);
    TEST_ASSERT_EQUAL_INT(0, pos.alt);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 48.4225, pos.lat_d);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 11.786500, pos.lon_d);
}

// Synthetischer Vektor (kein Korpus-Frame: die Bench-Flotte liegt auf der
// Nordhalbkugel/oestlich von Greenwich) -- deckt die S/W-Haelften der
// Hemisphaerenpruefung ab, die kein realer Mitschnitt liefert.
static void test_hemisphaere_sued_west(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS("3382.12S/15112.34W>Sydney", pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('S', pos.lat_c);
    TEST_ASSERT_EQUAL_CHAR('W', pos.lon_c);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);
    TEST_ASSERT_EQUAL_CHAR('>', pos.aprs_symbol);
    // decodeAPRSPOS() rechnet lat_d/lon_d nur nach Betrag um -- die
    // Hemisphaere (S/W => negativ) muss der Aufrufer selbst aus lat_c/lon_c
    // ableiten. Das ist bestehendes Verhalten, keine neue Erwartung dieser
    // Suite; hier nur dokumentiert, damit ein kuenftiger Umbau ihn nicht
    // versehentlich als Bug "korrigiert" und damit den Aufrufer bricht.
    // lat_d = 33 + 82.12/60 = 34.368666..., lon_d = 151 + 12.34/60 = 151.205666...
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 34.368666, pos.lat_d);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 151.205666, pos.lon_d);
}

// Leerer Payload: darf nicht abstuerzen, muss 0x01 liefern und alle Felder
// auf dem initAPRSPOS()-Default belassen (lat_c/aprs_group etc. werden nur
// im Erfolgsfall der inneren Schleifen gesetzt).
static void test_leerer_payload(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS("", pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_FLOAT(0.0, pos.lat);
    TEST_ASSERT_EQUAL_CHAR(0x00, pos.lat_c);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);    // initAPRSPOS()-Default, nie erreicht
    TEST_ASSERT_EQUAL_CHAR('&', pos.aprs_symbol);   // initAPRSPOS()-Default, nie erreicht
}

// Abgeschnittener Payload: kein 'N'/'S' im ganzen String (Frame endete
// mitten in der Breitenangabe). Darf nicht abstuerzen, muss 0x01 liefern
// und lat_c auf dem Default belassen -- derselbe Zustand wie beim leeren
// Payload, weil die aeussere Schleife nie auf N/S trifft.
static void test_abgeschnittener_payload_ohne_hemisphaere(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS("482", pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR(0x00, pos.lat_c);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);
}

// Ueberlanger Payload: 2000 Zeichen ohne 'N'/'S' -- die aeussere Schleife
// (kein Abbruch ausser bei N/S oder ipt>10) darf dabei nicht abstuerzen oder
// haengen, sondern muss linear terminieren. Seit der PT-01-Finding-1-Fix
// (siehe test_ipt_notbremse_ohne_hemisphaere_wird_abgelehnt unten) ist eine
// solche Position ohne echtes Hemisphaerenbyte kein Erfolg mehr -- 0x00 statt
// 0x01, initAPRSPOS()-Defaults bleiben stehen.
static void test_ueberlanger_payload_ohne_hemisphaere_terminiert(void)
{
    std::string huge(2000, '4');
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(String(huge), pos);

    TEST_ASSERT_EQUAL_UINT16(0x00, r);
}

// PT-01 Finding 1 (FIXED): die "ipt>10"-Notbremse derselben Schleife ist
// KEINE Erkennung von "kein N/S gefunden" -- sie las das Byte an der
// aktuellen Position vormals unbesehen als lat_c/aprs_group, auch wenn dort
// gar kein 'N'/'S' stand. decodeAPRSPOS() prueft jetzt das Byte am Cutoff:
// steht dort kein echtes 'N'/'S' (bzw. 'W'/'E' in der Laengengrad-Schleife),
// liefert die Funktion 0x00 und laesst alle Felder auf dem
// initAPRSPOS()-Default -- statt eines plausibel aussehenden, aber
// fabrizierten lat_c und eines 11-stelligen Fantasiewerts fuer lat. Bei
// diesem Vektor (2000x '4', keine Hemisphaere im Frame) greift die Notbremse
// in der Breitengrad-Schleife; die Laengengrad-Schleife wird dank des
// fruehen return gar nicht erst erreicht.
static void test_ipt_notbremse_ohne_hemisphaere_wird_abgelehnt(void)
{
    std::string huge(2000, '4');
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(String(huge), pos);

    TEST_ASSERT_EQUAL_UINT16(0x00, r);
    TEST_ASSERT_EQUAL_CHAR(0x00, pos.lat_c);       // initAPRSPOS()-Default, keine fabrizierte Hemisphaere
    TEST_ASSERT_EQUAL_FLOAT(0.0, pos.lat);         // initAPRSPOS()-Default, kein 11-stelliger Fantasiewert
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);   // initAPRSPOS()-Default
}

// Ueberlanger Kommentartext: pos_atxt ist auf 25 Zeichen (ipt<25) gedeckelt,
// unabhaengig davon, wie lang der Text im Payload tatsaechlich ist -- der
// Puffer dahinter (cConcat1[UDP_TX_BUF_SIZE]) wird also nie annaehernd
// ausgeschoepft. 300 'A' vor dem impliziten Leerzeichen-Terminator
// (PayloadBuffer.concat(" ") am Funktionsanfang).
static void test_ueberlanger_kommentartext_wird_gekappt(void)
{
    std::string atxt(300, 'A');
    String payload = "4825.35N/01147.19E-";
    payload.concat(String(atxt));

    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(payload, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_size_t(25u, pos.pos_atxt.length());
    TEST_ASSERT_EQUAL_STRING(std::string(25, 'A').c_str(), pos.pos_atxt.c_str());
}

// Steuerzeichen mitten in der Breitenangabe: kein Crash, sscanf("%lf", ...)
// bricht am ersten nicht-numerischen Byte ab -- die Ziffern DAVOR werden
// noch ausgewertet (0x01 = Steuerbyte, hier zwischen "48" und "25.35").
static void test_steuerzeichen_in_breitenangabe(void)
{
    String payload("48");
    payload.concat('\x01');
    payload.concat("25.35N/01147.19E-x");

    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(payload, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('N', pos.lat_c);   // Hemisphaerenbyte wird trotzdem gefunden ...
    TEST_ASSERT_EQUAL_FLOAT(48.0, pos.lat);  // ... aber sscanf() liest nur bis zum Steuerbyte
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_f003_battery_und_altitude);
    RUN_TEST(test_f011_kein_battery_leerer_kommentar);
    RUN_TEST(test_f001_gruppe_backslash_kein_ba);
    RUN_TEST(test_hemisphaere_sued_west);
    RUN_TEST(test_leerer_payload);
    RUN_TEST(test_abgeschnittener_payload_ohne_hemisphaere);
    RUN_TEST(test_ueberlanger_payload_ohne_hemisphaere_terminiert);
    RUN_TEST(test_ipt_notbremse_ohne_hemisphaere_wird_abgelehnt);
    RUN_TEST(test_ueberlanger_kommentartext_wird_gekappt);
    RUN_TEST(test_steuerzeichen_in_breitenangabe);
    return UNITY_END();
}
