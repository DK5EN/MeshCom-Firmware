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
#include "esp32/fw_update_net.h" // fwLanUrlParse() (AU-08), the pure part outside the ESP32 guard

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

// Command names of the X(name, KIND, SHAPE) list of src/rm_commands.h (the RM allowlist source), as
// ",name1,name2,...,". The list starts at "#define RM_COMMAND_LIST" and ends at the first line without a
// trailing backslash (the format contract in the header comment).
static std::string rm_command_list_names()
{
    std::istringstream in(read_repo_file("src/rm_commands.h"));
    std::string line, out = ",";
    bool inList = false;
    while (std::getline(in, line))
    {
        if (!inList)
        {
            inList = line.rfind("#define RM_COMMAND_LIST(X", 0) == 0;
            continue;
        }
        const size_t x = line.find("X(");
        const size_t comma = line.find(',', x == std::string::npos ? 0 : x);
        if (x != std::string::npos && comma != std::string::npos)
            out += line.substr(x + 2, comma - x - 2) + ",";
        if (line.empty() || line.find_last_not_of(" \t\r") == std::string::npos || line[line.find_last_not_of(" \t\r")] != '\\')
            break;
    }
    return out;
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

// ---- --remotemgmt (RM-06, issue icssw-org/MeshCom-Firmware#1189) -----------
// Same constraint as --stor: the rung lives in command_functions.cpp, which no
// native env compiles. The first test runs the real matcher on the names
// involved; the second pins rung, order, schema row and default in the sources.

// "remotemgmt" is an exact token: it must not swallow any other command, and no
// other rung may swallow "remotemgmt" / "remotemgmt on". The old name "rm" is gone.
static void test_rm_does_not_collide_with_other_commands()
{
    TEST_ASSERT_TRUE(commandMatches("remotemgmt on", "remotemgmt "));
    TEST_ASSERT_TRUE(commandMatches("remotemgmt off", "remotemgmt "));
    TEST_ASSERT_TRUE(commandMatches("remotemgmt", "remotemgmt"));
    // A space terminates the exact token, so the bare rung DOES match "remotemgmt on":
    // the argument rung must stay above it (pinned against the source below).
    TEST_ASSERT_TRUE(commandMatches("remotemgmt on", "remotemgmt"));
    TEST_ASSERT_FALSE(commandMatches("remotemgmt", "remotemgmt "));   // bare form is a different rung

    const char *others[] = {"reboot", "rotate 1", "relay on", "reflush", "regex", "regex x", "rm", "rm on",
                            "remote on", "remotemgm on", "remotemgmts", "route", "mesh on", "store", "stor on"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "remotemgmt "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "remotemgmt"), line);
    }

    const char *rungs[] = {"reboot", "rotate ", "relay on", "relay off", "reflush", "regex",
                           "store", "stor ", "stor", "mesh on", "mesh off"};
    for (const char *rung : rungs)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("remotemgmt on", rung), rung);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches("remotemgmt", rung), rung);
    }
}

static void test_rm_rung_schema_row_and_default_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"remotemgmt \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos, "no --remotemgmt rung in command_functions.cpp");
    const size_t bare = cmd.find("commandCheck(msg_text+2, (char*)\"remotemgmt\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bare != std::string::npos, "bare --remotemgmt (show) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("(char*)\"rm\") == 0") == std::string::npos && cmd.find("(char*)\"rm \") == 0") == std::string::npos,
                             "old --rm rung is still present (renamed to --remotemgmt)");
    TEST_ASSERT_TRUE_MESSAGE(rung < bare, "bare --remotemgmt rung is above the argument rung and would shadow --remotemgmt on/off");

    // all boards: the rung is NOT inside the ENABLE_MSGSTORE block
    const size_t guardOpen = cmd.find("#if defined(ENABLE_MSGSTORE)\n    if(commandCheck(msg_text+2, (char*)\"storecall \") == 0)");
    const size_t guardClose = cmd.find("#endif // ENABLE_MSGSTORE", guardOpen);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardClose != std::string::npos, "store-node ladder block not found");
    TEST_ASSERT_TRUE_MESSAGE(rung > guardClose, "--remotemgmt rung sits inside the ENABLE_MSGSTORE block (must work on all boards)");

    const std::string body = cmd.substr(rung, 1400);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+13") != std::string::npos, "--remotemgmt argument offset is not +13");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rm = 1") != std::string::npos, "--remotemgmt on does not set node_rm");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rm = 0") != std::string::npos, "--remotemgmt off does not clear node_rm");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") != std::string::npos, "--remotemgmt rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[RM];%s") != std::string::npos, "--remotemgmt rung does not print [RM];on|off");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[RM];warn;no passwd, RM stays inactive") != std::string::npos,
                             "--remotemgmt on with an empty passwd does not warn");
    TEST_ASSERT_TRUE_MESSAGE(body.find("node_passwd[0] == 0x00") != std::string::npos, "--remotemgmt passwd-empty check missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("--remotemgmt on/off     remote management via LoRa (RM1, needs --passwd)") != std::string::npos,
                             "--remotemgmt help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("\"...RM: %s strict=%s ok=%lu rej=%lu\\n\"") != std::string::npos,
                             "--info RM line (with strict=) missing");

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

// ---- --rmstrictsecurity (BF-01, docs/review/code-review-v440a-delta-20261006.md section 4) ---------
// Same constraint as --remotemgmt: the rung lives in command_functions.cpp, which no native env
// compiles, so the first test runs the real matcher and the second pins rung, order, schema row,
// default and the "not over RM" rule in the sources.

static void test_rmstrictsecurity_does_not_collide_with_other_commands()
{
    TEST_ASSERT_TRUE(commandMatches("rmstrictsecurity on", "rmstrictsecurity "));
    TEST_ASSERT_TRUE(commandMatches("rmstrictsecurity off", "rmstrictsecurity "));
    TEST_ASSERT_TRUE(commandMatches("rmstrictsecurity", "rmstrictsecurity"));
    // a space ends the exact token: the bare rung also matches "rmstrictsecurity on", so the
    // argument rung must stay above it (pinned against the source below)
    TEST_ASSERT_TRUE(commandMatches("rmstrictsecurity on", "rmstrictsecurity"));
    TEST_ASSERT_FALSE(commandMatches("rmstrictsecurity", "rmstrictsecurity "));

    const char *others[] = {"remotemgmt", "remotemgmt on", "rm", "rm on", "rmstrict on", "rmstrictsecurityx",
                            "rmstrictsecurit on", "reboot", "route", "store"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "rmstrictsecurity "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "rmstrictsecurity"), line);
    }
    // and --remotemgmt is not swallowed by the new rungs
    TEST_ASSERT_FALSE(commandMatches("remotemgmt on", "rmstrictsecurity "));
    TEST_ASSERT_FALSE(commandMatches("remotemgmt", "rmstrictsecurity"));
}

