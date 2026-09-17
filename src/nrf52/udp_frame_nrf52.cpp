// SPI and the W5100S driver are what nrf_eth.h's IPAddress comes from -- the
// Ethernet library defines it, not the Adafruit core. Included for the type
// only; this file touches no driver call.
#include "mc_text.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <Arduino.h>
#include <nrf_eth.h>
#include <debugconf.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include "dedup_functions.h"
#include <command_functions.h>
#include <time_functions.h>
#include <lora_setchip.h>
#include <lora_functions.h>
#include <extudp_functions.h>
#include "printfdeb_functions.h"
#include "via_functions.h"
#include "regex_functions.h"
#include "conf_frame.h"
#include "setlog_lines.h"
#include "udp_frame.h"

// C1/U1 carve of handleUdpFrame_nrf52() out of nrf_eth.cpp; see udp_frame.h
// for why it moved and why the two platform copies are not merged. Moved
// unchanged.

extern NrfETH neth;
// Defined in nrf_eth.cpp. The ESP32 bUDPLOG lives inside that file's
// `#if defined(ESP32)`, so on nRF52 this is a separate symbol with the
// same name -- see the note at nrf_eth.cpp's definition.
extern bool bUDPLOG;
extern String strSource_call;

/**@brief C1 carve-out (DRY unification U1): everything that happens once a
 * datagram is in the buffer -- no socket, no NTP, no DHCP. Returns 0 when the
 * frame was handled and 1 when it carried more than MAX_ZEROS zero bytes, in
 * which case the caller resets DHCP. Same signature as handleUdpFrame_esp32()
 * in udp_functions.h, so both sides can be linked into one native binary and
 * fed the same corpus (twin-differential).
 */
