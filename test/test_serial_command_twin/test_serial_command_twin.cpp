// Twin test for checkSerialCommand() (test plan U3 / section 4.5, N1).
//
// TWO BINARIES, not one -- see serial_command.h and the platformio.ini
// comment above native_serial_esp32/native_serial_nrf52. Both platform
// copies define the same symbol, so they cannot be linked together; this
// source is built once per side against exactly one of them:
//
//   pio test -e native_serial_esp32 -f test_serial_command_twin
//   pio test -e native_serial_nrf52 -f test_serial_command_twin
//
// The pass condition is the U6 country-twin one, not the U1/U2 one: each
// side's transcript is compared against ITS OWN committed baseline file
// under test/golden/native/, not against the other side's. Several of the
// scenario rows below happen to produce identical text on both sides (the
// shared parser logic is, after all, shared) -- that is incidental, not the
// point. Two rows are DESIGNED to differ (see the net-console drift test),
// which is exactly why this is a two-file, not a one-file, comparison.
//
// The table is printed on every run (like test_country_twin's), so
// regenerating a baseline after a deliberate change is copy-paste from the
// test output, not a hand edit.

#include <unity.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <serial_command.h>
#include <printfdeb_functions.h>
#include <command_functions.h>

#if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
#define SIDE "nrf52"
#define HAS_NET_CONSOLE 0
#else
#define SIDE "esp32"
#define HAS_NET_CONSOLE 1
#endif

#define EXPECTED_PATH "test/golden/native/serial-command-" SIDE ".txt"

// ---------------------------------------------------------------------------
// The externs checkSerialCommand() reads/writes. Only symbols it actually
// references need a definition here (the linker never asks for the rest of
// loop_functions_extern.h's several hundred externs -- they are declared,
// never used, from this one translation unit).
// ---------------------------------------------------------------------------

bool bDEBUG = false;
int isPhoneReady = 7;   // arbitrary, fixed: proves the value is forwarded, not hardcoded
char msg_text[MAX_MSG_LEN_PHONE * 2];

// --- recording sinks -------------------------------------------------------

struct SendCall
{
    std::string buf;   // exactly `len` bytes, NOT relying on a NUL (matches
                        // the explicit-length contract sendMessage() is
                        // documented to take, per BP-09 in loop_functions.h)
    int len;
};
struct CmdCall
{
    std::string buf;    // commandAction() gets a pointer only, no length --
                         // this relies on msg_buffer's own NUL, which the
                         // fill loop always writes right after the last
                         // real character.
    int iphone;
    bool rxFromPhone;
};

static std::vector<SendCall> g_sendMessage_calls;
static std::vector<CmdCall> g_commandAction_calls;
static std::vector<std::string> g_printfdeb_calls;
static int g_echo_count = 0;
static std::vector<int> g_origin_calls;   // MsgOrigin values, in call order

// net console byte source. A plain cursor over a string, not a queue with
// pop-front, since nothing here needs to interleave feeding and draining.
static std::string g_nc_bytes;
static size_t g_nc_pos = 0;

static void resetRecorders()
{
    g_sendMessage_calls.clear();
    g_commandAction_calls.clear();
    g_printfdeb_calls.clear();
    g_printfdeb_calls.reserve(4);
    g_echo_count = 0;
    g_origin_calls.clear();
    bDEBUG = false;
    isPhoneReady = 7;
    Serial.clearIn();
    Serial.clear();
    g_nc_bytes.clear();
    g_nc_pos = 0;
}

// --- definitions for the symbols checkSerialCommand() calls ---------------

int sendMessage(char *buf, int len)
{
    SendCall c;
    c.buf.assign(buf, buf + (len > 0 ? len : 0));
    c.len = len;
    g_sendMessage_calls.push_back(c);
    return 0;   // BP_SEND_OK
}

void commandAction(char *buf, int iphone, bool rxFromPhone)
{
    CmdCall c;
    c.buf = buf ? buf : "";
    c.iphone = iphone;
    c.rxFromPhone = rxFromPhone;
    g_commandAction_calls.push_back(c);
}

