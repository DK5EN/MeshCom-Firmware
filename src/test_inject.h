#pragma once

// (C) 2026 MeshCom contributors
//
// Test hook: inject a text message into the display pipeline exactly as if it
// had just been received over LoRa and decoded by decodeAPRS(), so UI tests
// (T-Deck, T-Deck Plus, e-paper, ...) don't need a second radio to exercise
// the "message arrived" path.
//
// Compiled in by default. Build with -D MC_INJECT_HOOKS=0 to strip it out of
// production firmware images entirely.
#ifndef MC_INJECT_HOOKS
#define MC_INJECT_HOOKS 1
#endif

#include <Arduino.h>

#if MC_INJECT_HOOKS

// Injects a text message as if it had just arrived over LoRa from src_call.
//
//   dst       Group number as a string, or a callsign for a DM to this node.
//             Use group "TEST" for bench/scenario traffic: checkRegexCall()
//             accepts it (and "TESTER") as a destination, and the central
//             server filters that group, so it never reaches the map or
//             dashboard. Group "9999" is a real, server-visible group -- do
//             not use it for test traffic (see docs/automation-runner-runbook.md §2.6).
//   text      Message payload (plain text). Must be non-empty and not exceed
//             the injector's payload cap (see test_inject.cpp).
//   src_call  Sender callsign to report as the source. NULL or "" defaults to
//             "DK5EN-93".
//   rssi/snr  Reported signal quality, as if measured on the (nonexistent)
//             reception.
//
// Always prints exactly one line to Serial:
//   [INJECT];ok;id;<msg_id hex>;dst;<dst>;src;<src_call>;len;<text len>
// on success, or
//   [INJECT];err;<reason>
// on failure (empty text, text too long, invalid dst, or the single-slot
// deferred-display queue already occupied).
//
// Returns true on success, false on failure.
bool inject_text_message(const char *dst, const char *text, const char *src_call, int16_t rssi, int8_t snr);

// Queue a position beacon (APRS '!' payload, decimal degrees, negative = S/W)
// as if received via LoRa: feeds sendDisplayPosition() -> OLED position page
// on every non-T-Deck display board. Prints [INJECTPOS];ok / ;err.
bool inject_position(const char *call, double lat, double lon, int16_t rssi, int8_t snr);

#else

// MC_INJECT_HOOKS=0: compiled out. Callers may call this unconditionally --
// it is a no-op that reports failure, so no #if is needed at call sites.
inline bool inject_text_message(const char *, const char *, const char *, int16_t, int8_t)
{
    return false;
}
inline bool inject_position(const char *, double, double, int16_t, int8_t)
{
    return false;
}


#endif // MC_INJECT_HOOKS
