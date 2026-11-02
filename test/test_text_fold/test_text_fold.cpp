#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "text_fold.h"

static char buf[128];
static void eq(const char* in, TextFoldTarget t, const char* want) {
    size_t n = text_fold(in, strlen(in), buf, sizeof(buf), t);
    TEST_ASSERT_EQUAL_STRING(want, buf);
    TEST_ASSERT_EQUAL_UINT(strlen(want), n);
}
static void eqb(const char* in, TextFoldTarget t, const char* want, size_t wl) {
    size_t n = text_fold(in, strlen(in), buf, sizeof(buf), t);
    TEST_ASSERT_EQUAL_UINT(wl, n);
    TEST_ASSERT_EQUAL_MEMORY(want, buf, wl);
}
void setUp() {}
void tearDown() {}

void test_german() {
    eq("Gr\xC3\xBC\xC3\x9F" "e \xC3\xA4\xC3\xB6\xC3\xBC \xC3\x84\xC3\x96\xC3\x9C", TF_ASCII, "Gruesse ae" "oe" "ue AeOeUe");
}
void test_languages() {
    eq("Zażółć gęślą jaźń", TF_ASCII, "Zazolc gesla jazn");
    eq("Œuvre élève ça", TF_ASCII, "OEuvre eleve ca");
    eq("¿Señor? ¡Año!", TF_ASCII, "?Senor? !Ano!");
    eq("ação", TF_ASCII, "acao");
    eq("Příliš", TF_ASCII, "Prilis");
    eq("Știință", TF_ASCII, "Stiinta");
    eq("æ Æ ł Ł đ Đ ð þ ĳ ø", TF_ASCII, "ae AE l L d D d th ij o");
    eq("Ĳ ŉ Ÿ ſ", TF_ASCII, "IJ 'n Y s");
}
void test_symbols() {
    eq("5\xE2\x82\xAC 20\xC2\xB0", TF_ASCII, "5EUR 20deg");
    eq("5\xE2\x82\xAC 20\xC2\xB0", TF_LATIN1, "5EUR 20\xB0");
    eq("\xE2\x80\x98x\xE2\x80\x99 \xE2\x80\x9Ey\xE2\x80\x9D \xE2\x80\x93 \xE2\x80\x94 \xE2\x80\xA6 \xE2\x80\xA2",
       TF_ASCII, "'x' \"y\" - - ... *");
    eq("\xC2\xAB" "a\xC2\xBB", TF_ASCII, "<<a>>");
    eq("a\xC2\xA0" "b\xE2\x80\xAF" "c\xE2\x80\x89" "d", TF_UTF8_LATIN1, "a b c d");
}
void test_emoji() {
    eq("a\xF0\x9F\x98\x80" "b", TF_ASCII, "a?b");
    eq("a\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7" "b", TF_ASCII, "a?b");
    eq("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", TF_ASCII, "?");
    eq("\xE2\x9D\xA4\xEF\xB8\x8F", TF_ASCII, "?");
    eq("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", TF_ASCII, "??");
    eq("\xE4\xB8\xAD\xE6\x96\x87", TF_ASCII, "??");
}
void test_invalid() {
    eq("a\xFF" "b", TF_ASCII, "a?b");
    eq("a\xC3", TF_ASCII, "a?");
    eq("\xC0\x80", TF_ASCII, "??");
    eq("\xED\xA0\x80", TF_ASCII, "???");
    eq("a\x01" "b", TF_ASCII, "ab");
}
void test_truncate() {
    char o[8];
    size_t n = text_fold("ab\xC3\xA4", 4, o, 4, TF_ASCII); // "ab" + "ae" needs 5
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_STRING("ab", o);
    n = text_fold("ab\xC3\xA4", 4, o, 5, TF_ASCII);
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_STRING("abae", o);
    n = text_fold("\xE2\x82\xAC\xE2\x82\xAC", 6, o, 3, TF_ASCII); // EUR needs 3+1
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("", o);
    n = text_fold("\xE2\x82\xAC\xE2\x82\xAC", 6, o, 6, TF_ASCII);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_STRING("EUR", o);
    n = text_fold("a\xC3\xA9\xC3\xA9", 5, o, 4, TF_UTF8_LATIN1); // a + 2 bytes fit, next would split
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_STRING("a\xC3\xA9", o);
    o[0] = 'x';
    TEST_ASSERT_EQUAL_UINT(0, text_fold("abc", 3, o, 0, TF_ASCII));
    TEST_ASSERT_EQUAL_CHAR('x', o[0]);
    TEST_ASSERT_EQUAL_UINT(0, text_fold("abc", 3, o, 1, TF_ASCII));
    TEST_ASSERT_EQUAL_CHAR(0, o[0]);
}
void test_cp437() {
    eqb("\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x84\xC3\x96\xC3\x9C\xC3\x9F", TF_CP437, "\x84\x94\x81\x8E\x99\x9A\xE1", 7);
    eqb("\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xB1\xC3\x91\xC2\xBF\xC2\xA1\xC2\xB0\xC2\xAB\xC2\xBB",
        TF_CP437, "\x82\x8A\x85\x87\xA4\xA5\xA8\xAD\xF8\xAE\xAF", 11);
    eq("\xC5\x82\xE2\x82\xAC\xC3\x95", TF_CP437, "lEURO");
}
void test_latin1() {
    eqb("\xC3\xA4\xC3\x9F\xC3\xA9\xC2\xB0\xC2\xBF\xC3\xBF", TF_LATIN1, "\xE4\xDF\xE9\xB0\xBF\xFF", 6);
    eq("\xC5\x82\xC5\x9B", TF_LATIN1, "ls");
}
void test_utf8_latin1() {
    eq("\xC3\xA9", TF_UTF8_LATIN1, "\xC3\xA9");
    eq("\xC5\x82", TF_UTF8_LATIN1, "l");
    eq("\xE2\x80\x93\xE2\x80\xA6", TF_UTF8_LATIN1, "\xE2\x80\x93\xE2\x80\xA6");
    eq("\xE2\x82\xAC", TF_UTF8_LATIN1, "EUR");
}
void test_api() {
    uint32_t cp;
    TEST_ASSERT_EQUAL_UINT(3, text_fold_utf8_decode("\xE2\x82\xAC", 3, &cp));
    TEST_ASSERT_EQUAL_HEX32(0x20AC, cp);
    TEST_ASSERT_EQUAL_UINT(2, text_fold_utf8_decode("\xE2\x82", 2, &cp)); // maximal subpart
    TEST_ASSERT_EQUAL_HEX32(0xFFFD, cp);
    char o[4];
    TEST_ASSERT_EQUAL_UINT(0, text_fold_codepoint(0xFE0F, TF_ASCII, o, 4));
    TEST_ASSERT_EQUAL_UINT(2, text_fold_codepoint(0xE4, TF_ASCII, o, 4));
    TEST_ASSERT_EQUAL_UINT(0, text_fold_codepoint(0xE4, TF_ASCII, o, 1));
}

