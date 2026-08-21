// Native Testsuite fuer appendHeySignalReport() — den HEY-Signal-Report
// "NCT,RSSI,SNR;", den ein Node beim Weiterleiten (Mesh) und ein Gateway
// beim UDP-Upload an die '@'-Payload anhaengt.
//
// Hintergrund: bis v4.35p wurde der Report NUR im Relay-Pfad (HF) angehaengt;
// der Gateway-Upload an den Server ging mit der rohen Payload raus. Seit dem
// Fix haengt das Gateway den Report vor addNodeData() an — beide Pfade rufen
// dieselbe Funktion, deren Format hier festgenagelt wird.
//
//   pio test -e native_aprs -f test_hey_report

#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
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

// Report an eine frische HEY-Payload: RSSI wird positiv, ';' terminiert
static void test_report_format(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R0;";

    appendHeySignalReport(m, -118, 7, 5);

    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", m.msg_payload.c_str());
}

// Negativer SNR bleibt vorzeichenbehaftet
static void test_report_negativer_snr(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R2;";

    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_EQUAL_STRING("R2;0,95,-3;", m.msg_payload.c_str());
}

// Mehrere Hops haengen ihre Reports hintereinander (Kette bleibt lesbar)
static void test_report_kette(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R0;";

    appendHeySignalReport(m, -118, 7, 5);
    appendHeySignalReport(m, -95, -1, 2);

    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;2,95,-1;", m.msg_payload.c_str());
}

// Der angereicherte Report ueberlebt encode -> decode (Wire-Roundtrip),
// d.h. genau diese Payload kommt beim Server bzw. naechsten Hop an.
static void test_report_roundtrip(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_id = 0x11223344;
    m.msg_source_path = "DK5EN-90";
    m.msg_destination_path = "HG";
    m.msg_destination_call = "HG";
    m.msg_payload = "R0;";

    appendHeySignalReport(m, -118, 7, 5);

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, m);
    TEST_ASSERT_GREATER_THAN_UINT16(0, len);

    struct aprsMessage d;
    initAPRS(d, 0x00);
    uint16_t t = decodeAPRS(buf, len, d);
    TEST_ASSERT_EQUAL_UINT16('@', t);
    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", d.msg_payload.c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", d.msg_source_call.c_str());
    TEST_ASSERT_EQUAL_STRING("HG", d.msg_destination_call.c_str());
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_report_format);
    RUN_TEST(test_report_negativer_snr);
    RUN_TEST(test_report_kette);
    RUN_TEST(test_report_roundtrip);
    return UNITY_END();
}
