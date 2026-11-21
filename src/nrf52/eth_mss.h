#pragma once

// Issue #1183 helpers shared by the web server (web_functions.cpp
// webApplyEthMss) and AppLink (src/nrf52/applink_nrf52.cpp): the W5100S
// latches Sn_MSSR at Sock_OPEN, so the MSS derived from --ethmtu is pre-written
// to CLOSED sockets before any server opens one.
//
// RAK4631 + RAK13800 only. Callers include it inside their BOARD_RAK4630 /
// HAS_NRF_ETH guard; it needs the W5100S library headers.

#include <stdint.h>
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <w5100.h>

// MSS = clamp(MTU, 1280..1500) - 40; a corrupt stored value never yields 0 or > 1460.
static inline uint16_t ethMssTarget(int mtu)
{
    if (mtu < 1280) mtu = 1280;
    if (mtu > 1500) mtu = 1500;
    return (uint16_t)(mtu - 40);
}

// Same cap as EthernetServer::available(): W5100S/W5100 have 4 sockets.
// chip 0 means no Ethernet hardware: the caller must not touch the bus then.
static inline uint8_t ethSockCount(uint8_t chip)
{
    uint8_t nsock = MAX_SOCK_NUM;
    if (nsock > 4 && (chip == 50 || chip == 51))
        nsock = 4;
    return nsock;
}

// Pre-write `mss` to every CLOSED socket whose register differs.
static inline void ethPrewriteClosedMss(uint16_t mss, uint8_t nsock)
{
    for (uint8_t i = 0; i < nsock; i++)
    {
        SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
        if (W5100.readSnSR(i) == SnSR::CLOSED && W5100.readSnMSSR(i) != mss)
            W5100.writeSnMSSR(i, mss);
        SPI.endTransaction();
    }
}
