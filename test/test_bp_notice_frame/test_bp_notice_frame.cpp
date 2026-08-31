// Native Testsuite fuer bpNoticeFillFrame() -- den BLE/Web-Rahmen einer
// BP-01-Notice (QRS/QRT/QTA/QRV) an die Phone-App bzw. Web-GUI.
//
// Kontrakt (Operator-Entscheidung 2026-08-31): Absender ist das RUFZEICHEN
// DES NODES, nicht der Pseudo-Absender "response" der Kommando-Antworten
// (addBLECommandBack()) -- McApp legt ungueltige Absender in seine
// Spam-Klasse (Gruppe 9999), wo der Operator die Notice nie sieht. Ziel ist
// "*" (Broadcast), msg_app_offline haelt den Rahmen lokal.
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

// Absender = Node-Rufzeichen, Ziel = "*", Payload unveraendert, lokal
static void test_frame_felder(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 0xABCD1234u);

    TEST_ASSERT_EQUAL_STRING("DK5EN-99", m.msg_source_path.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_call.c_str());
    TEST_ASSERT_EQUAL_STRING(QRS_TEXT, m.msg_payload.c_str());
    TEST_ASSERT_EQUAL_CHAR(':', m.payload_type);
    TEST_ASSERT_EQUAL_UINT32(0xABCD1234u, m.msg_id);
    TEST_ASSERT_TRUE_MESSAGE(m.msg_app_offline,
                             "Notice darf nie announced/retransmitted werden");
}

// Der Rahmen ueberlebt encode -> decode: genau so kommt er bei der App an
static void test_frame_roundtrip(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 0x11223344u);

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

// Regression: der Pseudo-Absender "response" darf nie zurueckkommen
static void test_kein_response_absender(void)
{
    struct aprsMessage m;
    bpNoticeFillFrame(m, "DK5EN-99", QRS_TEXT, 1u);

    TEST_ASSERT_TRUE_MESSAGE(m.msg_source_path.indexOf("response") < 0,
                             "Absender 'response' landet in McApps Spam-Klasse (9999)");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_frame_felder);
    RUN_TEST(test_frame_roundtrip);
    RUN_TEST(test_kein_response_absender);
    return UNITY_END();
}
