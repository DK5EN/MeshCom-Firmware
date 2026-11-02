// Host test for src/msg_input_budget.h (TD-25). Regression proof: the old send path cut the
// frame at byte 150, so test_zz_no_dm and test_emoji_straddle fail against a plain byte cut.
#include <unity.h>
#include <string>
#include <string.h>
#include "msg_input_budget.h"

static size_t clampfn(const std::string &t, size_t budget)
{
    return msg_input_clamp(t.data(), t.size(), budget);
}

static std::string rep(const char *s, int n)
{
    std::string r;
    for (int i = 0; i < n; i++) r += s;
    return r;
}

void setUp(void) {}
void tearDown(void) {}

static void test_budget_values(void)
{
    TEST_ASSERT_EQUAL_UINT(149, msg_input_budget(NULL));
    TEST_ASSERT_EQUAL_UINT(149, msg_input_budget(""));
    TEST_ASSERT_EQUAL_UINT(140, msg_input_budget("DK5EN-1"));
    TEST_ASSERT_EQUAL_UINT(0, msg_input_budget(std::string(200, 'A').c_str()));
}

static void test_zz_no_dm(void)
{
    std::string t = rep("\xC5\xBC", 150);  // 300 bytes
    size_t n = clampfn(t, msg_input_budget(NULL));
    TEST_ASSERT_EQUAL_UINT(148, n);
    TEST_ASSERT_EQUAL_UINT(74, n / 2);
}

static void test_zz_dm(void)
{
    std::string t = rep("\xC5\xBC", 150);
    size_t b = msg_input_budget("DK5EN-1");
    TEST_ASSERT_EQUAL_UINT(140, b);
    TEST_ASSERT_EQUAL_UINT(140, clampfn(t, b));  // 70 chars, even
}

static void test_emoji_straddle(void)
{
    // 147 ASCII + 4-byte emoji: bytes 147..150 straddle the 149 limit -> dropped whole
    std::string t = std::string(147, 'a') + "\xF0\x9F\x98\x80";
    TEST_ASSERT_EQUAL_UINT(147, clampfn(t, 149));
}

static void test_ascii_exact(void)
{
    std::string t(149, 'x');
    TEST_ASSERT_EQUAL_UINT(149, clampfn(t, 149));
    std::string u(150, 'x');
    TEST_ASSERT_EQUAL_UINT(149, clampfn(u, 149));
}

static void test_empty(void)
{
    TEST_ASSERT_EQUAL_UINT(0, clampfn("", 149));
    TEST_ASSERT_EQUAL_UINT(0, msg_input_clamp(NULL, 0, 149));
}

static void test_fits_send_limit(void)
{
    const char *call = "DK5EN-1";
    std::string t = rep("\xC5\xBC", 150);
    size_t n = clampfn(t, msg_input_budget(call));
    std::string msg = std::string(":{") + call + "}" + t.substr(0, n);
    TEST_ASSERT_TRUE(msg.size() <= 150);
    TEST_ASSERT_TRUE(msg.size() < 160);
    std::string m2 = ":" + t.substr(0, clampfn(t, msg_input_budget(NULL)));
    TEST_ASSERT_TRUE(m2.size() <= 150);
}

static void test_dest_valid(void)
{
    TEST_ASSERT_TRUE(msg_input_dest_valid("A"));
    TEST_ASSERT_TRUE(msg_input_dest_valid("DK5EN-1"));
    TEST_ASSERT_TRUE(msg_input_dest_valid("DL4MFH-55"));  // 9 chars, TD-26
    TEST_ASSERT_FALSE(msg_input_dest_valid("DL4MFH-555"));  // 10
    TEST_ASSERT_FALSE(msg_input_dest_valid(""));
    TEST_ASSERT_FALSE(msg_input_dest_valid(NULL));
    TEST_ASSERT_FALSE(msg_input_dest_valid("DK5 EN"));
    TEST_ASSERT_FALSE(msg_input_dest_valid("DK5}EN"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_budget_values);
    RUN_TEST(test_zz_no_dm);
    RUN_TEST(test_zz_dm);
    RUN_TEST(test_emoji_straddle);
    RUN_TEST(test_ascii_exact);
    RUN_TEST(test_empty);
    RUN_TEST(test_fits_send_limit);
    RUN_TEST(test_dest_valid);
    return UNITY_END();
}
