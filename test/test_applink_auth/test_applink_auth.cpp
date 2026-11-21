#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "applink_auth.h"

void setUp(void) {}
void tearDown(void) {}

static void nonce_seq(uint8_t n[APPLINK_NONCE_LEN])
{
    for (int i = 0; i < APPLINK_NONCE_LEN; i++)
        n[i] = (uint8_t)i;
}

static void to_hex(const uint8_t *b, size_t n, char *o)
{
    hexLower(b, n, o);
}

void test_mac_vector(void)
{
    uint8_t n[16], mac[32];
    char hex[65];
    nonce_seq(n);
    TEST_ASSERT_TRUE(alAuthExpectedMac("secret", n, mac));
    to_hex(mac, 32, hex);
    // python3: hmac.new(b'secret', b'MCAPPLINK1'+bytes(range(16)), sha256).hexdigest()
    TEST_ASSERT_EQUAL_STRING("d9682ef87ae8fab2245f1e213ad150c0c66f4d4eeae96883339c22f3f1e9a4c0", hex);
}

void test_trailing_spaces_stripped(void)
{
    uint8_t n[16], a[32], b[32];
    nonce_seq(n);
    TEST_ASSERT_TRUE(alAuthExpectedMac("secret", n, a));
    TEST_ASSERT_TRUE(alAuthExpectedMac("secret    ", n, b));
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
    TEST_ASSERT_TRUE(alAuthCheck(a, "secret   ", n));
}

void test_empty_password_refused(void)
{
    uint8_t n[16], a[32];
    nonce_seq(n);
    TEST_ASSERT_FALSE(alAuthExpectedMac("", n, a));
    TEST_ASSERT_FALSE(alAuthExpectedMac("      ", n, a));
    TEST_ASSERT_FALSE(alAuthExpectedMac(NULL, n, a));
    memset(a, 0, 32);
    TEST_ASSERT_FALSE(alAuthCheck(a, "", n));
}

void test_challenge_exact(void)
{
    uint8_t n[16];
    char o[256];
    nonce_seq(n);
    size_t l = alAuthChallengeJson(n, "DK5EN-1", "4.35a.11.21", "HELTEC_V3", o, sizeof(o));
    const char *want = "{\"t\":\"chal\",\"v\":1,\"nonce\":\"000102030405060708090a0b0c0d0e0f\","
                       "\"call\":\"DK5EN-1\",\"fw\":\"4.35a.11.21\",\"hw\":\"HELTEC_V3\"}";
    TEST_ASSERT_EQUAL_STRING(want, o);
    TEST_ASSERT_EQUAL_UINT32(strlen(want), l);
}

void test_challenge_escaping_and_small_buffer(void)
{
    uint8_t n[16];
    char o[256];
    nonce_seq(n);
    alAuthChallengeJson(n, "a\"b\\c\n\x01", "f", "h", o, sizeof(o));
    TEST_ASSERT_NOT_NULL(strstr(o, "\"call\":\"a\\\"b\\\\c\",\"fw\""));
    size_t full = alAuthChallengeJson(n, "x", "y", "z", o, sizeof(o));
    char small[64];
    TEST_ASSERT_EQUAL_UINT32(0, alAuthChallengeJson(n, "x", "y", "z", small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT32(0, alAuthChallengeJson(n, "x", "y", "z", o, full)); // no room for NUL
    TEST_ASSERT_EQUAL_UINT32(full, alAuthChallengeJson(n, "x", "y", "z", o, full + 1));
}

void test_small_messages(void)
{
    char o[64];
    TEST_ASSERT_EQUAL_UINT32(strlen("{\"t\":\"ok\",\"mtu\":257}"), alAuthOkJson(257, o, sizeof(o)));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"ok\",\"mtu\":257}", o);
    alAuthOkJson(0, o, sizeof(o));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"ok\",\"mtu\":0}", o);
    alAuthErrJson("auth", o, sizeof(o));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"err\",\"code\":\"auth\"}", o);
    alHbJson(o, sizeof(o));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"hb\"}", o);
    alByeJson("takeover", o, sizeof(o));
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"bye\",\"reason\":\"takeover\"}", o);
    TEST_ASSERT_EQUAL_UINT32(0, alHbJson(o, 5));
}

static const char *HEX64 = "d9682ef87ae8fab2245f1e213ad150c0c66f4d4eeae96883339c22f3f1e9a4c0";

static AlCtl parse(const char *s, uint8_t *mac)
{
    return alCtlParse(s, strlen(s), mac);
}

