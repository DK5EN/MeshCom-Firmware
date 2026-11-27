// FORMAL-02 9h (B9): src/reboot_defer.h (pure decision + arm rule) and the source pins that keep the config upload
// from blocking in delay() and the two main loops servicing the deferred reboot.
#include <string>
#include <unity.h>
#include "reboot_defer.h"
#include "../support/repo_file.h"

void setUp(void) {}
void tearDown(void) {}

static void test_due_rules(void)
{
    TEST_ASSERT_FALSE(rebootDeferDue(false, 100, 100));    // disarmed never fires
    TEST_ASSERT_FALSE(rebootDeferDue(false, 100, 5000));
    TEST_ASSERT_FALSE(rebootDeferDue(true, 100, 99));      // before the deadline
    TEST_ASSERT_TRUE(rebootDeferDue(true, 100, 100));      // at the deadline
    TEST_ASSERT_TRUE(rebootDeferDue(true, 100, 101));
    TEST_ASSERT_TRUE(rebootDeferDue(true, 0, 0));
}

static void test_wrap(void)
{
    // armed at 0xFFFFFF00 for 500 ms: deadline wraps to 0xF4
    const uint32_t at = 0xFFFFFF00u + 500u;
    TEST_ASSERT_EQUAL_UINT32(0x1F4u - 0x100u, at);
    TEST_ASSERT_FALSE(rebootDeferDue(true, at, 0xFFFFFF00u));
    TEST_ASSERT_FALSE(rebootDeferDue(true, at, 0xFFFFFFFFu));
    TEST_ASSERT_FALSE(rebootDeferDue(true, at, at - 1));
    TEST_ASSERT_TRUE(rebootDeferDue(true, at, at));
    TEST_ASSERT_TRUE(rebootDeferDue(true, at, at + 1000));
    // deadline right at the wrap edge
    TEST_ASSERT_FALSE(rebootDeferDue(true, 0xFFFFFFFFu, 0xFFFFFFFEu));
    TEST_ASSERT_TRUE(rebootDeferDue(true, 0xFFFFFFFFu, 0u));
}

static void test_second_arm_keeps_the_earlier_deadline(void)
{
    RebootDeferState s = {false, 0};
    rebootDeferArmState(s, 1000, 500);
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_UINT32(1500, s.at_ms);
    rebootDeferArmState(s, 1200, 5000); // later deadline: ignored
    TEST_ASSERT_EQUAL_UINT32(1500, s.at_ms);
    rebootDeferArmState(s, 1200, 100); // earlier deadline: taken
    TEST_ASSERT_EQUAL_UINT32(1300, s.at_ms);
    // across the wrap
    RebootDeferState w = {false, 0};
    rebootDeferArmState(w, 0xFFFFFFF0u, 100); // deadline 0x54
    rebootDeferArmState(w, 0xFFFFFFF8u, 20); // deadline 0x0C: earlier across the wrap
    TEST_ASSERT_EQUAL_UINT32(0x0Cu, w.at_ms);
    rebootDeferArmState(w, 0xFFFFFFF8u, 5000);
    TEST_ASSERT_EQUAL_UINT32(0x0Cu, w.at_ms);
}

static std::string fnBody(const std::string &src, const char *sig)
{
    const size_t a = src.find(sig);
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, sig);
    const size_t b = src.find("\n}\n", a);
    TEST_ASSERT_TRUE_MESSAGE(b != std::string::npos, sig);
    return src.substr(a, b - a);
}

static void test_config_upload_defers_the_reboot(void)
{
    const std::string wc = repo_slurp("src/web_functions/web_config.cpp");
    const std::string up = fnBody(wc, "void sub_config_upload(");
    TEST_ASSERT_TRUE_MESSAGE(up.find("delay(") == std::string::npos, "sub_config_upload blocks in delay()");
    TEST_ASSERT_TRUE_MESSAGE(up.find("rebootDeferArm(") != std::string::npos, "sub_config_upload does not arm the deferred reboot");
    TEST_ASSERT_TRUE_MESSAGE(up.find("ESP.restart()") == std::string::npos && up.find("NVIC_SystemReset") == std::string::npos,
                             "sub_config_upload restarts inside the request");
    TEST_ASSERT_TRUE_MESSAGE(up.find("web_client.stop();") < up.find("rebootDeferArm("), "the answer must be closed before the reboot is armed");
}

static void test_both_loops_service_the_deferred_reboot(void)
{
    const std::string e = repo_slurp("src/esp32/esp32_main.cpp");
    const std::string n = repo_slurp("src/nrf52/nrf52_main.cpp");
    const std::string el = fnBody(e, "void esp32loop()");
    const std::string nl = fnBody(n, "void nrf52loop()");
    TEST_ASSERT_TRUE_MESSAGE(el.find("rebootDeferService();") != std::string::npos, "esp32loop does not service the deferred reboot");
    TEST_ASSERT_TRUE_MESSAGE(nl.find("rebootDeferService();") != std::string::npos, "nrf52loop does not service the deferred reboot");
    // before any early return of the loop: within the first 40 lines
    const size_t ep = el.find("rebootDeferService();"), np = nl.find("rebootDeferService();");
    if (ep != std::string::npos)
        TEST_ASSERT_TRUE_MESSAGE(el.substr(0, ep).find("return;") == std::string::npos, "esp32loop returns before rebootDeferService()");
    if (np != std::string::npos)
        TEST_ASSERT_TRUE_MESSAGE(nl.substr(0, np).find("return;") == std::string::npos, "nrf52loop returns before rebootDeferService()");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_due_rules);
    RUN_TEST(test_wrap);
    RUN_TEST(test_second_arm_keeps_the_earlier_deadline);
    RUN_TEST(test_config_upload_defers_the_reboot);
    RUN_TEST(test_both_loops_service_the_deferred_reboot);
    return UNITY_END();
}