// The other commandAction() overload declared in command_functions.h
// (char*, bool) is never called by checkSerialCommand() and is intentionally
// left undefined -- nothing here links against it.

int printdeb(char) { g_echo_count++; return 1; }

int printfdeb(const char *format, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    if (n > 0)
        g_printfdeb_calls.push_back(std::string(buf));
    return n;
}

void setMsgOrigin(MsgOrigin origin) { g_origin_calls.push_back((int)origin); }
MsgOrigin getMsgOrigin(void) { return g_origin_calls.empty() ? ORIGIN_NONE : (MsgOrigin)g_origin_calls.back(); }

// net console (stubs/net_console.h): declared unconditionally there so the
// ESP32 copy's `#ifndef DISABLE_NET_CONSOLE` block compiles; on the nRF52
// side these are defined but never called (that absence of a call is the
// drift the dedicated test below pins).
bool netConsoleAvailable() { return g_nc_pos < g_nc_bytes.size(); }
int netConsoleRead()
{
    if (g_nc_pos >= g_nc_bytes.size())
        return -1;
    return (unsigned char)g_nc_bytes[g_nc_pos++];
}

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------

static void pump(int n) { for (int i = 0; i < n; i++) checkSerialCommand(); }

static void feed_serial_and_pump(const std::string &bytes)
{
    Serial.clearIn();
    Serial.feed(bytes.data(), bytes.size());
    pump((int)bytes.size() + 2);   // +2: drains the last byte and settles
}

static void nc_feed(const std::string &bytes)
{
    g_nc_bytes = bytes;
    g_nc_pos = 0;
}

// ---------------------------------------------------------------------------
// Formatting: one line per scenario, escaped so control bytes are visible
// and the baseline file stays plain text.
// ---------------------------------------------------------------------------

static std::string escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\\': out += "\\\\"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    char h[6];
                    snprintf(h, sizeof(h), "\\x%02X", c);
                    out += h;
                }
                else
                {
                    out += (char)c;
                }
        }
    }
    return out;
}

// Long buffers (the capacity-boundary scenario fills ~600 bytes) are
// summarized rather than dumped whole, so the golden file stays a table
// instead of growing a 600-column row.
static std::string describe_buf(const std::string &s)
{
    if (s.size() <= 40)
    {
        std::string out = "\"";
        out += escape(s);
        out += "\"";
        return out;
    }
    char head[64];
    snprintf(head, sizeof(head), "len=%d prefix=\"%s\" suffix=\"%s\"",
             (int)s.size(), escape(s.substr(0, 12)).c_str(),
             escape(s.substr(s.size() - 12)).c_str());
    return std::string(head);
}

// Runs one Serial-fed scenario and returns its formatted row. `debugOn`
// controls bDEBUG for the DISCARD path's gated debug print (the other two
// printfdeb call sites -- "wrong command" -- are unconditional and unaffected).
static std::string run_scenario(const char *name, const std::string &bytes, bool debugOn)
{
    resetRecorders();
    bDEBUG = debugOn;
    feed_serial_and_pump(bytes);

    std::string action, detail;
    if (!g_sendMessage_calls.empty())
    {
        action = "SEND";
        const SendCall &c = g_sendMessage_calls.back();
        char lenbuf[16];
        snprintf(lenbuf, sizeof(lenbuf), " len=%d", c.len);
        detail = "buf=" + describe_buf(c.buf) + lenbuf;
    }
    else if (!g_commandAction_calls.empty())
    {
        action = "CMD";
        const CmdCall &c = g_commandAction_calls.back();
        char rest[32];
        snprintf(rest, sizeof(rest), " iphone=%d rxFromPhone=%d", c.iphone, c.rxFromPhone ? 1 : 0);
        detail = "buf=" + describe_buf(c.buf) + rest;
    }
    else if (!g_printfdeb_calls.empty() && g_printfdeb_calls[0].rfind("\n...wrong command", 0) == 0)
    {
        action = "WRONG";
        detail = "msg=\"" + escape(g_printfdeb_calls[0]) + "\"";
    }
    else
    {
        action = "DISCARD";
        if (g_printfdeb_calls.empty())
            detail = "(no debug print)";
        else
        {
            detail = "msgs=[";
            for (size_t i = 0; i < g_printfdeb_calls.size(); i++)
            {
                if (i) detail += "; ";
                detail += escape(g_printfdeb_calls[i]);
            }
            detail += "]";
        }
    }

    char line[900];
    snprintf(line, sizeof(line), "%-20s echo=%-4d action=%-8s %s",
             name, g_echo_count, action.c_str(), detail.c_str());
    return std::string(line);
}

