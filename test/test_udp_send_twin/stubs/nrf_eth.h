// Shadow of src/nrf52/nrf_eth.h for the U2 twin (test plan 4.4).
//
// The real header declares a 40-member NrfETH whose definition drags in the
// W5100S driver, DHCP and NTP. The drain touches four members. They are
// copied verbatim from the real class body (checked by twin_stub_lint.py)
// and nothing else is declared, so a drain reaching for a fifth fails to
// compile rather than binding to a stub.
//
// sendUDP() and resetDHCP() are DEFINED BY THE TEST. On hardware
// NrfETH::sendUDP() is itself a thin wrapper over the C2 primitives
// (udpBeginRaw_nrf52/WriteRaw/EndRaw, see nrf_eth.cpp), so recording at this
// boundary records exactly the bytes that reach the socket layer -- the same
// boundary the ESP32 side is recorded at, one call deeper.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>

class NrfETH {

    public:

    bool hasIPaddress = false;

    bool sendUDP(uint8_t buffer [UDP_TX_BUF_SIZE], uint16_t rx_buf_size);

    //reset DHCP
    int resetDHCP();

    //flag indicates busy UDP RX or TX to avoid collisions
    bool udp_is_busy = false;
};
