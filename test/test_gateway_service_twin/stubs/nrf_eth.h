// Shadow of src/nrf52/nrf_eth.h for the U10 twin (test plan section 4.x,
// DR-14/DR-15).
//
// gatewayService_nrf52() touches five NrfETH members. Two of them
// (last_upd_timer, and the class itself) already appear in the U1/U2 twins'
// own nrf_eth.h stub (test/test_udp_frame_twin/stubs/nrf_eth.h); that stub
// does not carry hasIPaddress, getUDP(), initethfixIP() or resetDHCP(),
// because handleUdpFrame_nrf52() never reads them. This is a SEPARATE stub
// in this suite's own stubs/ directory (not an edit of that one) carrying
// exactly the five gatewayService_nrf52() reads/calls, verbatim in shape
// from src/nrf52/nrf_eth.h -- same discipline as that stub's own comment: a
// caller reaching for a sixth member (udp_dest_addr, had_initial_udp_conn,
// lora_tx_msg_len, ...) fails to compile rather than silently binding to
// something wider than gatewayService_nrf52() actually needs.
//
// gatewayService_nrf52.cpp forward-declares sendUDP()/startRadioReceive()
// itself (its own lines 16-17), so this header does not repeat them -- their
// recording-sink bodies live in test_gateway_service_twin.cpp regardless.
//
// Declarations only; every method body (a recording sink, several test-
// controlled) lives in test_gateway_service_twin.cpp, next to the call log
// it writes into.
#pragma once

#ifndef NATIVE_BUILD
#error "test stub header used outside the native test build"
#endif

#include <Arduino.h>
#include <configuration.h>

class NrfETH
{

    public:

    bool hasIPaddress = false;

    int getUDP();

    // TM-45: harvest-only substitute for getUDP(), for a bGATEWAY-off node.
    void harvestNTP();

    unsigned long last_upd_timer = 0; // last time we got a HB

    //reset DHCP
    int resetDHCP();

    void initethfixIP();
};
