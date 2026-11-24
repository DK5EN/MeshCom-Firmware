// T114-DARK-01 (#1314): palette values, contrast, and source pins.
#include <unity.h>
#include "../support/repo_file.h"
#include <math.h>
#include <string>
#include <fstream>
#include <sstream>
#include "t114_theme.h"

void setUp() {}
void tearDown() {}

static double lin(double c) { c /= 255.0; return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double lum(uint16_t c)
{
    double r = ((c >> 11) & 31) * 255.0 / 31, g = ((c >> 5) & 63) * 255.0 / 63, b = (c & 31) * 255.0 / 31;
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}
static double contrast(uint16_t a, uint16_t b)
{
    double l1 = lum(a), l2 = lum(b);
    if (l1 < l2) { double t = l1; l1 = l2; l2 = t; }
    return (l1 + 0.05) / (l2 + 0.05);
}

static std::string slurp(const char *p)
{
    return repo_slurp(p);
}

void test_light_is_todays_colours()
{
    const T114Palette &p = t114Palette(false);
    TEST_ASSERT_EQUAL_HEX16(0xF800, p.header);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, p.headerText);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, p.body);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p.bodyText);
}

void test_dark_body_black_text_light()
{
    const T114Palette &p = t114Palette(true);
    TEST_ASSERT_TRUE(lum(p.body) < 0.02);
    TEST_ASSERT_TRUE(contrast(p.bodyText, p.body) >= 4.5);
    TEST_ASSERT_TRUE(contrast(p.headerText, p.header) >= 4.5);
    TEST_ASSERT_TRUE(contrast(p.bad, p.body) >= 4.5);
    TEST_ASSERT_TRUE(contrast(p.good, p.body) >= 4.5);
    TEST_ASSERT_TRUE(contrast(p.warn, p.body) >= 4.5);
}

void test_dark_header_reads_red_and_differs_from_light()
{
    const T114Palette &d = t114Palette(true), &l = t114Palette(false);
    TEST_ASSERT_TRUE(d.header != l.header);
    TEST_ASSERT_TRUE(lum(d.header) < lum(l.header));
    TEST_ASSERT_TRUE((d.header >> 11) >= 16);          // red dominant
    TEST_ASSERT_EQUAL_HEX16(0, d.header & 0x07FF);     // no green/blue
}

void test_light_text_contrast()
{
    const T114Palette &p = t114Palette(false);
    TEST_ASSERT_TRUE(contrast(p.bodyText, p.body) >= 4.5);
    TEST_ASSERT_TRUE(contrast(p.headerText, p.header) >= 3.9);
}

void test_pin_tft_114_branch_has_no_colour_literal()
{
    std::string s = slurp("src/tft_display_functions.cpp");
    size_t a = s.find("#if defined(HAS_TFT_114)");
    TEST_ASSERT_TRUE(a != std::string::npos);
    std::string b = s.substr(a);
    TEST_ASSERT_TRUE(b.find("ST77XX_RED") == std::string::npos);
    TEST_ASSERT_TRUE(b.find("ST77XX_WHITE") == std::string::npos);
    TEST_ASSERT_TRUE(b.find("ST77XX_BLACK") == std::string::npos);
    TEST_ASSERT_TRUE(b.find("t114Palette(") != std::string::npos);
}

void test_pin_gps_page_has_no_raw_colour()
{
    std::string s = slurp("src/nrf52/t114_gps_page.cpp");
    TEST_ASSERT_TRUE(s.find("t114Palette(") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("#define C_") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("ST77XX_") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("C_WHITE") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("C_BLACK") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("C_RED") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("0xFFFF") == std::string::npos);
    TEST_ASSERT_TRUE(s.find("0xF800") == std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_light_is_todays_colours);
    RUN_TEST(test_dark_body_black_text_light);
    RUN_TEST(test_dark_header_reads_red_and_differs_from_light);
    RUN_TEST(test_light_text_contrast);
    RUN_TEST(test_pin_tft_114_branch_has_no_colour_literal);
    RUN_TEST(test_pin_gps_page_has_no_raw_colour);
    return UNITY_END();
}
