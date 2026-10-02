// Native Testsuite fuer url_decode.cpp (issue #1173). Der WebUI-Browser
// schickt jeden Parameter per encodeURIComponent(), also als
// prozentkodiertes UTF-8. Der alte Dekoder kannte nur eine feste Liste von
// Escapes (deutsche Umlaute, einige italienische Vokale, ASCII-Satzzeichen);
// ein polnisches "ą" ging als die sechs Zeichen "%C4%85" auf HF.
//
//   pio test -e native -f test_url_decode

#include <unity.h>

#include <string.h>

#include <url_decode.h>

void setUp(void) {}
void tearDown(void) {}

static char buf[256];

static const char *decode(const char *in)
{
    strncpy(buf, in, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t n = url_percent_decode(buf, strlen(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    return buf;
}

// The regression: encodeURIComponent("ąęśćłóżź") must come back byte for
// byte. The old whitelist decoder returned the input unchanged.
static void test_polish_diacritics_decode_to_utf8(void)
{
    TEST_ASSERT_EQUAL_STRING("\xC4\x85\xC4\x99\xC5\x9B\xC4\x87\xC5\x82\xC3\xB3\xC5\xBC\xC5\xBA",
                             decode("%C4%85%C4%99%C5%9B%C4%87%C5%82%C3%B3%C5%BC%C5%BA"));
}

static void test_german_umlauts_still_decode(void)
{
    TEST_ASSERT_EQUAL_STRING("\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F", decode("%C3%A4%C3%B6%C3%BC%C3%9F"));
}

static void test_cyrillic_and_emoji_decode(void)
{
    TEST_ASSERT_EQUAL_STRING("\xD0\x9F\xD1\x80\xD0\xB8", decode("%D0%9F%D1%80%D0%B8"));
    TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x98\x80", decode("%F0%9F%98%80"));
}

static void test_lowercase_hex_accepted(void)
{
    TEST_ASSERT_EQUAL_STRING("\xC4\x85", decode("%c4%85"));
}

static void test_plus_and_percent20_are_space(void)
{
    TEST_ASSERT_EQUAL_STRING("a b c", decode("a+b%20c"));
}

static void test_encoded_plus_stays_plus(void)
{
    TEST_ASSERT_EQUAL_STRING("1+1", decode("1%2B1"));
}

// The old decoder turned "%25" into "%" before the later replacements, so a
// typed "%28" (sent as "%2528") arrived as "(".
static void test_no_double_decoding(void)
{
    TEST_ASSERT_EQUAL_STRING("%28", decode("%2528"));
    TEST_ASSERT_EQUAL_STRING("100%", decode("100%25"));
}

static void test_malformed_percent_stays_literal(void)
{
    TEST_ASSERT_EQUAL_STRING("100%", decode("100%"));
    TEST_ASSERT_EQUAL_STRING("5%x", decode("5%x"));
    TEST_ASSERT_EQUAL_STRING("%G1", decode("%G1"));
    TEST_ASSERT_EQUAL_STRING("a%4", decode("a%4"));
}

static void test_crlf_becomes_dash(void)
{
    TEST_ASSERT_EQUAL_STRING("a-b", decode("a%0D%0Ab"));
}

static void test_controls_and_quote_dropped(void)
{
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%22b"));
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%09b"));
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%0Ab"));
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%0Db"));
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%7Fb"));
}

// %00 must not cut the C string short (send_message strncpy's the result).
static void test_nul_escape_dropped(void)
{
    TEST_ASSERT_EQUAL_STRING("ab", decode("a%00b"));
}

static void test_reserved_ascii_decode(void)
{
    TEST_ASSERT_EQUAL_STRING("{}:;,/?=&@#[]<>", decode("%7B%7D%3A%3B%2C%2F%3F%3D%26%40%23%5B%5D%3C%3E"));
}

static void test_plain_text_unchanged(void)
{
    TEST_ASSERT_EQUAL_STRING("Hallo DK5EN-99", decode("Hallo DK5EN-99"));
}

static void test_empty_and_null(void)
{
    TEST_ASSERT_EQUAL_STRING("", decode(""));
    TEST_ASSERT_EQUAL_size_t(0, url_percent_decode(nullptr, 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_polish_diacritics_decode_to_utf8);
    RUN_TEST(test_german_umlauts_still_decode);
    RUN_TEST(test_cyrillic_and_emoji_decode);
    RUN_TEST(test_lowercase_hex_accepted);
    RUN_TEST(test_plus_and_percent20_are_space);
    RUN_TEST(test_encoded_plus_stays_plus);
    RUN_TEST(test_no_double_decoding);
    RUN_TEST(test_malformed_percent_stays_literal);
    RUN_TEST(test_crlf_becomes_dash);
    RUN_TEST(test_controls_and_quote_dropped);
    RUN_TEST(test_nul_escape_dropped);
    RUN_TEST(test_reserved_ascii_decode);
    RUN_TEST(test_plain_text_unchanged);
    RUN_TEST(test_empty_and_null);
    return UNITY_END();
}