static void test_rmstrictsecurity_rung_schema_row_and_default_match_the_assumptions()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"rmstrictsecurity \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos, "no --rmstrictsecurity rung in command_functions.cpp");
    const size_t bare = cmd.find("commandCheck(msg_text+2, (char*)\"rmstrictsecurity\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(bare != std::string::npos, "bare --rmstrictsecurity (show) rung missing");
    TEST_ASSERT_TRUE_MESSAGE(rung < bare, "bare --rmstrictsecurity rung is above the argument rung and would shadow on/off");

    // next to --remotemgmt, on all boards (not in the ENABLE_MSGSTORE block), before the ESP32-only AU rungs
    const size_t rmBare = cmd.find("commandCheck(msg_text+2, (char*)\"remotemgmt\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rmBare != std::string::npos && rmBare < rung, "--rmstrictsecurity rung is not behind the --remotemgmt rungs");
    const size_t guardOpen = cmd.find("#if defined(ENABLE_MSGSTORE)\n    if(commandCheck(msg_text+2, (char*)\"storecall \") == 0)");
    const size_t guardClose = cmd.find("#endif // ENABLE_MSGSTORE", guardOpen);
    TEST_ASSERT_TRUE_MESSAGE(guardClose != std::string::npos && rung > guardClose,
                             "--rmstrictsecurity rung sits inside the ENABLE_MSGSTORE block (must work on all boards)");
    const size_t esp32Block = cmd.find("#if defined(ESP32)", bare);
    const size_t rungAu = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate \") == 0");
    TEST_ASSERT_TRUE_MESSAGE(esp32Block != std::string::npos && esp32Block < rungAu && bare < esp32Block,
                             "--rmstrictsecurity rungs are not in front of the ESP32-only AU block");

    const std::string body = cmd.substr(rung, 900);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+19") != std::string::npos, "--rmstrictsecurity argument offset is not +19");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rmstrict = 1") != std::string::npos, "--rmstrictsecurity on does not set node_rmstrict");
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_rmstrict = 0") != std::string::npos, "--rmstrictsecurity off does not clear node_rmstrict");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings()") != std::string::npos, "--rmstrictsecurity rung does not save");
    TEST_ASSERT_TRUE_MESSAGE(body.find("[RM];strict=%s") != std::string::npos, "--rmstrictsecurity rung does not print [RM];strict=on|off");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("--rmstrictsecurity on/off") != std::string::npos, "--rmstrictsecurity help line missing");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("an off sender can push an on target into its lockout") != std::string::npos,
                             "--rmstrictsecurity help does not state the D3 consequence");

    const std::string cfg = read_repo_file("src/config_json.h");
    const size_t row = cfg.find("X(\"node_rmstrict\"");
    TEST_ASSERT_TRUE_MESSAGE(row != std::string::npos, "no node_rmstrict schema row");
    const std::string rowtxt = cfg.substr(row, 100);
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("CFG_INT") != std::string::npos, "node_rmstrict is not CFG_INT");
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("0.0, 1.0") != std::string::npos, "node_rmstrict schema range is not 0..1");
    TEST_ASSERT_TRUE_MESSAGE(rowtxt.find("CFG_NOESC") != std::string::npos, "node_rmstrict is not CFG_NOESC");

    const std::string set = read_repo_file("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_rmstrict, 0)") != std::string::npos,
                             "node_rmstrict default is not 0 (off until the operator enables it)");
    // NVS key limit: 15 characters
    TEST_ASSERT_TRUE(strlen("node_rmstrict") <= 15);

    // never a node_sset4 bit: the flag has its own member
    TEST_ASSERT_TRUE_MESSAGE(cmd.substr(rung, 900).find("node_sset4") == std::string::npos,
                             "--rmstrictsecurity must not use a node_sset4 bit");

    // never over RM: the RM allowlist (a positive list) must not name it
    // RM_ALLOWLIST[] in remote_cmd.cpp is generated from the X(name, KIND, SHAPE) list of rm_commands.h
    // (DRY-07), so that list is what gets scanned: all 22 pinned names present, the console-only ones absent.
    const std::string rc = read_repo_file("src/remote_cmd.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rc.find("const RmAllowRow RM_ALLOWLIST[]") != std::string::npos, "RM_ALLOWLIST not found");
    TEST_ASSERT_TRUE_MESSAGE(rc.find("RM_COMMAND_LIST(RM_ALLOW_ROW)") != std::string::npos,
                             "RM_ALLOWLIST is no longer generated from rm_commands.h");
    const std::string names = rm_command_list_names();
    static const char *const pinned[] = {"reboot", "status", "sendpos", "sendtrack", "sync", "gps", "track",
                                         "display", "led", "gateway", "mesh", "txpower", "setout", "radio", "sens",
                                         "txq", "mbox", "maxhop", "name", "atxt", "pos", "mh"};
    TEST_ASSERT_EQUAL_UINT_MESSAGE(22, sizeof(pinned) / sizeof(pinned[0]), "pinned list is not 22 names");
    for (const char *nm : pinned)
        TEST_ASSERT_TRUE_MESSAGE(names.find(std::string(",") + nm + ",") != std::string::npos, nm);
    size_t count = 0;
    for (char ch : names)
        count += (ch == ',') ? 1 : 0;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(22 + 1, count, "rm_commands.h does not list exactly 22 commands");
    for (const char *bad : {"rmstrictsecurity", "remotemgmt", "passwd"})
        TEST_ASSERT_TRUE_MESSAGE(names.find(std::string(",") + bad + ",") == std::string::npos, bad);

    // the web switch is routed through the console command and reads node_rmstrict back
    const std::string web = read_repo_file("src/web_functions/web_setup.cpp");
    TEST_ASSERT_TRUE_MESSAGE(web.find("--rmstrictsecurity %s") != std::string::npos, "web setparam rmstrict does not route through --rmstrictsecurity");
    TEST_ASSERT_TRUE_MESSAGE(web.find("paramName.equals(\"rmstrict\")") != std::string::npos, "web rmstrict param missing");
}

