#include <Arduino.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <udp_functions.h>
#include <lora_functions.h>
#include <printfdeb_functions.h>
#include <debugconf.h>
#include "udp_drain.h"

// C2/U2 carve of sendMeshComUDP() out of udp_functions.cpp; see udp_drain.h
// for why it moved and why the two platform copies are not merged. Moved
// unchanged.

// State the drain reads, defined in udp_functions.cpp. These are file-scope
// there rather than declared in a header, so they are externed here; a native
// test TU provides its own definitions instead.
extern bool hasIPaddress;
extern IPAddress node_hostip;
extern bool udp_is_busy;
extern uint8_t err_cnt_udp_tx;
extern uint8_t convBuffer[];
void logRxDropUnconfigured(const char *call);

/**@brief UDP tx Routine
 */
void sendMeshComUDP()
{
    if(bWIFIAP)
      return;

    if(!hasIPaddress)
      return;

    if((uint32_t)node_hostip == 0)   // F6: Serveradresse noch nicht aufgeloest
      return;

    if(udpWrite != udpRead)
    {
        if(!udp_is_busy)
        {
            // CONC-16: snapshot the slot before the (comparatively slow) UDP
            // send touches it. addUdpOutBuffer() (CONC-16) can wrap the ring
            // and overwrite this exact slot from OnRxDone (nRF52 timer-
            // service task, see C-01) while Udp.write()/endPacket() below are
            // still running; everything from here on reads udpSnapshot, never
            // the live ring again.
            //
            // Sized past the source slot (UDP_TX_BUF_SIZE+20): the convBuffer
            // copy below reads from offset 1+36 for msg_len bytes, which can
            // run past what the producer actually wrote for a large msg_len
            // (pre-existing in ringBufferUDPout too, not introduced here) --
            // zero-filled so that tail is deterministic instead of reading
            // adjacent stack memory.
            static uint8_t udpSnapshot[UDP_TX_BUF_SIZE+64] = {0};
            int mySlot = udpRead;
#if defined(NRF52_SERIES)
            taskENTER_CRITICAL();
#endif
            memcpy(udpSnapshot, ringBufferUDPout[mySlot], sizeof(ringBufferUDPout[0]));
#if defined(NRF52_SERIES)
            taskEXIT_CRITICAL();
#endif
            uint16_t msg_len = (uint16_t)udpSnapshot[0];

            // send it over UDP

            udpBeginRaw_esp32();

            if (!udpWriteRaw_esp32(udpSnapshot + 1, msg_len))
            {
                if(bDisplayCont)
                  printlndeb("[ERROR]...Sending UDP Packet failed");

                err_cnt_udp_tx++;
                // if we have too much errors sending, reset UDP
                if (err_cnt_udp_tx >= MAX_ERR_UDP_TX)
                {
                    printfdeb("[WIFI-DBG] UDP TX error limit (%d) reached, calling resetMeshComUDP\n", MAX_ERR_UDP_TX);

                    // avoid TX and UDP
                    hasIPaddress = false;
                    meshcom_settings.node_hasIPaddress = hasIPaddress;
                    //cmd_counter = 50;

                    err_cnt_udp_tx = 0;
                    
                    resetMeshComUDP();
                    return;  // socket reset, don't call endPacket
                }
            }

            {
              bool tx_ok = udpEndRaw_esp32();
              udpCountTx(tx_ok);            // TM-31 instrument
              if(bUDPLOG)
                Serial.printf("[UDP];tx;ip;%s;port;%u;len;%u;ok;%d\n",
                              node_hostip.toString().c_str(), (unsigned)UDP_PORT,
                              (unsigned)msg_len, tx_ok ? 1 : 0);
            }

            // Der Slot enthaelt msg_len Bytes ab Offset 1: 36 Byte UDP-Header,
            // danach der APRS-Frame. msg_len Bytes ab Offset 1+36 zu kopieren
            // las immer 36 Bytes ueber das tatsaechlich Geschriebene hinaus
            // und gab decodeAPRS() eine um 36 zu grosse Laenge (der im
            // CONC-16-Commit dokumentierte Nebenbefund). Die wahre
            // APRS-Laenge ist msg_len-36.
            uint16_t aprs_len = (msg_len > 36) ? (uint16_t)(msg_len - 36) : 0;
            memcpy(convBuffer, udpSnapshot + 1 + 36, aprs_len);

            if(aprs_len > 0 && (convBuffer[0] == 0x3A || convBuffer[0] == 0x21 || convBuffer[0] == 0x40))
            {
              struct aprsMessage aprsmsg;

              // print which message type we got
              decodeAPRS(convBuffer, aprs_len, aprsmsg);

              // RX-01 (BACKLOG 3.8k), second door: this frame's UDP bytes
              // were already handed to Udp.write()/endPacket() above -- by
              // this point in the function the send has already happened,
              // so this check cannot prevent it. It still counts/marks the
              // leak (the primary guard in lora_functions.cpp's OnRxDone
              // should have kept an unconfigured source out of
              // ringBufferUDPout in the first place) and skips the debug
              // print for it.
              if(isUnconfiguredCall(aprsmsg.msg_source_call.c_str()))
              {
                logRxDropUnconfigured(aprsmsg.msg_source_call.c_str());
              }
              // print aprs message
              else if(bDisplayInfo)
              {
                printBuffer_aprs((char*)"TX-UDP ", aprsmsg);
              }
            }

            // zero out sent buffer and advance the read pointer under the same
            // lock as the writer's addRingPointer() (CONC-16). Guard against a
            // writer having already force-advanced udpRead past us via the
            // ring-full eviction path in addRingPointer() while we were
            // sending — extremely narrow (needs the ring to wrap completely
            // during one synchronous Udp.write()/endPacket()), but skipping
            // the advance in that case avoids a double-advance.
#if defined(NRF52_SERIES)
            taskENTER_CRITICAL();
#endif
            if (udpRead == mySlot)
            {
                memset(ringBufferUDPout[mySlot], 0, UDP_TX_BUF_SIZE);
                udpRead++;
                if (udpRead >= MAX_RING_UDP)
                    udpRead = 0;
            }
#if defined(NRF52_SERIES)
            taskEXIT_CRITICAL();
#endif

        }
        else
        {
            DEBUG_MSG("UDP", "UDP busy. Sending asap");
        }
    }
}