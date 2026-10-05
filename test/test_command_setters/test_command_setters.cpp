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

// ---- --rm (RM-06, issue icssw-org/MeshCom-Firmware#1189) -------------------
// Same constraint as --stor: the rung lives in command_functions.cpp, which no
// native env compiles. The first test runs the real matcher on the names
// involved; the second pins rung, order, schema row and default in the sources.

// "rm" is an exact token: it must not swallow any other command, and no other
// rung may swallow "rm" / "rm on".
static void test_rm_does_not_collide_with_other_commands()
{
    TEST_ASSERT_TRUE(commandMatches("rm on", "rm "));
    TEST_ASSERT_TRUE(commandMatches("rm off", "rm "));
    TEST_ASSERT_TRUE(commandMatches("rm", "rm"));
    // A space terminates the exact token, so the bare rung DOES match "rm on":
    // the argument rung must stay above it (pinned against the source below).
    TEST_ASSERT_TRUE(commandMatches("rm on", "rm"));
    TEST_ASSERT_FALSE(commandMatches("rm", "rm "));              // bare form is a different rung

    const char *others[] = {"reboot", "rotate 1", "relay on", "reflush", "regex", "regex x",
                            "rmonitor", "rmon", "rmi", "rm1", "route", "mesh on", "store", "stor on"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "rm "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "rm"), line);
    }

    const char *rungs[] = {"reboot", "rotate ", "relay on", "relay off", "reflush", "regex",
                           "store", "stor ", "stor", "mesh on", "mesh off"};
    for (const char *rung : rungs)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("rm on", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("rm", rung), rung);
    }
}

static void test_rm_rung_schema_row_and_default_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"rm \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos, "no --rm rung in command_functions.cpp");
    const size_t bare = cmd.find("commandCheck(msg_text+2, (char*)\"rm\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bare != std::string::npos, "bare --rm (show) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(rung < bare, "bare --rm rung is above the argument rung and would shadow --rm on/off");

    // all boards: the rung is NOT inside the ENABLE_MSGSTORE block
    const size_t guardOpen = cmd.find("#if defined(ENABLE_MSGSTORE)\n    if(commandCheck(msg_text+2, (char*)\"storecall \") == 0)");
    const size_t guardClose = cmd.find("#endif // ENABLE_MSGSTORE", guardOpen);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardClose != std::string::npos, "store-node ladder block not found");
    TEST_ASSERT_TRUE_MESSAGE(rung > guardClose, "--rm rung sits inside the ENABLE_MSGSTORE block (must work on all boards)");

    const std::string body = cmd.substr(rung, 1400);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+5") != std::string::npos, "--rm argument offset is not +5");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rm = 1") != std::string::npos, "--rm on does not set node_rm");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rm = 0") != std::string::npos, "--rm off does not clear node_rm");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") != std::string::npos, "--rm rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[RM];%s") != std::string::npos, "--rm rung does not print [RM];on|off");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[RM];warn;no passwd, RM stays inactive") != std::string::npos,
                             "--rm on with an empty passwd does not warn");
    TEST_ASSERT_TRUE_MESSAGE(body.find("node_passwd[0] == 0x00") != std::string::npos, "--rm passwd-empty check missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("--rm on/off             remote management via LoRa (RM1, needs --passwd)") != std::string::npos,
                             "--rm help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("\"...RM: %s ok=%lu rej=%lu\\n\"") != std::string::npos, "--info RM line missing");

    const std::string cfg = read_repo_file("src/config_json.h");
    const size_t row = cfg.find("X(\"node_rm\"");
    TEST_ASSERT_TRUE_MESSAGE(row != std::string::npos, "no node_rm schema row");
    const std::string rowtxt = cfg.substr(row, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("CFG_INT") != std::string::npos, "node_rm is not CFG_INT");
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("0.0, 1.0") != std::string::npos, "node_rm schema range is not 0..1");

    const std::string set = read_repo_file("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_rm, 0)") != std::string::npos,
                             "node_rm default is not 0 (RM-06: off until the operator enables it)");
}

// ---- --autoupdate / --updchan (AU-03, issue icssw-org/MeshCom-Firmware#1187) --
// ESP32 only: both rungs sit inside an `#if defined(ESP32)` block placed after
// the --rm rung and OUTSIDE the INSTRUMENT_ENABLED block (a field command).

