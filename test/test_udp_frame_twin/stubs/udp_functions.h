// Shadow of src/udp_functions.h for the U1 twin (test plan 4.3).
//
// The real header pulls the whole WiFi/DNS/NTP surface of udp_functions.cpp
// and, worse for this test, gates the two declarations the frame handler
// actually needs behind `#if defined(ESP32)`:
//
//   extern bool bUDPLOG;   -- read unconditionally at
//                             udp_frame_esp32.cpp:164 (the --udplog gate on
//                             the [GW];rx;type;DATA line)
//   void handleUdpFrame_esp32(...);
//
// On real hardware ESP32 is always defined for this TU, so both are always
// visible there. This native build never defines ESP32 (there is no board),
// so the real header would leave bUDPLOG with no declaration at all and
// udp_frame_esp32.cpp would fail to compile. Declared here unconditionally
// instead, plus resetMeshComUDP() (the real header's one call the frame
// handler makes on the too-many-zeros path, declared there without any
// ESP32 guard -- reproduced here so this stub is a complete substitute).
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>

extern bool bUDPLOG;         // --udplog on/off, one line per datagram

void resetMeshComUDP();

// C1 carve-out (DRY unification U1): verbatim signature, minus the ESP32
// guard around it in the real header (see the file comment above).
void handleUdpFrame_esp32(unsigned char inc_udp_buffer[500], int packetSize, IPAddress src_ip);
