/**
 * The numeric-argument helpers behind the D2-07 setters.
 *
 * The cases below are the ones the ladder actually gets wrong or relies on:
 * a non-numeric argument (which used to read an uninitialised temporary),
 * an argument that starts at the separator rather than past it, a value that
 * fails the range check (the destination must not be touched), and the
 * "no range" form that rungs without bounds use.
 */
#include <unity.h>
#include <string.h>

#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>

#include "command_setters.h"
#include "command_match.h"

static void test_a_plain_number_parses()
{
    TEST_ASSERT_EQUAL_INT(42, (int)cmdArgLong("42"));
    TEST_ASSERT_EQUAL_INT(-9, (int)cmdArgLong("-9"));
    TEST_ASSERT_EQUAL_DOUBLE(0.025, cmdArgDouble("0.025"));
}

// The bug this header exists for: `sscanf` left its target untouched, and the
// int temporary was uninitialised, so `--txpower abc` reported 16711680.
static void test_a_non_numeric_argument_is_zero_not_leftover()
{
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong("abc"));
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong(""));
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong(nullptr));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cmdArgDouble("abc"));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cmdArgDouble(nullptr));
}

// Several rungs aim at the separating space rather than past it; %s and %d both
// skip leading whitespace, so that has always worked and must keep working.
static void test_leading_space_is_skipped()
{
    TEST_ASSERT_EQUAL_INT(7, (int)cmdArgLong(" 7"));
    TEST_ASSERT_EQUAL_DOUBLE(1.5, cmdArgDouble("  1.5"));
}

static void test_trailing_junk_still_yields_the_leading_number()
{
    TEST_ASSERT_EQUAL_INT(12, (int)cmdArgLong("12abc"));   // as sscanf("%d") did
    TEST_ASSERT_EQUAL_INT(3, (int)cmdArgLong("3 4"));
}

// "%i" is auto-base and "%d" is not: two setters use "%i", where a leading zero
// means octal. Converting them to a decimal parse would silently change values.
static void test_percent_i_keeps_its_auto_base()
{
    TEST_ASSERT_EQUAL_INT(8, (int)cmdArgLongBase("010", 0));    // as "%i" reads it
    TEST_ASSERT_EQUAL_INT(16, (int)cmdArgLongBase("0x10", 0));
    TEST_ASSERT_EQUAL_INT(10, (int)cmdArgLong("010"));          // as "%d" reads it
}

