// Native Testsuite fuer decodeAPRS() — der Anfang des Test-Orakels aus
// docs/architecture/08-defect-catalogue.md §4.
//
// Die Vektoren sind KEINE Ausgaben des Pruefling-Decoders: es sind echte,
// am 2026-08-21 ueber den MC_TEST_HOOKS-Fang (OnRxDone-Hex-Dump, Mechanismus 2
// aus doc 08 §4) auf 433,175 MHz mitgeschnittene Frames FREMDER Nodes —
// encodiert von deren Firmware, nicht von unserer. Die Sollwerte wurden von
// Hand aus den Roh-Bytes gelesen (Rufzeichen und Payload stehen als
// ASCII im Frame, msg_id steht little-endian in Byte 1..4). Damit ist die
// Suite ein Interop-Orakel: aendert ein kuenftiger Decoder-Umbau das
// Verhalten gegenueber real existierenden Sendern, schlaegt sie fehl.
//
//   pio test -e native_aprs

#include <unity.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631 (wie auf der Bench) -- int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// Hilfsfunktion: Hex-String -> Bytes
static uint16_t hex2bin(const char *hex, uint8_t *out, uint16_t maxlen)
{
    uint16_t n = 0;
    while (hex[0] && hex[1] && n < maxlen)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int h = nib(hex[0]), l = nib(hex[1]);
        if (h < 0 || l < 0) break;
        out[n++] = (uint8_t)((h << 4) | l);
        hex += 2;
    }
    return n;
}

// ---------------------------------------------------------------- Vektoren
//
// Beide Frames wurden am 2026-08-21 gegen 10:57/10:58 UTC auf 433,175 MHz
// vom Bench-RAK4631 (DK5EN-90) mitgeschnitten. Die Sollwerte unten sind von
// Hand aus den Hex-Bytes gelesen (Rufzeichen/Payload stehen als ASCII im
// Frame, msg_id little-endian in Byte 1..4) und gegen die zeitgleich vom
// Geraet geloggten Klartext-Zeilen gegengeprueft.

// VEKTOR 1: Positionsbake ('!', MSG_TYPE_POSITION) von DL2JA-1, relayed
// ueber DL2JA-2 — fremde Firmware, RSSI -109 dBm (echter Fern-Node).
// Layout: 21 | AB 13 F1 E9 (msg_id LE) | 91 (Flags) |
//         "DL2JA-1,DL2JA-2>*" | 21 | "4825.35N\01147.19E-Marzling#Werner/R=9;"
//         | 00 | 2B 88 | 13 2F (FCS) | 23 AB 70 7E
static const char *VEC1_HEX =
    "21AB13F1E991444C324A412D312C444C324A412D323E2A21343832352E33354E5C"
    "30313134372E3139452D4D61727A6C696E67235765726E65722F523D393B002B88"
    "132F23AB707E";

static void test_vektor1_position_dl2ja(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = hex2bin(VEC1_HEX, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(72, len);

    struct aprsMessage m;
    initAPRS(m, 0x00);
    uint16_t t = decodeAPRS(buf, len, m);

    TEST_ASSERT_EQUAL_UINT16(0x21, t);                 // MSG_TYPE_POSITION
    TEST_ASSERT_EQUAL_UINT32(0xE9F113ABu, m.msg_id);   // Byte 1..4 little-endian
    TEST_ASSERT_EQUAL_STRING("DL2JA-1", m.msg_source_call.c_str());
    TEST_ASSERT_EQUAL_STRING("DL2JA-2", m.msg_source_last.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_call.c_str());
    TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.indexOf("Marzling#Werner") >= 0,
                             "Payload muss den Ortstext enthalten");
}

// VEKTOR 2: Zeitbake (':', MSG_TYPE_TEXT) von OE1XAR-33, relayed ueber
// DK5EN-98 (Produktions-Node) — RSSI -49 dBm.
// Layout: 3A | A4 23 88 6A (msg_id LE) | A2 (Flags) |
//         "OE1XAR-33,DK5EN-98>*" | 3A | "{CET}2026-08-21 10:58:58"
//         | 00 00 88 | 0D B5 (FCS) | 00 AB 23 7E
static const char *VEC2_HEX =
    "3AA423886AA24F45315841522D33332C444B35454E2D39383E2A3A7B4345547D32"
    "3032362D30382D32312031303A35383A35380000880DB500AB237E";

