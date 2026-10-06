// RM-02: protocol core of the authenticated remote management DM ("RM1").
// docs/concept-open-issues-20261004.md section 6 (issue #1189).
//
// Arduino-free, no printf, no malloc: parse, canonical string, key, tag
// verification, allowlist, counter / replay window, rate limit and lockout,
// reply build. Nothing here executes a command or touches flash; the caller
// (RM-03, loop task only) runs the command after RM_OK and persists the
// returned high-water mark.
//
// Wire format:  RM1 <ctr> <cmd>[ <args>] <tag>
//   tag = first 8 bytes (16 hex chars) of HMAC-SHA256(K, canonical)
//   K   = SHA-256(node_passwd with trailing spaces stripped)
//   canonical = "RM1|" dst "|" src "|" ctr "|" cmd [" " args]
// Reply:        RM1 <ctr> ok <status> <rtag>  /  RM1 <ctr> err <reason> <rtag>
//   rtag over "RM1R|" dst "|" src "|" ctr "|" result

#ifndef REMOTE_CMD_H
#define REMOTE_CMD_H

#include <stddef.h>
#include <stdint.h>

enum RmVerdict : uint8_t
{
    RM_OK = 0,        // execute, then rmAccept() and reply
    RM_CACHED,        // same ctr + valid tag within RM_CACHE_MS: re-send reply, do not execute
    RM_SYNC,          // valid "sync": reply "ok ctr=<hwm> v=<version>", no rmAccept()
    RM_REJ_FORMAT,    // silent
    RM_REJ_TAG,       // silent
    RM_REJ_REPLAY,    // silent
    RM_REJ_BLOCKED,   // not on the allowlist, bad args or forbidden characters; silent
    RM_REJ_RATE,      // silent
    RM_REJ_LOCKOUT,   // silent
    RM_REJ_DISABLED   // empty password; silent
};

// "ok","cached","sync","format","tag","replay","blocked","rate","lockout","disabled"
const char *rmVerdictName(RmVerdict v);

#define RM_RATE_MS 10000u        // minimum spacing of accepted commands
#define RM_SYNC_RATE_MS 60000u   // minimum spacing of accepted syncs (own limiter, independent of RM_RATE_MS)
#define RM_CACHE_MS 600000u      // lost-reply recovery window (10 min)
#define RM_REJ_WINDOW_MS 90000u  // 3 rejects inside this window ...
#define RM_REJ_LIMIT 3
#define RM_LOCKOUT_MS 300000u    // ... lock RM1 for 5 min
#define RM_MAX_RESULT 108         // longest result text a reply can carry; "RM1 <10 digits> <108> <16 hex>" = 140 chars
#define RM_MAX_ARGS 39            // longest args text of a command (RmCmd::args holds this + NUL)
#define RM_LEGACY_RESULT_MAX 63   // replies of the first 13 commands stay within this (older operators reject longer)
#define RM_CAP_LEVEL 2            // capability level advertised in the sync reply "rm=<n>"

struct RmCmd
{
    uint32_t ctr;
    char cmd[16];
    char args[RM_MAX_ARGS + 1];
    char tag[17]; // 16 lower-case hex + NUL
};

// Syntax only: "RM1 " prefix, single spaces, no leading/trailing space,
// decimal ctr 1..4294967295 (no leading zeros; 0 only with cmd "sync"),
// lower-case cmd/args, 16 lower-case hex tag. Allowlist is rmCheck()'s job.
bool rmParse(const char *text, RmCmd &out);
// True for a REPLY text "RM1 <ctr> ok ..." / "RM1 <ctr> err ..." (never a command: no allowlisted
// command is named ok/err). The receive hook shows replies to the operator instead of queueing them.
bool rmIsReply(const char *text);

// K = SHA-256(passwd with trailing spaces stripped); all-zero key for empty.
void rmDeriveKey(const char *passwd, uint8_t key[32]);

// Canonical string into out (NUL-terminated); returns its length, 0 if it
// does not fit or an argument is null.
size_t rmCanonical(const RmCmd &c, const char *dst, const char *src, char *out, size_t n);

struct RmState
{
    uint32_t hwm;           // persisted high-water mark
    uint32_t lastCtr;       // last accepted command
    char lastReply[RM_MAX_RESULT + 1]; // RESULT text of the last accepted command ("ok rebooting");
                            // the caller re-sends rmReply(cmd, lastReply, ...) on RM_CACHED
    uint32_t lastAcceptMs;  // acceptance time of lastCtr (cache window)
    uint8_t rejCount;       // rejects inside the current window
    uint32_t rejWindowMs;   // start of the reject window
    uint32_t lockUntilMs;
    bool lockActive;
    char lastTag[17];       // tag of the last accepted command
    bool haveLast;
    // appended by RM-02 (rmStateInit() sets them, callers never touch them):
    uint32_t lastRateMs;    // time of the last accepted command (rate limit; sync does not touch it)
    bool haveRate;
    // appended by W0a: sync has its own limiter so a replayed sync can neither starve commands nor
    // be answered more than once per RM_SYNC_RATE_MS:
    uint32_t lastSyncMs;    // time of the last accepted sync
    bool haveSync;
};

