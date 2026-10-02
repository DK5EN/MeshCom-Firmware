// Shadow of src/nrf52/nrf_eth.h for the U2 twin (test plan 4.4).
//
// The real header declares a 40-member NrfETH whose definition drags in the
// W5100S driver, DHCP and NTP. The drain touches four members, plus a fifth
// (udp_dest_addr) added for DR-21: the drain does not read it TODAY, but the
// decided fix (nRF52 sendUDP() gets an early return on an unresolved
// destination) will, and the U1 twin's stub already carries this same line
// verbatim from src/nrf52/nrf_eth.h for the same field read elsewhere
// (udp_frame_nrf52.cpp's CONF guard). They are copied verbatim from the real
// class body (checked by twin_stub_lint.py) and nothing else is declared, so
// a drain reaching for a sixth fails to compile rather than binding to a
// stub.
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

    IPAddress udp_dest_addr;

    bool sendUDP(uint8_t buffer [UDP_TX_BUF_SIZE], uint16_t rx_buf_size);

    //reset DHCP
    int resetDHCP();

    //flag indicates busy UDP RX or TX to avoid collisions
    bool udp_is_busy = false;
};
