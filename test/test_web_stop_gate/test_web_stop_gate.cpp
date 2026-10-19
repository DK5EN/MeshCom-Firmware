// Native Testsuite fuer webStopShouldAct() -- das Gate in stopWebserver().
//
// Regressionsfall WEB-STOP-01: ohne IP rief startWebserver() in jedem
// Loop-Durchlauf stopWebserver() auf; MDNS.end(), web_server.stop() und die
// Zeile "WEBServer stopped" kamen im Loop-Takt (T-Deck Plus, WLAN aus).
//
//   pio test -e native -f test_web_stop_gate

#include <unity.h>

#include <web_server_gate.h>

// Models stopWebserver(): counts the actions (MDNS.end + stop + log line).
static int g_actions;
static bool g_running;

static void modelStopWebserver(void)
{
    if (webStopShouldAct(g_running))
        g_actions++;
    g_running = false;
}

void setUp(void)
{
    g_actions = 0;
    g_running = false;
}

void tearDown(void) {}

static void test_no_ip_loop_never_acts(void)
{
    // Regression WEB-STOP-01: server not running, no IP, 1000 loop passes.
    for (int i = 0; i < 1000; i++)
        modelStopWebserver();
    TEST_ASSERT_EQUAL_INT(0, g_actions);
}

static void test_running_server_stops_exactly_once(void)
{
    g_running = true;
    for (int i = 0; i < 1000; i++)
        modelStopWebserver();
    TEST_ASSERT_EQUAL_INT(1, g_actions);
}

static void test_gate_values(void)
{
    TEST_ASSERT_TRUE(webStopShouldAct(true));
    TEST_ASSERT_FALSE(webStopShouldAct(false));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_ip_loop_never_acts);
    RUN_TEST(test_running_server_stops_exactly_once);
    RUN_TEST(test_gate_values);
    return UNITY_END();
}
