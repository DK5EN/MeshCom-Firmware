// GPS-SKY W3: sky plot view for the Position tab, see tdeck_gps_sky.h.
#include "tdeck_gps_sky.h"

#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "configuration.h"
#include "loop_functions_extern.h"   // posinfo_fix
#include "gps_functions.h"           // gpsData
#include "gps_skyplot.h"
#include "tdeck_theme.h"
#include "tdeck_gps_sky_layout.h"

LV_FONT_DECLARE(mc_font_latin_12);

// plot geometry inside the card, all rects from tdeck_gps_sky_layout.h
using skylayout::kCard;
static const int kCx = skylayout::kCx, kCy = skylayout::kCy, kR = skylayout::kR;
static const int kDot = 8;

static lv_obj_t *s_cont = NULL;
static lv_obj_t *s_text_view = NULL;
static lv_obj_t *s_tab_sky = NULL;
static lv_obj_t *s_tab_text = NULL;
static lv_obj_t *s_info = NULL;
static lv_obj_t *s_ring[3] = {NULL, NULL, NULL};
static lv_obj_t *s_dot[GPS_SKY_MAX_SATS];
static GpsSkySat s_sats[GPS_SKY_MAX_SATS];   // static: 480 B, not on the stack
static uint32_t s_hash = 0;
static bool s_hash_valid = false;
static char s_info_txt[40] = "";

static lv_color_t snr_color(int snr)
{
    if(snr <= 0)
        return lv_color_hex(0x808080);                       // in view, not tracked
    switch(gpsSkySnrLevel(snr))
    {
        case GPS_SKY_SNR_HIGH: return lv_color_hex(0x1E9E3C);   // green
        case GPS_SKY_SNR_MID:  return lv_color_hex(0xE8D000);   // yellow
        default:               return lv_color_hex(0xE02020);   // red
    }
}

static bool sky_visible()
{
    return s_cont != NULL && !lv_obj_has_flag(s_cont, LV_OBJ_FLAG_HIDDEN);
}

// rings carry a palette colour that no shared style provides; runs after every theme apply
static void tabs_theme_async(void *);

static void sky_theme_cb(void)
{
    const TdeckPalette &p = tdeck_theme_pal();
    for(int i = 0; i < 3; i++)
        if(s_ring[i] != NULL)
            lv_obj_set_style_border_color(s_ring[i], lv_color_hex(p.muted), LV_PART_MAIN);
    lv_async_call(tabs_theme_async, NULL);
}

static lv_obj_t *make_ring(lv_obj_t *parent, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, radius * 2, radius * 2);
    lv_obj_set_pos(o, kCx - radius, kCy - radius);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

static void make_label(lv_obj_t *parent, const char *txt, int cx, int cy)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &tdeck_style_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &mc_font_latin_12, LV_PART_MAIN);
    lv_label_set_text(l, txt);
    lv_obj_update_layout(l);
    lv_obj_set_pos(l, cx - lv_obj_get_width(l) / 2, cy - lv_obj_get_height(l) / 2);
}

