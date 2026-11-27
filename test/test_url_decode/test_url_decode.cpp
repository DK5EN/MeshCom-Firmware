// Native Testsuite fuer url_decode.cpp (issue #1173). Der WebUI-Browser
// schickt jeden Parameter per encodeURIComponent(), also als
// prozentkodiertes UTF-8. Der alte Dekoder kannte nur eine feste Liste von
// Escapes (deutsche Umlaute, einige italienische Vokale, ASCII-Satzzeichen);
// ein polnisches "ą" ging als die sechs Zeichen "%C4%85" auf HF.
//
//   pio test -e native -f test_url_decode

#include <unity.h>

#include <stdint.h>
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

// ---- FORMAL-02: urlDecode policies against the frozen pre-refactor decoders ------------------------
// Frozen copy of url_percent_decode as of 3b1ce88f (do not edit).
namespace frozen
{
static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
static int escape_at(const char *buf, size_t len, size_t i)
{
    if (i + 2 >= len || buf[i] != '%')
        return -1;
    int hi = hex_value(buf[i + 1]);
    int lo = hex_value(buf[i + 2]);
    if (hi < 0 || lo < 0)
        return -1;
    return (hi << 4) | lo;
}
static size_t legacy(char *buf, size_t len)
{
    if (buf == nullptr)
        return 0;
    size_t in = 0, out = 0;
    while (in < len)
    {
        char c = buf[in];
        if (c == '+')
        {
            buf[out++] = ' ';
            in++;
            continue;
        }
        int b = escape_at(buf, len, in);
        if (b < 0)
        {
            buf[out++] = c;
            in++;
            continue;
        }
        in += 3;
        if (b == 0x0D && escape_at(buf, len, in) == 0x0A)
        {
            buf[out++] = '-';
            in += 3;
        }
        else if (b == '"' || b < 0x20 || b == 0x7F)
        {
        }
        else
            buf[out++] = (char)b;
    }
    buf[out] = '\0';
    return out;
}
// Frozen rmparse::decode (web_rm_parse.h) on a NUL string; (size_t)-1 = reject.
static bool isHex(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static unsigned hexVal(unsigned char c)
{
    return c <= '9' ? (unsigned)(c - '0') : (unsigned)((c | 0x20) - 'a' + 10);
}
static bool strict(char *s)
{
    char *w = s;
    for (const char *r = s; *r; r++)
    {
        unsigned char ch = (unsigned char)*r;
        if (ch == '%' && isHex((unsigned char)r[1]) && isHex((unsigned char)r[2]))
        {
            ch = (unsigned char)(hexVal((unsigned char)r[1]) * 16 + hexVal((unsigned char)r[2]));
            r += 2;
        }
        if (ch < 0x20 || ch == 0x7f)
            return false;
        *w++ = (char)ch;
    }
    *w = '\0';
    return true;
}
} // namespace frozen

// Compares both policies with the frozen decoders on one input of n bytes (no raw NUL inside for STRICT,
// whose old form was a C string).
static void diff_one(const unsigned char *in, size_t n)
{
    char a[80], b[80];
    memset(a, 0x55, sizeof(a));
    memset(b, 0x55, sizeof(b));
    memcpy(a, in, n);
    memcpy(b, in, n);
    const size_t ra = frozen::legacy(a, n);
    const size_t rb = urlDecode(b, n, URLD_LEGACY);
    TEST_ASSERT_EQUAL_size_t(ra, rb);
    TEST_ASSERT_EQUAL_MEMORY(a, b, ra + 1);
    memset(b, 0x55, sizeof(b));
    memcpy(b, in, n);
    TEST_ASSERT_EQUAL_size_t(ra, url_percent_decode(b, n));
    TEST_ASSERT_EQUAL_MEMORY(a, b, ra + 1);

    bool has_nul = false;
    for (size_t i = 0; i < n; i++)
        has_nul |= (in[i] == 0);
    if (!has_nul)
    {
        memset(a, 0x55, sizeof(a));
        memset(b, 0x55, sizeof(b));
        memcpy(a, in, n);
        memcpy(b, in, n);
        a[n] = b[n] = '\0';
        const bool ok = frozen::strict(a);
        const size_t r = urlDecode(b, n, URLD_STRICT);
        TEST_ASSERT_EQUAL(ok, r != (size_t)-1);
        if (ok)
        {
            TEST_ASSERT_EQUAL_size_t(strlen(a), r);
            TEST_ASSERT_EQUAL_MEMORY(a, b, r + 1);
        }
    }
}

// Every string of 0..5 bytes over a 17-symbol alphabet that hits each branch (about 1.5M), plus every single byte.
static void test_policies_exhaustive_short(void)
{
    static const unsigned char alpha[] = {'%', '+', '0', '2', 'A', 'f', 'g', 'D', 'a', '"', '\r', '\n', 0x00, 0x1F, 0x7F, 0x80, 'x'};
    const size_t na = sizeof(alpha);
    unsigned char in[5];
    size_t cases = 0;
    for (int v = 0; v < 256; v++)
    {
        in[0] = (unsigned char)v;
        diff_one(in, 1);
        cases++;
    }
    for (size_t n = 0; n <= 5; n++)
    {
        size_t total = 1;
        for (size_t k = 0; k < n; k++)
            total *= na;
        for (size_t idx = 0; idx < total; idx++)
        {
            size_t t = idx;
            for (size_t k = 0; k < n; k++)
            {
                in[k] = alpha[t % na];
                t /= na;
            }
            diff_one(in, n);
            cases++;
        }
    }
    TEST_ASSERT_TRUE(cases > 1000000 / 2);
}

// 3-byte strings: first two bytes over all 256 values, third byte filtered (multiples of 7 plus % F 0 CR LF).
static void test_policies_all_three_byte_inputs(void)
{
    unsigned char in[3];
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++)
            for (int c = 0; c < 256; c += 1)
            {
                if (c % 7 != 0 && c != '%' && c != 'F' && c != '0' && c != 0x0A && c != 0x0D)
                    continue;
                in[0] = (unsigned char)a;
                in[1] = (unsigned char)b;
                in[2] = (unsigned char)c;
                diff_one(in, 3);
            }
}