// ---- --autoupdate / --updchan (AU-03, issue icssw-org/MeshCom-Firmware#1187) --
// ESP32 only: both rungs sit inside an `#if defined(ESP32)` block placed after
// the --remotemgmt rung and OUTSIDE the INSTRUMENT_ENABLED block (a field command).

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
                            "utcoff", "updchannel", "autoupdates", "autoupdate2", "remotemgmt on"};
    for (const char *line : others)
    {
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "autoupdate "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "autoupdate"), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "updchan "), line);
        TEST_ASSERT_FALSE_MESSAGE(commandMatches(line, "updchan"), line);
    }

    // none of the existing rungs with a similar name may swallow the new commands
    const char *rungs[] = {"audiodbg ", "ota-update", "utcoff", "remotemgmt ", "remotemgmt", "reboot", "update", "updrepo ", "upd"};
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

    // ESP32 only: the rungs sit between an `#if defined(ESP32)` (directly after the --remotemgmt rung)
    // and its `#endif`, and NOT inside the INSTRUMENT_ENABLED surface that starts later
    const size_t rmBare = cmd.find("commandCheck(msg_text+2, (char*)\"remotemgmt\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rmBare != std::string::npos && rmBare < rungAu, "AU rungs are not placed after the --remotemgmt rung");
    const size_t guardOpen = cmd.rfind("#if defined(ESP32)", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardOpen > rmBare,
                             "--autoupdate rung is not inside its own #if defined(ESP32) after --remotemgmt");
    // the block ends behind the bare --update rung; the --update body carries one nested
    // "#if INSTRUMENT_ENABLED" pair (stagelan, AU-08) that is not the block's closer
    const size_t bareUpdate = cmd.find("commandCheck(msg_text+2, (char*)\"update\") == 0", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(bareUpdate != std::string::npos, "bare --update rung missing (block closer anchor)");
    const size_t guardClose = cmd.find("#endif", bareUpdate);
    TEST_ASSERT_TRUE_MESSAGE(guardClose != std::string::npos && guardClose > bareCh,
                             "the ESP32 block closes before the --updchan rungs");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("#if defined(ESP32)", guardOpen + 1) > guardClose,
                             "--autoupdate/--updchan rungs are not in one closed ESP32 block");
    const size_t instr = cmd.find("\n#if INSTRUMENT_ENABLED", rungAu);
    const size_t rungUpdate = cmd.find("commandCheck(msg_text+2, (char*)\"update \") == 0", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(instr == std::string::npos || instr > guardClose || (rungUpdate != std::string::npos && instr > rungUpdate),
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
    const std::string rm = read_repo_file("src/remote_cmd.cpp") + rm_command_list_names();
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
                            "ota-update", "upd", "updchannel", "setname x", "remotemgmt on", "reboot"};
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

    // same closed ESP32 block as --autoupdate: opens after --remotemgmt, closes behind the bare --update rung,
    // no nested preprocessor line in between, and the chain continues with the txpower rung
    const size_t rungAu = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate \") == 0");
    TEST_ASSERT_TRUE(rungAu != std::string::npos);
    const size_t guardOpen = cmd.rfind("#if defined(ESP32)", rungAu);
    // the closer is the first #endif behind the bare --update rung: the --update body itself carries one
    // nested "#if INSTRUMENT_ENABLED" pair (the bench-only stagelan verb, AU-08), pinned further down
    const size_t guardClose = cmd.find("#endif", bareUp);
    TEST_ASSERT_TRUE_MESSAGE(guardOpen != std::string::npos && guardClose != std::string::npos && guardClose > bareUp,
                             "the --update rungs are outside the --autoupdate ESP32 block");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("#if defined(ESP32)", guardOpen + 1) > guardClose,
                             "--update rungs are not in the one closed ESP32 block");
    // the --update verbs are field commands: the only INSTRUMENT_ENABLED region inside the block is the
    // stagelan verb nested in the --update body, nothing before it (--autoupdate / --updchan stay field)
    const size_t instr = cmd.find("\n#if INSTRUMENT_ENABLED", rungAu);
    TEST_ASSERT_TRUE_MESSAGE(instr == std::string::npos || instr > guardClose || (instr > rungUp && instr < bareUp),
                             "--update rungs sit inside the INSTRUMENT_ENABLED block (must be a field command)");
    if (instr != std::string::npos && instr < guardClose)
        TEST_ASSERT_TRUE_MESSAGE(cmd.find("strncmp(_owner_c, \"stagelan \", 9) == 0", instr) - instr < 400,
                                 "the INSTRUMENT_ENABLED region inside --update is not the stagelan verb");
    const size_t after = cmd.find("else\n    if(commandCheck(msg_text+2, (char*)\"txpower \") == 0)", guardClose);
    TEST_ASSERT_TRUE_MESSAGE(after != std::string::npos && after - guardClose < 40, "ladder chain after the ESP32 block is broken");

    const std::string body = cmd.substr(rungUp, bareUp - rungUp);
    TEST_ASSERT_TRUE_MESSAGE(body.find("msg_text+9") != std::string::npos, "--update argument offset is not +9");
    TEST_ASSERT_TRUE_MESSAGE(body.find("\"check\"") != std::string::npos && body.find("\"install\"") != std::string::npos &&
                                 body.find("\"status\"") != std::string::npos && body.find("\"apply\"") != std::string::npos,
                             "--update verbs check/install/status/apply missing");
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
    const size_t help = cmd.find("--update check/install/status/apply  firmware update now (ESP32)");
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
    TEST_ASSERT_TRUE_MESSAGE(m.find("deferred_w3") == std::string::npos, "the deferred_w3 handover marker is still there");
    TEST_ASSERT_TRUE_MESSAGE(m.find("[AU];notify;%s") != std::string::npos, "notify marker missing");
    // AU-08: FW_HANDOVER prints the marker, then reboots through the ONE shared function (no restart
    // call of its own in the tick)
    const size_t tick = m.find("static void auTick(void)");
    const size_t loop = m.find("void esp32loop()");
    TEST_ASSERT_TRUE_MESSAGE(tick != std::string::npos && loop != std::string::npos && tick < loop, "auTick is not defined before esp32loop");
    const std::string body = m.substr(tick, loop - tick);
    TEST_ASSERT_TRUE_MESSAGE(body.find("esp_restart") == std::string::npos && body.find("ESP.restart") == std::string::npos &&
                                 body.find("esp32_reboot") == std::string::npos,
                             "auTick restarts on its own (it must go through auRebootToSafeboot)");
    {
        const size_t ho = body.find("if(act == FW_HANDOVER)");
        TEST_ASSERT_TRUE_MESSAGE(ho != std::string::npos, "FW_HANDOVER branch missing in auTick");
        const size_t hoEnd = body.find("if(act == FW_CHECK", ho);
        TEST_ASSERT_TRUE_MESSAGE(hoEnd != std::string::npos, "FW_CHECK branch does not follow the FW_HANDOVER branch");
        const std::string h = body.substr(ho, hoEnd - ho);
        const size_t load = h.find("fwNetLoadRecord(rec)");
        const size_t mark = h.find("[AU];handover;%s");
        const size_t reboot = h.find("auRebootToSafeboot()");
        TEST_ASSERT_TRUE_MESSAGE(load != std::string::npos && mark != std::string::npos && reboot != std::string::npos,
                                 "handover must validate the record, print [AU];handover;<tag> and call auRebootToSafeboot()");
        TEST_ASSERT_TRUE_MESSAGE(load < mark && mark < reboot, "handover order must be: load record, marker, reboot");
        TEST_ASSERT_TRUE_MESSAGE(h.find("s_auHandoverTag") != std::string::npos, "handover is not limited to one attempt per tag");
        // R1/R5: a handover that returns (no Safeboot / boot partition not settable) drops the record, and an
        // attempted tag is not re-read from NVS every second
        const size_t fail = h.find("[AU];fail;nosafeboot");
        const size_t clr = h.find("fwNetClearRecord()");
        TEST_ASSERT_TRUE_MESSAGE(reboot < fail && fail < clr, "a failed handover must print nosafeboot and then fwNetClearRecord()");
        const size_t cached = h.find("auTagSame(s_auHandoverTag, st.stagedTag)");
        TEST_ASSERT_TRUE_MESSAGE(cached != std::string::npos && cached < load, "the attempted-tag cache check must come before fwNetLoadRecord()");
    }
    // the call sits inside esp32loop()
    const size_t call = m.find("    auTick();\n", loop);
    TEST_ASSERT_TRUE_MESSAGE(call != std::string::npos, "esp32loop() never calls auTick()");
}

// ---- AU-08 (#1187): shared Safeboot reboot, --update apply, bench-only stagelan --------------

static void test_ota_update_and_the_handover_share_one_reboot_function()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    // the sequence exists exactly once, in auRebootToSafeboot(), inside an ESP32 guard
    const size_t fn = cmd.find("bool auRebootToSafeboot(void)\n{");
    TEST_ASSERT_TRUE_MESSAGE(fn != std::string::npos, "auRebootToSafeboot() is not defined in command_functions.cpp");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#ifdef ESP32", fn) > cmd.rfind("#endif", fn), "auRebootToSafeboot() is not inside an ESP32 guard");
    const size_t fnEnd = cmd.find("\n}\n", fn);
    const std::string body = cmd.substr(fn, fnEnd - fn);
    TEST_ASSERT_TRUE_MESSAGE(body.find("ESP_PARTITION_SUBTYPE_APP_FACTORY, \"safeboot\"") != std::string::npos, "the Safeboot partition lookup is gone");
    TEST_ASSERT_TRUE_MESSAGE(body.find("esp_ota_set_boot_partition(partition)") != std::string::npos, "the boot partition is not set");
    // R1: the boot-partition result is checked and a failure returns BEFORE the breadcrumb clear and the restart
    const size_t setBoot = body.find("if (esp_ota_set_boot_partition(partition) != ESP_OK)");
    TEST_ASSERT_TRUE_MESSAGE(setBoot != std::string::npos, "the esp_ota_set_boot_partition() result is not checked");
    TEST_ASSERT_TRUE_MESSAGE(body.find("return false", setBoot) < body.find("loopCrumbClear()") &&
                                 body.find("return false", setBoot) < body.find("esp_restart()"),
                             "a failed esp_ota_set_boot_partition() must return before loopCrumbClear()/esp_restart()");
    const size_t crumb = body.find("loopCrumbClear()");
    const size_t restart = body.find("esp_restart()");
    TEST_ASSERT_TRUE_MESSAGE(crumb != std::string::npos && restart != std::string::npos && crumb < restart,
                             "loopCrumbClear() must run before esp_restart() (INS-05)");
    TEST_ASSERT_TRUE_MESSAGE(body.find("return false") != std::string::npos, "no failure return without a Safeboot partition");

    // no second copy: the partition lookup / restart appear once in the file, the --ota-update rung calls the function
    size_t n = 0;
    for (size_t at = 0; (at = cmd.find("\"safeboot\"", at)) != std::string::npos; at += 10) n++;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, n, "the Safeboot partition lookup exists more than once");
    const size_t ota = cmd.find("commandCheck(msg_text+2, (char*)\"ota-update\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(ota != std::string::npos, "--ota-update rung missing");
    const std::string rung = cmd.substr(ota, cmd.find("#endif", ota) - ota);
    TEST_ASSERT_TRUE_MESSAGE(rung.find("auRebootToSafeboot()") != std::string::npos, "--ota-update does not call auRebootToSafeboot()");
    TEST_ASSERT_TRUE_MESSAGE(rung.find("esp_restart") == std::string::npos && rung.find("esp_ota_set_boot_partition") == std::string::npos,
                             "--ota-update still carries its own reboot sequence");
    TEST_ASSERT_TRUE_MESSAGE(cmd.rfind("#ifdef ESP32", ota) > cmd.rfind("#endif", ota), "--ota-update is not inside an ESP32 guard");

    // the prototype is in the ESP32-guarded part of the net header, so esp32_main.cpp sees it
    const std::string hdr = read_repo_file("src/esp32/fw_update_net.h");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("bool auRebootToSafeboot(void);") != std::string::npos, "auRebootToSafeboot() prototype missing");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("bool auRebootToSafeboot(void);") > hdr.find("#if defined(ESP32)"), "prototype is outside the ESP32 guard");
}

