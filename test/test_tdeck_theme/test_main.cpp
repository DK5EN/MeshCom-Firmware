// TD-THEME-01: palette contrast, auto/dark/light resolve table, --theme parser.
#include <stdio.h>
#include <unity.h>
#include <t-deck/tdeck_palette.h>

void setUp(void) {}
void tearDown(void) {}

static void check_pair(const char* mode, const char* name, uint32_t fg, uint32_t bg, double min)
{
    double c = tdeck_contrast(fg, bg);
    char msg[96];
    snprintf(msg, sizeof(msg), "%s %s %.2f < %.1f", mode, name, c, min);
    TEST_ASSERT_TRUE_MESSAGE(c >= min, msg);
}

static void check_palette(bool dark)
{
    const TdeckPalette& p = tdeck_palette(dark);
    const char* m = dark ? "dark" : "light";
    check_pair(m, "text/bg", p.text, p.bg, 4.5);
    check_pair(m, "text/surface", p.text, p.surface, 4.5);
    check_pair(m, "muted/bg", p.muted, p.bg, 4.5);
    check_pair(m, "muted/surface", p.muted, p.surface, 4.5);
    check_pair(m, "header_fg/header_bg", p.header_fg, p.header_bg, 4.5);
    check_pair(m, "warn/header_bg", p.warn, p.header_bg, 4.5);
    check_pair(m, "warn_bg/bg", p.warn_bg, p.bg, 4.5);
    check_pair(m, "warn_bg/surface", p.warn_bg, p.surface, 4.5);
    check_pair(m, "btn_fg_off/btn_off", p.btn_fg_off, p.btn_off, 4.5);
    check_pair(m, "header_fg/btn_on", p.header_fg, p.btn_on, 4.5);
    check_pair(m, "header_fg/tab_sel", p.header_fg, p.tab_sel, 4.5);
    check_pair(m, "text/bubble_in", p.text, p.bubble_in, 4.5);
    check_pair(m, "text/bubble_out", p.text, p.bubble_out, 4.5);
    check_pair(m, "accent/bg", p.accent, p.bg, 4.5);
    check_pair(m, "ok/bg", p.ok, p.bg, 4.5);
    check_pair(m, "border/bg", p.border, p.bg, 3.0);
    check_pair(m, "info/surface", p.info, p.surface, 3.0);
    check_pair(m, "tab_sel/tab_bg", p.tab_sel, p.tab_bg, 3.0);
    check_pair(m, "text/tab_bg", p.text, p.tab_bg, 4.5);
    check_pair(m, "ok/surface", p.ok, p.surface, 3.0);
    check_pair(m, "accent/surface", p.accent, p.surface, 4.5);
    check_pair(m, "muted/bubble_in", p.muted, p.bubble_in, 4.5);
    check_pair(m, "muted/bubble_out", p.muted, p.bubble_out, 4.5);
    // LVGL theme primary draws focus outlines, checked switches, checkboxes, slider indicator: state indicator, 3:1
    check_pair(m, "primary/bg", p.primary, p.bg, 3.0);
    check_pair(m, "primary/surface", p.primary, p.surface, 3.0);
    check_pair(m, "hdr_on/header_bg", p.hdr_on, p.header_bg, 3.0);
    check_pair(m, "hdr_off/header_bg", p.hdr_off, p.header_bg, 3.0);
    check_pair(m, "hdr_err/header_bg", p.hdr_err, p.header_bg, 3.0);
    // on/off/err must tell apart on the red header (luminance step; err also differs by hue)
    check_pair(m, "hdr_on/hdr_off", p.hdr_on, p.hdr_off, 1.4);
    check_pair(m, "hdr_on/hdr_err", p.hdr_on, p.hdr_err, 1.4);
    check_pair(m, "hdr_off/hdr_err", p.hdr_off, p.hdr_err, 1.4);
    check_pair(m, "text/bubble_sys", p.text, p.bubble_sys, 4.5);
    check_pair(m, "muted/bubble_sys", p.muted, p.bubble_sys, 4.5);
}

void test_contrast_dark(void) { check_palette(true); }
void test_contrast_light(void) { check_palette(false); }

void test_contrast_function(void)
{
    TEST_ASSERT_TRUE(fabs(tdeck_contrast(0x000000, 0xFFFFFF) - 21.0) < 0.01);
    TEST_ASSERT_TRUE(fabs(tdeck_contrast(0x123456, 0x123456) - 1.0) < 0.001);
}

void test_resolve_table(void)
{
    const uint8_t nights[3] = {NIGHT_UNKNOWN, NIGHT_DAY, NIGHT_NIGHT};
    for(int i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(tdeck_theme_resolve_dark(THEME_DARK, nights[i]));
        TEST_ASSERT_FALSE(tdeck_theme_resolve_dark(THEME_LIGHT, nights[i]));
    }
    TEST_ASSERT_TRUE(tdeck_theme_resolve_dark(THEME_AUTO, NIGHT_UNKNOWN));
    TEST_ASSERT_FALSE(tdeck_theme_resolve_dark(THEME_AUTO, NIGHT_DAY));
    TEST_ASSERT_TRUE(tdeck_theme_resolve_dark(THEME_AUTO, NIGHT_NIGHT));
}

void test_resolve_out_of_range_is_auto(void)
{
    TEST_ASSERT_TRUE(tdeck_theme_resolve_dark(3, NIGHT_UNKNOWN));
    TEST_ASSERT_FALSE(tdeck_theme_resolve_dark(3, NIGHT_DAY));
    TEST_ASSERT_TRUE(tdeck_theme_resolve_dark(255, NIGHT_NIGHT));
}

void test_parse(void)
{
    TEST_ASSERT_EQUAL_INT(0, tdeck_theme_parse("auto"));
    TEST_ASSERT_EQUAL_INT(1, tdeck_theme_parse("dark"));
    TEST_ASSERT_EQUAL_INT(2, tdeck_theme_parse("light"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse(NULL));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse(""));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse("Dark"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse("dark "));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse("dar"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_theme_parse("1"));
}

void test_name(void)
{
    TEST_ASSERT_EQUAL_STRING("auto", tdeck_theme_name(0));
    TEST_ASSERT_EQUAL_STRING("dark", tdeck_theme_name(1));
    TEST_ASSERT_EQUAL_STRING("light", tdeck_theme_name(2));
    TEST_ASSERT_EQUAL_STRING("auto", tdeck_theme_name(9));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_contrast_dark);
    RUN_TEST(test_contrast_light);
    RUN_TEST(test_contrast_function);
    RUN_TEST(test_resolve_table);
    RUN_TEST(test_resolve_out_of_range_is_auto);
    RUN_TEST(test_parse);
    RUN_TEST(test_name);
    return UNITY_END();
}
