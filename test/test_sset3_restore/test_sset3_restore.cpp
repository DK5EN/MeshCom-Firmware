// GPS-AUTOSYM-01: both mains must restore bGPSAutosymbol (node_sset3 0x1000) at boot.
//   pio test -e native -f test_sset3_restore
#include <unity.h>
#include "../support/repo_file.h"
#include <fstream>
#include <sstream>
#include <string>

void setUp(void) {}
void tearDown(void) {}

static std::string slurp(const char *rel)
{
    return repo_slurp(rel);
}

static void test_both_mains_restore_autosymbol_bit(void)
{
    const char *files[] = {"src/esp32/esp32_main.cpp", "src/nrf52/nrf52_main.cpp"};
    for (const char *rel : files)
        TEST_ASSERT_TRUE_MESSAGE(slurp(rel).find("bGPSAutosymbol = meshcom_settings.node_sset3 & 0x1000") != std::string::npos, rel);
}

static void test_writers_use_the_same_bit(void)
{
    std::string s = slurp("src/command_functions.cpp");
    size_t a = s.find("bGPSAutosymbol = true;");
    TEST_ASSERT_TRUE(a != std::string::npos);
    size_t b = s.find("node_sset3 |= 0x1000", a);
    TEST_ASSERT_TRUE(b != std::string::npos);
    TEST_ASSERT_TRUE(b - a < 200);
    size_t c = s.find("bGPSAutosymbol = false;");
    TEST_ASSERT_TRUE(c != std::string::npos);
    size_t d = s.find("node_sset3 &= ~0x1000", c);
    TEST_ASSERT_TRUE(d != std::string::npos);
    TEST_ASSERT_TRUE(d - c < 200);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_both_mains_restore_autosymbol_bit);
    RUN_TEST(test_writers_use_the_same_bit);
    return UNITY_END();
}