void test_parse_auth_ok_and_uppercase(void)
{
    uint8_t mac[32], n[16], want[32];
    nonce_seq(n);
    alAuthExpectedMac("secret", n, want);
    char j[200];
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", HEX64);
    memset(mac, 0, 32);
    TEST_ASSERT_EQUAL_INT(AL_CTL_AUTH, parse(j, mac));
    TEST_ASSERT_EQUAL_MEMORY(want, mac, 32);
    TEST_ASSERT_TRUE(alAuthCheck(mac, "secret", n));
    char up[65];
    for (int i = 0; i < 65; i++)
        up[i] = (HEX64[i] >= 'a' && HEX64[i] <= 'f') ? (char)(HEX64[i] - 32) : HEX64[i];
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", up);
    memset(mac, 0, 32);
    TEST_ASSERT_EQUAL_INT(AL_CTL_AUTH, parse(j, mac));
    TEST_ASSERT_EQUAL_MEMORY(want, mac, 32);
}

void test_parse_bad_mac_lengths_and_chars(void)
{
    uint8_t mac[32];
    char j[200];
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%.63s\"}", HEX64);
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse(j, mac));
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s0\"}", HEX64);
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse(j, mac));
    // exactly 64 chars, one of them not a hex digit (length check passes, only the hex check can reject)
    static const struct
    {
        size_t pos;
        char c;
    } badAt[] = {{0, 'z'}, {1, 'z'}, {31, 'g'}, {32, ' '}, {62, 'G'}, {63, 'z'}, {63, ' '}, {10, '-'}, {20, '\\'}};
    for (size_t b = 0; b < sizeof(badAt) / sizeof(badAt[0]); b++)
    {
        char m[65];
        memcpy(m, HEX64, 65);
        TEST_ASSERT_EQUAL_UINT32(64, strlen(m));
        m[badAt[b].pos] = badAt[b].c;
        TEST_ASSERT_EQUAL_UINT32(64, strlen(m));
        snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", m);
        TEST_ASSERT_EQUAL_INT_MESSAGE(AL_CTL_BAD, parse(j, mac), m);
    }
    // the unmodified 64 chars still parse
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", HEX64);
    TEST_ASSERT_EQUAL_INT(AL_CTL_AUTH, parse(j, mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{\"t\":\"auth\"}", mac));
}

void test_parse_hb_unknown_garbage(void)
{
    uint8_t mac[32];
    TEST_ASSERT_EQUAL_INT(AL_CTL_HB, parse("{\"t\":\"hb\"}", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_UNKNOWN, parse("{\"t\":\"zzz\"}", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_UNKNOWN, parse("{\"t\":5}", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{\"x\":1}", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("garbage", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{\"t\":\"hb\"", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{\"t\":\"hb", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, parse("{}", mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_BAD, alCtlParse(NULL, 3, mac));
}

void test_parse_whitespace_order_unknown_keys(void)
{
    uint8_t mac[32];
    char j[300];
    snprintf(j, sizeof(j),
             " {\n \"x\" : [1,{\"a\":\"}\"}] , \"mac\" :\t\"%s\" ,\"y\":null, \"t\" : \"auth\" } ", HEX64);
    TEST_ASSERT_EQUAL_INT(AL_CTL_AUTH, parse(j, mac));
    TEST_ASSERT_EQUAL_INT(AL_CTL_HB, parse("{ \"extra\":true , \"t\" : \"hb\" }", mac));
}

void test_check_wrong_mac_and_password(void)
{
    uint8_t n[16], mac[32];
    nonce_seq(n);
    alAuthExpectedMac("secret", n, mac);
    TEST_ASSERT_TRUE(alAuthCheck(mac, "secret", n));
    TEST_ASSERT_FALSE(alAuthCheck(mac, "Secret", n));
    mac[31] ^= 1;
    TEST_ASSERT_FALSE(alAuthCheck(mac, "secret", n));
    mac[31] ^= 1;
    n[0] ^= 1;
    TEST_ASSERT_FALSE(alAuthCheck(mac, "secret", n));
}

void test_lockout_four_fails_no_lock_fifth_locks(void)
{
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, 1000 + i * 1000);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 5000));
    alLockoutFail(&l, 5000);
    TEST_ASSERT_TRUE(alLockoutActive(&l, 5001));
    TEST_ASSERT_EQUAL_UINT32(60000 - 1, alLockoutRemainingMs(&l, 5001));
}

void test_lockout_spread_fails_do_not_lock(void)
{
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 20; i++)
        alLockoutFail(&l, i * 20000); // 3 per minute at most
    TEST_ASSERT_FALSE(alLockoutActive(&l, 20 * 20000));
}

void test_lockout_expires_after_60s(void)
{
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 5; i++)
        alLockoutFail(&l, 100 + i);
    TEST_ASSERT_TRUE(alLockoutActive(&l, 100 + 4 + 59999));
    TEST_ASSERT_FALSE(alLockoutActive(&l, 104 + 60000));
    TEST_ASSERT_EQUAL_UINT32(0, alLockoutRemainingMs(&l, 104 + 60000));
    // fresh count after expiry: 4 more fails do not re-lock
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, 70000 + i);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 70010));
}

