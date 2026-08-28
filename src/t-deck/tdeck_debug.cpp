/**
 * @file        tdeck_debug.cpp
 * @brief       runtime redraw observability for the T-Deck GUI
 * @license     MIT
 * @copyright   Copyright (c) 2025 ICSSW.org
 */

#include <configuration.h>
#include <debugconf.h>

#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)

#include "tdeck_debug.h"
#include "lv_obj_functions.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_debug_helpers.h>
#include <soc/cpu.h>

/* Globals owned by lv_obj_functions.cpp (see lv_obj_functions.cpp:87,106,110). */
extern lv_obj_t *tv;
extern lv_obj_t *msg_list;
extern lv_obj_t *map_ta;

namespace {

/* ---- always-on counters (single-threaded: LVGL/main task) ---- */
volatile bool s_redrawlog_on = false;
uint32_t s_inv_total = 0;
uint32_t s_refr_total = 0;
uint32_t s_last_refr_px = 0;
uint32_t s_last_refr_ms = 0;

/* ---- [REDRAW] rate cap: 200 lines/second ---- */
const uint32_t REDRAW_RATE_CAP = 200;
uint32_t s_rate_window_start_ms = 0;
uint32_t s_rate_window_count = 0;
uint32_t s_rate_dropped = 0;

/* Tab names in setDisplayLayout() creation order (lv_obj_functions.cpp ~601-620). */
const char * const TAB_NAMES[] = {
    "msg", "keyboard", "heart", "map", "gps", "list", "menu", "settings"
};
const int TAB_COUNT = sizeof(TAB_NAMES) / sizeof(TAB_NAMES[0]);

const char * classify_obj(const lv_obj_t * obj)
{
    const lv_obj_class_t * cls = lv_obj_get_class(obj);
    if(cls == &lv_obj_class) return "obj";
    if(cls == &lv_label_class) return "label";
    if(cls == &lv_img_class) return "img";
    if(cls == &lv_btn_class) return "btn";
    if(cls == &lv_textarea_class) return "ta";
    if(cls == &lv_tabview_class) return "tabview";
    if(cls == &lv_btnmatrix_class) return "btnm";
    if(cls == &lv_list_class) return "list";
    if(cls == &lv_dropdown_class) return "dd";
    if(cls == &lv_switch_class) return "sw";
    if(cls == &lv_slider_class) return "slider";
    if(cls == &lv_checkbox_class) return "cb";
    if(cls == &lv_line_class) return "line";
    if(cls == &lv_canvas_class) return "canvas";
    return "?";
}

const char * known_name(const lv_obj_t * obj)
{
    if(obj == tv) return "tv";
    if(obj == msg_list) return "msg_list";
    if(obj == map_ta) return "map_ta";
    return NULL;
}

uint32_t count_objs_recursive(const lv_obj_t * obj)
{
    if(obj == NULL) return 0;
    uint32_t total = 1;
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for(uint32_t i = 0; i < child_cnt; i++) {
        total += count_objs_recursive(lv_obj_get_child(obj, i));
    }
    return total;
}

bool drawer_is_open()
{
    if(tv == NULL) return false;
    lv_obj_t * btns = lv_tabview_get_tab_btns(tv);
    if(btns == NULL) return false;
    return !lv_obj_has_flag(btns, LV_OBJ_FLAG_HIDDEN);
}

} // namespace

extern "C" void tdeck_dbg_redrawlog(bool on)
{
    s_redrawlog_on = on;
}

/* Strong override of the weak hook declared in lib/lvgl/src/core/lv_obj_pos.c. */
/* Walk the Xtensa call stack from inside the hook. Frames 0..2 are this
 * helper, the hook and lv_obj_invalidate_area(); everything after that is the
 * interesting part (lv_obj_invalidate -> LVGL setter -> user code). */
static int __attribute__((noinline)) collect_backtrace(uint32_t * out, int max)
{
    esp_backtrace_frame_t f;
    esp_backtrace_get_start(&f.pc, &f.sp, &f.next_pc);
    int n = 0;
    int skip = 2;                       /* hook, lv_obj_invalidate_area */
    while(n < max && f.next_pc != 0) {
        if(!esp_backtrace_get_next_frame(&f)) break;
        if(skip > 0) { skip--; continue; }
        out[n++] = esp_cpu_process_stack_pc(f.pc);
    }
    return n;
}

