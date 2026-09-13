#ifndef _ESP32_SLEEP_H_
#define _ESP32_SLEEP_H_

// Issue 962 (docs/issue-962-deepsleep-verdict.md, Option A): shared entry
// point for the --deepsleep command on every ESP32 board that is not
// BOARD_HELTEC_T114 (nRF52, its own toggle-only branch) and not WP_DISP
// (Wireless Paper / Vision Master E213, already fixed by PR 1050 and left
// alone here). Puts the radio to sleep, the display into power-save, the
// PMU LoRa/GPS rails off, the GPS/ADC pins off, arms a button wake source,
// then calls esp_deep_sleep_start(). No parameters, no sleep reason, no RTC
// state -- this only replaces the manual command body, nothing else
// (the low-battery guard from the same doc, Option B, is a separate,
// deliberately out-of-scope piece of work).
void esp32EnterDeepSleep();

// Blocks (bounded, 10 s) until the runtime wake button (iButtonPin) reads
// HIGH, then a short debounce. Called right before a button wake source is
// armed, in esp32EnterDeepSleep() and in the WP_DISP --deepsleep branch:
// a long press reaches --deepsleep via OneButton's attachLongPressStart()
// with the button still held, and an ext1 low-level wake on a pin that is
// already LOW fires the instant deep sleep starts. No-op when no button is
// configured (99) or the pin is not pressed (serial/BLE --deepsleep).
void esp32WaitButtonRelease();

#endif
