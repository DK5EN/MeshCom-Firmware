// TD-21: WiFi IP-liveness decision logic (pure, std only, host-testable).
// A station can stay associated (WL_CONNECTED) while the IP path is dead
// (heap starvation). The ESP32 side pings the gateway every 30 s; this state
// machine turns the per-round result into an escalation.
#pragma once

#include <stdint.h>

namespace wifi_liveness {

constexpr int FAIL_ROUNDS_RECONNECT = 3;   // ~90 s at 30 s cadence
constexpr int FAIL_ROUNDS_RESET     = 3;   // after RECONNECT
constexpr int FAIL_ROUNDS_RESTART   = 10;  // after RESET, ~5 min

enum Action { NONE = 0, RECONNECT, RESET, RESTART };

struct State {
    bool armed = false;     // a probe succeeded since link up
    int  stage = 0;         // 0 ok, 1 after RECONNECT, 2 after RESET
    int  fails = 0;         // consecutive failed rounds in this stage
};

// probe_result: -1 no result yet / no probe, 0 round failed (0 replies), 1 ok
inline Action step(State &s, bool link_connected, int probe_result)
{
    if (!link_connected) { s = State(); return NONE; }
    if (probe_result < 0) return NONE;
    if (probe_result > 0) { s.armed = true; s.stage = 0; s.fails = 0; return NONE; }
    if (!s.armed) return NONE;
    s.fails++;
    if (s.stage == 0 && s.fails >= FAIL_ROUNDS_RECONNECT) { s.stage = 1; s.fails = 0; return RECONNECT; }
    if (s.stage == 1 && s.fails >= FAIL_ROUNDS_RESET)     { s.stage = 2; s.fails = 0; return RESET; }
    if (s.stage == 2 && s.fails >= FAIL_ROUNDS_RESTART)   { s.fails = 0; return RESTART; }
    return NONE;
}

// Round bookkeeping (pure). A ping round normally ends within ~3-4 s, i.e.
// before the next 30 s check. If the tcpip thread is wedged, on_ping_end never
// fires and the round stays pending forever; that must count as a failure,
// not as "no result" (else the watchdog never escalates).
constexpr int PENDING_CHECKS_DEAD = 2;     // still pending at the 2nd check after start (~60 s)

struct Probe {
    int  result = -1;       // -1 round still running / none, 0 failed, 1 ok
    bool done   = false;    // round is over (ended or declared dead)
    bool stuck  = false;    // done because it exceeded PENDING_CHECKS_DEAD
};

// Call once per check, before a new round is started.
// pending: a round was started and not yet collected; replies: -1 while
// on_ping_end has not fired, else the reply count of the ended round;
// pending_checks: checks this round was seen pending (caller keeps it).
inline Probe probe_eval(bool pending, int replies, int &pending_checks)
{
    Probe p;
    if (!pending) { pending_checks = 0; return p; }
    if (replies >= 0) {
        pending_checks = 0;
        p.done = true;
        p.result = (replies > 0) ? 1 : 0;
        return p;
    }
    if (++pending_checks >= PENDING_CHECKS_DEAD) {
        pending_checks = 0;
        p.done = true; p.stuck = true; p.result = 0;
    }
    return p;
}

} // namespace wifi_liveness
