// rm_runtime.h -- loop-task side of the authenticated remote management DM ("RM1").
// docs/concept-open-issues-20261004.md sections 6.2-6.5 (issue #1189).
//
// The protocol core (remote_cmd.h) only decides; this file drains the 2-slot queue that OnRxDone
// fills (rm_queue.h), runs the verified command through ONE literal console string from a fixed
// table (never the received text), persists the counter high-water mark, answers with a tagged
// DM and, for "reboot", restarts a few seconds after the reply was queued.
//
// Both entry points run in the loop task only (crypto, flash write and sendMessage() are not
// allowed in RX context).
#ifndef RM_RUNTIME_H
#define RM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

// Verdict counters since boot (printed by --info). One per RmVerdict class; ok counts commands that
// passed the check, whether or not their execution then succeeded. rej_disabled counts frames
// that were queued while RM was on and found RM off at drain time (toggle race only: with RM off
// the receive hook does not queue, the DM is ordinary text). rej_format counts rmParse failures;
// those do NOT advance the core's 3-in-90-s lockout (the core never sees an unparsable frame) --
// accepted: they cannot authenticate and change no state.
struct RmStats
{
    uint32_t ok;
    uint32_t cached;
    uint32_t sync;
    uint32_t rej_format;
    uint32_t rej_tag;
    uint32_t rej_replay;
    uint32_t rej_blocked;
    uint32_t rej_rate;
    uint32_t rej_lockout;
    uint32_t rej_disabled;
};

extern RmStats g_rmStats;

// ---- RM-09: status and sender side for the web "remote management" component -------------------
// ALL functions below run in the loop task only: the ESP32 web server (esp32loop() ->
// loopWebserver()) and the nRF52 one (nrf52loop() -> loopWebserver()) both execute their handlers
// there, as rmDrain() does. They are not thread safe against any other task.

// One EXECUTED command (RM_OK, also when its execution failed). No tag, no password ever.
struct RmLogEntry
{
    uint32_t ms;       // millis() at execution
    char src[10];      // requesting call
    uint32_t ctr;
    char cmd[40];      // "<cmd>[ <args>]"
    char result[64];   // "ok ..." / "err ..." (what the reply carried)
};

struct RmStatus
{
    bool on;               // node_rm == 1
    bool passwdSet;        // node_passwd not empty
    bool lockActive;       // 3 rejects in 90 s: RM1 locked
    uint32_t lockRemainS;  // seconds until the lockout ends (0 when not active)
    uint32_t hwm;          // persisted counter high-water mark
    RmStats stats;
    uint8_t nlog;          // used entries of log[]
    RmLogEntry log[5];     // newest first
};

void rmGetStatus(RmStatus &out);

// A command this node sent. verified = the reply's tag matched (HMAC under the target's key).
struct RmSent
{
    char dst[10];
    uint32_t ctr;
    char cmd[40];          // "<cmd>[ <args>]"
    uint32_t sentMs;       // millis() at send
    bool replied;          // some reply for (dst, ctr) arrived
    bool verified;         // ... and it is authentic
    char reply[72];        // verified: the result text ("ok ..."); unverified: the raw text, truncated
};

// Signs and sends "RM1 <ctr> <cmd>[ <args>] <tag>" as a DM to dst (call incl. SSID, upper case; lower
// case is folded up) via sendMessage(). passwd = the TARGET's password: used only to derive
// K = SHA-256(passwd) straight from the caller's buffer (no copy is made; the caller wipes its own
// buffer). K stays in RAM with the pending entry for RM_CACHE_MS (10 min) to verify the reply, is
// never persisted and never printed.
// ctr = max(persisted last-sent + 1, unix time when the clock is valid (bNTPDateTimeValid, RTC or GPS
// fix), known hwm of dst + 1 (from a verified sync/command reply)); "sync" uses ctr 0.
// cmd/args are checked with rmCommandAllowed() (the core's allowlist). A second send to the same dst
// inside RM_RATE_MS is refused ("busy": the target would rate-limit it anyway).
// false + short reason in err: "passwd", "cmd", "dst", "busy", "ctr" (counter exhausted),
// "store" (counter not persisted), "send" (sendMessage refused).
bool rmSendCommand(const char *dst, const char *passwd, const char *cmd, const char *args, char *err,
                   size_t errN, uint32_t *ctrOut);

// Last (up to 5) sent commands, newest first; returns the count copied.
uint8_t rmGetSent(RmSent *out, uint8_t max);

// Boot: loads the persisted high-water mark (counters_store.h) into the protocol state. Call once
// after the settings load. rmDrain() calls it lazily if the boot call was missed.
void rmInit(void);

// Loop task, once per pass: handles at most ONE queued RM1 frame and the deferred reboot.
void rmDrain(void);

#endif // RM_RUNTIME_H
