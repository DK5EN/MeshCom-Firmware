// debugconf.h fuer den nativen Testbuild.
//
// Das echte src/debugconf.h zieht net_console.h und damit WiFi/FreeRTOS nach.
// Nativ wird nur die Makro-Oberflaeche gebraucht.
#pragma once

#ifndef NATIVE_BUILD
#error "test/support/debugconf.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h>

#ifndef DO_DEBUG
#define DO_DEBUG 0
#endif

#define DEBUG_MSG(tag, ...) do { } while (0)
