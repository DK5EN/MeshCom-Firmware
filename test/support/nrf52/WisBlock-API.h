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
} s_meshcom_settings;

extern s_meshcom_settings meshcom_settings;