static void test_update_apply_hands_over_now_and_is_not_remote()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");
    const size_t rungUp = cmd.find("commandCheck(msg_text+2, (char*)\"update \") == 0");
    const size_t bareUp = cmd.find("commandCheck(msg_text+2, (char*)\"update\") == 0");
    TEST_ASSERT_TRUE(rungUp != std::string::npos && bareUp != std::string::npos && rungUp < bareUp);
    const std::string body = cmd.substr(rungUp, bareUp - rungUp);

    const size_t apply = body.find("casecmp(_owner_c, (char*)\"apply\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(apply != std::string::npos, "--update apply verb missing");
    const size_t status = body.find("casecmp(_owner_c, (char*)\"status\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(status != std::string::npos && apply < status, "--update apply is not a branch of the verb chain before status");
    const std::string a = body.substr(apply, status - apply);
    const size_t load = a.find("fwNetLoadRecord(_rec)");
    const size_t none = a.find("[AU];apply;nothing_staged");
    const size_t mark = a.find("[AU];handover;%s");
    const size_t reboot = a.find("auRebootToSafeboot()");
    TEST_ASSERT_TRUE_MESSAGE(load != std::string::npos && none != std::string::npos && mark != std::string::npos && reboot != std::string::npos,
                             "--update apply needs: record check, nothing_staged line, handover marker, reboot");
    TEST_ASSERT_TRUE_MESSAGE(load < none && none < mark && mark < reboot, "--update apply order must be record check, nothing_staged, marker, reboot");
    // operator action: no window / mode / idle gate in front of it
    TEST_ASSERT_TRUE_MESSAGE(a.find("localMinuteOfDay") == std::string::npos && a.find("node_autoupd") == std::string::npos &&
                                 a.find("fwTick") == std::string::npos && a.find("isPhoneReady") == std::string::npos,
                             "--update apply is gated on the update window / mode / phone");
    TEST_ASSERT_TRUE_MESSAGE(a.find("_auBusy") != std::string::npos, "--update apply does not refuse while a job runs");

    // not on the RM allowlist
    const std::string rm = read_repo_file("src/remote_cmd.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("apply") == std::string::npos && rm.find("auRebootToSafeboot") == std::string::npos,
                             "apply / the Safeboot reboot is reachable from the RM path");
}

// Web temp offset (field report 2026-10-10): maxlength="3" cut "-0.5" to "-0." and a decimal comma stopped
// sscanf/strtod, so "-0,5" was stored as 0 and the page reported success. Only "-.5" worked.
static void test_web_temp_offset_takes_comma_and_leading_zero()
{
    char v[16] = "-0,5";
    cmdDecimalComma(v);
    TEST_ASSERT_EQUAL_STRING("-0.5", v);

    float f = 0.0f, seen = 0.0f;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreFloat(v, &f, -50.0, 50.0, &seen));
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, f);

    char w[16] = "-12.5";
    cmdDecimalComma(w);
    TEST_ASSERT_EQUAL_STRING("-12.5", w);
    cmdDecimalComma(nullptr);

    // String(float) renders -50.00: six characters must fit
    const std::string web = read_repo_file("src/web_functions/web_functions.cpp");
    TEST_ASSERT_TRUE_MESSAGE(web.find("\"tempoffsetindoor\", 6,") != std::string::npos, "indoor offset input too short");
    TEST_ASSERT_TRUE_MESSAGE(web.find("\"tempoffsetoutdoor\", 6,") != std::string::npos, "outdoor offset input too short");

    // both setparam handlers normalise before they parse and before they build the console command
    const std::string set = read_repo_file("src/web_functions/web_setup.cpp");
    // the include must not sit inside a board guard: boards without ENABLE_MSGSTORE (T-Beam) failed to compile
    const size_t inc = set.find("#include <command_setters.h>");
    const size_t guard = set.find("#if");
    TEST_ASSERT_TRUE_MESSAGE(inc != std::string::npos && inc < guard, "command_setters.h include missing or inside an #if");
    size_t n = 0;
    for (size_t at = set.find("cmdDecimalComma(value);"); at != std::string::npos; at = set.find("cmdDecimalComma(value);", at + 1))
        ++n;
    TEST_ASSERT_EQUAL_INT(2, (int)n);
}

