// Minimal-Shim von nrf52/WisBlock-API.h fuer den nativen Testbuild
// (env:native_extern, PT-01 / BACKLOG.md #3.8j).
//
// loop_functions.h zieht auf Nicht-ESP32-Plattformen das echte
// nrf52/WisBlock-API.h herein (BLE-, SoftDevice- und SDK-Header), das nativ
// nicht kompiliert. Fuer die getExtern()/handleExternTelemetry()-Tests wird
// davon nur die Settings-Struktur gebraucht -- und von der nur die Felder,
// die aprs_functions.cpp (initAPRS()) UND extudp_functions.cpp
// (getExtern()/handleExternTelemetry()) tatsaechlich lesen/schreiben.
//
// Dieser Shim ist eine Obermenge von test/support/nrf52/WisBlock-API.h (das
// gleiche fuer test_hey_report/test_aprs_* dient): die dortigen
// aprs_functions.cpp-Felder plus die node_temp/.../node_alt-Felder, die
// handleExternTelemetry() beschreibt und die der sendPosition()-Aufruf
// danach wieder liest. -I test/test_getextern/stubs steht in platformio.ini
// vor -I test/support, dieser Shim ersetzt also den dortigen fuer
// env:native_extern.
//
// Absichtlich KEINE weiteren Felder: greift eine kuenftig getestete Funktion
// auf mehr zu, soll der Compiler das hier sichtbar machen, statt dass ein
// stillschweigend abweichender Shim falsche Sicherheit gibt.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_getextern/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

typedef struct
{
    // -- von aprs_functions.cpp (initAPRS() etc., siehe test/support-Shim) --
    char node_call[10] = {'X', 'X', '0', 'X', 'X', 'X', '-', '0', '0', 0x00};
    char node_symid = '/';
    char node_symcd = '#';
    char node_atxt[40] = {0};
    char node_aprsmc[10] = {0};
    int  node_gcb[6] = {0};
    int  node_country = 0;
    int  max_hop_text = 0;
    int  max_hop_pos = 0;

    // -- von extudp_functions.cpp: handleExternTelemetry() schreibt diese --
    // -- Felder aus dem "tele"-JSON, danach liest der sendPosition()-Aufruf --
    // -- sie wieder (Typen/Defaults gespiegelt aus src/nrf52/WisBlock-API.h) --
    double node_lat = 0.0;
    char   node_lat_c = ' ';
    double node_lon = 0.0;
    char   node_lon_c = ' ';
    int    node_alt = 0;
    float  node_temp = 0;
    float  node_hum = 0;
    float  node_press = 0;
    float  node_temp2 = 0;
    float  node_gas_res = 0;
    float  node_co2 = 0;
    int    node_press_alt = 0;
    float  node_press_asl = 0;
} s_meshcom_settings;

extern s_meshcom_settings meshcom_settings;
