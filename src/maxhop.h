/**
 * CS-01 / CS-02: the user-settable hop limit for text messages
 * (meshcom_settings.max_hop_text).
 *
 * Serial accepts 1..6, the web Config page offers a short drop-down (4, 3, 2)
 * and additionally shows the current value when the serial command set one
 * outside that list -- the wider serial range is deliberate (operator,
 * 2026-08-30). Both surfaces have to agree on what is valid and on what the
 * drop-down contains, so the rules live here once.
 *
 * Pure C++, no Arduino dependency -- unit-tested natively (test_maxhop).
 * MAXHOP_TEXT_FALLBACK mirrors MAX_HOP_TEXT_DEFAULT from configuration_global.h;
 * command_functions.cpp static_asserts that the two stay equal.
 */
#pragma once

#include <stdint.h>

#define MAXHOP_TEXT_MIN      1
#define MAXHOP_TEXT_MAX      6
#define MAXHOP_TEXT_FALLBACK 4      /* == MAX_HOP_TEXT_DEFAULT */

/* Number of values the web drop-down always offers (4, 3, 2), and the longest
 * list maxHopOptionList() can produce -- those three plus the current value
 * when it lies outside them. Size caller buffers with MAXHOP_OPTION_MAX. */
#define MAXHOP_WEB_OPTION_CNT 3
#define MAXHOP_OPTION_MAX     (MAXHOP_WEB_OPTION_CNT + 1)

/* A value the operator may set over serial. */
inline bool maxHopTextValid(int v)
{
    return v >= MAXHOP_TEXT_MIN && v <= MAXHOP_TEXT_MAX;
}

/* Plausibility at load: 0 ("nothing stored yet") or anything out of range
 * becomes the compile-time default, everything else is kept. */
inline int maxHopTextSanitize(int v)
{
    return maxHopTextValid(v) ? v : MAXHOP_TEXT_FALLBACK;
}

/* Fills out[] with the drop-down values in descending order: 4, 3, 2 plus the
 * current value when it is not one of them. `current` is sanitized first, so a
 * corrupt value never becomes an option. Writes at most `cap` entries and
 * returns how many were written (0 for out == NULL or cap <= 0). */
inline int maxHopOptionList(int current, int *out, int cap)
{
    if (out == nullptr || cap <= 0)
        return 0;

    const int fixed[MAXHOP_WEB_OPTION_CNT] = { 4, 3, 2 };
    const int cur = maxHopTextSanitize(current);

    bool placed = false;        // true once `cur` is in the output
    for (int i = 0; i < MAXHOP_WEB_OPTION_CNT; i++)
    {
        if (fixed[i] == cur)
            placed = true;      // already one of the offered values
    }

    int n = 0;
    for (int i = 0; i < MAXHOP_WEB_OPTION_CNT && n < cap; i++)
    {
        if (!placed && cur > fixed[i])
        {
            out[n++] = cur;
            placed = true;
            if (n >= cap)
                return n;
        }
        out[n++] = fixed[i];
    }

    if (!placed && n < cap)     // smaller than every offered value -> last entry
        out[n++] = cur;

    return n;
}

/* IDEE-04: relay hop step. max_hop_byte is the low hop nibble (bits 0-3) plus
 * optionally the high nibble. On air the high nibble of frame byte 5 carries
 * flags (0x80 server, 0x40 track, 0x20 app-offline, 0x10 mesh; see
 * aprs_functions.cpp); the receiver already splits them into bools, but the
 * helper keeps bits 4-7 untouched anyway. A hop nibble above MAXHOP_RELAY_LIMIT
 * (old firmware sends 8-15) is clamped to the limit before the decrement, so
 * such frames cannot circle longer than a regular one. A nibble of 0 is not
 * relayed: the caller's gate stays (nibble > 0); the helper returns the byte
 * unchanged for 0. */
#define MAXHOP_RELAY_LIMIT 7        /* == MAX_HOP_LIMIT, static_assert in lora_functions.cpp */

inline uint8_t relayHopNext(uint8_t max_hop_byte)
{
    uint8_t hop = max_hop_byte & 0x0F;
    const uint8_t flags = max_hop_byte & 0xF0;

    if (hop == 0)
        return max_hop_byte;                /* nothing to decrement, no borrow into the flags */
    if (hop > MAXHOP_RELAY_LIMIT)
        hop = MAXHOP_RELAY_LIMIT;
    return (uint8_t)(flags | (hop - 1));
}
