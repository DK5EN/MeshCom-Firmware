// Stand-in for src/nrf52/WisBlock-API.h, for the native nRF52 settings
// cutover suite (test_nrf52_settings_paths).
//
// WHY THIS FILE IS SHAPED THE WAY IT IS
// ---------------------------------------------------------------------------
// The real header is reached two different ways from the two real .cpp files
// under test:
//
//   - src/settings_schema.cpp (non-ESP32 branch) does
//         #include <nrf52/WisBlock-API.h>
//     an ANGLE-BRACKET include with the nrf52/ prefix -- ordinary -I
//     shadowing handles this the same way test_config_json/stubs/nrf52/
//     already does for config_json.cpp.
//
//   - src/nrf52/nrf52_flash.cpp and src/nrf52/settings_store_nrf52.cpp (the
//     product code this suite actually exercises) instead do
//         #include "WisBlock-API.h"
//     a QUOTED include with NO directory prefix, and both files physically
//     live in src/nrf52/ -- the SAME directory as the real header. For a
//     quoted include, the compiler searches the including file's own
//     directory FIRST, before any -I path is even considered (verified
//     empirically against this toolchain while building this suite: an -I
//     stub directory losing to a same-directory real header of the same
//     name). No -I ordering can shadow "WisBlock-API.h" for those two files;
//     the real header WILL be opened.
//
// The real header's body sits behind a classic `#ifndef SX126x_API_H /
// #define SX126x_API_H` guard (its own file-scope #ifndef name, from its
// original "SX126x-API.h" filename) and pulls in <bluefruit.h>,
// <LoRaWan-Arduino.h>, FreeRTOS handle types, BLE characteristic/service
// types -- none of it buildable on a native host, and none of it needed by
// the settings load/save paths under test.
//
// The fix used here: this file defines THE SAME GUARD MACRO, and is FORCE-
// INCLUDED (`-include .../nrf52/WisBlock-API.h`, see the build/verification
// command in the wave report) ahead of every translation unit in this
// suite's env. By the time nrf52_flash.cpp's or settings_store_nrf52.cpp's
// own `#include "WisBlock-API.h"` line is reached, the guard is already
// defined -- the compiler still opens the real file (unavoidable, quoted
// same-directory search), but `#ifndef SX126x_API_H` is now false, so its
// entire body (all the way to its closing #endif) is skipped, including
// every one of its problematic nested #includes. This is the standard
// "predefine the include guard" technique for overriding a header a caller
// can only quote-include from beside the real one.
//
// SCOPE: only what nrf52_flash.cpp / settings_store_nrf52.cpp /
// settings_schema.cpp actually use is declared below -- the struct is a
// full, field-for-field copy of the real s_meshcom_settings (required: the
// real settings_schema.cpp static_asserts every CFG_FIELD_LIST /
// SETTINGS_PERSIST_ONLY_LIST row's offsetof/sizeof against this exact type,
// so a narrowed-down struct the way test_config_json's shim uses would fail
// those asserts to compile at all) -- but none of the BLE/WiFi/LoRaWAN
// surface the real header also declares is reproduced, since neither .cpp
// under test touches any of it beyond a single BLEUart-shaped `g_ble_uart`
// (used only by the unrelated, uncalled ble_log_settings()).
#ifndef SX126x_API_H
#define SX126x_API_H

#ifndef NATIVE_BUILD
#error "test_nrf52_settings_paths/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h> // boolean, delay(), Serial (test/support/Arduino.h)
#include <cstdint>

// ---------------------------------------------------------------------------
// s_meshcom_settings -- the ONE definition, shared by both platforms since
// the D1-04 struct merge (src/meshcom_settings.h). This stub used to carry
// its own hand-copied struct body here; that copy is gone (D1-04 W3 step 4,
// Task 4(a)) now that the real header has no platform includes of its own
// and can be pulled in directly -- one fewer place for the two to drift.
// meshcom_settings.h also declares `extern s_meshcom_settings meshcom_settings;`
// itself, so that declaration is not repeated below.
// ---------------------------------------------------------------------------
#include <meshcom_settings.h>


// Flash -- declarations nrf52_flash.cpp's own definitions must match
// (flash_reset()/save_settings()/init_flash() ARE defined in nrf52_flash.cpp).
void init_flash(void);
bool save_settings(void);
void log_settings(void);
void flash_reset(void);
extern bool init_flash_done;

// Everything above this line is either the struct or a declaration that
// appears verbatim in src/nrf52/WisBlock-API.h (test/golden/twin_stub_lint.py
// checks this file against that one). Definitions this stub needs but the
// real header only declares, plus scaffolding the real header has no
// counterpart for at all (FakeBleUart), live in stub_only_globals.h instead
// -- see that file's top comment for why.
#include "stub_only_globals.h"

#endif // SX126x_API_H
