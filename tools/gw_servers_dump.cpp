// gw_servers_dump.cpp -- host helper for tools/tests/test_gw_servers_golden.py.
// Build: c++ -std=gnu++17 -I src src/gw_servers.cpp tools/gw_servers_dump.cpp
//
// Input (stdin, or one row per argv): "platform code own_ip hamnet_only own_ntp"
// separated by blanks; "-" stands for an empty code or an empty own_ntp.
// platform: esp32 | nrf_dyn | nrf_static   -- as the golden grid (nRF: own NTP "")
//           nrf_dyn_own | nrf_static_own   -- extra: nRF with own_ntp passed on
// Output: one JSON object per row, same order. The new code is called exactly as
// the platform callers will: gwResolveNode(code, mode, ownIp, ownNtp, hasResolver).
// "hamnet_only" in the output is the input setting; the static platforms map 0 to the legacy
// static rule (44.x address -> 1, else 0) before the call; other values pass through.
#include "gw_servers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>

static std::string dotted(const uint8_t ip[4])
{
    char b[24];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return b;
}

static std::string js(const std::string &s)
{
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

static int doRow(const std::string &line)
{
    std::istringstream is(line);
    std::string plat, code, ip, mode, ntp;
    if (!(is >> plat >> code >> ip >> mode >> ntp)) {
        fprintf(stderr, "bad row: %s\n", line.c_str());
        return 1;
    }
    if (code == "-") code.clear();
    if (ntp == "-") ntp.clear();

    unsigned a[4] = {0, 0, 0, 0};
    if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4) {
        fprintf(stderr, "bad ip: %s\n", ip.c_str());
        return 1;
    }
    uint8_t own[4] = {(uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3]};

    bool resolver = (plat == "esp32");
    std::string ownNtp = ntp;
    if (plat == "nrf_dyn" || plat == "nrf_static") ownNtp.clear();
    else if (plat != "esp32" && plat != "nrf_dyn_own" && plat != "nrf_static_own") {
        fprintf(stderr, "bad platform: %s\n", plat.c_str());
        return 1;
    }

    // Static nRF52 (startFIXUDP): mode 0 (unset/legacy) keeps the static 44er rule, every other
    // value is passed on to gwSelectNet unchanged (docs/gw-servers-unification.md section 4).
    uint8_t gmode = gwEffectiveMode((uint8_t)atoi(mode.c_str()), own,
                                    plat == "nrf_static" || plat == "nrf_static_own");

    GwTarget t;
    if (!gwResolveNode(code.c_str(), gmode, own, ownNtp.c_str(), resolver, &t)) {
        printf("{\"platform\": %s, \"code\": %s, \"own_ip\": %s, \"hamnet_only\": %s, \"own_ntp\": %s, \"error\": true}\n",
               js(plat).c_str(), js(code).c_str(), js(ip).c_str(), mode.c_str(), js(ntp).c_str());
        return 0;
    }
    // Host names are used only where a resolver exists (ESP32); the nRF52 uses the literals.
    std::string srv = (resolver && t.srv_host) ? std::string(t.srv_host) : dotted(t.srv_ip);
    std::string nt = (resolver && t.ntp_host) ? std::string(t.ntp_host) : dotted(t.ntp_ip);
    printf("{\"platform\": %s, \"code\": %s, \"own_ip\": %s, \"hamnet_only\": %s, \"own_ntp\": %s, "
           "\"srv\": %s, \"ntp\": %s, \"path\": %s, \"ownntp_ignored\": %s}\n",
           js(plat).c_str(), js(code).c_str(), js(ip).c_str(), mode.c_str(), js(ntp).c_str(),
           js(srv).c_str(), js(nt).c_str(), js(t.path).c_str(), t.ownntp_ignored ? "true" : "false");
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) rc |= doRow(argv[i]);
        return rc;
    }
    std::string line;
    while (std::getline(std::cin, line))
        if (!line.empty()) rc |= doRow(line);
    return rc;
}
