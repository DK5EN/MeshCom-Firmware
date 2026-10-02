// Minimal stand-in for src/lora_setchip.h, for the native nRF52 settings
// cutover suite (test_nrf52_settings_paths).
//
// src/nrf52/nrf52_flash.cpp includes the real header (angle-bracket
// `#include <lora_setchip.h>`, no directory-collision issue -- unlike
// "WisBlock-API.h" below, this one is safely shadowed by plain -I ordering)
// only for `max_country`, used to build the RadioLimits passed into
// sanitize_radio_params() (settings_sanitize.h, compiled for real). The real
// header additionally pulls in <Arduino.h>/<configuration.h>/<debugconf.h> and
// declares strCountry[], none of which nrf52_flash.cpp's sanitize path
// touches -- so none of that is reproduced here (same "don't widen the
// surface past what's used" discipline as test_config_json/stubs).
#pragma once

#ifndef NATIVE_BUILD
#error "test_nrf52_settings_paths/stubs/lora_setchip.h darf nur im nativen Testbuild verwendet werden"
#endif

#define max_country 17
