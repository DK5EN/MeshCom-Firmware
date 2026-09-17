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
// WHAT THE TWO COPIES DO DIFFERENTLY. All four were open drift-matrix
// questions as of the M2 review; all four are now DECIDED (2026-09-12,
// docs/testplan/drift-matrix.csv, drift-matrix-review-verdict-20260912.md).
// STAND 2026-09-17 (Welle W6): DR-02, DR-05 und DR-18 sind IMPLEMENTIERT,
// zusammen mit DR-04, DR-06, DR-07, DR-08, DR-09 und DR-19 -- jede Zeile
// traegt jetzt einen test_agreement_*-Fall in
// test/test_udp_frame_twin/test_udp_frame_twin.cpp statt eines
// test_drift_*-Falls. OFFEN bleibt allein DR-20, und zwar bewusst: dessen
// Entscheidung verlangt, dass der AUFRUFER die Verbindungspolitik
// entscheidet, also muessen sich udp_functions.h und udp_functions.cpp
// mitaendern -- der Rueckgabetyp allein in dieser Datei zu drehen wuerde
// entweder nicht uebersetzen oder den Socket-Reset auf ESP32 still
// verlieren. Eigene Welle.
//
// Von DR-18 ist nur der TEIL (1), die FORMPARITAET, umgesetzt: der
// EXTUDP-Abzweig liegt auf beiden Plattformen in einem eigenen Typtest vor
// is_new_packet(). TEIL (2), der JSON-Ack, IST W6-Arbeit und steht noch aus --
// die Zeile benennt dafuer ausdruecklich die Stellen, die
// buildAckPhoneFrame()/addBLEOutBuffer() fuer 0x41 aufrufen, also DR-09s
// Stelle auf nRF52. Mit offen: der G2-EXPECTED-DIFF-Eintrag, der
// Ausgangstyp-Vertrag in docs/ext_udp_telemetry.md und die Korrektur in
// docs/ack-wer-hat-quittiert.md:266. Ausserdem fehlt der von der Zeile
// bestellte Test: je ein DEKODIERBARER Frame pro Typ durch beide Handler --
// heute deckt nur 0x3A positiv ab, 0x21 und 0x40 sind unbelegt (nachgewiesen:
// den Typtest auf beiden Seiten auf 0x3A zu verengen laesst die Suite gruen).
//
//   - DR-20 (return value / reset call): ESP32 returns void and swallows the
//     over-MAX_ZEROS case, calling resetMeshComUDP() itself; nRF52 returns 1
//     for it and its caller (NrfETH::getUDP()) resets DHCP. DECIDED
//     nrf52-correct on WHO decides: both handlers return a status: the
//     handler is a parser, not connectivity policy. The reset ITSELF stays
//     platform-specific (ESP32 resets the UDP socket, nRF52 resets DHCP --
//     there is no shared answer to want); only the caller-decides shape is
//     unified. ESP32's caller becomes getMeshComUDP() (udp_functions.cpp),
//     mirroring NrfETH::getUDP() -- not gatewayService_esp32().
//   - DR-02 (RX-01 unconfigured-source guard): ESP32 runs the guard on a
//     GATE frame before radiating it onto LoRa; nRF52 has no such guard.
//     DECIDED esp32-correct: port the guard to handleUdpFrame_nrf52() so an
//     unconfigured-source frame from an nRF52 gateway cannot defeat the
//     primary OnRxDone guard's purpose either.
//   - DR-05 (display paths / TM-31 shape): ESP32 reads the dedup gate BEFORE
//     the position branch inserts the msg_id (the documented TM-31 fix) and
//     also calls sendDisplayPosition() there; nRF52 never had the early
//     insert to begin with (the historical TM-31 bug does not reproduce on
//     either side today) and never calls sendDisplayPosition() at all
//     (only sendDisplayText()). DECIDED esp32-correct: port
//     sendDisplayPosition() to nRF52's position branch for display parity;
//     the TM-31 early-dedup difference is now a no-op and can be unified for
//     hygiene without behaviour risk, landing after DR-06's return-value
//     gate so a rejected frame doesn't reach it.
//   - DR-18 (EXTUDP forward): ESP32 forwards to EXTUDP only for recognised
//     msg_type_b (0x3A/0x21/0x40) INSIDE the relay branch; nRF52 forwards
//     unconditionally, BEFORE that type check. DECIDED: shape parity, not a
//     wider type set -- lift the EXTUDP forward out of the relay branch on
//     BOTH platforms into its own explicit type test (same 0x3A/0x21/0x40
//     set ESP32 already uses), kept ahead of is_new_packet() so duplicates
//     still reach EXTUDP as today. One edit together with DR-07 (its outer
//     hasExternIPaddress check becomes the guard on the lifted call) and
//     DR-19 (drops the (uint8_t) cast on the same line). A widened set
//     including ACK 0x41 was considered and WITHDRAWN -- see
//     docs/architecture/11-wire-format.md §3 and
//     docs/ack-wer-hat-quittiert.md §6.3 for the separate JSON-ack answer,
//     which is not this mechanism.
//
// The twin turns each of those into a failing-on-change test rather than a
// comment; see test/test_udp_frame_twin.
