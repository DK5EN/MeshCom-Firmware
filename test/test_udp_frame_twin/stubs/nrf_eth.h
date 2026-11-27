// Shadow of src/nrf52/nrf_eth.h for the U1 twin (test plan 4.3).
//
// The real header declares a 40-member NrfETH whose definition drags in the
// W5100S driver, DHCP and NTP. The nRF52 platform hooks of the frame handler
// touch three of them (last_upd_timer, udp_dest_addr, had_initial_udp_conn).
// Each declaration line below is copied verbatim from the real class body --
// same
// discipline as test/test_udp_send_twin/stubs/nrf_eth.h and checked the same
// way against src/nrf52/nrf_eth.h by test/golden/twin_stub_lint.py, whose
// PAIRS list carries this file too. A handler reaching for a fifth member
// fails to compile rather than binding to a stub.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>

class NrfETH {

    public:

    IPAddress udp_dest_addr;

    unsigned long last_upd_timer = 0; // last time we got a HB
    bool had_initial_udp_conn = false;  // indicator that we had already a udp connection
};
