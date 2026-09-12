// Minimal-Shim von nrf52/WisBlock-API.h fuer den nativen Testbuild.
//
// loop_functions.h zieht auf Nicht-ESP32-Plattformen das echte
// nrf52/WisBlock-API.h herein (BLE-, SoftDevice- und SDK-Header), das nativ
// nicht kompiliert. Fuer die aprs_functions-Tests wird davon nur die
// Settings-Struktur gebraucht — und von der nur die Felder, die
// aprs_functions.cpp tatsaechlich liest. Feldtypen und -groessen spiegeln
// das Original (src/nrf52/WisBlock-API.h), damit Wertebereiche und
// String-Laengen im Test denen der Hardware entsprechen.
//
// Absichtlich KEINE weiteren Felder: greift eine kuenftig getestete Funktion
// auf mehr zu, soll der Compiler das hier sichtbar machen, statt dass ein
// stillschweigend abweichender Shim falsche Sicherheit gibt.
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

typedef struct
{
    char node_call[10] = {'X', 'X', '0', 'X', 'X', 'X', '-', '0', '0', 0x00};
    char node_symid = '/';
    char node_symcd = '#';
    char node_atxt[40] = {0};
    char node_aprsmc[10] = {0};
    int  node_gcb[6] = {0};
    int  node_country = 0;
    int  max_hop_text = 0;
    int  max_hop_pos = 0;
    // U2 twin: the ESP32 drain mirrors its hasIPaddress into the settings
    // blob when the TX error limit trips. Verbatim from both platform
    // headers (esp32_flash.h:206, nrf52/WisBlock-API.h:372).
    bool node_hasIPaddress = false;
    // U1 twin: both frame handlers read/write node_short in the CONF branch,
    // and via_functions.cpp (checkVia) reads node_via. Verbatim size and
    // default from both platform headers (esp32_flash.h:18/:180,
    // nrf52/WisBlock-API.h:188/:341) -- the two agree on both fields.
    char node_short[6] = {0x58, 0x58, 0x58, 0x34, 0x30, 0x00};
    char node_via[40] = {0};
} s_meshcom_settings;

extern s_meshcom_settings meshcom_settings;

// U1 twin. A REAL DRIFT, not a stub convenience: this function has a
// different return type per platform --
//   esp32/esp32_flash.h:245     void save_settings(void);
//   nrf52/WisBlock-API.h:614    bool save_settings(void);
// The C++ mangled name ignores the return type, so both spell _Z13save_settingsv
// and nothing diagnoses the mismatch. This shim is the nRF52 one, so it carries
// the nRF52 signature. The U1 twin compiles BOTH frame handlers into one binary
// against this single declaration; that is sound only because neither call site
// uses the return value (esp32/udp_frame_esp32.cpp:428,
// nrf52/udp_frame_nrf52.cpp:416). If either ever does, this shim stops being
// able to serve both sides and the drift has to be resolved for real.
bool save_settings(void);
