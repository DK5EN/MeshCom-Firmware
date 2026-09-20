// Minimal-Shim von nrf52/WisBlock-API.h fuer den nativen Testbuild
// (env:native_config, CS-03 / BACKLOG.md #3.8h).
//
// Vor D1-04 W3 (dem Struct-Merge) trug diese Datei eine eigene, von Hand
// gepflegte Kopie von struct s_meshcom_settings, weil config_json.cpp auf
// Nicht-ESP32-Plattformen nrf52/WisBlock-API.h hereinzog (BLE-, SoftDevice-
// und SDK-Header, die nativ nicht kompilieren) und davon nur der Struct
// gebraucht wurde. Seit dem Merge gibt es genau EINEN Struct
// (src/meshcom_settings.h, aus X-Makro-Listen generiert, fuer beide
// Plattformen) und config_json.cpp zieht ihn direkt via
// `#include <meshcom_settings.h>` herein -- diese Datei muss nur noch
// dieselbe Definition sichtbar machen, nicht mehr eine eigene, drift-
// faehige Abschrift davon pflegen. -I test/test_config_json/stubs steht in
// platformio.ini vor -I test/support UND vor -I src fuer den WisBlock-API.h-
// Pfad selbst; #include <meshcom_settings.h> darunter loest ganz normal
// gegen -I src auf (kein Namenskonflikt, andere Datei).
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_config_json/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <meshcom_settings.h>
