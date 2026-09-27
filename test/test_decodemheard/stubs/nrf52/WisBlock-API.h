// Minimal-Shim von nrf52/WisBlock-API.h fuer den env:native_parsers Testbuild
// (PT-01, BACKLOG SS3.8j).
//
// Schattet test/support/nrf52/WisBlock-API.h: env:native_parsers listet
// "-I test/test_decodemheard/stubs" vor "-I test/support" (platformio.ini),
// darum gewinnt diese Datei fuer alle hier kompilierten
// Uebersetzungseinheiten (Regexp.cpp, regex_functions.cpp, aprs_functions.cpp,
// via_functions.cpp), nicht nur fuer via -- und fuer native_topo_shadow, das
// die MHeard-Referenzkopie (test/test_topo_shadow/reference/) mitbaut.
//
// Ist deshalb ein Superset des test/support-Shims: alle Felder, die
// aprs_functions.cpp bereits braucht (siehe dort), PLUS node_via/node_utcoff,
// die via_functions.cpp bzw. die MHeard-Referenzkopie lesen. Feldtypen, -groessen
// und Default-Werte spiegeln das Original (src/nrf52/WisBlock-API.h).
//
// Absichtlich KEINE weiteren Felder: greift eine kuenftig getestete Funktion
// auf mehr zu, soll der Compiler das hier sichtbar machen, statt dass ein
// stillschweigend abweichender Shim falsche Sicherheit gibt.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_decodemheard/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
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

    // via_functions.cpp: checkVia() routing target
    char node_via[40] = {0};

    // test/test_topo_shadow/reference/mheard_functions.cpp: showPath()
    // timezone offset (hours) for the printed timestamp -- compiled
    // unconditionally (no board guard) so the field must exist.
    float node_utcoff = 0;
} s_meshcom_settings;

extern s_meshcom_settings meshcom_settings;
