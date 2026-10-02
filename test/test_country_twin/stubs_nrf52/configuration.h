// SX126x side of the country twin (test plan U6 / audit D3-05).
//
// Copied verbatim from variants/wiscore_rak4631/configuration.h. The board
// guard is what makes countryProfile() take the other branch of every
// `#if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)`;
// it is set by the env's build_flags, not here, so the same header serves a
// T114 or T-Echo build if one is ever added.
//
// Note the units: frequency in Hz, bandwidth and coding rate as radio-API
// indices. That split is OPT-D14 and the reason this twin exists.
#pragma once

#define RF_FREQUENCY 433175000
#define LORA_APRS_FREQUENCY 433775000
#define LORA_BANDWIDTH 1
#define LORA_SF 11
#define LORA_CR 2
#define DEFAULT_PREAMPLE_LENGTH 32
#define LORA_PREAMBLE_LENGTH DEFAULT_PREAMPLE_LENGTH
