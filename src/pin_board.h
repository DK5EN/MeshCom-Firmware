// GPIO-01 W1b: firmware side of the pin policy (src/pin_policy.h). Included ONLY by firmware TUs that see the
// variant macros (command_functions.cpp, esp32_main.cpp, esp32_flash.cpp), AFTER the configuration/variant
// includes; never by host tests. The reserved list is the macro list that used to live in wxWindPinOk()
// (WX-01), now compiled on every platform, plus the classes docs/gpio-pin-map.md section 3 found missing
// (display reset, Vext/power enable, ADC control, battery pin) where a macro names them. The list is split in
// HARD (pinBoardReservedHard: radio, SPI bus, Ethernet, SD) and SOFT (pinBoardReservedSoft: I2C, GPS, button,
// display reset, Vext, ADC control, battery): both refuse a user SET, only HARD is repaired at load.
// The user-function defaults (OneWire_GPIO, BUTTON_PIN, ANALOG_PIN) and BOARD_LED are NOT reserved: the
// defaults are exempt in pinCanAssign(), BOARD_LED is a "claimed" pin (pinBoardClaimed()).
#pragma once
#include <stdint.h>
#include <meshcom_settings.h>
#include "pin_policy.h"
#include "mc_board_id.h"

extern bool bONEWIRE;
extern bool bButtonCheck;
extern bool bAnalogCheck;
#if defined(ESP32)
extern bool bWINDON;
#endif

