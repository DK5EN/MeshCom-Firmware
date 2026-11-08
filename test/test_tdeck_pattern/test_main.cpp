// DISP-PAT-01: --tdpattern colour generators and argument parser (LVGL-free).
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>
#include <t-deck/tdeck_pattern.h>

void setUp(void) {}
void tearDown(void) {}

static int ch(uint32_t c, int s) { return (int)((c >> s) & 0xFF); }
static long lum(uint32_t c) { return 2126L * ch(c, 16) + 7152L * ch(c, 8) + 722L * ch(c, 0); }

static void test_grey_ends(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x000000, tdeck_pat_grey(0));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, tdeck_pat_grey(31));
}

static void test_grey_monotonic_neutral_on_grid(void)
{
    uint32_t prev = 0;
    for(uint8_t i = 0; i < 32; i++)
    {
        uint32_t c = tdeck_pat_grey(i);
        TEST_ASSERT_EQUAL_HEX32(tdeck_rgb565(c), c);
        TEST_ASSERT_EQUAL_INT(ch(c, 16), ch(c, 0));
        TEST_ASSERT_TRUE(abs(ch(c, 8) - ch(c, 16)) <= 4);
        if(i > 0)
            TEST_ASSERT_TRUE(lum(c) > lum(prev));
        prev = c;
    }
}

static void test_ramps(void)
{
    for(uint8_t h = 0; h < 6; h++)
    {
        long pd = -1, pl = -1;
        for(uint8_t k = 0; k < 8; k++)
        {
            uint32_t d = tdeck_pat_ramp_dark(h, k), l = tdeck_pat_ramp_light(h, k);
            TEST_ASSERT_EQUAL_HEX32(tdeck_rgb565(d), d);
            TEST_ASSERT_EQUAL_HEX32(tdeck_rgb565(l), l);
            TEST_ASSERT_TRUE(lum(d) > pd);
            TEST_ASSERT_TRUE(lum(l) > pl);
            pd = lum(d);
            pl = lum(l);
        }
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, tdeck_pat_ramp_light(h, 7));
    }
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, tdeck_pat_ramp_dark(0, 7));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, tdeck_pat_ramp_dark(1, 7));
    TEST_ASSERT_EQUAL_HEX32(0x0000FF, tdeck_pat_ramp_dark(2, 7));
    TEST_ASSERT_EQUAL_HEX32(0x00FFFF, tdeck_pat_ramp_dark(3, 7));
    TEST_ASSERT_EQUAL_HEX32(0xFF00FF, tdeck_pat_ramp_dark(4, 7));
    TEST_ASSERT_EQUAL_HEX32(0xFFFF00, tdeck_pat_ramp_dark(5, 7));
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, tdeck_pat_ramp_light(0, 0));
    TEST_ASSERT_EQUAL_HEX32(0xFFFF00, tdeck_pat_ramp_light(5, 0));
}

static void test_parse(void)
{
    int p = -9, b = -9;
    TEST_ASSERT_EQUAL_INT(0, tdeck_pat_parse("2", &p, &b));
    TEST_ASSERT_EQUAL_INT(2, p);
    TEST_ASSERT_EQUAL_INT(0, b);
    TEST_ASSERT_EQUAL_INT(0, tdeck_pat_parse(" 4 bl 16", &p, &b));
    TEST_ASSERT_EQUAL_INT(4, p);
    TEST_ASSERT_EQUAL_INT(16, b);
    TEST_ASSERT_EQUAL_INT(0, tdeck_pat_parse("0  bl 1\r\n", &p, &b));
    TEST_ASSERT_EQUAL_INT(0, p);
    TEST_ASSERT_EQUAL_INT(1, b);
    TEST_ASSERT_EQUAL_INT(1, tdeck_pat_parse(" off", &p, &b));
    TEST_ASSERT_EQUAL_INT(1, tdeck_pat_parse("off\n", &p, &b));
}

static void test_parse_rejects(void)
{
    int p, b;
    const char* bad[] = {NULL, "", " ", "5", "-1", "10", "x", "2 bl", "2 bl 0", "2 bl 17", "2 bl x", "2 bl 8 9",
                         "2 8", "2 BL 8", "OFF", "off 1", "2x", "2 bl 08x", "bl 8", "1 2"};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        char m[48];
        snprintf(m, sizeof(m), "case %u", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, tdeck_pat_parse(bad[i], &p, &b), m);
    }
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_grey_ends);
    RUN_TEST(test_grey_monotonic_neutral_on_grid);
    RUN_TEST(test_ramps);
    RUN_TEST(test_parse);
    RUN_TEST(test_parse_rejects);
    return UNITY_END();
}
