// RM ext W1b -- src/rm_text.h: free-text allowlist (refuse, never alter), position argument,
// argument shapes, HTML escape.
#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rm_text.h>

void setUp(void) {}
void tearDown(void) {}

static bool N(const char *t) { return rmTextAllowed(t, RM_TEXT_NAME); }
static bool A(const char *t) { return rmTextAllowed(t, RM_TEXT_ATXT); }

static void test_text_accepts(void)
{
    TEST_ASSERT_TRUE(N("Martin"));
    TEST_ASSERT_TRUE(N("MeshCom Garten"));
    TEST_ASSERT_TRUE(A("MeshCom Garten"));
    TEST_ASSERT_TRUE(A("Test-Node_1.2+x@home? (a)*"));
    TEST_ASSERT_TRUE(N("a"));
    TEST_ASSERT_TRUE(N("Hans, Peter/OM"));
    TEST_ASSERT_TRUE(N("nonex"));
    TEST_ASSERT_TRUE(N("none x"));
}

static void test_text_refuses_each_byte(void)
{
    const char *bad = "#:={};%<>\"'&|\\~`";
    for (const char *p = bad; *p; p++)
    {
        char t[8];
        snprintf(t, sizeof t, "a%cb", *p);
        TEST_ASSERT_FALSE_MESSAGE(N(t), t);
        TEST_ASSERT_FALSE_MESSAGE(A(t), t);
    }
    TEST_ASSERT_FALSE(N("<>\"'&"));
    TEST_ASSERT_FALSE(N("a:ack5"));
    TEST_ASSERT_FALSE(A("a:ack5"));
    TEST_ASSERT_FALSE(A("a,b"));
    TEST_ASSERT_FALSE(A("a/b"));
    char t[4] = {'a', 0x01, 'b', 0};
    TEST_ASSERT_FALSE(N(t));
    t[1] = '\t';
    TEST_ASSERT_FALSE(N(t));
    t[1] = '\n';
    TEST_ASSERT_FALSE(A(t));
    t[1] = 0x7F;
    TEST_ASSERT_FALSE(N(t));
    TEST_ASSERT_FALSE(A(t));
    t[1] = (char)0x80;
    TEST_ASSERT_FALSE(N(t));
    TEST_ASSERT_FALSE(A(t));
    t[1] = (char)0xC3;
    TEST_ASSERT_FALSE(N(t));
}

static void test_text_lengths(void)
{
    char b[64];
    memset(b, 'a', sizeof b);
    b[19] = 0;
    TEST_ASSERT_TRUE(N(b));
    b[19] = 'a';
    b[20] = 0;
    TEST_ASSERT_FALSE(N(b));
    memset(b, 'a', sizeof b);
    b[39] = 0;
    TEST_ASSERT_TRUE(A(b));
    b[39] = 'a';
    b[40] = 0;
    TEST_ASSERT_FALSE(A(b));
    TEST_ASSERT_FALSE(N(""));
    TEST_ASSERT_FALSE(A(""));
    TEST_ASSERT_FALSE(N(nullptr));
}

static void test_text_spaces_and_none(void)
{
    TEST_ASSERT_FALSE(N(" a"));
    TEST_ASSERT_FALSE(N("a "));
    TEST_ASSERT_FALSE(N(" "));
    TEST_ASSERT_FALSE(A("a  b"));
    TEST_ASSERT_FALSE(N("a  b"));
    TEST_ASSERT_FALSE(A(" a"));
    TEST_ASSERT_FALSE(A("a "));
    TEST_ASSERT_FALSE(N("none"));
    TEST_ASSERT_FALSE(N("NONE"));
    TEST_ASSERT_FALSE(N("None"));
    TEST_ASSERT_FALSE(N("nOnE"));
    TEST_ASSERT_TRUE(A("none")); // only the name uses "none" as the clear word in the console path
}

static void test_pos_accepts(void)
{
    RmPosArg p;
    TEST_ASSERT_TRUE(rmPosParse("48.40812 11.73812 492", &p));
    TEST_ASSERT_TRUE(fabs(p.lat - (48.40812)) < 1e-9);
    TEST_ASSERT_TRUE(fabs(p.lon - (11.73812)) < 1e-9);
    TEST_ASSERT_EQUAL_INT(492, p.alt);
    TEST_ASSERT_TRUE(rmPosParse("-33.9 -151.2 0", &p));
    TEST_ASSERT_TRUE(fabs(p.lat - (-33.9)) < 1e-9);
    TEST_ASSERT_TRUE(fabs(p.lon - (-151.2)) < 1e-9);
    TEST_ASSERT_EQUAL_INT(0, p.alt);
    TEST_ASSERT_TRUE(rmPosParse("90 180 40000", &p));
    TEST_ASSERT_TRUE(rmPosParse("-90 -180 0", &p));
    TEST_ASSERT_TRUE(rmPosParse("90.000000 180.000000 1", &p));
    TEST_ASSERT_TRUE(rmPosParse("1.123456 2 3", &p));
    TEST_ASSERT_TRUE(rmPosParse("1 2 3", nullptr));
}

