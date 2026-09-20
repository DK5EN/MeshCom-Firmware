// Stand-in for src/nrf52/WisBlock-API.h, for the native test_ble_settings_v1
// suite (BLE settings v1 wire-format conversion).
//
// WHY THIS FILE IS SHAPED THE WAY IT IS
// ---------------------------------------------------------------------------
// src/nrf52/ble_settings_v1.h -- the header under test -- does a QUOTED
// include of "WisBlock-API.h", and it lives in src/nrf52/, the SAME
// directory as the real header. For a quoted include, the compiler searches
// the including file's own directory FIRST, before any -I path is even
// consulted (same fact test_nrf52_settings_paths's stub documents at
// length). No -I ordering can shadow "WisBlock-API.h" for that file; the
// real header WILL be opened.
//
// The real header pulls in <bluefruit.h>, <LoRaWan-Arduino.h>, FreeRTOS
// handle types, BLE characteristic/service types -- none of it buildable on
// a native host, and none of it needed by the conversion functions under
// test. The fix is the same "predefine the include guard" technique
// test_nrf52_settings_paths already uses: this file defines the SAME guard
// macro (SX126x_API_H) and is FORCE-INCLUDED ahead of every translation
// unit in this suite's env (see [env:native_ble_settings_v1] in
// platformio.ini and force_include_stub.py in this directory). By the time
// ble_settings_v1.h's own `#include "WisBlock-API.h"` line is reached, the
// guard is already defined, so the real file's entire body is skipped.
//
// SCOPE: only the struct is reproduced -- nothing else the real header
// declares (BLE/WiFi/LoRaWAN surface, flash function prototypes, globals)
// is needed by src/nrf52/ble_settings_v1.h/.cpp, which touch only the
// s_meshcom_settings TYPE, never the global `meshcom_settings` instance or
// any flash/BLE function.
//
// THIS STRUCT MUST STAY A FULL, FIELD-FOR-FIELD COPY of the real
// s_meshcom_settings (src/nrf52/WisBlock-API.h): same order, same types,
// same array bounds, same default member initialisers. The suite's golden
// byte image and every static_assert in ble_settings_v1.h check the REAL
// struct's contract; a stub that has drifted from it would make this suite
// pass while proving nothing about the shipped layout. There is no
// automated drift check registered for this stub (test/golden/
// twin_stub_lint.py's PAIRS list is outside this brief's file set) -- diff
// this struct body against src/nrf52/WisBlock-API.h by hand if either one
// changes.
#ifndef SX126x_API_H
#define SX126x_API_H

#ifndef NATIVE_BUILD
#error "test_ble_settings_v1/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h> // boolean (test/support/Arduino.h)
#include <cstdint>

// ---------------------------------------------------------------------------
// s_meshcom_settings -- the ONE definition, shared by both platforms since
// the D1-04 struct merge (src/meshcom_settings.h). This stub used to carry
// its own hand-copied struct body here; that copy is gone (D1-04 W3 step 4,
// Task 5) now that the real header has no platform includes of its own and
// can be pulled in directly -- one fewer place for the two to drift. Also
// defines MESHCOM_DATA_MARKER, so the standalone #define above it is gone
// too.
// ---------------------------------------------------------------------------
#include <meshcom_settings.h>


#endif // SX126x_API_H
