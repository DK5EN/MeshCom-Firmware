// PING-NRF-01: ping budget helpers (src/ping_budget.h) shared by esp32_main.cpp and nrf52_main.cpp.
//   pio test -e native -f test_ping_budget
#include <unity.h>
#include <fstream>
#include <sstream>
#include <string>

#include <ping_budget.h>

void setUp(void) {}
void tearDown(void) {}

static void test_closed_at_count_zero(void)
{
    TEST_ASSERT_FALSE(pingBudgetOpen(60, "DK5EN-1", 0));
    TEST_ASSERT_FALSE(pingBudgetOpen(60, "DK5EN-1", -1));
}

static void test_budget_open_needs_call_and_pingtime_over_29(void)
{
    TEST_ASSERT_TRUE(pingBudgetOpen(30, "DK5EN-1", 1));
    TEST_ASSERT_FALSE(pingBudgetOpen(29, "DK5EN-1", 5));
    TEST_ASSERT_FALSE(pingBudgetOpen(0, "DK5EN-1", 5));
    TEST_ASSERT_FALSE(pingBudgetOpen(60, "", 5));
    TEST_ASSERT_FALSE(pingBudgetOpen(60, 0, 5));
}

static void test_ping_start_then_five_sends_then_closed(void)
{
    int count = 5; // --ping start: node_pingcount = PING_MAX
    int sends = 0;
    while (pingBudgetOpen(60, "DK5EN-90", count) && sends < 20) {
        sends++;
        count = pingCountAfterSend(count, false, false);
    }
    TEST_ASSERT_EQUAL_INT(5, sends);
    TEST_ASSERT_EQUAL_INT(0, count);
    TEST_ASSERT_FALSE(pingBudgetOpen(60, "DK5EN-90", count));
}

static void test_track_and_invalid_do_not_decrement(void)
{
    TEST_ASSERT_EQUAL_INT(3, pingCountAfterSend(3, true, false));
    TEST_ASSERT_EQUAL_INT(3, pingCountAfterSend(3, false, true));
    TEST_ASSERT_EQUAL_INT(2, pingCountAfterSend(3, false, false)); // QUEUED / RING_REFUSED
}

