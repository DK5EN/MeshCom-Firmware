#include <unity.h>
#include <string.h>
#include <string>
#include "msg_body.h"

static std::string vis(const char *s)
{
    return std::string(s, msg_body_visible_len(s, strlen(s)));
}
void setUp() {}
void tearDown() {}

void test_marker() { TEST_ASSERT_EQUAL_STRING("hello", vis("hello{123").c_str()); }
void test_braces_in_text() { TEST_ASSERT_EQUAL_STRING("Code {x} test", vis("Code {x} test").c_str()); }
void test_text_braces_then_marker() { TEST_ASSERT_EQUAL_STRING("a{b}c", vis("a{b}c{42").c_str()); }
void test_leading_tag() { TEST_ASSERT_EQUAL_STRING("{CET}2026-11-02", vis("{CET}2026-11-02").c_str()); }
void test_leading_tag_marker() { TEST_ASSERT_EQUAL_STRING("{ping}x", vis("{ping}x{007").c_str()); }
void test_utf8() { TEST_ASSERT_EQUAL_STRING("Zażółć", vis("Zażółć{7").c_str()); }
void test_empty() { TEST_ASSERT_EQUAL_STRING("", vis("").c_str()); }
void test_lone_brace() { TEST_ASSERT_EQUAL_STRING("{", vis("{").c_str()); }
void test_no_digits() { TEST_ASSERT_EQUAL_STRING("x{", vis("x{").c_str()); }
void test_five_digits() { TEST_ASSERT_EQUAL_STRING("x", vis("x{12345").c_str()); }
void test_too_many_digits() { TEST_ASSERT_EQUAL_STRING("x{123456", vis("x{123456").c_str()); }
// Trade-off, same rule as the PN parser (pnPayloadIsPn): a user text ending in "{" + 1..5 digits
// reads as an ACK marker and is hidden. The old code cut everything after the first '{'.
void test_year_reads_as_marker() { TEST_ASSERT_EQUAL_STRING("Meeting ", vis("Meeting {2026").c_str()); }
void test_brace_digits_only() { TEST_ASSERT_EQUAL_STRING("{12", vis("{12").c_str()); }

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_marker);
    RUN_TEST(test_braces_in_text);
    RUN_TEST(test_text_braces_then_marker);
    RUN_TEST(test_leading_tag);
    RUN_TEST(test_leading_tag_marker);
    RUN_TEST(test_utf8);
    RUN_TEST(test_empty);
    RUN_TEST(test_lone_brace);
    RUN_TEST(test_no_digits);
    RUN_TEST(test_five_digits);
    RUN_TEST(test_too_many_digits);
    RUN_TEST(test_year_reads_as_marker);
    RUN_TEST(test_brace_digits_only);
    return UNITY_END();
}
