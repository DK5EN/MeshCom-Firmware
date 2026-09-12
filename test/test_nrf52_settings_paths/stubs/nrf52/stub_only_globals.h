// Stub-only scaffolding for the native nRF52 settings cutover suite
// (test_nrf52_settings_paths) -- deliberately kept OUT of
// nrf52/WisBlock-API.h (the paired file test/golden/twin_stub_lint.py checks
// against src/nrf52/WisBlock-API.h). That checker treats the stub as a
// strict subset of the real header: every ';'-terminated declaration line in
// the stub must appear verbatim somewhere in the real one, or it is flagged
// as drift. Everything below is either a DEFINITION the real header only
// declares (init_flash_done, log_settings()) or has no counterpart in the
// real header at all (FakeBleUart and its instance) -- keeping any of it in
// the paired file would make twin_stub_lint fail on lines that are correctly
// NOT verbatim, since they are not supposed to be.
//
// Included once, from the bottom of nrf52/WisBlock-API.h (after the real
// struct + verbatim declarations), so everything nrf52_flash.cpp and
// settings_store_nrf52.cpp need is still in scope after the guard-preempt
// trick fires -- see that file's top comment for why the forced -include
// only reaches this indirectly.
#pragma once

#ifndef NATIVE_BUILD
#error "test_nrf52_settings_paths/stubs/nrf52/stub_only_globals.h darf nur im nativen Testbuild verwendet werden"
#endif

// init_flash_done is declared `extern bool init_flash_done;` in the paired
// WisBlock-API.h stub (verbatim match against the real header's line 417).
// Its real definition lives in WisBlock-API.cpp, which is not part of this
// suite's compiled set -- this is that missing definition. C++17 inline
// variable: one definition, shared by every translation unit that includes
// this header.
inline bool init_flash_done = false;

// log_settings() is declared `void log_settings(void);` in the paired stub
// (verbatim match against the real header's line 415). Its real definition
// (WisBlock-API.cpp) prints the current settings and is not part of any
// load/save path this suite exercises -- a trivial no-op definition here is
// enough for the real nrf52_flash.cpp (which calls it) to link.
inline void log_settings(void) {}

// ble_log_settings() (nrf52_flash.cpp) is unused by every load/save path
// this suite exercises, but it is part of the same translation unit and
// must still compile and link: a minimal BLEUart-shaped stand-in with the
// one method (`printf`) that function calls. Not part of the real
// WisBlock-API.h surface at all -- pure test scaffolding, has no verbatim
// counterpart to match.
struct FakeBleUart
{
	int printf(const char *fmt, ...)
	{
		(void)fmt;
		return 0;
	}
};
inline FakeBleUart g_ble_uart;
inline bool g_ble_uart_is_connected = false;