static void test_stagelan_is_instrument_only_everywhere()
{
    const std::string cmd = read_repo_file("src/command_functions.cpp");
    const std::string net = read_repo_file("src/esp32/fw_update_net.cpp");
    const std::string hdr = read_repo_file("src/esp32/fw_update_net.h");

    // command_functions.cpp: the verb string occurs only between "#if INSTRUMENT_ENABLED" and its "#endif"
    const size_t verb = cmd.find("strncmp(_owner_c, \"stagelan \", 9) == 0");
    TEST_ASSERT_TRUE_MESSAGE(verb != std::string::npos, "stagelan verb missing");
    const size_t open = cmd.rfind("#if INSTRUMENT_ENABLED", verb);
    const size_t close = cmd.find("#endif", verb);
    TEST_ASSERT_TRUE_MESSAGE(open != std::string::npos && close != std::string::npos && verb - open < 400 &&
                                 cmd.rfind("#endif", verb) < open,
                             "the stagelan verb is not directly inside its own #if INSTRUMENT_ENABLED region");
    const std::string region = cmd.substr(open, close - open);
    TEST_ASSERT_TRUE_MESSAGE(region.find("fwNetStartLan(") != std::string::npos, "stagelan does not start the LAN job");
    TEST_ASSERT_TRUE_MESSAGE(region.find("%199s %lu %23s") != std::string::npos, "stagelan argument format is not <url> <ilen> <tag>");
    TEST_ASSERT_TRUE_MESSAGE(region.find("[AU];stagelan;started;") != std::string::npos, "stagelan has no started marker");
    // every other occurrence of the word outside this region is a comment line
    for (size_t at = 0; (at = cmd.find("stagelan", at)) != std::string::npos; at += 8)
    {
        if (at > open && at < close) continue;
        const size_t ls = cmd.rfind('\n', at) + 1;
        const std::string line = cmd.substr(ls, cmd.find('\n', at) - ls);
        TEST_ASSERT_TRUE_MESSAGE(line.find("//") != std::string::npos && line.find("//") < at - ls,
                                 "the word stagelan appears in non-comment code outside the INSTRUMENT_ENABLED region");
    }
    // not on the RM allowlist
    const std::string rm = read_repo_file("src/remote_cmd.cpp");
    TEST_ASSERT_TRUE_MESSAGE(rm.find("stagelan") == std::string::npos, "stagelan is on the RM allowlist");

    // fw_update_net.cpp: no string literal says "stagelan", the LAN code sits in INSTRUMENT_ENABLED regions
    TEST_ASSERT_TRUE_MESSAGE(net.find("\"stagelan") == std::string::npos && net.find("stagelan\"") == std::string::npos,
                             "a stagelan string literal exists in fw_update_net.cpp");
    const char *lanOnly[] = {"const char *runStageLan()", "bool fwNetStartLan(const char *url, uint32_t ilen, const char *tag)\n{",
                             "LanArg s_lan;", "fwLanUrlParse(a.url, u)", "http.begin(client, String(u.host)"};
    for (const char *sym : lanOnly)
    {
        const size_t at = net.find(sym);
        TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, sym);
        const size_t o = net.rfind("#if INSTRUMENT_ENABLED", at);
        const size_t c = net.rfind("#endif", at);
        TEST_ASSERT_TRUE_MESSAGE(o != std::string::npos && (c == std::string::npos || c < o), sym);
    }
    // the task dispatch reaches runStageLan only under the guard, the generic start refuses FWJ_STAGELAN
    const size_t disp = net.find("runStageLan()\n#endif");
    TEST_ASSERT_TRUE_MESSAGE(disp != std::string::npos, "the task dispatch of FWJ_STAGELAN is not inside an #if INSTRUMENT_ENABLED");
    TEST_ASSERT_TRUE_MESSAGE(net.find("if (job != FWJ_CHECK && job != FWJ_DOWNLOAD)\n        return false;") != std::string::npos,
                             "fwNetStart() no longer refuses FWJ_STAGELAN");
    // plain http, no redirects, same stage steps as DOWNLOAD
    const size_t lan = net.find("const char *runStageLan()");
    const std::string lb = net.substr(lan, net.find("\n#endif", lan) - lan);
    TEST_ASSERT_TRUE_MESSAGE(lb.find("WiFiClientSecure") == std::string::npos, "STAGELAN uses TLS (it must be plain http)");
    TEST_ASSERT_TRUE_MESSAGE(lb.find("HTTPC_DISABLE_FOLLOW_REDIRECTS") != std::string::npos, "STAGELAN follows redirects");
    TEST_ASSERT_TRUE_MESSAGE(lb.find("stagePrepare(") != std::string::npos && lb.find("stageFinish(") != std::string::npos &&
                                 lb.find("dlFeed") != std::string::npos,
                             "STAGELAN does not go through the shared stage steps");
    TEST_ASSERT_TRUE_MESSAGE(lb.find("stageFinish(sg, err, a.tag, a.ilen, nullptr)") != std::string::npos,
                             "STAGELAN must record the computed SHA-256 (no digest to compare)");
    const size_t dlPos = net.find("const char *runDownload()");
    const std::string db = net.substr(dlPos, lan - dlPos);
    TEST_ASSERT_TRUE_MESSAGE(db.find("stagePrepare(") != std::string::npos && db.find("stageFinish(sg, err, tag, ilen, av.sha)") != std::string::npos,
                             "DOWNLOAD no longer goes through the shared stage steps with the API digest");
    // the net header declares the job and the start function without a string
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("FWJ_STAGELAN") != std::string::npos && hdr.find("bool fwNetStartLan(") != std::string::npos,
                             "net header lacks FWJ_STAGELAN / fwNetStartLan");
}

static void test_lan_url_accepts_only_plain_http_to_private_ipv4()
{
    FwLanUrl u;
    TEST_ASSERT_TRUE(fwLanUrlParse("http://192.168.68.10:8000/fw/heltec.bin.zz", u));
    TEST_ASSERT_EQUAL_STRING("192.168.68.10", u.host);
    TEST_ASSERT_EQUAL_UINT16(8000, u.port);
    TEST_ASSERT_EQUAL_STRING("/fw/heltec.bin.zz", u.path);
    TEST_ASSERT_TRUE(fwLanUrlParse("http://10.0.0.5/x.zz", u));
    TEST_ASSERT_EQUAL_UINT16(80, u.port);
    TEST_ASSERT_TRUE(fwLanUrlParse("http://172.16.0.1:1/a", u));
    TEST_ASSERT_TRUE(fwLanUrlParse("http://172.31.255.254:65535/a", u));
    TEST_ASSERT_EQUAL_UINT16(65535, u.port);

    const char *bad[] = {
        nullptr,
        "",
        "https://192.168.1.2/a.zz",            // TLS scheme
        "HTTP://192.168.1.2/a.zz",             // case-sensitive scheme, keep it strict
        "ftp://192.168.1.2/a.zz",
        "http://example.com/a.zz",             // hostname
        "http://localhost/a.zz",
        "http://8.8.8.8/a.zz",                 // public
        "http://127.0.0.1/a.zz",               // loopback
        "http://169.254.1.1/a.zz",             // link-local
        "http://172.15.0.1/a.zz",              // just below 172.16/12
        "http://172.32.0.1/a.zz",              // just above
        "http://192.169.0.1/a.zz",
        "http://11.0.0.1/a.zz",
        "http://0.0.0.0/a.zz",
        "http://192.168.1.2",                  // no path
        "http://192.168.1.2:8000",             // no path
        "http://192.168.1.2:/a",               // empty port
        "http://192.168.1.2:0/a",              // port 0
        "http://192.168.1.2:65536/a",          // port too big
        "http://192.168.1.2:123456/a",
        "http://192.168.1.256/a",              // octet overflow
        "http://192.168.1/a",                  // too few octets
        "http://192.168.1.2.3/a",              // too many
        "http://192.168.01.2/a",               // leading zero (octal ambiguity)
        "http://192.168.1.2@10.0.0.1/a",       // userinfo trick
        "http://10.0.0.1@evil/a",
        "http://user:pw@10.0.0.1/a",
        "http://10.0.0.1:80:90/a",
        "http://10.0.0.1/a b",                 // space
        "http://10.0.0.1/a\tb",                // control char
        "http://10.0.0.1/a#frag",
        "http://10.0.0.1/a\\b",
        "http:// 10.0.0.1/a",
        "http://10.0.0.1\n/a",
    };
    for (const char *url : bad)
        TEST_ASSERT_FALSE_MESSAGE(fwLanUrlParse(url, u), url ? url : "(null)");
    // a refused URL leaves an empty host (a caller that ignores the result cannot connect anywhere)
    TEST_ASSERT_FALSE(fwLanUrlParse("http://8.8.8.8/a", u));
    TEST_ASSERT_EQUAL_STRING("", u.host);

    // path capacity: 159 characters fit, 160 do not
    std::string ok = "http://10.0.0.1/";
    ok.append(158, 'a');
    TEST_ASSERT_TRUE(fwLanUrlParse(ok.c_str(), u));
    TEST_ASSERT_EQUAL_UINT(159, (unsigned)strlen(u.path));
    ok.append(1, 'a');
    TEST_ASSERT_FALSE(fwLanUrlParse(ok.c_str(), u));
}

