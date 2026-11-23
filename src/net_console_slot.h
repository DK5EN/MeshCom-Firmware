#pragma once

// F2-10-SLOT: the console has ONE handshake slot (s_hs_running). A peer that connects and then stays
// silent would hold it for the whole 10 s handshake deadline and turn every honest client away
// ("Rejected: auth already in progress"), repeatable without a password.
//
// Rule: a new connection may take the slot over from a holder that has not sent a single byte for
// NET_CONSOLE_HS_SILENT_MS. A holder that is sending is never preempted (otherwise an attacker could
// kick honest clients mid-handshake). Formal model: specs/tla/netconsole.tla (Takeover).
//
// 3 s: an honest client answers the NONCE right away (one RTT plus an HMAC on the client); even a slow
// LAN/WLAN link needs well under 1 s. 3 s leaves a margin and still ends the lock-out three times
// faster than the 10 s total deadline, which stays as the cap for trickling peers.

#include <stdbool.h>
#include <stdint.h>

#define NET_CONSOLE_HS_SILENT_MS 3000u

/**
 * @brief May a new connection take the handshake slot over?
 *
 * @param hsRunning  the slot is held
 * @param rxSeen     the holder has sent at least one byte (or already passed the receive phase)
 * @param nowMs      millis()
 * @param startMs    millis() when the holder took the slot
 */
static inline bool netConsoleSlotMayPreempt(bool hsRunning, bool rxSeen, uint32_t nowMs, uint32_t startMs)
{
    return hsRunning && !rxSeen && (uint32_t)(nowMs - startMs) >= NET_CONSOLE_HS_SILENT_MS;
}

/**
 * @brief Has the CURRENT slot holder sent a byte? The flag is a generation number, so a preempted task that
 * wakes late (EOF after shutdown) and writes its own, older generation cannot mark the new holder as sending.
 *
 * @param rxGen   generation last written by a task that saw a byte or left the receive phase
 * @param curGen  generation of the current slot holder
 */
static inline bool netConsoleSlotRxSeen(uint32_t rxGen, uint32_t curGen)
{
    return rxGen == curGen;
}
