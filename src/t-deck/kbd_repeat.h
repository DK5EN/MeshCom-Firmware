/**
 * @file        kbd_repeat.h
 * @brief       Pure state machine for T-Deck keyboard auto-repeat (raw I2C mode)
 * @license     MIT
 * @copyright   Copyright (c) 2025 ICSSW.org
 *
 * Header-only, no Arduino dependency: takes `now` (millis()) from the caller
 * and never touches I2C itself, so it builds and runs on the native test
 * target.
 */
#ifndef KBD_REPEAT_H
#define KBD_REPEAT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define KBD_RAW_FRAME_LEN     5
#define KBD_RAW_TIMEOUT_MS    5000
#define KBD_RAW_PROBE_MAX     3
#define KBD_RAW_MAX_BITS      3      /* key + up to two modifiers */

enum kbd_raw_support { KBD_RAW_UNKNOWN = 0, KBD_RAW_YES, KBD_RAW_NO };

struct KbdRepeat {
    uint8_t  mask[KBD_RAW_FRAME_LEN];
    uint32_t since_ms;
    uint32_t key;          /* the remapped act_key being repeated */
    uint8_t  support;      /* enum kbd_raw_support */
    uint8_t  probes;
    bool     active;
};

/* Frame plausibility: 1..KBD_RAW_MAX_BITS bits set across all five bytes,
 * and no byte equal to 0xFF (0xFF marks an old keyboard's idle-high SDA
 * response to a request it does not understand). */
static inline bool kbdRawFrameValid(const uint8_t *f)
{
    int bits = 0;
    for (int i = 0; i < KBD_RAW_FRAME_LEN; i++)
    {
        if (f[i] == 0xFF)
            return false;
        for (uint8_t b = f[i]; b != 0; b >>= 1)
            bits += (b & 1);
    }
    return bits >= 1 && bits <= KBD_RAW_MAX_BITS;
}

/* First frame after a key: arm the window on a plausible frame; otherwise
 * count the probe and, after KBD_RAW_PROBE_MAX misses, mark this boot's
 * keyboard as unsupported. Returns false when the frame disqualifies it.
 *
 * An all-zero frame does NOT count as a probe failure: the char can arrive
 * up to one LVGL poll plus one C3 matrix scan (roughly 65 ms) after the
 * physical press, so a quick tap is often already released by the time the
 * first raw frame is read. That is indistinguishable from "no key down"
 * and must not be mistaken for "old firmware" -- only a frame that fails
 * kbdRawFrameValid() for another reason (a 0xFF byte, or more than
 * KBD_RAW_MAX_BITS bits) is a real probe failure. */
static inline bool kbdRepeatArm(struct KbdRepeat *s, const uint8_t *f, uint32_t key, uint32_t now)
{
    if (kbdRawFrameValid(f))
    {
        memcpy(s->mask, f, KBD_RAW_FRAME_LEN);
        s->since_ms = now;
        s->key      = key;
        s->active   = true;
        s->support  = KBD_RAW_YES;
        return true;
    }

    bool all_zero = true;
    for (int i = 0; i < KBD_RAW_FRAME_LEN; i++)
    {
        if (f[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
        return false;               /* released before the read -- not a probe */

    s->probes++;
    if (s->probes >= KBD_RAW_PROBE_MAX)
        s->support = KBD_RAW_NO;
    return false;
}

/* Subsequent frames: true while every bit armed in `mask` is still set in
 * `f` (extra bits, e.g. a modifier added mid-hold, are accepted) and the
 * window has not timed out. `now - since_ms` in uint32_t arithmetic is the
 * standard millis()-rollover-safe comparison. */
static inline bool kbdRepeatHold(struct KbdRepeat *s, const uint8_t *f, uint32_t now)
{
    if (!s->active)
        return false;
    if ((uint32_t)(now - s->since_ms) >= KBD_RAW_TIMEOUT_MS)
        return false;
    for (int i = 0; i < KBD_RAW_FRAME_LEN; i++)
    {
        if ((f[i] & s->mask[i]) != s->mask[i])
            return false;
    }
    return true;
}

/* Ends the hold. `support`/`probes` are a per-boot verdict on the keyboard
 * firmware, not per-hold state -- left untouched here. */
static inline void kbdRepeatClear(struct KbdRepeat *s)
{
    s->active = false;
    memset(s->mask, 0, KBD_RAW_FRAME_LEN);
    s->key = 0;
}

#endif /* KBD_REPEAT_H */
