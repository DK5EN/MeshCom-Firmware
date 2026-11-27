// mcHexVal (src/hex_digit.h): the one hex-digit decoder. Exhaustive over all 256 char values,
// against frozen copies of the nine decoders it replaced (FORMAL-02 step 1).
//
//   pio test -e native_hex_digit   (host only)

#include <unity.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hex_digit.h>

void setUp(void) {}
void tearDown(void) {}

// ---- frozen copies of the replaced decoders (do not edit) ----------------------------------------
// url_decode.cpp hex_value, applink_auth.h alHexVal, fw_update_assets.h hexVal, gps_functions.cpp
// nmeaHexVal (order of branches differs), test_inject.cpp hexNibble: same shape.
static int old_url_decode(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
static int old_applink(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
// fw_update_http.h hexv: uint8_t argument
static int old_fw_http(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
// ext_reg.h inline chain: -1 stands for "not a digit" (the else branch)
static int old_ext_reg(char h)
{
    unsigned v = 0;
    if (h >= '0' && h <= '9')
        v |= (unsigned)(h - '0');
    else if (h >= 'a' && h <= 'f')
        v |= (unsigned)(h - 'a' + 10);
    else if (h >= 'A' && h <= 'F')
        v |= (unsigned)(h - 'A' + 10);
    else
        return -1;
    return (int)v;
}
// web_rm_parse.h isHex + hexVal: -1 when !isHex
static bool old_rm_isHex(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static unsigned old_rm_hexVal(unsigned char c)
{
    return c <= '9' ? (unsigned)(c - '0') : (unsigned)((c | 0x20) - 'a' + 10);
}
// web_rm_util.h rm_form_decode (dead): isxdigit + strtoul
static int old_rm_util(char c)
{
    if (!isxdigit((unsigned char)c))
        return -1;
    const char h[2] = {c, 0};
    return (int)strtoul(h, nullptr, 16);
}

static void test_all_256_values_against_old_decoders(void)
{
    int valid = 0;
    for (int i = 0; i < 256; i++)
    {
        const char c = (char)i;
        const int v = mcHexVal(c);
        char msg[32];
        snprintf(msg, sizeof(msg), "char value %d", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(old_url_decode(c), v, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(old_applink(c), v, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(old_fw_http((uint8_t)c), v, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(old_ext_reg(c), v, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(old_rm_util(c), v, msg);
        if (old_rm_isHex((unsigned char)c))
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)old_rm_hexVal((unsigned char)c), v, msg);
        else
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, v, msg);
        if (v >= 0)
            valid++;
    }
    TEST_ASSERT_EQUAL_INT(22, valid); // 10 digits + 6 + 6
}

static void test_range_and_exact_set(void)
{
    for (int i = 0; i < 256; i++)
    {
        const int v = mcHexVal((char)i);
        const bool hex = (i >= '0' && i <= '9') || (i >= 'a' && i <= 'f') || (i >= 'A' && i <= 'F');
        TEST_ASSERT_TRUE(v >= -1 && v <= 15);
        TEST_ASSERT_EQUAL(hex, v >= 0);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_256_values_against_old_decoders);
    RUN_TEST(test_range_and_exact_set);
    return UNITY_END();
}
