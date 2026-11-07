// CLK-01: redraw gate of sendDisplayTime() (src/display_clock_gate.h).
// Current behaviour passes; F1/F2 fix criteria are TEST_IGNORE until the fix wave.
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

// Old inline logic, verbatim order, as oracle.
static DispClkDecision oldLogic(const DispClkInputs &in)
{
    if (!in.hasDisplay) return DispClkDecision::SkipNoDisplay;
    if (in.displayIsOff) return DispClkDecision::SkipOff;
    if (in.displayType == 0) return DispClkDecision::SkipType0;
    if (in.setDisplay) return DispClkDecision::SkipSetDisplay;
    if (in.second == 0 || in.second == 15 || in.second == 30 || in.second == 45 || in.oneButton || in.displayDirty)
        return DispClkDecision::Draw;
    return DispClkDecision::SkipGate15;
}

void test_exhaustive_matches_old_logic(void)
{
    for (int m = 0; m < 128; m++)
        for (int sec = 0; sec < 60; sec++)
            for (int ty = 0; ty < 10; ty += 3)
            {
                DispClkInputs in = base(sec);
                in.hasDisplay = m & 1; in.displayIsOff = m & 2; in.setDisplay = m & 4;
                in.oneButton = m & 8; in.displayDirty = m & 16; in.pageHold = (m & 32) ? 5 : 0;
                in.displayType = ty;
                TEST_ASSERT_TRUE(oldLogic(in) == dispClkDecide(in));
            }
}

void test_gate15_seconds(void)
{
    for (int s = 0; s < 60; s++)
    {
        bool q = (s % 15) == 0;
        TEST_ASSERT_TRUE((dispClkDecide(base(s)) == DispClkDecision::Draw) == q);
    }
}

void test_message_page_skips_type0(void)
{
    DispClkInputs in = base(0);
    in.displayType = 0;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::SkipType0);
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
}

void test_button_and_dirty_force_draw(void)
{
    DispClkInputs in = base(7);
    in.oneButton = true;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::Draw);
    in.oneButton = false; in.displayDirty = true;
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

void test_fix_F1_message_page_draws_at_second_7(void)
{
    TEST_IGNORE_MESSAGE("CLK-01 fix wave: F1");
    DispClkInputs in = base(7);
    in.displayType = 0;
    TEST_ASSERT_TRUE(dispClkDecide(in) == DispClkDecision::Draw);
}

void test_fix_F2_every_second_draws(void)
{
    TEST_IGNORE_MESSAGE("CLK-01 fix wave: F2");
    for (int s = 0; s < 60; s++)
        TEST_ASSERT_TRUE(dispClkDecide(base(s)) == DispClkDecision::Draw);
}

// Source pin: sendDisplayTrack() (track page) keeps its own 15 s gate. The W2 refactor once
// removed it by mistake (redraw every second, full e-paper refresh each tick).
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

void test_display_track_keeps_its_15s_gate(void)
{
    const std::string src = read_repo_file("src/loop_functions.cpp");
    const size_t start = src.find("void sendDisplayTrack()");
    TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos, "sendDisplayTrack() not found");
    const size_t end = src.find("\nvoid ", start + 10);
    const std::string body = src.substr(start, end == std::string::npos ? std::string::npos : end - start);
    TEST_ASSERT_TRUE_MESSAGE(body.find("meshcom_settings.node_date_second == 15") != std::string::npos,
                             "sendDisplayTrack() lost its 15 s redraw gate");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_exhaustive_matches_old_logic);
    RUN_TEST(test_display_track_keeps_its_15s_gate);
    RUN_TEST(test_gate15_seconds);
    RUN_TEST(test_message_page_skips_type0);
    RUN_TEST(test_order_off_before_type0_before_setdisplay);
    RUN_TEST(test_button_and_dirty_force_draw);
    RUN_TEST(test_reason_strings);
    RUN_TEST(test_fix_F1_message_page_draws_at_second_7);
    RUN_TEST(test_fix_F2_every_second_draws);
    return UNITY_END();
}
