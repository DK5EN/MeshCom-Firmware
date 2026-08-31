// Native Testsuite fuer bpNoticeFillFrame() -- den BLE/Web-Rahmen einer
// BP-01-Notice (QRS/QRT/QTA/QRV) an die Phone-App bzw. Web-GUI -- und fuer
// bpPeekDst(), den Praefix-Peek, der dem Refuse-Pfad in sendMessage() das
// Ziel liefert, bevor die Nachricht ueberhaupt geparst ist.
//
// Kontrakt (Operator-Entscheidung 2026-08-31): Absender ist das RUFZEICHEN
// DES NODES, nicht der Pseudo-Absender "response" der Kommando-Antworten
// (addBLECommandBack()) -- McApp legt ungueltige Absender in seine
// Spam-Klasse (Gruppe 9999), wo der Operator die Notice nie sieht.
//
// BP-06: das Ziel der Notice ist das Ziel der ausloesenden Nachricht (Gruppe,
// DM-Call oder "*"), nicht mehr fest "*". msg_app_offline haelt den Rahmen
// weiterhin lokal -- er geht nie on air, auch nicht bei einem DM-Ziel.
//
//   pio test -e native_aprs -f test_bp_notice_frame

#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <bp_notice_frame.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

static const char *QRS_TEXT = "QRS - slow down, TX buffer is filling";

// Absender = Node-Rufzeichen, Ziel = "*" (Broadcast-Fall), Payload
// unveraendert, lokal
static void test_frame_felder(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 0xABCD1234u, "*");

    TEST_ASSERT_EQUAL_STRING("DK5EN-99", m.msg_source_path.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_call.c_str());
    TEST_ASSERT_EQUAL_STRING(QRS_TEXT, m.msg_payload.c_str());
    TEST_ASSERT_EQUAL_CHAR(':', m.payload_type);
    TEST_ASSERT_EQUAL_UINT32(0xABCD1234u, m.msg_id);
    TEST_ASSERT_TRUE_MESSAGE(m.msg_app_offline,
                             "Notice darf nie announced/retransmitted werden");
}

// BP-06: ein Gruppenziel landet unveraendert in Pfad UND Call. Faellt ohne
// den dst-Parameter durch (alte Rahmung nagelte "*" fest) -- fails-before.
static void test_frame_ziel_gruppe(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 1u, "20");

    TEST_ASSERT_EQUAL_STRING("20", m.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING("20", m.msg_destination_call.c_str());
    TEST_ASSERT_TRUE_MESSAGE(m.msg_app_offline,
                             "auch bei Gruppenziel: nie on air");
}

// BP-06: ein DM-Ziel landet ebenso unveraendert im Rahmen -- msg_app_offline
// haelt ihn trotzdem lokal, der DM-Partner sieht ihn nie. fails-before.
static void test_frame_ziel_dm(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 1u, "DL7CL-7");

    TEST_ASSERT_EQUAL_STRING("DL7CL-7", m.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING("DL7CL-7", m.msg_destination_call.c_str());
    TEST_ASSERT_TRUE_MESSAGE(m.msg_app_offline,
                             "DM-Ziel bleibt lokal: nie on air");
}

// Der Rahmen ueberlebt encode -> decode: genau so kommt er bei der App an
static void test_frame_roundtrip(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 0x11223344u, "*");

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, m);
    TEST_ASSERT_GREATER_THAN_UINT16(0, len);

    struct aprsMessage d;
    initAPRS(d, 0x00);
    uint16_t t = decodeAPRS(buf, len, d);
    TEST_ASSERT_GREATER_THAN_UINT16(0, t);

    TEST_ASSERT_EQUAL_STRING("DK5EN-99", d.msg_source_path.c_str());
    TEST_ASSERT_EQUAL_STRING("*", d.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING(QRS_TEXT, d.msg_payload.c_str());
    TEST_ASSERT_EQUAL_CHAR(':', d.payload_type);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, d.msg_id);
}

