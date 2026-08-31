// Native Testsuite fuer charset_filter.cpp (CHR-01 / CHR-02, docs/BACKLOG.md
// Paragraph 3.8p). UTF-8-Allowlist-Filter: druckbares ASCII plus gueltiges
// UTF-8 passiert, C0/C1/DEL, ungueltige/overlong Sequenzen und die
// Bidi-/Zero-Width-Format-Zeichen werden entfernt; STRIP_SEPARATORS
// entfernt zusaetzlich die von den APRS-Parsern selbst als Trenner
// genutzten Bytes ('{' '}' ':' ';' ',' '/'). Dazu die UTF-8-sichere
// Kuerzung, die die 25-Byte-atxt-Falle (aprs_functions.cpp:632) vermeidet.
//
//   pio test -e native_aprs -f test_charset_filter

#include <unity.h>

#include <string.h>
#include <stdio.h>

#include <charset_filter.h>

void setUp(void) {}
void tearDown(void) {}

// ---- charset_filter_apply(): plain mode -----------------------------------

static void test_ascii_and_umlaut_passes(void)
{
    // "GrUE Ess e!" mit deutschen Umlauten UE (C3 9C) und scharfem S (C3 9F),
    // als char-Array statt String-Literal: C++ haengt an ein \xNN alle
    // folgenden Hex-Ziffern an (auch aus dem naechsten \xNN), ein
    // String-Literal mit mehreren UTF-8-Bytes hintereinander waere also ein
    // einziger, ueberlanger hex-escape.
    char buf[] = { 'G', 'r', (char)0xC3, (char)0x9C, (char)0xC3, (char)0x9F, 'e', '!', 0 };
    size_t orig_len = strlen(buf);

    size_t out = charset_filter_apply(buf, orig_len, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(orig_len, out);
    buf[out] = 0;
    char expect[] = { 'G', 'r', (char)0xC3, (char)0x9C, (char)0xC3, (char)0x9F, 'e', '!', 0 };
    TEST_ASSERT_EQUAL_STRING(expect, buf);
}

static void test_emoji_passes(void)
{
    // U+1F600 GRINNING FACE, 4-Byte-Sequenz F0 9F 98 80.
    char buf[] = { 'h', 'i', ' ', (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, ' ', 't', 'h', 'e', 'r', 'e', 0 };
    size_t orig_len = strlen(buf);
    char expect[sizeof(buf)];
    memcpy(expect, buf, sizeof(buf));

    size_t out = charset_filter_apply(buf, orig_len, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(orig_len, out);
    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING(expect, buf);
}

static void test_c0_and_del_stripped(void)
{
    // 0x01 (SOH), 0x1F (US) und 0x7F (DEL) zwischen druckbaren Zeichen.
    char buf[] = { 'A', 0x01, 'B', 0x1F, 'C', 0x7F, 'D', 0 };
    size_t orig_len = strlen(buf);

    size_t out = charset_filter_apply(buf, orig_len, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(4, out);
    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING("ABCD", buf);
}

static void test_c1_stripped(void)
{
    // U+0085 NEL als 2-Byte-Sequenz C2 85 -- der klassische C1-Kontrollcode,
    // der nur ueber UTF-8 ueberhaupt in einem char-Buffer auftauchen kann.
    char buf[] = { 'A', (char)0xC2, (char)0x85, 'B', 0 };

    size_t out = charset_filter_apply(buf, 4, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(2, out);
    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

static void test_invalid_and_overlong_dropped_without_corrupting_neighbors(void)
{
    // C0 80 ist die overlong 2-Byte-Kodierung von NUL (immer ungueltig,
    // RFC 3629); ein einzelnes 0x80 ist ein Continuation-Byte ohne Lead;
    // ED A0 80 kodiert einen Surrogate-Codepoint (U+D800, immer ungueltig);
    // F4 90 80 80 liegt oberhalb U+10FFFF. Alle vier stehen zwischen
    // gueltigen ASCII-Buchstaben, die unangetastet bleiben muessen.
    char buf[] = {
        'A', (char)0xC0, (char)0x80,
        'B', (char)0x80,
        'C', (char)0xED, (char)0xA0, (char)0x80,
        'D', (char)0xF4, (char)0x90, (char)0x80, (char)0x80,
        'E', 0
    };
    size_t orig_len = strlen(buf);

    size_t out = charset_filter_apply(buf, orig_len, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(5, out);
    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING("ABCDE", buf);
}

static void test_overlong_3_and_4_byte_dropped(void)
{
    // E0 80 80 ist die overlong 3-Byte-Kodierung von NUL; F0 80 80 80 die
    // overlong 4-Byte-Kodierung von NUL.
    char buf3[] = { 'X', (char)0xE0, (char)0x80, (char)0x80, 'Y', 0 };
    size_t out3 = charset_filter_apply(buf3, strlen(buf3), CHARSET_FILTER_PLAIN);
    buf3[out3] = 0;
    TEST_ASSERT_EQUAL_STRING("XY", buf3);

    char buf4[] = { 'X', (char)0xF0, (char)0x80, (char)0x80, (char)0x80, 'Y', 0 };
    size_t out4 = charset_filter_apply(buf4, strlen(buf4), CHARSET_FILTER_PLAIN);
    buf4[out4] = 0;
    TEST_ASSERT_EQUAL_STRING("XY", buf4);
}

static void test_bidi_and_zero_width_stripped(void)
{
    // U+200B ZERO WIDTH SPACE (E2 80 8B), U+202A LRE (E2 80 AA),
    // U+2060 WORD JOINER (E2 81 A0), U+FEFF BOM (EF BB BF).
    char buf[] = {
        'A', (char)0xE2, (char)0x80, (char)0x8B,
        'B', (char)0xE2, (char)0x80, (char)0xAA,
        'C', (char)0xE2, (char)0x81, (char)0xA0,
        'D', (char)0xEF, (char)0xBB, (char)0xBF,
        'E', 0
    };
    size_t orig_len = strlen(buf);

    size_t out = charset_filter_apply(buf, orig_len, CHARSET_FILTER_PLAIN);

    TEST_ASSERT_EQUAL_UINT(5, out);
    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING("ABCDE", buf);
}

// ---- charset_filter_apply(): separator-strip mode (CHR-02) ----------------

static void test_separator_mode_strips_exact_derived_set(void)
{
    // Trenner-Menge, hergeleitet aus den Parsern (siehe charset_filter.h):
    // '{' '}' ':' ';' ',' '/'. Andere Interpunktion (. - ! ? " ' _) und
    // Ziffern/Buchstaben bleiben in BEIDEN Modi erhalten.
    const char *input = "a{b}c:d;e,f/g.h-i!j?k\"l'm_n1";

    char plain[64];
    snprintf(plain, sizeof(plain), "%s", input);
    size_t out_plain = charset_filter_apply(plain, strlen(plain), CHARSET_FILTER_PLAIN);
    plain[out_plain] = 0;
    TEST_ASSERT_EQUAL_STRING(input, plain);

    char stripped[64];
    snprintf(stripped, sizeof(stripped), "%s", input);
    size_t out_stripped = charset_filter_apply(stripped, strlen(stripped), CHARSET_FILTER_STRIP_SEPARATORS);
    stripped[out_stripped] = 0;
    TEST_ASSERT_EQUAL_STRING("abcdefg.h-i!j?k\"l'm_n1", stripped);
}

static void test_separator_mode_still_strips_controls_and_format_chars(void)
{
    // STRIP_SEPARATORS ist PLAIN plus Trenner, nicht ein Ersatz dafuer.
    char buf[] = { 'a', 0x01, '{', 'b', '/', 0x7F, 'c', 0 };

    size_t out = charset_filter_apply(buf, strlen(buf), CHARSET_FILTER_STRIP_SEPARATORS);

    buf[out] = 0;
    TEST_ASSERT_EQUAL_STRING("abc", buf);
}

// ---- charset_utf8_safe_truncate() ------------------------------------------

static void test_truncate_noop_when_within_limit(void)
{
    const char *s = "Hello";
    TEST_ASSERT_EQUAL_UINT(5, charset_utf8_safe_truncate(s, 5, 25));
    TEST_ASSERT_EQUAL_UINT(5, charset_utf8_safe_truncate(s, 5, 5));
}

static void test_truncate_splits_2byte_sequence(void)
{
    // "A" + oe (C3 B6, 2 Byte) + "B" -- ein Cap von 2 Byte faellt genau
    // zwischen die beiden Bytes der oe-Sequenz.
    const char buf[] = { 'A', (char)0xC3, (char)0xB6, 'B' };

    size_t out = charset_utf8_safe_truncate(buf, sizeof(buf), 2);

    // Die angeschnittene 2-Byte-Sequenz wird ganz verworfen, nicht halbiert.
    TEST_ASSERT_EQUAL_UINT(1, out);
}

static void test_truncate_splits_3byte_sequence(void)
{
    // "A" + Euro-Zeichen (E2 82 AC, 3 Byte) + "B" -- Cap von 3 Byte faellt
    // mitten in die 3-Byte-Sequenz (nur 2 ihrer 3 Bytes passen).
    const char buf[] = { 'A', (char)0xE2, (char)0x82, (char)0xAC, 'B' };

    size_t out = charset_utf8_safe_truncate(buf, sizeof(buf), 3);

    TEST_ASSERT_EQUAL_UINT(1, out);

    // Cap von 4 Byte deckt "A" plus die volle 3-Byte-Sequenz ab (1+3=4) --
    // die Sequenz passt exakt, "B" faellt weg.
    out = charset_utf8_safe_truncate(buf, sizeof(buf), 4);
    TEST_ASSERT_EQUAL_UINT(4, out);

    // Cap von 5 Byte umfasst die volle Sequenz und "B".
    out = charset_utf8_safe_truncate(buf, sizeof(buf), 5);
    TEST_ASSERT_EQUAL_UINT(5, out);
}

static void test_truncate_splits_4byte_sequence(void)
{
    // "A" + Emoji (F0 9F 98 80, 4 Byte) + "B" -- Cap von 4 Byte faellt
    // mitten in die 4-Byte-Sequenz.
    const char buf[] = { 'A', (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 'B' };

    size_t out = charset_utf8_safe_truncate(buf, sizeof(buf), 4);
    TEST_ASSERT_EQUAL_UINT(1, out);

    out = charset_utf8_safe_truncate(buf, sizeof(buf), 6);
    TEST_ASSERT_EQUAL_UINT(6, out);
}

static void test_truncate_exact_boundary_keeps_full_sequence(void)
{
    // Cap genau auf der Sequenzgrenze: die Sequenz bleibt vollstaendig.
    const char buf[] = { 'A', (char)0xC3, (char)0xB6 };  // "A" + oe, 3 Byte

    TEST_ASSERT_EQUAL_UINT(3, charset_utf8_safe_truncate(buf, sizeof(buf), 3));
}

// ---- empty / NULL input -----------------------------------------------------

static void test_empty_and_null_input(void)
{
    TEST_ASSERT_EQUAL_UINT(0, charset_filter_apply(NULL, 0, CHARSET_FILTER_PLAIN));
    TEST_ASSERT_EQUAL_UINT(0, charset_filter_apply(NULL, 10, CHARSET_FILTER_PLAIN));

    char buf[4] = { 'A', 'B', 'C', 0 };
    TEST_ASSERT_EQUAL_UINT(0, charset_filter_apply(buf, 0, CHARSET_FILTER_PLAIN));

    TEST_ASSERT_EQUAL_UINT(0, charset_utf8_safe_truncate(NULL, 0, 25));
    TEST_ASSERT_EQUAL_UINT(0, charset_utf8_safe_truncate(NULL, 10, 25));
    TEST_ASSERT_EQUAL_UINT(0, charset_utf8_safe_truncate(buf, 0, 25));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_ascii_and_umlaut_passes);
    RUN_TEST(test_emoji_passes);
    RUN_TEST(test_c0_and_del_stripped);
    RUN_TEST(test_c1_stripped);
    RUN_TEST(test_invalid_and_overlong_dropped_without_corrupting_neighbors);
    RUN_TEST(test_overlong_3_and_4_byte_dropped);
    RUN_TEST(test_bidi_and_zero_width_stripped);
    RUN_TEST(test_separator_mode_strips_exact_derived_set);
    RUN_TEST(test_separator_mode_still_strips_controls_and_format_chars);
    RUN_TEST(test_truncate_noop_when_within_limit);
    RUN_TEST(test_truncate_splits_2byte_sequence);
    RUN_TEST(test_truncate_splits_3byte_sequence);
    RUN_TEST(test_truncate_splits_4byte_sequence);
    RUN_TEST(test_truncate_exact_boundary_keeps_full_sequence);
    RUN_TEST(test_empty_and_null_input);
    return UNITY_END();
}