// AU-08 rework: the stage area is written through ONE helper on a private flash chip, and R4 (tries).
static void test_stage_flash_goes_through_one_guarded_helper()
{
    const std::string net = read_repo_file("src/esp32/fw_update_net.cpp");
    const std::string hdr = read_repo_file("src/esp32/fw_update_net.h");

    // no partition-API erase/write/read on ota_0 in code (a running-partition write aborts, IDF 4.4 region_protected)
    for (const char *api : {"esp_partition_write(", "esp_partition_erase_range(", "esp_partition_read("})
    {
        // AU-12: fwNetSafebootVersion() reads the separate "safeboot" factory partition (never ota_0, never
        // the running slot) with esp_partition_read; that one function is exempt from the ota_0 rule.
        const size_t sbBeg = net.find("int fwNetSafebootVersion(void)\n{");
        const size_t sbEnd = net.find("bool fwNetSafebootCapable(void)\n{");
        for (size_t at = 0; (at = net.find(api, at)) != std::string::npos; at += 8)
        {
            if (sbBeg != std::string::npos && sbEnd != std::string::npos && at > sbBeg && at < sbEnd &&
                std::string(api) == "esp_partition_read(")
                continue;
            const size_t ls = net.rfind('\n', at) + 1;
            const std::string line = net.substr(ls, net.find('\n', at) - ls);
            TEST_ASSERT_TRUE_MESSAGE(line.find("//") != std::string::npos && line.find("//") < at - ls, api);
        }
    }

    // the private chip: copy of the default chip + private os table without region_protected
    TEST_ASSERT_TRUE_MESSAGE(net.find("#include <esp_flash.h>") != std::string::npos, "esp_flash.h is not included");
    TEST_ASSERT_TRUE_MESSAGE(net.find("s_sf.os = *esp_flash_default_chip->os_func;") != std::string::npos, "the os table is not copied from the default chip");
    TEST_ASSERT_TRUE_MESSAGE(net.find("s_sf.os.region_protected = nullptr;") != std::string::npos, "region_protected is not cleared on the private os table");
    TEST_ASSERT_TRUE_MESSAGE(net.find("s_sf.chip = *esp_flash_default_chip;") != std::string::npos, "the chip is not copied from the default chip");
    TEST_ASSERT_TRUE_MESSAGE(net.find("s_sf.chip.os_func = &s_sf.os;") != std::string::npos, "the private chip does not use the private os table");

    // exactly one place calls the esp_flash_* functions, and it is the helper
    size_t nEr = 0, nWr = 0, nRd = 0;
    for (size_t at = 0; (at = net.find("esp_flash_erase_region(&s_sf.chip", at)) != std::string::npos; at++) nEr++;
    for (size_t at = 0; (at = net.find("esp_flash_write(&s_sf.chip", at)) != std::string::npos; at++) nWr++;
    for (size_t at = 0; (at = net.find("esp_flash_read(&s_sf.chip", at)) != std::string::npos; at++) nRd++;
    TEST_ASSERT_EQUAL_UINT(1, nEr);
    TEST_ASSERT_EQUAL_UINT(1, nWr);
    TEST_ASSERT_EQUAL_UINT(1, nRd);
    const size_t fn = net.find("esp_err_t stageFlash(StageOp op, uint32_t rel, void *buf, uint32_t len)\n{");
    TEST_ASSERT_TRUE_MESSAGE(fn != std::string::npos, "the stageFlash() helper is missing");
    const std::string hb = net.substr(fn, net.find("\n}\n", fn) - fn);
    TEST_ASSERT_TRUE_MESSAGE(hb.find("esp_flash_erase_region(&s_sf.chip") != std::string::npos &&
                                 hb.find("esp_flash_write(&s_sf.chip") != std::string::npos &&
                                 hb.find("esp_flash_read(&s_sf.chip") != std::string::npos,
                             "the esp_flash_* calls are not inside stageFlash()");
    // the range guard comes first: armed, non-empty, at or above the stage start, inside ota_0, no overflow
    const size_t guard = hb.find("if (!s_sf.armed || len == 0 || rel < s_sf.lo || rel > s_sf.size || len > s_sf.size - rel)");
    TEST_ASSERT_TRUE_MESSAGE(guard != std::string::npos && guard < hb.find("esp_flash_"), "the stage range assert is missing or not before the flash calls");
    // the chip is armed from fwStageLayout's offset only, after the layout check, and disarmed again
    const size_t prep = net.find("const char *stagePrepare(");
    const size_t layout = net.find("fwStageLayout(", prep);
    const size_t arm = net.find("stageFlashArm(ota0, off)", prep);
    TEST_ASSERT_TRUE_MESSAGE(layout != std::string::npos && arm != std::string::npos && layout < arm, "the chip is armed before the layout check");
    TEST_ASSERT_TRUE_MESSAGE(net.find("stageFlashDisarm();", net.find("const char *stageFinish(")) != std::string::npos, "stageFinish() does not disarm the private chip");
    // only the stage steps call the helper
    for (size_t at = 0; (at = net.find("stageFlash(STAGE_", at)) != std::string::npos; at++)
    {
        const bool inDl = at > net.find("bool dlFeed(") && at < net.find("const char *layoutReason(");
        const bool inPrep = at > prep && at < net.find("const char *stageFinish(");
        const bool inFin = at > net.find("const char *stageFinish(") && at < net.find("const char *runDownload()");
        TEST_ASSERT_TRUE_MESSAGE(inDl || inPrep || inFin, "stageFlash() is called outside dlFeed/stagePrepare/stageFinish");
    }
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("esp_flash") == std::string::npos, "the private chip leaked into the net header");

    // R4: clearing the record also removes Safeboot's attempt counter, in the clear helper and on a stale record
    const size_t clr = net.find("bool nvsClearRecord()\n{");
    TEST_ASSERT_TRUE_MESSAGE(clr != std::string::npos, "nvsClearRecord() missing");
    const std::string cb = net.substr(clr, net.find("\n}\n", clr) - clr);
    TEST_ASSERT_TRUE_MESSAGE(cb.find("p.remove(FWNET_KEY_TRIES)") != std::string::npos, "nvsClearRecord() does not remove \"tries\"");
    TEST_ASSERT_TRUE_MESSAGE(net.find("#define FWNET_KEY_TRIES \"tries\"") != std::string::npos, "the tries key name changed (Safeboot uses \"tries\")");
    const size_t stale = net.find("[AU];refuse;stalerec");
    TEST_ASSERT_TRUE_MESSAGE(stale != std::string::npos && net.rfind("p.remove(FWNET_KEY_TRIES)", stale) > net.rfind("bool fwNetLoadRecord(", stale),
                             "a stale record does not take Safeboot's tries counter with it");
    // stagePrepare clears through nvsClearRecord() before the erase
    TEST_ASSERT_TRUE_MESSAGE(net.find("nvsClearRecord()", prep) < net.find("stageFlash(STAGE_ERASE", prep), "stagePrepare erases before it clears the record/tries");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("void fwNetClearRecord(void);") != std::string::npos, "fwNetClearRecord() prototype missing");
}

