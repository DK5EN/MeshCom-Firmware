// Native Testsuite fuer isPlausibleAckFrame() -- die Plausibilitaetspruefung,
// die handleACK() vorgeschaltet wurde.
//
// Hintergrund: handleACK() akzeptierte einen Frame allein aufgrund von
// payload[0] == 0x41 und size >= 12. 0x41 ist als ASCII 'A', damit lief jedes
// Bruchstueck eines Text- oder Positionspakets, das mit 'A' beginnt, durch den
// ACK-Pfad, belegte einen Prio-1-Queueplatz, warf dabei einen Heartbeat aus der
// vollen Sendequeue und wurde ins Mesh weitergesendet.
//
// Die Testvektoren sind KEINE erfundenen Beispiele, sondern woertlich aus den
// Logs von DG0OPK-11/-12/-13 (24./25.08.2026, 32,7 h) uebernommen. Die Log-
// Ausgabe (printBuffer_ack) druckt die 32-Bit-Felder MSB-first, hier stehen
// die Bytes in Speicherreihenfolge.
//
//   pio test -e native -f test_ack_validate

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <ack_functions.h>
#include <ack_attribution.h>

// ---------------------------------------------------------------------------
// Hilfsmittel
// ---------------------------------------------------------------------------

// Baut einen ACK-Frame so, wie lora_functions.cpp ihn erzeugt:
// 0x41 | msg_counter(4) | 0x80|max_hop | msg_id(4) | 0x01 | 0x00
static void buildAck(uint8_t *buf, uint32_t ownId, uint8_t byte5, uint32_t ackedId)
{
    buf[0]  = MSG_TYPE_ACK;
    buf[1]  = (uint8_t)(ownId & 0xFF);
    buf[2]  = (uint8_t)((ownId >> 8) & 0xFF);
    buf[3]  = (uint8_t)((ownId >> 16) & 0xFF);
    buf[4]  = (uint8_t)((ownId >> 24) & 0xFF);
    buf[5]  = byte5;
    buf[6]  = (uint8_t)(ackedId & 0xFF);
    buf[7]  = (uint8_t)((ackedId >> 8) & 0xFF);
    buf[8]  = (uint8_t)((ackedId >> 16) & 0xFF);
    buf[9]  = (uint8_t)((ackedId >> 24) & 0xFF);
    buf[10] = 0x01;
    buf[11] = 0x00;
}

// ---------------------------------------------------------------------------
// Gueltige Frames -- muessen ALLE durchkommen
// ---------------------------------------------------------------------------

// Im Feld beobachtet: Byte 5 der gueltigen ACKs liegt ausschliesslich bei
// 0x80..0x84 (8235 von 8741 Frames). 0x84 = frisch erzeugt mit
// MAX_HOP_TEXT_DEFAULT, 0x80 = Hop-Budget aufgebraucht.
void test_gueltige_hopwerte_werden_akzeptiert(void)
{
    uint8_t buf[12];

    for(uint8_t hop = 0; hop <= MAX_HOP_LIMIT; hop++)
    {
        buildAck(buf, 0x687B40C8, (uint8_t)(0x80 | hop), 0x9BF3C002);
        TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT),
                                 "0x80|hop innerhalb der Schranke muss akzeptiert werden");
    }
}

// Genau so erzeugt lora_functions.cpp den Frame beim Quittieren.
void test_frisch_erzeugter_ack_wird_akzeptiert(void)
{
    uint8_t buf[12];
    buildAck(buf, 0x0001E240, (uint8_t)(0x80 | MAX_HOP_TEXT_DEFAULT), 0x687B40C8);

    TEST_ASSERT_TRUE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT));
}

// Laengere Frames sind zulaessig -- handleACK kopiert ohnehin nur 12 Byte.
void test_laengerer_frame_wird_akzeptiert(void)
{
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    buildAck(buf, 0x687B40C8, 0x82, 0x9BF3C002);

    TEST_ASSERT_TRUE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT));
}

// ---------------------------------------------------------------------------
// Gruppe C aus der Feldmessung: Server-Bit fehlt (285 Frames, 3,3 %)
// ---------------------------------------------------------------------------

// Der Frame, der die Ringueberlauf-Analyse verfaelscht hat. In der Log-Anzeige
// "41 3030303D 2C 502F3832 3D 31", in Speicherreihenfolge lesbar als
// ASCII "A=000,28/P=1" -- ein Bruchstueck einer APRS-Telemetriezeile.
void test_ascii_bruchstueck_wird_verworfen(void)
{
    const uint8_t frame[12] = {
        0x41, 0x3D, 0x30, 0x30, 0x30, 0x2C, 0x32, 0x38, 0x2F, 0x50, 0x3D, 0x31
    };

    TEST_ASSERT_FALSE_MESSAGE(isPlausibleAckFrame(frame, sizeof(frame), MAX_HOP_LIMIT),
                              "ASCII-Bruchstueck 'A=000,28/P=1' darf kein ACK sein");
}

