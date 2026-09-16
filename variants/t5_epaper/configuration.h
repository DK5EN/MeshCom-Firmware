/**
 * definitions for LilyGo T5 4.7" ePaper S3 Pro Board
 */

#pragma once

#include <Arduino.h>
#include <configuration_global.h>

// T5 ePaper specific config
#define MODUL_HARDWARE T5_EPAPER

// RF defaults. The board runs its own SX1262 driver (src/t5-epaper/peri_lora.cpp),
// which reads LORA_FREQUENNCY/LORA_BANDWIDTH/... from src/t5-epaper/peripheral.h
// directly. These generic macros are still required by board-independent code
// (country_profile.cpp, esp32_flash.cpp) that every board defines.
#define RF_FREQUENCY 433.175000         // src/t5-epaper/peripheral.h: LORA_FREQUENNCY
#define LORA_APRS_FREQUENCY 433.775000  // copied from t_deck_pro/t_deck_plus (standard APRS default in this region; peripheral.h has no separate APRS frequency)

// SX1262: RadioLib output power range is -9 .. 22 dBm (comment at
// src/t5-epaper/peri_lora.cpp:105), matching TX_POWER_MIN/MAX on t_deck_pro/t_deck_plus.
// peripheral.h:15 carries "-17 - 22 dBm" in its own comment; that is the SX127x
// range, not this part's, and peri_lora.cpp is the file that actually calls
// setOutputPower().
#define TX_POWER_MAX 22
#define TX_POWER_MIN -9

// Default TX power, consumed by resolve_tx_power() in command_functions.cpp
// when the stored value is 0 or the -20 "unset" sentinel (#1132).
#define TX_OUTPUT_POWER 22  // src/t5-epaper/peripheral.h: LORA_OUTPUT_POWER

// GPIO 0, the BOOT button -- src/t5-epaper/utilities.h:44 BOARD_BOOT_BTN.
#define BUTTON_PIN 0

/** RadioLib Spreading Factor
 * case 6: SF_6;
    case 7: SF_7;
    case 8: SF_8;
    case 9: SF_9;
    case 10: SF_10;
    case 11: SF_11;
    case 12: SF_12;
*/
#define LORA_SF 11  // src/t5-epaper/peripheral.h: LORA_SPREAD_FACTOR

#define LORA_PREAMBLE_LENGTH DEFAULT_PREAMPLE_LENGTH  // Same for Tx and Rx (t_deck_pro/t_deck_plus convention)

/**
 * RadioLib Coding Rate: Allowed values range from 5 to 8.
 * case 5: CR_4_5;
    case 6: CR_4_6;
    case 7: CR_4_7;
    case 8: CR_4_8;
*/
#define LORA_CR 6  // src/t5-epaper/peripheral.h: LORA_CODING_RATE

// RadioLib LoRa Bandwidth Setting in kHz. Spelled "250.0" (not "250") to stay
// token-identical with src/t5-epaper/peripheral.h's LORA_BANDWIDTH, which is
// included ahead of this file in the same translation unit in several
// src/t5-epaper/*.cpp files (peripheral.h -> lora_functions.h -> configuration.h);
// a differently-spelled redefinition would warn.
#define LORA_BANDWIDTH 250.0
