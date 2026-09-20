// Minimal stand-in for src/lora_setchip.h, for the native ESP32 flash
// lifecycle suite (test_esp32_flash_lifecycle).
//
// src/esp32/esp32_flash.cpp includes the real header (angle-bracket
// `#include <lora_setchip.h>`, no directory-collision issue -- ordinary -I
// shadowing works) only for `max_country`, used to build the RadioLimits
// passed into sanitize_radio_params() (settings_sanitize.h, compiled for
// real) from inside sanitize_loaded_settings(). The real header additionally
// pulls in <Arduino.h>/<configuration.h>/<debugconf.h> (a full board
// configuration, selected by a BOARD_* macro none of which this suite's
// target needs) and declares strCountry[]/getFreq()/etc., none of which
// esp32_flash.cpp's sanitize path touches -- so none of that is reproduced
// here. Exact same shape as test/test_nrf52_settings_paths/stubs/lora_setchip.h,
// which does the identical narrowing for the nRF52 sanitize path.
#pragma once

#ifndef NATIVE_BUILD
#error "test_esp32_flash_lifecycle/stubs/lora_setchip.h darf nur im nativen Testbuild verwendet werden"
#endif

#define max_country 17
