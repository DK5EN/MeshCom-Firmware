// Native Testsuite fuer bpNoticeFillFrame() -- den BLE/Web-Rahmen einer
// BP-01-Notice (QRS/QRT/QTA/QRV) an die Phone-App bzw. Web-GUI -- und fuer
// bpNackCompose() (BP-07), den Textbauer fuer die Pro-Nachricht-Quittung
// "QRT NOT SENT - <Text>" / "QTA NOT SENT - <Text>".
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
// BP-07: der frühere Ziel-Zweitparse (ein Klammer-Peek auf den noch
// unkodierten Rohtext) ist ersatzlos entfallen -- der Refuse-Check in
// sendMessage() laeuft jetzt HINTER der {ZIEL}-Parsung (Grundentscheidung,
// bp-l1-l4-impl-plan.md), braucht also keinen Zweitparse mehr. Die
// frueheren Testfaelle dafuer unten sind deshalb entfernt, nicht ersetzt.
//
//   pio test -e native_aprs -f test_bp_notice_frame

#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <backpressure.h>
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

// ---- bpNackCompose() (BP-07) ------------------------------------------------
// prefix + up to BP_NACK_TEXT_MAX bytes of text, "..." on truncation.

static void test_nack_compose_kurzer_text(void)
{
    char out[64];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT),
                               "Hello World 17");

    TEST_ASSERT_EQUAL_STRING("QRT NOT SENT - Hello World 17", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), len);
}

// Genau BP_NACK_TEXT_MAX (120) Byte Text: passt vollstaendig, kein "..." --
// die Kante zwischen "passt" und "wird gekuerzt".
static void test_nack_compose_exakt_max_laenge(void)
{
    char text[BP_NACK_TEXT_MAX + 1];
    memset(text, 'A', BP_NACK_TEXT_MAX);
    text[BP_NACK_TEXT_MAX] = '\0';

    char out[16 + BP_NACK_TEXT_MAX + 8];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QTA), text);

    char expect[16 + BP_NACK_TEXT_MAX + 8];
    snprintf(expect, sizeof(expect), "%s", bpNackPrefix(BP_NACK_QTA));
    size_t p = strlen(expect);
    memset(expect + p, 'A', BP_NACK_TEXT_MAX);
    expect[p + BP_NACK_TEXT_MAX] = '\0';

    TEST_ASSERT_EQUAL_STRING(expect, out);
    TEST_ASSERT_EQUAL_UINT(strlen(expect), len);
}

// Ein Byte mehr als das Budget kippt die Kante: 120 Byte Text plus "...".
static void test_nack_compose_zu_langer_text_bekommt_ellipse(void)
{
    char text[BP_NACK_TEXT_MAX + 2];
    memset(text, 'B', BP_NACK_TEXT_MAX + 1);
    text[BP_NACK_TEXT_MAX + 1] = '\0';

    char out[16 + BP_NACK_TEXT_MAX + 8];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT), text);

    char expect[16 + BP_NACK_TEXT_MAX + 8];
    snprintf(expect, sizeof(expect), "%s", bpNackPrefix(BP_NACK_QRT));
    size_t p = strlen(expect);
    memset(expect + p, 'B', BP_NACK_TEXT_MAX);
    strcpy(expect + p + BP_NACK_TEXT_MAX, "...");

    TEST_ASSERT_EQUAL_STRING(expect, out);
    TEST_ASSERT_EQUAL_UINT(strlen(expect), len);
}

