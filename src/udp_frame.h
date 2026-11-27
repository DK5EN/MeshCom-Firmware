#pragma once

// The gateway UDP receive handler (FORMAL-02 consolidation, 2026-11-27;
// docs/udp-consolidation-20261127.md).
//
// ONE handler, handleUdpFrame() in src/udp_frame.cpp, serves both platforms:
// the ESP32 / RAK-WiFi gateway and the nRF52 Ethernet gateway. Until the
// consolidation there were two copies (src/esp32/udp_frame_esp32.cpp and
// src/nrf52/udp_frame_nrf52.cpp), carved out of udp_functions.cpp and
// nrf_eth.cpp by C1/U1 so a native test could link them, and kept apart on
// purpose while the drift between them was an open question
// (docs/testplan/drift-matrix.csv, DR-01..DR-25). The questions are answered
// and the copies are gone; this header is the contract of what is left.
//
// WHERE THINGS LIVE
//   src/udp_frame.cpp          the handler: registration reply, zero scan, GATE / CONF /
//                              BEAT / OTHER dispatch, relay, ACK, display, store-node hook
//   src/udp_frame_parse.h      the pure core (no globals, no Arduino): zero scan, indicator
//                              class, GATE span; the proof target of FORMAL-02
//   src/udp_frame_platform.h   the entry point's declaration and the platform hooks (H1..H5);
//                              bodies in src/esp32/udp_frame_platform_esp32.cpp and
//                              src/nrf52/udp_frame_platform_nrf52.cpp
//   this header                buildExternAckJson() / queueExternAck(), see below
//
// WHAT STAYS A PLATFORM DIFFERENCE (the hooks, nothing else)
//   H1 server-rx timestamp storage (ESP32 globals, nRF52 `neth` members), H2 the resolved
//   gateway server address for the CONF source guard, H3 the LED flag on a relayed frame
//   (ESP32 only), H4 the reboot after a server-pushed CONF rename (ESP32 skips T-Deck),
//   H5 the KISS tap (ESP32 only; nRF52 has no KISS server). The TU guard
//   `!defined(NRF52_SERIES) || HAS_NRF_ETH` keeps nRF52 boards without Ethernet compiling
//   the file empty.
//
// WHAT THE CALLER OWNS
//   The busy flag (udp_is_busy) and the wipe of the receive buffer: getMeshComUDP()
//   (udp_functions.cpp) on ESP32, NrfETH::getUDP() (nrf_eth.cpp) on nRF52. The handler
//   returns 0 (handled) or 1 (more than MAX_ZEROS zero bytes, DR-20); the caller resets what
//   its platform resets (ESP32 the UDP socket, nRF52 DHCP) -- the handler is a parser, not
//   connectivity policy. The payload copy and the re-encode buffer are the handler's own
//   static buffer, not a shared global.
//
// THE DECISIONS THE CONSOLIDATION FIXED (operator, 2026-11-27)
//   D1  every GATE frame from the server stamps server liveness (heartbeat watchdog, "Server:"
//       status line), whatever its type byte says; the OTHER bucket still does not (UDP-03).
//   D2  a new server position (0x21) enters the dedup ring at once on receipt on both
//       platforms, also when it is then not relayed (bGATEWAY_NOPOS, unconfigured source, a
//       known own tx), so the RF copy of it is a duplicate. The relay / reflect branches skip
//       their own insert for 0x21 (one slot, one stat_newid per id).
//   D3  nRF52 gateways display server DM / group / `*` text and answer {pong} / {MCP} like
//       ESP32 (the `!bGATEWAY` guard around sendDisplayText() is gone).
//   D4  the store-node hook classifies the payload as received, before the text branch cuts it
//       at `{NNN` (ESP32's order; nRF52 used to classify after the cut).
//   D5  a datagram under 4 bytes is class OTHER and a 4-byte GATE carries no type; nothing is
//       read from behind the datagram (udp_frame_parse.h).
//   C7  the "RX-UDP" debug line prints after the destination path was reset to the destination
//       call, on both platforms.
//   Earlier decisions (DR-02 unconfigured-source guard, DR-04 bGATEWAY_NOPOS, DR-05
//   sendDisplayPosition(), DR-06 decode gate, DR-07/18/19 EXTUDP forward, DR-09 ACK frame
//   attribution, DR-20 return value) hold for both platforms, one code path each.
//
// HOW IT IS TESTED
//   test/test_udp_frame_twin   the same datagram run as the ESP32 and as the nRF52 board (the
//                              test defines the hooks and switches them per side): agreement
//                              tests for everything shared, pins for the hooks, one regression
//                              test per decision above.
//   test/test_udp_frame_diff   the live handler against byte-frozen copies of the two
//                              pre-consolidation handlers (test/test_udp_frame_diff/frozen)
//                              over about 110k generated, corpus and mutated datagrams; the
//                              only differences allowed are the whitelisted classes D1..D5,
//                              C7 and REORDER (a moved debug print), each counted and asserted
//                              exercised.
//   test/golden/carve_extern_lint.py  compares the handler's hand-written externs with their
//                              definitions.

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "ack_attribution.h"   // ackAttrCallLen() -- see buildExternAckJson() below

