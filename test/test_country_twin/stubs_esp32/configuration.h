// SX127x side of the country twin (test plan U6 / audit D3-05).
//
// countryProfile() reads nothing but these macros, so the twin is built twice
// with the two variants' values rather than run twice on hardware. These are
// copied verbatim from variants/heltec_wifi_lora_32_V3/configuration.h; if
// that file changes, test_country_twin's committed table changes with it and
// the test says so.
#pragma once

#define RF_FREQUENCY 433.175000
#define LORA_APRS_FREQUENCY 433.775000
#define LORA_BANDWIDTH 250
#define LORA_SF 11
#define LORA_CR 6
#define DEFAULT_PREAMPLE_LENGTH 32
#define LORA_PREAMBLE_LENGTH DEFAULT_PREAMPLE_LENGTH
