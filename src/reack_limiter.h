// reack_limiter.h -- rate limit for the 0.2 duplicate-for-me re-ACK path.
//
// Stage 0.2 of docs/dm-transport-impl-plan-20260913.md: a duplicate DM
// addressed to this node is re-acked (see src/lora_functions.cpp, the
// setlogCountDedup() gate) so a lost :ackNNN can be repaired. Unbounded, a
// replayed frame could be amplified into a re-ACK storm -- this caps it to
// one ACK per (source call, NNN) per 30 s, counting the ORIGINAL ack: the
// receive path seeds the limiter when it acks a new DM, so the relayed copy
// of that DM (same msg_id, arrives seconds later as a duplicate) is not
// re-acked, while the sender's own retransmit (40 s cadence, see
// updateRetransmissionStatus()) is. The plan text says 60 s; that window
// would swallow the first retry, so it is 30 s here.
//
// Header-only and platform-neutral on purpose (stdint/string.h only, no
// Arduino) so it is callable from OnRxDone() (nRF52: LORA task) and
// includable directly by the native test (test/test_reack_limiter).
#pragma once

#include <stdint.h>
#include <string.h>

#define REACK_LIMITER_SIZE     8
#define REACK_LIMITER_WINDOW_MS 30000UL
#define REACK_CALL_MAX          10

struct ReackEntry
{
    char     call[REACK_CALL_MAX];
    uint16_t nnn;
    uint32_t last_ms;
    bool     used;
};

static ReackEntry reack_table[REACK_LIMITER_SIZE];
static uint8_t    reack_oldest = 0;   // next slot to overwrite, round-robin

/**
 * @brief Zero the limiter's state. Test-only entry point.
 */
static inline void reackLimiterReset(void)
{
    memset(reack_table, 0, sizeof(reack_table));
    reack_oldest = 0;
}

/**
 * @brief May (src_call, nnn) be re-acked now?
 *
 * Denies exactly when the same pair was allowed within the last 30000 ms
 * (rollover-safe: `(uint32_t)(now - last) < window`). Otherwise records the
 * pair -- overwriting the oldest of at most 8 tracked entries -- and allows
 * it.
 *
 * @param src_call NUL-terminated callsign, copied at most REACK_CALL_MAX-1
 *                 bytes (truncated, never overrun).
 * @param nnn      the {NNN transport sequence number.
 * @param now_ms   caller's millis(), passed in so this stays testable
 *                 without a clock.
 * @return true if this call may send a re-ACK now.
 */
static inline bool reackAllowed(const char *src_call, uint16_t nnn, uint32_t now_ms)
{
    if(src_call == NULL)
        return false;

    for(int i = 0; i < REACK_LIMITER_SIZE; i++)
    {
        if(!reack_table[i].used)
            continue;

        if(reack_table[i].nnn != nnn)
            continue;

        if(strncmp(reack_table[i].call, src_call, REACK_CALL_MAX - 1) != 0)
            continue;

        if((uint32_t)(now_ms - reack_table[i].last_ms) < REACK_LIMITER_WINDOW_MS)
            return false;

        // Same pair, window elapsed: refresh in place and allow.
        reack_table[i].last_ms = now_ms;
        return true;
    }

    // Not tracked yet: record in the oldest slot (round-robin -- the first
    // 8 calls fill slots 0..7 in order, matching "record and return true"
    // for a never-seen pair).
    uint8_t slot = reack_oldest;

    for(int i = 0; i < REACK_LIMITER_SIZE; i++)
    {
        if(!reack_table[i].used)
        {
            slot = (uint8_t)i;
            break;
        }
    }

    strncpy(reack_table[slot].call, src_call, REACK_CALL_MAX - 1);
    reack_table[slot].call[REACK_CALL_MAX - 1] = 0x00;
    reack_table[slot].nnn = nnn;
    reack_table[slot].last_ms = now_ms;
    reack_table[slot].used = true;

    reack_oldest = (uint8_t)((slot + 1) % REACK_LIMITER_SIZE);

    return true;
}
