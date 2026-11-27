// NrfETH stub for the frozen nRF52 handler (test_udp_frame_diff only; found before the twin's stub of the
// same name, which matches the real header line by line, see test/golden/twin_stub_lint.py).
// The frozen handler still writes neth.lora_tx_msg_len, a member the consolidation removed from the real
// class; everything else is the twin stub's.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>

class NrfETH {

    public:

    int lora_tx_msg_len;   // frozen handler only

    IPAddress udp_dest_addr;

    unsigned long last_upd_timer = 0; // last time we got a HB
    bool had_initial_udp_conn = false;  // indicator that we had already a udp connection
};
