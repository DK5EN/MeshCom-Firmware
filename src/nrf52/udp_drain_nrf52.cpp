#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <Arduino.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <lora_functions.h>
#include <printfdeb_functions.h>
#include <debugconf.h>
#include <nrf_eth.h>
#include "udp_drain.h"

// C2/U2 carve of sendUDP() out of nrf52_main.cpp; see udp_drain.h for why it
// moved and why the two platform copies are not merged. Moved unchanged.

extern NrfETH neth;
extern uint8_t err_cnt_udp_tx;
// File-static in nrf52_main.cpp before the carve, and used by nothing but
// this function, so it moves here rather than becoming a global.
static uint8_t convBuffer[UDP_TX_BUF_SIZE+50];

/**@brief UDP tx Routine
 */
void sendUDP()
{
    if(udpWrite != udpRead)
    {
        if(bDisplayCont)
            Serial.printf("udpWrite:%i udpRead:%i neth.udp_is_busy:%i\n", udpWrite, udpRead, neth.udp_is_busy);

        if(!neth.udp_is_busy)
        {
            // CONC-16 (nRF52-Leser): der Schreiber addUdpOutBuffer() laeuft
            // ueber addNodeData() im Timer-Service-Task (OnRxDone, siehe
            // C-01) und kann diesen Slot per Ring-voll-Eviction ueberholen,
            // waehrend hier gesendet wird. Laenge und Payload deshalb als
            // Snapshot unter kurzem Lock lesen und den Index-Advance unten
            // gegen ein zwischenzeitliches Vorruecken sichern — gleiche
            // Behandlung wie sendMeshComUDP() in udp_functions.cpp (ESP32).
            // Snapshot bewusst groesser als der Quell-Slot und nullgefuellt
            // (siehe dortige Begruendung).
            static uint8_t udpSnapshot[UDP_TX_BUF_SIZE+64] = {0};
            int mySlot = udpRead;
            /*BISECT*/ memcpy(udpSnapshot, ringBufferUDPout[mySlot], sizeof(ringBufferUDPout[0]));

            uint16_t msg_len = udpSnapshot[0];

            // send it over UDP
            if (!neth.sendUDP(udpSnapshot + 1, msg_len))
            {
                Serial.printf("Sending UDP Packet failed <%i>!\n", msg_len);

                DEBUG_MSG("ERROR", "Sending UDP Packet failed!");

                err_cnt_udp_tx++;
                // if we have too much errors sending, reset UDP
                if (err_cnt_udp_tx >= MAX_ERR_UDP_TX)
                {
                    // avoid TX and UDP
                    neth.hasIPaddress = false;

                    Serial.print(getTimeString());
                    Serial.printf(" [MAIN] resetDHCP\n");

                    err_cnt_udp_tx = 0;
                    neth.resetDHCP();
                }
            }
            else
            {
                // UDP DATA Header 36 byte. Der Slot enthaelt msg_len Bytes ab
                // Offset 1 (Header + APRS-Frame); msg_len Bytes ab Offset 1+36
                // zu kopieren las 36 Bytes ueber das Geschriebene hinaus — bei
                // msg_len > 239 sogar ueber das Slot-Ende (Slot ist
                // UDP_TX_BUF_SIZE+20). Wahre APRS-Laenge ist msg_len-36.
                // (Nebenbefund aus dem CONC-16-Commit; auf nRF52-Gateways
                // aktiv — Schreiber ist addUdpOutBuffer() via addNodeData(),
                // auf Hardware am TX-UDP-Log verifiziert.)
                uint16_t aprs_len = (msg_len > 36) ? (uint16_t)(msg_len - 36) : 0;
                memcpy(convBuffer, udpSnapshot + 1 + 36, aprs_len);

                if(aprs_len > 0 && (convBuffer[0] == 0x3A || convBuffer[0] == 0x21 || convBuffer[0] == 0x40))
                {
                    struct aprsMessage aprsmsg;

                    // print which message type we got
                    decodeAPRS(convBuffer, aprs_len, aprsmsg);

                    // print aprs message
                    if(bDisplayVia)
                    {
                        printBuffer_aprs((char*)"[MESHu]...TX-UDP  ", aprsmsg);
                    }
                    else
                    {
                        if(bDisplayInfo)
                        {
                            printBuffer_aprs((char*)"TX-UDP  ", aprsmsg);
                        }
                    }
                }
            }

            // zero out sent buffer and advance the read pointer under the same
            // lock as the writer's addRingPointer() (CONC-16). Guard against a
            // writer having already force-advanced udpRead past us via the
            // ring-full eviction path while we were sending.
            /*BISECT*/ if (udpRead == mySlot)
            {
                memset(ringBufferUDPout[mySlot], 0, UDP_TX_BUF_SIZE);
                udpRead++;
                if (udpRead >= MAX_RING_UDP)
                    udpRead = 0;
            }

        }
        else
        {
            DEBUG_MSG("UDP", "UDP busy. Sending asap");
        }
    }
}