// active tab: the visible card's own fill and border colour, border on top/right/bottom only (open towards the
// card = the nose), overlapping the card border. Inactive tab: screen background, muted border and text, a gap to
// the card (same border colour as the card, so the active tab never reads weaker). No button fill: the btn_off colour reads brown in dark mode.
static void tab_apply(lv_obj_t *tab, bool active, const skylayout::Rect &r, lv_obj_t *card)
{
    const TdeckPalette &p = tdeck_theme_pal();
    lv_obj_remove_style_all(tab);
    lv_obj_set_style_radius(tab, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab, skylayout::kBorder, LV_PART_MAIN);
    lv_obj_set_style_border_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
    if(active)
    {
        lv_obj_set_style_bg_color(tab, lv_obj_get_style_bg_color(card, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_style_border_color(tab, lv_obj_get_style_border_color(card, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_style_text_color(tab, lv_color_hex(p.text), LV_PART_MAIN);
        lv_obj_set_style_border_side(tab, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), LV_PART_MAIN);
        lv_obj_set_pos(tab, r.x, r.y);
        lv_obj_set_size(tab, r.w, r.h);
    }
    else
    {
        lv_obj_set_style_bg_color(tab, lv_color_hex(p.bg), LV_PART_MAIN);
        lv_obj_set_style_border_color(tab, lv_obj_get_style_border_color(card, LV_PART_MAIN), LV_PART_MAIN);   // same weight as the card
        lv_obj_set_style_text_color(tab, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_border_side(tab, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
        lv_obj_set_pos(tab, r.x + skylayout::kTabInactiveDx, r.y);
        lv_obj_set_size(tab, r.w - skylayout::kTabInactiveDx, r.h);
    }
}

static void tabs_show(bool text)
{
    if(s_tab_sky == NULL || s_tab_text == NULL || s_cont == NULL || s_text_view == NULL)
        return;
    tab_apply(s_tab_sky, !text, skylayout::kTabSky, s_cont);
    tab_apply(s_tab_text, text, skylayout::kTabText, s_text_view);
    lv_obj_move_foreground(s_tab_sky);
    lv_obj_move_foreground(s_tab_text);
}

// theme change: the cards restyle in their own callbacks, so the tabs copy their colours one LVGL cycle later
static void tabs_theme_async(void *)
{
    if(s_cont != NULL)
        tabs_show(lv_obj_has_flag(s_cont, LV_OBJ_FLAG_HIDDEN));
}

static void tab_event_cb(lv_event_t *e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED || s_cont == NULL || s_text_view == NULL)
        return;
    const bool want_text = (lv_event_get_target(e) == s_tab_text);
    const bool is_text = lv_obj_has_flag(s_cont, LV_OBJ_FLAG_HIDDEN);
    if(want_text == is_text)
        return;   // active tab: nothing to do
    if(want_text)
    {
        lv_obj_add_flag(s_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_text_view, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(s_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_text_view, LV_OBJ_FLAG_HIDDEN);
        s_hash_valid = false;   // force a redraw on the way back
    }
    tabs_show(want_text);
    tdeckGpsSkyRefresh();
}

static lv_obj_t *make_tab(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t, tab_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(t);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return t;
}

void tdeckGpsSkyCreate(lv_obj_t *tab, lv_obj_t *text_view)
{
    if(tab == NULL || text_view == NULL)
        return;
    s_text_view = text_view;

    s_cont = lv_obj_create(tab);
    lv_obj_remove_style_all(s_cont);
    lv_obj_add_style(s_cont, &tdeck_style_surface, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_cont, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(s_cont, kCard.w, kCard.h);
    lv_obj_set_pos(s_cont, kCard.x, kCard.y);

    s_ring[0] = make_ring(s_cont, kR);               // horizon (elev 0)
    s_ring[1] = make_ring(s_cont, kR * 2 / 3);       // 30 deg
    s_ring[2] = make_ring(s_cont, kR / 3);           // 60 deg
    sky_theme_cb();
    tdeck_theme_on_change(sky_theme_cb);

    make_label(s_cont, "N", kCx, kCy - kR - 7);
    make_label(s_cont, "S", kCx, kCy + kR + 5);   // +5: clear of the 2 px container border
    make_label(s_cont, "W", kCx - kR - 8, kCy);
    make_label(s_cont, "E", kCx + kR + 8, kCy);

    for(int i = 0; i < GPS_SKY_MAX_SATS; i++)
    {
        lv_obj_t *d = lv_obj_create(s_cont);
        lv_obj_remove_style_all(d);
        lv_obj_clear_flag(d, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_size(d, kDot, kDot);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        s_dot[i] = d;
    }

    s_tab_sky = make_tab(tab, "SKY");
    s_tab_text = make_tab(tab, "TEXT");
    tabs_show(false);   // boot view is the sky plot

    s_info = lv_label_create(tab);
    lv_obj_add_style(s_info, &tdeck_style_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_info, &mc_font_latin_12, LV_PART_MAIN);
    lv_obj_set_pos(s_info, skylayout::kInfo.x, skylayout::kInfo.y);
    lv_obj_set_width(s_info, skylayout::kInfo.w);
    lv_label_set_text(s_info, "");

    lv_obj_add_flag(text_view, LV_OBJ_FLAG_HIDDEN);   // default view: sky plot
}

// non-zero while a notice (e.g. "POS SENT") holds the info label
static uint32_t s_notice_until = 0;

void tdeckGpsSkyNotice(const char *txt)
{
    if(!sky_visible() || s_info == NULL || txt == NULL)
        return;
    lv_label_set_text(s_info, txt);
    s_info_txt[0] = '\0';   // forces the SAT/AGE text back after the hold
    s_notice_until = millis() + 2000;
    if(s_notice_until == 0)
        s_notice_until = 1;
}

void tdeckGpsSkyRefresh()
{
    if(!sky_visible())
        return;

    const uint32_t now = millis();

    if(s_notice_until != 0 && (int32_t)(now - s_notice_until) >= 0)
        s_notice_until = 0;

    char age[24];
    gpsSkyFormatAge(age, sizeof(age), posinfo_fix, gpsData.age_ms);
    char info[40];
    snprintf(info, sizeof(info), "SAT %d\n%s", gpsSkySatsInView(now), age);
    if(s_notice_until == 0 && strcmp(info, s_info_txt) != 0)
    {
        strncpy(s_info_txt, info, sizeof(s_info_txt) - 1);
        s_info_txt[sizeof(s_info_txt) - 1] = '\0';
        if(s_info != NULL)
            lv_label_set_text(s_info, s_info_txt);
    }

    const int n = gpsSkyCopy(s_sats, GPS_SKY_MAX_SATS, now);

    uint32_t h = 2166136261u;
    for(int i = 0; i < n; i++)
    {
        const GpsSkySat &s = s_sats[i];
        h = (h ^ s.prn) * 16777619u;
        h = (h ^ (uint8_t)s.elev) * 16777619u;
        h = (h ^ (uint16_t)s.az) * 16777619u;
        h = (h ^ (s.snr <= 0 ? 255u : (uint32_t)gpsSkySnrLevel(s.snr))) * 16777619u;
    }
    h = (h ^ (uint32_t)n) * 16777619u;
    if(s_hash_valid && h == s_hash)
        return;
    s_hash = h;
    s_hash_valid = true;

    int used = 0;
    for(int i = 0; i < n && used < GPS_SKY_MAX_SATS; i++)
    {
        const GpsSkySat &s = s_sats[i];
        if(s.elev < 0 || s.az < 0)
            continue;
        int x = 0, y = 0;
        gpsSkyProject(s.elev, s.az, kCx, kCy, kR, &x, &y);
        lv_obj_t *d = s_dot[used++];
        lv_obj_set_pos(d, x - kDot / 2, y - kDot / 2);
        lv_obj_set_style_bg_color(d, snr_color(s.snr), LV_PART_MAIN);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    for(int i = used; i < GPS_SKY_MAX_SATS; i++)
        lv_obj_add_flag(s_dot[i], LV_OBJ_FLAG_HIDDEN);
}

#endif
