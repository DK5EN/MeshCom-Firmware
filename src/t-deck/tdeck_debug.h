/**
 * @file        tdeck_debug.h
 * @brief       runtime redraw observability for the T-Deck GUI
 * @license     MIT
 * @copyright   Copyright (c) 2025 ICSSW.org
 */

#ifndef _TDECK_DEBUG_H_
#define _TDECK_DEBUG_H_

#if defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS)

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime gate for the [REDRAW]/[REFR]/[REFRSTART] log lines. Off by default;
 * inv_total/refr_total counters keep accumulating regardless of the gate. */
void tdeck_dbg_redrawlog(bool on);
bool tdeck_dbg_redrawlog_enabled(void);
void tdeck_dbg_flushfix(bool on);
void tdeck_dbg_reflush(void);
void tdeck_dbg_mapzoom(int dir);
void tdeck_dbg_blink(int n);
void tdeck_dbg_framedump_arm(bool on);
bool tdeck_dbg_framedump_armed(void);
void tdeck_dbg_invalidate(void);
bool tdeck_dbg_flushfix_enabled(void);

/* One-shot UI/heap/counter snapshot, see brief for the exact field list. */
void tdeck_dbg_uistat(void);

/* Lists the 8 tabs (index + name) plus the active tab index. */
void tdeck_dbg_tab_list(void);

/* Switches the active tab. Returns false and prints an error line on a bad index. */
bool tdeck_dbg_tab(int idx);

/* Opens/closes the tab drawer to match the requested state. */
void tdeck_dbg_drawer(bool open);

/* Scrolls the page of tab idx by dy pixels (dy > 0 = scroll down). Prints
 * [SCROLL];tab;idx;dy;N;y;before;after;bottom;remaining. */
bool tdeck_dbg_scroll(int idx, int dy);

/* Input injection for the bench harness. Keys go through keypad_get_key()
 * (the I2C keyboard path), trackball steps through mouse_read() (the GPIO
 * edge path), so the whole LVGL indev chain is exercised. */
void tdeck_dbg_key(const char *text);            /* --key <text>          */
void tdeck_dbg_ball(const char *dir, int n);     /* --ball <dir> <n>      */
bool tdeck_dbg_inject_key(uint32_t code);        /* implemented in tdeck_main.cpp */
void tdeck_dbg_inject_ball(int dir, int n);      /* implemented in tdeck_main.cpp */
void tdeck_dbg_balledge(bool on);                /* --balledge on/off: edge counting vs level compare */
void tdeck_dbg_balledges(bool reset);            /* --balledges [reset]: print (and clear) the counters */

/* Display sleep/wake control: 1 = tft_on(), 0 = tft_off(), 2 = state only.
 * Always prints a [TFT] status line, see brief for the exact field list. */
void tdeck_dbg_tft(int mode);

/* Reads back the panel frame memory (8 horizontal bands) and prints a CRC32
 * fingerprint per band plus a non-black pixel count, see brief for the exact
 * field list. */
void tdeck_dbg_screencrc(void);

/* TM-41 colour/geometry display test, verified driver-side.
 * phase: "" or "full" = the whole sequence, else one of
 *        invert|colors|square|circle|triangle.
 * stride: pixels per growth step of the square/circle phases (<= 0 -> 1).
 * Runs synchronously on the loop task, bypasses LVGL (see
 * tdeck_dbg_disptest_running()) and prints one [DISPTEST];step line with the
 * CRC32 of exactly the bytes handed to tft.pushColors() per frame. */
void tdeck_dbg_disptest(const char *phase, int stride);

/* True while tdeck_dbg_disptest() owns the panel; disp_flush() drops its
 * transfer (and reports the area done) for as long as this is set. */
bool tdeck_dbg_disptest_running(void);

/* lv_disp_drv_t callbacks, wired up in tdeck_main.cpp:setupLvgl(). */
void tdeck_dbg_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t time_ms, uint32_t px);
void tdeck_dbg_render_start_cb(lv_disp_drv_t * disp_drv);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_T_DECK || BOARD_T_DECK_PLUS */

#endif /* _TDECK_DEBUG_H_ */
