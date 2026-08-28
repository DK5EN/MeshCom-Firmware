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

/* One-shot UI/heap/counter snapshot, see brief for the exact field list. */
void tdeck_dbg_uistat(void);

/* Lists the 8 tabs (index + name) plus the active tab index. */
void tdeck_dbg_tab_list(void);

/* Switches the active tab. Returns false and prints an error line on a bad index. */
bool tdeck_dbg_tab(int idx);

/* Opens/closes the tab drawer to match the requested state. */
void tdeck_dbg_drawer(bool open);

/* Display sleep/wake control: 1 = tft_on(), 0 = tft_off(), 2 = state only.
 * Always prints a [TFT] status line, see brief for the exact field list. */
void tdeck_dbg_tft(int mode);

/* Reads back the panel frame memory (8 horizontal bands) and prints a CRC32
 * fingerprint per band plus a non-black pixel count, see brief for the exact
 * field list. */
void tdeck_dbg_screencrc(void);

/* lv_disp_drv_t callbacks, wired up in tdeck_main.cpp:setupLvgl(). */
void tdeck_dbg_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t time_ms, uint32_t px);
void tdeck_dbg_render_start_cb(lv_disp_drv_t * disp_drv);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_T_DECK || BOARD_T_DECK_PLUS */

#endif /* _TDECK_DEBUG_H_ */
