#if defined(NRF52_SERIES) && HAS_NRF_ETH

#include "eth_mss.h"
#include "meshcom_settings.h"   // meshcom_settings.node_ethmtu

/**
 * Issue #1183: the W5100S has no path-MTU discovery and sends full 1460-byte MSS
 * segments with DF set. Behind a tunnel with a path MTU below 1500 (HAMNET) the
 * first large web page chunk is blackholed and the socket times out. The user-set
 * Ethernet MTU therefore caps the MSS (MTU - 40) the web server advertises.
 *
 * Sn_MSSR is latched by the chip at Sock_OPEN (datasheet 3.2.9); writing it on a
 * socket that is already LISTENing changes nothing. So the target is pre-written
 * to every CLOSED socket (the register survives CLOSE, and whichever index
 * socketBegin() picks next latches it; a UDP socket inheriting it is harmless,
 * its datagrams stay far below 548 B), and a port-80 LISTEN socket that latched
 * an older value is closed and re-opened through server.begin().
 *
 * Called after every available() (which re-listens on a fresh socket) and once
 * after begin(). Steady state: SR (+MSSR) reads only, no writes. The clamp keeps
 * a corrupt stored value away from MSS 0 or above 1460.
 */
void ethApplyWebServerMss(EthernetServer &server)
{
    const uint16_t mss = ethMssTarget(meshcom_settings.node_ethmtu);

    // chip 0 means no Ethernet hardware (no SPI traffic then)
    const uint8_t chip = W5100.getChip();
    if (!chip)
        return;

    const uint8_t nsock = ethSockCount(chip);

    bool closedStale = false;

    // pass A: pre-write CLOSED sockets, close a stale port-80 LISTEN socket
    for (uint8_t i = 0; i < nsock; i++)
    {
        SPI.beginTransaction(SPI_ETHERNET_SETTINGS);

        const uint8_t sr = W5100.readSnSR(i);

        if (sr == SnSR::CLOSED)
        {
            if (W5100.readSnMSSR(i) != mss)
                W5100.writeSnMSSR(i, mss);
        }
        else if (sr == SnSR::LISTEN && EthernetServer::server_port[i] == 80 && W5100.readSnMSSR(i) != mss)
        {
            W5100.execCmdSn(i, Sock_CLOSE);
            EthernetServer::server_port[i] = 0;
            closedStale = true;
        }

        SPI.endTransaction();
    }

    if (!closedStale)
        return;

    // pass B: the socket just closed must hold the target before it is re-opened
    ethPrewriteClosedMss(mss, nsock);

    server.begin();   // socketBegin() takes the first CLOSED index and latches its MSSR
}

#endif
