// CLK-01: tick arithmetic of Clock::CheckEvent() (src/clock_tick.h).
// Current behaviour passes; F3 fix criteria are TEST_IGNORE until the fix wave.
#include <unity.h>
#include <stdint.h>
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

void test_tick_counts_scheduled_second_only(void)
{
    // Current behaviour (F3): a tick handled 3 s late still counts 1 s.
    uint32_t start = 0, next = clockTickRearm(start);
    TEST_ASSERT_TRUE(clockTickDue(next + 3000, next));
    TEST_ASSERT_EQUAL_UINT32(1u, clockTickSeconds(start, next));
}

void test_current_3600_late_ticks_lose_time(void)
{
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
    TEST_IGNORE_MESSAGE("CLK-01 fix wave: F3");
    // 3600 ticks each 40 ms late must give real elapsed seconds (3744 s), not 3600.
    uint32_t now = 0, start = 0, next = clockTickRearm(0), secs = 0;
    for (int i = 0; i < 3600; i++)
    {
        now = next + 40;
        TEST_ASSERT_TRUE(clockTickDue(now, next));
        secs += (now - start) / 1000;  // fix: real elapsed (remainder carry omitted here)
        start = now;
        next  = clockTickRearm(start);
    }
    TEST_ASSERT_UINT32_WITHIN(2u, 3744u, secs);
}

void test_fix_F3_3s_block_counts_3s(void)
{
    TEST_IGNORE_MESSAGE("CLK-01 fix wave: F3");
    uint32_t start = 0, next = clockTickRearm(0);
    uint32_t now = next + 2000;  // loop blocked: handled at 3000 ms
    TEST_ASSERT_EQUAL_UINT32(3u, (now - start) / 1000);
    TEST_ASSERT_EQUAL_UINT32(3u, clockTickSeconds(start, now));  // helper must account real time
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
    RUN_TEST(test_tick_counts_scheduled_second_only);
    RUN_TEST(test_current_3600_late_ticks_lose_time);
    RUN_TEST(test_fix_F3_3600_late_ticks_count_3600s);
    RUN_TEST(test_fix_F3_3s_block_counts_3s);
    RUN_TEST(test_instrument_late_and_lost);
    RUN_TEST(test_delta_clamp_ms);
    return UNITY_END();
}