// ---- helpers ---------------------------------------------------------------
static size_t enc(uint32_t cp, char* o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}
// fold "a" + cp + "b" for every target and compare to want (same for all targets)
static void around(uint32_t cp, const char* want) {
    char in[8];
    size_t n = 0;
    in[n++] = 'a';
    n += enc(cp, in + n);
    in[n++] = 'b';
    in[n] = 0;
    for (int t = TF_ASCII; t <= TF_UTF8_LATIN1; t++) {
        char o[16];
        text_fold(in, n, o, sizeof(o), (TextFoldTarget)t);
        char msg[48];
        snprintf(msg, sizeof(msg), "cp %05lX target %d", (unsigned long)cp, t);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, o, msg);
    }
}
static void absorbed_range(uint32_t lo, uint32_t hi) {
    for (uint32_t cp = lo; cp <= hi; cp++) around(cp, "ab");
}

// ---- item 1: independent CP437 reference (official chart, bytes 0x80..0xFF -> Unicode) ----
static const uint16_t kRef437[128] = {
    // 0x80
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    // 0x90
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    // 0xA0
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    // 0xB0
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    // 0xC0
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    // 0xD0
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    // 0xE0
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    // 0xF0
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static int ref_byte_for(uint32_t cp) { // -1 = CP437 has no such char
    for (int i = 0; i < 128; i++)
        if (kRef437[i] == cp) return 0x80 + i;
    return -1;
}

void test_cp437_reference_table() {
    // sanity of the reference itself: 128 distinct entries
    for (int i = 0; i < 128; i++)
        for (int j = i + 1; j < 128; j++) TEST_ASSERT_NOT_EQUAL(kRef437[i], kRef437[j]);
    char o[8];
    // every Latin-1 cp (U+00A0..U+00FF): exactly the reference byte when CP437 has it
    for (uint32_t cp = 0xA0; cp <= 0xFF; cp++) {
        const int want = ref_byte_for(cp);
        const size_t n = text_fold_codepoint(cp, TF_CP437, o, sizeof(o));
        char msg[32];
        snprintf(msg, sizeof(msg), "cp %04lX", (unsigned long)cp);
        if (cp == 0xAD) { // soft hyphen: absorbed on every target
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, n, msg);
            continue;
        }
        TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, n, msg);
        if (cp == 0xA0) { // NBSP: deliberately a space (0xFF would be blank anyway)
            TEST_ASSERT_EQUAL_UINT_MESSAGE(1, n, msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(' ', (uint8_t)o[0], msg);
        } else if (want >= 0) {
            TEST_ASSERT_EQUAL_UINT_MESSAGE(1, n, msg);
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(want, (uint8_t)o[0], msg);
        } else {
            for (size_t k = 0; k < n; k++) TEST_ASSERT_LESS_THAN_UINT8_MESSAGE(0x80, (uint8_t)o[k], msg);
        }
    }
    // every cp of the reference: any high byte emitted maps back to exactly that cp
    for (int i = 0; i < 128; i++) {
        const uint32_t cp = kRef437[i];
        const size_t n = text_fold_codepoint(cp, TF_CP437, o, sizeof(o));
        char msg[32];
        snprintf(msg, sizeof(msg), "ref cp %04lX", (unsigned long)cp);
        for (size_t k = 0; k < n; k++) {
            const uint8_t b = (uint8_t)o[k];
            if (b >= 0x80) {
                TEST_ASSERT_EQUAL_UINT_MESSAGE(1, n, msg);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(cp, kRef437[b - 0x80], msg);
            }
        }
    }
}