static void test_pos_refuses(void)
{
    RmPosArg p;
    const char *bad[] = {"90.000001 0 0", "-90.000001 0 0", "0 180.1 0", "0 -180.1 0", "0 0 40001",
                         "0 0 -1", "91 0 0", "1e5 2 3", "+1 2 3", " 1 2 3", "1  2 3", "1 2", "1 2 3 4",
                         "nan 1 2", "inf 1 2", "1,5 2 3", "1 2 3 ", "1 2 3.0", "1 2 123456", "1. 2 3",
                         ".5 2 3", "1.1234567 2 3", "1234 2 3", "--1 2 3", "1\t2 3", "", "a b c"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        TEST_ASSERT_FALSE_MESSAGE(rmPosParse(bad[i], &p), bad[i]);
    TEST_ASSERT_FALSE(rmPosParse(nullptr, &p));
}

static void test_shapes(void)
{
    TEST_ASSERT_TRUE(rmArgIsRowIndex("0"));
    TEST_ASSERT_TRUE(rmArgIsRowIndex("999"));
    TEST_ASSERT_FALSE(rmArgIsRowIndex("1000"));
    TEST_ASSERT_FALSE(rmArgIsRowIndex(""));
    TEST_ASSERT_FALSE(rmArgIsRowIndex("1a"));
    TEST_ASSERT_FALSE(rmArgIsRowIndex("-1"));
    TEST_ASSERT_FALSE(rmArgIsRowIndex(nullptr));

    TEST_ASSERT_TRUE(rmArgIsCall("DK5EN"));
    TEST_ASSERT_TRUE(rmArgIsCall("dk5en-12"));
    TEST_ASSERT_TRUE(rmArgIsCall("OE1ABC-15"));
    TEST_ASSERT_TRUE(rmArgIsCall("ABC"));
    TEST_ASSERT_FALSE(rmArgIsCall("AB"));
    TEST_ASSERT_FALSE(rmArgIsCall("OE1ABCDEFG"));
    TEST_ASSERT_FALSE(rmArgIsCall("DK5EN-"));
    TEST_ASSERT_FALSE(rmArgIsCall("DK5EN-123"));
    TEST_ASSERT_FALSE(rmArgIsCall("DK5EN-1a"));
    TEST_ASSERT_FALSE(rmArgIsCall("DK5/EN"));
    TEST_ASSERT_FALSE(rmArgIsCall("DK 5EN"));
    TEST_ASSERT_FALSE(rmArgIsCall(""));
    TEST_ASSERT_FALSE(rmArgIsCall(nullptr));

    TEST_ASSERT_TRUE(rmArgIsFreeText("Hello World", 39));
    TEST_ASSERT_TRUE(rmArgIsFreeText("a<b>&", 39)); // coarse shape only; rmTextAllowed refuses these
    TEST_ASSERT_FALSE(rmArgIsFreeText("", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a{b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a}b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a|b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a:b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a\x01" "b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a\x7f" "b", 39));
    TEST_ASSERT_FALSE(rmArgIsFreeText("a\xc3\xa4", 39));
    TEST_ASSERT_TRUE(rmArgIsFreeText("abcde", 5));
    TEST_ASSERT_FALSE(rmArgIsFreeText("abcdef", 5));

    TEST_ASSERT_TRUE(rmArgIsPos("48.4 11.7 492"));
    TEST_ASSERT_TRUE(rmArgIsPos("999 999 99999")); // syntax only, range is the executor's business
    TEST_ASSERT_FALSE(rmArgIsPos("1 2"));
    TEST_ASSERT_FALSE(rmArgIsPos("1e5 2 3"));
    TEST_ASSERT_FALSE(rmArgIsPos(nullptr));
}

static void test_html_escape(void)
{
    char o[64];
    TEST_ASSERT_EQUAL_UINT(15, rmHtmlEscape("a&b<>", o, sizeof o));
    TEST_ASSERT_EQUAL_STRING("a&amp;b&lt;&gt;", o);
    TEST_ASSERT_EQUAL_UINT(15, strlen(o));
    TEST_ASSERT_EQUAL_UINT(strlen("&quot;&#39;"), rmHtmlEscape("\"'", o, sizeof o));
    TEST_ASSERT_EQUAL_STRING("&quot;&#39;", o);
    TEST_ASSERT_EQUAL_UINT(5, rmHtmlEscape("plain", o, sizeof o));
    TEST_ASSERT_EQUAL_STRING("plain", o);
    TEST_ASSERT_EQUAL_UINT(strlen("&lt;script&gt;"), rmHtmlEscape("<script>", o, sizeof o));
    TEST_ASSERT_EQUAL_STRING("&lt;script&gt;", o);
    // exact fit: 4 chars + terminator
    char e[5];
    // "<" -> "&lt;" is 4 bytes, needs 5 with terminator
    TEST_ASSERT_EQUAL_UINT(4, rmHtmlEscape("<", e, 5));
    TEST_ASSERT_EQUAL_STRING("&lt;", e);
    // one too small: nothing half-written
    char s[4];
    memset(s, 'x', sizeof s);
    TEST_ASSERT_EQUAL_UINT(0, rmHtmlEscape("<", s, 4));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)s[0]);
    char q[7]; // "ab" + "&amp;" = 7 bytes + terminator = 8 needed
    TEST_ASSERT_EQUAL_UINT(0, rmHtmlEscape("ab&", q, 7));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)q[0]);
    char q8[8];
    TEST_ASSERT_EQUAL_UINT(7, rmHtmlEscape("ab&", q8, 8));
    TEST_ASSERT_EQUAL_STRING("ab&amp;", q8);
    TEST_ASSERT_EQUAL_UINT(0, rmHtmlEscape("x", nullptr, 4));
    TEST_ASSERT_EQUAL_UINT(0, rmHtmlEscape("x", o, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_accepts);
    RUN_TEST(test_text_refuses_each_byte);
    RUN_TEST(test_text_lengths);
    RUN_TEST(test_text_spaces_and_none);
    RUN_TEST(test_pos_accepts);
    RUN_TEST(test_pos_refuses);
    RUN_TEST(test_shapes);
    RUN_TEST(test_html_escape);
    return UNITY_END();
}
