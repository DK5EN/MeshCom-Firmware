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
        TEST_ASSERT_TRUE_MESSAGE(s.find("PongFail(meshcom_settings.node_pingcall)") != std::string::npos, rel);
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
    return UNITY_END();
}