// The form the rungs use: parse into their own temporary, fold the false
// return into their own reject path. `*out` is still set so the existing error
// wording ("txpower %i dBm not between ...") prints a deterministic number.
static void test_cmd_arg_wrappers_report_failure_and_still_set_out()
{
    int i = 99;
    TEST_ASSERT_TRUE(cmdArgInt("14", &i));
    TEST_ASSERT_EQUAL_INT(14, i);

    TEST_ASSERT_FALSE(cmdArgInt("abc", &i));
    TEST_ASSERT_EQUAL_INT(0, i);          // deterministic, and the caller rejects

    float f = 9.0f;
    TEST_ASSERT_FALSE(cmdArgFloat("abc", &f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f);
    TEST_ASSERT_TRUE(cmdArgFloat("1.25", &f));
    TEST_ASSERT_EQUAL_FLOAT(1.25f, f);

    double d = 9.0;
    TEST_ASSERT_FALSE(cmdArgDbl("", &d));
    TEST_ASSERT_TRUE(cmdArgDbl("-3.5", &d));
    TEST_ASSERT_EQUAL_DOUBLE(-3.5, d);
}

static void test_range_is_inclusive()
{
    TEST_ASSERT_TRUE(cmdInRange(1, 1, 6));
    TEST_ASSERT_TRUE(cmdInRange(6, 1, 6));
    TEST_ASSERT_FALSE(cmdInRange(0, 1, 6));
    TEST_ASSERT_FALSE(cmdInRange(7, 1, 6));
}

static void test_lo_above_hi_means_no_range_check()
{
    TEST_ASSERT_TRUE(cmdInRange(-99999, 1, 0));
    TEST_ASSERT_TRUE(cmdInRange(99999, 1, 0));
}

static void test_an_out_of_range_value_does_not_reach_the_destination()
{
    int dest = 4;
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, cmdStoreInt("99", &dest, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(4, dest);      // untouched
    TEST_ASSERT_EQUAL_INT(99, seen);     // but reportable
}

static void test_an_in_range_value_is_stored()
{
    int dest = 4;
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("5", &dest, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(5, dest);
    TEST_ASSERT_EQUAL_INT(5, seen);
}

// `--txpower abc` end to end. The destination must survive untouched.
static void test_junk_is_rejected_not_stored()
{
    int dest = 14;
    int seen = -1;
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, cmdStoreInt("abc", &dest, -9, 22, &seen));
    TEST_ASSERT_EQUAL_INT(14, dest);
    TEST_ASSERT_EQUAL_INT(0, seen);
}

// The trap that made rejection the right choice rather than coercion: 0 sits
// INSIDE txpower's -9..22 range, so a helper that turned junk into 0 would
// quietly store 0 dBm. NAN must outrank the range test.
static void test_junk_is_rejected_even_when_zero_would_be_in_range()
{
    int dest = 5;
    int seen = -1;
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, cmdStoreInt("abc", &dest, 0, 10, &seen));
    TEST_ASSERT_EQUAL_INT(5, dest);
}

static void test_float_and_double_stores()
{
    float f = 1.0f;
    float fseen = 0.0f;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreFloat("0.5", &f, 0.1, 2.0, &fseen));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, f);

    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, cmdStoreFloat("9.9", &f, 0.1, 2.0, &fseen));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, f);            // untouched
    TEST_ASSERT_EQUAL_FLOAT(9.9f, fseen);

    double d = 0.0;
    double dseen = 0.0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreDouble("2.5", &d, 1, 0, &dseen)); // no range
    TEST_ASSERT_EQUAL_DOUBLE(2.5, d);
}

static void test_null_destination_is_tolerated()
{
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("3", nullptr, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(3, seen);
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("3", nullptr, 1, 6, nullptr));
}

// ---- --ethmtu (issue icssw-org/MeshCom-Firmware#1183) ----------------------
// command_functions.cpp is compiled by no native env, so the rung cannot run
// here. It is written as exactly this call (cmdStoreInt with 1280..1500 and
// node_ethmtu as destination); the first group pins what that call does, the
// last test pins that the rung, the schema row and the default really are the
// ones assumed here, so the group cannot drift away from the firmware.
static const double ETHMTU_LO = 1280;
static const double ETHMTU_HI = 1500;

static CmdSetResult ethmtu_set(const char *arg, int *dest)
{
    int seen = 0;
    return cmdStoreInt(arg, dest, ETHMTU_LO, ETHMTU_HI, &seen);
}

static void test_ethmtu_accepts_a_value_inside_the_range()
{
    int mtu = 1500;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, ethmtu_set("1400", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
}

static void test_ethmtu_bounds_are_inclusive()
{
    int mtu = 1400;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, ethmtu_set("1280", &mtu));
    TEST_ASSERT_EQUAL_INT(1280, mtu);
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, ethmtu_set("1500", &mtu));
    TEST_ASSERT_EQUAL_INT(1500, mtu);
}