void test_server_bit_fehlt_wird_verworfen(void)
{
    uint8_t buf[12];
    // Im Feld beobachtete Byte-5-Werte ohne Server-Bit.
    const uint8_t byte5[] = { 0x00, 0x01, 0x1A, 0x24, 0x2A, 0x2B, 0x2C, 0x2F, 0x31, 0x34, 0x35 };

    for(size_t i = 0; i < sizeof(byte5); i++)
    {
        buildAck(buf, 0x687B40C8, byte5[i], 0x9BF3C002);
        TEST_ASSERT_FALSE_MESSAGE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT),
                                  "Byte 5 ohne Server-Bit darf kein ACK sein");
    }
}

// ---------------------------------------------------------------------------
// Gruppe B aus der Feldmessung: Server-Bit gesetzt, Hops 5..116 (221 Frames)
// ---------------------------------------------------------------------------

// Das ist der Fall, den eine reine Server-Bit-Pruefung durchgelassen haette --
// und ausgerechnet dieser traegt die absurden Hop-Budgets.
void test_absurdes_hop_budget_wird_verworfen(void)
{
    uint8_t buf[12];
    // Im Feld beobachtet: 0x8B = 11 Hops, 0x99 = 25, 0xB0 = 48, 0xC2 = 66,
    // 0xF4 = 116 Hops. Alle mit gesetztem Server-Bit.
    const uint8_t byte5[] = { 0x8B, 0x99, 0xAF, 0xB0, 0xBF, 0xC0, 0xC2, 0xF2, 0xF3, 0xF4 };

    for(size_t i = 0; i < sizeof(byte5); i++)
    {
        buildAck(buf, 0x4DAC6665, byte5[i], 0x7412416C);
        TEST_ASSERT_FALSE_MESSAGE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT),
                                  "Server-Bit allein genuegt nicht -- Hop-Budget muss plausibel sein");
    }
}

// Die Schranke selbst: MAX_HOP_LIMIT ist noch gueltig, ein Hop mehr nicht.
void test_schranke_ist_exklusiv_oberhalb(void)
{
    uint8_t buf[12];

    buildAck(buf, 0x687B40C8, (uint8_t)(0x80 | MAX_HOP_LIMIT), 0x9BF3C002);
    TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT),
                             "genau MAX_HOP_LIMIT muss noch durchkommen");

    buildAck(buf, 0x687B40C8, (uint8_t)(0x80 | (MAX_HOP_LIMIT + 1)), 0x9BF3C002);
    TEST_ASSERT_FALSE_MESSAGE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT),
                              "ein Hop ueber MAX_HOP_LIMIT muss verworfen werden");
}

// ---------------------------------------------------------------------------
// Grundlegende Eingangspruefungen
// ---------------------------------------------------------------------------

void test_falscher_typ_wird_verworfen(void)
{
    uint8_t buf[12];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);

    buf[0] = 0x3A;   // Textnachricht
    TEST_ASSERT_FALSE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT));

    buf[0] = 0x21;   // Position
    TEST_ASSERT_FALSE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT));

    buf[0] = 0x40;   // Heartbeat
    TEST_ASSERT_FALSE(isPlausibleAckFrame(buf, 12, MAX_HOP_LIMIT));
}

// Der 7-Byte-Frame Richtung Telefon darf nie als Funk-ACK durchgehen --
// er hat gar kein Byte 5 im Sinne des Funkprotokolls.
void test_zu_kurzer_frame_wird_verworfen(void)
{
    uint8_t buf[12];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);

    for(uint16_t len = 0; len < 12; len++)
        TEST_ASSERT_FALSE_MESSAGE(isPlausibleAckFrame(buf, len, MAX_HOP_LIMIT),
                                  "Frames unter 12 Byte duerfen nicht akzeptiert werden");
}

void test_nullzeiger_wird_verworfen(void)
{
    TEST_ASSERT_FALSE(isPlausibleAckFrame(NULL, 12, MAX_HOP_LIMIT));
}

// ---------------------------------------------------------------------------
// Draht-Anhang (ackWireAppendixLen() / ackWireHash(), siehe ack_attribution.h)
//
// Der Anhang darf ein ACK niemals kosten: jeder Fall wird zusaetzlich mit
// isPlausibleAckFrame() geprueft, das Byte 11 gar nicht ansieht.
// ---------------------------------------------------------------------------

