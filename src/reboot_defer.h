#pragma once

// FORMAL-02 9h (B9): a deferred reboot, serviced from the main loop. A request handler answers first and arms the
// reboot; the loop restarts the node once the delay has passed, so no handler blocks in delay() and the answer is out.
// The header is host-testable (pure state + decision); the glue is src/reboot_defer.cpp (ESP32 and nRF52).

#include <stdint.h>

// true when the deadline of an armed reboot has been reached; wrap-safe for a delay below 2^31 ms
inline bool rebootDeferDue(bool armed, uint32_t at_ms, uint32_t now_ms)
{
    return armed && (int32_t)(now_ms - at_ms) >= 0;
}

struct RebootDeferState
{
    bool armed;
    uint32_t at_ms;
};

// Arm: a second Arm keeps the EARLIER deadline (wrap-safe), never postpones a pending reboot.
inline void rebootDeferArmState(RebootDeferState &s, uint32_t now_ms, uint32_t delay_ms)
{
    const uint32_t at = now_ms + delay_ms;
    if (!s.armed || (int32_t)(at - s.at_ms) < 0)
        s.at_ms = at;
    s.armed = true;
}

void rebootDeferArm(uint32_t delay_ms, const char *why); // restart delay_ms from now; why goes to the serial line
void rebootDeferService(void);                            // main loop, once per pass, unconditionally