int handleUdpFrame_nrf52(unsigned char *inc_udp_buffer, int packetSize, IPAddress remote_ip)
{
  char source_call[MC_CALL_LEN_Z] = {0};
  char destination_call[MC_CALL_LEN_Z] = {0};

  // an extra buffer for udp tx, as we add other stuff (ID, RSSI, SNR, MODE)
  uint8_t convBuffer[UDP_TX_BUF_SIZE+5];

  // if more than n values are 00 we might have received a faulty message
  uint8_t zerocount = 0;

  for (int i = 0; i + 1 < packetSize; i+=2)
  {
    if (inc_udp_buffer[i] == 0x00 && inc_udp_buffer[i + 1] == 0x00)
    {
      zerocount += 2;
    }
    else
      zerocount = 0;
  }

  if(packetSize > 0 && bDEBUG)// && bDEBUG)
    printfdeb("[UDP_ETH] UDP zerocount: %i ? > 6\n", zerocount);

  if (zerocount <= MAX_ZEROS)
  {
    /* we now need to distinguish if we got a LoRa packet to send from the server
    or it is a config message. First 4 Bytes indicate if it is
    GATE: 0x47 41 54 45
    CONF: 0x43 4F 4E 46
    */

    // get the first 4 bytes of the incoming udp message
    char indicator_b[UDP_MSG_INDICATOR_LEN];

    memcpy(indicator_b, inc_udp_buffer, UDP_MSG_INDICATOR_LEN);

    char gate[] = "GATE";
    char conf[] = "CONF";
    char beat[] = "BEAT";

    if (memcmp(indicator_b, gate, UDP_MSG_INDICATOR_LEN) == 0)
    {

      if(bDEBUG)
        printfdeb("[GATE] Received a LoRa packet to transmit\n");

      neth.last_upd_timer = millis();

      neth.lora_tx_msg_len = packetSize - UDP_MSG_INDICATOR_LEN;
      if (neth.lora_tx_msg_len > UDP_TX_BUF_SIZE)
        neth.lora_tx_msg_len = UDP_TX_BUF_SIZE; // zur Sicherheit

      memcpy(RcvBuffer, inc_udp_buffer+UDP_MSG_INDICATOR_LEN, neth.lora_tx_msg_len);

      // send JSON to Extern IP
      if(bEXTUDP)
        sendExtern(true, (char*)"udp", RcvBuffer, (uint8_t)neth.lora_tx_msg_len, 0, 0);

      // printout message type
      uint8_t msg_type_b = RcvBuffer[0];

      switch (msg_type_b)
      {
        case 0x3A: DEBUG_MSG("UDP", "Received Textmessage"); break; // ':'
        case 0x21: DEBUG_MSG("UDP", "Received PosInfo"); break;
        case 0x40: DEBUG_MSG("UDP", "Received Weather"); break;
        default: DEBUG_MSG("UDP", "Received unknown"); break;
      }

      if (msg_type_b == 0x3A || msg_type_b == 0x21 || msg_type_b == 0x40)
      {
        bool bBLELoopOut = true;

        struct aprsMessage aprsmsg;
        
        // print which message type we got
        uint8_t msg_type_b_lora = decodeAPRS(RcvBuffer, neth.lora_tx_msg_len, aprsmsg);

        if(msg_type_b_lora > 0)
        {
          if(bDisplayInfo)
          {
            printBuffer_aprs((char*)"RX-UDP ", aprsmsg);
          }

          bool bUDPtoLoraSend = true;

          snprintf(source_call, sizeof(source_call), "%s", aprsmsg.msg_source_call);
          snprintf(destination_call, sizeof(destination_call), "%s", aprsmsg.msg_destination_call);

          mcAppendChar(aprsmsg.msg_source_path, sizeof(aprsmsg.msg_source_path), ',');
          mcAppend(aprsmsg.msg_source_path, sizeof(aprsmsg.msg_source_path), meshcom_settings.node_call);

          aprsmsg.msg_server = true;

          aprsmsg.msg_last_hw = BOARD_HARDWARE | 0x80; // hardware  last sending node
          aprsmsg.msg_source_mod = (getMOD() & 0xF) | (meshcom_settings.node_country << 4); // modulation & country

          if(bDEBUG)
          {
            printfdeb("RX-UDP Source-Path:%s\n",  aprsmsg.msg_source_path);
          }


          memset(convBuffer, 0x00, UDP_TX_BUF_SIZE);

          checkVia(aprsmsg);

          uint16_t size = encodeAPRS(convBuffer, aprsmsg);

          if(size > UDP_TX_BUF_SIZE)
              size = UDP_TX_BUF_SIZE;

          if(bDEBUG)
          {
            printfdeb("RX-UDP Check-payload (%i):%02X \n", size, msg_type_b);
          }

          // TM-39: raw & unconditional (printfdeb needs --debug and strips
          // ';' outside csv) -- classify by the same {SET}/{CET} prefixes
          // the dispatch below matches; everything else in a GATE frame is
          // a relayed mesh frame (position/text/hey) going back down to LoRa.
          {
            const char *gwRxType = "DATA";
            if(msg_type_b == 0x3A)
            {
              if(memcmp(aprsmsg.msg_payload, "{SET}", 5) == 0)
                gwRxType = "SET";
              else if(memcmp(aprsmsg.msg_payload, "{CET}", 5) == 0)
                gwRxType = "CET";
            }
            // DATA (a relayed mesh frame) is high-rate on a busy gateway: only with --udplog
            if(gwRxType[0] != 'D' || bUDPLOG)
              Serial.printf("[GW];rx;type;%s;len;%d;ms;%lu\n", gwRxType, packetSize, (unsigned long)millis());
          }

          if(msg_type_b == 0x3A)
          {
            if(memcmp(aprsmsg.msg_payload, "{SET}", 5) == 0)
            {
                sendDisplayText(aprsmsg, (int16_t)99, (int8_t)0);
            }
            else
            if(memcmp(aprsmsg.msg_payload, "{CET}", 5) == 0)
            {
                sendDisplayText(aprsmsg, (int16_t)99, (int8_t)0);
            }
            else
            if((strcmp(destination_call, "*") == 0 && !bNoMSGtoALL) || strcmp(destination_call, meshcom_settings.node_call) == 0 || CheckGroup(destination_call) > 0)
            {
                // wenn eine Meldung via UDP kommt und den eigene Node betrifft dann keine weiterleitung an LoRa TX
                if(strcmp(destination_call, meshcom_settings.node_call) == 0)
                    bUDPtoLoraSend=false;

                unsigned int iAckId = 0;

                int iAckPos=mcIndexOfStr(aprsmsg.msg_payload, ":ack");
                int iRefPos=mcIndexOfStr(aprsmsg.msg_payload, ":rej");
                int iEnqPos=mcIndexOfStrFrom(aprsmsg.msg_payload, "{", 1);

                if(strcmp(destination_call, "*") == 0)
                {
                  iAckPos=0;
                  iRefPos=0;
                  iEnqPos=0;
                }
                
                if(iAckPos > 0 || iRefPos > 0)
                {
                    unsigned int iAckId = (unsigned int)mcSliceToLong(aprsmsg.msg_payload, (size_t)(iAckPos+4), strlen(aprsmsg.msg_payload));
                    msg_counter = ((_GW_ID & 0x3FFFFF) << 10) | (iAckId & 0x3FF);

                    uint8_t print_buff[30];

                    print_buff[0]=0x41;
                    print_buff[1]=msg_counter & 0xFF;
                    print_buff[2]=(msg_counter >> 8) & 0xFF;
                    print_buff[3]=(msg_counter >> 16) & 0xFF;
                    print_buff[4]=(msg_counter >> 24) & 0xFF;
                    print_buff[5]=0x01;  // ACK
                    print_buff[6]=0x00;

                    int iackcheck = checkOwnTx(msg_counter);
                    if(iackcheck >= 0)
                    {
                        own_msg_id[iackcheck][4] = 0x02;   // 02...ACK
                        // DRY-21: von der ESP32-Kopie (udp_functions.cpp) abgedriftet —
                        // dort bekommt die App fuer die eigene Nachricht den ACK-Level
                        // 0x02 ("eigene Nachricht bestaetigt"); hier blieb es bei 0x01,
                        // die App zeigte auf nRF52-Gateways nie den vollen ACK-Status.
                        print_buff[5]=0x02;  // 02...ACK
                    }

                    // DRY-21: Debug-Ausgabe wie in der ESP32-Kopie — nach dem
                    // checkOwnTx (damit der ACK-Level stimmt) und mit der msg_id in
                    // MSB-Reihenfolge statt verdreht.
                    if(bDisplayInfo)
                        printfdeb("[UDP-MSGID] ack_msg_id:%02X%02X%02X%02X ACK...%02X\n", print_buff[4], print_buff[3], print_buff[2], print_buff[1], print_buff[5]);

                    addBLEOutBuffer(print_buff, 7);

                    if(strcmp(source_call, meshcom_settings.node_call) == 0)
                        bUDPtoLoraSend=false;

                    bBLELoopOut=false;
                }

                if(iEnqPos > 0)
                {
                  iAckId = (unsigned int)mcSliceToLong(aprsmsg.msg_payload, (size_t)(iEnqPos+1), strlen(aprsmsg.msg_payload));
                  mcTruncate(aprsmsg.msg_payload, sizeof(aprsmsg.msg_payload), (size_t)(iEnqPos));
                }

                if(iAckPos <= 0)
                {
                  if(!bGATEWAY)
                    sendDisplayText(aprsmsg, (int16_t)99, (int8_t)0);
                }

                aprsmsg.max_hop = aprsmsg.max_hop | 0x20;   // msg_app_offline true

                uint8_t tempRcvBuffer[UDP_TX_BUF_SIZE];

                aprsmsg.msg_last_hw = BOARD_HARDWARE | 0x80; // hardware  last sending node
                aprsmsg.msg_source_mod = (getMOD() & 0xF) | (meshcom_settings.node_country << 4); // modulation & country

                checkVia(aprsmsg);

                uint16_t tempsize = encodeAPRS(tempRcvBuffer, aprsmsg);

                addBLEOutBuffer(tempRcvBuffer, tempsize);

                bBLELoopOut=false;

                // DM message for lokal Node 
                if(iAckId > 0)
                {
                  strSource_call = source_call;
                  SendAckMessage(strSource_call, iAckId);
                }
            }
          }

          // Check dedup ring first (same check that LoRa RX path uses)
          uint8_t udp_mid[4] = {
              (uint8_t)(aprsmsg.msg_id),
              (uint8_t)(aprsmsg.msg_id >> 8),
              (uint8_t)(aprsmsg.msg_id >> 16),
              (uint8_t)(aprsmsg.msg_id >> 24)
          };

          if(is_new_packet(udp_mid))
          {
            int icheck = checkOwnTx(aprsmsg.msg_id);

            if(bDisplayInfo)
              printfdeb("OWN-TX-CHECK-UDP msg_id:%08X check:%i\n", aprsmsg.msg_id, icheck);

            if(icheck < 0)
            {
              // resend only Packet
              if(bUDPtoLoraSend)
              {
                // store last message to compare later on
                insertOwnTx(aprsmsg.msg_id);

                addTxRingEntry(convBuffer, size, RING_STATUS_DONE, "udp_rx", 0); // fire-and-forget, no retransmission for UDP relay

                if(bDisplayLog)
                {
                    char buf[96];
                    setlogFormatGwi(buf, sizeof(buf), aprsmsg.msg_id, aprsmsg.payload_type,
                                     aprsmsg.max_hop & 0x0F, aprsmsg.msg_source_call, (uint32_t)millis());
                    setlogPrint(buf);
                }

                addLoraRxBuffer(aprsmsg.msg_id, true);
                stat_newid.fetch_add(1); // S2: server-injected ids occupy dedup-ring slots too

                // add rcvMsg to BLE out Buff
                // size message is int -> uint16_t buffer size
                if(isPhoneReady == 1 && bBLELoopOut) // wird schon vorher abgehandelt
                {
                    if(bDEBUG)
                    {
                      printfdeb("RX-UDP addBLEOutBuffer\n");
                    }

                    addBLEOutBuffer(convBuffer, size);
                }
              }
            }
          }
        }
      }
    }
    else if (memcmp(indicator_b, conf, UDP_MSG_INDICATOR_LEN) == 0)
    {

      if(bDisplayInfo)
      {
        printdeb(getTimeString());
        printfdeb("[CONF] received from server\n");
      }

      // TM-39: raw & unconditional, so rx-by-type sums match total RX.
      // CONF (server-pushed callsign/lat/lon/alt) is not part of the
      // SET/CET/BEAT/DATA/OTHER taxonomy; kept as its own type. Since
      // b624bd33 the ESP32/RAK-WiFi getMeshComUDPpacket() recognizes CONF
      // too (used to fall into OTHER there).
      Serial.printf("[GW];rx;type;CONF;len;%d;ms;%lu\n", packetSize, (unsigned long)millis());

      neth.last_upd_timer = millis();

      neth.had_initial_udp_conn = true;

      // CONF-01: guard, parse and apply mirror the ESP32 handler
      // (src/udp_functions.cpp, commit b624bd33) via the shared
      // bounds-checked parseConfFrame() (src/conf_frame.cpp). Applied
      // only when the datagram's source matches the resolved gateway
      // server -- on nRF52 that is udp_dest_addr (the address
      // startUDP()/startFIXUDP() set as GATE/BEAT/CONF destination and
      // origin). remote_ip above was read fresh for this exact packet,
      // so unlike the ESP32 side (which tracks a separate "last seen rx
      // IP" across getUDP() calls) there is no staleness window here.
      if (packetSize < UDP_MSG_INDICATOR_LEN || packetSize > UDP_CONF_BUFF_SIZE)
      {
        printfdeb("[CONF] ignored: size %d out of bounds\n", packetSize);
      }
      else if (!(remote_ip == neth.udp_dest_addr))   // nRF52 IPAddress has no operator!=
      {
        printfdeb("[CONF] ignored: source %d.%d.%d.%d does not match gateway server %d.%d.%d.%d\n",
                   remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3],
                   neth.udp_dest_addr[0], neth.udp_dest_addr[1], neth.udp_dest_addr[2], neth.udp_dest_addr[3]);
      }
      else
      {
        ConfFrame cf;

        if (!parseConfFrame(inc_udp_buffer + UDP_MSG_INDICATOR_LEN, packetSize - UDP_MSG_INDICATOR_LEN, cf))
        {
          printfdeb("[CONF] ignored: malformed frame\n");
        }
        else
        {
          // lat/lon/alt: parsed for visibility, not applied -- same as
          // the ESP32 side.
          if(cf.hasLat)
            printfdeb("[CONF] lat received (not applied): %ld\n", (long)cf.lat);
          if(cf.hasLon)
            printfdeb("[CONF] lon received (not applied): %ld\n", (long)cf.lon);
          if(cf.hasAlt)
            printfdeb("[CONF] alt received (not applied): %ld\n", (long)cf.alt);

          String sCall = String(cf.call);
          sCall.trim();
          sCall.toUpperCase();

          if (!checkRegexCall(sCall))
          {
            printfdeb("[CONF] ignored: callsign <%s> from server not valid\n", sCall.c_str());
          }
          else
          {
            snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", sCall.c_str());

            if(cf.hasShort)
              snprintf(meshcom_settings.node_short, sizeof(meshcom_settings.node_short), "%s", cf.shortname);
            else
              snprintf(meshcom_settings.node_short, sizeof(meshcom_settings.node_short), "%s", convertCallToShort(meshcom_settings.node_call).c_str());

            printfdeb("[CONF] Call:%s Short:%s set from server\n", meshcom_settings.node_call, meshcom_settings.node_short);

            save_settings();

            // same auto-reboot as --setcall (src/command_functions.cpp:3452).
            // No T-Deck exception needed here -- BOARD_T_DECK/BOARD_T_DECK_PLUS
            // are ESP32-only board defines, never set in an nRF52 build.
            rebootAuto = millis() + 15 * 1000; // 15 Sekunden
          }
        }
      }
    }
    else if (memcmp(indicator_b, beat, UDP_MSG_INDICATOR_LEN) == 0)
    {

      // we got an heartbeat from server which we use to check connection (saving time we got it)
      if(bDEBUG)
      {
        printdeb(getTimeString());
        printfdeb(" [BEAT] Heartbeat from server\n");
      }

      // TM-39: raw & unconditional
      Serial.printf("[GW];rx;type;BEAT;len;%d;ms;%lu\n", packetSize, (unsigned long)millis());

      neth.last_upd_timer = millis();
      
      /**
       * TODO check HB accordingly to format not only BEAT at beginning
       * 15:16:08  <UDP_ETH> UDP Packet received with length: 22
        42 45 41 54 00 09 4F 45 31 4B 46 52 2D 47 57 01 05 4B 46 52 36 35
      */
    }
    else
    {
      printfdeb("[ERROR] Received udp message without indicator\n");
      // TM-39: raw & unconditional
      Serial.printf("[GW];rx;type;OTHER;len;%d;ms;%lu\n", packetSize, (unsigned long)millis());
      neth.last_upd_timer = millis();
    }

    return 0;   // handled
  }

  return 1;   // too many zeros
}
