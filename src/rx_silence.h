#pragma once
// RX-silence tracker (LRF W1c, D5). Pure C++, no Arduino/platform dependencies, host-testable,
// shared by nRF52 (W1c) and ESP32 (W2b).
//
// Fed with RX events (OnRxDone, OnRxError, header detect, ACK frame) and "now" (ms, wrap-safe uint32).
// - Silence starts when no RX event was seen for RX_SILENCE_MS (default 60 s, override with -DRX_SILENCE_MS=..).
//   Its duration is measured from the LAST RX event (so it includes the 60 s threshold).
// - tick() returns true when a snapshot is due: at 60 s, then every further RX_SILENCE_MS while silent.
//   If tick() runs late it emits one snapshot and skips the missed ones (no burst).
// - The silence ends with the first RX event after it. End reason: the radio action noted most recently
//   (noteAction: CAD, TX, WATCHDOG) no more than RX_SILENCE_END_WINDOW_MS (default 3000 ms) before that
//   first RX event; no action inside the window -> RX (the silence ended by itself).
// - Counters: silences (started), longest and total seconds (ended silences; *Now() adds a running one).
// Thread model: all calls from ONE task (the loop). Producers in other tasks only publish an event
// timestamp, which the loop forwards.

#include <stdint.h>

#ifndef RX_SILENCE_MS
#define RX_SILENCE_MS 60000UL
#endif
#ifndef RX_SILENCE_END_WINDOW_MS
#define RX_SILENCE_END_WINDOW_MS 3000UL
#endif

enum RxSilenceReason : uint8_t
{
    RXS_BY_RX = 0,
    RXS_BY_CAD = 1,
    RXS_BY_TX = 2,
    RXS_BY_WATCHDOG = 3
};

inline const char *rxSilenceReasonName(uint8_t r)
{
    switch (r)
    {
    case RXS_BY_CAD:      return "cad";
    case RXS_BY_TX:       return "tx";
    case RXS_BY_WATCHDOG: return "watchdog";
    default:              return "rx";
    }
}

struct RxSilenceTracker
{
    uint32_t last_event_ms = 0;
    bool     silent = false;
    uint32_t snaps = 0;           // snapshots emitted in the current silence
    uint8_t  action = RXS_BY_RX;  // last noted radio action (RXS_BY_RX = none)
    uint32_t action_ms = 0;
    bool     has_action = false;

    // counters
    uint32_t silences = 0;
    uint32_t longest_s = 0;
    uint32_t total_s = 0;

    // result of the last end (valid after noteRx() returned true)
    uint32_t end_dur_s = 0;
    uint8_t  end_reason = RXS_BY_RX;

    void init(uint32_t now)
    {
        *this = RxSilenceTracker();
        last_event_ms = now;
    }

    void noteAction(uint8_t kind, uint32_t now)
    {
        action = kind;
        action_ms = now;
        has_action = true;
    }

    // Seconds since the last RX event.
    uint32_t sinceEventS(uint32_t now) const { return (uint32_t)(now - last_event_ms) / 1000UL; }

    // RX event at 'now'. Returns true if this ended a silence (end_dur_s / end_reason set).
    bool noteRx(uint32_t now)
    {
        bool ended = false;
        if (silent)
        {
            end_dur_s = (uint32_t)(now - last_event_ms) / 1000UL;
            end_reason = (has_action && (uint32_t)(now - action_ms) <= RX_SILENCE_END_WINDOW_MS)
                             ? action : (uint8_t)RXS_BY_RX;
            total_s += end_dur_s;
            if (end_dur_s > longest_s) longest_s = end_dur_s;
            silent = false;
            snaps = 0;
            ended = true;
        }
        last_event_ms = now;
        has_action = false;  // an action before this event must not explain a later silence
        return ended;
    }

    // Periodic check. Returns true when a snapshot line is due; t_s = silence age in seconds.
    bool tick(uint32_t now, uint32_t *t_s = 0)
    {
        uint32_t el = (uint32_t)(now - last_event_ms);
        if (!silent)
        {
            if (el < (uint32_t)RX_SILENCE_MS) return false;
            silent = true;
            silences++;
            snaps = 0;
        }
        uint32_t due = el / (uint32_t)RX_SILENCE_MS;  // >= 1 while silent
        if (due <= snaps) return false;
        snaps = due;
        if (t_s) *t_s = el / 1000UL;
        return true;
    }

    uint32_t longestNow(uint32_t now) const
    {
        uint32_t c = silent ? sinceEventS(now) : 0;
        return c > longest_s ? c : longest_s;
    }
    uint32_t totalNow(uint32_t now) const { return total_s + (silent ? sinceEventS(now) : 0); }
};