// WX_RES(x) is safe for a macro that is defined EMPTY (LORA_DIO0 on the T-Beam Supreme: "+ 0" makes it 0, which
// only reserves pin 0 = "off" anyway) and for one that is a runtime expression (SS, RST_LoRa: Arduino constants).
#define WX_RES(x) , (int)(x + 0)
// HARD: electrically busy whenever the firmware runs (radio, its SPI bus, Ethernet, SD card). Refused when set AND repaired at load.
inline uint64_t pinBoardReservedHard()
{
    const int reserved[] = {
        -1
    #if defined(LORA_CS)
    WX_RES(LORA_CS)
    #endif
    #if defined(LORA_NSS)
    WX_RES(LORA_NSS)
    #endif
    #if defined(LORA_RST)
    WX_RES(LORA_RST)
    #endif
    #if defined(LORA_RESET)
    WX_RES(LORA_RESET)
    #endif
    #if defined(LORA_DIO0)
    WX_RES(LORA_DIO0)
    #endif
    #if defined(LORA_DIO1)
    WX_RES(LORA_DIO1)
    #endif
    #if defined(LORA_DIO2)
    WX_RES(LORA_DIO2)
    #endif
    #if defined(LORA_DIO3)
    WX_RES(LORA_DIO3)
    #endif
    #if defined(LORA_SCK)
    WX_RES(LORA_SCK)
    #endif
    #if defined(LORA_MISO)
    WX_RES(LORA_MISO)
    #endif
    #if defined(LORA_MOSI)
    WX_RES(LORA_MOSI)
    #endif
    #if defined(LORA_IRQ)
    WX_RES(LORA_IRQ)
    #endif
    #if defined(LORA_IO0)
    WX_RES(LORA_IO0)
    #endif
    #if defined(LORA_IO1)
    WX_RES(LORA_IO1)
    #endif
    #if defined(LORA_IO2)
    WX_RES(LORA_IO2)
    #endif
    #if defined(SX1262X_CS)
    WX_RES(SX1262X_CS)
    #endif
    #if defined(SX1262X_IRQ)
    WX_RES(SX1262X_IRQ)
    #endif
    #if defined(SX1262X_RST)
    WX_RES(SX1262X_RST)
    #endif
    #if defined(SX1262X_GPIO)
    WX_RES(SX1262X_GPIO)
    #endif
    #if defined(SX1268_CS)
    WX_RES(SX1268_CS)
    #endif
    #if defined(SX1268_IRQ)
    WX_RES(SX1268_IRQ)
    #endif
    #if defined(SX1268_RST)
    WX_RES(SX1268_RST)
    #endif
    #if defined(SX1268_GPIO)
    WX_RES(SX1268_GPIO)
    #endif
    #if defined(SX126x_CS)
    WX_RES(SX126x_CS)
    #endif
    #if defined(SX126x_IRQ)
    WX_RES(SX126x_IRQ)
    #endif
    #if defined(SX126x_RST)
    WX_RES(SX126x_RST)
    #endif
    #if defined(SX126x_GPIO)
    WX_RES(SX126x_GPIO)
    #endif
    #if defined(SX126x_BUSY)
    WX_RES(SX126x_BUSY)
    #endif
    #if defined(RF95_SCK)
    WX_RES(RF95_SCK)
    #endif
    #if defined(RF95_MISO)
    WX_RES(RF95_MISO)
    #endif
    #if defined(RF95_MOSI)
    WX_RES(RF95_MOSI)
    #endif
    #if defined(RF95_NSS)
    WX_RES(RF95_NSS)
    #endif
    #if defined(RADIO_SCLK_PIN)
    WX_RES(RADIO_SCLK_PIN)
    #endif
    #if defined(RADIO_MISO_PIN)
    WX_RES(RADIO_MISO_PIN)
    #endif
    #if defined(RADIO_MOSI_PIN)
    WX_RES(RADIO_MOSI_PIN)
    #endif
    #if defined(RADIO_CS_PIN)
    WX_RES(RADIO_CS_PIN)
    #endif
    #if defined(RADIO_RST_PIN)
    WX_RES(RADIO_RST_PIN)
    #endif
    #if defined(RADIO_IRQ_PIN)
    WX_RES(RADIO_IRQ_PIN)
    #endif
    #if defined(RADIO_BUSY_PIN)
    WX_RES(RADIO_BUSY_PIN)
    #endif
    #if defined(RADIO_DIO1_PIN)
    WX_RES(RADIO_DIO1_PIN)
    #endif
    #if defined(RADIO_SCK)
    WX_RES(RADIO_SCK)
    #endif
    #if defined(RADIO_MISO)
    WX_RES(RADIO_MISO)
    #endif
    #if defined(RADIO_MOSI)
    WX_RES(RADIO_MOSI)
    #endif
    #if defined(RADIO_CTRL)
    WX_RES(RADIO_CTRL)
    #endif
    #if defined(RADIO_LDO_EN)
    WX_RES(RADIO_LDO_EN)
    #endif
    #if defined(PIN_LORA_NSS)
    WX_RES(PIN_LORA_NSS)
    #endif
    #if defined(PIN_LORA_DIO_1)
    WX_RES(PIN_LORA_DIO_1)
    #endif
    #if defined(PIN_LORA_NRST)
    WX_RES(PIN_LORA_NRST)
    #endif
    #if defined(PIN_LORA_BUSY)
    WX_RES(PIN_LORA_BUSY)
    #endif
    #if defined(PIN_LORA_SCK)
    WX_RES(PIN_LORA_SCK)
    #endif
    #if defined(PIN_LORA_MISO)
    WX_RES(PIN_LORA_MISO)
    #endif
    #if defined(PIN_LORA_MOSI)
    WX_RES(PIN_LORA_MOSI)
    #endif
    #if defined(E22_NSS)
    WX_RES(E22_NSS)
    #endif
    #if defined(E22_BUSY)
    WX_RES(E22_BUSY)
    #endif
    #if defined(E22_DIO1)
    WX_RES(E22_DIO1)
    #endif
    #if defined(E22_NRST)
    WX_RES(E22_NRST)
    #endif
    #if defined(E22_RXEN)
    WX_RES(E22_RXEN)
    #endif
    #if defined(E22_TXEN)
    WX_RES(E22_TXEN)
    #endif
    #if defined(E22_SCK)
    WX_RES(E22_SCK)
    #endif
    #if defined(ETH_CS_PIN)
    WX_RES(ETH_CS_PIN)
    #endif
    #if defined(ETH_INT_PIN)
    WX_RES(ETH_INT_PIN)
    #endif
    #if defined(ETH_RST_PIN)
    WX_RES(ETH_RST_PIN)
    #endif
    #if defined(ETH_SCLK_PIN)
    WX_RES(ETH_SCLK_PIN)
    #endif
    #if defined(ETH_MISO_PIN)
    WX_RES(ETH_MISO_PIN)
    #endif
    #if defined(ETH_MOSI_PIN)
    WX_RES(ETH_MOSI_PIN)
    #endif
    #if defined(SDCARD_CS)
    WX_RES(SDCARD_CS)
    #endif
    #if defined(SDCARD_SCLK)
    WX_RES(SDCARD_SCLK)
    #endif
    #if defined(SDCARD_MISO)
    WX_RES(SDCARD_MISO)
    #endif
    #if defined(SDCARD_MOSI)
    WX_RES(SDCARD_MOSI)
    #endif
    #if defined(TDECK_SDCARD_CS)
    WX_RES(TDECK_SDCARD_CS)
    #endif
    #if defined(TDECK_SPI_SCK)
    WX_RES(TDECK_SPI_SCK)
    #endif
    #if defined(TDECK_SPI_MISO)
    WX_RES(TDECK_SPI_MISO)
    #endif
    #if defined(TDECK_SPI_MOSI)
    WX_RES(TDECK_SPI_MOSI)
    #endif
    };
    uint64_t m = 0;
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        m |= pinBit(reserved[i]);   // entries < 0 ("not wired") give 0
    return m;
}

