// Regressionstest UP-01: BLE-JSON-Rahmen wird durch die Puffergroesse begrenzt.
//
// Hintergrund: upstream 2cb6bb4d (Revert von #1090) hat in mheard_functions.cpp
// serializeJson(mhdoc, bleBuffer+1, measureJson(mhdoc)+1) wiederhergestellt.
// Damit setzt das Dokument die Schreibgrenze, nicht der Puffer: ein Dokument,
// das laenger ist als bleBuffer[MAX_MSG_LEN_PHONE], schreibt ueber den
// Stack-Rahmen hinaus. Mit den heutigen 13 Schluesseln liegt der schlechteste
// Fall bei 284 von 299 Byte (docs/review/2026-08-29-upstream-sync-verdict.md);
// jeder weitere Schluessel macht den Ueberlauf scharf.
//
// Der Test prueft bleJsonFrame() gegen einen Puffer mit Kanarienvogel-Bytes
// dahinter. Vor dem Fix (Schranke measureJson()+1) wird der Kanarienvogel
// ueberschrieben, nach dem Fix bleibt er unberuehrt.
//
//   pio test -e native -f test_ble_json_frame

#include <unity.h>
#include <string.h>

#include <ble_json_frame.h>

void setUp(void) {}
void tearDown(void) {}

// So sah der Aufruf vor dem Fix aus - als Kontrollfall, damit der Test
// nachweislich den Fehler sieht, den er verhindern soll.
static uint16_t frame_bounded_by_document(const JsonDocument &doc, uint8_t *buf)
{
    size_t json_len = serializeJson(doc, (char *)buf + 1, measureJson(doc) + 1);
    return (uint16_t)(json_len + 1);
}

// Ein MH-Dokument wie in updateMheard(), mit einem Rufzeichen, das die
// Rufzeichenpruefung passiert (UP-06: [0-9]+ ist unbegrenzt).
static void fill_mh_doc(JsonDocument &doc, size_t call_len)
{
    static char call[200];
    memset(call, '1', sizeof(call));
    call[0] = 'D';
    call[call_len - 1] = 'A';
    call[call_len] = 0;

    doc["TYP"] = "MH";
    doc["CALL"] = call;
    doc["DATE"] = "29.08.2026";
    doc["TIME"] = "12:34:56";
    doc["PLT"] = (uint8_t)58;
    doc["HW"] = (uint8_t)9;
    doc["MOD"] = (uint8_t)8;
    doc["RSSI"] = (int16_t)-120;
    doc["SNR"] = (int8_t)-20;
    doc["DIST"] = 12345.678;
    doc["PL"] = (uint8_t)6;
    doc["MESH"] = (uint8_t)1;
    doc["NCNT"] = (uint8_t)255;
}

// Der Kanarienvogel ist groesser als jedes MH-Dokument, damit der Kontrollfall
// (alte Schranke) den Ueberlauf vollstaendig im Kanarienvogel ablegt statt den
// Stack-Rahmen des Tests zu zerstoeren.
#define BUF 64
#define CANARY 512

struct framed_buffer {
    uint8_t buf[BUF];
    uint8_t canary[CANARY];
};

static void arm(struct framed_buffer &fb)
{
    memset(fb.buf, 0, sizeof(fb.buf));
    memset(fb.canary, 0xA5, sizeof(fb.canary));
    fb.buf[0] = 0x44;
}

static bool canary_intact(const struct framed_buffer &fb)
{
    for (size_t i = 0; i < CANARY; i++)
        if (fb.canary[i] != 0xA5)
            return false;
    return true;
}

// ---------------------------------------------------------- passt in den Puffer

static void test_kurzes_dokument_wird_vollstaendig_geschrieben(void)
{
    JsonDocument doc;
    doc["TYP"] = "MH";
    doc["CALL"] = "DK5EN-14";

    struct framed_buffer fb;
    arm(fb);
    uint16_t len = bleJsonFrame(doc, fb.buf, sizeof(fb.buf));

    TEST_ASSERT_EQUAL_UINT16(measureJson(doc) + 1, len);
    TEST_ASSERT_EQUAL_UINT8(0x44, fb.buf[0]);
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"MH\",\"CALL\":\"DK5EN-14\"}", (const char *)fb.buf + 1);
    TEST_ASSERT_TRUE(canary_intact(fb));
}

// ---------------------------------------------------------- laenger als der Puffer

static void test_langes_dokument_wird_auf_den_puffer_begrenzt(void)
{
    JsonDocument doc;
    fill_mh_doc(doc, 119);
    TEST_ASSERT_GREATER_THAN_size_t(BUF, measureJson(doc));

    struct framed_buffer fb;
    arm(fb);
    uint16_t len = bleJsonFrame(doc, fb.buf, sizeof(fb.buf));

    TEST_ASSERT_LESS_OR_EQUAL_UINT16(BUF, len);
    TEST_ASSERT_TRUE(canary_intact(fb));
}

// Kontrollfall: die alte Aufrufform ueberschreibt den Kanarienvogel. Faellt
// dieser Test, hat sich ArduinoJson geaendert und der Regressionstest oben
// beweist nichts mehr.
static void test_kontrolle_alte_schranke_ueberschreibt_den_puffer(void)
{
    JsonDocument doc;
    fill_mh_doc(doc, 119);

    struct framed_buffer fb;
    arm(fb);
    (void)frame_bounded_by_document(doc, fb.buf);

    TEST_ASSERT_FALSE(canary_intact(fb));
}

// ---------------------------------------------------------- Randfaelle

static void test_leerer_oder_zu_kleiner_puffer_schreibt_nichts(void)
{
    JsonDocument doc;
    doc["TYP"] = "MH";

    uint8_t one[1] = {0x44};
    TEST_ASSERT_EQUAL_UINT16(0, bleJsonFrame(doc, one, sizeof(one)));
    TEST_ASSERT_EQUAL_UINT8(0x44, one[0]);
    TEST_ASSERT_EQUAL_UINT16(0, bleJsonFrame(doc, nullptr, 64));
}

static void test_schlechtester_heutiger_fall_passt_in_max_msg_len_phone(void)
{
    // 13 Schluessel, 119 Zeichen Rufzeichen: 284 Byte Rahmen laut Verdict.
    // Dokumentiert die Reserve, damit ein neuer Schluessel hier auffaellt.
    JsonDocument doc;
    fill_mh_doc(doc, 119);
    uint8_t buf[300];
    uint16_t len = bleJsonFrame(doc, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN_UINT16(300, len);
    TEST_ASSERT_GREATER_THAN_UINT16(250, len);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_kurzes_dokument_wird_vollstaendig_geschrieben);
    RUN_TEST(test_langes_dokument_wird_auf_den_puffer_begrenzt);
    RUN_TEST(test_kontrolle_alte_schranke_ueberschreibt_den_puffer);
    RUN_TEST(test_leerer_oder_zu_kleiner_puffer_schreibt_nichts);
    RUN_TEST(test_schlechtester_heutiger_fall_passt_in_max_msg_len_phone);
    return UNITY_END();
}
