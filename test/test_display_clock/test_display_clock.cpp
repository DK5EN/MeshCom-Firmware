// CLK-01: redraw gate of sendDisplayTime() (src/display_clock_gate.h).
// W2 contract: every second draws on pages with a clock line; no display / off / message page (type 0,
// F1) / draw in progress skip.
#include <unity.h>
#include <string.h>
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "display_clock_gate.h"

void setUp() {}
void tearDown() {}

static DispClkInputs base(int sec = 0)
{
    DispClkInputs in;
    in.hasDisplay = true; in.displayIsOff = false; in.displayType = 1; in.pageHold = 0;
    in.setDisplay = false; in.second = sec; in.oneButton = false; in.displayDirty = false;
    return in;
}

// REAL pre-W2 logic (as in HEAD before the W2 change), kept verbatim as the oracle: type0 skip (F1)
// and the 15 s gate (F2).
static DispClkDecision oldLogic(const DispClkInputs &in)
{
    if (!in.hasDisplay) return DispClkDecision::SkipNoDisplay;
    if (in.displayIsOff) return DispClkDecision::SkipOff;
    if (in.displayType == 0) return DispClkDecision::SkipType0;
    if (in.setDisplay) return DispClkDecision::SkipSetDisplay;
    if (in.second == 0 || in.second == 15 || in.second == 30 || in.second == 45
        || in.oneButton || in.displayDirty)
        return DispClkDecision::Draw;
    return DispClkDecision::SkipGate15;
}

// The intended difference old -> W2 is exactly one: the 15 s gate (F2) is gone. Everything else,
// including the message-page skip (F1 operator decision), is unchanged.
void test_exhaustive_differs_from_old_logic_only_by_gate15(void)
{
    int gate_diffs = 0;
    for (int m = 0; m < 128; m++)
        for (int sec = 0; sec < 60; sec++)
            for (int ty = 0; ty < 10; ty += 3)
            {
                DispClkInputs in = base(sec);
                in.hasDisplay = m & 1; in.displayIsOff = m & 2; in.setDisplay = m & 4;
                in.oneButton = m & 8; in.displayDirty = m & 16; in.pageHold = (m & 32) ? 5 : 0;
                in.displayType = ty;
                const DispClkDecision o = oldLogic(in), n = dispClkDecide(in);
                if (o == DispClkDecision::SkipGate15)
                {
                    TEST_ASSERT_TRUE(n == DispClkDecision::Draw);
                    gate_diffs++;
                }
                else
                    TEST_ASSERT_TRUE(o == n);
            }
    TEST_ASSERT_TRUE(gate_diffs > 0);   // the gate difference really occurs in the sweep
}

// W2 (changed from the 15 s pin): the gate no longer depends on the second.
void test_gate15_seconds(void)
{
    for (int s = 0; s < 60; s++)
        TEST_ASSERT_TRUE(dispClkDecide(base(s)) == DispClkDecision::Draw);
}

void test_order_off_before_type0_before_setdisplay(void)
{
    DispClkInputs in = base(0);
    in.displayIsOff = true; in.displayType = 0; in.setDisplay = true;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipOff);
    in.displayIsOff = false;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipType0);
    in.displayType = 1;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipSetDisplay);
    in.setDisplay = false;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::Draw);
}

void test_pagehold_does_not_block_clock(void)
{
    DispClkInputs in = base(7);
    in.pageHold = 5;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::Draw);
}

void test_reason_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("type0", dispClkReason(DispClkDecision::SkipType0));
    TEST_ASSERT_EQUAL_STRING("hold", dispClkReason(DispClkDecision::SkipHold));
    TEST_ASSERT_EQUAL_STRING("setdisplay", dispClkReason(DispClkDecision::SkipSetDisplay));
    TEST_ASSERT_EQUAL_STRING("off", dispClkReason(DispClkDecision::SkipOff));
    TEST_ASSERT_EQUAL_STRING("ping", dispClkReason(DispClkDecision::SkipPing));
    TEST_ASSERT_EQUAL_STRING("gate15", dispClkReason(DispClkDecision::SkipGate15));
    TEST_ASSERT_EQUAL_STRING("draw", dispClkReason(DispClkDecision::Draw));
}