void test_anhang_n0_liefert_laenge_null(void)
{
    uint8_t buf[12];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
    buf[11] = 0x00;

    TEST_ASSERT_EQUAL_UINT8(0, ackWireAppendixLen(buf, sizeof(buf)));
    TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT),
                              "n=0 darf das ACK nicht kosten");
}

void test_anhang_n3_wird_erkannt_und_hash_stimmt(void)
{
    uint8_t buf[15];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
    buf[11] = 0x03;
    buf[12] = 0x21;
    buf[13] = 0x5F;
    buf[14] = 0x3A;

    TEST_ASSERT_EQUAL_UINT8(3, ackWireAppendixLen(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0x3A5F21, ackWireHash(buf));
    TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT),
                              "n=3 darf das ACK nicht kosten");
}

void test_anhang_puffer_zu_kurz_wird_verworfen(void)
{
    uint8_t buf[14];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
    buf[11] = 0x03;
    buf[12] = 0x21;
    buf[13] = 0x5F;

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ackWireAppendixLen(buf, sizeof(buf)),
                                     "size 14 < 12+n=15 -- Anhang faellt");
    TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT),
                              "verworfener Anhang darf das ACK nicht kosten");
}

// Nur n == 3 wird akzeptiert -- alle anderen Werte, auch mit ausreichend
// Puffer, liefern 0.
void test_anhang_andere_laengen_werden_verworfen(void)
{
    struct { uint8_t n; uint16_t size; } cases[] = {
        { 6,  18 },
        { 10, 22 },
        { 11, 23 },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint8_t buf[24];
        memset(buf, 0xAA, sizeof(buf));
        buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
        buf[11] = cases[i].n;

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ackWireAppendixLen(buf, cases[i].size),
                                         "nur n=3 wird akzeptiert");
        TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, cases[i].size, MAX_HOP_LIMIT),
                                  "verworfener Anhang darf das ACK nicht kosten");
    }
}

// Byte 11 != 0 ohne Anhang (altes Format, ~0,4 % der Feldmessung) ist kein
// Fehler -- nur eben auch kein gueltiger Anhang.
void test_anhang_byte11_ohne_anhang_ist_kein_fehler(void)
{
    uint8_t buf[12];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
    buf[11] = 0x01;

    TEST_ASSERT_EQUAL_UINT8(0, ackWireAppendixLen(buf, sizeof(buf)));
    TEST_ASSERT_TRUE_MESSAGE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT),
                              "altes Format ohne Anhang bleibt ein gueltiges ACK");
}

void test_anhang_nullzeiger_liefert_null(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, ackWireAppendixLen(NULL, 15));
}

// Obere Bits von Byte 14 sind reserviert -- ackWireHash() maskiert auf 22 Bit.
void test_anhang_hash_wird_auf_22_bit_maskiert(void)
{
    uint8_t buf[15];
    buildAck(buf, 0x687B40C8, 0x84, 0x9BF3C002);
    buf[11] = 0x03;
    buf[12] = 0xFF;
    buf[13] = 0xFF;
    buf[14] = 0xFF;

    TEST_ASSERT_EQUAL_UINT8(3, ackWireAppendixLen(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0x3FFFFF, ackWireHash(buf));
    TEST_ASSERT_TRUE(isPlausibleAckFrame(buf, sizeof(buf), MAX_HOP_LIMIT));
}

// ---------------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_gueltige_hopwerte_werden_akzeptiert);
    RUN_TEST(test_frisch_erzeugter_ack_wird_akzeptiert);
    RUN_TEST(test_laengerer_frame_wird_akzeptiert);

    RUN_TEST(test_ascii_bruchstueck_wird_verworfen);
    RUN_TEST(test_server_bit_fehlt_wird_verworfen);
    RUN_TEST(test_absurdes_hop_budget_wird_verworfen);
    RUN_TEST(test_schranke_ist_exklusiv_oberhalb);

    RUN_TEST(test_falscher_typ_wird_verworfen);
    RUN_TEST(test_zu_kurzer_frame_wird_verworfen);
    RUN_TEST(test_nullzeiger_wird_verworfen);

    RUN_TEST(test_anhang_n0_liefert_laenge_null);
    RUN_TEST(test_anhang_n3_wird_erkannt_und_hash_stimmt);
    RUN_TEST(test_anhang_puffer_zu_kurz_wird_verworfen);
    RUN_TEST(test_anhang_andere_laengen_werden_verworfen);
    RUN_TEST(test_anhang_byte11_ohne_anhang_ist_kein_fehler);
    RUN_TEST(test_anhang_nullzeiger_liefert_null);
    RUN_TEST(test_anhang_hash_wird_auf_22_bit_maskiert);

    return UNITY_END();
}