static std::string slurp(const char *rel)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_ping_budget/test_ping_budget.cpp";
    std::string root = file.substr(0, file.size() - suffix.size());
    std::ifstream f((root + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_both_loops_use_the_helpers(void)
{
    const char *files[] = {"src/esp32/esp32_main.cpp", "src/nrf52/nrf52_main.cpp"};
    for (const char *rel : files) {
        std::string s = slurp(rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("#include \"ping_budget.h\"") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("pingBudgetOpen(") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("pingCountAfterSend(") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("pingFailCheckDue(") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("pingFinalArmAfterSend(") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("PongFail(meshcom_settings.node_pingcall)") != std::string::npos, rel);
    }
}

// LAST-PONG-01: the last ping of a run gets its fail verdict one interval after the last send.
struct PingSim {
    int count; bool waiting; bool armed; int fails;
    void send() { bool had = pingBudgetOpen(60, "X", count); if (!had) return;
        if (waiting) fails++;            // verdict for the previous ping (in-budget block)
        armed = false;
        int before = count; count = pingCountAfterSend(count, false, false);
        waiting = true; armed = pingFinalArmAfterSend(before, count, true); }
    void tick() {                        // interval elapsed
        if (pingBudgetOpen(60, "X", count)) { send(); return; }
        if (pingFailCheckDue(armed, false, true)) { armed = false; if (waiting) fails++; waiting = false; }
    }
};

static void test_last_ping_without_pong_fails_exactly_once(void)
{
    PingSim s = {3, false, false, 0};
    s.tick(); s.tick(); s.tick();        // N sends
    TEST_ASSERT_EQUAL(0, s.count);
    TEST_ASSERT_EQUAL(2, s.fails);       // verdicts for pings 1 and 2 only
    s.tick();                            // final check tick
    TEST_ASSERT_EQUAL(3, s.fails);
    s.tick(); s.tick();
    TEST_ASSERT_EQUAL(3, s.fails);       // nothing more
}

static void test_final_check_not_due_before_last_send_or_when_not_armed(void)
{
    TEST_ASSERT_FALSE(pingFailCheckDue(false, false, true));
    TEST_ASSERT_FALSE(pingFailCheckDue(true, false, false));
    TEST_ASSERT_FALSE(pingFailCheckDue(true, true, true));   // new run opened: no stale verdict
    TEST_ASSERT_TRUE(pingFailCheckDue(true, false, true));
    TEST_ASSERT_FALSE(pingFinalArmAfterSend(3, 2, true));
    TEST_ASSERT_FALSE(pingFinalArmAfterSend(0, 0, true));
    TEST_ASSERT_TRUE(pingFinalArmAfterSend(1, 0, true));
}

static void test_pong_received_no_fail(void)
{
    PingSim s = {1, false, false, 0};
    s.tick();
    s.waiting = false;                   // pong arrived
    s.tick();
    TEST_ASSERT_EQUAL(0, s.fails);
}

static void test_stop_clears_no_fail(void)
{
    PingSim s = {1, false, false, 0};
    s.tick();
    s.waiting = false; s.count = 0;      // --ping stop clears bPingSend (PING-STOP-01)
    s.tick();
    TEST_ASSERT_EQUAL(0, s.fails);
}

/* LAST-PONG-01 advisor item: a refused last send (PING_RING_REFUSED) uses budget but sets no
 * bPingSend; arming on it gave the previous ping a second verdict. Only a queued send arms. */
static void test_refused_last_send_does_not_arm(void)
{
    TEST_ASSERT_FALSE(pingFinalArmAfterSend(1, 0, false));
    TEST_ASSERT_TRUE(pingFinalArmAfterSend(1, 0, true));
}

/* PING-FAIL-REPEAT-01: after a send attempt that queued no new ping (refused ring, TRACK,
 * invalid target) the stale bPingSend is cleared, so one unanswered ping is reported once.
 * The clear sits AFTER sendPing(): clearing before it would let sendPing() draw "SENT" over the
 * "PONG FAILED" frame on OLED boards (sendPing draws only while !bPingSend). The final check
 * arms only for a queued send. */
static void test_fail_verdict_clears_flag_and_arm_needs_queued(void)
{
    const char *files[] = {"src/esp32/esp32_main.cpp", "src/nrf52/nrf52_main.cpp"};
    for (const char *rel : files) {
        std::string s = slurp(rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("pingResult == PING_QUEUED") != std::string::npos, rel);
        const size_t send = s.find("PingResult pingResult = sendPing(");
        TEST_ASSERT_TRUE_MESSAGE(send != std::string::npos, rel);
        const size_t clr = s.find("if(pingResult != PING_QUEUED)", send);
        TEST_ASSERT_TRUE_MESSAGE(clr != std::string::npos && clr - send < 1200, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.substr(clr, 80).find("bPingSend = false;") != std::string::npos, rel);
        // no clear between the in-budget verdict and the new send
        const size_t verdict = s.rfind("PongFail(meshcom_settings.node_pingcall);", send);
        TEST_ASSERT_TRUE_MESSAGE(verdict != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.substr(verdict, send - verdict).find("bPingSend = false;") == std::string::npos, rel);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_closed_at_count_zero);
    RUN_TEST(test_budget_open_needs_call_and_pingtime_over_29);
    RUN_TEST(test_ping_start_then_five_sends_then_closed);
    RUN_TEST(test_track_and_invalid_do_not_decrement);
    RUN_TEST(test_both_loops_use_the_helpers);
    RUN_TEST(test_last_ping_without_pong_fails_exactly_once);
    RUN_TEST(test_final_check_not_due_before_last_send_or_when_not_armed);
    RUN_TEST(test_pong_received_no_fail);
    RUN_TEST(test_stop_clears_no_fail);
    RUN_TEST(test_refused_last_send_does_not_arm);
    RUN_TEST(test_fail_verdict_clears_flag_and_arm_needs_queued);
    return UNITY_END();
}
