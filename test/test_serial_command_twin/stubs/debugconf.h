#pragma once

// Shadow of src/debugconf.h for the U3 serial-command twin.
//
// Reached via the angle-include `<debugconf.h>` in loop_functions.h and
// command_functions.h, both of which search this stubs/ directory before
// test/support/ and src/ (platformio.ini puts -I stubs first). Content is
// the real file's DO_DEBUG/DEBUG_MSG* ladder, unchanged -- the only edit is
// which net_console.h its own (quote) include resolves to: from a file that
// lives here, "net_console.h" resolves to stubs/net_console.h, which
// declares netConsoleRead()/netConsoleAvailable() unconditionally instead of
// only under `#if defined(ESP32)`. See stubs/net_console.h for why that
// matters.

#include "net_console.h"

#ifndef DO_DEBUG
#define DO_DEBUG 0
#endif

#if DO_DEBUG > 0
#define DEBUG_MSG(tag, ...)                \
	do                                   \
	{                                    \
		if (tag)                         \
        Serial.print("");\
		Serial.printf("  <%s> ", tag); \
		Serial.printf(__VA_ARGS__);      \
		Serial.printf("\n");             \
	} while (0)

#define DEBUG_MSG_VAL(tag, val, ...)                \
	do                                   \
	{                                    \
		if (tag)                         \
        Serial.print("");\
		Serial.printf("  <%s> ", tag); \
		Serial.printf(__VA_ARGS__);      \
		Serial.printf("  <%d> ", val); \
		Serial.printf("\n");             \
	} while (0)

#define DEBUG_MSG_TXT(tag, txt, ...)                \
	do                                   \
	{                                    \
		if (tag)                         \
        Serial.print("");\
		Serial.printf("  <%s> ", tag); \
		Serial.printf(__VA_ARGS__);      \
		Serial.printf("  %s ", txt); \
		Serial.printf("\n");             \
	} while (0)

#else
#define DEBUG_MSG(...)
#define DEBUG_MSG_VAL(...)
#define DEBUG_MSG_TXT(...)
#endif