static void test_ethmtu_rejects_out_of_range_and_junk_and_keeps_the_value()
{
    int mtu = 1400;
    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, ethmtu_set("1279", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, ethmtu_set("1501", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, ethmtu_set("0", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, ethmtu_set("abc", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, ethmtu_set("", &mtu));
    TEST_ASSERT_EQUAL_INT(1400, mtu);
}

static bool file_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

// __FILE__ is relative under PlatformIO's native runner, so walk up from the
// working directory until the test file and platformio.ini sit side by side.
static std::string read_repo_file(const char *rel)
{
    const std::string self = "test/test_command_setters/test_command_setters.cpp";
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);

    for (int hops = 0; hops < 16; hops++)
    {
        if (file_exists(dir + "/" + self) && file_exists(dir + "/platformio.ini"))
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }

    const std::string path = dir + "/" + rel;
    std::ifstream f(path.c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), ("could not open " + path).c_str());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_ethmtu_rung_schema_row_and_default_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");
    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"mtu \") == 0 || commandCheck(msg_text+2, (char*)\"ethmtu \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos, "no --mtu/--ethmtu alias rung in command_functions.cpp");
    const std::string body = cmd.substr(rung, 1400);
    // --mtu argument at msg_text+6 ("mtu " after "--"), the --ethmtu alias at msg_text+9
    TEST_ASSERT_TRUE_MESSAGE(body.find("bMtuShort ? msg_text+6 : msg_text+9") != std::string::npos,
                             "--mtu / --ethmtu argument offsets are not +6 / +9");
    TEST_ASSERT_TRUE_MESSAGE(body.find("cmdStoreInt(mtuArg, &meshcom_settings.node_ethmtu, 1280, 1500") != std::string::npos,
                             "--mtu rung does not range-check 1280..1500 through cmdStoreInt");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") != std::string::npos, "--mtu rung does not save");
    // not behind a board guard: the rung is on every board
    // the nearest preprocessor line above the rung must not be an opening #if
    // (comments and the "else" between rungs are allowed in between)
    size_t lineEnd = cmd.rfind("\n", rung);
    std::string ppline;
    while (lineEnd != std::string::npos && lineEnd > 0)
    {
        const size_t lineStart = cmd.rfind("\n", lineEnd - 1);
        const size_t from = (lineStart == std::string::npos) ? 0 : lineStart + 1;
        const std::string line = cmd.substr(from, lineEnd - from);
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '#')
        {
            ppline = line.substr(first);
            break;
        }
        if (lineStart == std::string::npos)
            break;
        lineEnd = lineStart;
    }
    TEST_ASSERT_TRUE_MESSAGE(ppline.find("#if") == std::string::npos,
                             ("--mtu rung sits inside an #if guard: " + ppline).c_str());

    const std::string cfg = read_repo_file("src/config_json.h");
    const size_t row = cfg.find("X(\"node_ethmtu\"");
    TEST_ASSERT_TRUE_MESSAGE(row != std::string::npos, "no node_ethmtu schema row");
    const std::string rowtxt = cfg.substr(row, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("1280.0, 1500.0") != std::string::npos, "schema range is not 1280..1500");

    const std::string set = read_repo_file("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_ethmtu, 1280)") != std::string::npos,
                             "node_ethmtu default is not 1280 (NMTU-D2)");
}

// ---- --stor (SNF-D7, issue icssw-org/MeshCom-Firmware#1188) ---------------
// Same constraint as --ethmtu: the rung lives in command_functions.cpp, which
// no native env compiles. The first test runs the real matcher on the names
// involved; the second pins rung, guard, schema row and default in the sources.

// "stor" must never swallow a --store* line, and no --store* rung may swallow
// "stor". commandMatches() is exact-token (trailing space = argument prefix),
// so this holds whatever the rung order is.
static void test_stor_does_not_collide_with_the_store_family()
{
    TEST_ASSERT_TRUE(commandMatches("stor on", "stor "));
    TEST_ASSERT_TRUE(commandMatches("stor off", "stor "));
    TEST_ASSERT_TRUE(commandMatches("stor", "stor"));
    // A space terminates the exact token, so the bare rung DOES match "stor on":
    // the argument rung must stay above it (pinned against the source below).
    TEST_ASSERT_TRUE(commandMatches("stor on", "stor"));
    TEST_ASSERT_FALSE(commandMatches("stor", "stor "));          // bare form is a different rung

    const char *store_lines[] = {"store", "store own", "store off", "store list", "store heard",
                                 "storecall DK5EN-1", "storecall", "storetime 24", "storetime",
                                 "storeslots 10", "storeslots", "storenotice on", "storenotice"};
    for (const char *line : store_lines)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "stor "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "stor"), line);
    }

    const char *store_rungs[] = {"store", "store off", "store own", "store list", "store heard",
                                 "storecall ", "storecall", "storetime ", "storetime",
                                 "storeslots ", "storeslots", "storenotice ", "storenotice"};
    for (const char *rung : store_rungs)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("stor on", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("stor", rung), rung);
    }
}