// AU-08 follow-up: old-Safeboot guard. OTA rewrites only ota_0, so an updated node keeps a Safeboot
// that ignores FWS2; without this the auto handover would reboot the node every update window.
static void test_old_safeboot_guard_marks_gates_and_reports()
{
    const std::string net = read_repo_file("src/esp32/fw_update_net.cpp");
    const std::string hdr = read_repo_file("src/esp32/fw_update_net.h");
    const std::string m = read_repo_file("src/esp32/esp32_main.cpp");
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    // NVS keys (the Safeboot side uses the same names)
    TEST_ASSERT_TRUE_MESSAGE(net.find("#define FWNET_KEY_HAND \"hand\"") != std::string::npos, "the hand key name changed");
    TEST_ASSERT_TRUE_MESSAGE(net.find("#define FWNET_KEY_NOCAP \"nocap\"") != std::string::npos, "the nocap key name changed");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("bool fwNetMarkHandover(const char *tag);") != std::string::npos &&
                                 hdr.find("void fwNetUnmarkHandover(void);") != std::string::npos &&
                                 hdr.find("bool fwNetSafebootOld(void);") != std::string::npos,
                             "the old-Safeboot API is not declared in fw_update_net.h");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("bool fwNetSafebootOld(void);") > hdr.find("#if defined(ESP32)"), "fwNetSafebootOld is outside the ESP32 guard");

    // boot check: hand + still-valid record => nocap + [AU];refuse;old_safeboot; hand always consumed; once
    const size_t bc = net.find("void bootCheckOnce()\n{");
    TEST_ASSERT_TRUE_MESSAGE(bc != std::string::npos, "bootCheckOnce() missing");
    const std::string b = net.substr(bc, net.find("\nvoid ensureStagedLoaded()", bc) - bc);
    TEST_ASSERT_TRUE_MESSAGE(b.find("s_hcClaimed") != std::string::npos, "the boot check is not once-only");
    const size_t ld = b.find("fwNetLoadRecord(r)");
    const size_t nc = b.find("putUChar(FWNET_KEY_NOCAP, nocapTag())");
    const size_t rm = b.find("p.remove(FWNET_KEY_HAND)");
    TEST_ASSERT_TRUE_MESSAGE(ld != std::string::npos && nc != std::string::npos && rm != std::string::npos && ld < nc && nc < rm,
                             "boot check order must be: load record, set nocap, remove hand");
    TEST_ASSERT_TRUE_MESSAGE(b.find("if (markOld)\n                p.putUChar") != std::string::npos, "nocap is not conditional on a surviving record");
    TEST_ASSERT_TRUE_MESSAGE(b.find("[AU];refuse;old_safeboot") != std::string::npos, "the old_safeboot refuse line is missing");
    TEST_ASSERT_TRUE_MESSAGE(net.find("bootCheckOnce();", net.find("void ensureStagedLoaded()")) != std::string::npos, "the status load does not run the boot check");
    const size_t so = net.find("bool fwNetSafebootOld(void)\n{");
    TEST_ASSERT_TRUE_MESSAGE(so != std::string::npos && net.find("bootCheckOnce();", so) - so < 60, "fwNetSafebootOld() does not run the boot check first");

    // auto path: nocap gates it, marker before the reboot, once per boot
    const size_t tick = m.find("static void auTick(void)");
    const size_t loop = m.find("void esp32loop()");
    const std::string t = m.substr(tick, loop - tick);
    const size_t ho = t.find("if(act == FW_HANDOVER)");
    const size_t blocked = t.find("[AU];handover;blocked;old_safeboot", ho);
    const size_t gate = t.find("if(!fwNetSafebootCapable())", ho);   // AU-12: capability, not only the nocap flag
    const size_t mark = t.find("fwNetMarkHandover(rec.tag)", ho);
    const size_t reboot = t.find("auRebootToSafeboot()", ho);
    TEST_ASSERT_TRUE_MESSAGE(gate != std::string::npos && blocked != std::string::npos && mark != std::string::npos && reboot != std::string::npos,
                             "auto handover lacks the old-Safeboot gate, the blocked marker or the hand marker");
    TEST_ASSERT_TRUE_MESSAGE(gate < blocked && blocked < mark && mark < reboot, "auto path order must be: nocap gate, blocked marker, hand marker, reboot");
    TEST_ASSERT_TRUE_MESSAGE(t.find("return;", blocked) < mark, "the blocked branch does not return before the handover");
    TEST_ASSERT_TRUE_MESSAGE(t.find("s_auBlockedPrinted") != std::string::npos, "the blocked marker is not once per boot");

    // --update apply: note when flagged, still tries, marker before the reboot, unmarked if the reboot returns
    const size_t rungUp = cmd.find("commandCheck(msg_text+2, (char*)\"update \") == 0");
    const size_t bareUp = cmd.find("commandCheck(msg_text+2, (char*)\"update\") == 0");
    const std::string body = cmd.substr(rungUp, bareUp - rungUp);
    const size_t ap = body.find("casecmp(_owner_c, (char*)\"apply\") == 0");
    const std::string a = body.substr(ap, body.find("casecmp(_owner_c, (char*)\"status\") == 0") - ap);
    const size_t note = a.find("[AU];apply;note;old_safeboot_flag_set_trying_anyway");
    const size_t amark = a.find("fwNetMarkHandover(_rec.tag)");
    const size_t areboot = a.find("auRebootToSafeboot()");
    TEST_ASSERT_TRUE_MESSAGE(note != std::string::npos && amark != std::string::npos && areboot != std::string::npos, "--update apply lacks the note, the hand marker or the reboot");
    TEST_ASSERT_TRUE_MESSAGE(a.find("fwNetSafebootOld()") < note && note < amark && amark < areboot, "--update apply order must be: flag note, hand marker, reboot");
    TEST_ASSERT_TRUE_MESSAGE(a.find("fwNetUnmarkHandover()", areboot) != std::string::npos, "--update apply leaves the hand marker if the reboot returned");
    // apply is not gated on the flag (no return/else on fwNetSafebootOld)
    TEST_ASSERT_TRUE_MESSAGE(a.find("if(fwNetSafebootOld())\n                    Serial.printf") != std::string::npos, "--update apply gates on the flag instead of only noting it");

    // status: ;safeboot;old|ok (old whenever the Safeboot is not capable), existing prefix unchanged, AU-12 version fields appended last
    TEST_ASSERT_TRUE_MESSAGE(body.find("[AU];status;mode;%s;chan;%s;avail;%s;newer;%d;staged;%s;busy;%d;err;%s;safeboot;%s;sbver;%d;sbneed;%d\\n") != std::string::npos,
                             "--update status lacks the safeboot / sbver / sbneed fields");
    TEST_ASSERT_TRUE_MESSAGE(body.find("!fwNetSafebootCapable() ? \"old\" : \"ok\"") != std::string::npos, "--update status does not derive old|ok from the capability");
    TEST_ASSERT_TRUE_MESSAGE(body.find("fwNetSafebootVersion(), AU_SAFEBOOT_MIN);") != std::string::npos, "--update status does not pass the version and AU_SAFEBOOT_MIN last");

    // clearing the record also clears the marker (a hopeless handover must not leave one behind)
    const size_t clr = net.find("bool nvsClearRecord()\n{");
    TEST_ASSERT_TRUE_MESSAGE(net.substr(clr, net.find("\n}\n", clr) - clr).find("p.remove(FWNET_KEY_HAND)") != std::string::npos, "nvsClearRecord() does not remove hand");
}

// AU-12 (#1187): versioned Safeboot. Auto update may only be switched on, and may only run, while the
// Safeboot partition is capable (version >= AU_SAFEBOOT_MIN). Source-scan only: the scanner itself is
// covered by test_safeboot_ver, the fw_update_net.cpp bodies by the bench.
// AU-12 rework (advisor F1/F2/F6): the nocap verdict is tied to the Safeboot version it was earned under,
// the web aumode path cannot change the channel when auto is refused, --update apply names a too-old Safeboot.
static void test_safeboot_nocap_is_version_tagged_and_web_order()
{
    const std::string net = read_repo_file("src/esp32/fw_update_net.cpp");
    TEST_ASSERT_TRUE_MESSAGE(net.find("p.putUChar(FWNET_KEY_NOCAP, nocapTag());") != std::string::npos, "nocap is not written with the version tag");
    TEST_ASSERT_TRUE_MESSAGE(net.find("old = nc != 0 && nc == nocapTag();") != std::string::npos, "a nocap of another Safeboot version still counts");
    const size_t clr = net.find("p.remove(FWNET_KEY_NOCAP);");
    TEST_ASSERT_TRUE_MESSAGE(clr != std::string::npos && net.find("[AU];safeboot;nocap;cleared", clr) != std::string::npos, "a stale nocap is not cleared and logged");

    const std::string web = read_repo_file("src/web_functions/web_setup.cpp");
    const size_t au = web.find("\"--autoupdate auto\");", web.find("AU-12: auto first"));
    const size_t ch = web.find("\"--updchan %s\", auDev", au);
    TEST_ASSERT_TRUE_MESSAGE(au != std::string::npos && ch != std::string::npos && au < ch, "aumode must run --autoupdate auto before --updchan");
    TEST_ASSERT_TRUE_MESSAGE(web.find("if(meshcom_settings.node_autoupd == 2) {", au) < ch, "--updchan must run only when auto took");

    const std::string cmd = read_repo_file("src/command_functions.cpp");
    TEST_ASSERT_TRUE_MESSAGE(cmd.find("[AU];apply;note;safeboot_too_old;ver;%d;trying_anyway") != std::string::npos, "--update apply is silent on a too-old Safeboot");
}

