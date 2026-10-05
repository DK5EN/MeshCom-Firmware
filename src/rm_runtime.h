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

#include "rm_sender_policy.h"

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
    // filled by rmGetSent() from rm_sender_policy.h (contract C2): the entry state at that moment
    uint8_t state;         // RmEntryState: queued, waiting, noanswer, ok, err, unverified
    const char *stateName; // "queued" ... "unverified" (static string)
    const char *msg;       // one plain sentence for the operator (static string, never null)
};

// Signs and sends "RM1 <ctr> <cmd>[ <args>] <tag>" as a DM to dst (call incl. SSID, upper case; lower
// case is folded up) via sendMessage(). passwd = the TARGET's password: used only to derive
// K = SHA-256(passwd) straight from the caller's buffer (no copy is made; the caller wipes its own
// buffer). It is a thin wrapper: validate, derive, rmSendCommandKey(), wipe the derived key.
// The password must pass rmValidatePasswordN() after trailing spaces are stripped (as every key
// derivation does); a longer raw string than 14 bytes is refused, never truncated.
// viaSync (optional): set true when the command was NOT sent yet but queued behind an automatic sync
// (see rmSendCommandKey); *ctrOut is then 0.
bool rmSendCommand(const char *dst, const char *passwd, const char *cmd, const char *args, char *err,
                   size_t errN, uint32_t *ctrOut, bool *viaSync = nullptr);

// Same, for a caller that already holds the 32-byte key K = SHA-256(password of the TARGET) (the
// known-node slots). The key is copied into the pending-reply entry exactly as the password variant
// does (it verifies the reply, lives RM_CACHE_MS, is wiped on every exit path); the CALLER's buffer
// is the caller's to wipe.
// Order of checks, none of which consumes a counter: dst, cmd, sender policy (rm_sender_policy.h:
// 10 s spacing, at most 2 unanswered sends to one target inside 90 s), then counter + send.
// ctr = max(persisted last-sent + 1, unix time when the clock is valid (bNTPDateTimeValid, RTC or GPS
// fix), known hwm of dst + 1 (from a verified sync/command reply)); "sync" uses ctr 0.
// Automatic sync: a sender WITHOUT a trusted clock and WITHOUT a learnt mark for dst first sends ONE
// sync and keeps the command (one slot, key included) until that sync is answered and verified and
// 10 s have passed; then it goes out by itself from rmDrain(). The call then returns true with
// *viaSync = true and *ctrOut = 0. A sync that stays unanswered drops the queued command.
// false + short reason in err: "passwd", "cmd", "dst", "busy" (10 s spacing, or a command is already
// queued behind a sync for dst), "limit" (2 unanswered sends inside 90 s: a third could lock the target),
// "ctr" (counter exhausted), "store" (counter not persisted), "send" (sendMessage refused).
// A refusal consumes no counter, so a double click never burns one.
bool rmSendCommandKey(const char *dst, const uint8_t key[32], const char *cmd, const char *args, char *err,
                      size_t errN, uint32_t *ctrOut, bool *viaSync = nullptr);

// Last (up to 5) sent commands, newest first; returns the count copied.
uint8_t rmGetSent(RmSent *out, uint8_t max);

// Per managed node seen in the sent book (newest first), for the JSON writer.
struct RmTarget
{
    char dst[10];
    bool locked;           // 2 unanswered sends inside 90 s: sends are refused ("limit")
    uint32_t retryS;       // seconds until a send is accepted again (spacing included), 0 = now
    bool pending;          // a command waits behind an automatic sync for this node
    const char *chainErr;  // token (static) why the last chained command was NOT sent ("nosync", "lost",
                           // "limit", "busy", "send", ...), nullptr = none; cleared by the next accepted send
                           // to this node
    const char *chainMsg;  // plain sentence for chainErr (static), nullptr when chainErr is
};
uint8_t rmGetTargets(RmTarget *out, uint8_t max);

// Policy probe for one call (upper case): true = a send would pass the sender policy now; retryS (optional)
// gets the wait in seconds otherwise (0 when allowed).
bool rmTargetMaySend(const char *dst, uint32_t *retryS);

// Boot: loads the persisted high-water mark (counters_store.h) into the protocol state. Call once
// after the settings load. rmDrain() calls it lazily if the boot call was missed.
void rmInit(void);

// Loop task, once per pass: handles at most ONE queued RM1 frame and the deferred reboot.
void rmDrain(void);

#endif // RM_RUNTIME_H
