// DISP-PAT-01: T-Deck full-screen display test pattern (--tdpattern). INSTRUMENT builds only; a normal
// build contains none of it. Pure part (colours, parser) is in tdeck_pattern.h and host-tested.
#include "tdeck_pattern.h"

#if INSTRUMENT_ENABLED && (defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS))

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include "tdeck_helpers.h"
#include "tdeck_extern.h"
#include "lv_obj_functions.h"

LV_FONT_DECLARE(mc_font_latin_12);
LV_FONT_DECLARE(mc_font_latin_14);
LV_FONT_DECLARE(mc_font_latin_16);

static const int kW = 320, kH = 240, kTop = 48;   // reference strip: white patch, label strip, black patch

static lv_obj_t* s_root = NULL;
static uint8_t s_prev_bl = 0;   // backlight level to restore on "off"

static lv_color_t col(uint32_t c) { return lv_color_hex(c); }

static lv_obj_t* rect(lv_obj_t* parent, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, col(c), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

static lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* f, uint32_t c)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col(c), 0);
    return l;
}

static void num_label(lv_obj_t* patch, int n, uint32_t c)
{
    char b[8];
    snprintf(b, sizeof(b), "%d", n);
    lv_obj_align(text(patch, b, &mc_font_latin_12, c), LV_ALIGN_CENTER, 0, 0);
}

static void grey_row(lv_obj_t* root, int y, int h, int first)
{
    for(int j = 0; j < 8; j++)
    {
        int i = first + j;
        lv_obj_t* p = rect(root, j * 40, y, 40, h, tdeck_pat_grey((uint8_t)i));
        num_label(p, i, i < 16 ? 0xFFFFFF : 0x000000);
    }
}

static void page_grey(lv_obj_t* root)
{
    for(int r = 0; r < 4; r++)
        grey_row(root, kTop + r * 48, 48, r * 8);
}

static void page_detail(lv_obj_t* root)
{
    grey_row(root, kTop, 96, 24);
    grey_row(root, kTop + 96, 96, 0);
}

static void page_ramps(lv_obj_t* root, bool light)
{
    for(int h = 0; h < 6; h++)
        for(int k = 0; k < 8; k++)
            rect(root, k * 40, kTop + h * 32, 40, 32,
                 light ? tdeck_pat_ramp_light((uint8_t)h, (uint8_t)k) : tdeck_pat_ramp_dark((uint8_t)h, (uint8_t)k));
}

static void aa_row(lv_obj_t* tile, uint32_t fg)
{
    static const lv_font_t* f[3] = {&mc_font_latin_12, &mc_font_latin_14, &mc_font_latin_16};
    static const int x[3] = {10, 60, 110};
    for(int i = 0; i < 3; i++)
        lv_obj_align(text(tile, "Aa", f[i], fg), LV_ALIGN_LEFT_MID, x[i], 0);
}

static void page_theme(lv_obj_t* root)
{
    const int rowh = 38, half = kW / 2;
    for(int m = 0; m < 2; m++)
    {
        const TdeckPalette& p = tdeck_palette(m == 1);
        const int x0 = m * half;
        for(int r = 0; r < 5; r++)
        {
            const int y = kTop + r * rowh;
            lv_obj_t* t = NULL;
            switch(r)
            {
                case 0: t = rect(root, x0, y, half, rowh, p.bubble_in);  aa_row(t, p.text); break;
                case 1: t = rect(root, x0, y, half, rowh, p.bubble_out); aa_row(t, p.text); break;
                case 2: t = rect(root, x0, y, half, rowh, p.chat_bg);    aa_row(t, p.muted); break;
                case 3:
                    rect(root, x0, y, half, rowh, p.chat_bg);
                    t = rect(root, x0 + 4, y + 3, half - 8, rowh - 6, p.bubble_in);
                    lv_obj_set_style_border_width(t, 2, 0);
                    lv_obj_set_style_border_color(t, col(p.bubble_border), 0);
                    lv_obj_set_style_border_opa(t, LV_OPA_COVER, 0);
                    aa_row(t, p.text);
                    break;
                default: t = rect(root, x0, y, half, rowh, p.header_bg); aa_row(t, p.header_fg); break;
            }
        }
    }
}

static void build(int page)
{
    if(s_root)
        lv_obj_del(s_root);
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, kW, kH);
    lv_obj_set_style_bg_color(s_root, col(0x000000), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);

    rect(s_root, 0, 0, kTop, kTop, 0xFFFFFF);          // white reference
    rect(s_root, kW - kTop, 0, kTop, kTop, 0x000000);  // black reference
    rect(s_root, kTop, 0, kW - 2 * kTop, kTop, 0x000000);
    char b[24];
    snprintf(b, sizeof(b), "P%d BL%u", page, (unsigned)current_brightness_level);
    lv_obj_align(text(s_root, b, &mc_font_latin_12, 0xFFFFFF), LV_ALIGN_TOP_MID, 0, 16);

    switch(page)
    {
        case 0: page_grey(s_root); break;
        case 1: page_detail(s_root); break;
        case 2: page_ramps(s_root, false); break;
        case 3: page_ramps(s_root, true); break;
        default: page_theme(s_root); break;
    }
    lv_obj_invalidate(s_root);
}

bool tdeck_pattern_active() { return s_root != NULL; }

void tdeck_pattern_cmd(const char* args)
{
    int page = 0, bl = 0;
    const int rc = tdeck_pat_parse(args, &page, &bl);
    if(rc < 0)
    {
        const char* p = args ? args : "";
        while(*p == ' ' || *p == '\t')
            p++;
        if(*p == 0 || *p == '\r' || *p == '\n')
            Serial.println("[TDPAT];err;usage: --tdpattern <0..4> [bl <1..16>] | off");
        else
            Serial.println("[TDPAT];err;bad args (page 0..4, bl 1..16, off)");
        return;
    }
    if(rc == 1)
    {
        if(s_root)
        {
            lv_obj_del(s_root);
            s_root = NULL;
            if(s_prev_bl > 0)
                setBrightness(s_prev_bl);   // setBrightness also re-arms pre_sleep_brightness_level
        }
        tdeck_tft_timer = millis();
        Serial.println("[TDPAT];off");
        return;
    }
    if(tft_is_sleeping || current_brightness_level == 0)
        tft_on();
    if(s_root == NULL)
        s_prev_bl = current_brightness_level > 0 ? current_brightness_level : pre_sleep_brightness_level;
    if(bl > 0)
        setBrightness((uint8_t)bl);   // not stored: no cycleBrightness, no brightness_store_level
    tdeck_tft_timer = millis();
    build(page);
    Serial.printf("[TDPAT];page;%d;bl;%u\n", page, (unsigned)current_brightness_level);
}

#endif
