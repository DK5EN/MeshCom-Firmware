#ifndef NETMODE_LABEL_H
#define NETMODE_LABEL_H

// --info "NETWORK Mode:" label (RAK-NETMODE-01, #1289). node_netmode is only a stored WiFi/ETH switch
// on ESP32 boards with Ethernet; on nRF52 nothing sets it, the uplink is fixed by the build:
// RAK4631 + RAK13800 (HAS_NRF_ETH=1) is Ethernet, T114 (HAS_NRF_ETH=0) has no IP network.
//
// Pure C++, no Arduino includes: test/test_netmode_label/ runs it on the host.

inline const char *infoNetModeLabel(int netmode, bool nrf52, bool nrfEth)
{
    if (nrf52)
        return nrfEth ? "ETH" : "none";
    return netmode == 1 ? "ETH" : "WiFi";
}

#endif
