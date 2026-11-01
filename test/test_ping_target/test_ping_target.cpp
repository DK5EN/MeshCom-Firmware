// LRF 2.1: ping target rule (pingTargetValid), operator decision D1.
//
//   pio test -e native -f test_ping_target
//
// Define PING_TARGET_OLD_RULE to run the same cases against the old
// checkRegexCall()-only rule (the "before" run).

#include <unity.h>

#include <Arduino.h>
#include <regex_functions.h>
#include <ping_target.h>

static const char *OWN = "OE3XIR-12";

#ifdef PING_TARGET_OLD_RULE
static bool valid(const char *c, const char *) { return checkRegexCall(String(c)); }
#else
static bool valid(const char *c, const char *own) { return pingTargetValid(c, own); }
#endif

void setUp(void) {}
void tearDown(void) {}

static void test_rejects_junk_and_groups(void)
{
    TEST_ASSERT_FALSE_MESSAGE(valid("255.", "OE3XWJ-12"), "255.");
    TEST_ASSERT_FALSE_MESSAGE(valid("2025", "OE3XWJ-12"), "2025");
    TEST_ASSERT_FALSE_MESSAGE(valid("", OWN), "empty");
}

static void test_rejects_special_targets(void)
{
    TEST_ASSERT_FALSE_MESSAGE(valid("*", OWN), "*");
    TEST_ASSERT_FALSE_MESSAGE(valid("TEST", OWN), "TEST");
    TEST_ASSERT_FALSE_MESSAGE(valid("HG", OWN), "HG");
    TEST_ASSERT_FALSE_MESSAGE(valid("H", OWN), "H");
    TEST_ASSERT_FALSE_MESSAGE(valid("TESTER", OWN), "TESTER");
    TEST_ASSERT_FALSE_MESSAGE(valid("WLNK-1", OWN), "WLNK-1");
}

static void test_rejects_own_call(void)
{
    TEST_ASSERT_FALSE(valid("OE3XIR-12", "OE3XIR-12"));
}

static void test_accepts_callsign_with_ssid(void)
{
    TEST_ASSERT_TRUE(valid("OE3XIR-12", "DK5EN-1"));
    TEST_ASSERT_TRUE(valid("DK5EN", "OE3XIR-12"));
}

static void test_rejects_leading_zero_ssid(void)
{
    TEST_ASSERT_FALSE(valid("OE3XIR-05", "DK5EN-1"));
}

#ifndef PING_TARGET_OLD_RULE
// The pure shape matcher must agree with the Regexp-based checkRegexCall().
static void test_shape_matches_checkRegexCall(void)
{
    static const char *const cases[] = { "OE3XIR-12", "OE3XIR-5", "OE3XIR-", "DK5EN", "DL2JA-99", "9A1ABC",
                                         "OE1KBC", "A", "1", "AB", "DE", "255.", "2025", "OE3XIRA-1", "OE3XIRAB",
                                         "DK5EN-123", "DK5EN12", "dk5en", "OE3XIR-1-2", "KK6AB", "3DA0XYZ-1",
                                         "W1AW", "DK5EN-0", "-1", "12AB3C" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        TEST_ASSERT_EQUAL_MESSAGE(checkRegexCall(String(cases[i])), pingTargetIsCallShape(cases[i]), cases[i]);
}

static void test_reason_codes(void)
{
    TEST_ASSERT_EQUAL_INT(PING_TARGET_GROUP, pingTargetCheck("2025", OWN));
    TEST_ASSERT_EQUAL_INT(PING_TARGET_SPECIAL, pingTargetCheck("TEST", OWN));
    TEST_ASSERT_EQUAL_INT(PING_TARGET_OWN, pingTargetCheck("OE3XIR-12", OWN));
    TEST_ASSERT_EQUAL_INT(PING_TARGET_NOT_CALL, pingTargetCheck("255.", OWN));
    TEST_ASSERT_EQUAL_INT(PING_TARGET_OK, pingTargetCheck("DK5EN-1", OWN));
}
#endif

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_junk_and_groups);
    RUN_TEST(test_rejects_special_targets);
    RUN_TEST(test_rejects_own_call);
    RUN_TEST(test_accepts_callsign_with_ssid);
    RUN_TEST(test_rejects_leading_zero_ssid);
#ifndef PING_TARGET_OLD_RULE
    RUN_TEST(test_shape_matches_checkRegexCall);
    RUN_TEST(test_reason_codes);
#endif
    return UNITY_END();
}