static void test_safeboot_capability_gates_auto_update()
{
    const std::string hdr = read_repo_file("src/esp32/fw_update_net.h");
    const std::string m = read_repo_file("src/esp32/esp32_main.cpp");
    const std::string cmd = read_repo_file("src/command_functions.cpp");

    // the two declarations exist, inside the ESP32 guard of the header
    const size_t g = hdr.find("#if defined(ESP32)");
    const size_t dv = hdr.find("int fwNetSafebootVersion(void);");
    const size_t dc = hdr.find("bool fwNetSafebootCapable(void);");
    TEST_ASSERT_TRUE_MESSAGE(g != std::string::npos && dv != std::string::npos && dc != std::string::npos,
                             "fwNetSafebootVersion()/fwNetSafebootCapable() are not declared in fw_update_net.h");
    TEST_ASSERT_TRUE_MESSAGE(dv > g && dc > g, "the capability declarations are outside the ESP32 guard");
    TEST_ASSERT_TRUE_MESSAGE(hdr.find("#endif // ESP32", g) > dc, "the capability declarations are not inside the ESP32 guard");

    // --autoupdate: refuse before anything is changed or saved; off / notify carry no capability check
    const size_t rung = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate \") == 0");
    const size_t bare = cmd.find("commandCheck(msg_text+2, (char*)\"autoupdate\") == 0");
    TEST_ASSERT_TRUE_MESSAGE(rung != std::string::npos && bare != std::string::npos && rung < bare, "--autoupdate rungs not found");
    const std::string body = cmd.substr(rung, bare - rung);
    const size_t bOff = body.find("casecmp(_owner_c, (char*)\"off\") == 0");
    const size_t bNotify = body.find("casecmp(_owner_c, (char*)\"notify\") == 0");
    const size_t bAuto = body.find("casecmp(_owner_c, (char*)\"auto\") == 0");
    const size_t bElse = body.find("must be off, notify or auto");
    TEST_ASSERT_TRUE_MESSAGE(bOff != std::string::npos && bNotify != std::string::npos && bAuto != std::string::npos && bElse != std::string::npos &&
                                 bOff < bNotify && bNotify < bAuto && bAuto < bElse,
                             "--autoupdate off/notify/auto branch order changed");
    const std::string offNotify = body.substr(bOff, bAuto - bOff);
    TEST_ASSERT_TRUE_MESSAGE(offNotify.find("fwNetSafeboot") == std::string::npos, "--autoupdate off/notify must not need Safeboot");
    const std::string autoBr = body.substr(bAuto, bElse - bAuto);
    const size_t chk = autoBr.find("if(!fwNetSafebootCapable())");
    const size_t err = autoBr.find("[ERR];autoupdate;safeboot_too_old;ver;%d;need;%d\\n");
    const size_t hint = autoBr.find("[AU];note;flash once with the web flasher to install the new Safeboot");
    const size_t ret = autoBr.find("return;", chk);
    const size_t set2 = autoBr.find("meshcom_settings.node_autoupd = 2;");
    TEST_ASSERT_TRUE_MESSAGE(chk != std::string::npos && err != std::string::npos && hint != std::string::npos && ret != std::string::npos && set2 != std::string::npos,
                             "--autoupdate auto lacks the capability check, the refuse line, the hint or the return");
    TEST_ASSERT_TRUE_MESSAGE(chk < err && err < hint && hint < ret && ret < set2, "--autoupdate auto must refuse (and return) before node_autoupd = 2");
    TEST_ASSERT_TRUE_MESSAGE(autoBr.find("save_settings") == std::string::npos, "the auto branch saves before the shared save_settings() after the refuse");
    TEST_ASSERT_TRUE_MESSAGE(body.find("save_settings();", bElse) != std::string::npos && bElse < body.find("save_settings();", bElse),
                             "the shared save_settings() must stay after the refuse");
    TEST_ASSERT_TRUE_MESSAGE(autoBr.find("fwNetSafebootVersion(), AU_SAFEBOOT_MIN") != std::string::npos, "the refuse line does not report version and need");

    // boot demotion: once, only when auto, to notify, saved, announced; no reboot
    const size_t tick = m.find("static void auTick(void)");
    const size_t loop = m.find("void esp32loop()");
    TEST_ASSERT_TRUE_MESSAGE(tick != std::string::npos && loop != std::string::npos && tick < loop, "auTick() not found");
    const std::string t = m.substr(tick, loop - tick);
    const size_t once = t.find("static bool s_auBootCapChecked = false;");
    const size_t cond = t.find("meshcom_settings.node_autoupd == 2 && !fwNetSafebootCapable()");
    const size_t dem = t.find("meshcom_settings.node_autoupd = 1;", cond);
    const size_t sv = t.find("save_settings();", dem);
    const size_t msg = t.find("[AU];refuse;safeboot_too_old;mode;notify;ver;%d;need;%d\\n", sv);
    const size_t timerInit = t.find("fwTimerInit(");
    TEST_ASSERT_TRUE_MESSAGE(once != std::string::npos && cond != std::string::npos && dem != std::string::npos && sv != std::string::npos && msg != std::string::npos,
                             "auTick lacks the boot demotion (once-flag, condition, node_autoupd = 1, save_settings, refuse line)");
    TEST_ASSERT_TRUE_MESSAGE(once < cond && cond < dem && dem < sv && sv < msg, "boot demotion order must be: once flag, condition, set notify, save, announce");
    TEST_ASSERT_TRUE_MESSAGE(t.find("s_auBootCapChecked = true;", once) < cond, "the demotion once-flag is not set before the check");
    TEST_ASSERT_TRUE_MESSAGE(msg < timerInit, "the boot demotion must run before the AU timer is initialised");
    TEST_ASSERT_TRUE_MESSAGE(t.substr(once, msg - once).find("ESP.restart") == std::string::npos, "the boot demotion must not reboot");
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
    RUN_TEST(test_rmstrictsecurity_does_not_collide_with_other_commands);
    RUN_TEST(test_rmstrictsecurity_rung_schema_row_and_default_match_the_assumptions);
    RUN_TEST(test_autoupdate_updchan_do_not_collide_with_other_commands);
    RUN_TEST(test_autoupdate_updchan_rungs_schema_rows_and_defaults_match_the_assumptions);
    RUN_TEST(test_update_does_not_collide_with_other_commands);
    RUN_TEST(test_update_rungs_sit_in_the_esp32_block_with_the_expected_output);
    RUN_TEST(test_au_tick_and_wifi_gate_are_wired_in_esp32_main);
    RUN_TEST(test_ota_update_and_the_handover_share_one_reboot_function);
    RUN_TEST(test_update_apply_hands_over_now_and_is_not_remote);
    RUN_TEST(test_web_temp_offset_takes_comma_and_leading_zero);
    RUN_TEST(test_stagelan_is_instrument_only_everywhere);
    RUN_TEST(test_lan_url_accepts_only_plain_http_to_private_ipv4);
    RUN_TEST(test_stage_flash_goes_through_one_guarded_helper);
    RUN_TEST(test_old_safeboot_guard_marks_gates_and_reports);
    RUN_TEST(test_safeboot_capability_gates_auto_update);
    RUN_TEST(test_safeboot_nocap_is_version_tagged_and_web_order);
    return UNITY_END();
}