// BP-06: ein DM-Ziel ueberlebt denselben encode -> decode-Roundtrip
static void test_frame_roundtrip_dm_ziel(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 0x22334455u, "DL7CL-7");

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, m);
    TEST_ASSERT_GREATER_THAN_UINT16(0, len);

    struct aprsMessage d;
    initAPRS(d, 0x00);
    uint16_t t = decodeAPRS(buf, len, d);
    TEST_ASSERT_GREATER_THAN_UINT16(0, t);

    TEST_ASSERT_EQUAL_STRING("DK5EN-99", d.msg_source_path.c_str());
    TEST_ASSERT_EQUAL_STRING("DL7CL-7", d.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING(QRS_TEXT, d.msg_payload.c_str());
}

// Regression: der Pseudo-Absender "response" darf nie zurueckkommen
static void test_kein_response_absender(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 1u, "*");

    TEST_ASSERT_TRUE_MESSAGE(m.msg_source_path.indexOf("response") < 0,
                             "Absender 'response' landet in McApps Spam-Klasse (9999)");
}

// ---- bpPeekDst() -----------------------------------------------------------
// Paritaet zur strDestinationCall-Extraktion in sendMessage() (iCall<11):
// gleicher Klammer-Scan, gleiche Grenze, gleiches Trim/Upper.

static void test_peek_gruppe(void)
{
    char out[12];
    bpPeekDst("{20}Hallo", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("20", out);
}

static void test_peek_dm_call(void)
{
    char out[12];
    bpPeekDst("{OE1KBC-99}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("OE1KBC-99", out);
}

static void test_peek_upper(void)
{
    char out[12];
    bpPeekDst("{dl7cl-7}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("DL7CL-7", out);
}

static void test_peek_ohne_klammer(void)
{
    char out[12];
    bpPeekDst("ohne Klammer", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

static void test_peek_unterminiert(void)
{
    char out[12];
    bpPeekDst("{20", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

// Schliessende Klammer erst an Position 12 -- verletzt die iCall<11-Regel,
// die auch die echte Ziel-Extraktion in sendMessage() durchsetzt.
static void test_peek_ueberlang(void)
{
    char out[12];
    bpPeekDst("{12345678901}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

static void test_peek_trim(void)
{
    char out[12];
    bpPeekDst("{ 20 }x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("20", out);
}

static void test_peek_leerstring(void)
{
    char out[12];
    bpPeekDst("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

// Advisor BP-06/1: die REJECT-Kante der iCall<11-Regel exakt -- '}' an
// Index 11 (10 Zeichen Inhalt) ist die erste abgelehnte Position. Die
// Accept-Kante (Index 10, {OE1KBC-99}) pinnt test_peek_dm_call.
static void test_peek_reject_kante(void)
{
    char out[12];
    bpPeekDst("{1234567890}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

// Advisor BP-06/2: leeres bzw. reines Leerzeichen-Ziel -> "*" (bewusste
// Divergenz zur autoritativen Extraktion, die dort "" liefert -- eine
// Notice an "" landet nirgends; dokumentiert am Helper).
static void test_peek_leeres_ziel(void)
{
    char out[12];
    bpPeekDst("{}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
    bpPeekDst("{ }x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("*", out);
}

// Advisor BP-06/3: Trim entfernt wie String::trim() auch Tab/CR/LF, nicht
// nur 0x20 -- "{\t20}x" muss "20" liefern, nicht "\t20".
static void test_peek_trim_isspace(void)
{
    char out[12];
    bpPeekDst("{\t20}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("20", out);
    bpPeekDst("{20\r\n}x", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("20", out);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_frame_felder);
    RUN_TEST(test_frame_ziel_gruppe);
    RUN_TEST(test_frame_ziel_dm);
    RUN_TEST(test_frame_roundtrip);
    RUN_TEST(test_frame_roundtrip_dm_ziel);
    RUN_TEST(test_kein_response_absender);
    RUN_TEST(test_peek_gruppe);
    RUN_TEST(test_peek_dm_call);
    RUN_TEST(test_peek_upper);
    RUN_TEST(test_peek_ohne_klammer);
    RUN_TEST(test_peek_unterminiert);
    RUN_TEST(test_peek_ueberlang);
    RUN_TEST(test_peek_trim);
    RUN_TEST(test_peek_leerstring);
    RUN_TEST(test_peek_reject_kante);
    RUN_TEST(test_peek_leeres_ziel);
    RUN_TEST(test_peek_trim_isspace);
    return UNITY_END();
}