// ---------------------------------------------------------------------------
// Scenario table
// ---------------------------------------------------------------------------

struct Scenario
{
    const char *name;
    std::string bytes;
    bool debugOn;
};

static std::vector<Scenario> scenarios()
{
    std::string capacity_cmd = "--" + std::string(596, 'A') + "\r";   // 599 bytes total, see comment below
    return {
        {"dash_info",        "--info\r", false},
        {"colon_group9",     "::9 characterization test\r", false},
        {"single_dash_wrong","-x\r", false},
        {"brace_wrong",      "{cfg\r", false},
        {"plain_discard",    "hi\r", false},
        {"plain_discard_dbg","hi\r", true},
        {"empty_line",       "\r", false},
        {"crlf_ending",      "--info\r\n", false},
        {"lf_only_ending",   "--info\n", false},
        {"embedded_nul",     std::string(":a\0b\r", 5), false},
        {"backspace_edit",   "::9 helllo\x08\x08o\r", false},
        {"capacity_boundary",capacity_cmd, false},
    };
}

void setUp(void) {}
void tearDown(void) {}

static void test_table_matches_the_committed_baseline(void)
{
    printf("\n--- checkSerialCommand(), " SIDE " side ---\n");
    std::vector<std::string> rows;
    for (auto &s : scenarios())
    {
        rows.push_back(run_scenario(s.name, s.bytes, s.debugOn));
        printf("%s\n", rows.back().c_str());
    }
    printf("--- end, baseline: " EXPECTED_PATH " ---\n\n");

    FILE *f = fopen(EXPECTED_PATH, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(
        f, "no committed baseline at " EXPECTED_PATH
           " -- copy the table printed above into it");

    char line[900];
    size_t i = 0;
    while (fgets(line, sizeof(line), f) && i < rows.size())
    {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;
        char msg[256];
        snprintf(msg, sizeof(msg), "row %zu: baseline %s", i, line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(line, rows[i].c_str(), msg);
        i++;
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)rows.size(), (int)i,
                                  "baseline has a different number of rows");
}

// ---------------------------------------------------------------------------
// Edge cases that need mid-state assertions, not a one-line transcript
// ---------------------------------------------------------------------------

static void test_line_split_across_multiple_calls_still_accumulates(void)
{
    resetRecorders();
    Serial.feed("--in", 4);
    pump(6);
    TEST_ASSERT_TRUE_MESSAGE(g_commandAction_calls.empty(),
                             "command fired before the line was complete");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, g_echo_count, "partial bytes were not echoed");

    Serial.feed("fo\r", 3);
    pump(5);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_commandAction_calls.size(),
                                  "the completed line never reached commandAction");
    TEST_ASSERT_EQUAL_STRING("--info", g_commandAction_calls[0].buf.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, g_echo_count,
                                  "echo count across the split does not add up");
}

static void test_origin_bracket_set_and_cleared_around_sendmessage(void)
{
    // BP-01: setMsgOrigin(ORIGIN_SERIAL) immediately before sendMessage(),
    // setMsgOrigin(ORIGIN_NONE) immediately after -- identical on both
    // platform copies. Mutation-sensitive: dropping either call, or
    // reordering them, is caught here.
    resetRecorders();
    feed_serial_and_pump("::9 hi\r");
    TEST_ASSERT_EQUAL_INT(1, (int)g_sendMessage_calls.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)g_origin_calls.size(),
                                  "expected exactly the set/clear pair");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)ORIGIN_SERIAL, g_origin_calls[0],
                                  "origin was not tagged ORIGIN_SERIAL before sendMessage");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)ORIGIN_NONE, g_origin_calls[1],
                                  "origin was not cleared after sendMessage");
}