static void test_autoupdate_updchan_do_not_collide_with_other_commands()
{
    TEST_ASSERT_TRUE(commandMatches("autoupdate notify", "autoupdate "));
    TEST_ASSERT_TRUE(commandMatches("autoupdate", "autoupdate"));
    // a space ends the exact token, so the bare rung also matches "autoupdate off":
    // the argument rung must stay above it (pinned against the source below)
    TEST_ASSERT_TRUE(commandMatches("autoupdate off", "autoupdate"));
    TEST_ASSERT_FALSE(commandMatches("autoupdate", "autoupdate "));
    TEST_ASSERT_TRUE(commandMatches("updchan dev", "updchan "));
    TEST_ASSERT_TRUE(commandMatches("updchan", "updchan"));
    TEST_ASSERT_TRUE(commandMatches("updchan dev", "updchan"));
    TEST_ASSERT_FALSE(commandMatches("updchan", "updchan "));

    const char *others[] = {"update", "update check", "updates", "auto", "audiodbg 1", "ota-update", "upd",
                            "utcoff", "updchannel", "autoupdates", "autoupdate2", "rm on"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "autoupdate "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "autoupdate"), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "updchan "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "updchan"), line);
    }

    // none of the existing rungs with a similar name may swallow the new commands
    const char *rungs[] = {"audiodbg ", "ota-update", "utcoff", "rm ", "rm", "reboot", "update", "updrepo ", "upd"};
    for (const char *rung : rungs)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("autoupdate notify", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("autoupdate", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("updchan dev", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("updchan", rung), rung);
    }
}

