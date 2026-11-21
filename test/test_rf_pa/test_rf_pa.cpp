// TBEAM1W-PWR-01 (#1301): boot and runtime path must configure the SX126x PA the same way.
#include <unity.h>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "rf_pa.h"

struct MockRadio {
    int calls = 0;
    int8_t power = 0;
    bool optimize = true;
    int16_t setOutputPower(int8_t p, bool o)
    {
        calls++;
        power = p;
        optimize = o;
        return 0;
    }
};

void setUp(void) {}
void tearDown(void) {}

static void test_helper_passes_optimize_false_by_default(void)
{
    MockRadio r;
    TEST_ASSERT_EQUAL_INT16(0, rfApplyOutputPower(r, 12));
    TEST_ASSERT_EQUAL_INT(1, r.calls);
    TEST_ASSERT_EQUAL_INT8(12, r.power);
    TEST_ASSERT_FALSE(r.optimize);
}

static void test_helper_tdeckpro_keeps_table(void)
{
    MockRadio r;
    rfApplyOutputPower(r, -9, true);
    TEST_ASSERT_EQUAL_INT8(-9, r.power);
    TEST_ASSERT_TRUE(r.optimize);
}

static std::string slurp(const char *rel)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_rf_pa/test_rf_pa.cpp";
    std::string root = file.substr(0, file.size() - suffix.size());
    std::ifstream f((root + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_both_paths_use_the_helper(void)
{
    const char *files[] = {"src/lora_setchip.cpp", "src/esp32/esp32_main.cpp"};
    for (const char *rel : files) {
        std::string s = slurp(rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("rfApplyOutputPower(radio,") != std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("radio.setOutputPower(") == std::string::npos, rel);
        TEST_ASSERT_TRUE_MESSAGE(s.find("#include \"rf_pa.h\"") != std::string::npos, rel);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_helper_passes_optimize_false_by_default);
    RUN_TEST(test_helper_tdeckpro_keeps_table);
    RUN_TEST(test_both_paths_use_the_helper);
    return UNITY_END();
}