// F1 (operator decision 2026-11-08): the message page keeps its sender header, so no clock draw on
// iDisplayType == 0 at any second, with or without button / dirty / page hold (button browsing).
void test_f1_message_page_never_draws_clock(void)
{
    for (int s = 0; s < 60; s++)
        for (int m = 0; m < 8; m++)
        {
            DispClkInputs in = base(s);
            in.displayType = 0;
            in.oneButton = m & 1; in.displayDirty = m & 2; in.pageHold = (m & 4) ? 5 : 0;
            TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipType0);
        }
}

// Model of the sendDisplayClockLine() contract: pageText[0] is only written on Draw. On the message
// page pageText[0] is "GM<grp> <CALL> <rssi>" and must survive; on a clock page it becomes the clock.
void test_f1_message_header_survives_clock_pass(void)
{
    char pageText0[26];
    for (int type = 0; type < 10; type += 1)
    {
        if (type != 0 && type != 1 && type != 9) continue;
        strcpy(pageText0, "GM262 DK5EN-1 -97");
        DispClkInputs in = base(33);
        in.displayType = type;
        if (dispClkDecide(in) == DispClkDecision::Draw)
            strcpy(pageText0, "G     12:34:56  87%");
        if (type == 0)
            TEST_ASSERT_EQUAL_STRING("GM262 DK5EN-1 -97", pageText0);
        else
            TEST_ASSERT_EQUAL_STRING("G     12:34:56  87%", pageText0);
    }
}

// Ping pages are full-screen text without a clock line: kept like the message page.
void test_ping_active_predicate(void)
{
    TEST_ASSERT_TRUE(dispClkPingActive(true, 30, 3));
    TEST_ASSERT_FALSE(dispClkPingActive(false, 30, 3));   // no ping call
    TEST_ASSERT_FALSE(dispClkPingActive(true, 0, 3));     // no interval
    TEST_ASSERT_FALSE(dispClkPingActive(true, 30, 0));    // count used up
}

void test_fix_F2_every_second_draws(void)
{
    for (int s = 0; s < 60; s++)
        TEST_ASSERT_TRUE(dispClkDecide(base(s)) == DispClkDecision::Draw);
}

