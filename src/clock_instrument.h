// CLK-01 W0 (docs/campaign-clk-01-20261107.md) -- display-clock lag instrumentation.
// INSTRUMENT builds only (-D INSTRUMENT_ENABLED=1), RAM only. In a normal build this header
// reduces to the no-op CLK_SRC() macro and nothing else exists. Line formats: see the module
// docstring of tools/bench/clock_drift.py (host side) and the campaign doc.
#pragma once

#include "instrument.h"

#if INSTRUMENT_ENABLED
#include <stdint.h>
#include <time.h>

// Announce which source the NEXT setCurrentTime() / SetClock() call comes from
// ("NTP", "GPS", "RTC", "CET", "PHONE", "CMD", "SER"). Consumed by that call; a set without an
// announcement is logged as "OTHER" (boot restore, TZ re-anchor, topo restore, offset change).
void clockInstrSrc(const char *src);
#define CLK_SRC(s) clockInstrSrc(s)

// --timesrc gate, asked by Clock::setCurrentTime(): false = ignore this set.
bool clockInstrAccept(void);
// true in --timesrc cet: the {CET} frame handler must run regardless of RTC/GPS/NTP state.
bool clockInstrCetOnly(void);
// true while --timesrc is not "all": the free-running clock must run alone (no hardware RTC
// write-back, no GPS/RTC-driven bMyClock=false), see esp32_main.cpp / nrf52_main.cpp.
bool clockInstrGated(void);

// Clock::SetClock(time_t, bool) hook: old/new epoch (tsClock_m convention), ms already elapsed in
// the current clock second (millis() - u32Start_m) and millis().
void clockInstrOnSet(time_t old_epoch, time_t new_epoch, uint32_t into_second_ms, uint32_t now_ms);
// Clock::CheckEvent() hook, one call per handled tick; secs = seconds added to the clock.
void clockInstrOnTick(uint32_t now_ms, uint32_t next_ms, uint32_t secs);
// Clock::CheckEvent() hook: emits [CLOCK];stat once per 60 s.
void clockInstrStatPoll(time_t epoch, uint32_t now_ms);

// command_functions.cpp arms
void clockInstrCmdClockstat(void);                 // --clockstat
void clockInstrCmdTimesrc(const char *arg);        // --timesrc all|cet|none (arg: text after the command)
void clockInstrCmdOledshot(void);                  // --oledshot
void clockInstrCmdTdshot(void);                    // --tdshot (T-Deck / T-Deck Plus)

// sendDisplayTime() / WP_DISP hooks, at most one line per second and kind.
void clockInstrDispDraw(int h, int m, int s, int type);
void clockInstrDispSkip(const char *reason);
#else
#define CLK_SRC(s) ((void)0)
#endif
