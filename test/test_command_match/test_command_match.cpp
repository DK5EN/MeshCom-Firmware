// D2-10: the command ladder's matching rule (src/command_match.h).
//
// Until this suite existed the rule had NO executable test: it lives in
// command_functions.cpp, which pulls in Arduino/LVGL/the radio stack and is
// compiled by no env:native*. The only thing standing between a wrong rule
// and the fleet was the ordering lint plus a hardware capture.
#include <unity.h>

#include "command_match.h"
#include "command_dash.h"

#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>

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

// --- CMD-DASH-01: look-alike dashes before a command (discussion #1042) -------------
// UTF-8 spelled out as escapes so this file stays ASCII.
#define MINUS "\xE2\x88\x92"   // U+2212 minus sign (what the command web page carried)
#define ENDASH "\xE2\x80\x93"  // U+2013
#define EMDASH "\xE2\x80\x94"  // U+2014 (phone keyboards turn "--" into this)

static std::string fold(const char *in)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "%s", in);
    const size_t n = commandFoldDashes(buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    return std::string(buf);
}

static void test_dash_two_minus_signs_become_a_command(void)
{
    TEST_ASSERT_EQUAL_STRING("--save", fold(MINUS MINUS "save").c_str());
    // #1042: the exact text copied from the command page
    TEST_ASSERT_EQUAL_STRING("--loradebug off", fold(MINUS MINUS "loradebug off").c_str());
    TEST_ASSERT_EQUAL_STRING("--setgrc232;2321;262;20", fold(MINUS MINUS "setgrc232;2321;262;20").c_str());
}

static void test_dash_single_en_or_em_dash_becomes_a_command(void)
{
    TEST_ASSERT_EQUAL_STRING("--loradebug off", fold(EMDASH "loradebug off").c_str());
    TEST_ASSERT_EQUAL_STRING("--info", fold(ENDASH "info").c_str());
    TEST_ASSERT_EQUAL_STRING("--info", fold(EMDASH EMDASH "info").c_str());
}

static void test_dash_mixed_ascii_and_unicode(void)
{
    TEST_ASSERT_EQUAL_STRING("--save", fold("-" MINUS "save").c_str());
    TEST_ASSERT_EQUAL_STRING("--save", fold(MINUS "-save").c_str());
}

static void test_dash_every_command_of_a_multi_command_line(void)
{
    TEST_ASSERT_EQUAL_STRING("--setcall DK5EN-1 --save",
                             fold(MINUS MINUS "setcall DK5EN-1 " EMDASH "save").c_str());
    TEST_ASSERT_EQUAL_STRING("  --info", fold("  " EMDASH "info").c_str());
}

static void test_dash_plain_ascii_is_untouched(void)
{
    TEST_ASSERT_EQUAL_STRING("--loradebug off", fold("--loradebug off").c_str());
    TEST_ASSERT_EQUAL_STRING("-info", fold("-info").c_str());   // one ASCII hyphen stays a wrong command
    TEST_ASSERT_EQUAL_STRING("", fold("").c_str());
    TEST_ASSERT_EQUAL_size_t(0, commandFoldDashes(NULL));
}

static void test_dash_in_text_arguments_is_kept(void)
{
    // not followed by a lowercase letter
    TEST_ASSERT_EQUAL_STRING("--setname Max " EMDASH "Moritz", fold("--setname Max " EMDASH "Moritz").c_str());
    TEST_ASSERT_EQUAL_STRING("--atxt a " EMDASH " b", fold("--atxt a " EMDASH " b").c_str());
    TEST_ASSERT_EQUAL_STRING("--atxt 5 " MINUS "3", fold("--atxt 5 " MINUS "3").c_str());
    // not at a word start
    TEST_ASSERT_EQUAL_STRING("--atxt abc" EMDASH "def", fold("--atxt abc" EMDASH "def").c_str());
    // three dashes are not a command
    TEST_ASSERT_EQUAL_STRING(EMDASH EMDASH EMDASH "save", fold(EMDASH EMDASH EMDASH "save").c_str());
    // a lone dash at the end
    TEST_ASSERT_EQUAL_STRING("--atxt x " EMDASH, fold("--atxt x " EMDASH).c_str());
}

