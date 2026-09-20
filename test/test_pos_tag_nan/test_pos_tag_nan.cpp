// Native Testsuite fuer posTagIsNan() -- NaN-Guard fuer die optionalen
// APRS-Tags des Positionsbeacons (/P=, /H=, /T=, /O=, /F=, /Q=, /G=, /C= in
// PositionToAPRS(), src/loop_functions.cpp). Vorher verglich jeder der acht
// Guards den falschen Puffer (immer cpress statt seines eigenen), sodass
// z.B. ein NaN in der Luftfeuchtigkeit als "/H=nan" on air ging statt vom
// Guard abgefangen zu werden. Siehe src/pos_tag_nan.h.
//
//   pio test -e native_parsers -f test_pos_tag_nan

#include <unity.h>
#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <aprs_structures.h>
#include <nrf52/WisBlock-API.h>
#include <parser_link_stubs.h>
#include <pos_tag_nan.h>

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp/mheard_functions.cpp/via_functions.cpp
// (env:native_parsers linkt alle drei Parser in jedes Testprogramm der Env,
// siehe test/test_decodemheard/stubs/parser_link_stubs.h; pos_tag_nan.h
// selbst braucht keinen davon, aber build_src_filter ist env-weit, nicht
// pro Test-Case)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631 -- int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

static void test_p_nan_is_true(void)
{
    TEST_ASSERT_TRUE(posTagIsNan("/P=nan"));
}

static void test_h_negative_nan_is_true(void)
{
    TEST_ASSERT_TRUE(posTagIsNan("/H=-nan"));
}

static void test_t_numeric_value_is_false(void)
{
    TEST_ASSERT_FALSE(posTagIsNan("/T=22.5"));
}

// Der zuvor falsch verglichene Fall: /H=nan wurde vor dem Fix gegen
// "/P=nan" geprueft und daher NICHT erkannt.
static void test_h_nan_is_true(void)
{
    TEST_ASSERT_TRUE(posTagIsNan("/H=nan"));
}

static void test_q_numeric_value_is_false(void)
{
    TEST_ASSERT_FALSE(posTagIsNan("/Q=1013.2"));
}

static void test_empty_string_is_false(void)
{
    TEST_ASSERT_FALSE(posTagIsNan(""));
}

static void test_g_empty_value_is_false(void)
{
    TEST_ASSERT_FALSE(posTagIsNan("/G="));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_p_nan_is_true);
    RUN_TEST(test_h_negative_nan_is_true);
    RUN_TEST(test_t_numeric_value_is_false);
    RUN_TEST(test_h_nan_is_true);
    RUN_TEST(test_q_numeric_value_is_false);
    RUN_TEST(test_empty_string_is_false);
    RUN_TEST(test_g_empty_value_is_false);
    return UNITY_END();
}