// Regel 1: die 120-Byte-Kante faellt hier mitten in eine zweibytige
// UTF-8-Sequenz ("Ä" = 0xC3 0x84, Lead-Byte an Index 119, Folgebyte an
// Index 120) -- die Kuerzung muss auf die Codepoint-Grenze zurueckweichen
// (Index 119, nur 119 volle Byte Text vor der Ellipse), nicht mitten
// hineinschneiden.
static void test_nack_compose_kuerzt_auf_utf8_codepoint_grenze(void)
{
    char text[BP_NACK_TEXT_MAX + 20];
    memset(text, 'A', 119);
    text[119] = (char)0xC3;   // "Ä" Lead-Byte, sitzt genau auf der Kante
    text[120] = (char)0x84;   // Folgebyte
    memset(text + 121, 'Z', 10);
    text[131] = '\0';

    char out[16 + BP_NACK_TEXT_MAX + 8];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT), text);

    char expect[16 + BP_NACK_TEXT_MAX + 8];
    snprintf(expect, sizeof(expect), "%s", bpNackPrefix(BP_NACK_QRT));
    size_t p = strlen(expect);
    memset(expect + p, 'A', 119);
    strcpy(expect + p + 119, "...");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, out,
        "Kuerzung muss auf der Codepoint-Grenze zurueckweichen, nicht das Folgebyte kappen");
    TEST_ASSERT_EQUAL_UINT(strlen(expect), len);
}

// Regel 2: '"', '\' und jedes Steuerzeichen (< 0x20) werden zu einem
// Leerzeichen -- Byte fuer Byte ersetzt, nicht entfernt (die Laenge bleibt
// 1:1, siehe Kommentar am Helper: das haelt das EXTUDP-JSON-Escaping klein).
static void test_nack_compose_ersetzt_anfuehrungszeichen_backslash_steuerzeichen(void)
{
    char out[64];
    const char text[] = { 'a', '"', 'b', '\\', 'c', 0x01, 'd', '\t', 'e', '\0' };

    size_t len = bpNackCompose(out, sizeof(out), "QTA NOT SENT - ", text);

    TEST_ASSERT_EQUAL_STRING("QTA NOT SENT - a b c d e", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), len);
}

// Leerer Text: nur das Praefix kommt heraus, kein "..." (nichts wurde gekuerzt).
static void test_nack_compose_leerer_text(void)
{
    char out[64];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT), "");

    TEST_ASSERT_EQUAL_STRING("QRT NOT SENT - ", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), len);
}

// text == nullptr: behandelt wie leerer Text, kein Absturz.
static void test_nack_compose_text_nullptr(void)
{
    char out[64];
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QTA), nullptr);

    TEST_ASSERT_EQUAL_STRING("QTA NOT SENT - ", out);
    TEST_ASSERT_EQUAL_UINT(strlen(out), len);
}

// out_len == 1: nur Platz fuer die terminierende NUL, kein Byte Inhalt.
static void test_nack_compose_out_len_eins(void)
{
    char out[1] = { 'x' };
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT), "Hello");

    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT(0, len);
}

// out_len kleiner als das Praefix: das Praefix selbst wird gekappt, der Text
// faellt komplett weg -- out_len-sicher heisst hier "nie ueberschreiben",
// nicht "immer vollstaendig".
static void test_nack_compose_out_len_kleiner_als_praefix(void)
{
    char out[5];   // "QRT NOT SENT - " ist 15 Byte lang
    size_t len = bpNackCompose(out, sizeof(out), bpNackPrefix(BP_NACK_QRT), "Hello");

    TEST_ASSERT_EQUAL_STRING("QRT ", out);
    TEST_ASSERT_EQUAL_UINT(4, len);
}

// out == nullptr: no-op, kein Absturz.
static void test_nack_compose_out_nullptr(void)
{
    size_t len = bpNackCompose(nullptr, 64, bpNackPrefix(BP_NACK_QRT), "Hello");
    TEST_ASSERT_EQUAL_UINT(0, len);
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
    RUN_TEST(test_nack_compose_kurzer_text);
    RUN_TEST(test_nack_compose_exakt_max_laenge);
    RUN_TEST(test_nack_compose_zu_langer_text_bekommt_ellipse);
    RUN_TEST(test_nack_compose_kuerzt_auf_utf8_codepoint_grenze);
    RUN_TEST(test_nack_compose_ersetzt_anfuehrungszeichen_backslash_steuerzeichen);
    RUN_TEST(test_nack_compose_leerer_text);
    RUN_TEST(test_nack_compose_text_nullptr);
    RUN_TEST(test_nack_compose_out_len_eins);
    RUN_TEST(test_nack_compose_out_len_kleiner_als_praefix);
    RUN_TEST(test_nack_compose_out_nullptr);
    return UNITY_END();
}
