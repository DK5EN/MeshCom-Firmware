// Native Testsuite fuer buildAckPhoneFrame() und ackAttrCallLen()
// (src/ack_attribution.h) -- den BLE-Statusframe Richtung Telefon.
//
// Kernanforderung: das leere Rufzeichen (heutiger Normalfall, kein Absender
// bekannt) muss byteidentisch den heutigen 7-Byte-Frame liefern. Alles
// jenseits von [A-Z0-9-], 1..ACK_ATTR_CALL_MAX Zeichen faellt auf n=0 zurueck
// -- lieber kein Rufzeichen als ein halbes oder ein falsch geparstes.
//
//   pio test -e native -f test_ack_phone_frame

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <ack_attribution.h>

// ---------------------------------------------------------------------------
// ACK_PHONE_MAX_LEN -- Puffergroessen-Vertrag
// ---------------------------------------------------------------------------

void test_ack_phone_max_len_ist_17(void)
{
    TEST_ASSERT_EQUAL_UINT32(17, ACK_PHONE_MAX_LEN);
}

// ---------------------------------------------------------------------------
// Leeres/fehlendes Rufzeichen -- byteidentisch mit dem heutigen 7-Byte-Layout
// ---------------------------------------------------------------------------

void test_leeres_rufzeichen_liefert_alten_7_byte_frame(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "");

    TEST_ASSERT_EQUAL_UINT16(7, len);

    const uint8_t expected[7] = { 0x41, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, out, 7,
                                           "muss byteidentisch mit dem alten 7-Byte-Layout sein");
}

void test_null_rufzeichen_liefert_alten_7_byte_frame(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, NULL);

    TEST_ASSERT_EQUAL_UINT16(7, len);

    const uint8_t expected[7] = { 0x41, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, out, 7,
                                           "NULL muss wie das leere Rufzeichen behandelt werden");
}

// ---------------------------------------------------------------------------
// Gueltige Rufzeichen -- Anhang wird angehaengt, kein NUL-Terminator
// ---------------------------------------------------------------------------

void test_gueltiges_rufzeichen_dk5en_98(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "DK5EN-98");

    TEST_ASSERT_EQUAL_UINT16(15, len);
    TEST_ASSERT_EQUAL_UINT8(8, out[6]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((const uint8_t *)"DK5EN-98", out + 7, 8,
                                           "Rufzeichen muss ab Byte 7 unveraendert stehen");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, out[15],
                                     "kein NUL-Terminator -- Puffer dahinter bleibt unberuehrt");
}

// Laenge 10 -- die obere Schranke ACK_ATTR_CALL_MAX wird noch akzeptiert.
void test_gueltiges_rufzeichen_10_zeichen(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "OE1XYZ-123");

    TEST_ASSERT_EQUAL_UINT8(10, out[6]);
    TEST_ASSERT_EQUAL_UINT16(17, len);
}

// Laenge 11 -- ein Zeichen ueber der Schranke, faellt komplett auf n=0 zurueck.
void test_zu_langes_rufzeichen_faellt_auf_n0_zurueck(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "OE1XYZ-1234");

    TEST_ASSERT_EQUAL_UINT8(0, out[6]);
    TEST_ASSERT_EQUAL_UINT16(7, len);
}

// ---------------------------------------------------------------------------
// Zeichensatz-Verstoesse -- jeder faellt auf n=0 zurueck
// ---------------------------------------------------------------------------

void test_kleinbuchstabe_faellt_auf_n0_zurueck(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "dk5en-98");

    TEST_ASSERT_EQUAL_UINT8(0, out[6]);
    TEST_ASSERT_EQUAL_UINT16(7, len);
}

void test_leerzeichen_faellt_auf_n0_zurueck(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "DK5EN 98");

    TEST_ASSERT_EQUAL_UINT8(0, out[6]);
    TEST_ASSERT_EQUAL_UINT16(7, len);
}

void test_unterstrich_faellt_auf_n0_zurueck(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "DK5EN_98");

    TEST_ASSERT_EQUAL_UINT8(0, out[6]);
    TEST_ASSERT_EQUAL_UINT16(7, len);
}

// Hash-Token fuer Stufe 4 ("H" + 6 Hex-Ziffern) -- passt in den Zeichensatz
// [A-Z0-9-] und wird wie jedes andere gueltige Rufzeichen akzeptiert.
void test_hash_token_h3a5f21_wird_akzeptiert(void)
{
    uint8_t out[ACK_PHONE_MAX_LEN];
    memset(out, 0xEE, sizeof(out));

    uint16_t len = buildAckPhoneFrame(out, 0x11223344, 0x00, "H3A5F21");

    TEST_ASSERT_EQUAL_UINT8(7, out[6]);
    TEST_ASSERT_EQUAL_UINT16(14, len);
}

// ---------------------------------------------------------------------------
// Status landet unveraendert in Byte 5
// ---------------------------------------------------------------------------

void test_status_landet_in_byte5(void)
{
    const uint8_t status[] = { 0x00, 0x01, 0x02 };

    for(size_t i = 0; i < sizeof(status); i++)
    {
        uint8_t out[ACK_PHONE_MAX_LEN];
        buildAckPhoneFrame(out, 0x11223344, status[i], "");

        TEST_ASSERT_EQUAL_UINT8(status[i], out[5]);
    }
}

// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// ackMsgIdFromNode(): eigene msg_id vs. weitergeleitete
// ---------------------------------------------------------------------------

void test_msgid_vom_eigenen_node_wird_erkannt(void)
{
    uint32_t gw = 0x12345678;
    uint32_t own = ((gw & 0x3FFFFF) << 10) | 0x123;
    TEST_ASSERT_TRUE(ackMsgIdFromNode(own, gw));
    TEST_ASSERT_TRUE(ackMsgIdFromNode(((gw & 0x3FFFFF) << 10) | 0x3FF, gw));
}

void test_msgid_fremder_node_wird_abgelehnt(void)
{
    uint32_t gw = 0x12345678;
    // Bench 2026-09-05: DK5EN-98 (1AE1E221) gegen fremde 134EF38A / A1BD804D
    TEST_ASSERT_FALSE(ackMsgIdFromNode(0x134EF38A, gw));
    TEST_ASSERT_TRUE(ackMsgIdFromNode(0x1AE1E221, 0x1AE1E221 >> 10));
    TEST_ASSERT_FALSE(ackMsgIdFromNode(0xA1BD804D, 0x1AE1E221 >> 10));
    TEST_ASSERT_FALSE(ackMsgIdFromNode(0, gw));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_ack_phone_max_len_ist_17);

    RUN_TEST(test_leeres_rufzeichen_liefert_alten_7_byte_frame);
    RUN_TEST(test_null_rufzeichen_liefert_alten_7_byte_frame);

    RUN_TEST(test_gueltiges_rufzeichen_dk5en_98);
    RUN_TEST(test_gueltiges_rufzeichen_10_zeichen);
    RUN_TEST(test_zu_langes_rufzeichen_faellt_auf_n0_zurueck);

    RUN_TEST(test_kleinbuchstabe_faellt_auf_n0_zurueck);
    RUN_TEST(test_leerzeichen_faellt_auf_n0_zurueck);
    RUN_TEST(test_unterstrich_faellt_auf_n0_zurueck);
    RUN_TEST(test_hash_token_h3a5f21_wird_akzeptiert);

    RUN_TEST(test_status_landet_in_byte5);

    RUN_TEST(test_msgid_vom_eigenen_node_wird_erkannt);
    RUN_TEST(test_msgid_fremder_node_wird_abgelehnt);

    return UNITY_END();
}
