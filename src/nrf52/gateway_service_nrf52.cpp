#include <SPI.h>
#include <RAK13800_W5100S.h>   // IPAddress for nrf_eth.h, same order as nrf_eth.cpp
#include <Arduino.h>
#include "loop_functions.h"
#include "loop_functions_extern.h"
#include "gateway_service.h"
#include <nrf_eth.h>
#include "printfdeb_functions.h"
#include "instrument.h"

// C4 carve-out of the gateway service block out of nrf52_main.cpp; see
// gateway_service.h for why it is carved but not unified. Moved unchanged.

extern NrfETH neth;
extern unsigned long iReceiveTimeOutTime;
void sendUDP(void);
void startRadioReceive(void);

void gatewayService_nrf52(void)
{
// get UDP & send UDP message from ringBufferOut if there is one to tx
if(bGATEWAY)
{
    INSTR_SECTION("gateway");
    int bUDPReceived = false;

    // check if we received a UDP packet
    if (neth.hasIPaddress)
    {
        bSPI_ETH_Active = true;   // SPI guard: Ethernet owns bus
        INSTR_SECTION("eth_udp");
        if(neth.getUDP() == 1)  // 1...no udp-paket received
        {
            { INSTR_SECTION("eth_udp_tx"); sendUDP(); }
        }
        else
        {
            bUDPReceived=true;

            if(bDEBUG)
                Serial.println("LOOP GATEWAY actions UDP received");
        }
        bSPI_ETH_Active = false;  // SPI guard: release bus
        if(bPendingRadioRx) { bPendingRadioRx = false; startRadioReceive(); }
    }
    else
    {
        //neth.last_upd_timer = 0; // ETH new
    }

    // UDP Action for next loop
    if(!bUDPReceived)
    {
        meshcom_settings.node_hasIPaddress = neth.hasIPaddress;
        meshcom_settings.node_last_upd_timer = neth.last_upd_timer;
        
        // check HB response (we also check successful sending KEEP. check if they work together!)
        if((uint32_t)(millis() - neth.last_upd_timer) >= (uint32_t)(MAX_HB_RX_TIME * 1000))
        {
            if(bDEBUG)
                Serial.println("LOOP GATEWAY last_upd_timer actions");

            neth.last_upd_timer = millis();

            // avoid TX and UDP
            if(!neth.hasIPaddress)
            {
                neth.hasIPaddress = false;
                iReceiveTimeOutTime = millis();

                if(strlen(meshcom_settings.node_ownip) > 6 && strlen(meshcom_settings.node_ownms) > 6 && strlen(meshcom_settings.node_owngw) > 6)
                {
                    if(bDEBUG)
                    {
                        Serial.print(getTimeString());
                        Serial.println(" [MAIN] initethETH fix-IP");
                    }

                    neth.initethfixIP();
                }
                else
                {
                    Serial.print(getTimeString());
                    Serial.println(" [MAIN] resetDHCP (retry)");

                    // N-20: initethDHCP() wuerde den W5100S bei jedem
                    // Retry per initETH_HW() hardware-resetten — danach
                    // braucht die PHY-Aushandlung mehrere Sekunden und der
                    // Link-Check in startETH() sieht dauerhaft LinkOFF:
                    // ein einmal gezogenes Kabel verbindet nie wieder (auf
                    // Hardware beobachtet). Das volle HW-Init ist nur beim
                    // Boot noetig (Setup); hier reicht resetDHCP() ohne
                    // PHY-Reset — der Link-Zustand ist dann echt, und bei
                    // LinkOFF bricht startETH() sofort ab statt 10 s zu
                    // blocken.
                    neth.resetDHCP();
                }
            }
        }
        // ETH-01: DHCP refresh moved above, ahead of this if(bGATEWAY)
        // block, so it also runs when bGATEWAY is off.
    }
}
else if(neth.hasIPaddress)
{
    // TM-45: the block above never runs while bGATEWAY is off, so it
    // never reads the socket -- do only the NTP-reply harvest instead
    // of the full gateway receive path (no double read: exactly one of
    // the two branches runs per loop pass).
    INSTR_SECTION("udp"); neth.harvestNTP();
}
}