static void test_stale_buffer_content_does_not_leak_between_calls(void)
{
    // N-22 (serial_command.h): msg_buffer is `static` on nRF52 (BSS, a
    // loop-task stack fix) and a plain local on ESP32 (stack). The
    // difference is a storage-class change, not a logic change, and it is
    // NOT observable from here: commandAction()/sendMessage() only ever see
    // msg_buffer through a pointer the fill loop has already NUL-terminated
    // right after the last real character, on both sides, every time. A
    // stack copy's un-overwritten tail is exactly as invisible to a caller
    // as a BSS copy's un-overwritten tail, as long as that terminator write
    // is intact -- which this pins, rather than the storage duration, which
    // this cannot reach without taking msg_buffer's address (the carve
    // deliberately keeps it file-local; see serial_command.h).
    //
    // What IS pinned: a large fill followed by a short one leaves no trace
    // of the large one, on either side.
    resetRecorders();
    feed_serial_and_pump(std::string("--") + std::string(300, 'B') + "\r");
    TEST_ASSERT_EQUAL_INT(1, (int)g_commandAction_calls.size());
    TEST_ASSERT_EQUAL_INT(302, (int)g_commandAction_calls[0].buf.size());

    resetRecorders();
    feed_serial_and_pump("--hi\r");
    TEST_ASSERT_EQUAL_INT(1, (int)g_commandAction_calls.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("--hi", g_commandAction_calls[0].buf.c_str(),
                                     "short command carries a stale tail from the prior long fill");
}

// ---------------------------------------------------------------------------
// DRIFT: the net console. ESP32 also reads it; nRF52 has none at all --
// not "reads it and ignores it", literally no call to either function.
// ---------------------------------------------------------------------------

static void test_drift_net_console_esp32_only_nrf52_ignores_it(void)
{
    resetRecorders();
    // LF, not CR: the net console branch explicitly strips '\r'
    // (`rd != '\r' && rd != 0x00`) before storing, so CR can never complete
    // a line from this input path -- only Serial's does that. Keep LF, an
    // independent within-ESP32 nuance from the esp32-vs-nrf52 drift this
    // test exists for, noted rather than silently worked around.
    nc_feed("--info\n");
    pump(10);   // more than enough calls to drain 7 bytes, on the side that reads them at all

#if HAS_NET_CONSOLE
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_commandAction_calls.size(),
                                  "esp32 stopped reading the net console");
    TEST_ASSERT_EQUAL_STRING("--info", g_commandAction_calls[0].buf.c_str());
    TEST_ASSERT_TRUE_MESSAGE(g_nc_pos >= g_nc_bytes.size(),
                             "esp32 left bytes unread in the net console queue");
#else
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_commandAction_calls.size(),
                                  "nrf52 grew a net-console reader -- drift row changed, update the matrix");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_nc_pos,
                                  "nrf52 advanced the net console cursor despite calling neither "
                                  "netConsoleAvailable() nor netConsoleRead()");
#endif

#if HAS_NET_CONSOLE
    // Telnet IAC negotiation skip (ESP32-only branch): 0xFF then two option
    // bytes must be consumed without entering the command buffer at all.
    resetRecorders();
    std::string withIac;
    withIac.push_back((char)0xFF);
    withIac.push_back((char)0xFB);   // WILL
    withIac.push_back((char)0x01);   // ECHO
    withIac += "--info\n";           // LF: see the CR note above
    nc_feed(withIac);
    pump((int)withIac.size() + 2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_commandAction_calls.size(),
                                  "esp32 did not recover the command after an IAC sequence");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("--info", g_commandAction_calls[0].buf.c_str(),
                                     "esp32 let IAC bytes leak into the command buffer");
#endif
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_matches_the_committed_baseline);
    RUN_TEST(test_line_split_across_multiple_calls_still_accumulates);
    RUN_TEST(test_origin_bracket_set_and_cleared_around_sendmessage);
    RUN_TEST(test_stale_buffer_content_does_not_leak_between_calls);
    RUN_TEST(test_drift_net_console_esp32_only_nrf52_ignores_it);
    return UNITY_END();
}
