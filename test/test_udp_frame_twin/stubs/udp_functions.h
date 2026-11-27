// Shadow of src/udp_functions.h for the U1 twin (test plan 4.3).
//
// The real header pulls the whole WiFi/DNS/NTP surface of udp_functions.cpp
// and, worse for this test, gates the two declarations the frame handler
// actually needs behind `#if defined(ESP32)`:
//
//   extern bool bUDPLOG;   -- the --udplog gate on the [GW];rx;type;DATA
//                             line of src/udp_frame.cpp
//
// This native build never defines ESP32 (there is no board), so the real
// header would leave bUDPLOG with no declaration at all. Declared here
// unconditionally instead, plus resetMeshComUDP() (declared in the real
// header without any ESP32 guard -- reproduced here so this stub is a
// complete substitute). The frame handler itself is declared in
// src/udp_frame_platform.h.
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
