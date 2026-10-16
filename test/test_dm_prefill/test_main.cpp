// Native tests for the T-Deck DM prefill decision (dm_prefill.h).
//
//   pio test -e native -f test_dm_prefill

#include <unity.h>

#include "t-deck/dm_prefill.h"

void setUp(void) {}
void tearDown(void) {}

#define OWN "DK5EN-14"

static void test_targets(void)
{
    TEST_ASSERT_EQUAL_STRING("DL1ABC-1", dmTargetForGroup("DL1ABC-1", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("9", dmTargetForGroup("9", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("*", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("MSG", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup(NULL, OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("booting...", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("dk5en-14", OWN).c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("DK5EN-14", "dk5en-14").c_str());
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup(" DK5EN-14 ", " dk5en-14 ").c_str());
    // longer than the 9-char dm_callsign field
    TEST_ASSERT_EQUAL_STRING("", dmTargetForGroup("DL1ABCDE-12", OWN).c_str());
}

static void test_empty_field_writes(void)
{
    DmPrefill d = dmPrefillDecide("", "", "DL1ABC-1", OWN);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_STRING("DL1ABC-1", d.value.c_str());
}

static void test_last_auto_overwritten(void)
{
    DmPrefill d = dmPrefillDecide("DL1ABC-1", "DL1ABC-1", "DL2XYZ", OWN);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_STRING("DL2XYZ", d.value.c_str());
    // switching to "*" clears the auto value
    d = dmPrefillDecide("DL1ABC-1", "DL1ABC-1", "*", OWN);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL_STRING("", d.value.c_str());
}

static void test_user_value_kept(void)
{
    DmPrefill d = dmPrefillDecide("OE1ABC", "DL1ABC-1", "DL2XYZ", OWN);
    TEST_ASSERT_FALSE(d.write);
    d = dmPrefillDecide("OE1ABC", "", "DL2XYZ", OWN);
    TEST_ASSERT_FALSE(d.write);
}

static void test_explicit_equal_to_old_auto_wins(void)
{
    // Auto wrote DL1ABC-1, then reply button set DL1ABC-1 explicitly
    // (marker reset to ""), active tab is another conversation.
    DmPrefill d = dmPrefillDecide("DL1ABC-1", "", "DL2XYZ", OWN);
    TEST_ASSERT_FALSE(d.write);
}

static void test_no_redundant_write(void)
{
    DmPrefill d = dmPrefillDecide("", "", "*", OWN);
    TEST_ASSERT_FALSE(d.write);
}

static void test_sender(void)
{
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", dmSenderFromHeader("DK5EN-1,DB0XYZ-12 -> DK5EN-14").c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", dmSenderFromHeader("DK5EN-1 -> *").c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", dmSenderFromHeader("DK5EN-1").c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", dmSenderFromHeader("  DK5EN-1 , X  ->  Y ").c_str());
    TEST_ASSERT_EQUAL_STRING("", dmSenderFromHeader("").c_str());
    TEST_ASSERT_EQUAL_STRING("", dmSenderFromHeader(NULL).c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_targets);
    RUN_TEST(test_empty_field_writes);
    RUN_TEST(test_last_auto_overwritten);
    RUN_TEST(test_user_value_kept);
    RUN_TEST(test_explicit_equal_to_old_auto_wins);
    RUN_TEST(test_no_redundant_write);
    RUN_TEST(test_sender);
    return UNITY_END();
}
