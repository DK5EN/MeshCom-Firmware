// CLK-01: tick arithmetic of Clock::CheckEvent() (src/clock_tick.h).
// F3 fix (W2): real elapsed seconds counted, remainder carried (clockTickAdvance).
#include <unity.h>
#include <stdint.h>
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "clock_tick.h"

void setUp() {}
void tearDown() {}

// Old inline expressions, copied verbatim from the pre-refactor clock.cpp.
static bool     oldDue(uint32_t now, uint32_t next) { return (int32_t)(now - next) > 0; }
static uint32_t oldDiff(uint32_t start, uint32_t next) { return ((next - start) / 1000); }
static uint32_t oldRearm(uint32_t start) { return start + 1000; }

static const uint32_t EDGE[] = {0u, 1u, 999u, 1000u, 1001u, 123456u, 0x7FFFFFFFu, 0x80000000u,
                                0xFFFFFC00u, 0xFFFFFF00u, 0xFFFFFFFFu};

void test_helpers_match_old_inline_expressions(void)
{
    for (uint32_t a : EDGE)
        for (uint32_t b : EDGE)
        {
            TEST_ASSERT_EQUAL(oldDue(a, b), clockTickDue(a, b));
            TEST_ASSERT_EQUAL_UINT32(oldDiff(a, b), clockTickSeconds(a, b));
        }
    for (uint32_t a : EDGE)
        TEST_ASSERT_EQUAL_UINT32(oldRearm(a), clockTickRearm(a));
}

void test_tick_due_after_deadline(void)
{
    TEST_ASSERT_FALSE(clockTickDue(1000, 1000));
    TEST_ASSERT_TRUE(clockTickDue(1001, 1000));
    TEST_ASSERT_FALSE(clockTickDue(999, 1000));
}

void test_tick_due_across_millis_rollover(void)
{
    uint32_t start = 0xFFFFFE00u;           // 512 ms before wrap
    uint32_t next  = clockTickRearm(start); // wraps to 488
    TEST_ASSERT_EQUAL_UINT32(488u, next);
    TEST_ASSERT_FALSE(clockTickDue(0xFFFFFFF0u, next));
    TEST_ASSERT_TRUE(clockTickDue(489u, next));
    TEST_ASSERT_EQUAL_UINT32(1u, clockTickSeconds(start, next));
}

void test_prefix_model_counts_scheduled_second_only(void)
{
    // PRE-FIX MODEL of defect F3 (not the current behaviour, see test_fix_F3_*): the old scheme
    // counted the scheduled second only, so a tick handled 3 s late still counted 1 s.
    uint32_t start = 0, next = clockTickRearm(start);
    TEST_ASSERT_TRUE(clockTickDue(next + 3000, next));
    TEST_ASSERT_EQUAL_UINT32(1u, clockTickSeconds(start, next));
}

void test_prefix_model_3600_late_ticks_lose_time(void)
{
    // PRE-FIX MODEL of defect F3 (old scheme inline: one second per tick, start = now): documents
    // why the clock ran slow; the fixed path is test_fix_F3_3600_late_ticks_count_3600s.
    uint32_t now = 0, start = 0, next = clockTickRearm(0);
    uint32_t secs = 0;
    uint32_t ticks = 0;
    while (ticks < 3600)
    {
        now += 10;  // loop pass granularity
        if (clockTickDue(now, next))
        {
            secs += clockTickSeconds(start, next);
            start = now;
            next  = clockTickRearm(start);
            ticks++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3600u, secs);
    TEST_ASSERT_TRUE(now > 3600u * 1000u);  // real elapsed exceeds counted: clock runs slow
}

void test_fix_F3_3600_late_ticks_count_3600s(void)
{
    // Loop passes every 40 ms, ticks handled up to 40 ms late: the counted seconds must follow
    // real elapsed time (remainder carried), not drift behind it.
    uint32_t now = 0, start = 0, next = clockTickRearm(0), secs = 0;
    while (now < 3744u * 1000u)
    {
        now += 40;
        if (clockTickDue(now, next))
        {
            uint32_t d = clockTickSeconds(start, now);
            secs += d;
            start = clockTickAdvance(start, d);
            next  = clockTickRearm(start);
        }
    }
    TEST_ASSERT_UINT32_WITHIN(1u, 3744u, secs);
}

void test_fix_F3_3s_block_counts_3s(void)
{
    uint32_t start = 0, next = clockTickRearm(0);
    uint32_t now = next + 2000;  // loop blocked: handled at 3000 ms
    TEST_ASSERT_EQUAL_UINT32(3u, (now - start) / 1000);
    TEST_ASSERT_EQUAL_UINT32(3u, clockTickSeconds(start, now));  // helper must account real time
    TEST_ASSERT_EQUAL_UINT32(3000u, clockTickAdvance(start, 3u));
    TEST_ASSERT_EQUAL_UINT32(4000u, clockTickRearm(clockTickAdvance(start, 3u)));
}

// clockTickStep() is the call Clock::CheckEvent() makes: seconds out, start/next moved.
void test_step_late_tick_carries_remainder(void)
{
    uint32_t start = 0, next = clockTickRearm(0);
    const uint32_t secs = clockTickStep(&start, &next, 1040u);   // handled 40 ms late
    TEST_ASSERT_EQUAL_UINT32(1u, secs);
    TEST_ASSERT_EQUAL_UINT32(1000u, start);    // advanced by exactly 1 s, 40 ms stay as lateness
    TEST_ASSERT_EQUAL_UINT32(2000u, next);
    TEST_ASSERT_FALSE(clockTickDue(2000u, next));
    TEST_ASSERT_TRUE(clockTickDue(2001u, next));
}

void test_step_block_across_millis_wrap_counts_70s(void)
{
    uint32_t start = 0xFFFFFE00u;                // 512 ms before the millis() wrap
    uint32_t next  = clockTickRearm(start);
    const uint32_t now = start + 70u * 1000u + 250u;   // loop blocked 70.25 s, now wrapped
    TEST_ASSERT_TRUE(now < start);               // the block really crosses the wrap
    TEST_ASSERT_TRUE(clockTickDue(now, next));
    const uint32_t secs = clockTickStep(&start, &next, now);
    TEST_ASSERT_EQUAL_UINT32(70u, secs);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFE00u + 70000u, start);
    TEST_ASSERT_EQUAL_UINT32(250u, now - start);       // remainder carried across the wrap
    TEST_ASSERT_EQUAL_UINT32(start + 1000u, next);
    TEST_ASSERT_FALSE(clockTickDue(now, next));        // next deadline is 750 ms ahead
}

