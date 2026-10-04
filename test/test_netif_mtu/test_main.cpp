// Host-Test fuer src/esp32/netif_mtu.h (NMTU-01, #1190): MTU-Clamp und die
// Lage der Hooks (nicht auf AP_START, vor jedem web server begin). Der
// lwIP-Teil ist unter NATIVE_BUILD ausgeblendet.
//
//   pio test -e native_netif_mtu

#include <unity.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>

#include "esp32/netif_mtu.h"

using namespace netif_mtu;

void setUp(void) {}
void tearDown(void) {}

static void test_clamp_in_range_passes(void)
{
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(1280));
    TEST_ASSERT_EQUAL_UINT16(1400, clampMtu(1400));
    TEST_ASSERT_EQUAL_UINT16(1492, clampMtu(1492));
    TEST_ASSERT_EQUAL_UINT16(1500, clampMtu(1500));
}

static void test_clamp_out_of_range_falls_back_to_1280(void)
{
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(0));
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(-1));
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(1279));
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(1501));
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(65535));
    TEST_ASSERT_EQUAL_UINT16(1280, clampMtu(70000));
}

// Repo root = first parent of cwd that holds platformio.ini (pio test runs from it).
static std::string read_repo_file(const char *rel)
{
    char cwd_buf[1024];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        std::ifstream probe((dir + "/platformio.ini").c_str());
        if (probe.good())
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The body of `case <ev>:` up to the next case/default.
static std::string case_body(const std::string &src, const char *ev)
{
    const size_t at = src.find(std::string("case ") + ev + ":");
    if (at == std::string::npos)
        return std::string();
    size_t end = src.find("case ", at + 5);
    const size_t def = src.find("default:", at);
    if (def != std::string::npos && (end == std::string::npos || def < end))
        end = def;
    return src.substr(at, end - at);
}

// Advisor finding on NMTU-01: an apply on AP_START can land before esp_netif's
// netif_add() resets mtu to 1500, and nothing re-applied it. The AP path must
// apply on STACONNECTED, never rely on AP_START, and both web servers must be
// preceded by an apply.
static void test_hooks_avoid_ap_start_and_precede_web_server(void)
{
    const char *files[] = {"src/udp_functions.cpp", "src/safeboot/main.cpp"};
    for (const char *rel : files)
    {
        const std::string src = read_repo_file(rel);
        TEST_ASSERT_TRUE_MESSAGE(case_body(src, "ARDUINO_EVENT_WIFI_AP_START").find("applyConfiguredMtu") == std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(case_body(src, "ARDUINO_EVENT_WIFI_AP_STACONNECTED").find("applyConfiguredMtu") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(case_body(src, "ARDUINO_EVENT_WIFI_STA_GOT_IP").find("applyConfiguredMtu") != std::string::npos, rel);
    }
    const std::string sb = read_repo_file("src/safeboot/main.cpp");
    const size_t sbBegin = sb.find("webServer.begin();");
    TEST_ASSERT_TRUE(sbBegin != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(sb.rfind("applyConfiguredMtu", sbBegin) > sb.rfind(";", sbBegin - 1) - 200, "safeboot: no apply right before webServer.begin()");
    const std::string web = read_repo_file("src/web_functions/web_functions.cpp");
    const size_t wBegin = web.find("    web_server.begin();\n\n\n#else");
    TEST_ASSERT_TRUE(wBegin != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(web.substr(wBegin - 120, 120).find("applyConfiguredMtu") != std::string::npos, "app: no apply right before web_server.begin()");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_clamp_in_range_passes);
    RUN_TEST(test_clamp_out_of_range_falls_back_to_1280);
    RUN_TEST(test_hooks_avoid_ap_start_and_precede_web_server);
    return UNITY_END();
}
