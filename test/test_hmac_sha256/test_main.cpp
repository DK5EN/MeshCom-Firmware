// RM-01: src/hmac_sha256.h against FIPS 180-4 / NIST SHA-256 vectors and
// RFC 4231 HMAC-SHA256 test cases 1-7.
//
//   pio test -e native_hmac_sha256

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <hmac_sha256.h>

static const uint8_t *U(const char *s)
{
    return reinterpret_cast<const uint8_t *>(s);
}

static void expectHex(const char *want, const uint8_t *got, size_t nbytes)
{
    char hex[65];
    hexLower(got, nbytes, hex);
    TEST_ASSERT_EQUAL_STRING(want, hex);
}

static void checkSha(const char *msg, const char *want)
{
    uint8_t d[32];
    sha256(U(msg), strlen(msg), d);
    expectHex(want, d, 32);
}

void test_sha_empty(void)
{
    checkSha("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void test_sha_abc(void)
{
    checkSha("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_sha_448bit(void)
{
    checkSha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

void test_sha_896bit(void)
{
    checkSha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
             "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

void test_sha_million_a_odd_chunks(void)
{
    static const size_t chunks[] = {1, 3, 7, 61, 63, 64, 65, 127, 1000, 4097};
    uint8_t a[4097];
    memset(a, 'a', sizeof(a));

    Sha256Ctx c;
    sha256Init(c);
    size_t left = 1000000;
    size_t i = 0;
    while (left > 0)
    {
        size_t n = chunks[i++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if (n > left)
            n = left;
        sha256Update(c, a, n);
        left -= n;
    }
    uint8_t d[32];
    sha256Final(c, d);
    expectHex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", d, 32);
}

void test_sha_zero_length_update_is_noop(void)
{
    Sha256Ctx c;
    uint8_t d[32];
    sha256Init(c);
    sha256Update(c, U("ab"), 0);
    sha256Update(c, NULL, 0);
    sha256Update(c, U("abc"), 3);
    sha256Final(c, d);
    expectHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", d, 32);
}

void test_sha_final_wipes_context(void)
{
    Sha256Ctx c;
    uint8_t d[32];
    sha256Init(c);
    sha256Update(c, U("abc"), 3);
    sha256Final(c, d);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&c);
    for (size_t i = 0; i < sizeof(c); i++)
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
}

// incremental update split into two pieces at every position 0..130,
// message lengths around the padding boundaries as well
void test_sha_split_at_every_boundary(void)
{
    uint8_t msg[131];
    for (size_t i = 0; i < sizeof(msg); i++)
        msg[i] = (uint8_t)(i * 7 + 3);

    for (size_t len = 0; len <= sizeof(msg); len++)
    {
        uint8_t ref[32];
        sha256(msg, len, ref);
        for (size_t split = 0; split <= len; split++)
        {
            Sha256Ctx c;
            uint8_t d[32];
            sha256Init(c);
            sha256Update(c, msg, split);
            sha256Update(c, msg + split, len - split);
            sha256Final(c, d);
            TEST_ASSERT_EQUAL_MEMORY(ref, d, 32);
        }
    }
}

void test_sha_byte_at_a_time_equals_oneshot(void)
{
    uint8_t msg[200];
    for (size_t i = 0; i < sizeof(msg); i++)
        msg[i] = (uint8_t)(255 - i);
    uint8_t ref[32], d[32];
    sha256(msg, sizeof(msg), ref);
    Sha256Ctx c;
    sha256Init(c);
    for (size_t i = 0; i < sizeof(msg); i++)
        sha256Update(c, msg + i, 1);
    sha256Final(c, d);
    TEST_ASSERT_EQUAL_MEMORY(ref, d, 32);
}

// RFC 4231 ------------------------------------------------------------------

static void checkHmac(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, const char *want, size_t nbytes)
{
    uint8_t t[32];
    hmacSha256(key, klen, msg, mlen, t);
    expectHex(want, t, nbytes);
}

void test_rfc4231_case1(void)
{
    uint8_t key[20];
    memset(key, 0x0b, sizeof(key));
    checkHmac(key, sizeof(key), U("Hi There"), 8, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
}

void test_rfc4231_case2(void)
{
    const char *m = "what do ya want for nothing?";
    checkHmac(U("Jefe"), 4, U(m), strlen(m), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);
}

void test_rfc4231_case3(void)
{
    uint8_t key[20], data[50];
    memset(key, 0xaa, sizeof(key));
    memset(data, 0xdd, sizeof(data));
    checkHmac(key, sizeof(key), data, sizeof(data), "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", 32);
}

void test_rfc4231_case4(void)
{
    uint8_t key[25], data[50];
    for (size_t i = 0; i < sizeof(key); i++)
        key[i] = (uint8_t)(i + 1);
    memset(data, 0xcd, sizeof(data));
    checkHmac(key, sizeof(key), data, sizeof(data), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b", 32);
}

// case 5: only the leftmost 128 bits are specified
void test_rfc4231_case5_truncated(void)
{
    uint8_t key[20];
    memset(key, 0x0c, sizeof(key));
    const char *m = "Test With Truncation";
    uint8_t t[32];
    hmacSha256(key, sizeof(key), U(m), strlen(m), t);
    expectHex("a3b6167473100ee06e0c796c2955552b", t, 16);
}

void test_rfc4231_case6(void)
{
    uint8_t key[131];
    memset(key, 0xaa, sizeof(key));
    const char *m = "Test Using Larger Than Block-Size Key - Hash Key First";
    checkHmac(key, sizeof(key), U(m), strlen(m), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 32);
}

void test_rfc4231_case7(void)
{
    uint8_t key[131];
    memset(key, 0xaa, sizeof(key));
    const char *m = "This is a test using a larger than block-size key and a larger than block-size data. The key needs to be "
                    "hashed before being used by the HMAC algorithm.";
    checkHmac(key, sizeof(key), U(m), strlen(m), "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2", 32);
}

// RFC 2104: a key longer than the block size is replaced by its hash, so
// HMAC(K) == HMAC(SHA256(K)) for len(K) > 64.
void test_hmac_long_key_equals_hashed_key(void)
{
    uint8_t key[100];
    for (size_t i = 0; i < sizeof(key); i++)
        key[i] = (uint8_t)(i ^ 0x5a);
    uint8_t kh[32], a[32], b[32];
    sha256(key, 65, kh);
    hmacSha256(key, 65, U("msg"), 3, a);
    hmacSha256(kh, 32, U("msg"), 3, b);
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
}

void test_hmac_empty_key_and_message(void)
{
    uint8_t t[32];
    hmacSha256(NULL, 0, NULL, 0, t);
    expectHex("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad", t, 32);
}

// helpers ---------------------------------------------------------------------

void test_hex_lower(void)
{
    const uint8_t in[] = {0x00, 0x01, 0x9f, 0xa0, 0xab, 0xff};
    char out[2 * sizeof(in) + 1];
    memset(out, 'X', sizeof(out));
    hexLower(in, sizeof(in), out);
    TEST_ASSERT_EQUAL_STRING("00019fa0abff", out);

    // prefix only
    hexLower(in, 3, out);
    TEST_ASSERT_EQUAL_STRING("00019f", out);

    // zero bytes: just the terminator
    out[0] = 'X';
    hexLower(in, 0, out);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)out[0]);
}

void test_ct_equal(void)
{
    const uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8];
    memcpy(b, a, sizeof(a));
    TEST_ASSERT_TRUE(ctEqual(a, b, sizeof(a)));
    TEST_ASSERT_TRUE(ctEqual(a, b, 0));

    for (size_t i = 0; i < sizeof(a); i++)
    {
        memcpy(b, a, sizeof(a));
        b[i] ^= 0x80;
        TEST_ASSERT_FALSE(ctEqual(a, b, sizeof(a)));
    }

    // difference beyond n is not seen
    memcpy(b, a, sizeof(a));
    b[7] ^= 1;
    TEST_ASSERT_TRUE(ctEqual(a, b, 7));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha_empty);
    RUN_TEST(test_sha_abc);
    RUN_TEST(test_sha_448bit);
    RUN_TEST(test_sha_896bit);
    RUN_TEST(test_sha_million_a_odd_chunks);
    RUN_TEST(test_sha_zero_length_update_is_noop);
    RUN_TEST(test_sha_final_wipes_context);
    RUN_TEST(test_sha_split_at_every_boundary);
    RUN_TEST(test_sha_byte_at_a_time_equals_oneshot);
    RUN_TEST(test_rfc4231_case1);
    RUN_TEST(test_rfc4231_case2);
    RUN_TEST(test_rfc4231_case3);
    RUN_TEST(test_rfc4231_case4);
    RUN_TEST(test_rfc4231_case5_truncated);
    RUN_TEST(test_rfc4231_case6);
    RUN_TEST(test_rfc4231_case7);
    RUN_TEST(test_hmac_long_key_equals_hashed_key);
    RUN_TEST(test_hmac_empty_key_and_message);
    RUN_TEST(test_hex_lower);
    RUN_TEST(test_ct_equal);
    return UNITY_END();
}