void test_step_3600_late_ticks_over_wrap_count_real_seconds(void)
{
    // same loop as the F3 test, driven through clockTickStep() and across the millis() wrap
    uint32_t start = 0xFFFFE000u, next = clockTickRearm(start), now = start, secs = 0;
    const uint32_t t0 = start;
    while ((uint32_t)(now - t0) < 3744u * 1000u)
    {
        now += 40;
        if (clockTickDue(now, next))
            secs += clockTickStep(&start, &next, now);
    }
    TEST_ASSERT_UINT32_WITHIN(1u, 3744u, secs);
}

// Source pin: the real call site (Clock::CheckEvent in src/clock.cpp) goes through clockTickStep(),
// so the tests above exercise the code that runs on the device and not a copy of it.
static std::string read_repo_file(const char *rel)
{
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        std::ifstream a((dir + "/test/test_clock_tick/test_clock_tick.cpp").c_str()), b((dir + "/platformio.ini").c_str());
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

void test_checkevent_uses_clockTickStep(void)
{
    const std::string src = read_repo_file("src/clock.cpp");
    const size_t start = src.find("Clock::EEvent Clock::CheckEvent()");
    TEST_ASSERT_TRUE_MESSAGE(start != std::string::npos, "CheckEvent() not found");
    const size_t end = src.find("\n//----", start);
    const std::string body = src.substr(start, end == std::string::npos ? std::string::npos : end - start);
    TEST_ASSERT_TRUE_MESSAGE(body.find("clockTickStep(&u32Start_m, &u32Next_m") != std::string::npos,
                             "CheckEvent() no longer calls clockTickStep()");
    TEST_ASSERT_TRUE_MESSAGE(body.find("clockTickAdvance(") == std::string::npos,
                             "CheckEvent() computes the tick arithmetic inline again");
}

// CLK-01 W0 instrument helpers
static void test_instrument_late_and_lost(void)
{
    TEST_ASSERT_EQUAL_UINT32(40u, clockTickLateMs(1040u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(5u, clockTickLateMs(3u, 0xFFFFFFFEu));   // across the millis rollover
    TEST_ASSERT_EQUAL_INT32(0, clockLostMs(1000u, 0u, 1u));
    TEST_ASSERT_EQUAL_INT32(40, clockLostMs(1040u, 0u, 1u));
    // 100 ticks, each handled 40 ms late with the old scheme: 100 s counted, 104 s elapsed
    TEST_ASSERT_EQUAL_INT32(4000, clockLostMs(104000u, 0u, 100u));
    TEST_ASSERT_EQUAL_INT32(0, clockLostMs(984u, 0xFFFFFFF0u, 1u));   // set before the rollover: 1000 ms elapsed
}

static void test_delta_clamp_ms(void)
{
    TEST_ASSERT_EQUAL_INT32(0, clockDeltaClampMs(0));
    TEST_ASSERT_EQUAL_INT32(-1500, clockDeltaClampMs(-1500));
    TEST_ASSERT_EQUAL_INT32(2147483647, clockDeltaClampMs((int64_t)2147483647));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, clockDeltaClampMs((int64_t)-2147483647 - 1));
    // first set after boot: new epoch ~1.78e9 s minus old epoch 0 = ~1.78e12 ms
    TEST_ASSERT_EQUAL_INT32(2147483647, clockDeltaClampMs((int64_t)1780000000 * 1000));
    TEST_ASSERT_EQUAL_INT32(2147483647, clockDeltaClampMs((int64_t)2147483648LL));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, clockDeltaClampMs(-(int64_t)1780000000 * 1000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_helpers_match_old_inline_expressions);
    RUN_TEST(test_tick_due_after_deadline);
    RUN_TEST(test_tick_due_across_millis_rollover);
    RUN_TEST(test_prefix_model_counts_scheduled_second_only);
    RUN_TEST(test_prefix_model_3600_late_ticks_lose_time);
    RUN_TEST(test_fix_F3_3600_late_ticks_count_3600s);
    RUN_TEST(test_fix_F3_3s_block_counts_3s);
    RUN_TEST(test_step_late_tick_carries_remainder);
    RUN_TEST(test_step_block_across_millis_wrap_counts_70s);
    RUN_TEST(test_step_3600_late_ticks_over_wrap_count_real_seconds);
    RUN_TEST(test_checkevent_uses_clockTickStep);
    RUN_TEST(test_instrument_late_and_lost);
    RUN_TEST(test_delta_clamp_ms);
    return UNITY_END();
}
