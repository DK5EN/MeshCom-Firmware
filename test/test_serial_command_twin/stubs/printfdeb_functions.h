#pragma once

// Shadow of test/support/printfdeb_functions.h for the U3 serial-command
// twin.
//
// The shared shim (test/support/printfdeb_functions.h) provides INLINE no-op
// bodies for every overload -- fine for suites that need printfdeb() to be a
// harmless sink, wrong for this one: the whole point here is to capture what
// checkSerialCommand() printed (the "wrong command" text, the per-byte debug
// echo) and assert on it. Since those bodies are `inline`, this test cannot
// both include the shared shim AND define its own -- that is a redefinition,
// not an override. Declarations only; test_serial_command_twin.cpp defines
// printdeb(char) and printfdeb(const char*, ...), the two overloads
// checkSerialCommand() actually calls. The rest are declared (matching
// src/printfdeb_functions.h) so any transitively-included header that only
// *names* them still compiles; none of them is called from this
// translation unit, so none of them needs a definition.
//
// Reached the same way as the real header: a quote-include from
// src/esp32/serial_command_esp32.cpp / src/nrf52/serial_command_nrf52.cpp,
// resolved via the -I search path (this stubs/ directory is listed before
// test/support in platformio.ini, so it wins over the shared shim).

#include <Arduino.h>

int printlndeb(const char *buff);
int printdeb(const char *buff);

int printlndeb(int iVar);

int printdeb(int iVar);
int printdeb(unsigned int iVar);
int printdeb(short iVar);
int printdeb(float fVar);
int printdeb(char c);
int printdeb(unsigned char c);

int printlndeb(String str);
int printdeb(String str);

int printfdeb(const char *format, ...);

unsigned long printfdebDroppedBytes(void);
