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
    in.holdActive = false;
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
    in.holdActive = true;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipHold);   // hold before type0
    in.holdActive = false;
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

// CLK-PING-01: a held full-screen frame (ping/pong page etc., holdActive = dispClkHeldFrame()) has no
// clock line: SkipHold. Without a hold every clock page draws, whether or not a ping is configured
// (the ping configuration is not an input of the decision at all).
void test_hold_skips_clock_no_hold_draws(void)
{
    for (int s = 0; s < 60; s += 7)
        for (int ty = 1; ty < 10; ty++)
        {
            DispClkInputs in = base(s);
            in.displayType = ty;
            in.holdActive = true;
            TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipHold);
            in.holdActive = false;
            TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::Draw);
        }
    DispClkInputs t0 = base(0);
    t0.displayType = 0; t0.holdActive = true;
    TEST_ASSERT_TRUE(dispClkDecide(t0) == DispClkDecision::SkipHold);   // hold wins over type0
    t0.holdActive = false;
    TEST_ASSERT_TRUE(dispClkDecide(t0) == DispClkDecision::SkipType0);  // message page stays frozen
    DispClkInputs off = base(0);
    off.displayIsOff = true; off.holdActive = true;
    TEST_ASSERT_TRUE(dispClkDecide(off) == DispClkDecision::SkipOff);   // off wins over hold
    DispClkInputs nod = base(0);
    nod.hasDisplay = false; nod.holdActive = true;
    TEST_ASSERT_TRUE(dispClkDecide(nod) == DispClkDecision::SkipNoDisplay);
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

// Code lines of a source window (comment-only lines dropped), so the assertions below do not trip
// over words in comments.
static std::string code_only(const std::string &w)
{
    std::istringstream is(w);
    std::string line, out;
    while (std::getline(is, line))
    {
        const size_t f = line.find_first_not_of(" \t");
        if (f == std::string::npos || line.compare(f, 2, "//") == 0) continue;
        out += line + "\n";
    }
    return out;
}

// CLK-PING-01 (1): sendDisplayTime() (button page-hold countdown + clock line) is called every second,
// unconditionally: no DisplayOffWait / hold / ping condition around the call site. The hold only
// suppresses the clock LINE, inside dispClkDecide() (fed from dispClkHeldFrame(): hold AND drawn frame).
void test_sendDisplayTime_call_site_ungated(void)
{
    const std::string src = read_repo_file("src/loop_functions.cpp");
    const size_t call = src.find("sendDisplayTime(); // Time only");
    TEST_ASSERT_TRUE_MESSAGE(call != std::string::npos, "sendDisplayTime() call site not found");
    const size_t start = src.rfind("#else", call);
    TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos && call - start < 600, "#else of the call site not found");
    const std::string code = code_only(src.substr(start, call - start));
    TEST_ASSERT_TRUE_MESSAGE(code.find("if") == std::string::npos, "sendDisplayTime() call is inside a condition");
    TEST_ASSERT_TRUE_MESSAGE(code.find("DisplayOffWait") == std::string::npos, "call gated on DisplayOffWait");
    TEST_ASSERT_TRUE_MESSAGE(code.find("dispClk") == std::string::npos && code.find("Hold") == std::string::npos, "call gated on hold");
    TEST_ASSERT_TRUE_MESSAGE(code.find("node_ping") == std::string::npos, "call gated on ping");
    // the hold is an input of the clock-line decision
    const std::string body = func_body(src, "static void sendDisplayClockLine()\n{");
    const size_t hold = body.find("dcIn.holdActive   = dispClkHeldFrame(DisplayOffWait, bHeldFrameDrawn);");
    const size_t dec = body.find("dispClkDecide(dcIn)");
    TEST_ASSERT_TRUE_MESSAGE(hold != std::string::npos && dec != std::string::npos && hold < dec,
                             "dcIn.holdActive not fed from dispClkHeldFrame() before dispClkDecide()");
    TEST_ASSERT_TRUE_MESSAGE(code_only(body).find("BOARD_T_DECK_PRO") == std::string::npos, "board special case in the hold input");
}

// CLK-PING-01 (2): esp32loop() ticks the clock (mainStartTimeLoop) whether or not a ping is configured.
void test_esp32_clock_tick_not_gated_on_ping(void)
{
    const std::string src = read_repo_file("src/esp32/esp32_main.cpp");
    const size_t call = src.find("mainStartTimeLoop();");
    TEST_ASSERT_TRUE_MESSAGE(call != std::string::npos, "mainStartTimeLoop() call not found in esp32_main.cpp");
    size_t start = src.rfind("\n\n", call);
    TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos, "block start not found");
    const std::string code = code_only(src.substr(start, call - start));
    TEST_ASSERT_TRUE_MESSAGE(code.find("node_ping") == std::string::npos, "mainStartTimeLoop() gated on the ping configuration");
    TEST_ASSERT_TRUE_MESSAGE(code.find("if") == std::string::npos, "mainStartTimeLoop() is inside a condition");
}

