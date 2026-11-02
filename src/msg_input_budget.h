// TD-25: byte budget of the on-device message input (T-Deck, T-Deck Pro).
// Mirrors the web GUI rule (src/web_functions/web_functions.cpp): 149 bytes for the
// text; with a DM callsign set, minus strlen(call) + 2 for the braces. Text stays
// UTF-8 (2 bytes per national character), so the cap counts bytes, never characters.
// Pure header, no Arduino/LVGL deps; needs charset_filter.cpp for the UTF-8 cut.
#ifndef MSG_INPUT_BUDGET_H
#define MSG_INPUT_BUDGET_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "charset_filter.h"

#define MSG_INPUT_TEXT_BYTES 149

static inline size_t msg_input_budget(const char *dm_call)
{
    size_t cost = 0;
    if (dm_call != NULL && dm_call[0] != '\0')
        cost = strlen(dm_call) + 2;
    return cost >= MSG_INPUT_TEXT_BYTES ? 0 : MSG_INPUT_TEXT_BYTES - cost;
}

// Safe length (<= budget) that never splits a UTF-8 sequence.
static inline size_t msg_input_clamp(const char *text, size_t len, size_t budget)
{
    return charset_utf8_safe_truncate(text, len, budget);
}

// TD-26: a DM destination is 1..9 chars of [A-Za-z0-9-] (callsign with SSID, e.g. DL4MFH-55).
// Anything else must refuse to send; it must never fall back to a broadcast.
#define MSG_INPUT_DEST_MAX 9

static inline bool msg_input_dest_valid(const char *dest)
{
    if (dest == NULL)
        return false;
    size_t n = strlen(dest);
    if (n < 1 || n > MSG_INPUT_DEST_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        char c = dest[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

#endif
