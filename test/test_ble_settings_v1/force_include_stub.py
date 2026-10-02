"""Force-include the BLE settings v1 suite's WisBlock-API stub, C++ only.

Why a script rather than a build flag: src/nrf52/ble_settings_v1.h does
`#include "WisBlock-API.h"` -- quoted and unprefixed -- and lives in
src/nrf52/, next to the real header. A quoted include resolves against the
including file's own directory before any -I is consulted, so no -I
ordering can shadow it. The stub therefore predefines the real header's own
guard macro (SX126x_API_H) and must be seen BEFORE anything else, which is
what -include does.

It cannot go in build_flags or build_src_flags: both reach C translation
units too (Unity's unity.c among them), and force-feeding a C++ header into
a .c file dies at <cstdarg>. CXXFLAGS is the only place that is C++ only.

Used by [env:native_ble_settings_v1]. Same technique as
tools/force_include_settings_stub.py (test_nrf52_settings_paths), kept as
its own copy here because that script's STUB path is hardcoded to a
different suite's stub file.
"""

Import("env")  # noqa: F821  -- injected by SCons/PlatformIO

STUB = "test/test_ble_settings_v1/stubs/nrf52/WisBlock-API.h"

env.Append(CXXFLAGS=["-include", STUB])  # noqa: F821
