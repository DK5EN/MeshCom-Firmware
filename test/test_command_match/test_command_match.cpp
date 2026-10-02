// D2-10: the command ladder's matching rule (src/command_match.h).
//
// Until this suite existed the rule had NO executable test: it lives in
// command_functions.cpp, which pulls in Arduino/LVGL/the radio stack and is
// compiled by no env:native*. The only thing standing between a wrong rule
// and the fleet was the ordering lint plus a hardware capture.
#include <unity.h>

#include "command_match.h"

// ---------------------------------------------------------------------------
// The old rule, kept verbatim so every case below can state what CHANGED.
// This is what shipped before D2-10: truncate the input to the candidate's
// length and compare. It is here as a reference implementation only.
// ---------------------------------------------------------------------------
static bool oldPrefixMatch(const char *msg, const char *command)
{
    char vmsg[100];
    strncpy(vmsg, msg, sizeof(vmsg) - 1);
    vmsg[sizeof(vmsg) - 1] = '\0';
    if (strlen(vmsg) < strlen(command))
        return false; // the old code read uninitialised stack here
    vmsg[strlen(command)] = 0x00;
    return casecmp(vmsg, command) == 0;
}

// --- exact token -----------------------------------------------------------

static void test_bare_name_matches(void)
{
    TEST_ASSERT_TRUE(commandMatches("pos", "pos"));
    TEST_ASSERT_TRUE(commandMatches("info", "info"));
}

// The defect D2-10 exists for. Each of these really is a live pair in the
// ladder: msg/msgid, pos/posshot, instr/instreset, mh/mheard.
static void test_a_longer_name_no_longer_matches_a_shorter_command(void)
{
    TEST_ASSERT_FALSE(commandMatches("msgid", "msg"));
    TEST_ASSERT_FALSE(commandMatches("posshot", "pos"));
    TEST_ASSERT_FALSE(commandMatches("instreset", "instr"));
    TEST_ASSERT_FALSE(commandMatches("mheard", "mh"));

    // ... and that this is a CHANGE, not something that already held.
    TEST_ASSERT_TRUE(oldPrefixMatch("msgid", "msg"));
    TEST_ASSERT_TRUE(oldPrefixMatch("posshot", "pos"));
    TEST_ASSERT_TRUE(oldPrefixMatch("instreset", "instr"));
    TEST_ASSERT_TRUE(oldPrefixMatch("mheard", "mh"));
}

// Bench-checked on DK5EN-93: --posx / --infox / --msgidx are rejected.
static void test_garbage_suffix_is_rejected(void)
{
    TEST_ASSERT_FALSE(commandMatches("posx", "pos"));
    TEST_ASSERT_FALSE(commandMatches("infox", "info"));
    TEST_ASSERT_TRUE(oldPrefixMatch("posx", "pos")); // used to run --pos
}

// A space still terminates, so setters that take an argument keep working.
static void test_space_terminates_an_exact_token(void)
{
    TEST_ASSERT_TRUE(commandMatches("setctry 1", "setctry"));
    TEST_ASSERT_TRUE(commandMatches("maxhop 4", "maxhop"));
}

// The line arrives with its terminator on some transports.
static void test_line_endings_terminate(void)
{
    TEST_ASSERT_TRUE(commandMatches("pos\r", "pos"));
    TEST_ASSERT_TRUE(commandMatches("pos\n", "pos"));
    TEST_ASSERT_TRUE(commandMatches("pos\r\n", "pos"));
}

// --- argument form (command ends in a space) -------------------------------

static void test_trailing_space_is_a_prefix_match(void)
{
    TEST_ASSERT_TRUE(commandMatches("setname Martin", "setname "));
    TEST_ASSERT_TRUE(commandMatches("postime 600", "postime "));
    // The value may be anything, including something that looks like a name.
    TEST_ASSERT_TRUE(commandMatches("setname pos", "setname "));
}

static void test_argument_form_still_needs_the_space(void)
{
    TEST_ASSERT_FALSE(commandMatches("setnameMartin", "setname "));
}

// --- the two-word forms the ladder uses ------------------------------------

static void test_two_word_commands(void)
{
    TEST_ASSERT_TRUE(commandMatches("mesh on", "mesh on"));
    TEST_ASSERT_FALSE(commandMatches("mesh off", "mesh on"));
    // `softser app0` must NOT be swallowed by `softser app` -- the exact
    // defect command_ladder_lint.py was written for.
    TEST_ASSERT_FALSE(commandMatches("softser app0", "softser app"));
    TEST_ASSERT_TRUE(oldPrefixMatch("softser app0", "softser app"));
}

// --- boundaries ------------------------------------------------------------

static void test_short_input_never_matches_a_long_command(void)
{
    TEST_ASSERT_FALSE(commandMatches("ball", "balledge on"));
    TEST_ASSERT_FALSE(commandMatches("", "pos"));
}

static void test_matching_is_case_insensitive(void)
{
    TEST_ASSERT_TRUE(commandMatches("POS", "pos"));
    TEST_ASSERT_TRUE(commandMatches("SetName Martin", "setname "));
}

// An input longer than the old 100-byte scratch buffer must not match a short
// command by being truncated into one.
static void test_overlong_input_is_not_truncated_into_a_match(void)
{
    char longmsg[300];
    memset(longmsg, 'a', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    memcpy(longmsg, "pos", 3);
    TEST_ASSERT_FALSE(commandMatches(longmsg, "pos"));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bare_name_matches);
    RUN_TEST(test_a_longer_name_no_longer_matches_a_shorter_command);
    RUN_TEST(test_garbage_suffix_is_rejected);
    RUN_TEST(test_space_terminates_an_exact_token);
    RUN_TEST(test_line_endings_terminate);
    RUN_TEST(test_trailing_space_is_a_prefix_match);
    RUN_TEST(test_argument_form_still_needs_the_space);
    RUN_TEST(test_two_word_commands);
    RUN_TEST(test_short_input_never_matches_a_long_command);
    RUN_TEST(test_matching_is_case_insensitive);
    RUN_TEST(test_overlong_input_is_not_truncated_into_a_match);
    return UNITY_END();
}