static void test_stor_rung_schema_row_and_default_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    // the rung sits inside the store-node block: after its "#if defined(ENABLE_MSGSTORE)"
    // opener and before the matching "#endif // ENABLE_MSGSTORE"
    const size_t guardOpen = cmd.find("#if defined(ENABLE_MSGSTORE)\n    if(commandCheck(msg_text+2, (char*)\"storecall \") == 0)");
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos, "store-node ladder block not found");
    const size_t guardClose = cmd.find("#endif // ENABLE_MSGSTORE", guardOpen);
    TEST_ASSERT_TRUE_MESSAGE(guardClose != std::string::npos, "store-node ladder block is not closed");

    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"stor \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos, "no --stor rung in command_functions.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rung > guardOpen && rung < guardClose, "--stor rung is outside the ENABLE_MSGSTORE block");
    const size_t bare = cmd.find("commandCheck(msg_text+2, (char*)\"stor\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bare != std::string::npos && bare > guardOpen && bare < guardClose,
                             "bare --stor (show) rung missing or outside the ENABLE_MSGSTORE block");

    TEST_ASSERT_TRUE_MESSAGE(rung < bare, "bare --stor rung is above the argument rung and would shadow --stor on/off");

    const std::string body = cmd.substr(rung, 900);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+7") != std::string::npos, "--stor argument offset is not +7");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_stor = 1") != std::string::npos, "--stor on does not set node_stor");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_stor = 0") != std::string::npos, "--stor off does not clear node_stor");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") != std::string::npos, "--stor rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[STOR];%s") != std::string::npos, "--stor rung does not print [STOR];on|off");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("--stor on/off           announce mailbox calls to the server (STOR, default off)") != std::string::npos,
                             "--stor help line missing");

    const std::string cfg = read_repo_file("src/config_json.h");
    const size_t row = cfg.find("X(\"node_stor\"");
    TEST_ASSERT_TRUE_MESSAGE(row != std::string::npos, "no node_stor schema row");
    const std::string rowtxt = cfg.substr(row, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("CFG_INT") != std::string::npos, "node_stor is not CFG_INT");
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("0.0, 1.0") != std::string::npos, "node_stor schema range is not 0..1");

    const std::string set = read_repo_file("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_stor, 0)") != std::string::npos,
                             "node_stor default is not 0 (SNF-D7: off until the operator approves)");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_plain_number_parses);
    RUN_TEST(test_a_non_numeric_argument_is_zero_not_leftover);
    RUN_TEST(test_leading_space_is_skipped);
    RUN_TEST(test_trailing_junk_still_yields_the_leading_number);
    RUN_TEST(test_percent_i_keeps_its_auto_base);
    RUN_TEST(test_cmd_arg_wrappers_report_failure_and_still_set_out);
    RUN_TEST(test_range_is_inclusive);
    RUN_TEST(test_lo_above_hi_means_no_range_check);
    RUN_TEST(test_an_out_of_range_value_does_not_reach_the_destination);
    RUN_TEST(test_an_in_range_value_is_stored);
    RUN_TEST(test_junk_is_rejected_not_stored);
    RUN_TEST(test_junk_is_rejected_even_when_zero_would_be_in_range);
    RUN_TEST(test_float_and_double_stores);
    RUN_TEST(test_null_destination_is_tolerated);
    RUN_TEST(test_ethmtu_accepts_a_value_inside_the_range);
    RUN_TEST(test_ethmtu_bounds_are_inclusive);
    RUN_TEST(test_ethmtu_rejects_out_of_range_and_junk_and_keeps_the_value);
    RUN_TEST(test_ethmtu_rung_schema_row_and_default_match_the_assumptions);
    RUN_TEST(test_stor_does_not_collide_with_the_store_family);
    RUN_TEST(test_stor_rung_schema_row_and_default_match_the_assumptions);
    return UNITY_END();
}
