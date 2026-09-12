// Shadow of src/udp_functions.h for the U10 twin (test plan section 4.x,
// DR-14/DR-15).
//
// gatewayService_esp32() calls five externs the real header declares:
// getMeshComUDP(), sendMeshComHeartbeat(), resetMeshComUDP() unconditionally,
// sendMeshComUDP() via the real header's own `#include "udp_drain.h"` (also
// unconditional) -- and ntpHarvestUDP(), which the real header gates behind
// `#if defined(ESP32)` (TM-45's own comment there). This native build never
// defines ESP32, so the real header would leave ntpHarvestUDP() undeclared
// and gateway_service_esp32.cpp would fail to compile on the bGATEWAY-off
// branch alone. All five declared here unconditionally instead -- same
// discipline as test/test_udp_frame_twin/stubs/udp_functions.h, which faced
// the identical bUDPLOG/handleUdpFrame_esp32 problem for the frame handler.
//
// Bodies (recording sinks) live in test_gateway_service_twin.cpp, next to the
// call log they write into.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

void getMeshComUDP();
// sendMeshComUDP() is NOT re-declared here. It lives in the real
// src/udp_drain.h (the U2 carve moved it out of udp_functions.cpp), and
// test_udp_send_twin/stubs/udp_functions.h already set the discipline:
// include the real header rather than restate its declaration, so the
// two cannot drift. twin_stub_lint.py caught the restated copy here the
// first time this stub was added to its PAIRS list -- the spelling had
// already diverged (`()` against the real `(void)`).
#include "udp_drain.h"
void sendMeshComHeartbeat();
void resetMeshComUDP();
void ntpHarvestUDP();