static void test_vektor2_text_oe1xar(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = hex2bin(VEC2_HEX, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(60, len);

    struct aprsMessage m;
    initAPRS(m, 0x00);
    uint16_t t = decodeAPRS(buf, len, m);

    TEST_ASSERT_EQUAL_UINT16(0x3A, t);                 // MSG_TYPE_TEXT
    TEST_ASSERT_EQUAL_UINT32(0x6A8823A4u, m.msg_id);
    TEST_ASSERT_EQUAL_STRING("OE1XAR-33", m.msg_source_call.c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-98", m.msg_source_last.c_str());
    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_call.c_str());
    TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.indexOf("{CET}2026-08-21 10:58:58") >= 0,
                             "Payload muss den Zeitstempel enthalten");
}

// CHR-03: Latin-1 durch BEIDE Chokepoints, nicht nur durch den Filter.
// encodePayloadAPRS() ist die TX-Engstelle, decodeAPRS() die RX-Engstelle;
// dieser Test faehrt eine Nachricht mit Latin-1-Umlauten einmal ganz herum.
// Das Orakel ist nicht der Filter selbst, sondern die FCS-Pruefung im
// Decoder (aprs_functions.cpp:427): haetten die Chokepoints Bytes entfernt
// oder veraendert, passte die vom Encoder ueber die Wire-Bytes gerechnete
// Pruefsumme nicht mehr und decodeAPRS() wuerde den Frame verwerfen.
// Rufzeichen bewusst DK5EN-90 (eigenes Bench-Call), nicht fremd.
static void test_latin1_umlaute_ueberleben_encode_und_decode(void)
{
    // "Gruesse" mit ue (0xFC) und scharfem S (0xDF) als Latin-1-Einzelbytes,
    // wie PinPoint sie sendet -- plus ein UTF-8-ae (C3 A4) im selben Text,
    // damit auch die Mischung geprueft ist.
    String payload = "Gr";
    payload += (char)0xFC;
    payload += (char)0xDF;
    payload += "e ";
    payload += (char)0xC3;
    payload += (char)0xA4;

    struct aprsMessage tx;
    initAPRS(tx, ':');
    tx.msg_id = 0x12345678u;
    tx.msg_source_path = "DK5EN-90";
    tx.msg_destination_path = "*";
    tx.msg_payload = payload;

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, tx);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "encodeAPRS() muss einen Frame liefern");

    // Die Latin-1-Bytes muessen unveraendert auf der Leitung stehen -- ein
    // Byte bleibt ein Byte, es wird nichts nach UTF-8 transkodiert.
    bool found_fc = false;
    bool found_df = false;
    for(uint16_t i = 0; i < len; i++)
    {
        if(buf[i] == 0xFC) found_fc = true;
        if(buf[i] == 0xDF) found_df = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(found_fc, "Latin-1 'ue' (0xFC) fehlt auf der Leitung");
    TEST_ASSERT_TRUE_MESSAGE(found_df, "Latin-1 'sz' (0xDF) fehlt auf der Leitung");

    struct aprsMessage rx;
    initAPRS(rx, 0x00);
    uint16_t t = decodeAPRS(buf, len, rx);

    TEST_ASSERT_EQUAL_UINT16(0x3A, t);                 // MSG_TYPE_TEXT
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, rx.msg_id);
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", rx.msg_source_call.c_str());
    TEST_ASSERT_EQUAL_STRING(payload.c_str(), rx.msg_payload.c_str());
}

static void test_leerer_frame_wird_abgelehnt(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    struct aprsMessage m;
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_UINT16(0, decodeAPRS(buf, 0, m));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_leerer_frame_wird_abgelehnt);
    RUN_TEST(test_vektor1_position_dl2ja);
    RUN_TEST(test_vektor2_text_oe1xar);
    RUN_TEST(test_latin1_umlaute_ueberleben_encode_und_decode);
    return UNITY_END();
}
