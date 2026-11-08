// TD-THEME-01: palette contrast, auto/dark/light resolve table, --theme parser.
#include <stdio.h>
#include <unity.h>
#include <t-deck/tdeck_palette.h>

void setUp(void) {}
void tearDown(void) {}

static void check_pair(const char* mode, const char* name, uint32_t fg, uint32_t bg, double min)
{
    // judged on the RGB565 colour the panel shows, not on the 24-bit token
    double c = tdeck_contrast(tdeck_rgb565(fg), tdeck_rgb565(bg));
    char msg[96];
    snprintf(msg, sizeof(msg), "%s %s %.2f < %.2f", mode, name, c, min);
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

// Regression (T-Deck photos 2026-11-08): in light mode the bubbles vanished into the white chat area. The chat
// area now has its own blue-grey chat_bg and the bubbles are light on it; the outline carries WCAG 1.4.11 (3:1),
// the fill only has to be visibly distinct (1.2:1). Muted on chat_bg is the "No messages yet" hint.
void test_light_bubbles_stand_out(void)
{
    const TdeckPalette& p = tdeck_palette(false);
    const double min = 1.2;
    check_pair("light", "bubble_in/chat_bg", p.bubble_in, p.chat_bg, min);
    check_pair("light", "bubble_out/chat_bg", p.bubble_out, p.chat_bg, min);
    check_pair("light", "bubble_sys/chat_bg", p.bubble_sys, p.chat_bg, min);
    check_pair("light", "muted/chat_bg", p.muted, p.chat_bg, 4.5);
    check_pair("light", "text/chat_bg", p.text, p.chat_bg, 4.5);
    check_pair("light", "border/chat_bg", p.bubble_border, p.chat_bg, 3.0);
    check_pair("dark", "muted/chat_bg", tdeck_palette(true).muted, tdeck_palette(true).chat_bg, 4.5);
    // dark chat area must look as before: the chat tab showed LVGL's dark screen colour 0x15171A
    TEST_ASSERT_EQUAL_HEX32(tdeck_rgb565(0x15171A), tdeck_rgb565(tdeck_palette(true).chat_bg));
}

// DISP-PAT-01 panel photos (2026-11-08): near white only every third RGB565 step is visible, received and sent
// bubbles (white vs pale pink) were 1.02:1 on the panel, and 0x181818 did not read as black. Operator decision:
// light text and outline pure black, muted much darker, every chat fill >= 3 steps from every other.
void test_light_panel_measured(void)
{
    const TdeckPalette& p = tdeck_palette(false);
    TEST_ASSERT_EQUAL_HEX32(0x000000, p.text);
    TEST_ASSERT_EQUAL_HEX32(0x000000, p.bubble_border);
    // operator 2026-11-08 (RAW photos): the sent bubble 0xCEF3CE read as white, its tint must be unmistakable
    {
        char msg[64];
        const double st = tdeck_rgb565_steps(p.bubble_out, 0xFFFFFF);
        snprintf(msg, sizeof(msg), "bubble_out/white %.1f steps < 8", st);
        TEST_ASSERT_TRUE_MESSAGE(st >= 8.0, msg);
    }
    check_pair("light", "muted/bubble_in", p.muted, p.bubble_in, 7.0);
    check_pair("light", "muted/chat_bg", p.muted, p.chat_bg, 7.0);
    const uint32_t fills[] = {p.chat_bg, p.bubble_in, p.bubble_out, p.bubble_sys};
    const char* names[] = {"chat_bg", "bubble_in", "bubble_out", "bubble_sys"};
    for(int i = 0; i < 4; i++)
        for(int j = i + 1; j < 4; j++)
        {
            char msg[64];
            const double st = tdeck_rgb565_steps(fills[i], fills[j]);
            snprintf(msg, sizeof(msg), "%s/%s %.1f steps < 3", names[i], names[j], st);
            TEST_ASSERT_TRUE_MESSAGE(st >= 3.0, msg);
        }
}

void test_rgb565_steps(void)
{
    TEST_ASSERT_TRUE(tdeck_rgb565_steps(0xFFFFFF, 0xFFFFFF) == 0.0);
    TEST_ASSERT_TRUE(tdeck_rgb565_steps(0xFFFFFF, 0x000000) == 31.5 || tdeck_rgb565_steps(0xFFFFFF, 0x000000) == 31.0);
    TEST_ASSERT_TRUE(tdeck_rgb565_steps(0xFFFFFF, 0xE7FFFF) == 3.0); // red 31 -> 28
}

// Operator decision 2026-11-08 (photo IMG_9963): light bubbles get a near-black outline, dark ones none.
void test_bubble_border(void)
{
    const TdeckPalette& l = tdeck_palette(false);
    // operator 2026-11-08 (IMG_9970): 2 px was too thin on the panel; group buttons carry the same outline,
    // and their pad (6 px) and the bubble pad (8 px) absorb it, so the width must stay <= 6
    TEST_ASSERT_TRUE(l.bubble_border_w >= 3);
    TEST_ASSERT_TRUE(l.bubble_border_w <= 6);
    check_pair("light", "bubble_border/chat_bg", l.bubble_border, l.chat_bg, 3.0);
    check_pair("light", "bubble_border/bubble_in", l.bubble_border, l.bubble_in, 3.0);
    check_pair("light", "bubble_border/bubble_out", l.bubble_border, l.bubble_out, 3.0);
    check_pair("light", "bubble_border/bubble_sys", l.bubble_border, l.bubble_sys, 3.0);
    TEST_ASSERT_EQUAL_UINT8(0, tdeck_palette(true).bubble_border_w);
}

void test_rgb565_quantise(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, tdeck_rgb565(0xFFFFFF));
    TEST_ASSERT_EQUAL_HEX32(0x000000, tdeck_rgb565(0x000000));
    TEST_ASSERT_EQUAL_HEX32(0xF7F3F7, tdeck_rgb565(0xF0F0F0)); // the old light bubble_in, as the panel shows it
    TEST_ASSERT_EQUAL_HEX32(0xFFFBFF, tdeck_rgb565(0xF9F9F9));
    // light text, muted and bubbles are on the grid, so the host checks exactly what the panel shows
    const TdeckPalette& p = tdeck_palette(false);
    const uint32_t on_grid[] = {p.text, p.muted, p.bubble_in, p.bubble_out, p.bubble_sys, p.chat_bg};
    for(uint32_t c : on_grid)
        TEST_ASSERT_EQUAL_HEX32(c, tdeck_rgb565(c));
}

