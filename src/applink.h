#pragma once

// AppLink: the BLE phone protocol over a WebSocket on the LAN.
// Contract: docs/applink-api-contract.md. Plan: docs/applink-plan-firmware.md.
//
// This header is the platform API. The implementations live in
// src/esp32/applink_esp32.cpp and src/nrf52/applink_nrf52.cpp; everything they
// share (WebSocket codec, login, lockout, phone-link owner) is Arduino-free in
// src/applink_ws.h, src/applink_auth.h, src/phone_link.h.
//
// All functions run in the loop task only. Boards without AppLink get inline
// no-op stubs, so callers need no #if.

#include <stddef.h>
#include <stdint.h>

#include "ble_phone_drain.h"   // BlePhoneSend

#if defined(ESP32) && !defined(DISABLE_APPLINK)
#define HAS_APPLINK 1
#elif defined(NRF52_SERIES) && defined(HAS_NRF_ETH) && !defined(DISABLE_APPLINK)
#if HAS_NRF_ETH
#define HAS_APPLINK 1
#endif
#endif

// Persisted "--applink on": one bit of one settings word. Both come from here only; users write
// APPLINK_SSET_WORD(meshcom_settings) |= APPLINK_SSET_BIT.
// node_sset2 bit 0x0010 (the retired bGPSDEBUG, no reader or writer left). Why not the obvious ones:
//  - node_sset4 0x0100/0x0200 are cleared by prodLockNormalise() on every load and config import
//    (PRODLOCK_SSET4_CLEAR_NBR), 0x8000 by "--setlog off" (mask 0x7FFB); node_sset3 has all 16 bits in use;
//  - a bit above 15 does not survive the config JSON import (config_json.h clamps the sset words to 65535).
// node_sset2 is int (32 bit) on ESP32 and nRF52, stored whole (ble_settings_v1.cpp copies it 1:1), is not
// touched by prod_lock.h, and every writer uses a 32-bit AND mask that keeps bit 4 (test_config_json).
#define APPLINK_SSET_FIELD node_sset2
#define APPLINK_SSET_WORD(s) ((s).APPLINK_SSET_FIELD)
#define APPLINK_SSET_BIT 0x0010
// CONFIGURED marker (charge-LED pattern, charge_led_mode.h): bit 0x0010 is STALE on nodes that ran
// "--gpsdebug on" before v4.35p (2026-04). The value bit counts only together with the marker, which
// "--applink on|off" always writes. ACCEPTED RESIDUAL RISK (operator decision 2026-11-21): 0x2000 was
// written by "--checkmesh on" in released v4.35p (2083fdbd, May-June 2026). A node that ran both and
// never switched them off boots with AppLink on - only if --passwd is set, the login stays HMAC
// protected, and the boot prints "[APPLINK] on". Every node_sset2 bit had a writer at some point, so
// no bit choice in this word avoids it; "--applink off" clears it for good.
#define APPLINK_SSET_MARK 0x2000

// Boot decision: AppLink is on only if the word was written by "--applink on|off" and says on.
static inline bool applinkSettingOn(int word)
{
    return (word & (APPLINK_SSET_BIT | APPLINK_SSET_MARK)) == (APPLINK_SSET_BIT | APPLINK_SSET_MARK);
}

// The word after "--applink on|off": marker always set, value bit as requested.
static inline int applinkSettingWrite(int word, bool on)
{
    word |= APPLINK_SSET_MARK;
    return on ? (word | APPLINK_SSET_BIT) : (word & ~APPLINK_SSET_BIT);
}

// Runtime switch, restored from the settings bit at boot (defined in loop_functions.cpp).
extern bool bAPPLINK;

#if defined(HAS_APPLINK)

// Password changed (--passwd); trailing spaces are stripped inside. An empty
// password stops a running server and closes the session with 4403.
void applinkSetPassword(const char *pw);

// Start or stop the server to match bAPPLINK, the password and the network.
// Called after --applink on/off and once at boot; applinkLoop() also retries
// while the network is not up yet.
void applinkApplySetting();

// Loop task: accept, read, login, heartbeat, timeouts, flushing output.
void applinkLoop();

// Close the current session (if any) with an RFC 6455 close code, e.g. 4409
// when a BLE hello took the phone session over. Calls phoneSessionEnded(PL_NET).
void applinkClose(uint16_t code, const char *reason);

// A session has completed the login (contract 3.1 "ok").
bool applinkSessionUp();

// Drain sink: send one phone frame as one binary message.
// SENT when queued completely, BUSY when the TCP buffer is full (the drain
// retries), DOWN when no authenticated session exists.
BlePhoneSend applinkSendFrame(const uint8_t *buf, uint16_t len);

// One-line status for --applink and --info, NUL-terminated. Returns length.
size_t applinkStatus(char *out, size_t out_size);

#else

static inline void applinkSetPassword(const char *) {}
static inline void applinkApplySetting() {}
static inline void applinkLoop() {}
static inline void applinkClose(uint16_t, const char *) {}
static inline bool applinkSessionUp() { return false; }
static inline BlePhoneSend applinkSendFrame(const uint8_t *, uint16_t) { return BLE_SEND_DOWN; }
static inline size_t applinkStatus(char *out, size_t out_size)
{
    if (out && out_size)
        out[0] = 0;
    return 0;
}

#endif

// ETH-06: nrf_eth.cpp initETH_HW() calls this right before a W5100S hardware reset. The reset wipes every chip
// socket inside one loop pass, so applinkLoop() never sees "no IP"; afterwards the listener/session index may name
// a socket the chip handed to someone else (gateway UDP). Forget it without chip access and end the session (1006);
// applinkLoop() opens a new listener once Ethernet is up. No-op where there is no W5100S.
#if defined(HAS_APPLINK) && defined(NRF52_SERIES)
void applinkChipReset();
#else
static inline void applinkChipReset() {}
#endif
