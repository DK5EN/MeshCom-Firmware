// Minimal-Shim von nrf52/WisBlock-API.h fuer den nativen decodeTinyXML-Test.
//
// loop_functions.h zieht auf Nicht-ESP32-Plattformen das echte
// nrf52/WisBlock-API.h herein (BLE-, SoftDevice- und SDK-Header), das nativ
// nicht kompiliert. decodeTinyXML() (src/tinyxml_functions.cpp) schreibt in
// sechs meshcom_settings-Felder (node_parm_1, node_unit, node_values,
// node_parm_t, node_parm_id, node_utcoff) -- das ist der Ausschnitt, den
// dieser Shim traegt. Groessen gegen das Original gespiegelt
// (src/nrf52/WisBlock-API.h, zweiter s_meshcom_settings-Block: node_parm_t
// dort nur 25 Byte -- kuerzer als der erste Block/esp32_flash.h mit 150; ein
// laengeres <VT t="..."> Datetime-Attribut kann dort schon abgeschnitten
// werden. Siehe PT-01-Fund in test_decodetinyxml_datetime_overrun.
//
// Eigener Shim statt test/support/nrf52/WisBlock-API.h, weil der dortige
// (fuer die aprs_functions-Suiten) diese sechs Felder bewusst nicht traegt --
// -I test/test_decodetinyxml/stubs kommt vor -I test/support (platformio.ini)
// und ueberdeckt ihn fuer diese Env gezielt.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_decodetinyxml/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

typedef struct s_meshcom_settings
{
    char node_parm_1[100] = {0};
    char node_parm_t[25] = {0};
    char node_parm_id[100] = {0};
    char node_unit[50] = {0};
    char node_values[50] = {0};
    float node_utcoff = 0;
} s_meshcom_settings;

extern s_meshcom_settings meshcom_settings;
