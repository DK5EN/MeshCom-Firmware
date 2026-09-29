// teleport_clock.h -- virtual millis() for "millis teleportation" host tests.
//
// A time-driven state machine on the node only ever sees a uint32_t millis()
// that wraps every 2^32 ms (49.7 days). This helper lets a test put that
// counter anywhere -- just before the wrap, across it, or a big jump forward
// -- and keep ticking the REAL code from there, without waiting.
//
//   TeleportClock c;              // starts at 0
//   c.teleport_to(0xFFFFFFFFu - 500);
//   c.tick(2000, 10, [&](uint32_t now) { loopSchedulerRun(now); });
//
// Header-only, no Arduino dependency: the caller's tick lambda mirrors the
// clock into whatever the code under test reads (mc_test_set_millis() in
// test/support/Arduino.h, or just the `now` argument it is handed).
//
// Two views of the same time:
//   now_ms    what the firmware sees: uint32_t, wraps.
//   total_ms  the true elapsed virtual time, 64 bit, never wraps. A teleport
//             adds the forward distance (uint32_t)(abs - now_ms), so a jump
//             counts as elapsed time (that is what a stalled loop looks like
//             from the outside).
#pragma once

#include <stdint.h>

#define UPTIME_MIN_CORE_ONLY
#include "../../src/uptime_min.h"   // uptimeMinStep(): the firmware's wrap-safe minutes

struct TeleportClock
{
    uint32_t now_ms = 0;
    uint64_t total_ms = 0;

    // Firmware-side constants worth naming in tests.
    static constexpr uint32_t kWrapMinus(uint32_t k) { return 0xFFFFFFFFu - k; }   // k ms before the last value
    static constexpr uint32_t kHour = 3600000u;

    // Move forward by ms; wraps exactly like millis() does.
    void advance(uint32_t ms)
    {
        now_ms += ms;
        total_ms += ms;
    }

    // Jump to an absolute millis() value. Counted as the forward distance,
    // so the jump to a smaller value is a wrap-sized hop (millis() never goes
    // backwards on the node; a "backwards" abs is a forward jump of
    // 2^32 - delta).
    void teleport_to(uint32_t abs_ms)
    {
        total_ms += (uint32_t)(abs_ms - now_ms);
        now_ms = abs_ms;
    }

    // Tick loop: advance by step_ms until total_ms has grown by span_ms,
    // calling fn(now_ms) AFTER each advance (the way a loop pass reads
    // millis() once and hands it on). Returns the number of ticks. The last
    // step is shortened so the span is met exactly.
    template <class Fn>
    uint32_t tick(uint64_t span_ms, uint32_t step_ms, Fn fn)
    {
        const uint64_t end = total_ms + span_ms;
        uint32_t n = 0;
        while (total_ms < end)
        {
            uint64_t left = end - total_ms;
            advance(left < step_ms ? (uint32_t)left : step_ms);
            fn(now_ms);
            n++;
        }
        return n;
    }

    // Tick until pred(now_ms) is true (checked after fn ran) or limit_ms of
    // virtual time has passed. Returns true when pred fired.
    template <class Fn, class Pred>
    bool tick_until(uint64_t limit_ms, uint32_t step_ms, Fn fn, Pred pred)
    {
        const uint64_t end = total_ms + limit_ms;
        while (total_ms < end)
        {
            advance(step_ms);
            fn(now_ms);
            if (pred(now_ms))
                return true;
        }
        return false;
    }

    // The minute counter the firmware hands to the NBR matrix and friends:
    // `(uint16_t)(millis() / 60000UL)` (loop_functions.cpp, lora_functions.cpp,
    // esp32_main.cpp nbrSweep call, ...). NOT continuous across the millis()
    // wrap: 2^32 ms is 71582.788 minutes, not a multiple of 65536.
    // The firmware's 16-bit uptime minutes (src/uptime_min.h, uptimeMin16()),
    // fed with this clock. Was (uint16_t)(now_ms / 60000) until 2026-09-29,
    // which jumped 6046 -> 0 at the wrap and wiped the NBR matrix.
    mutable uptime_min_state_t fw_min_state = {0, 0};
    uint16_t fw_minutes() const { return uptimeMinStep(&fw_min_state, now_ms); }

    // The true minute count (64 bit), for the expected value of a test.
    uint64_t true_minutes() const { return total_ms / 60000ULL; }
};
