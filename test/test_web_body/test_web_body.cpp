// Host test for src/web_functions/web_body.h -- the request body accumulator (FORMAL-02 wave 7a).
//
//   pio test -e native_web_body   (host only, no hardware)
//
// 1. Unit cases: Content-Length edge values, push beyond want/cap, NUL bytes, short end, exact-cap buffer (ASan).
// 2. Reference: String-free copies of the byte handling and return codes of web_read_body()
//    (web_functions.cpp) and rmReadBody() (web_rm_handlers.cpp) over a scripted byte source
//    (bytes + "stalls after k bytes"), compared with Begin/Push/End for every Content-Length 0..17 and
//    every available length 0..16 and stall point, caps 4, 8, 261, 6145.

#include <unity.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <web_functions/web_body.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- scripted source

struct Src
{
    std::vector<char> bytes;
    size_t            pos = 0;
    // stall: the source delivers bytes.size() bytes, then nothing (idle deadline / disconnect)
    bool available() const { return pos < bytes.size(); }
    int  read() { return (unsigned char)bytes[pos++]; }
};

// ---------------------------------------------------------------- reference: web_read_body (max = CONFIG_JSON_MAX)

static int ref_web_read_body(char *buf, long max, long content_length, Src &s)
{
    buf[0] = '\0';
    if (content_length <= 0)
        return -1;
    if (content_length > max)
        return -2;
    long got = 0;
    while (got < content_length && s.available())
    {
        int c = s.read();
        if (c < 0)
            break;
        buf[got++] = (char)c;
    }
    buf[got] = '\0';
    return (got == content_length) ? (int)got : -3;
}

// ---------------------------------------------------------------- reference: rmReadBody

static const char *ref_rmReadBody(char *buf, size_t cap, long content_length, Src &s)
{
    buf[0] = '\0';
    if (content_length <= 0 || (size_t)content_length >= cap)
        return "size";
    long got = 0;
    while (got < content_length && s.available())
    {
        int c = s.read();
        if (c < 0)
            break;
        buf[got++] = (char)c;
    }
    buf[got] = '\0';
    return got == content_length ? nullptr : "short";
}

// ---------------------------------------------------------------- driver over the accumulator

static int8_t drive(WebBodyAcc &a, char *buf, size_t cap, long cl, Src &s)
{
    int8_t b = webBodyBegin(&a, buf, cap, cl);
    if (b < 0)
        return b;
    while (a.got < a.want && s.available())
        webBodyPush(&a, (char)s.read());
    return webBodyEnd(&a);
}

// ---------------------------------------------------------------- unit cases

static void test_begin_edges(void)
{
    const size_t cap = 8;
    char *buf = (char *)malloc(cap); // exactly cap bytes: ASan flags any write at or past cap
    WebBodyAcc a;
    const long nolen[] = {-5, -1, 0, LONG_MIN};
    for (long v : nolen)
        TEST_ASSERT_EQUAL_INT(WB_NOLEN, webBodyBegin(&a, buf, cap, v));
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, cap, 1));
    TEST_ASSERT_EQUAL_INT(1, a.want);
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, cap, (long)cap - 1));
    TEST_ASSERT_EQUAL_INT(7, a.want);
    const long big[] = {(long)cap, (long)cap + 1, 65535, 65536, 70000, LONG_MAX};
    for (long v : big)
        TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, buf, cap, v));
    if (sizeof(long) > 4)
    {
        TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, buf, cap, (long)INT32_MAX + 1));
        TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, buf, cap, (long)4294967296LL + 3)); // would truncate to 3
    }
    TEST_ASSERT_EQUAL_INT(WB_NOLEN, webBodyBegin(&a, buf, cap, 0));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
    free(buf);
}

static void test_cap_zero_and_huge_cap(void)
{
    WebBodyAcc a;
    char c = 'x';
    TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, &c, 0, 1));
    TEST_ASSERT_EQUAL_CHAR('x', c); // untouched
    TEST_ASSERT_FALSE(webBodyPush(&a, 'a'));
    char *buf = (char *)malloc(70000);
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, 70000, 100));
    TEST_ASSERT_EQUAL_UINT16(65535, a.cap);
    TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, buf, 70000, 65535)); // want >= clamped cap
    free(buf);
}

static void test_push_bounds(void)
{
    const size_t cap = 4;
    char *buf = (char *)malloc(cap);
    WebBodyAcc a;
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, cap, 3));
    TEST_ASSERT_TRUE(webBodyPush(&a, 'a'));
    TEST_ASSERT_TRUE(webBodyPush(&a, 'b'));
    TEST_ASSERT_TRUE(webBodyPush(&a, 'c'));
    TEST_ASSERT_FALSE(webBodyPush(&a, 'd')); // beyond want
    TEST_ASSERT_FALSE(webBodyPush(&a, 'e'));
    TEST_ASSERT_EQUAL_UINT16(3, a.got);
    TEST_ASSERT_EQUAL_INT(0, webBodyEnd(&a));
    TEST_ASSERT_EQUAL_STRING("abc", buf); // NUL landed at buf[3] == cap-1, inside the buffer
    free(buf);
}

