// Native-Tests fuer src/cet_age.h (Kampagne TXP, W0).
#include <unity.h>
#include <string.h>
#include "cet_age.h"

void setUp(void) {}
void tearDown(void) {}

static const char *P = "{CET}2026-10-24 12:00:00";

static uint16_t buildText(uint8_t *b, const char *src, const char *dst, const char *payload, uint8_t type = 0x3A)
{
    uint16_t n = 0;
    b[n++] = type;
    b[n++] = 1; b[n++] = 2; b[n++] = 3; b[n++] = 4;
    b[n++] = 0x14;
    memcpy(b + n, src, strlen(src)); n += (uint16_t)strlen(src);
    b[n++] = '>';
    memcpy(b + n, dst, strlen(dst)); n += (uint16_t)strlen(dst);
    b[n++] = 0x3A;
    memcpy(b + n, payload, strlen(payload)); n += (uint16_t)strlen(payload);
    b[n++] = 0;
    return n;
}

void test_parse_valid_known_epoch(void)
{
    uint32_t e = 0;
    TEST_ASSERT_TRUE(cetParsePayload(P, strlen(P), &e));
    TEST_ASSERT_EQUAL_UINT32(1792843200UL, e);   // python calendar.timegm
}

void test_parse_leap_day(void)
{
    uint32_t e = 0;
    const char *s = "{CET}2028-02-29 00:00:01";
    TEST_ASSERT_TRUE(cetParsePayload(s, strlen(s), &e));
    TEST_ASSERT_EQUAL_UINT32(1835395201UL, e);
    const char *bad = "{CET}2027-02-29 00:00:01";
    TEST_ASSERT_FALSE(cetParsePayload(bad, strlen(bad), &e));
}

void test_parse_rejects(void)
{
    uint32_t e = 0;
    const char *c[] = {
        "{CET}<2026-10-24 12:00:00", "{CET}2026-10-24 12:00", "{CET}", "",
        "{CET}2026-13-24 12:00:00", "{CET}2026-00-24 12:00:00", "{CET}2026-10-32 12:00:00",
        "{CET}2026-10-00 12:00:00", "{CET}2026-10-24 24:00:00", "{CET}2026-10-24 12:60:00",
        "{CET}2026-10-24 12:00:60", "{CET}2023-12-31 23:59:59", "{CET}2026/10/24 12:00:00",
        "{SET}2026-10-24 12:00:00", "{CET}2026-1x-24 12:00:00"};
    for(size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(cetParsePayload(c[i], strlen(c[i]), &e), c[i]);
    TEST_ASSERT_FALSE(cetParsePayload(NULL, 30, &e));
}

void test_age_sign(void)
{
    TEST_ASSERT_EQUAL_INT32(30, cetAgeS(1000030, 1000000));
    TEST_ASSERT_EQUAL_INT32(-5, cetAgeS(1000000, 1000005));
    TEST_ASSERT_EQUAL_INT32(0, cetAgeS(7, 7));
}

void test_trusted_truth_table(void)
{
    for(int m = 0; m < 8; m++)
        TEST_ASSERT_EQUAL(m != 0, cetClockTrusted(m & 1, m & 2, m & 4));
}

void test_find_in_frame(void)
{
    uint8_t b[300];
    uint32_t e = 0;
    uint16_t n = buildText(b, "OE1XXX-1,DB0AAA", "*", P);
    TEST_ASSERT_TRUE(cetFindInFrame(b, n, &e));
    TEST_ASSERT_EQUAL_UINT32(1792843200UL, e);

    n = buildText(b, "DK5EN-90", "*", "Hallo Welt");
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));

    n = buildText(b, "DK5EN-90", "*", P, 0x21);   // Position, kein Text
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));

    n = buildText(b, "OE1XXX-1", "*", "{CET}<2026-10-24 12:00:00");
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));
    TEST_ASSERT_FALSE(cetFindInFrame(b, 5, &e));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid_known_epoch);
    RUN_TEST(test_parse_leap_day);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_age_sign);
    RUN_TEST(test_trusted_truth_table);
    RUN_TEST(test_find_in_frame);
    return UNITY_END();
}
