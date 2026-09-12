// Issue 962 (docs/issue-962-deepsleep-verdict.md, section 6.4). See nrf52_sleep.h.

#include "nrf52_sleep.h"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRaWan-Arduino.h>   // defines the global `Radio` (radio/radio.h), used by
                               // nrf52_radio.cpp the same way

#include <configuration.h>    // angle-bracket include resolves per env (-I variants/<env>)
                               // to that board's own variants/<env>/configuration.h --
                               // pulls in PIN_VEXT_CTL / PIN_TFT_LEDA_CTL / PIN_TFT_VDD_CTL
                               // on Heltec T114; nothing needed from here for RAK4631 or T-Echo

#if defined(BOARD_T_ECHO)
#include "t_echo_utilities.h"   // Power_On_Pin -- a lightweight pin-macro header (no epaper libs)
#endif

// stop_advertising() (nrf52_ble.cpp) is declared in WisBlock-API.h, which also
// drags in mbed/rtos/ArduinoJson -- forward-declare it directly here instead,
// the same way command_functions.cpp already reaches across translation units
// for e.g. `extern bool bDEEP_SLEEP;`.
extern void stop_advertising();

#if defined(BOARD_T_ECHO)
// Battery-empty e-ink screen (nrf52_functions.cpp) -- not declared in
// nrf52_functions.h, same forward-declare approach as stop_advertising() above.
extern void Batterie_Vide_logo(void);
#endif

// Runtime wake-button GPIO (loop_functions.cpp:186). Seeded from the
// compile-time BUTTON_PIN at boot but overridable via `--button <pin>` /
// meshcom_settings.node_button_pin (nrf52_main.cpp:611-613); 99 means "no
// button configured" -- the same sentinel the ESP32 side uses
// (esp32_main.cpp:890-896, esp32_sleep.cpp) for the identical purpose.
extern uint8_t iButtonPin;

void nrf52EnterDeepSleep()
{
    // (a) Radio to sleep first, so it stops burning RX current while the rest
    // of this sequence runs. All three nRF52 boards use SX126x-Arduino and
    // share the same `Radio` global (nrf52_radio.cpp).
    Radio.Sleep();

    // (b) BLE: stop advertising and clear connection-state flags
    // (Bluefruit.Advertising.stop(), nrf52_ble.cpp). There is no simple
    // stored "current connection handle" global in this codebase to hand a
    // graceful Bluefruit.disconnect() (only per-callback parameters exist,
    // and BLEPeriph exposes no accessor for it) -- System OFF drops any BLE
    // link regardless, so this is enough.
    stop_advertising();

    // (c) Display off. RAK4631 has none.
    #if defined(BOARD_HELTEC_T114)
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);   // TFT backlight off
    digitalWrite(PIN_TFT_VDD_CTL, HIGH);    // TFT VDD off
    #endif

    #if defined(BOARD_T_ECHO)
    Batterie_Vide_logo();   // battery-empty e-ink screen, same as the old boardPWROff()
    #endif

    // (d) Peripheral rail off, one switch per board. BOARD_HELTEC_T114 and
    // BOARD_T_ECHO are mutually exclusive with each other and with the bare
    // RAK4631 build, so this #elif chain never double-fires.
    //
    // BOARD_RAK4630 is NOT RAK4631-exclusive: platformio.ini defines it in
    // nrf52_base.build_flags, which all three nRF52 envs extend (comment at
    // platformio.ini:97-102 -- "wird von heltec_t114 und t_echo ABSICHTLICH
    // mitgeerbt"), so heltec_t114 and t_echo also compile with BOARD_RAK4630
    // defined. The #elif below only reaches the RAK4631-only WB_IO2 cut
    // because the two prior arms already claim BOARD_HELTEC_T114 and
    // BOARD_T_ECHO; confirmed against variants/wiscore_rak4631/variant.h
    // (WB_IO2 = pin 34) and RAK19007's "IO2=0 -> 3V3_S off" documentation.
    #if defined(BOARD_HELTEC_T114)
    digitalWrite(PIN_VEXT_CTL, LOW);   // GPS/peripheral rail off
    #elif defined(BOARD_T_ECHO)
    digitalWrite(Power_On_Pin, LOW);   // peripheral rail off
    #elif defined(BOARD_RAK4630)
    // Power down the green and blue LEDs on the RAK4630 board
    digitalWrite(LED_GREEN, LOW);   // green LED off
    digitalWrite(LED_BLUE, LOW);    // blue LED off
    digitalWrite(WB_IO2, 0);   // 3V3_S off: W5100S, RAK12500 GPS, every WisBlock slot
    #endif

    // (e) Peripheral buses / USB down, so no clock or pull-up leaks through
    // System OFF (Meshtastic's nRF52 cpuDeepSleep() runs the same four calls
    // before sd_power_system_off()).
    Serial.end();
    // Uart::end() (Adafruit core) busy-waits on TXSTOPPED && RXTO after
    // issuing STOPRX/STOPTX -- those events never latch on a UARTE that was
    // never begun (or already ended), so an unconditional end() hangs
    // forever here. gps_functions.cpp's gpsScanBauds() calls Serial1.end()
    // after every baud probe on T114/T-Echo regardless of whether a GPS
    // answered, so a node with no GPS attached (or a silent one) boots with
    // Serial1 already ended -- exactly the case an unconditional call here
    // would hit. Uart::operator bool() reports _begun; only end() a port
    // that is.
    if (Serial1) Serial1.end();
    Wire.end();
    SPI.end();

    // (f) Wake source + sleep. All three boards define BUTTON_PIN
    // unconditionally, so iButtonPin is a real pin in practice -- still
    // guarded, for defensiveness and consistency with the ESP32 side's
    // treatment of an unconfigured/out-of-range button. `--button` accepts
    // 0..99 and iButtonPin takes that value verbatim (nrf52_main.cpp), but
    // systemOff()'s `g_ADigitalPinMap[pin]` lookup (wiring.c) does no bounds
    // check against PINS_COUNT (48 on all three variants) the way
    // pinMode()/digitalWrite() do -- an out-of-range pin would read past the
    // map. A node with no button configured, or misconfigured with
    // `--button` above 47, wakes only on RESET or USB plug-in (VBUS
    // DETECT), which sd_power_system_off() still honours.
    if (iButtonPin < PINS_COUNT)
    {
        systemOff(iButtonPin, LOW);   // active-low button, internal pull-up configured by systemOff() itself
    }
    else
    {
        sd_power_system_off();
    }
    // Neither call returns; wake is a hardware reset, exactly like
    // esp_deep_sleep_start() on the ESP32 side.
}