void test_lockout_millis_wrap(void)
{
    AlLockout l;
    alLockoutInit(&l);
    uint32_t t = 0xFFFFFFFFu - 2000u;
    for (int i = 0; i < 5; i++)
        alLockoutFail(&l, t + (uint32_t)i * 1000u); // crosses the wrap
    TEST_ASSERT_TRUE(alLockoutActive(&l, t + 5000u));
    TEST_ASSERT_TRUE(alLockoutActive(&l, t + 4000u + 59999u));
    TEST_ASSERT_FALSE(alLockoutActive(&l, t + 4000u + 60000u));
}

void test_lockout_stale_failures_pruned_by_poll_before_wrap(void)
{
    // probe: 4 failures at t=0..3, a full 2^32 ms wrap passes, one more failure at t=100
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, i);
    // every connection attempt polls alLockoutActive; poll once per 2^28 ms (about 3 days) across the wrap
    for (uint32_t t = 1u << 28; t != 0; t += (1u << 28))
        TEST_ASSERT_FALSE(alLockoutActive(&l, t));
    alLockoutFail(&l, 100);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 101));
    // and the lockout still works afterwards
    for (uint32_t i = 1; i < 5; i++)
        alLockoutFail(&l, 100 + i);
    TEST_ASSERT_TRUE(alLockoutActive(&l, 110));
}

void test_lockout_poll_inside_window_keeps_failures(void)
{
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, 1000 + i);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 30000));  // poll must not forget fresh failures
    alLockoutFail(&l, 59999);                       // 5th inside 60 s of the first
    TEST_ASSERT_TRUE(alLockoutActive(&l, 60000));
    // poll past the window of 4 failures drops them
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, 1000 + i);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 1000 + 60000));
    alLockoutFail(&l, 62000);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 62001));
}

void test_lockout_success_clears(void)
{
    AlLockout l;
    alLockoutInit(&l);
    for (uint32_t i = 0; i < 4; i++)
        alLockoutFail(&l, 1000 + i);
    alLockoutSuccess(&l);
    alLockoutFail(&l, 2000);
    TEST_ASSERT_FALSE(alLockoutActive(&l, 2001));
    TEST_ASSERT_EQUAL_UINT8(1, l.n);
}

// Contract: "hw" is the board name ("HELTEC_V3"). The firmware's name table is nbrHardwareName(); where it has no name
// for the board ("no info", or nothing) the node sends the board number instead (operator decision 2026-11-21).
void test_hw_label_name_or_number(void)
{
    char buf[24];
    TEST_ASSERT_EQUAL_STRING("HELTEC_V3", alHwLabel("HELTEC_V3", 43, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("RAK4631", alHwLabel("RAK4631", 9, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("62", alHwLabel("no info", 62, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("62", alHwLabel("", 62, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("62", alHwLabel(NULL, 62, buf, sizeof(buf)));
    // the longest names in the table fit the glue buffers (24 B)
    TEST_ASSERT_EQUAL_STRING("ESP32_LORAPRS_RA01", alHwLabel("ESP32_LORAPRS_RA01", 34, buf, sizeof(buf)));
    // a too small buffer truncates instead of overflowing
    char small[4];
    TEST_ASSERT_EQUAL_STRING("HEL", alHwLabel("HELTEC_V3", 43, small, sizeof(small)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_mac_vector);
    RUN_TEST(test_trailing_spaces_stripped);
    RUN_TEST(test_empty_password_refused);
    RUN_TEST(test_challenge_exact);
    RUN_TEST(test_challenge_escaping_and_small_buffer);
    RUN_TEST(test_small_messages);
    RUN_TEST(test_parse_auth_ok_and_uppercase);
    RUN_TEST(test_parse_bad_mac_lengths_and_chars);
    RUN_TEST(test_parse_hb_unknown_garbage);
    RUN_TEST(test_parse_whitespace_order_unknown_keys);
    RUN_TEST(test_check_wrong_mac_and_password);
    RUN_TEST(test_lockout_four_fails_no_lock_fifth_locks);
    RUN_TEST(test_lockout_spread_fails_do_not_lock);
    RUN_TEST(test_lockout_expires_after_60s);
    RUN_TEST(test_lockout_millis_wrap);
    RUN_TEST(test_lockout_stale_failures_pruned_by_poll_before_wrap);
    RUN_TEST(test_lockout_poll_inside_window_keeps_failures);
    RUN_TEST(test_lockout_success_clears);
    RUN_TEST(test_hw_label_name_or_number);
    return UNITY_END();
}
