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

// ---------------------------------------------------------------------------
// TD-21b: Session-Buchfuehrung (pure). udp_functions.cpp fuehrt nur die
// Seiteneffekte aus (esp_ping_*), die dieser Layer vorgibt.
// Bridge zum Ping-Task: on_ping_end schreibt busy (->false), stuck (->false) und
// replies (nur wenn pending); alles andere gehoert dem Loop-Task. Die ESP-Seite
// haelt dafuer volatile-Statics, laedt sie vor und schreibt sie nach jedem Aufruf
// zurueck (nur geaenderte Felder).
struct Session {
    bool have = false;        // Session (Handle) existiert
    bool pending = false;     // Runde gestartet, noch nicht ausgewertet
    bool busy = false;        // Ping-Task in einer Runde        [on_ping_end loescht]
    bool stuck = false;       // Runde tot erklaert, Task haengt [on_ping_end loescht]
    uint32_t gw = 0;          // Gateway der Session
    int  pending_checks = 0;  // Checks, in denen die Runde pending war
    int  replies = -1;        // -1 bis on_ping_end feuert       [on_ping_end setzt]
    int  idle_busy_checks = 0;// Sicherheitsventil: busy ohne pending
};

struct Plan {
    int  result  = -1;     // -1 kein Ergebnis, 0 Runde fehlgeschlagen, 1 ok
    bool stop    = false;  // esp_ping_stop(handle)
    bool del     = false;  // esp_ping_delete_session(handle), danach Handle weg
    bool abandon = false;  // Handle weg ohne delete (haengender Task)
    bool create  = false;  // neue Session fuer gw anlegen (session_created() bei Erfolg)
    bool disarm  = false;  // Gateway-Wechsel: State zuruecksetzen
};

// Spiegel von wifiPingEnd (Referenz fuer Tests; der Callback arbeitet auf den volatiles).
inline void session_on_end(Session &s, int replies)
{
    s.busy = false;
    s.stuck = false;
    if (!s.pending) return;      // verspaetetes Ende einer abgebrochenen Runde
    s.replies = replies;
}

// Session wegen Gateway-Wechsel loswerden. true = Session ist weg.
// busy && !stuck: nur stoppen (kein Delete, use-after-free im Ping-Task).
inline bool session_drop(Session &s, Plan &pl)
{
    if (s.have) {
        if (s.busy) {
            if (!s.stuck) { pl.stop = true; return false; }
            pl.abandon = true;
        } else {
            pl.stop = true; pl.del = true;
        }
    }
    s.have = false; s.busy = false; s.stuck = false; s.gw = 0;
    s.pending = false; s.pending_checks = 0; s.replies = -1;
    return true;
}

// Einmal pro Check vor step(): Ergebnis der letzten Runde + Session-Verwaltung.
inline Plan session_collect(Session &s, uint32_t gw)
{
    Plan pl;
    if (gw == 0) return pl;

    // Pending-Runde immer mit derselben Stuck-Regel auswerten, auch bei
    // Gateway-Wechsel (sonst bleibt eine haengende alte Runde ewig "busy").
    if (s.pending && s.have) {
        Probe pr = probe_eval(true, s.replies, s.pending_checks);
        if (!pr.done) {
            if (gw == s.gw) return pl;         // Runde laeuft noch: weder ok noch Fehler
        } else {
            if (pr.stuck) { s.stuck = s.busy; pl.stop = true; }
            pl.result = pr.result;
            s.pending = false;
        }
    } else if (s.stuck && s.busy && s.have && gw == s.gw) {
        pl.result = 0;
    }
    if (!s.pending && s.busy && !s.stuck) {
        if (++s.idle_busy_checks >= 2) { s.idle_busy_checks = 0; s.busy = false; }
    } else {
        s.idle_busy_checks = 0;
    }

    if (s.have && gw != s.gw) {
        pl.disarm = true;
        pl.result = -1;
        if (!session_drop(s, pl)) return pl;
    }
    if (!s.have) pl.create = true;
    return pl;
}

inline void session_created(Session &s, uint32_t gw)
{
    s.have = true; s.gw = gw; s.pending = false; s.pending_checks = 0; s.replies = -1;
}

// true = Runde starten (esp_ping_start); bei Fehlschlag session_start_failed().
inline bool session_start_round(Session &s)
{
    if (!s.have || s.pending || s.busy) return false;
    s.replies = -1; s.pending_checks = 0;
    s.pending = true; s.busy = true; s.stuck = false;
    return true;
}
inline void session_start_failed(Session &s) { s.pending = false; s.busy = false; }

// Laufende Runde abbrechen; true = esp_ping_stop(handle).
inline bool session_abort_round(Session &s)
{
    bool stop = s.pending && s.have;
    s.pending = false; s.pending_checks = 0; s.replies = -1;
    return stop;
}

} // namespace wifi_liveness
