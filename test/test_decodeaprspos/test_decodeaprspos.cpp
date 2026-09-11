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
// Die Kommentar/Name-Region endet erst am ERSTEN /X=-Token nach dem Symbol
// ("/B=099"), nicht an einem beliebigen '/' -- "/dk5en" und "/mcapp" sind
// keine Token (Kleinbuchstabe nach '/'), bleiben also Teil der Region.
// Letztes '#' in der Region trennt Kommentar "github.com/dk5en/mcapp" von
// Name "Martin" (docs/architecture/11-wire-format.md §1.8.1).
static const char *VEC_F003 =
    "4824.43N/01144.40E#github.com/dk5en/mcapp#Martin/B=099/A=001657/N1/R=20;232;262;9;26244;26244;";

// f011 (golden.txt): Positionsbake DK5EN-91 -- Symbol direkt gefolgt von
// '/', also LEERER Kommentartext. lat_d = 48 + 25.00/60 = 48.41666...,
// lon_d = 11 + 45.46/60 = 11.757666...
static const char *VEC_F011 = "4825.00N/01145.46E#/B=057/N1";

// ---- /D= (MCP23017 Port A, GPA0 zuerst) -- neue Vektoren fuer die
// Digital-Erweiterung. VEC_F003 dient unveraendert als "kein /D="-Fall.

// f003-Tail mit eingefuegtem /D= zwischen /A= und /N1 -- bat/alt muessen
// trotz des neuen Tokens weiterhin korrekt geparst werden.
static const char *VEC_DIN_ZWISCHEN_A_UND_N =
    "4824.43N/01144.40E#github.com/dk5en/mcapp#Martin/B=099/A=001657/D=01001100/N1/R=20;232;262;9;26244;26244;";

// /D= als letztes Token des Payloads (kein abschliessendes '/') -- die
// Funktion haengt intern ein Leerzeichen an, das als Terminator wirkt.
static const char *VEC_DIN_7_ZEICHEN = "4825.00N/01145.46E#/D=0100110";
static const char *VEC_DIN_9_ZEICHEN = "4825.00N/01145.46E#/D=010011001";
static const char *VEC_DIN_MIT_BUCHSTABE = "4825.00N/01145.46E#/D=0100110x";
static const char *VEC_DIN_LETZTES_TOKEN = "4825.00N/01145.46E#/D=11111111";

// /D= direkt nach dem Symbol bei leerem Kommentartext (wie VEC_F011),
// gefolgt von /B=.
static const char *VEC_DIN_LEERER_KOMMENTAR = "4825.00N/01145.46E#/D=00000000/B=057/N1";

// ---- #name-Split-Vektoren (docs/architecture/11-wire-format.md §1.8.1) --
// Kontrakt: die Region laeuft bis zum ERSTEN /X=-Token (nicht bis zum
// ersten '/' oder Leerzeichen); der Name ist der Text nach dem LETZTEN '#'
// in der Region, alles davor ist der Kommentar.

// (a) Leerzeichen unmittelbar vor dem Token -- weder das Leerzeichen selbst
// noch dessen Naehe zum '/' darf die Region vorzeitig beenden oder das
// Leerzeichen abschneiden. Kein '#', also kein Name.
static const char *VEC_LEERZEICHEN_VOR_TOKEN = "4825.00N/01145.46E#MeshCom Zeltweg /B=050";

// (b) Kommentar enthaelt selbst ein '#' ("Net#1 Info"); erst das LETZTE '#'
// vor dem /B=-Token trennt den Namen ("Werner") ab.
static const char *VEC_HASH_IM_KOMMENTAR = "4825.00N/01145.46E#Net#1 Info#Werner/B=060";

// (c) Symbol ist '#'; die Region selbst beginnt sofort mit einem weiteren
// '#' -- Kommentar leer, kompletter Regionsinhalt ist der Name.
static const char *VEC_NUR_NAME = "4825.00N/01145.46E##Name/B=070";

// (d) Kein '#' irgendwo in der Region -- pos_name bleibt leer, der ganze
// Regionsinhalt ist der Kommentar (Gegenstueck zu VEC_F001, das denselben
// Payload MIT "#Werner" verwendet).
static const char *VEC_OHNE_HASH = "4825.35N\\01147.19E-Marzling/R=9;";

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
    TEST_ASSERT_EQUAL_STRING("github.com/dk5en/mcapp", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("Martin", pos.pos_name.c_str());
    TEST_ASSERT_EQUAL_INT(99, pos.bat);
    TEST_ASSERT_EQUAL_INT(1657, pos.alt);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 48.407166, pos.lat_d);
    TEST_ASSERT_FLOAT_WITHIN(0.0001, 11.740000, pos.lon_d);
    TEST_ASSERT_EQUAL_STRING("", pos.din);   // kein /D= im Frame -- initAPRSPOS()-Default bleibt
}