static void test_push_without_begin_ok(void)
{
    char buf[4];
    WebBodyAcc a;
    TEST_ASSERT_EQUAL_INT(WB_SIZE, webBodyBegin(&a, buf, sizeof(buf), 4));
    TEST_ASSERT_FALSE(webBodyPush(&a, 'a')); // failed Begin: nothing accepted
    TEST_ASSERT_EQUAL_UINT16(0, a.got);
}

static void test_nul_bytes(void)
{
    char *buf = (char *)malloc(8);
    WebBodyAcc a;
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, 8, 5));
    const char in[5] = {'a', '\0', 'b', '\0', 'c'};
    for (char c : in)
        TEST_ASSERT_TRUE(webBodyPush(&a, c));
    TEST_ASSERT_EQUAL_INT(0, webBodyEnd(&a));
    TEST_ASSERT_EQUAL_UINT16(5, a.got);
    TEST_ASSERT_EQUAL_MEMORY(in, buf, 5);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[5]);
    free(buf);
}

static void test_short_end(void)
{
    char *buf = (char *)malloc(8);
    WebBodyAcc a;
    memset(buf, 'Z', 8);
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, 8, 6));
    webBodyPush(&a, 'x');
    webBodyPush(&a, 'y');
    TEST_ASSERT_EQUAL_INT(WB_SHORT, webBodyEnd(&a));
    TEST_ASSERT_EQUAL_STRING("xy", buf);
    free(buf);
}

static void test_exact_cap_minus_one(void)
{
    const size_t cap = 16;
    char *buf = (char *)malloc(cap);
    WebBodyAcc a;
    TEST_ASSERT_EQUAL_INT(0, webBodyBegin(&a, buf, cap, (long)cap - 1));
    for (size_t i = 0; i < cap + 4; i++)
        webBodyPush(&a, 'k');
    TEST_ASSERT_EQUAL_INT(0, webBodyEnd(&a));
    TEST_ASSERT_EQUAL_UINT16(cap - 1, a.got);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[cap - 1]);
    free(buf);
}

// ---------------------------------------------------------------- reference comparison

static int g_compared = 0;

static void test_vs_readers(void)
{
    const size_t caps[] = {4, 8, 261, 6145};
    for (size_t cap : caps)
    {
        for (long cl = -1; cl <= 17; cl++)
        {
            for (size_t len = 0; len <= 16; len++)
            {
                // stall after k bytes: k == len means the source simply ends after all its bytes
                Src s0;
                for (size_t i = 0; i < len; i++)
                    s0.bytes.push_back((char)(i == 3 ? 0 : 'A' + (int)i)); // includes a NUL byte
                char *r1 = (char *)malloc(cap), *r2 = (char *)malloc(cap), *t = (char *)malloc(cap);
                memset(r1, 0x55, cap);
                memset(r2, 0x55, cap);
                memset(t, 0x55, cap);
                Src sa = s0, sb = s0, sc = s0;

                // web_read_body reference with max = cap - 1 (CONFIG_JSON_MAX)
                int w = ref_web_read_body(r1, (long)cap - 1, cl, sa);
                const char *rm = ref_rmReadBody(r2, cap, cl, sb);
                WebBodyAcc a;
                int8_t n = drive(a, t, cap, cl, sc);

                // web_read_body: bytes / -1 / -2 / -3
                int mapped_w = (n == 0) ? (int)a.got : n;
                TEST_ASSERT_EQUAL_INT(w, mapped_w);
                // rmReadBody: nullptr / "size" / "short"
                const char *mapped_rm = (n == 0) ? nullptr : (n == WB_SHORT ? "short" : "size");
                if (rm == nullptr)
                    TEST_ASSERT_NULL(mapped_rm);
                else
                    TEST_ASSERT_EQUAL_STRING(rm, mapped_rm);
                // buffer contents: both readers leave the same bytes up to and incl. the NUL
                size_t used = (n < 0 && n != WB_SHORT) ? 1 : (size_t)a.got + 1;
                TEST_ASSERT_EQUAL_MEMORY(r1, t, used);
                TEST_ASSERT_EQUAL_MEMORY(r2, t, used);
                g_compared += 2;
                free(r1);
                free(r2);
                free(t);
            }
        }
    }
}

static void test_compared_count(void)
{
    // 4 caps * 19 Content-Length values * 17 lengths * 2 readers
    TEST_ASSERT_EQUAL_INT(4 * 19 * 17 * 2, g_compared);
    printf("compared %d reader results\n", g_compared);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_edges);
    RUN_TEST(test_cap_zero_and_huge_cap);
    RUN_TEST(test_push_bounds);
    RUN_TEST(test_push_without_begin_ok);
    RUN_TEST(test_nul_bytes);
    RUN_TEST(test_short_end);
    RUN_TEST(test_exact_cap_minus_one);
    RUN_TEST(test_vs_readers);
    RUN_TEST(test_compared_count);
    return UNITY_END();
}
