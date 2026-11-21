// POSINFO-NEXT-01: clamped NEXT countdown (src/posinfo_next.h), six display sites.
//   pio test -e native -f test_posinfo_next
#include <unity.h>
#include <stdint.h>
#include <fstream>
#include <sstream>
#include <string>

#include <posinfo_next.h>

void setUp(void) {}
void tearDown(void) {}

// old unsigned expression, uint32_t to model the boards (host unsigned long is 64 bit)
static int legacyNextExpr(uint32_t now, uint32_t timer, uint32_t interval)
{
    return (int)(((timer + (interval * 1000u)) - now) / 1000);
}

static void test_overdue_by_one_ms_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, posinfoNextSeconds(120001, 100000, 20));
}

static void test_overdue_30s_window_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, posinfoNextSeconds(150000, 100000, 20));
}

static void test_normal_countdown(void)
{
    TEST_ASSERT_EQUAL_INT(15, posinfoNextSeconds(105000, 100000, 20));
    TEST_ASSERT_EQUAL_INT(0, posinfoNextSeconds(120000, 100000, 20));
    TEST_ASSERT_EQUAL_INT(1800, posinfoNextSeconds(100000, 100000, 1800));
}

static void test_millis_rollover(void)
{
    TEST_ASSERT_EQUAL_INT(18, posinfoNextSeconds(1000, 0xFFFFFF00u, 20));
    TEST_ASSERT_EQUAL_INT(0, posinfoNextSeconds(20000, 0xFFFFFF00u, 20));
}

static void test_legacy_expression_wraps(void)
{
    TEST_ASSERT_EQUAL_INT(4294967, legacyNextExpr(120001, 100000, 20));
}

static std::string slurp(const char *rel)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_posinfo_next/test_posinfo_next.cpp";
    std::string root = file.substr(0, file.size() - suffix.size());
    std::ifstream f((root + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_all_six_sites_use_the_helper(void)
{
    const char *files[] = {"src/command_functions.cpp", "src/web_functions/web_functions.cpp", "src/t-deck/lv_obj_functions.cpp",
                           "src/loop_functions.cpp", "src/t-deck-pro/ui_deckpro.cpp"};
    for (const char *rel : files) {
        std::string s = slurp(rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("posinfoNextSeconds(") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("posinfo_interval * 1000)) - millis()") == std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("posinfo_interval*1000)) - millis()") == std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("- (int)posinfo_timer)") == std::string::npos, rel);
    }
    // command_functions.cpp carries two sites (--pos and JSON NEXT)
    std::string c = slurp("src/command_functions.cpp");
    size_t n = 0, p = 0;
    while ((p = c.find("posinfoNextSeconds(", p)) != std::string::npos) { n++; p++; }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2, n, "command_functions.cpp NEXT sites");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_overdue_by_one_ms_is_zero);
    RUN_TEST(test_overdue_30s_window_is_zero);
    RUN_TEST(test_normal_countdown);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_legacy_expression_wraps);
    RUN_TEST(test_all_six_sites_use_the_helper);
    return UNITY_END();
}