// SOFT: a convention while its function is off (I2C, GPS, button, display reset, Vext, ADC control, battery). Refused when a user SETS a pin, never repaired at load (a Heltec V2 node with `onewire gpio 13` keeps its sensor).
inline uint64_t pinBoardReservedSoft()
{
    const int reserved[] = {
        -1
    #if defined(I2C_SDA)
    WX_RES(I2C_SDA)
    #endif
    #if defined(I2C_SCL)
    WX_RES(I2C_SCL)
    #endif
    #if defined(I2C1_SDA)
    WX_RES(I2C1_SDA)
    #endif
    #if defined(I2C1_SCL)
    WX_RES(I2C1_SCL)
    #endif
    #if defined(SDA_PIN)
    WX_RES(SDA_PIN)
    #endif
    #if defined(SCL_PIN)
    WX_RES(SCL_PIN)
    #endif
    #if defined(GPS_RX_PIN)
    WX_RES(GPS_RX_PIN)
    #endif
    #if defined(GPS_TX_PIN)
    WX_RES(GPS_TX_PIN)
    #endif
    #if defined(GPS_EN_PIN)
    WX_RES(GPS_EN_PIN)
    #endif
    #if defined(GPS_PPS_PIN)
    WX_RES(GPS_PPS_PIN)
    #endif
    #if defined(BUTTON_PIN)
    WX_RES(BUTTON_PIN)
    #endif
    // docs/gpio-pin-map.md section 3: display reset, Vext / power enable, ADC control, battery pin. Only macros that are
    // a pin number in every env that defines them: RESET_OLED / VEXT_ENABLE are aliases to Arduino constants (RST_OLED,
    // Vext) that not every framework variant declares, and a LEVEL (1) on t114/t_echo; RST_OLED / OLED_RST are
    // `static const`, never macros.
    #if defined(PIN_DISPLAY_RST)
    WX_RES(PIN_DISPLAY_RST)
    #endif
    // VEXT_CTRL / ADC_CTRL: a pin number on the ESP32-S3 Heltec Wireless Tracker; the same names are unused leftovers
    // in the nRF52 variants (heltec_t114, t_echo), so only the ESP32 build counts them
    #if defined(ESP32) && defined(VEXT_CTRL)
    WX_RES(VEXT_CTRL)
    #endif
    #if defined(ESP32) && defined(ADC_CTRL)
    WX_RES(ADC_CTRL)
    #endif
    #if defined(PIN_PCB_VEXT)
    WX_RES(PIN_PCB_VEXT)
    #endif
    #if defined(ADC_CTRL_PIN)
    WX_RES(ADC_CTRL_PIN)
    #endif
    #if defined(BATTERY_PIN)
    WX_RES(BATTERY_PIN)
    #endif
    // Arduino variant SPI constants (not macros, always present): the radio bus on Heltec V2 and the E22 DevKits,
    // ordinary pins on T3-S3 or T-ETH-Elite whose radio uses other pins, so SOFT: refused for new settings,
    // never reset at load (GPIO-01 W1 advisor)
        WX_RES(SS)
        WX_RES(SCK)
        WX_RES(MISO)
        WX_RES(MOSI)
    };
    uint64_t m = 0;
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
        m |= pinBit(reserved[i]);   // entries < 0 ("not wired") give 0
    return m;
}

