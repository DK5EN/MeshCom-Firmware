#pragma once

// Shadow of src/net_console.h for the U3 serial-command twin.
//
// The real header declares netConsoleRead()/netConsoleAvailable() only
// inside `#if defined(ESP32) && !defined(DISABLE_NET_CONSOLE)`. Neither macro
// is defined in this native build (ESP32 is never defined natively -- see
// serial_command.h and the platformio.ini comment above the two envs), so on
// real hardware the guard is what makes the net-console block ESP32-only;
// here it would just make the two functions vanish on BOTH sides, which
// breaks the ESP32 copy at compile time (it calls them unconditionally,
// guarded only by `#ifndef DISABLE_NET_CONSOLE`) rather than reproducing the
// platform drift the test exists to pin.
//
// This stub declares both unconditionally instead. It reaches the build by
// shadowing debugconf.h (this same directory), which is what actually
// includes "net_console.h" -- the real debugconf.h does the same include,
// as a quote-include resolved relative to debugconf.h's own directory, so
// only shadowing debugconf.h itself (not this file directly) gets a stub
// net_console.h picked up over src/net_console.h.
//
// Definitions live in test_serial_command_twin.cpp, backed by a byte queue
// the test feeds explicitly -- the same shape as SerialStub's input side.
int netConsoleRead();
bool netConsoleAvailable();