// DR-18 part 2 (docs/ack-wer-hat-quittiert.md §6.3): the outbound EXTUDP ack
// status datagram, {"type":"ack","msg_id":...,"status":...,"from":...,
// "via":...}. Declared HERE rather than in extudp_functions.h (its natural
// home) because that header is outside this carve's file set; udp_frame.cpp
// already includes this one.
// Defined in extudp_functions.cpp, queued through the SAME ring buffer
// queueExtern() uses (flushed once per main loop pass, never sent from
// OnRxDone) -- an entry marked JSON is sent verbatim on flush instead of
// going through sendExtern()'s decodeAPRS()-based type switch, which is
// deliberately NOT widened for 0x41 (see the DR-18 bullet above; that
// widening was considered and withdrawn).
void queueExternAck(uint32_t msg_id, uint8_t status, const char *from, const char *via);

// Pure JSON builder queueExternAck() uses -- declared here, not in
// extudp_functions.cpp only, so it is also reachable (and native-pinnable)
// from this header's other two includers without pulling in ArduinoJson:
// native_udp_frame_twin (test/test_udp_frame_twin) has no ArduinoJson
// lib_dep, unlike native_extern, and platformio.ini is outside this carve's
// file set to extend. Hand-rolled rather than ArduinoJson for that reason --
// extern_notice_json.h's externNoticeJson() is the ArduinoJson-based sibling
// for a context (native_extern) that already links ArduinoJson. `from` is
// embedded only when ackAttrCallLen() accepts it (the same [A-Z0-9-] gate
// the wire attachment already uses in ack_attribution.h) -- cheap insurance
// against a JSON special character reaching this string unescaped, and it
// doubles as the doc's "from weggelassen wenn nicht bekannt" rule. Returns
// the number of bytes written (excluding the NUL), 0 if `out` is NULL, too
// small, or the encoding failed.
static inline size_t buildExternAckJson(char *out, size_t out_len, uint32_t msg_id,
                                        uint8_t status, const char *from, const char *via)
{
    if(out == nullptr || out_len == 0)
        return 0;

    char c_msg_id[9];
    snprintf(c_msg_id, sizeof(c_msg_id), "%08X", (unsigned)msg_id);   // uint32_t ist nicht ueberall unsigned int

    int n;
    // ADVISOR-BEFUND W6b (F5): `from` lief durch ackAttrCallLen() und war damit
    // auf [A-Z0-9-] begrenzt, `via` aber wurde ROH interpoliert. Nachgewiesen
    // durch Uebersetzen: via = u"dp ergibt {... "via":"u"dp"} -- kein gueltiges
    // JSON mehr, und der Empfaenger ist MCProxys normalize_extudp_ack().
    // Heute uebergeben beide Aufrufstellen das Literal "udp", der Fehler war
    // also latent; der Kommentar darueber behauptete aber Schutz, den es fuer
    // dieses Feld nicht gab. Jetzt gilt fuer via dieselbe Schranke wie fuer
    // from: was sie nicht passiert, wird weggelassen statt eingebettet.
    // ackAttrCallLen() ist die falsche Schranke fuer via: sie laesst nur
    // GROSSBUCHSTABEN durch, und beide Aufrufstellen uebergeben "udp" --
    // damit waere das Feld immer leer gewesen. via ist kein Rufzeichen,
    // sondern ein Transportname. Eigene Schranke: nur Buchstaben, Ziffern,
    // '-' und '_', hoechstens 12 Zeichen. Alles andere -- und damit jedes
    // JSON-Sonderzeichen -- fuehrt zum leeren Feld statt zu kaputtem JSON.
    char via_safe[13] = {0};
    if(via)
    {
        size_t vn = 0;
        for(; via[vn] && vn < sizeof(via_safe) - 1; vn++)
        {
            char c = via[vn];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                   || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if(!ok) { vn = 0; break; }
            via_safe[vn] = c;
        }
        if(via[vn] != 0 && vn == sizeof(via_safe) - 1)
            vn = 0;                       // zu lang -> weglassen, nicht kuerzen
        via_safe[vn] = 0;
    }

    if(ackAttrCallLen(from) > 0)
        n = snprintf(out, out_len,
                     "{\"type\":\"ack\",\"msg_id\":\"%s\",\"status\":%u,\"from\":\"%s\",\"via\":\"%s\"}",
                     c_msg_id, (unsigned)status, from, via_safe);
    else
        n = snprintf(out, out_len,
                     "{\"type\":\"ack\",\"msg_id\":\"%s\",\"status\":%u,\"via\":\"%s\"}",
                     c_msg_id, (unsigned)status, via_safe);

    if(n <= 0 || (size_t)n >= out_len)
        return 0;

    return (size_t)n;
}