static void test_autoupdate_updchan_rungs_schema_rows_and_defaults_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    const size_t rungAu = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rungAu != std::string::npos, "no --autoupdate rung in command_functions.cpp");
    const size_t bareAu = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bareAu != std::string::npos, "bare --autoupdate (show) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(rungAu < bareAu, "bare --autoupdate rung is above the argument rung and would shadow --autoupdate <mode>");
    const size_t rungCh = cmd.find("commandCheck(msg_text+2, (char*)\"updchan \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rungCh != std::string::npos, "no --updchan rung in command_functions.cpp");
    const size_t bareCh = cmd.find("commandCheck(msg_text+2, (char*)\"updchan\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bareCh != std::string::npos, "bare --updchan (show) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(rungCh < bareCh, "bare --updchan rung is above the argument rung and would shadow --updchan prod/dev");

    // ESP32 only: the rungs sit between an `#if defined(ESP32)` (directly after the --rm rung)
    // and its `#endif`, and NOT inside the INSTRUMENT_ENABLED surface that starts later
    const size_t rmBare = cmd.find("commandCheck(msg_text+2, (char*)\"rm\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rmBare != std::string::npos && rmBare < rungAu, "AU rungs are not placed after the --rm rung");
    const size_t guardOpen = cmd.rfind("#if defined(ESP32)", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardOpen > rmBare,
                             "--autoupdate rung is not inside its own #if defined(ESP32) after --rm");
    const size_t guardClose = cmd.find("#endif", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(guardClose != std::string::npos && guardClose > bareCh,
                             "the ESP32 block closes before the --updchan rungs");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("#if defined(ESP32)", guardOpen + 1) > guardClose,
                             "--autoupdate/--updchan rungs are not in one closed ESP32 block");
    const size_t instr = cmd.find("\n#if INSTRUMENT_ENABLED", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(instr == std::string::npos || instr > guardClose,
                             "--autoupdate rungs sit inside the INSTRUMENT_ENABLED block (must be a field command)");
    // the chain continues after the block with the txpower rung's own else
    const size_t after = cmd.find("else\n    if(commandCheck(msg_text+2, (char*)\"txpower \") == 0)", guardClose);
    TEST_ASSERT_TRUE_MESSAGE(after != std::string::npos && after - guardClose < 40, "ladder chain after the ESP32 block is broken");

    const std::string bodyAu = cmd.substr(rungAu, 1400);
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("msg_text+13") != std::string::npos, "--autoupdate argument offset is not +13");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("meshcom_settings.node_autoupd = 0") != std::string::npos, "--autoupdate off does not set 0");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("meshcom_settings.node_autoupd = 1") != std::string::npos, "--autoupdate notify does not set 1");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("meshcom_settings.node_autoupd = 2") != std::string::npos, "--autoupdate auto does not set 2");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("save_settings()") != std::string::npos, "--autoupdate rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("[AU];mode;%s") != std::string::npos, "--autoupdate rung does not print [AU];mode;<m>");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("\"notify\"") != std::string::npos && bodyAu.find("\"auto\"") != std::string::npos,
                             "--autoupdate mode names missing");
    TEST_ASSERT_TRUE_MESSAGE(bodyAu.find("[ERR];autoupdate;") != std::string::npos, "--autoupdate has no error line");

    const std::string bodyCh = cmd.substr(rungCh, 900);
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("msg_text+10") != std::string::npos, "--updchan argument offset is not +10");
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("meshcom_settings.node_updchan = 0") != std::string::npos, "--updchan prod does not set 0");
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("meshcom_settings.node_updchan = 1") != std::string::npos, "--updchan dev does not set 1");
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("save_settings()") != std::string::npos, "--updchan rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("[AU];chan;%s") != std::string::npos, "--updchan rung does not print [AU];chan;prod|dev");
    TEST_ASSERT_TRUE_MESSAGE(bodyCh.find("[ERR];updchan;") != std::string::npos, "--updchan has no error line");

    // help lines (ESP32 guard) and the --info line (ESP32 guard)
    const size_t help = cmd.find("--autoupdate off/notify/auto  firmware auto update (ESP32)");
    TEST_ASSERT_TRUE_MESSAGE(help != std::string::npos, "--autoupdate help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("--updchan prod/dev       update source: prod icssw-org, dev DK5EN") != std::string::npos,
                             "--updchan help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#if defined(ESP32)", help) > cmd.rfind("#endif", help), "--autoupdate help line is not inside an ESP32 guard");
    const size_t info = cmd.find("\"...AU: %s chan=%s\\n\"");
    TEST_ASSERT_TRUE_MESSAGE(info != std::string::npos, "--info AU line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#if defined(ESP32)", info) > cmd.rfind("#endif", info), "--info AU line is not inside an ESP32 guard");

    // RM allowlist is a positive list: neither command may appear in remote_cmd.cpp
    const std::string rm = read_repo_file("src/remote_cmd.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("autoupdate") == std::string::npos, "autoupdate is on the RM allowlist");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("updchan") == std::string::npos, "updchan is on the RM allowlist");

    const std::string cfg = read_repo_file("src/config_json.h");
    const size_t rowAu = cfg.find("X(\"node_autoupd\"");
    TEST_ASSERT_TRUE_MESSAGE(rowAu != std::string::npos, "no node_autoupd schema row");
    const std::string rowAuTxt = cfg.substr(rowAu, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowAuTxt.find("CFG_INT") != std::string::npos, "node_autoupd is not CFG_INT");
    TEST_ASSERT_TRUE_MESSAGE(rowAuTxt.find("0.0, 2.0") != std::string::npos, "node_autoupd schema range is not 0..2");
    const size_t rowCh = cfg.find("X(\"node_updchan\"");
    TEST_ASSERT_TRUE_MESSAGE(rowCh != std::string::npos, "no node_updchan schema row");
    const std::string rowChTxt = cfg.substr(rowCh, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowChTxt.find("CFG_INT") != std::string::npos, "node_updchan is not CFG_INT");
    TEST_ASSERT_TRUE_MESSAGE(rowChTxt.find("0.0, 1.0") != std::string::npos, "node_updchan schema range is not 0..1");
    TEST_ASSERT_TRUE_MESSAGE(cfg.find("X(\"node_updrepo\"") == std::string::npos, "node_updrepo must not exist (AU-D2)");

    const std::string set = read_repo_file("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_autoupd, 0)") != std::string::npos,
                             "node_autoupd default is not 0 (AU-D1: off)");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_updchan, 0)") != std::string::npos,
                             "node_updchan default is not 0 (AU-D2: prod)");
}

// ---- --update check/install/status (AU-05, issue icssw-org/MeshCom-Firmware#1187) --
// Same ESP32 block as --autoupdate / --updchan, after the --updchan rungs.

