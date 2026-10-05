// AU W2 (#1187): src/fw_update_http.h -- the HTTP/1.1 chunked decoder of the update
// download. Pure host tests: wire bytes in, payload and verdict out.
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "fw_update_http.h"

void setUp(void) {}
void tearDown(void) {}

static bool collect(void *ctx, const uint8_t *d, size_t n)
{
    static_cast<std::string *>(ctx)->append(reinterpret_cast<const char *>(d), n);
    return true;
}

// Feeds `wire` in pieces of `step` bytes (0 = all at once). Returns the last verdict.
static int run(const std::string &wire, std::string &out, size_t step = 0)
{
    FwChunkDec d;
    size_t i = 0;
    int r = 0;
    if (step == 0)
        step = wire.size() ? wire.size() : 1;
    while (i < wire.size() && r == 0)
    {
        size_t k = wire.size() - i < step ? wire.size() - i : step;
        r = d.feed(reinterpret_cast<const uint8_t *>(wire.data()) + i, k, collect, &out);
        i += k;
    }
    return r;
}

static void test_plain_chunks(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n", out));
    TEST_ASSERT_EQUAL_STRING("hello world", out.c_str());
}

static void test_size_with_extension(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("5;name=value\r\nhello\r\n3;x\r\nabc\r\n0;last=1\r\n\r\n", out));
    TEST_ASSERT_EQUAL_STRING("helloabc", out.c_str());
}

static void test_crlf_and_bare_lf(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("3\nabc\n2\r\nde\r\n1\nf\n0\n\n", out));
    TEST_ASSERT_EQUAL_STRING("abcdef", out.c_str());
}

static void test_hex_case_and_big_chunk(void)
{
    std::string payload(0x1A3, 'x');
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("1A3\r\n" + payload + "\r\n0\r\n\r\n", out));
    TEST_ASSERT_EQUAL_UINT32(0x1A3, (uint32_t)out.size());
    out.clear();
    TEST_ASSERT_EQUAL_INT(1, run("1a3\r\n" + payload + "\r\n0\r\n\r\n", out));
    TEST_ASSERT_EQUAL_UINT32(0x1A3, (uint32_t)out.size());
}

static void test_more_than_eight_hex_digits_rejected(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(-1, run("123456789\r\nx\r\n", out));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)out.size());
    // exactly eight digits are still a valid (huge) size line: no rejection yet
    out.clear();
    FwChunkDec d;
    const char *eight = "00000005\r\nhello";
    TEST_ASSERT_EQUAL_INT(0, d.feed((const uint8_t *)eight, strlen(eight), collect, &out));
    TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

static void test_leading_zeros(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("0005\r\nhello\r\n000\r\n\r\n", out));
    TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

static void test_final_chunk_only(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("0\r\n\r\n", out));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)out.size());
}

static void test_split_at_every_byte(void)
{
    const std::string wire = "5;e=1\r\nhello\r\nA\r\n0123456789\r\n1\r\n!\r\n0\r\nX-T: 1\r\n\r\n";
    for (size_t step = 1; step <= wire.size(); step++)
    {
        std::string out;
        TEST_ASSERT_EQUAL_INT(1, run(wire, out, step));
        TEST_ASSERT_EQUAL_STRING("hello0123456789!", out.c_str());
    }
}

static bool refuse_sink(void *, const uint8_t *, size_t) { return false; }

static void test_sink_refusal(void)
{
    FwChunkDec d;
    const char *w = "5\r\nhello\r\n0\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(-1, d.feed((const uint8_t *)w, strlen(w), refuse_sink, nullptr));
}

static void test_malformed_size(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(-1, run("zz\r\nabc\r\n", out));
    out.clear();
    TEST_ASSERT_EQUAL_INT(-1, run("\r\nabc\r\n", out)); // no digits
    out.clear();
    TEST_ASSERT_EQUAL_INT(-1, run(";ext\r\nabc\r\n", out)); // extension without size
    out.clear();
    TEST_ASSERT_EQUAL_INT(-1, run("3\r\nabcXX", out));  // garbage after the data
    out.clear();
    TEST_ASSERT_EQUAL_INT(-1, run("-5\r\nhello\r\n", out));
}

static void test_trailers_ignored(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(1, run("4\r\ndata\r\n0\r\nX-Checksum: abc\r\nX-Other: 1\r\n\r\n", out));
    TEST_ASSERT_EQUAL_STRING("data", out.c_str());
}

static void test_incomplete_needs_more(void)
{
    std::string out;
    TEST_ASSERT_EQUAL_INT(0, run("5\r\nhel", out));
    TEST_ASSERT_EQUAL_STRING("hel", out.c_str());
    out.clear();
    TEST_ASSERT_EQUAL_INT(0, run("5\r\nhello\r\n", out)); // no final chunk yet
}

static void test_random_splits(void)
{
    srand(7);
    for (int t = 0; t < 500; t++)
    {
        std::string payload;
        int plen = rand() % 2000;
        for (int i = 0; i < plen; i++)
            payload += (char)(rand() % 256);
        std::string wire;
        size_t pos = 0;
        while (pos < payload.size())
        {
            size_t k = 1 + (size_t)(rand() % 500);
            if (k > payload.size() - pos)
                k = payload.size() - pos;
            char h[32];
            snprintf(h, sizeof(h), (rand() & 1) ? "%zx" : "%zX", k);
            wire += h;
            if (rand() % 5 == 0)
                wire += ";ext=1";
            wire += (rand() % 4 == 0) ? "\n" : "\r\n";
            wire.append(payload, pos, k);
            wire += "\r\n";
            pos += k;
        }
        wire += "0\r\n\r\n";
        std::string out;
        TEST_ASSERT_EQUAL_INT(1, run(wire, out, 1 + (size_t)(rand() % 97)));
        TEST_ASSERT_TRUE(out == payload);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_plain_chunks);
    RUN_TEST(test_size_with_extension);
    RUN_TEST(test_crlf_and_bare_lf);
    RUN_TEST(test_hex_case_and_big_chunk);
    RUN_TEST(test_more_than_eight_hex_digits_rejected);
    RUN_TEST(test_leading_zeros);
    RUN_TEST(test_final_chunk_only);
    RUN_TEST(test_split_at_every_byte);
    RUN_TEST(test_sink_refusal);
    RUN_TEST(test_malformed_size);
    RUN_TEST(test_trailers_ignored);
    RUN_TEST(test_incomplete_needs_more);
    RUN_TEST(test_random_splits);
    return UNITY_END();
}