// Source pins on src/loop_functions.cpp.
static std::string read_repo_file(const char *rel)
{
    const std::string self = "test/test_display_clock/test_display_clock.cpp";
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        std::ifstream a((dir + "/" + self).c_str()), b((dir + "/platformio.ini").c_str());
        if (a.good() && b.good())
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string func_body(const std::string &src, const char *sig)
{
    const size_t start = src.find(sig);
    TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos, sig);
    const size_t end = src.find("\n}\n", start);
    return src.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// A1 "every page", track page: the page rebuild keeps its 15 s gate (full e-paper refresh / format
// cost), and on the other seconds the clock line is redrawn (sendDisplayClockLine) before the
// gated rebuild; iDisplayType = 9 is set only inside the rebuild, so a browsed message page (type 0)
// keeps its header (F1).
void test_display_track_clock_every_second_page_every_15s(void)
{
    const std::string body = func_body(read_repo_file("src/loop_functions.cpp"), "void sendDisplayTrack()");
    const size_t gate = body.find("meshcom_settings.node_date_second == 15");
    TEST_ASSERT_TRUE_MESSAGE(gate != std::string::npos, "sendDisplayTrack() lost its 15 s rebuild gate");
    const size_t clk = body.find("sendDisplayClockLine();");
    TEST_ASSERT_TRUE_MESSAGE(clk != std::string::npos, "sendDisplayTrack() no longer redraws the clock between rebuilds");
    TEST_ASSERT_TRUE_MESSAGE(clk > gate, "clock line call must sit in the not-a-rebuild-second branch");
    const size_t ty = body.find("iDisplayType=9;");
    TEST_ASSERT_TRUE_MESSAGE(ty != std::string::npos && ty > clk,
                             "iDisplayType=9 must be set only in the rebuild, after the clock-only branch");
}

// F1: the draw is decided by dispClkDecide() (type0 skip) before pageText[0] is touched.
void test_clockline_decides_before_touching_pagetext(void)
{
    const std::string body = func_body(read_repo_file("src/loop_functions.cpp"), "static void sendDisplayClockLine()\n{");
    const size_t in = body.find("dcIn.displayType  = iDisplayType;");
    const size_t dec = body.find("dispClkDecide(dcIn)");
    const size_t ret = body.find("return;", dec);
    const size_t mc = body.find("memcpy(pageText[0]");
    const size_t tft = body.find("displayTFT(msg_text)");
    TEST_ASSERT_TRUE_MESSAGE(in != std::string::npos && dec != std::string::npos && ret != std::string::npos,
                             "type feed / decision / early return not found");
    TEST_ASSERT_TRUE_MESSAGE(mc != std::string::npos && tft != std::string::npos, "pageText[0] / TFT write not found");
    TEST_ASSERT_TRUE_MESSAGE(in < dec && dec < ret && ret < mc && ret < tft,
                             "pageText[0] / TFT header written before the dispClkDecide() early return");
}

// Every function that builds a clock page draws the current time at once (sendDisplayMainline), so
// the clock is current on the page change back from a message page.
void test_clock_pages_draw_time_on_page_change(void)
{
    const std::string src = read_repo_file("src/loop_functions.cpp");
    static const char *sigs[] = {"void sendDisplayHead(bool bInit)", "void sendDisplayWX()",
                                 "void sendDisplayPosition(struct aprsMessage &aprsmsg"};
    for (const char *sig : sigs)
    {
        const std::string body = func_body(src, sig);
        TEST_ASSERT_TRUE_MESSAGE(body.find("sendDisplayMainline();") != std::string::npos, sig);
    }
    const std::string track = func_body(src, "void sendDisplayTrack()");
    TEST_ASSERT_TRUE_MESSAGE(track.find("sendDisplayMainline();") != std::string::npos, "track page");
}

// Configured ping: the caller gate is the shared predicate, not a copy.
void test_ping_gate_uses_shared_predicate(void)
{
    const std::string src = read_repo_file("src/loop_functions.cpp");
    const size_t call = src.find("sendDisplayTime(); // Time only");
    TEST_ASSERT_TRUE_MESSAGE(call != std::string::npos, "sendDisplayTime() call site not found");
    const size_t gate = src.rfind("dispClkPingActive(", call);
    TEST_ASSERT_TRUE_MESSAGE(gate != std::string::npos && call - gate < 400, "ping gate lost dispClkPingActive()");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_exhaustive_differs_from_old_logic_only_by_gate15);
    RUN_TEST(test_display_track_clock_every_second_page_every_15s);
    RUN_TEST(test_clockline_decides_before_touching_pagetext);
    RUN_TEST(test_clock_pages_draw_time_on_page_change);
    RUN_TEST(test_ping_gate_uses_shared_predicate);
    RUN_TEST(test_gate15_seconds);
    RUN_TEST(test_order_off_before_type0_before_setdisplay);
    RUN_TEST(test_pagehold_does_not_block_clock);
    RUN_TEST(test_reason_strings);
    RUN_TEST(test_f1_message_page_never_draws_clock);
    RUN_TEST(test_f1_message_header_survives_clock_pass);
    RUN_TEST(test_ping_active_predicate);
    RUN_TEST(test_fix_F2_every_second_draws);
    return UNITY_END();
}