static void test_update_does_not_collide_with_other_commands()
{
    TEST_ASSERT_TRUE(commandMatches("update check", "update "));
    TEST_ASSERT_TRUE(commandMatches("update install", "update "));
    TEST_ASSERT_TRUE(commandMatches("update status", "update "));
    TEST_ASSERT_TRUE(commandMatches("update", "update"));
    // a space ends the exact token, so the bare rung also matches "update status":
    // the argument rung must stay above it (pinned against the source below)
    TEST_ASSERT_TRUE(commandMatches("update status", "update"));
    TEST_ASSERT_FALSE(commandMatches("update", "update "));

    // exact-token: neither the neighbouring AU commands nor look-alikes reach the update rungs
    const char *others[] = {"updchan", "updchan dev", "autoupdate", "autoupdate auto", "updates", "updatex",
                            "ota-update", "upd", "updchannel", "setname x", "rm on", "reboot"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "update "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "update"), line);
    }

    // and the update rungs do not swallow the earlier AU rungs
    const char *rungs[] = {"autoupdate ", "autoupdate", "updchan ", "updchan"};
    for (const char *rung : rungs)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("update check", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("update", rung), rung);
    }
}

static void test_update_rungs_sit_in_the_esp32_block_with_the_expected_output()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    const size_t bareCh = cmd.find("commandCheck(msg_text+2, (char*)\"updchan\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bareCh != std::string::npos, "bare --updchan rung missing");
    const size_t rungUp = cmd.find("commandCheck(msg_text+2, (char*)\"update \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rungUp != std::string::npos, "no --update rung in command_functions.cpp");
    const size_t bareUp = cmd.find("commandCheck(msg_text+2, (char*)\"update\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bareUp != std::string::npos, "bare --update (usage) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(rungUp < bareUp, "bare --update rung is above the argument rung and would shadow --update <verb>");
    TEST_ASSERT_TRUE_MESSAGE(rungUp > bareCh, "--update rung is not behind the --updchan rungs (same ESP32 block)");

    // same closed ESP32 block as --autoupdate: opens after --rm, closes behind the bare --update rung,
    // no nested preprocessor line in between, and the chain continues with the txpower rung
    const size_t rungAu = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate \") == 0");
    TEST_ASSERT_TRUE(rungAu != std::string::npos);
    const size_t guardOpen = cmd.rfind("#if defined(ESP32)", rungAu);
    const size_t guardClose = cmd.find("#endif", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardClose != std::string::npos && guardClose > bareUp,
                             "the --update rungs are outside the --autoupdate ESP32 block");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("#if defined(ESP32)", guardOpen + 1) > guardClose,
                             "--update rungs are not in the one closed ESP32 block");
    const size_t instr = cmd.find("\n#if INSTRUMENT_ENABLED", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(instr == std::string::npos || instr > guardClose,
                             "--update rungs sit inside the INSTRUMENT_ENABLED block (must be a field command)");
    const size_t after = cmd.find("else\n    if(commandCheck(msg_text+2, (char*)\"txpower \") == 0)", guardClose);
    TEST_ASSERT_TRUE_MESSAGE(after != std::string::npos && after - guardClose < 40, "ladder chain after the ESP32 block is broken");

    const std::string body = cmd.substr(rungUp, bareUp - rungUp);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+9") != std::string::npos, "--update argument offset is not +9");
    TEST_ASSERT_TRUE_MESSAGE(body.find("\"check\"") != std::string::npos && body.find("\"install\"") != std::string::npos &&
                                 body.find("\"status\"") != std::string::npos,
                             "--update verbs check/install/status missing");
    TEST_ASSERT_TRUE_MESSAGE(body.find("fwNetStart(FWJ_CHECK") != std::string::npos, "--update check does not start a CHECK");
    TEST_ASSERT_TRUE_MESSAGE(body.find("fwNetStart(FWJ_DOWNLOAD") != std::string::npos, "--update install does not start a DOWNLOAD");
    TEST_ASSERT_TRUE_MESSAGE(body.find("fwNetGetStatus(") != std::string::npos, "--update does not read the net status");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[AU];check;started") != std::string::npos, "--update check output line missing");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[AU];install;") != std::string::npos, "--update install output line missing");
    // AU-05 rework: already_staged is decided BEFORE availNewer (staging makes the next CHECK say
    // "not newer"), and a newer release without a .zz asset/digest never starts a job
    const size_t posStaged = body.find("[AU];install;already_staged;");
    const size_t posNoAsset = body.find("[AU];install;no_asset;");
    const size_t posStart = body.find("fwNetStart(FWJ_DOWNLOAD");
    TEST_ASSERT_TRUE_MESSAGE(posStaged != std::string::npos, "--update install: already_staged line missing");
    TEST_ASSERT_TRUE_MESSAGE(posNoAsset != std::string::npos, "--update install: no_asset line missing");
    TEST_ASSERT_TRUE_MESSAGE(posStaged < posNoAsset && posNoAsset < posStart,
                             "--update install order must be already_staged, no_asset, then the DOWNLOAD start");
    {
        const size_t stagedTest = body.find("_au.staged &&");
        const size_t newerTest = body.find("else if(_auNewer");
        TEST_ASSERT_TRUE_MESSAGE(stagedTest != std::string::npos && newerTest != std::string::npos && stagedTest < newerTest,
                                 "--update install tests staged AFTER availNewer (already_staged unreachable)");
        const std::string stagedCond = body.substr(stagedTest, body.find("\n", stagedTest) - stagedTest);   // the condition line only
        TEST_ASSERT_TRUE_MESSAGE(stagedCond.find("_auNewer") == std::string::npos && stagedCond.find("availNewer") == std::string::npos,
                                 "already_staged must not depend on availNewer");
        TEST_ASSERT_TRUE_MESSAGE(body.find("_auNewer && !_au.installable") != std::string::npos,
                                 "--update install does not refuse a release that is not installable");
    }
    TEST_ASSERT_TRUE_MESSAGE(body.find("check_first;retry_when_done") != std::string::npos,
                             "--update install without a known newer release does not check first");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[AU];status;mode;%s;chan;%s;avail;%s;newer;%d;staged;%s;busy;%d;err;%s") != std::string::npos,
                             "--update status line format changed");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[ERR];update;") != std::string::npos, "--update has no error line");
    // none of the verbs writes a setting
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") == std::string::npos, "--update must not persist anything");

    // help line and the extra --info line, both inside an ESP32 guard
    const size_t help = cmd.find("--update check/install/status  firmware update now (ESP32)");
    TEST_ASSERT_TRUE_MESSAGE(help != std::string::npos, "--update help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#if defined(ESP32)", help) > cmd.rfind("#endif", help), "--update help line is not inside an ESP32 guard");
    const size_t info = cmd.find("\"...AU avail=%s staged=%s\\n\"");
    TEST_ASSERT_TRUE_MESSAGE(info != std::string::npos, "--info AU avail/staged line missing");
    // the guard opens at the AU: line (12-space indent); the nested #ifndef/#endif pairs for
    // MC_ENV_NAME / MC_BUILD_TAG sit at the same indent, so look for the guard's own closer
    // (an #endif whose #if is the ESP32 one) by checking that no #endif follows the last
    // "#if defined(ESP32)" before the next "#endif" that is not paired with an #ifndef
    const size_t infoGuard = cmd.rfind("#if defined(ESP32)", info);
    TEST_ASSERT_TRUE_MESSAGE(infoGuard != std::string::npos, "--info AU avail/staged line has no ESP32 guard");
    const std::string between = cmd.substr(infoGuard, info - infoGuard);
    size_t opens = 0, closes = 0;
    for (size_t at = 0; (at = between.find("#if", at)) != std::string::npos; at += 3) opens++;
    for (size_t at = 0; (at = between.find("#endif", at)) != std::string::npos; at += 6) closes++;
    TEST_ASSERT_TRUE_MESSAGE(closes < opens, "--info AU avail/staged line is outside its ESP32 guard");
    // the pinned --info literals of AU-03 stay as they are
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("\"...AU: %s chan=%s\\n\"") != std::string::npos, "--info AU mode line literal changed");

    // the ESP32-only header is included inside an ESP32 guard
    const size_t inc = cmd.find("#include \"esp32/fw_update_net.h\"");
    TEST_ASSERT_TRUE_MESSAGE(inc != std::string::npos, "esp32/fw_update_net.h is not included");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#ifdef ESP32", inc) > cmd.rfind("#endif", inc), "esp32/fw_update_net.h include is not inside an ESP32 guard");

    // not on the RM allowlist (positive list): no rung may name "update" there
    const std::string rm = read_repo_file("src/remote_cmd.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("\"update\"") == std::string::npos, "update is on the RM allowlist");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("fwNet") == std::string::npos, "the RM path reaches the update worker");
}

