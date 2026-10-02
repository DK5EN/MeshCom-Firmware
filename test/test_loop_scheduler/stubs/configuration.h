// configuration.h fuer den nativen Testbuild von env:native_loop_scheduler.
//
// Schattet test/support/configuration.h (die -I test/test_loop_scheduler/stubs
// geht in platformio.ini vor -I test/support vor -I src). test/support's
// eigene configuration.h laesst absichtlich jedes ENABLE_* Sensor-Flag aus
// (kein Board zu simulieren -- nur configuration_global.h fuer die
// Ringgroessen). Der D1-10-Scheduler braucht aber ENABLE_BMP390/ENABLE_MC811/
// ENABLE_INA226/ENABLE_MCP23017 gesetzt, sonst existieren die zugehoerigen
// Tabelleneintraege in loop_scheduler.cpp gar nicht und der Twin-Test kann
// sie nicht pruefen. configuration_default.h (dieselbe Datei, die jede
// echte variants/<board>/configuration.h als letzte Zeile einbindet) traegt
// diese vier -- zusammen mit allen anderen Fleet-Defaults, die hier nicht
// stoeren, weil loop_scheduler.cpp/.h nur auf die vier oben genannten prueft.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_loop_scheduler/stubs/configuration.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h>

#ifndef CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_IDF_TARGET_ESP32S3 1
#endif

#include <configuration_global.h>
#include <configuration_default.h>
