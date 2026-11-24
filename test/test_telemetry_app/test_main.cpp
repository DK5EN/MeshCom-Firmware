// TLM-03: the periodic T# timer must serve the node's own telemetry (ID 0) unless the soft-serial app is on.
//
//   pio test -e native_telemetry_app
//
// Before the fix esp32_main.cpp and nrf52_main.cpp called sendTelemetry(SOFTSER_APP_ID) (= 1) every interval;
// with ID 1 sendTelemetry() returns unless node_parm_1 (soft-serial only) is set, so --parm/--unit/--values
// never reached the air. The source pins below fail on that code.

#include <unity.h>
#include "../support/repo_file.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "telemetry_app.h"

void setUp(void) {}
void tearDown(void) {}

static std::string read_repo_file(const char *rel)
{
    return repo_slurp(rel);
}

static int count(const std::string &hay, const std::string &needle)
{
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size()))
        n++;
    return n;
}

static void test_own_telemetry_without_softser(void)
{
    TEST_ASSERT_EQUAL_INT(0, telemetryAppId(false, 1));
}

static void test_softser_app_when_on(void)
{
    TEST_ASSERT_EQUAL_INT(1, telemetryAppId(true, 1));
    TEST_ASSERT_EQUAL_INT(7, telemetryAppId(true, 7));
}

static void pin_timer_call(const char *file)
{
    const std::string src = read_repo_file(file);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count(src, "sendTelemetry(SOFTSER_APP_ID)"),
                                  "periodic timer still passes SOFTSER_APP_ID unconditionally");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count(src, "sendTelemetry(telemetryAppId(bSOFTSERON, SOFTSER_APP_ID))"),
                                  "periodic timer must pick the source via telemetryAppId()");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count(src, "#include \"telemetry_app.h\""), "telemetry_app.h not included");
}

static void test_esp32_timer_uses_helper(void) { pin_timer_call("src/esp32/esp32_main.cpp"); }
static void test_nrf52_timer_uses_helper(void) { pin_timer_call("src/nrf52/nrf52_main.cpp"); }

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_own_telemetry_without_softser);
    RUN_TEST(test_softser_app_when_on);
    RUN_TEST(test_esp32_timer_uses_helper);
    RUN_TEST(test_nrf52_timer_uses_helper);
    return UNITY_END();
}