void test_latin_table_rows() {
    // one expected-output assertion per 16-entry row of U+00C0..U+017F (typed by hand)
    static const char* const want[12] = {
        "AAAAAeAAECEEEEIIII",       // 00C0 A A A A Ae A AE C E E E E I I I I
        "DNOOOOOexOUUUUeYTHss",     // 00D0 D N O O O O Oe x O U U U Ue Y TH ss
        "aaaaaeaaeceeeeiiii",       // 00E0
        "dnoooooe/ouuuueythy",      // 00F0 d n o o o o oe / o u u u ue y th y
        "AaAaAaCcCcCcCcDd",         // 0100
        "DdEeEeEeEeEeGgGg",         // 0110 (Đ đ ... Ğ ğ)
        "GgGgHhHhIiIiIiIi",         // 0120 (... Ħ ħ ...)
        "IiIJijJjKkkLlLlLlL",       // 0130 (İ ı Ĳ ĳ ... Ŀ)
        "lLlNnNnNn'nNgngOoOo",      // 0140 (ŀ ... ŉ Ŋ ŋ ...)
        "OoOEoeRrRrRrSsSsSs",       // 0150 (Ő ő Œ œ ...)
        "SsTtTtTtUuUuUuUu",         // 0160 (... Ŧ ŧ ...)
        "UuUuWwYyYZzZzZzs"};        // 0170 (Ű ű ... Ÿ ... ſ)
    for (uint32_t row = 0; row < 12; row++) {
        char in[80];
        size_t n = 0;
        for (uint32_t k = 0; k < 16; k++) n += enc(0xC0 + row * 16 + k, in + n);
        in[n] = 0;
        char o[64];
        text_fold(in, n, o, sizeof(o), TF_ASCII);
        char msg[24];
        snprintf(msg, sizeof(msg), "row %03lX", (unsigned long)(0xC0 + row * 16));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want[row], o, msg);
    }
}