// Random longer inputs, escape-heavy alphabet and fully random bytes (fixed seed).
static void test_policies_random_long(void)
{
    static const unsigned char alpha[] = {'%', '%', '+', '0', '1', '2', '2', 'A', 'D', 'a', 'F', 'f', 'g', '"', '\r', '\n', 0x00, 0x7F, 0xC4, 0x85, 'x', ' '};
    uint32_t st = 0x12345678u;
    auto rnd = [&st]() { st = st * 1664525u + 1013904223u; return st >> 8; };
    unsigned char in[64];
    for (int it = 0; it < 200000; it++)
    {
        const size_t n = rnd() % 61;
        const bool heavy = (rnd() & 1) != 0;
        for (size_t k = 0; k < n; k++)
            in[k] = heavy ? alpha[rnd() % sizeof(alpha)] : (unsigned char)rnd();
        diff_one(in, n);
    }
}

static void test_urldecode_null_and_unknown_policy(void)
{
    TEST_ASSERT_EQUAL_size_t(0, urlDecode(nullptr, 3, URLD_STRICT));
    char b[8] = "a+%41";
    TEST_ASSERT_EQUAL_size_t(3, urlDecode(b, 5, 77)); // unknown policy = LEGACY
    TEST_ASSERT_EQUAL_STRING("a A", b);
}

static void test_strict_policy_table(void)
{
    char b[32] = "a+b%22%41";
    TEST_ASSERT_EQUAL_size_t(5, urlDecode(b, 9, URLD_STRICT)); // '+' literal, %22 kept
    TEST_ASSERT_EQUAL_STRING("a+b\"A", b);
    char c[8] = "a%00";
    TEST_ASSERT_EQUAL_size_t((size_t)-1, urlDecode(c, 4, URLD_STRICT));
    char d[8] = "%0D%0A";
    TEST_ASSERT_EQUAL_size_t((size_t)-1, urlDecode(d, 6, URLD_STRICT));
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
    RUN_TEST(test_policies_exhaustive_short);
    RUN_TEST(test_policies_all_three_byte_inputs);
    RUN_TEST(test_policies_random_long);
    RUN_TEST(test_urldecode_null_and_unknown_policy);
    RUN_TEST(test_strict_policy_table);
    return UNITY_END();
}
