#if !defined(NRF52_SERIES) || HAS_NRF_ETH   // Ethernet only (hooks read neth); host twins do not define NRF52_SERIES
#include "udp_frame_platform.h"
#include <nrf_eth.h>
#include <loop_functions_extern.h>   // rebootAuto

// FORMAL-02 platform seam, nRF52 side (see udp_frame_platform.h).
extern NrfETH neth;

void udpPlatformNoteServerRx(unsigned long now_ms, bool initial_conn)
{
    neth.last_upd_timer = now_ms;
    if(initial_conn)
        neth.had_initial_udp_conn = true;
}

IPAddress udpPlatformServerAddr(void)
{
    return neth.udp_dest_addr;
}

void udpPlatformNoteRelayedFrame(void)
{
    // no LED flag on nRF52: bLED_ORANGE has no reader outside esp32_main.cpp
}

void udpPlatformScheduleConfReboot(unsigned long delay_ms)
{
    rebootAuto = millis() + delay_ms;   // the T-Deck defines are never set on nRF52
}

void udpPlatformKissTap(uint8_t *, uint16_t, int16_t, int8_t)
{
    // no KISS server on nRF52
}
#endif
