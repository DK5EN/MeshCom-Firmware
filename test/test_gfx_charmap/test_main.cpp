// Host unit tests for the GFXfont national-character converter (src/gfx_charmap.h).
// Run with:  pio test -e native_gfx_charmap

#include <unity.h>

#include <cstring>

#include "gfx_charmap.h"

void setUp(void) {}
void tearDown(void) {}

static void conv(const char *in, char *out, size_t cap)
{
    gfx_utf8_to_font(in, strlen(in), out, cap);
}

// The table must stay sorted by codepoint (binary search) and use the codes 0x80..0x9A once each.
void test_table_sorted_and_codes_unique(void)
{
    bool seen[256] = {false};
    for (size_t i = 0; i < kGfxCharmapCount; i++)
    {
        if (i > 0) TEST_ASSERT_TRUE(kGfxCharmap[i - 1].cp < kGfxCharmap[i].cp);
        TEST_ASSERT_TRUE(kGfxCharmap[i].code >= 0x80 && kGfxCharmap[i].code <= 0x9A);
        TEST_ASSERT_FALSE(seen[kGfxCharmap[i].code]);
        seen[kGfxCharmap[i].code] = true;
    }
    TEST_ASSERT_EQUAL_UINT(27, kGfxCharmapCount);
}

void test_ascii_unchanged(void)
{
    char out[64];
    conv("Hello 123 :-) {42}", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello 123 :-) {42}", out);
}

void test_polish_lowercase(void)
{
    char out[64];
    conv("\xC4\x85\xC4\x87\xC4\x99\xC5\x82\xC5\x84\xC3\xB3\xC5\x9B\xC5\xBA\xC5\xBC", out, sizeof(out));   // ąćęłńóśźż
    const char exp[] = {(char)0x80, (char)0x81, (char)0x82, (char)0x83, (char)0x84, (char)0x85,
                        (char)0x86, (char)0x87, (char)0x88, 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_polish_uppercase(void)
{
    char out[64];
    conv("\xC4\x84\xC4\x86\xC4\x98\xC5\x81\xC5\x83\xC3\x93\xC5\x9A\xC5\xB9\xC5\xBB", out, sizeof(out));   // ĄĆĘŁŃÓŚŹŻ
    const char exp[] = {(char)0x89, (char)0x8A, (char)0x8B, (char)0x8C, (char)0x8D, (char)0x8E,
                        (char)0x8F, (char)0x90, (char)0x91, 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_german_and_symbols(void)
{
    char out[64];
    conv("\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x84\xC3\x96\xC3\x9C\xC3\x9F\xC2\xB0\xE2\x82\xAC", out, sizeof(out));   // äöüÄÖÜß°€
    const char exp[] = {(char)0x92, (char)0x93, (char)0x94, (char)0x95, (char)0x96, (char)0x97,
                        (char)0x98, (char)0x99, (char)0x9A, 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_mixed_text(void)
{
    char out[64];
    conv("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 21\xC2\xB0" "C", out, sizeof(out));   // Zażółć 21°C
    const char exp[] = {'Z', 'a', (char)0x88, (char)0x85, (char)0x83, (char)0x81, ' ', '2', '1', (char)0x99, 'C', 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_unknown_characters_dropped(void)
{
    char out[64];
    conv("a\xC3\xA9" "b", out, sizeof(out));                       // é: no glyph -> dropped
    TEST_ASSERT_EQUAL_STRING("ab", out);
    conv("x\xF0\x9F\x98\x80y", out, sizeof(out));                  // emoji (4 bytes) -> dropped
    TEST_ASSERT_EQUAL_STRING("xy", out);
}

void test_invalid_and_cut_off_sequences(void)
{
    char out[64];
    conv("a\x80" "b", out, sizeof(out));                           // stray continuation byte
    TEST_ASSERT_EQUAL_STRING("ab", out);
    conv("a\xC5" "b", out, sizeof(out));                           // lead byte without continuation
    TEST_ASSERT_EQUAL_STRING("ab", out);
    conv("ab\xC5", out, sizeof(out));                              // cut off by the end
    TEST_ASSERT_EQUAL_STRING("ab", out);
    conv("ab\xE2\x82", out, sizeof(out));                          // 3-byte sequence cut off
    TEST_ASSERT_EQUAL_STRING("ab", out);
}

void test_output_is_bounded(void)
{
    char out[5];
    size_t n = gfx_utf8_to_font("abcdefgh", 8, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_STRING("abcd", out);
    TEST_ASSERT_EQUAL_UINT(0, gfx_utf8_to_font("abc", 3, out, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_sorted_and_codes_unique);
    RUN_TEST(test_ascii_unchanged);
    RUN_TEST(test_polish_lowercase);
    RUN_TEST(test_polish_uppercase);
    RUN_TEST(test_german_and_symbols);
    RUN_TEST(test_mixed_text);
    RUN_TEST(test_unknown_characters_dropped);
    RUN_TEST(test_invalid_and_cut_off_sequences);
    RUN_TEST(test_output_is_bounded);
    return UNITY_END();
}
