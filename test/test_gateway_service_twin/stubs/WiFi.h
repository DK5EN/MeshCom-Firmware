// Shadow of the Arduino-ESP32 core's WiFi.h for the U10 twin (test plan
// section 4.x, DR-15).
//
// gatewayService_esp32() reads exactly one thing off the real WiFi library:
// `WiFi.status() == WL_CONNECTED`, twice, to decide whether the heartbeat
// watchdog's two stages treat a silent server as "WiFi is down, reset now"
// or "WiFi is fine, the server itself is unresponsive, wait". Nothing else
// in this file touches WiFi/lwIP/the ESP-IDF network stack, so this shim
// carries only that one boolean, test-controlled via `mock_connected`
// instead of a real radio.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

enum wl_status_t
{
    WL_CONNECTED = 3,
    WL_DISCONNECTED = 6,
};

class WiFiClass
{
public:
    // Test-controlled: flip before calling gatewayService_esp32() to drive
    // DR-15's link-down vs. link-up-but-silent split.
    bool mock_connected = true;

    // Body (a recording sink) lives in test_gateway_service_twin.cpp.
    wl_status_t status();
};

extern WiFiClass WiFi;
