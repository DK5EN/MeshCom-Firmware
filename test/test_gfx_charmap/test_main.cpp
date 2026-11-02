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

// The table must stay sorted by codepoint (binary search) and use the codes 0x80..0xDF once each.
void test_table_sorted_and_codes_unique(void)
{
    bool seen[256] = {false};
    for (size_t i = 0; i < kGfxCharmapCount; i++)
    {
        if (i > 0) TEST_ASSERT_TRUE(kGfxCharmap[i - 1].cp < kGfxCharmap[i].cp);
        TEST_ASSERT_TRUE(kGfxCharmap[i].code >= 0x80 && kGfxCharmap[i].code <= 0xDF);
        TEST_ASSERT_FALSE(seen[kGfxCharmap[i].code]);
        seen[kGfxCharmap[i].code] = true;
    }
    TEST_ASSERT_EQUAL_UINT(96, kGfxCharmapCount);
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

void test_french(void)
{
    char out[64];
    conv("Œuvre élève ça", out, sizeof(out));
    const char exp[] = {(char)0xD0, 'u', 'v', 'r', 'e', ' ', (char)0xBD, 'l', (char)0xBC, 'v', 'e', ' ', (char)0xBB, 'a', 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
    conv("Ÿ", out, sizeof(out));
    const char exp2[] = {(char)0xD2, 0};
    TEST_ASSERT_EQUAL_STRING(exp2, out);
}

void test_spanish(void)
{
    char out[64];
    conv("¿Señor? ¡Año!", out, sizeof(out));
    const char exp[] = {(char)0xD4, 'S', 'e', (char)0xC5, 'o', 'r', '?', ' ', (char)0xD3, 'A', (char)0xC5, 'o', '!', 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_portuguese(void)
{
    char out[64];
    conv("ação", out, sizeof(out));
    const char exp[] = {'a', (char)0xBB, (char)0xB8, 'o', 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_punctuation(void)
{
    char out[64];
    conv("‘’“”„–—…•«»", out, sizeof(out));
    const char exp[] = {(char)0xD7, (char)0xD8, (char)0xD9, (char)0xDA, (char)0xDB, (char)0xDC,
                        (char)0xDD, (char)0xDE, (char)0xDF, (char)0xD5, (char)0xD6, 0};
    TEST_ASSERT_EQUAL_STRING(exp, out);
}

void test_latin1_complete(void)
{
    // every Latin-1 letter U+00C0..U+00FF except multiplication and division signs are mapped
    for (uint32_t cp = 0xC0; cp <= 0xFF; cp++)
    {
        if (cp == 0xD7 || cp == 0xF7) { TEST_ASSERT_EQUAL_UINT8(0, gfx_font_code(cp)); continue; }
        TEST_ASSERT_TRUE(gfx_font_code(cp) >= 0x80);
    }
}

void test_unknown_characters_fold(void)
{
    char out[64];
    conv("a\xC5\x99" "b", out, sizeof(out));                       // ř (Czech): no glyph -> text_fold ASCII "r"
    TEST_ASSERT_EQUAL_STRING("arb", out);
    conv("x\xF0\x9F\x98\x80y", out, sizeof(out));                  // emoji (4 bytes) -> "?"
    TEST_ASSERT_EQUAL_STRING("x?y", out);
    conv("\xE2\x82\xAC", out, sizeof(out));                       // € still has its glyph
    TEST_ASSERT_EQUAL_UINT8(0x9A, (uint8_t)out[0]);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)out[1]);
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

void test_zwj_sequences_collapse_to_one_question_mark(void)
{
    char out[64];
    conv("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", out, sizeof(out));   // family: man ZWJ woman ZWJ girl
    TEST_ASSERT_EQUAL_STRING("?", out);
    conv("a\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88" "b", out, sizeof(out));        // rainbow flag: flag FE0F ZWJ rainbow
    TEST_ASSERT_EQUAL_STRING("a?b", out);
    conv("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", out, sizeof(out));                                            // two emoji without ZWJ stay two
    TEST_ASSERT_EQUAL_STRING("??", out);
    conv("\xF0\x9F\x98\x80\xE2\x80\x8D" "a\xF0\x9F\x98\x80", out, sizeof(out));                           // a letter ends the join
    TEST_ASSERT_EQUAL_STRING("?a?", out);
}

void test_expansion_that_does_not_fit_ends_the_text(void)
{
    char out[4];
    // U+0132 -> "IJ" needs 2 bytes, only 1 left after 'a': stop, do not write the later 'b'
    size_t n = gfx_utf8_to_font("a\xC4\xB2" "b", 4, out, 3);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_STRING("a", out);
    n = gfx_utf8_to_font("a\xC4\xB2" "b", 4, out, 4);                                                         // fits exactly: "aIJ", then 'b' does not
    TEST_ASSERT_EQUAL_STRING("aIJ", out);
    TEST_ASSERT_EQUAL_UINT(3, n);
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
    RUN_TEST(test_french);
    RUN_TEST(test_spanish);
    RUN_TEST(test_portuguese);
    RUN_TEST(test_punctuation);
    RUN_TEST(test_latin1_complete);
    RUN_TEST(test_unknown_characters_fold);
    RUN_TEST(test_invalid_and_cut_off_sequences);
    RUN_TEST(test_output_is_bounded);
    RUN_TEST(test_zwj_sequences_collapse_to_one_question_mark);
    RUN_TEST(test_expansion_that_does_not_fit_ends_the_text);
    return UNITY_END();
}
