// Shadow of src/udp_functions.h for the U2 twin (test plan 4.4).
//
// The real header pulls the whole WiFi/DNS/NTP surface of udp_functions.cpp.
// The ESP32 drain uses the six declarations below. All six are copied
// verbatim and checked by test/golden/twin_stub_lint.py.
//
// udpBeginRaw_esp32()/udpWriteRaw_esp32()/udpEndRaw_esp32() are the C2 carve
// and are DEFINED BY THE TEST as a recording sink -- that is the whole point
// of C2 and the reason this twin can exist.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>

extern bool bUDPLOG;         // --udplog on/off, one line per datagram
void udpCountTx(bool ok);
bool udpBeginRaw_esp32();
bool udpWriteRaw_esp32(const uint8_t *buf, uint16_t len);
bool udpEndRaw_esp32();
void resetMeshComUDP();

#include "udp_drain.h"