void rmStateInit(RmState &s, uint32_t hwm);

// Clears the lockout (lockActive) and the reject counter, nothing else: hwm, lastCtr, the reply cache,
// the rate limiter and the sync limiter stay. The operator standing at the node (own password change,
// console or web) may always unlock it; it is never reachable from the air.
void rmReceiverUnlock(RmState &s);

// Full check incl. allowlist, counter, rate limit, lockout. Does NOT execute.
// maxTxPower bounds "txpower <n>" (0 <= n <= maxTxPower). Every reject counts
// towards the lockout, except RM_REJ_DISABLED, RM_REJ_LOCKOUT, RM_REJ_RATE and
// RM_REJ_REPLAY (each of the last two requires a VALID tag: the lockout throttles key
// guessing, a valid tag is not a guess, and a replayed or overtaken frame reveals and
// executes nothing; counting it let a jittery legitimate path lock the node). A strike
// exactly RM_REJ_WINDOW_MS after the window start opens a new window. The lockout is reachable without the
// key (3 junk DMs per 5 min keep RM unavailable): accepted by design, RM fails
// closed, never open (ADR). RM_SYNC
// has its own limiter (RM_SYNC_RATE_MS, a second authenticated sync inside it is RM_REJ_RATE, silent):
// it neither stamps nor obeys the command limiter, and it never moves hwm.
RmVerdict rmCheck(RmState &s, const RmCmd &c, const char *dst, const char *src, const char *passwd,
                  int maxTxPower, uint32_t nowMs);

// After the caller executed an RM_OK command: records hwm, result, time.
// CONTRACT: call it for EVERY RM_OK, also when execution failed (pass the
// "err <reason>" result) -- otherwise the same ctr stays executable and the
// rate limiter is not stamped.
// Returns the new hwm to persist. result is truncated to RM_MAX_RESULT.
uint32_t rmAccept(RmState &s, const RmCmd &c, const char *result, uint32_t nowMs);

// RM-09 (sender side, web "remote management"): the allowlist of rmCheck() as a predicate on plain
// strings (cmd and args as they appear on the wire, lower case; "" for no args). Includes the
// forbidden-character rule; txpower is bounded by maxTxPower.
bool rmCommandAllowed(const char *cmd, const char *args, int maxTxPower);

// RM-09: builds "RM1 <ctr> <cmd>[ <args>] <tag>" (what rmParse() accepts) with the 16-hex tag of
// HMAC-SHA256(key, canonical). dst = managed node, src = this node. key = rmDeriveKey() of the
// TARGET's password. Pure builder: the allowlist is rmCommandAllowed()'s job. Returns the length,
// 0 if it does not fit, an argument is null/oversized or cmd is empty.
size_t rmBuildCommand(const char *dst, const char *src, uint32_t ctr, const char *cmd, const char *args,
                      const uint8_t key[32], char *out, size_t n);

// RM-09: checks a REPLY text "RM1 <ctr> <result> <rtag>" against dst (managed node), src (this node),
// the expected ctr and the key; the tag covers "RM1R|dst|src|ctr|result". On success copies the result
// ("ok ..." / "err ...") to result and returns true; false on any mismatch (result untouched then),
// also when result does not fit n.
bool rmVerifyReply(const char *text, const char *dst, const char *src, uint32_t ctr, const uint8_t key[32],
                   char *result, size_t n);

// "RM1 <ctr> <result> <rtag>" into out (NUL-terminated); returns the length,
// 0 if it does not fit, result is longer than RM_MAX_RESULT or the password is empty.
size_t rmReply(const RmCmd &c, const char *result, const char *dst, const char *src, const char *passwd,
               char *out, size_t n);

// Pure result sanitiser: allowed bytes are space, A-Z a-z 0-9 and "- . / = + _ @ ? ( ) , * #";
// every other byte becomes '?'. In place, returns the length.
size_t rmSanitizeResult(char *result);
// Capability level of a sync result ("ok ctr=<n> v=<ver> rm=<k>"): k, or 0 when the token is absent/malformed.
int rmCapLevel(const char *syncResult);

#endif // REMOTE_CMD_H