static void test_f011_kein_battery_leerer_kommentar(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_F011, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('/', pos.aprs_group);
    TEST_ASSERT_EQUAL_CHAR('#', pos.aprs_symbol);
    TEST_ASSERT_EQUAL_STRING("", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("", pos.pos_name.c_str());
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
    TEST_ASSERT_EQUAL_STRING("Marzling", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("Werner", pos.pos_name.c_str());
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

// Ueberlanger Kommentartext: die Kommentar/Name-Region ist auf 47 Zeichen
// gedeckelt (char cregion[48], Puffergrenze minus Terminator) -- kein /X=-
// Token stoppt die Region vorher, und keins der 300 'A' bildet zufaellig
// eins, also greift ausschliesslich die Puffergrenze. 300 'A' vor dem
// impliziten Leerzeichen-Terminator (PayloadBuffer.concat(" ") am
// Funktionsanfang) werden nie erreicht.
static void test_ueberlanger_kommentartext_wird_gekappt(void)
{
    std::string atxt(300, 'A');
    String payload = "4825.35N/01147.19E-";
    payload.concat(String(atxt));

    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(payload, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_size_t(47u, pos.pos_atxt.length());
    TEST_ASSERT_EQUAL_STRING(std::string(47, 'A').c_str(), pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("", pos.pos_name.c_str());
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

// /D=01001100 zwischen /A= und /N1 -- din wird gesetzt, bat/alt bleiben
// unveraendert korrekt (die neue Schleife darf die Batt-/Alt-Schleifen davor
// nicht stoeren).
static void test_din_zwischen_a_und_n(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_ZWISCHEN_A_UND_N, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_INT(99, pos.bat);
    TEST_ASSERT_EQUAL_INT(1657, pos.alt);
    TEST_ASSERT_EQUAL_STRING("01001100", pos.din);
}

// 7 Ziffern nach /D= -- zu kurz, din bleibt beim initAPRSPOS()-Default "".
static void test_din_7_zeichen_ungueltig(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_7_ZEICHEN, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("", pos.din);
}

// 9 Ziffern nach /D= -- zu lang, din bleibt "". Deckt auch die
// Ueberlaufbremse der neuen Schleife ab (das 9. Datenbyte darf decode_text
// nicht ueberschreiben).
static void test_din_9_zeichen_ungueltig(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_9_ZEICHEN, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("", pos.din);
}

// 8 Zeichen, aber ein Buchstabe statt '0'/'1' -- din bleibt "".
static void test_din_mit_buchstabe_ungueltig(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_MIT_BUCHSTABE, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("", pos.din);
}

// /D= als letztes Token des gesamten Payloads (kein abschliessendes '/') --
// muss trotzdem korrekt erkannt werden, weil decodeAPRSPOS() intern ein
// Leerzeichen anhaengt.
static void test_din_letztes_token(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_LETZTES_TOKEN, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("11111111", pos.din);
}

// /D= direkt nach dem Symbol bei leerem Kommentartext (wie VEC_F011),
// gefolgt von /B= -- din wird gesetzt und bat parst weiterhin korrekt.
static void test_din_leerer_kommentar(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_DIN_LEERER_KOMMENTAR, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_INT(57, pos.bat);
    TEST_ASSERT_EQUAL_STRING("00000000", pos.din);
}

// (a) Ein Leerzeichen unmittelbar vor dem /B=-Token darf die Region weder
// vorzeitig beenden noch das Leerzeichen selbst abschneiden.
static void test_leerzeichen_im_kommentar_bleibt_erhalten(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_LEERZEICHEN_VOR_TOKEN, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("MeshCom Zeltweg ", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("", pos.pos_name.c_str());
    TEST_ASSERT_EQUAL_INT(50, pos.bat);
}

// (b) Kommentar enthaelt selbst ein '#' -- nur das LETZTE '#' vor dem Token
// trennt den Namen ab, das erste bleibt Teil des Kommentartexts.
static void test_hash_im_kommentar_letztes_trennt_namen(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_HASH_IM_KOMMENTAR, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("Net#1 Info", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("Werner", pos.pos_name.c_str());
    TEST_ASSERT_EQUAL_INT(60, pos.bat);
}

// (c) Nur ein Name, kein Kommentartext -- Symbol ist '#', die Region
// beginnt sofort mit einem weiteren '#'.
static void test_nur_name_leerer_kommentar(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_NUR_NAME, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_CHAR('#', pos.aprs_symbol);
    TEST_ASSERT_EQUAL_STRING("", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("Name", pos.pos_name.c_str());
    TEST_ASSERT_EQUAL_INT(70, pos.bat);
}

// (d) Kein '#' in der Region -- pos_name bleibt leer, der komplette
// Regionsinhalt bleibt Kommentartext.
static void test_kein_hash_pos_name_leer(void)
{
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(VEC_OHNE_HASH, pos);

    TEST_ASSERT_EQUAL_UINT16(0x01, r);
    TEST_ASSERT_EQUAL_STRING("Marzling", pos.pos_atxt.c_str());
    TEST_ASSERT_EQUAL_STRING("", pos.pos_name.c_str());
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
    RUN_TEST(test_din_zwischen_a_und_n);
    RUN_TEST(test_din_7_zeichen_ungueltig);
    RUN_TEST(test_din_9_zeichen_ungueltig);
    RUN_TEST(test_din_mit_buchstabe_ungueltig);
    RUN_TEST(test_din_letztes_token);
    RUN_TEST(test_din_leerer_kommentar);
    RUN_TEST(test_leerzeichen_im_kommentar_bleibt_erhalten);
    RUN_TEST(test_hash_im_kommentar_letztes_trennt_namen);
    RUN_TEST(test_nur_name_leerer_kommentar);
    RUN_TEST(test_kein_hash_pos_name_leer);
    return UNITY_END();
}