extern "C" void lv_obj_invalidate_hook(const lv_obj_t * obj, const lv_area_t * area, void * ret_addr)
{
    s_inv_total++;

    if(!s_redrawlog_on) return;

    uint32_t now = millis();
    if(now - s_rate_window_start_ms >= 1000) {
        if(s_rate_dropped > 0) {
            Serial.printf("[REDRAW];dropped;%u\n", (unsigned)s_rate_dropped);
            s_rate_dropped = 0;
        }
        s_rate_window_start_ms = now;
        s_rate_window_count = 0;
    }

    if(s_rate_window_count >= REDRAW_RATE_CAP) {
        s_rate_dropped++;
        return;
    }
    s_rate_window_count++;

    const char * cls = classify_obj(obj);
    const char * name = known_name(obj);

    uint32_t bt[8];
    int nbt = collect_backtrace(bt, 8);
    char btbuf[8 * 11 + 1];
    int off = 0;
    for(int k = 0; k < nbt; k++)
        off += snprintf(btbuf + off, sizeof(btbuf) - off, "%s0x%08lx", k ? "," : "", (unsigned long)bt[k]);
    if(nbt == 0) snprintf(btbuf, sizeof(btbuf), "-");

    Serial.printf("[REDRAW];ms;%lu;obj;0x%08lx;cls;%s;area;%d;%d;%d;%d;ra;0x%08lx;bt;%s",
                  (unsigned long)now, (unsigned long)(uintptr_t)obj, cls,
                  (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
                  (unsigned long)(uintptr_t)ret_addr, btbuf);
    if(name != NULL) Serial.printf(";name;%s", name);
    Serial.print("\n");
}

extern "C" void tdeck_dbg_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t time_ms, uint32_t px)
{
    (void)disp_drv;
    s_refr_total++;
    s_last_refr_px = px;
    s_last_refr_ms = time_ms;

    if(!s_redrawlog_on) return;
    Serial.printf("[REFR];ms;%lu;px;%lu;t_ms;%lu\n",
                  (unsigned long)millis(), (unsigned long)px, (unsigned long)time_ms);
}

extern "C" void tdeck_dbg_render_start_cb(lv_disp_drv_t * disp_drv)
{
    (void)disp_drv;
    if(!s_redrawlog_on) return;

    lv_disp_t * disp = lv_disp_get_default();
    uint32_t n = (disp != NULL) ? disp->inv_p : 0;
    Serial.printf("[REFRSTART];ms;%lu;areas;%lu\n", (unsigned long)millis(), (unsigned long)n);
}

extern "C" void tdeck_dbg_uistat(void)
{
    int active_tab = (tv != NULL) ? lv_tabview_get_tab_act(tv) : -1;
    int drawer = drawer_is_open() ? 1 : 0;
    uint32_t objs = count_objs_recursive(lv_scr_act());
    int msg_list_children = (msg_list != NULL) ? (int)lv_obj_get_child_cnt(msg_list) : -1;

    Serial.printf("[UISTAT];tab;%d;drawer;%d;objs;%lu;msg_list;%d;inv_total;%lu;refr_total;%lu;"
                  "last_refr_px;%lu;last_refr_ms;%lu;redrawlog;%d;heap_free;%lu;heap_min;%lu;psram_free;%lu\n",
                  active_tab, drawer, (unsigned long)objs, msg_list_children,
                  (unsigned long)s_inv_total, (unsigned long)s_refr_total,
                  (unsigned long)s_last_refr_px, (unsigned long)s_last_refr_ms,
                  s_redrawlog_on ? 1 : 0,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                  (unsigned long)ESP.getFreePsram());
}

extern "C" void tdeck_dbg_tab_list(void)
{
    for(int i = 0; i < TAB_COUNT; i++) {
        Serial.printf("[TAB];%d;%s\n", i, TAB_NAMES[i]);
    }
    int active_tab = (tv != NULL) ? lv_tabview_get_tab_act(tv) : -1;
    Serial.printf("[TAB];active;%d\n", active_tab);
}

extern "C" bool tdeck_dbg_tab(int idx)
{
    if(tv == NULL || idx < 0 || idx >= TAB_COUNT) {
        Serial.println("[TAB];err;range");
        return false;
    }

    uint32_t before = s_inv_total;
    lv_tabview_set_act(tv, idx, LV_ANIM_OFF);
    uint32_t delta = s_inv_total - before;

    Serial.printf("[TAB];set;%d;inv_delta;%lu;\n", idx, (unsigned long)delta);
    return true;
}

extern "C" void tdeck_dbg_drawer(bool open)
{
    if(open) {
        if(!drawer_is_open()) tdeck_show_tab_menu();
    }
    else {
        if(drawer_is_open()) tdeck_toggle_tab_menu();
    }
    Serial.printf("[DRAWER];%d\n", drawer_is_open() ? 1 : 0);
}

#endif /* BOARD_T_DECK || BOARD_T_DECK_PLUS */
