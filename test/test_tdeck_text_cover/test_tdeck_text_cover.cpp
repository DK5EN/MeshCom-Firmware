// Host test for src/t-deck/tdeck_text_cover.h (CHARS-01). Build with -DTDECK_COVER_LEGACY to run the
// cases against the pre-change behaviour (only U+202F -> space).
#include <unity.h>
#include <string>
#include <string.h>
#ifndef TDECK_COVER_LEGACY
#include "t-deck/tdeck_text_cover.h"
#endif

struct Str { std::string s; void append(const char* p, size_t n) { s.append(p, n); } };

static std::string cover(const std::string& in)
{
#ifdef TDECK_COVER_LEGACY
    std::string r = in;
    const std::string f = "\xE2\x80\xAF";
    for (size_t p; (p = r.find(f)) != std::string::npos;) r.replace(p, 3, " ");
    return r;
#else
    Str o;
    tdeck_text_cover(in.data(), in.size(), o);
    return o.s;
#endif
}

void setUp(void) {}
void tearDown(void) {}

static void test_mixed(void)
{
    TEST_ASSERT_EQUAL_STRING("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \xC5\x99 s \xE2\x82\xAC",
                             cover("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \xC5\x99 \xC8\x99 \xE2\x82\xAC").c_str());
}
static void test_emoji_zwj(void)
{
    // man ZWJ woman ZWJ girl
    TEST_ASSERT_EQUAL_STRING("?", cover("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7").c_str());
    TEST_ASSERT_EQUAL_STRING("a?b", cover("a\xF0\x9F\x98\x80" "b").c_str());
}
static void test_combining_e(void)
{
    // e + U+0301 -> composed char absorbed: base kept, mark dropped
    TEST_ASSERT_EQUAL_STRING("e", cover("e\xCC\x81").c_str());
}
static void test_nnbsp(void)
{
    TEST_ASSERT_EQUAL_STRING("a b", cover("a\xE2\x80\xAF" "b").c_str());
}
static void test_ascii(void)
{
    TEST_ASSERT_EQUAL_STRING("Hello {world} ? ~\nx", cover("Hello {world} ? ~\nx").c_str());
}
static void test_latin_kept(void)
{
    const char* s = "\xC3\xA9 \xC3\x9F \xC2\xB0 \xE2\x80\x9E" "x\xE2\x80\x9C \xE2\x80\xA6";
    TEST_ASSERT_EQUAL_STRING(s, cover(s).c_str());
}
static void test_invalid(void)
{
    TEST_ASSERT_EQUAL_STRING("a?b", cover("a\xFF" "b").c_str());
}
static void test_greek_cjk(void)
{
    std::string r = cover("\xE4\xBD\xA0");
    TEST_ASSERT_EQUAL_STRING("?", r.c_str());
}

static void test_crlf_is_one_newline(void)
{
    TEST_ASSERT_EQUAL_STRING("a\nb", cover("a\r\nb").c_str());            // CRLF -> LF, no stray space
    TEST_ASSERT_EQUAL_STRING("a b", cover("a\rb").c_str());                // lone CR is a space
    TEST_ASSERT_EQUAL_STRING("a \nb", cover("a\r\r\nb").c_str());          // first CR (next is CR) -> space, second dropped
    TEST_ASSERT_EQUAL_STRING("a\n\nb", cover("a\n\nb").c_str());          // LF untouched
    TEST_ASSERT_EQUAL_STRING("a ", cover("a\r").c_str());                  // CR at the end of the text
}
static void test_range_boundaries(void)
{
    TEST_ASSERT_EQUAL_STRING("a", cover("a\xC2\x9F").c_str());             // U+009F C1 control: dropped
    TEST_ASSERT_EQUAL_STRING("\xC2\xA0", cover("\xC2\xA0").c_str());       // U+00A0: first covered cp, kept
    TEST_ASSERT_EQUAL_STRING("\xC5\xBF", cover("\xC5\xBF").c_str());       // U+017F: last covered cp, kept
    TEST_ASSERT_EQUAL_STRING("?", cover("\xC6\x80").c_str());              // U+0180: outside the fonts, no ASCII mapping -> "?"
}
static void test_punctuation_kept(void)
{
    const char* s = "\xE2\x80\x93\xE2\x80\x94\xE2\x80\x98\xE2\x80\x99\xE2\x80\xA2\xE2\x82\xAC";   // - -- ' ' bullet euro
    TEST_ASSERT_EQUAL_STRING(s, cover(s).c_str());
    TEST_ASSERT_EQUAL_STRING("-", cover("\xE2\x80\x90").c_str());          // U+2010 hyphen is not in the cmap: folded
}
static void test_controls(void)
{
    TEST_ASSERT_EQUAL_STRING("a b", cover("a\tb").c_str());                // tab -> space
    TEST_ASSERT_EQUAL_STRING("ab", cover("a\x01" "b").c_str());             // C0 control dropped
    TEST_ASSERT_EQUAL_STRING("ab", cover("a\x7F" "b").c_str());             // DEL dropped
}
static void test_truncated_sequence_at_end(void)
{
    TEST_ASSERT_EQUAL_STRING("a?", cover("a\xE2\x82").c_str());            // 3-byte sequence cut off
    TEST_ASSERT_EQUAL_STRING("a?", cover("a\xC5").c_str());                // lead byte last
    TEST_ASSERT_EQUAL_STRING("a?", cover("a\xF0\x9F\x98").c_str());         // 4-byte sequence cut off
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mixed);
    RUN_TEST(test_emoji_zwj);
    RUN_TEST(test_combining_e);
    RUN_TEST(test_nnbsp);
    RUN_TEST(test_ascii);
    RUN_TEST(test_latin_kept);
    RUN_TEST(test_invalid);
    RUN_TEST(test_greek_cjk);
    RUN_TEST(test_crlf_is_one_newline);
    RUN_TEST(test_range_boundaries);
    RUN_TEST(test_punctuation_kept);
    RUN_TEST(test_controls);
    RUN_TEST(test_truncated_sequence_at_end);
    return UNITY_END();
}
