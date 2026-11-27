#pragma once

// Platform seam of the gateway UDP receive handler (FORMAL-02, docs/
// udp-consolidation-20261127.md section 5.2) and its entry point. The handler
// (src/udp_frame.cpp) is shared logic; the things that really differ per
// platform sit behind these hooks, one definition per board:
//   src/esp32/udp_frame_platform_esp32.cpp   ESP32 (globals of udp_functions.cpp)
//   src/nrf52/udp_frame_platform_nrf52.cpp   nRF52 (the NrfETH instance `neth`)
// The native twin / differential tests define their own hooks that switch on
// the platform under test, because both platforms share one test binary.

// The one platform include selector: nRF52 needs the Ethernet headers before
// IPAddress exists, the other cores get it through Arduino.h.
#if defined(NRF52_SERIES)
#include <SPI.h>
#include <RAK13800_W5100S.h>
#endif
#include <Arduino.h>
#include <stdint.h>

// H1: a datagram from the server arrived. Stamps the platform's server-rx
// timestamp (heartbeat watchdog input) with now_ms; initial_conn also sets
// the platform's had_initial_udp_conn flag (CONF frames only).
void udpPlatformNoteServerRx(unsigned long now_ms, bool initial_conn);

// H2: the gateway server address this node resolved and sends GATE traffic
// to; the CONF source guard compares the datagram source against it.
IPAddress udpPlatformServerAddr(void);

// H3: a server frame was shown/relayed (ESP32: LED flag; nRF52: nothing).
void udpPlatformNoteRelayedFrame(void);

// H4: reboot delay_ms from now after a server-pushed CONF rename (ESP32 skips
// it on T-Deck, which keeps its own reboot policy).
void udpPlatformScheduleConfReboot(unsigned long delay_ms);

// H5 (P11): a server text/position frame also goes to a local KISS client.
// ESP32 queues it (queueKiss(), itself a no-op under DISABLE_KISS_TCP); nRF52
// has no KISS server, its hook is empty.
void udpPlatformKissTap(uint8_t *buf, uint16_t len, int16_t rssi, int8_t snr);

// The one handler of a datagram from the gateway server (src/udp_frame.cpp).
// Returns 0 when the frame was handled, 1 when it carried more than MAX_ZEROS
// zero bytes (the caller resets: ESP32 the UDP socket, nRF52 DHCP). The caller
// owns the busy flag and the wipe of buf; buf holds at least UDP_TX_BUF_SIZE
// bytes and packetSize <= UDP_TX_BUF_SIZE. On an nRF52 board without Ethernet
// the file compiles empty and nothing calls this.
int handleUdpFrame(unsigned char *buf, int packetSize, IPAddress src_ip);
