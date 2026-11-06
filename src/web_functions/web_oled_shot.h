/**
 * Bench screenshot of the OLED (INSTRUMENT builds with a u8g2 panel, not nRF52 without Ethernet).
 *
 *   GET /screen.oled -> 200 application/octet-stream, X-Screen-Format: oled1-<w>x<h>, Cache-Control: no-store,
 *                       body = the u8g2 full-frame buffer as it is (see src/oled_shot.h), no copy.
 *                    -> 503 text/plain, X-Reason: no-display | display-off | not-full-frame
 *
 * Route in web_functions.cpp work_webpage(), next to the other GET chain entries:
 *
 *   #include "web_oled_shot.h"
 *   #if OLED_SHOT_WEB
 *       else if (web_header.startsWith("GET /screen.oled"))
 *       { // bench screenshot, tools/bench/oled_screenshot.py
 *           sub_screen_oled();
 *       }
 *   #endif
 */
#ifndef WEB_OLED_SHOT_H
#define WEB_OLED_SHOT_H

#include <instrument.h>            // INSTRUMENT_ENABLED
#include <configuration_global.h>  // MC_HAS_U8G2

#if INSTRUMENT_ENABLED && MC_HAS_U8G2 && !defined(NRF52_SERIES)
#define OLED_SHOT_WEB 1
void sub_screen_oled(void);
#else
#define OLED_SHOT_WEB 0
#endif

#endif