void test_exhaustive_byte_ranges() {
    char o[8];
    for (uint32_t cp = 0; cp <= 0x2FFFF; cp++) {
        const size_t n1 = text_fold_codepoint(cp, TF_LATIN1, o, sizeof(o));
        for (size_t k = 0; k < n1; k++) {
            const uint8_t b = (uint8_t)o[k];
            if (b >= 0x80 && b <= 0x9F) TEST_FAIL_MESSAGE("LATIN1 emitted 0x80..0x9F");
            if (b >= 0xA0 && b != cp) TEST_FAIL_MESSAGE("LATIN1 high byte != cp");
        }
        const size_t n4 = text_fold_codepoint(cp, TF_CP437, o, sizeof(o));
        for (size_t k = 0; k < n4; k++) {
            const uint8_t b = (uint8_t)o[k];
            if (b >= 0x80) {
                if (n4 != 1 || kRef437[b - 0x80] != cp) TEST_FAIL_MESSAGE("CP437 byte does not map back");
            }
        }
        const size_t n0 = text_fold_codepoint(cp, TF_ASCII, o, sizeof(o));
        for (size_t k = 0; k < n0; k++)
            if ((uint8_t)o[k] >= 0x80) TEST_FAIL_MESSAGE("ASCII emitted high byte");
    }
}

// ---- item 2: decoder edge cases, ZWJ ----
void test_decoder_pinned() {
    eq("\xE0\x80\x80", TF_ASCII, "???");        // overlong 3-byte
    eq("\xF4\x90\x80\x80", TF_ASCII, "????");   // > U+10FFFF
    eq("\xF5", TF_ASCII, "?");
    eq("\x80", TF_ASCII, "?");
    eq("\xC3" "A", TF_ASCII, "?A");
    uint32_t cp;
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xE0\x80\x80", 3, &cp));
    TEST_ASSERT_EQUAL_HEX32(0xFFFD, cp);
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xF4\x90\x80\x80", 4, &cp));
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xF5\x80\x80\x80", 4, &cp));
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xED\xA0\x80", 3, &cp)); // surrogate
    TEST_ASSERT_EQUAL_UINT(0, text_fold_utf8_decode("x", 0, &cp));
    TEST_ASSERT_EQUAL_UINT(4, text_fold_utf8_decode("\xF4\x8F\xBF\xBF", 4, &cp)); // U+10FFFF ok
    TEST_ASSERT_EQUAL_HEX32(0x10FFFF, cp);
}
void test_zwj() {
    eq("a\xE2\x80\x8D\xF0\x9F\x98\x80", TF_ASCII, "a?");
    eq("\xF0\x9F\x98\x80\xE2\x80\x8D\xC3\xA4", TF_ASCII, "?ae");
}

// ---- item 3: more absorbed ranges ----
void test_absorb_variation_selectors() {
    absorbed_range(0xFE00, 0xFE0F);
    absorbed_range(0xE0100, 0xE01EF);
    around(0xFDFF, "a?b");
    around(0xFE10, "a?b");
    around(0xE00FF, "a?b");
    around(0xE01F0, "a?b");
}
void test_absorb_tags() {
    absorbed_range(0xE0020, 0xE007F);
    around(0xE001F, "a?b");
    around(0xE0080, "a?b");
    // Scotland flag: black flag + tag letters gbsct + cancel tag -> one '?'
    eq("\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xB3\xF3\xA0\x81\xA3\xF3\xA0\x81\xB4\xF3\xA0\x81\xBF",
       TF_ASCII, "?");
}
void test_absorb_combining() {
    absorbed_range(0x0300, 0x036F);
    absorbed_range(0x20D0, 0x20FF);
    around(0x02FF, "a?b");
    around(0x0370, "a?b");
    around(0x20CF, "a?b");
    around(0x2100, "a?b");
    eq("e\xCC\x81", TF_ASCII, "e");                      // NFD e + acute
    eq("A\xCC\x88", TF_UTF8_LATIN1, "A");
    eq("1\xEF\xB8\x8F\xE2\x83\xA3", TF_ASCII, "1");     // keycap 1
    eq("#\xEF\xB8\x8F\xE2\x83\xA3 *\xE2\x83\xA3", TF_CP437, "# *");
}