// AU-05 wiring in esp32_main.cpp: the WiFi gate and the once-per-second tick.
static void test_au_tick_and_wifi_gate_are_wired_in_esp32_main()
{
    const std::string m = read_repo_file("src/esp32/esp32_main.cpp");

    const size_t gate = m.find("if(bGATEWAY || bEXTUDP || bWEBSERVER || bNETCONSOLE || meshcom_settings.node_autoupd > 0)");
    TEST_ASSERT_TRUE_MESSAGE(gate != std::string::npos, "WiFi STA gate does not include node_autoupd > 0");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwTimerInit(s_auTimer") != std::string::npos, "fwTimerInit is not called");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwTick(s_auTimer") != std::string::npos, "fwTick is not called");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwNetStart(FWJ_CHECK") != std::string::npos, "the tick never starts a CHECK");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwNetStart(FWJ_DOWNLOAD") != std::string::npos, "the tick never starts a DOWNLOAD");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwCheckDone(s_auTimer") != std::string::npos, "fwCheckDone is not called");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwAttemptAllowed(s_auTimer") != std::string::npos, "fwAttemptAllowed is not called");
    TEST_ASSERT_TRUE_MESSAGE(m.find("fwAttemptFailed(s_auTimer") != std::string::npos, "fwAttemptFailed is not called");
    // the automatic DOWNLOAD needs an installable release (matching .bin.zz asset + digest), so a
    // prod release without the asset does not burn the 3 attempts; mode 1 only notifies
    {
        const size_t dl = m.find("in.mode == 2 && st.availNewer && st.installable");
        TEST_ASSERT_TRUE_MESSAGE(dl != std::string::npos, "the automatic DOWNLOAD is not gated on st.installable");
        const size_t start = m.find("fwNetStart(FWJ_DOWNLOAD", dl);
        const size_t attempt = m.find("fwAttemptAllowed(s_auTimer", dl);
        TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos && attempt != std::string::npos && attempt < start,
                                 "fwAttemptAllowed must be evaluated inside the installable gate");
    }
    TEST_ASSERT_TRUE_MESSAGE(m.find("!in.onBattery || in.battMv >= FW_BATT_FLOOR_MV") != std::string::npos,
                             "AU-D17: the download is not gated on the battery floor");
    TEST_ASSERT_TRUE_MESSAGE(m.find("[AU];handover;deferred_w3") != std::string::npos, "FW_HANDOVER marker missing");
    TEST_ASSERT_TRUE_MESSAGE(m.find("[AU];notify;%s") != std::string::npos, "notify marker missing");
    // the Safeboot apply is the next wave: the tick must not reboot
    const size_t tick = m.find("static void auTick(void)");
    const size_t loop = m.find("void esp32loop()");
    TEST_ASSERT_TRUE_MESSAGE(tick != std::string::npos && loop != std::string::npos && tick < loop, "auTick is not defined before esp32loop");
    const std::string body = m.substr(tick, loop - tick);
    TEST_ASSERT_TRUE_MESSAGE(body.find("esp_restart") == std::string::npos && body.find("ESP.restart") == std::string::npos &&
                                 body.find("esp32_reboot") == std::string::npos,
                             "auTick reboots (Safeboot apply is not wired yet)");
    // the call sits inside esp32loop()
    const size_t call = m.find("    auTick();\n", loop);
    TEST_ASSERT_TRUE_MESSAGE(call != std::string::npos, "esp32loop() never calls auTick()");
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
    RUN_TEST(test_rm_does_not_collide_with_other_commands);
    RUN_TEST(test_rm_rung_schema_row_and_default_match_the_assumptions);
    RUN_TEST(test_autoupdate_updchan_do_not_collide_with_other_commands);
    RUN_TEST(test_autoupdate_updchan_rungs_schema_rows_and_defaults_match_the_assumptions);
    RUN_TEST(test_update_does_not_collide_with_other_commands);
    RUN_TEST(test_update_rungs_sit_in_the_esp32_block_with_the_expected_output);
    RUN_TEST(test_au_tick_and_wifi_gate_are_wired_in_esp32_main);
    return UNITY_END();
}
