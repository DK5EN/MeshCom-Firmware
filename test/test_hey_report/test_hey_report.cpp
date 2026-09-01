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

// ---- Laengenschranke HEY_PATH_PAYLOAD_MAX ---------------------------------
// Die Kette waechst je Relais um bis zu HEY_REPORT_GROUP_MAX Zeichen. Regulaer
// begrenzt MAX_HOP_LIMIT die Zahl der Gruppen, ein von der Luft kommendes
// '@'-Paket mit ueberlanger Nutzlast aber nicht. Ohne Schranke waechst der
// re-encodierte Rahmen ueber UDP_TX_BUF_SIZE und wird dort auf Byteebene
// gekappt -- mitten in einer Gruppe, was updateHeyPath() nicht mehr parsen kann.

// Eine regulaere Kette ueber die volle Hop-Tiefe darf die Schranke NICHT
// beruehren: sonst kuerzt der Fix gueltige Pfade.
static void test_report_volle_hoptiefe_bleibt_unangetastet(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R80;";

    // unguenstigste regulaere Gruppe: "80,128,-128;" (12 Zeichen)
    for(int hop = 0; hop < MAX_HOP_LIMIT; hop++)
        appendHeySignalReport(m, -128, -128, 80);

    // 4 + 7*12 = 88 Zeichen, alle sieben Gruppen sind angehaengt
    TEST_ASSERT_EQUAL_size_t(4u + (size_t)MAX_HOP_LIMIT * 12u, m.msg_payload.length());
    TEST_ASSERT_TRUE(m.msg_payload.length() <= HEY_PATH_PAYLOAD_MAX);
}

// Direkt unterhalb der Schranke wird noch angehaengt.
static void test_report_schranke_untere_kante(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = String('x', 0);
    while(m.msg_payload.length() < (unsigned)(HEY_PATH_PAYLOAD_MAX - HEY_REPORT_GROUP_MAX))
        m.msg_payload.concat('x');

    const unsigned before = m.msg_payload.length();
    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.length() > before,
                             "genau an der Kante muss noch angehaengt werden");
}

// Ein Zeichen darueber wird nicht mehr angehaengt -- und die vorhandene Kette
// bleibt unveraendert, statt abgeschnitten zu werden.
static void test_report_schranke_kappt_nicht_sondern_beendet(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R80;";
    while(m.msg_payload.length() < (unsigned)(HEY_PATH_PAYLOAD_MAX - HEY_REPORT_GROUP_MAX + 1))
        m.msg_payload.concat('x');

    const String before = m.msg_payload;
    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), m.msg_payload.c_str(),
                                     "ueber der Schranke darf nichts angehaengt und nichts gekappt werden");
}

// Auch wiederholte Aufrufe treiben die Nutzlast nicht ueber die Schranke --
// der Fall, den ein fehlerhaft geflutetes '@'-Paket ausloesen wuerde.
static void test_report_bleibt_beschraenkt(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload = "R80;";

    for(int i = 0; i < 50; i++)
        appendHeySignalReport(m, -128, -128, 80);

    TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.length() <= HEY_PATH_PAYLOAD_MAX,
                             "Nutzlast muss unter HEY_PATH_PAYLOAD_MAX bleiben");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_report_format);
    RUN_TEST(test_report_negativer_snr);
    RUN_TEST(test_report_kette);
    RUN_TEST(test_report_roundtrip);
    RUN_TEST(test_report_volle_hoptiefe_bleibt_unangetastet);
    RUN_TEST(test_report_schranke_untere_kante);
    RUN_TEST(test_report_schranke_kappt_nicht_sondern_beendet);
    RUN_TEST(test_report_bleibt_beschraenkt);
    return UNITY_END();
}