#undef WX_RES

inline PinChip pinBoardChip()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return PIN_CHIP_ESP32S3;
#elif defined(ESP32)
    return PIN_CHIP_ESP32;
#elif defined(NRF52_SERIES)
    return PIN_CHIP_NRF52840;
#else
    return PIN_CHIP_NONE;
#endif
}

// octal PSRAM/flash (qio_opi, opi_opi in the board JSON -> sdkconfig.h) takes GPIO 33-37 on the S3
inline bool pinBoardOctal()
{
#if defined(CONFIG_SPIRAM_MODE_OCT) || defined(CONFIG_ESPTOOLPY_OCT_FLASH)
    return true;
#else
    return false;
#endif
}

// Board default pin of a function; 0 = none. Wind has no default.
inline int pinBoardDefault(PinFunc f)
{
    int d = 0;
    switch (f)
    {
    case PIN_FN_ONEWIRE:
#if defined(OneWire_GPIO)
        d = (int)(OneWire_GPIO + 0);
#endif
        break;
    case PIN_FN_BUTTON:
#if defined(BUTTON_PIN)
        d = (int)(BUTTON_PIN + 0);
#endif
        break;
    case PIN_FN_ANALOG:
#if defined(ANALOG_PIN)
        d = (int)(ANALOG_PIN + 0);
#endif
        break;
    default: break;
    }
    return d > 0 ? d : 0;
}

// Effective pin of a function (a setting of 0 / unset means the board default), 0 = none.
inline int pinBoardEffective(PinFunc f)
{
    int v = 0;
    switch (f)
    {
    case PIN_FN_ONEWIRE: v = meshcom_settings.node_owgpio; break;
    case PIN_FN_BUTTON: v = meshcom_settings.node_button_pin; break;
    case PIN_FN_ANALOG: v = meshcom_settings.node_analog_pin; break;
#if defined(ESP32)
    case PIN_FN_WIND: v = meshcom_settings.node_wind_pin; break;
#endif
    default: break;
    }
    if (f == PIN_FN_ANALOG && v >= 99) v = 0;   // 99 = unset
#if defined(BOARD_TBEAM)
    if (f == PIN_FN_ONEWIRE && v == 16) v = 0;  // onewire_functions.cpp: stored 16 -> 0 -> OneWire_GPIO
#endif
    if (v <= 0) v = pinBoardDefault(f);
    return v;
}

// Pins of the OTHER enabled functions, plus the board LED pad.
inline uint64_t pinBoardClaimed(PinFunc except)
{
    uint64_t m = 0;
    if (except != PIN_FN_ONEWIRE && bONEWIRE) m |= pinBit(pinBoardEffective(PIN_FN_ONEWIRE));
    if (except != PIN_FN_BUTTON && bButtonCheck) m |= pinBit(pinBoardEffective(PIN_FN_BUTTON));
    if (except != PIN_FN_ANALOG && bAnalogCheck) m |= pinBit(pinBoardEffective(PIN_FN_ANALOG));
#if defined(ESP32)
    if (except != PIN_FN_WIND && bWINDON) m |= pinBit(pinBoardEffective(PIN_FN_WIND));
#endif
#if defined(BOARD_LED)
    m |= pinBit((int)(BOARD_LED + 0));
#endif
    return m;
}

inline PinContext pinBoardContext(PinFunc except)
{
    PinContext c;
    c.chip = pinBoardChip();
    c.octal = pinBoardOctal();
    c.reserved = pinBoardReservedHard();
    c.soft = pinBoardReservedSoft();
    c.claimed = pinBoardClaimed(except);
    c.table = pinBoardFor(MC_ENV_NAME);
    return c;
}

// A stored pin that the loader may keep: not forbidden and, for the wind input, not input-only (no pull-up).
// Strapping / claimed / unreviewed pins stay usable so no working setup breaks (GPIO-01).
inline bool pinBoardLoadedUsable(PinFunc f, int pin)
{
    if (pin == 0) return false;
    const PinClass k = pinClassify(pinBoardContext(f), pin);
    return k != PIN_FORBIDDEN && (k != PIN_INPUT_ONLY || f != PIN_FN_WIND);
}
