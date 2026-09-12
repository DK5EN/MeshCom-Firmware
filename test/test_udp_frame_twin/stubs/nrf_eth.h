// Shadow of src/nrf52/nrf_eth.h for the U1 twin (test plan 4.3).
//
// The real header declares a 40-member NrfETH whose definition drags in the
// W5100S driver, DHCP and NTP. handleUdpFrame_nrf52() touches four of them
// (last_upd_timer, lora_tx_msg_len, udp_dest_addr, had_initial_udp_conn) plus
// its own declaration. Each declaration line below is copied verbatim from
// the real class body / the real free-function declaration -- same
// discipline as test/test_udp_send_twin/stubs/nrf_eth.h and checked the same
// way by eye against src/nrf52/nrf_eth.h (this test's own env is not wired
// into test/golden/twin_stub_lint.py, which only knows the U2 pair; see the
// report for that gap). A handler reaching for a fifth member fails to
// compile rather than binding to a stub.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>

// C1 carve-out (DRY unification U1): verbatim from src/nrf52/nrf_eth.h.
int handleUdpFrame_nrf52(unsigned char *inc_udp_buffer, int packetSize, IPAddress remote_ip);

class NrfETH {

    public:

    int lora_tx_msg_len;

    IPAddress udp_dest_addr;

    unsigned long last_upd_timer = 0; // last time we got a HB
    bool had_initial_udp_conn = false;  // indicator that we had already a udp connection
};