// ---- item 4: \t \n \r -> one space per run ----
void test_whitespace_controls() {
    eq("a\tb", TF_ASCII, "a b");
    eq("a\nb", TF_ASCII, "a b");
    eq("a\rb", TF_ASCII, "a b");
    eq("a\r\nb", TF_ASCII, "a b");
    eq("a\n\n\t\r b", TF_ASCII, "a  b");   // the run collapses to one space; the real space stays
    eq("a\n\n\t\rb", TF_LATIN1, "a b");
    eq("\n", TF_UTF8_LATIN1, " ");
    eq("a \nb", TF_ASCII, "a  b");
    eq("a\n\x01\nb", TF_ASCII, "a b");     // other (absorbed) controls do not break the run
    eq("a\nb\nc", TF_CP437, "a b c");
    eq("a\x01" "b\n\t", TF_ASCII, "ab ");
    char o[4];
    TEST_ASSERT_EQUAL_UINT(1, text_fold_codepoint('\n', TF_ASCII, o, 4));
    TEST_ASSERT_EQUAL_CHAR(' ', o[0]);
}

// ---- item 5: one '?' per maximal subpart ----
void test_maximal_subpart() {
    eq("a\xE2\x82", TF_ASCII, "a?");
    eq("a\xE2\x82" "b", TF_ASCII, "a?b");
    eq("\x80\x80", TF_ASCII, "??");
    eq("\xF0\x9F\x98", TF_ASCII, "?");
    eq("\xF0\x9F" "A", TF_ASCII, "?A");
    eq("\xE2\x82\xE2\x82\xAC", TF_ASCII, "?EUR");
    eq("\xC3\xC3\xA4", TF_ASCII, "?ae");
    eq("\xE2\x82\xFF", TF_ASCII, "??");
    eq("\xE0\xA0", TF_ASCII, "?");                // valid prefix of E0 A0..BF
    eq("\xED\x9F", TF_ASCII, "?");                // valid prefix of U+D7FF
    eq("\xF4\x8F\xBF", TF_ASCII, "?");
    eq("\xF4\x90", TF_ASCII, "??");               // 0x90 not allowed after F4
    uint32_t cp;
    TEST_ASSERT_EQUAL_UINT(2, text_fold_utf8_decode("\xE2\x82" "X", 3, &cp));
    TEST_ASSERT_EQUAL_HEX32(0xFFFD, cp);
    TEST_ASSERT_EQUAL_UINT(3, text_fold_utf8_decode("\xF0\x9F\x98" "X", 4, &cp));
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xC3", 1, &cp));
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\xC3" "A", 2, &cp));
    TEST_ASSERT_EQUAL_UINT(1, text_fold_utf8_decode("\x80\x80", 2, &cp));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_german);
    RUN_TEST(test_languages);
    RUN_TEST(test_symbols);
    RUN_TEST(test_emoji);
    RUN_TEST(test_invalid);
    RUN_TEST(test_truncate);
    RUN_TEST(test_cp437);
    RUN_TEST(test_latin1);
    RUN_TEST(test_utf8_latin1);
    RUN_TEST(test_api);
    RUN_TEST(test_cp437_reference_table);
    RUN_TEST(test_latin_table_rows);
    RUN_TEST(test_exhaustive_byte_ranges);
    RUN_TEST(test_decoder_pinned);
    RUN_TEST(test_zwj);
    RUN_TEST(test_absorb_variation_selectors);
    RUN_TEST(test_absorb_tags);
    RUN_TEST(test_absorb_combining);
    RUN_TEST(test_whitespace_controls);
    RUN_TEST(test_maximal_subpart);
    return UNITY_END();
}
