// Stand-in for the NimBLE porting layer: the two critical-section calls NimBLEAttValue.cpp makes (no-ops on the host).
#pragma once
#include <stdint.h>
static inline uint32_t ble_npl_hw_enter_critical(void) { return 0; }
static inline void ble_npl_hw_exit_critical(uint32_t) {}
