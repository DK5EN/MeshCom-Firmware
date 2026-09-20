/**
 * toggleApply() -- the dispatcher behind the D2-06 toggle table.
 *
 * command_functions.cpp is compiled by no native env (Arduino, LVGL, the radio
 * stack), so the 70 rungs this table replaced never had an executable test.
 * The logic that now stands in for all of them therefore gets one here, with
 * the cases chosen from the things that actually differ between rows -- the
 * ones a hand transcription would have flattened:
 *
 *   - the mask is an and/or PAIR, not a bit number. `node_sset*` are `int`,
 *     so `&= ~0x0020` has to leave bits 16-31 alone while the literal
 *     `& 0x7FEF` that four `off` rungs use has to clear them.
 *   - a row may set its flag true while CLEARING its bit (`mesh on`).
 *   - post() runs before the mask for four rows and after it for the rest.
 *   - a row reports `breturn` (fall through to the ladder tail) or not
 *     (the rung used a bare `return;`) -- the caller depends on the difference.
 */
#include <unity.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "command_toggles.h"

static bool  flagA, flagB;
static int   ssetA;
static int   post_calls;
static int   post_saw_sset;   // value of ssetA at the moment post() ran

static void post_probe() { ++post_calls; post_saw_sset = ssetA; }

static const ToggleRow TBL[] =
{
    // name              flag    sset    and_mask     or_mask      post         dirty            opt
    { "--alpha on",     &flagA, &ssetA, 0xFFFFFFFF,  0x0008,      nullptr,     TG_DIRTY_NODE,   TG_SAVE | TG_FLAG_TRUE | TG_BLE_ECHO },
    { "--alpha off",    &flagA, &ssetA, 0xFFFFFFF7,  0x00000000,  nullptr,     TG_DIRTY_NODE,   TG_SAVE | TG_BRETURN },
    { "--lit off",      &flagB, &ssetA, 0x00007FEF,  0x00000000,  nullptr,     TG_DIRTY_NONE,   TG_SAVE },
    { "--inv on",       &flagB, &ssetA, 0xFFFFFFDF,  0x00000000,  nullptr,     TG_DIRTY_SENS,   TG_FLAG_TRUE },
    { "--late on",      &flagA, &ssetA, 0xFFFFFFFF,  0x0040,      post_probe,  TG_DIRTY_NONE,   0 },
    { "--early on",     &flagA, &ssetA, 0xFFFFFFFF,  0x0040,      post_probe,  TG_DIRTY_NONE,   TG_POST_FIRST },
    { "--bare on",      nullptr,nullptr,0xFFFFFFFF,  0x00000000,  nullptr,     TG_DIRTY_NONE,   TG_ECHO_LN },
    { "--echof on",     nullptr,nullptr,0xFFFFFFFF,  0x00000000,  nullptr,     TG_DIRTY_NONE,   TG_ECHO_F },
};
static const size_t N = sizeof(TBL) / sizeof(TBL[0]);

static void reset()
{
    flagA = flagB = false;
    ssetA = 0;
    post_calls = 0;
    post_saw_sset = -1;
}

static ToggleAction run(const char *cmd) { return toggleApply(TBL, N, cmd); }

// Rows that hand a hook back must not have run it yet; rows that do not have a
// hook must hand back nothing.
static void expect_no_pending_post(const ToggleAction &a)
{
    TEST_ASSERT_TRUE(a.post_after == nullptr);
}

// --------------------------------------------------------------------------

static void test_unknown_command_is_not_matched_and_changes_nothing()
{
    reset();
    ssetA = 0x1234;
    ToggleAction a = run("nosuchthing on");
    TEST_ASSERT_FALSE(a.matched);
    TEST_ASSERT_EQUAL_INT(0x1234, ssetA);
    TEST_ASSERT_FALSE(flagA);
    TEST_ASSERT_EQUAL_INT(0, post_calls);
}

static void test_on_row_sets_flag_and_ors_its_bit()
{
    reset();
    ToggleAction a = run("alpha on");
    expect_no_pending_post(a);
    TEST_ASSERT_TRUE(a.matched);
    TEST_ASSERT_TRUE(flagA);
    TEST_ASSERT_EQUAL_INT(0x0008, ssetA);
    TEST_ASSERT_TRUE(a.save);
    TEST_ASSERT_EQUAL_UINT8(TG_DIRTY_NODE, a.dirty);
    TEST_ASSERT_TRUE(a.ble_echo);
    TEST_ASSERT_EQUAL_STRING("--alpha on", a.name);
}

