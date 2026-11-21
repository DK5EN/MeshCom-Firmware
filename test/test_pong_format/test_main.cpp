#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "pong_format.h"

// OLD behaviour (before the fix), kept to show the regression.
static void oldPayload(char *o, size_t n, uint32_t id) { snprintf(o, n, "{pong}{%03i}", (int)id); }
static void oldDisplay(const char *p, char *o, size_t n)
{
    const char *c = (strlen(p) > 14) ? p + 14 : "";
    snprintf(o, n, "%.3s", c);
}

void setUp(void) {}
void tearDown(void) {}


void test_top_bit_id_unsigned(void)
{
    char b[40], e[40];
    uint32_t id = 0xE6F91DA7u;
    int r = pongFormatPayload(b, sizeof(b), id);
    snprintf(e, sizeof(e), "{pong}{%u}", (unsigned)id);
    TEST_ASSERT_NULL(strchr(b, '-'));
    TEST_ASSERT_EQUAL_STRING(e, b);
    TEST_ASSERT_EQUAL_INT((int)strlen(e), r);
}

void test_display_unsigned(void)
{
    char d[4];
    pongDisplayId("{pong}{3867557991}", d);
    TEST_ASSERT_EQUAL_STRING("991", d);
}

void test_display_legacy_signed(void)
{
    char d[4];
    pongDisplayId("{pong}{-427409305}", d);
    TEST_ASSERT_EQUAL_STRING("305", d);
}

void test_small_id(void)
{
    char b[40], d[4];
    pongFormatPayload(b, sizeof(b), 7);
    TEST_ASSERT_EQUAL_STRING("{pong}{007}", b);
    pongDisplayId(b, d);
    TEST_ASSERT_EQUAL_STRING("007", d);
}

void test_short_pad(void)
{
    char d[4];
    pongDisplayId("{pong}{5}", d);
    TEST_ASSERT_EQUAL_STRING("005", d);
}

void test_truncated_no_overrun(void)
{
    char d[4];
    pongDisplayId("{pong}", d);
    TEST_ASSERT_EQUAL_STRING("", d);
    pongDisplayId("{pong}{", d);
    TEST_ASSERT_EQUAL_STRING("", d);
    pongDisplayId("{pong}{12", d);
    TEST_ASSERT_EQUAL_STRING("012", d);
    // payload in an exact-size buffer: ASan/valgrind-style overrun guard
    char tight[8];
    memcpy(tight, "{pong}{", 8);
    pongDisplayId(tight, d);
    TEST_ASSERT_EQUAL_STRING("", d);
}

void test_non_pong(void)
{
    char d[4] = {'x', 'x', 'x', 0};
    pongDisplayId("{ping}{123}", d);
    TEST_ASSERT_EQUAL_STRING("", d);
    pongDisplayId("hello", d);
    TEST_ASSERT_EQUAL_STRING("", d);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_bit_id_unsigned);
    RUN_TEST(test_display_unsigned);
    RUN_TEST(test_display_legacy_signed);
    RUN_TEST(test_small_id);
    RUN_TEST(test_short_pad);
    RUN_TEST(test_truncated_no_overrun);
    RUN_TEST(test_non_pong);
    return UNITY_END();
}
