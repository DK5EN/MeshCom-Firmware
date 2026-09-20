// R1-02 Schritt 1. Erster ausfuehrbarer Test der Telefon-Rahmung ueberhaupt:
// bis hierher war sie nur ueber BLE beobachtbar (tools/bench/ble_golden.py
// plus Telefon), weshalb der Unterschied zwischen den beiden Kopien in
// sendToPhone()/sendComToPhone() jahrelang unbemerkt blieb.

#include <unity.h>
#include <string.h>

#include "../../src/ble_phone_frame.h"

static uint8_t out[300];

static void reset(void) { memset(out, 0, sizeof(out)); }

// --- 0x91 MHeard: Typbyte faellt weg, blelen-1 Bytes -----------------------

void test_mheard_drops_the_type_byte(void)
{
    reset();
    const uint8_t payload[] = {0x91, 'A', 'B', 'C'};
    TEST_ASSERT_TRUE(blePhoneFrame(payload, 4, out, sizeof(out)));
    // blelen-1 == 3 Bytes ab payload[0]: das Typbyte selbst und 'A','B'
    TEST_ASSERT_EQUAL_UINT8(0x91, out[0]);
    TEST_ASSERT_EQUAL_UINT8('A', out[1]);
    TEST_ASSERT_EQUAL_UINT8('B', out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[3]);   // 'C' wird NICHT mehr kopiert
}

// --- 0x44 JSON: unveraendert, blelen Bytes ---------------------------------

void test_json_is_copied_whole(void)
{
    reset();
    const uint8_t payload[] = {0x44, '{', '}', 0x00};
    TEST_ASSERT_TRUE(blePhoneFrame(payload, 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0x44, out[0]);
    TEST_ASSERT_EQUAL_UINT8('{', out[1]);
    TEST_ASSERT_EQUAL_UINT8('}', out[2]);
}

// --- sonst: 0x40 davor, Nutzlast VOLLSTAENDIG -----------------------------
//
// Das ist der Arm, in dem sich die beiden Kopien unterschieden. Massgeblich
// ist sendToPhone(): blelen, nicht blelen-1. Ein Text darf am Telefon nicht
// um sein letztes Zeichen gekuerzt ankommen.

void test_text_gets_the_tag_and_keeps_its_last_byte(void)
{
    reset();
    const uint8_t payload[] = {':', 'h', 'i', '!'};
    TEST_ASSERT_TRUE(blePhoneFrame(payload, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(BLE_PHONE_TAG_TEXT, out[0]);
    TEST_ASSERT_EQUAL_UINT8(':', out[1]);
    TEST_ASSERT_EQUAL_UINT8('h', out[2]);
    TEST_ASSERT_EQUAL_UINT8('i', out[3]);
    TEST_ASSERT_EQUAL_UINT8('!', out[4]);   // die blelen-1-Fassung verlor dies
}

void test_position_frames_take_the_text_arm(void)
{
    reset();
    const uint8_t payload[] = {'!', 'p', 'o', 's'};
    TEST_ASSERT_TRUE(blePhoneFrame(payload, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(BLE_PHONE_TAG_TEXT, out[0]);
    TEST_ASSERT_EQUAL_UINT8('!', out[1]);
    TEST_ASSERT_EQUAL_UINT8('s', out[4]);
}

// --- N-04-Rest: blelen == 0 darf NICHT auf 255 unterlaufen -----------------

void test_zero_length_is_refused_not_underflowed(void)
{
    reset();
    const uint8_t payload[] = {0x91, 'x'};
    TEST_ASSERT_FALSE(blePhoneFrame(payload, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);   // nichts geschrieben
}

// --- Zielpuffer zu klein: refuse, nicht ueberschreiben ---------------------

void test_oversize_is_refused_in_every_arm(void)
{
    uint8_t small[4];
    const uint8_t json[]   = {0x44, 1, 2, 3, 4, 5};
    const uint8_t mheard[] = {0x91, 1, 2, 3, 4, 5};
    const uint8_t text[]   = {':',  1, 2, 3, 4, 5};

    memset(small, 0xEE, sizeof(small));
    TEST_ASSERT_FALSE(blePhoneFrame(json, 6, small, sizeof(small)));
    TEST_ASSERT_FALSE(blePhoneFrame(mheard, 6, small, sizeof(small)));
    TEST_ASSERT_FALSE(blePhoneFrame(text, 4, small, sizeof(small)));
    for (size_t i = 0; i < sizeof(small); i++)
        TEST_ASSERT_EQUAL_UINT8(0xEE, small[i]);   // unberuehrt
}

// --- der Text-Arm braucht genau ein Byte mehr als seine Nutzlast -----------

void test_text_arm_fits_exactly_at_the_boundary(void)
{
    uint8_t exact[5];
    const uint8_t text[] = {'a', 'b', 'c', 'd'};
    memset(exact, 0, sizeof(exact));
    TEST_ASSERT_TRUE(blePhoneFrame(text, 4, exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_UINT8(BLE_PHONE_TAG_TEXT, exact[0]);
    TEST_ASSERT_EQUAL_UINT8('d', exact[4]);
}

void test_null_pointers_are_refused(void)
{
    const uint8_t payload[] = {0x44, 'x'};
    TEST_ASSERT_FALSE(blePhoneFrame(0, 2, out, sizeof(out)));
    TEST_ASSERT_FALSE(blePhoneFrame(payload, 2, 0, sizeof(out)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_mheard_drops_the_type_byte);
    RUN_TEST(test_json_is_copied_whole);
    RUN_TEST(test_text_gets_the_tag_and_keeps_its_last_byte);
    RUN_TEST(test_position_frames_take_the_text_arm);
    RUN_TEST(test_zero_length_is_refused_not_underflowed);
    RUN_TEST(test_oversize_is_refused_in_every_arm);
    RUN_TEST(test_text_arm_fits_exactly_at_the_boundary);
    RUN_TEST(test_null_pointers_are_refused);
    return UNITY_END();
}

void setUp(void) {}
void tearDown(void) {}
