// CLK-01 (docs/campaign-clk-01-20261107.md) -- tick arithmetic of
// Clock::CheckEvent(), carved out as pure functions for host tests.
// Behaviour is identical to the former inline expressions.
#pragma once
#include <stdint.h>

// Tick is due when now has passed next (wrap-safe signed compare).
static inline bool clockTickDue(uint32_t now_ms, uint32_t next_ms)
{
    return (int32_t)(now_ms - next_ms) > 0;
}

// Whole seconds added to the clock for one tick: the SCHEDULED interval,
// not the real elapsed time (F3: loop latency per tick is lost).
static inline uint32_t clockTickSeconds(uint32_t start_ms, uint32_t next_ms)
{
    return (next_ms - start_ms) / 1000;
}

// Next deadline after a tick that was handled at now_ms.
static inline uint32_t clockTickRearm(uint32_t now_ms)
{
    return now_ms + 1000;
}

// CLK-01 W0 (INSTRUMENT [CLOCK];stat): how late a tick fired against its deadline, in ms.
// Only meaningful when clockTickDue() was true.
static inline uint32_t clockTickLateMs(uint32_t now_ms, uint32_t next_ms)
{
    return now_ms - next_ms;
}

// CLK-01 W0: ms lost since the last clock set = real elapsed ms since that set minus the seconds
// counted since then (x 1000). Evaluated when a tick was handled; 0 for a loss-free clock.
static inline int32_t clockLostMs(uint32_t now_ms, uint32_t set_ms, uint32_t counted_s)
{
    return (int32_t)(now_ms - set_ms) - (int32_t)(counted_s * 1000u);
}

// CLK-01 W0: clamp a 64-bit millisecond delta to the int32 range so it prints with %ld on the
// 32-bit targets (ESP32, nRF52; no %lld in newlib-nano). The first set after boot is ~1e11 ms.
static inline int32_t clockDeltaClampMs(int64_t delta_ms)
{
    if (delta_ms > INT32_MAX) return INT32_MAX;
    if (delta_ms < INT32_MIN) return INT32_MIN;
    return (int32_t)delta_ms;
}
