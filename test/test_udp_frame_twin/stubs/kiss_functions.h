// Shadow of src/kiss_functions.h for the U1 twin (test plan 4.3).
//
// The real header turns queueKiss() into an inline no-op whenever ESP32 is
// not defined -- and this native build never defines ESP32. With the real
// header the KISS/TCP server-relay tap in handleUdpFrame_esp32()
// (udp_frame_esp32.cpp, upstream f070ad50, re-anchored after the C1/U1
// carve in the 2026-09-25 upstream merge) would compile to nothing here and
// no test could see whether it fires. This stub declares queueKiss() as a
// plain function instead; test_udp_frame_twin.cpp defines it and records
// every call. The other KISS entry points are not called by either frame
// handler and keep the real header's no-op form.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>

void queueKiss(uint8_t *buffer, uint16_t buflen, int16_t rssi, int8_t snr);

inline void kissSetup() {}
inline void kissLoop() {}
inline void kissStop() {}
inline void kissSetPassword(const char *) {}
inline void flushKissQueue() {}
inline bool isKissClientConnected() { return false; }
