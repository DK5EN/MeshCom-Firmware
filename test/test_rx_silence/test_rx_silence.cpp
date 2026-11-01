// Native tests for the RX-silence tracker (src/rx_silence.h, LRF W1c).
//   pio test -e native -f test_rx_silence

#include <unity.h>

#include <rx_silence.h>

void setUp(void)    {}
void tearDown(void) {}

static RxSilenceTracker T;

void test_no_silence_before_threshold(void)
{
    T.init(1000);
    TEST_ASSERT_FALSE(T.tick(1000 + RX_SILENCE_MS - 1));
    TEST_ASSERT_FALSE(T.silent);
    TEST_ASSERT_EQUAL_UINT32(0, T.silences);
}

void test_start_at_threshold(void)
{
    T.init(1000);
    uint32_t t = 0;
    TEST_ASSERT_TRUE(T.tick(1000 + RX_SILENCE_MS, &t));
    TEST_ASSERT_TRUE(T.silent);
    TEST_ASSERT_EQUAL_UINT32(60, t);
    TEST_ASSERT_EQUAL_UINT32(1, T.silences);
}

void test_snapshots_every_interval(void)
{
    T.init(0);
    uint32_t t = 0;
    TEST_ASSERT_TRUE(T.tick(60000, &t));
    TEST_ASSERT_FALSE(T.tick(60001));
    TEST_ASSERT_FALSE(T.tick(119999));
    TEST_ASSERT_TRUE(T.tick(120000, &t));
    TEST_ASSERT_EQUAL_UINT32(120, t);
    TEST_ASSERT_FALSE(T.tick(120500));
    TEST_ASSERT_TRUE(T.tick(180000, &t));
    TEST_ASSERT_EQUAL_UINT32(1, T.silences);
}

void test_late_tick_gives_one_snapshot(void)
{
    T.init(0);
    TEST_ASSERT_TRUE(T.tick(250000));
    TEST_ASSERT_FALSE(T.tick(250100));
    TEST_ASSERT_TRUE(T.tick(300000));
}

void test_rx_event_resets_clock_without_silence(void)
{
    T.init(0);
    TEST_ASSERT_FALSE(T.noteRx(59000));
    TEST_ASSERT_FALSE(T.tick(60000 + 58000));
    TEST_ASSERT_TRUE(T.tick(59000 + 60000));
    TEST_ASSERT_EQUAL_UINT32(1, T.silences);
}

static void run_end(uint8_t kind, bool with_action, uint32_t action_gap, uint8_t expect)
{
    T.init(0);
    T.tick(60000);
    T.tick(120000);
    uint32_t end = 130000;
    if (with_action)
        T.noteAction(kind, end - action_gap);
    TEST_ASSERT_TRUE(T.noteRx(end));
    TEST_ASSERT_EQUAL_UINT32(130, T.end_dur_s);
    TEST_ASSERT_EQUAL_UINT8(expect, T.end_reason);
    TEST_ASSERT_FALSE(T.silent);
}

void test_end_by_rx(void)       { run_end(0, false, 0, RXS_BY_RX); }
void test_end_by_cad(void)      { run_end(RXS_BY_CAD, true, 1500, RXS_BY_CAD); }
void test_end_by_tx(void)       { run_end(RXS_BY_TX, true, 400, RXS_BY_TX); }
void test_end_by_watchdog(void) { run_end(RXS_BY_WATCHDOG, true, RX_SILENCE_END_WINDOW_MS, RXS_BY_WATCHDOG); }
void test_action_outside_window_is_rx(void)
{
    run_end(RXS_BY_CAD, true, RX_SILENCE_END_WINDOW_MS + 1, RXS_BY_RX);
}

void test_latest_action_wins(void)
{
    T.init(0);
    T.tick(60000);
    T.noteAction(RXS_BY_CAD, 129000);
    T.noteAction(RXS_BY_WATCHDOG, 129800);
    TEST_ASSERT_TRUE(T.noteRx(130000));
    TEST_ASSERT_EQUAL_UINT8(RXS_BY_WATCHDOG, T.end_reason);
}

void test_stale_action_from_before_prior_event_ignored(void)
{
    T.init(0);
    T.noteAction(RXS_BY_TX, 1000);
    T.noteRx(2000);            // consumes the action
    T.tick(62000);
    TEST_ASSERT_TRUE(T.noteRx(62500));
    TEST_ASSERT_EQUAL_UINT8(RXS_BY_RX, T.end_reason);
}

void test_counters(void)
{
    T.init(0);
    T.tick(60000);                         // silence 1
    T.noteRx(100000);                      // 100 s
    T.tick(160000);                        // silence 2 (60 s after 100000)
    T.noteRx(400000);                      // 300 s
    T.tick(460000);                        // silence 3
    T.noteRx(480000);                      // 80 s
    TEST_ASSERT_EQUAL_UINT32(3, T.silences);
    TEST_ASSERT_EQUAL_UINT32(300, T.longest_s);
    TEST_ASSERT_EQUAL_UINT32(480, T.total_s);
}

void test_running_silence_in_now_counters(void)
{
    T.init(0);
    T.tick(60000);
    T.tick(90000);
    TEST_ASSERT_EQUAL_UINT32(90, T.longestNow(90000));
    TEST_ASSERT_EQUAL_UINT32(90, T.totalNow(90000));
    TEST_ASSERT_EQUAL_UINT32(0, T.total_s);
}

void test_millis_wraparound(void)
{
    uint32_t start = 0xFFFFFFFFUL - 30000UL;     // 30 s before wrap
    T.init(start);
    TEST_ASSERT_FALSE(T.tick(start + 59999UL));  // wraps past 0
    uint32_t t = 0;
    TEST_ASSERT_TRUE(T.tick(start + 60000UL, &t));
    TEST_ASSERT_EQUAL_UINT32(60, t);
    TEST_ASSERT_TRUE(T.tick(start + 120000UL, &t));
    T.noteAction(RXS_BY_TX, start + 125000UL);
    TEST_ASSERT_TRUE(T.noteRx(start + 126000UL));
    TEST_ASSERT_EQUAL_UINT32(126, T.end_dur_s);
    TEST_ASSERT_EQUAL_UINT8(RXS_BY_TX, T.end_reason);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_silence_before_threshold);
    RUN_TEST(test_start_at_threshold);
    RUN_TEST(test_snapshots_every_interval);
    RUN_TEST(test_late_tick_gives_one_snapshot);
    RUN_TEST(test_rx_event_resets_clock_without_silence);
    RUN_TEST(test_end_by_rx);
    RUN_TEST(test_end_by_cad);
    RUN_TEST(test_end_by_tx);
    RUN_TEST(test_end_by_watchdog);
    RUN_TEST(test_action_outside_window_is_rx);
    RUN_TEST(test_latest_action_wins);
    RUN_TEST(test_stale_action_from_before_prior_event_ignored);
    RUN_TEST(test_counters);
    RUN_TEST(test_running_silence_in_now_counters);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