static void test_off_row_clears_flag_and_its_bit()
{
    reset();
    ssetA = 0x0009;
    ToggleAction a = run("alpha off");
    TEST_ASSERT_TRUE(a.matched);
    TEST_ASSERT_FALSE(flagA);
    TEST_ASSERT_EQUAL_INT(0x0001, ssetA);     // bit 3 cleared, bit 0 untouched
}

// The point of a 32-bit and_mask: ~0x0008 must not disturb the high half.
static void test_tilde_mask_leaves_the_upper_bits_alone()
{
    reset();
    ssetA = (int)0x7FFF0008u;
    run("alpha off");
    TEST_ASSERT_EQUAL_HEX32(0x7FFF0000u, (unsigned)ssetA);
}

// ...and the point of storing the LITERAL mask: `& 0x7FEF` must clear them.
static void test_literal_mask_clears_the_upper_bits_like_the_rung_did()
{
    reset();
    ssetA = (int)0x7FFF7FFFu;
    run("lit off");
    TEST_ASSERT_EQUAL_HEX32(0x00007FEFu, (unsigned)ssetA);
}

// `mesh on` in the real table: flag true, bit cleared. A "bit + polarity"
// column would have got this backwards.
static void test_a_row_can_set_its_flag_while_clearing_its_bit()
{
    reset();
    ssetA = 0x0020;
    ToggleAction a = run("inv on");
    TEST_ASSERT_TRUE(flagB);
    TEST_ASSERT_EQUAL_INT(0x0000, ssetA);
    TEST_ASSERT_EQUAL_UINT8(TG_DIRTY_SENS, a.dirty);
    TEST_ASSERT_FALSE(a.save);
}

// A row without TG_POST_FIRST does NOT run its hook inside toggleApply(): it
// hands it back so the caller can run it AFTER save_settings(), which is where
// the ladder had it. setupINA226() zeroes four PERSISTED settings floats when
// the chip is absent, so running it before the save would write those to flash.
static void test_post_is_handed_back_to_run_after_the_save()
{
    reset();
    ToggleAction a = run("late on");
    TEST_ASSERT_EQUAL_INT(0, post_calls);           // not run yet
    TEST_ASSERT_TRUE(a.post_after == post_probe);
    TEST_ASSERT_EQUAL_INT(0x0040, ssetA);           // mask already applied
    a.post_after();
    TEST_ASSERT_EQUAL_INT(1, post_calls);
    TEST_ASSERT_EQUAL_INT(0x0040, post_saw_sset);
}

static void test_post_first_runs_before_the_mask()
{
    reset();
    ToggleAction a = run("early on");
    TEST_ASSERT_EQUAL_INT(1, post_calls);           // run inside toggleApply
    TEST_ASSERT_EQUAL_INT(0x0000, post_saw_sset);   // mask not applied yet
    TEST_ASSERT_EQUAL_INT(0x0040, ssetA);           // but applied afterwards
    TEST_ASSERT_TRUE(a.post_after == nullptr);      // and not handed back twice
}

static void test_breturn_distinguishes_fallthrough_from_bare_return()
{
    reset();
    TEST_ASSERT_FALSE(run("alpha on").breturn);     // rung used `return;`
    reset();
    TEST_ASSERT_TRUE(run("alpha off").breturn);     // rung set bReturn = true
}

static void test_rows_without_flag_or_sset_are_safe()
{
    reset();
    ssetA = 0x1234;
    ToggleAction a = run("bare on");
    TEST_ASSERT_TRUE(a.matched);
    TEST_ASSERT_EQUAL_INT(0x1234, ssetA);           // untouched
    TEST_ASSERT_EQUAL_UINT8(TG_ECHO_LN, a.echo);
    TEST_ASSERT_FALSE(a.ble_echo);
}

static void test_echo_style_is_reported_per_row()
{
    reset();
    TEST_ASSERT_EQUAL_UINT8(TG_ECHO_LN, run("bare on").echo);
    reset();
    TEST_ASSERT_EQUAL_UINT8(TG_ECHO_F, run("echof on").echo);
    reset();
    TEST_ASSERT_EQUAL_UINT8(0, run("alpha on").echo);
}