void test_contrast_dark(void) { check_palette(true); }
void test_contrast_light(void) { check_palette(false); }

void test_contrast_function(void)
{
    TEST_ASSERT_TRUE(fabs(tdeck_contrast(0x000000, 0xFFFFFF) - 21.0) < 0.01);
    TEST_ASSERT_TRUE(fabs(tdeck_contrast(0x123456, 0x123456) - 1.0) < 0.001);
}

void test_chatfont(void)
{
    TEST_ASSERT_EQUAL_INT(0, tdeck_chatfont_parse("s"));
    TEST_ASSERT_EQUAL_INT(1, tdeck_chatfont_parse("m"));
    TEST_ASSERT_EQUAL_INT(2, tdeck_chatfont_parse("l"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_chatfont_parse(NULL));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_chatfont_parse(""));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_chatfont_parse("S"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_chatfont_parse("ml"));
    TEST_ASSERT_EQUAL_INT(-1, tdeck_chatfont_parse("1"));
    TEST_ASSERT_EQUAL_STRING("s", tdeck_chatfont_name(0));
    TEST_ASSERT_EQUAL_STRING("m", tdeck_chatfont_name(1));
    TEST_ASSERT_EQUAL_STRING("l", tdeck_chatfont_name(2));
    TEST_ASSERT_EQUAL_STRING("s", tdeck_chatfont_name(9));
    TEST_ASSERT_EQUAL_INT(12, tdeck_chatfont_px(0));
    TEST_ASSERT_EQUAL_INT(14, tdeck_chatfont_px(1));
    TEST_ASSERT_EQUAL_INT(16, tdeck_chatfont_px(2));
    TEST_ASSERT_EQUAL_INT(12, tdeck_chatfont_px(200));
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

void test_theme_segments(void)
{
    // setup segments Auto | Light | Dark <-> TdeckTheme 0, 2, 1
    TEST_ASSERT_EQUAL_UINT8(THEME_AUTO, tdeck_theme_seg_to_setting(0));
    TEST_ASSERT_EQUAL_UINT8(THEME_LIGHT, tdeck_theme_seg_to_setting(1));
    TEST_ASSERT_EQUAL_UINT8(THEME_DARK, tdeck_theme_seg_to_setting(2));
    TEST_ASSERT_EQUAL_UINT8(THEME_AUTO, tdeck_theme_seg_to_setting(7));
    TEST_ASSERT_EQUAL_UINT32(0, tdeck_theme_setting_to_seg(THEME_AUTO));
    TEST_ASSERT_EQUAL_UINT32(1, tdeck_theme_setting_to_seg(THEME_LIGHT));
    TEST_ASSERT_EQUAL_UINT32(2, tdeck_theme_setting_to_seg(THEME_DARK));
    TEST_ASSERT_EQUAL_UINT32(0, tdeck_theme_setting_to_seg(9));
    for(uint8_t s = 0; s < 3; s++)
        TEST_ASSERT_EQUAL_UINT8(s, tdeck_theme_seg_to_setting(tdeck_theme_setting_to_seg(s)));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_contrast_dark);
    RUN_TEST(test_contrast_light);
    RUN_TEST(test_contrast_function);
    RUN_TEST(test_light_bubbles_stand_out);
    RUN_TEST(test_rgb565_quantise);
    RUN_TEST(test_bubble_border);
    RUN_TEST(test_light_panel_measured);
    RUN_TEST(test_rgb565_steps);
    RUN_TEST(test_chatfont);
    RUN_TEST(test_resolve_table);
    RUN_TEST(test_resolve_out_of_range_is_auto);
    RUN_TEST(test_parse);
    RUN_TEST(test_name);
    RUN_TEST(test_theme_segments);
    return UNITY_END();
}