// CLK-PING-01 (3): on hold expiry with display-off the clear "#C" is sent regardless of a configured
// ping (as nrf52_main.cpp), else the pong frame stays lit on a display that is nominally off.
void test_esp32_hold_expiry_clear_not_gated_on_ping(void)
{
    const std::string src = read_repo_file("src/esp32/esp32_main.cpp");
    const size_t tick = src.find("mainStartTimeLoop();");
    TEST_ASSERT_TRUE_MESSAGE(tick != std::string::npos, "mainStartTimeLoop() call not found");
    const size_t clr = src.find("(char*)\"#C\"", tick);
    TEST_ASSERT_TRUE_MESSAGE(clr != std::string::npos, "hold-expiry #C clear not found");
    const size_t exp = src.find("bDisplayIsOff=true;", tick);
    TEST_ASSERT_TRUE_MESSAGE(exp != std::string::npos && exp < clr && clr - exp < 600, "hold-expiry block not found");
    const std::string code = code_only(src.substr(exp, clr - exp));
    TEST_ASSERT_TRUE_MESSAGE(code.find("node_ping") == std::string::npos, "#C clear gated on the ping configuration");
}

// CLK-PING-01: DisplayOffWait is also set on boards whose DisplayPong draws nothing (T114, T-Echo, Stick V3,
// Tracker, T-Deck family, T-Connect-Pro, e-paper) and on the T-Deck Pro it never resets. The clock may only be
// skipped while a held frame was really drawn: hold running AND frame drawn; an expired hold clears the flag.
void test_held_frame_needs_a_drawn_frame(void)
{
    bool drawn = false;
    TEST_ASSERT_FALSE_MESSAGE(dispClkHeldFrame(5000UL, drawn), "hold without a drawn frame skips the clock");
    drawn = true;
    TEST_ASSERT_TRUE_MESSAGE(dispClkHeldFrame(5000UL, drawn), "drawn frame during its hold not protected");
    TEST_ASSERT_TRUE(drawn);
    TEST_ASSERT_FALSE_MESSAGE(dispClkHeldFrame(0UL, drawn), "expired hold still skips the clock");
    TEST_ASSERT_FALSE_MESSAGE(drawn, "expired hold does not clear the drawn flag");
    TEST_ASSERT_FALSE_MESSAGE(dispClkHeldFrame(5000UL, drawn), "next undrawn hold skips the clock after an old frame");
}

// The flag is set exactly where a held frame is drawn: DisplayPong's u8g2 branch and the hold-text frame.
void test_held_frame_flag_set_where_frames_are_drawn(void)
{
    const std::string src = read_repo_file("src/loop_functions.cpp");
    const std::string pong = code_only(func_body(src, "void DisplayPong(char line1[20], char line2[20], char line3[20], char line4[20])\n{"));
    const size_t first = pong.find("u8g2->firstPage();");
    const size_t set = pong.find("bHeldFrameDrawn = true;");
    TEST_ASSERT_TRUE_MESSAGE(first != std::string::npos && set != std::string::npos && set > first, "DisplayPong does not mark its drawn frame");
    const size_t hold = src.find("DisplayOffWait = millis() + (unsigned long)hold_s * 1000UL;");
    TEST_ASSERT_TRUE_MESSAGE(hold != std::string::npos, "hold-text frame not found");
    const size_t hset = src.find("bHeldFrameDrawn = true;", hold);
    TEST_ASSERT_TRUE_MESSAGE(hset != std::string::npos && hset - hold < 200, "hold-text frame does not mark its drawn frame");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_exhaustive_differs_from_old_logic_only_by_gate15);
    RUN_TEST(test_display_track_clock_every_second_page_every_15s);
    RUN_TEST(test_clockline_decides_before_touching_pagetext);
    RUN_TEST(test_clock_pages_draw_time_on_page_change);
    RUN_TEST(test_sendDisplayTime_call_site_ungated);
    RUN_TEST(test_esp32_clock_tick_not_gated_on_ping);
    RUN_TEST(test_esp32_hold_expiry_clear_not_gated_on_ping);
    RUN_TEST(test_gate15_seconds);
    RUN_TEST(test_order_off_before_type0_before_setdisplay);
    RUN_TEST(test_pagehold_does_not_block_clock);
    RUN_TEST(test_reason_strings);
    RUN_TEST(test_f1_message_page_never_draws_clock);
    RUN_TEST(test_f1_message_header_survives_clock_pass);
    RUN_TEST(test_hold_skips_clock_no_hold_draws);
    RUN_TEST(test_fix_F2_every_second_draws);
    RUN_TEST(test_held_frame_needs_a_drawn_frame);
    RUN_TEST(test_held_frame_flag_set_where_frames_are_drawn);
    return UNITY_END();
}
