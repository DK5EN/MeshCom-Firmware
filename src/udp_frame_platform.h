#pragma once

// Platform seam of the gateway UDP receive handlers (FORMAL-02, docs/
// udp-consolidation-20261127.md section 5.2). The handler bodies are shared
// logic; the four things that really differ per platform sit behind these
// hooks, one definition per board:
//   src/esp32/udp_frame_platform_esp32.cpp   ESP32 (globals of udp_functions.cpp)
//   src/nrf52/udp_frame_platform_nrf52.cpp   nRF52 (the NrfETH instance `neth`)
// The native twin / differential tests define their own hooks that switch on
// the platform under test, because both handlers share one test binary.
//
// Not a hook on purpose: D3 (nRF52 gateways display server text), kept as
// the inline `!bGATEWAY` guard in udp_frame_nrf52.cpp until it is flipped.

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