static void test_dash_folded_line_reaches_the_ladder(void)
{
    // the ladder matches msg_text+2 against the command name; before the fold this was "wrong command"
    const std::string raw = MINUS MINUS "loradebug off";
    TEST_ASSERT_FALSE(raw.compare(0, 2, "--") == 0);
    const std::string folded = fold(raw.c_str());
    TEST_ASSERT_TRUE(folded.compare(0, 2, "--") == 0);
    TEST_ASSERT_TRUE(commandMatches(folded.c_str() + 2, "loradebug off"));
}

static bool dash_file_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

static std::string dash_read_repo_file(const char *rel)
{
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        if (dash_file_exists(dir + "/test/test_command_match/test_command_match.cpp") && dash_file_exists(dir + "/platformio.ini"))
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str());
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Source pin: both commandAction() entry points fold before they look at "--".
static void test_dash_both_entry_points_fold_first(void)
{
    const std::string src = dash_read_repo_file("src/command_functions.cpp");

    const size_t multi = src.find("void commandAction(char *msg_text, int iphone, bool rxFromPhone)");
    TEST_ASSERT_TRUE_MESSAGE(multi != std::string::npos, "no multi-command commandAction()");
    const size_t splitter = src.find("memcmp(msg_text+ipos, \"--\", 2)", multi);
    const size_t foldMulti = src.find("commandFoldDashes(msg_text);", multi);
    TEST_ASSERT_TRUE_MESSAGE(splitter != std::string::npos, "no splitter in the multi-command entry");
    TEST_ASSERT_TRUE_MESSAGE(foldMulti != std::string::npos && foldMulti < splitter,
                             "the multi-command entry does not fold before the splitter");

    const size_t single = src.find("void commandAction(char *umsg_text, bool ble)");
    TEST_ASSERT_TRUE_MESSAGE(single != std::string::npos, "no single-command commandAction()");
    const size_t sVar = src.find("String sVar = umsg_text;", single);
    const size_t foldSingle = src.find("commandFoldDashes(umsg_text);", single);
    TEST_ASSERT_TRUE_MESSAGE(sVar != std::string::npos, "no sVar copy in the single-command entry");
    TEST_ASSERT_TRUE_MESSAGE(foldSingle != std::string::npos && foldSingle < sVar,
                             "the single-command entry does not fold before it copies the line");
}

// Upstream #1288: the "aprswx" status rung must not catch "aprswx on/off" (TG_BRETURN
// toggle rows). commandMatches() lets them through (a space ends the token);
// commandMatchesBare() only takes the bare status line.
void test_bare_status_rung_does_not_catch_toggle_forms(void)
{
    TEST_ASSERT_TRUE(commandMatches("aprswx on", "aprswx"));   // the defect
    TEST_ASSERT_FALSE(commandMatchesBare("aprswx on", "aprswx"));
    TEST_ASSERT_FALSE(commandMatchesBare("aprswx off", "aprswx"));
    TEST_ASSERT_FALSE(commandMatchesBare("aprswxint 10", "aprswx"));
    TEST_ASSERT_TRUE(commandMatchesBare("aprswx", "aprswx"));
    TEST_ASSERT_TRUE(commandMatchesBare("APRSWX", "aprswx"));
    TEST_ASSERT_TRUE(commandMatchesBare("aprswx \r\n", "aprswx"));
    TEST_ASSERT_FALSE(commandMatchesBare("aprsw", "aprswx"));
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
    RUN_TEST(test_bare_status_rung_does_not_catch_toggle_forms);
    RUN_TEST(test_short_input_never_matches_a_long_command);
    RUN_TEST(test_matching_is_case_insensitive);
    RUN_TEST(test_overlong_input_is_not_truncated_into_a_match);
    RUN_TEST(test_dash_two_minus_signs_become_a_command);
    RUN_TEST(test_dash_single_en_or_em_dash_becomes_a_command);
    RUN_TEST(test_dash_mixed_ascii_and_unicode);
    RUN_TEST(test_dash_every_command_of_a_multi_command_line);
    RUN_TEST(test_dash_plain_ascii_is_untouched);
    RUN_TEST(test_dash_in_text_arguments_is_kept);
    RUN_TEST(test_dash_folded_line_reaches_the_ladder);
    RUN_TEST(test_dash_both_entry_points_fold_first);
    return UNITY_END();
}
