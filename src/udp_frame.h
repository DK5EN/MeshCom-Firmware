#pragma once

// Carve for the U1 twin (test plan section 4.3, `test_udp_frame_twin`): the
// inbound UDP frame handler, one file per platform.
//
// THIS HEADER DECLARES NOTHING ON PURPOSE. Both handlers are already declared
// where their callers look for them -- handleUdpFrame_esp32() in
// udp_functions.h, handleUdpFrame_nrf52() in nrf52/nrf_eth.h, both established
// by C1 and unchanged by this move. Re-declaring them here would add a third
// spelling of a signature the campaign exists to stop having several of. What
// this file is for is the one thing that had no home: why the move happened
// and what the two copies do differently.
//
// WHY THE MOVE. C1 gave the two handlers the same signature and lifted the
// socket out of them, which is what the plan's C1 row asked for. It was not
// enough to test them -- for the third time in this campaign, and each time
// for the same reason:
//
//   C1/U1  bodies stayed in udp_functions.cpp (web_functions.h, ArduinoJson)
//          and nrf_eth.cpp (SPI, RAK13800_W5100S)
//   C2/U2  drains stayed in udp_functions.cpp and nrf52_main.cpp
//   C3/U3  checkSerialCommand() stayed in the two largest TUs in the tree
//
// None of those TUs compiles on a host, so no amount of signature matching
// made the function linkable into a native binary. All three rows named the
// call as the obstacle when the obstacle was the translation unit. Recorded
// here rather than in a commit message because the same trap is still open
// for every uncarved row of the plan.
//
// WHAT THE TWO COPIES DO DIFFERENTLY. Deliberately not merged: which of them
// is right is a drift-matrix decision, and the differences are the point.
//
//   - ESP32 returns void and swallows the over-MAX_ZEROS case; nRF52 returns
//     1 for it and its caller resets DHCP.
//   - ESP32 runs the RX-01 unconfigured-source guard on a GATE frame before
//     radiating it; nRF52 has no such guard.
//   - ESP32 reads the dedup gate BEFORE the position branch inserts the
//     msg_id (TM-31); the nRF52 path never had the early insert to begin with.
//   - ESP32 forwards to EXTUDP and drives both display paths
//     (sendDisplayText and sendDisplayPosition); nRF52 drives only the text
//     one.
//
// The twin turns each of those into a failing-on-change test rather than a
// comment; see test/test_udp_frame_twin.
