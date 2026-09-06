#pragma once

// CTY-02 (issue #1133): single source of truth for the MeshCom server that a
// node talks to, derived from the country setting (--gateway srv OE/DL/IT,
// meshcom_settings.node_gwsrv) and the transport (HAMNET vs. plain Internet).
//
// Before this header the same country/transport matrix was spelled out three
// times -- ESP32 startMeshComUDP() (src/udp_functions.cpp) plus nRF52
// startUDP() and startFIXUDP() (src/nrf52/nrf_eth.cpp) -- and drifted: when
// the IT server was added, the DL arm on the Internet branch was replaced
// rather than extended, so every DL node on a normal Internet uplink silently
// landed on the OE server regardless of its stored setting. The nRF52 paths
// never carried a DL arm on their Internet branch at all.
//
// The two platforms need different shapes of the same answer: the ESP32 path
// has an async DNS resolver and wants a name, the nRF52 Ethernet path has no
// resolver and wants a literal. Both are returned from one table so they can
// never drift apart again -- .host and .ip are always the same endpoint.
//
// Pure, freestanding, no Arduino dependency, so test/test_gwsrv_select pins
// the whole matrix on the host (env:native).

#include <stdint.h>
#include <string.h>

struct GwSrvTarget
{
    const char *host;   // DNS name, or a dotted-quad string where there is no name
    uint8_t     ip[4];  // the same endpoint as a literal, for the resolver-less nRF52 path
    const char *path;   // "hamnet" | "inet" -- the [GW];srv marker's path field
};

// gwsrv: meshcom_settings.node_gwsrv (char[3], "OE"/"DL"/"IT"; anything else,
//        including the empty default, is treated as OE -- matching the
//        else-branch the call sites always had).
// hamnet: true when the node is on a 44/8 address or --sethamnet on is set.
static inline GwSrvTarget gwsrvSelect(const char *gwsrv, bool hamnet)
{
    // IT has no HAMNET presence, so it answers the same on both transports.
    if (memcmp(gwsrv, "IT", 2) == 0)
        return GwSrvTarget{ "meshcom.dig-italia.it", { 145, 239, 75, 155 }, "inet" };

    if (hamnet)
    {
        if (memcmp(gwsrv, "DL", 2) == 0)
            return GwSrvTarget{ "meshcom.hamnet.cloud", { 44, 148, 230, 197 }, "hamnet" };

        // --- reserved HAMNET arms ------------------------------------------
        // Uncomment and fill in to bring a further national HAMNET server up.
        // See the activation checklist at the bottom of this file -- the arm
        // alone is not enough, the country code must also be accepted by the
        // --gateway srv command.
        //
        // if (memcmp(gwsrv, "HB", 2) == 0)   // Switzerland
        //     return GwSrvTarget{ "<hamnet hostname>", { 44, 0, 0, 0 }, "hamnet" };
        //
        // if (memcmp(gwsrv, "US", 2) == 0)   // United States
        //     return GwSrvTarget{ "<hamnet hostname>", { 44, 0, 0, 0 }, "hamnet" };

        // OE HAMNET has no name in DNS; the literal is what the ESP32 path has
        // always handed to wifiDnsStart(), which short-circuits dotted quads.
        return GwSrvTarget{ "44.143.8.143", { 44, 143, 8, 143 }, "hamnet" };
    }

    // CTY-02: this arm is the fix. It existed as a bare literal before the IT
    // server was introduced and was overwritten rather than extended, which is
    // what made --gateway srv dl look like it was being ignored.
    if (memcmp(gwsrv, "DL", 2) == 0)
        return GwSrvTarget{ "meshcom.hamnet.network", { 192, 68, 17, 26 }, "inet" };

    // --- reserved Internet arms --------------------------------------------
    // Uncomment and fill in to bring a further national Internet server up.
    // See the activation checklist below.
    //
    // if (memcmp(gwsrv, "HB", 2) == 0)   // Switzerland
    //     return GwSrvTarget{ "<hostname>", { 0, 0, 0, 0 }, "inet" };
    //
    // if (memcmp(gwsrv, "US", 2) == 0)   // United States
    //     return GwSrvTarget{ "<hostname>", { 0, 0, 0, 0 }, "inet" };

    // OE is the default for every code that is not matched above, including the
    // empty setting a freshly flashed node starts with.
    return GwSrvTarget{ "meshcom.oevsv.at", { 89, 185, 97, 38 }, "inet" };
}

// ------------------------------------------------- adding a country: checklist
//
// The commented arms above are deliberately inert until three places agree.
// Uncommenting only the arm leaves it unreachable, because the command that
// sets the country rejects anything outside its own allow-list first.
//
// 1. This file: uncomment the arm(s) for the new code and fill in BOTH the
//    hostname and the literal it resolves to. The ESP32 path uses .host (it has
//    an async DNS resolver), the nRF52 Ethernet path uses .ip[] (it has none) --
//    if the two name different endpoints, the same node reaches different
//    servers depending on which board it runs on. A country with no HAMNET
//    presence needs only the Internet arm and should report path "inet" on both
//    transports, the way IT already does.
//
// 2. src/command_functions.cpp, the "gateway srv " handler: add the code to the
//    validation chain (`strCtry != "OE" && strCtry != "DL" && ...`) and to the
//    error text next to it, or --gateway srv <cc> answers "Gateway-Server fault"
//    and never stores the value.
//
// 3. test/test_gwsrv_select/: add the cell. The suite asserts host AND literal
//    together for every country precisely so a half-filled entry cannot ship.
//
// The country code is compared with memcmp(.., 2), so it is exactly two
// characters and case-sensitive uppercase -- the command upper-cases its
// argument before storing it. node_gwsrv is char[3]; a longer code does not
// fit and would need that field widened (it is persisted in NVS on ESP32 and in
// the nRF52 flash struct, so widening it is a flash-format change).
