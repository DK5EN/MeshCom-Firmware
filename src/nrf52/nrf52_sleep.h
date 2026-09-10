#ifndef _NRF52_SLEEP_H_
#define _NRF52_SLEEP_H_

// Issue 962 (docs/issue-962-deepsleep-verdict.md, section 6.4): shared entry
// point for the --deepsleep command and the T-Echo long-press power-off
// (boardPWROff(), nrf52_functions.cpp) on all three nRF52 boards (RAK4631,
// Heltec T114, T-Echo). Puts the SX126x radio to sleep, stops BLE
// advertising, switches off the display where one exists (T114 TFT,
// T-Echo e-ink), cuts the peripheral rail (RAK4631 WB_IO2 / T114
// PIN_VEXT_CTL / T-Echo Power_On_Pin), tears down the peripheral buses,
// then enters real nRF52 System OFF (systemOff() / sd_power_system_off()).
// This call does not return -- wake is a hardware reset, exactly like
// esp_deep_sleep_start() on the ESP32 side. No parameters, no sleep-reason
// state: the low-battery LPCOMP wake from the same doc section (6.4.1 step
// 8) is separate, deliberately out-of-scope work.
void nrf52EnterDeepSleep();

#endif