// D2-10's rule has to hold through the table, or hoisting it above the rest of
// the ladder would swallow commands that are not toggles at all.
static void test_matching_is_exact_token_not_prefix()
{
    reset();
    TEST_ASSERT_FALSE(run("alpha onx").matched);
    TEST_ASSERT_FALSE(run("alph on").matched);
    TEST_ASSERT_FALSE(run("alpha").matched);
    TEST_ASSERT_TRUE(run("alpha on").matched);
    reset();
    TEST_ASSERT_TRUE(run("alpha on\n").matched);
    reset();
    TEST_ASSERT_TRUE(run("ALPHA ON").matched);      // case-insensitive, as before
}

// A trailing argument still terminates the token: the ladder's `--setlog `
// rung must keep getting `--setlog DK5EN-90` while `--setlog on` hits the row.
static void test_a_trailing_argument_does_not_match_a_toggle_row()
{
    reset();
    TEST_ASSERT_FALSE(run("alpha something").matched);
    TEST_ASSERT_TRUE(run("alpha on extra").matched);  // "on" token, then args
}

// ---------------------------------------------------------------------------
// EXT-02, second half (docs/BACKLOG.md 3.8as): the "--extudp off" row in the
// REAL COMMAND_TOGGLES table (src/command_functions.cpp) used to carry
// nullptr in its post column, so hasExternIPaddress and the UdpExtern socket
// were never released -- "--extudp off" then "on" looked exactly like a
// reset and did nothing.
//
// command_functions.cpp cannot be linked into this native test (it pulls in
// Arduino/BLE/radio/display headers throughout its ~5700 lines -- see the
// file header comment above for why the synthetic TBL[] exists at all), so
// the production row is checked as text instead: the same kind of source
// scan this project already relies on when a binary can't be built for the
// check. This still fails exactly when the fix regresses -- either the row's
// post column reverts to nullptr/a different function, or that function
// stops calling resetExternUDP().
// ---------------------------------------------------------------------------

static bool path_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

// Repo root, derived from __FILE__ rather than the working directory: a
// PlatformIO native test binary's CWD is not guaranteed (same reasoning as
// test/test_udp_send_twin/test_udp_send_twin.cpp's repo_root()).
static std::string repo_root()
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_command_toggles/test_command_toggles.cpp";

    if (!file.empty() && file[0] == '/')
    {
        TEST_ASSERT_TRUE_MESSAGE(
            file.size() > suffix.size() &&
                file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0,
            ("unexpected absolute __FILE__ shape: " + file).c_str());
        std::string root = file.substr(0, file.size() - suffix.size());
        while (!root.empty() && root.back() == '/')
            root.pop_back();
        TEST_ASSERT_TRUE_MESSAGE(path_exists(root + "/platformio.ini"),
                                 ("derived repo root does not look like one "
                                  "(no platformio.ini): " + root).c_str());
        return root;
    }

    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);

    for (int hops = 0; hops < 16; hops++)
    {
        if (path_exists(dir + "/" + file) && path_exists(dir + "/platformio.ini"))
            return dir;
        if (dir.empty() || dir == "/")
            break;
        size_t pos = dir.find_last_of('/');
        dir = (pos == std::string::npos || pos == 0) ? "/" : dir.substr(0, pos);
    }

    TEST_FAIL_MESSAGE(("could not derive repo root: walked up from the runtime "
                       "cwd looking for '" + file + "' next to platformio.ini "
                       "and found neither").c_str());
    return "";
}

static std::string read_whole_file(const std::string &path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), ("could not open " + path).c_str());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Returns field `index` (0-based) of a "{ f0, f1, ..., fN }" table row line,
// trimmed of surrounding whitespace. A comma never occurs inside the row's
// one quoted string ("--extudp off"), so a naive split on ',' is safe.
static std::string nth_field(const std::string &line, int index)
{
    size_t brace = line.find('{');
    size_t close = line.rfind('}');
    TEST_ASSERT_TRUE_MESSAGE(brace != std::string::npos && close != std::string::npos && close > brace,
                              ("row line has no '{ ... }': " + line).c_str());
    std::string body = line.substr(brace + 1, close - brace - 1);

    std::istringstream ss(body);
    std::string field;
    int i = 0;
    while (std::getline(ss, field, ','))
    {
        if (i == index)
        {
            size_t start = field.find_first_not_of(" \t");
            size_t end = field.find_last_not_of(" \t");
            if (start == std::string::npos)
                return std::string();
            return field.substr(start, end - start + 1);
        }
        ++i;
    }
    return std::string();
}

