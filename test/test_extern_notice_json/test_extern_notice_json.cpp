// Native Testsuite fuer externNoticeJson() -- das EXTUDP-Datagramm einer
// BP-01-Notice (QRS/QRT/QTA/QRV).
//
// Kontrakt (Operator-Entscheidung 2026-08-31): die Notice kommt beim Peer
// (McApp) als GANZ NORMALE Textnachricht an, so als haette der Node sie ueber
// LoRa vom eigenen Rufzeichen empfangen -- src_type "lora", type "msg",
// numerische Firmware-Version. Ein eigener "notice"-Typ (erster Wurf dieses
// Pfads) war fuer McApp unsichtbar; ein Pseudo-Absender landet dort in der
// Spam-Klasse (Gruppe 9999). Dieser Test nagelt jede Feldbelegung fest.
//
//   pio test -e native -f test_extern_notice_json

#include <unity.h>

#include <string.h>

#include <ArduinoJson.h>
#include <extern_notice_json.h>

void setUp(void) {}
void tearDown(void) {}

// Jedes Feld exakt wie ein ueber LoRa empfangenes Text-Frame
static void test_msg_shape_komplett(void)
{
    char out[300];
    size_t len = externNoticeJson(out, sizeof(out), "DK5EN-99", 35, "p",
                                  0xABCD1234u,
                                  "QRS - slow down, TX buffer is filling");

    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);
    TEST_ASSERT_EQUAL_UINT(strlen(out), (unsigned)len);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());

    TEST_ASSERT_EQUAL_STRING("lora", doc["src_type"]);
    TEST_ASSERT_EQUAL_STRING("msg", doc["type"]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-99", doc["src"]);
    TEST_ASSERT_EQUAL_STRING("*", doc["dst"]);
    TEST_ASSERT_EQUAL_STRING("QRS - slow down, TX buffer is filling",
                             doc["msg"]);
    TEST_ASSERT_EQUAL_STRING("ABCD1234", doc["msg_id"]);
    // numerisch (35), nicht der "4.35"-String der "node"-Form
    TEST_ASSERT_TRUE(doc["firmware"].is<int>());
    TEST_ASSERT_EQUAL_INT(35, doc["firmware"].as<int>());
    TEST_ASSERT_EQUAL_STRING("p", doc["fw_sub"]);
    TEST_ASSERT_EQUAL_INT(0, doc["rssi"].as<int>());
    TEST_ASSERT_EQUAL_INT(0, doc["snr"].as<int>());

    // kein Rueckfall auf die alte "notice"-Form
    TEST_ASSERT_TRUE(doc["code"].isNull());
}

// msg_id ist immer 8 Hex-Zeichen, fuehrende Nullen inklusive
static void test_msg_id_format(void)
{
    char out[300];
    size_t len = externNoticeJson(out, sizeof(out), "DK5EN-99", 35, "p",
                                  0x2Au, "QRV - ready again, TX buffer clear");
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok,
                      deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING("0000002A", doc["msg_id"]);
}

// Die Schranke ist die Puffergroesse (JSN-01): ein zu kleiner Puffer wird
// nie ueberschrieben -- Kanarienvogel-Bytes hinter dem Puffer bleiben stehen.
static void test_puffer_schranke(void)
{
    char raw[64 + 8];
    memset(raw, 0xAA, sizeof(raw));

    size_t len = externNoticeJson(raw, 64, "DK5EN-99", 35, "p", 0x2Au,
                                  "QRT - stopping to accept new messages, "
                                  "TX buffer full");

    TEST_ASSERT_LESS_OR_EQUAL_UINT(64, (unsigned)len);
    for(int i = 0; i < 8; i++)
        TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t)raw[64 + i]);
}

// Degenerierte Eingaben: kein Puffer / Laenge 0 -> 0, kein Absturz
static void test_degeneriert(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)externNoticeJson(nullptr, 300,
                            "DK5EN-99", 35, "p", 1u, "x"));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)externNoticeJson(out, 0,
                            "DK5EN-99", 35, "p", 1u, "x"));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_msg_shape_komplett);
    RUN_TEST(test_msg_id_format);
    RUN_TEST(test_puffer_schranke);
    RUN_TEST(test_degeneriert);
    return UNITY_END();
}