static void test_real_extudp_off_row_has_a_non_null_post_action()
{
    std::string src = read_whole_file(repo_root() + "/src/command_functions.cpp");

    std::istringstream lines(src);
    std::string line;
    std::string row_line;
    while (std::getline(lines, line))
    {
        if (line.find("\"--extudp off\"") != std::string::npos)
        {
            row_line = line;
            break;
        }
    }
    TEST_ASSERT_FALSE_MESSAGE(row_line.empty(),
        "no '--extudp off' row found in src/command_functions.cpp -- has COMMAND_TOGGLES moved?");

    // Columns: name, flag, sset, and_mask, or_mask, post, dirty, opt (post = index 5).
    std::string post_field = nth_field(row_line, 5);

    TEST_ASSERT_FALSE_MESSAGE(post_field.empty(), "could not parse the row's post column");
    TEST_ASSERT_TRUE_MESSAGE(post_field != "nullptr",
        "EXT-02 regression: '--extudp off' row's post-action is nullptr again -- "
        "hasExternIPaddress/UdpExtern are never released, so '--extudp off' then "
        "'--extudp on' will not reopen the socket");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("tg_post_extudp_off", post_field.c_str(),
        "'--extudp off' row's post-action changed -- update the companion test "
        "below (that the function resets the extern socket) to match");
}

static void test_real_extudp_off_post_action_resets_the_extern_socket()
{
    std::string src = read_whole_file(repo_root() + "/src/command_functions.cpp");

    size_t decl = src.find("tg_post_extudp_off()");
    TEST_ASSERT_TRUE_MESSAGE(decl != std::string::npos,
        "tg_post_extudp_off() not found in src/command_functions.cpp");

    size_t open_brace = src.find('{', decl);
    TEST_ASSERT_TRUE_MESSAGE(open_brace != std::string::npos, "tg_post_extudp_off() has no body");

    // A one-line body ({ resetExternUDP(); }): no nested braces to worry about.
    size_t close_brace = src.find('}', open_brace);
    TEST_ASSERT_TRUE_MESSAGE(close_brace != std::string::npos, "tg_post_extudp_off() body never closes");

    std::string body = src.substr(open_brace, close_brace - open_brace);

    TEST_ASSERT_TRUE_MESSAGE(body.find("resetExternUDP()") != std::string::npos,
        "tg_post_extudp_off() no longer calls resetExternUDP() -- that is the "
        "routine (src/extudp_functions.cpp) that stops UdpExtern and clears "
        "hasExternIPaddress without reopening it while bEXTUDP is false");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unknown_command_is_not_matched_and_changes_nothing);
    RUN_TEST(test_on_row_sets_flag_and_ors_its_bit);
    RUN_TEST(test_off_row_clears_flag_and_its_bit);
    RUN_TEST(test_tilde_mask_leaves_the_upper_bits_alone);
    RUN_TEST(test_literal_mask_clears_the_upper_bits_like_the_rung_did);
    RUN_TEST(test_a_row_can_set_its_flag_while_clearing_its_bit);
    RUN_TEST(test_post_is_handed_back_to_run_after_the_save);
    RUN_TEST(test_post_first_runs_before_the_mask);
    RUN_TEST(test_breturn_distinguishes_fallthrough_from_bare_return);
    RUN_TEST(test_rows_without_flag_or_sset_are_safe);
    RUN_TEST(test_echo_style_is_reported_per_row);
    RUN_TEST(test_matching_is_exact_token_not_prefix);
    RUN_TEST(test_a_trailing_argument_does_not_match_a_toggle_row);
    RUN_TEST(test_real_extudp_off_row_has_a_non_null_post_action);
    RUN_TEST(test_real_extudp_off_post_action_resets_the_extern_socket);
    return UNITY_END();
}
