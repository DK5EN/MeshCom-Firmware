// rm_sender_policy.h -- pure sender-side policy of the RM web GUI (docs/rm-gui/impl-plan.md, C2).
//
// Header-only, no Arduino, no allocation, no clock access: every function takes "now" and all time
// math is unsigned 32-bit subtraction, so a millis() wrap at 2^32 is harmless.
//
//  1. Send policy per target: a rejected RM1 frame (wrong key, stale counter, even a wrong-key sync)
//     is SILENT on air and counts toward the target's lockout (3 within 90 s = 5 min locked, and the
//     sender can not see any of it). So the sender allows at most 2 unanswered sends to one target
//     inside 90 s and spaces sends to one target by 10 s (the target rate limits to one per 10 s).
//  2. Entry state: queued / waiting / noanswer / ok / err / unverified as a function of timestamps.
//  3. Plain sentences for every state and every RM error token.
//  4. The compact `status` reply: `s=<letters> p=<cur>/<max>` encode/decode and the whole formatter,
//     so the 63-char reply limit (RM_MAX_RESULT) is asserted on the real code in a native test.
#ifndef RM_SENDER_POLICY_H
#define RM_SENDER_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- constants (RM_POLICY_COOLDOWN_MS must equal RM_RATE_MS of remote_cmd.h; rm_runtime.cpp asserts it)
#define RM_POLICY_COOLDOWN_MS 10000u   // minimum spacing of two sends to one target
#define RM_POLICY_WINDOW_MS 90000u     // window of the unanswered-send budget (= target's reject window)
#define RM_POLICY_MAX_UNANSWERED 2u    // a 3rd unanswered send would be the target's 3rd strike
#define RM_NOANSWER_MS 75000u          // no reply for this long: "no answer"
#define RM_QUEUED_MS 3000u             // first seconds after the send: still in the local TX queue
#define RM_POLICY_MAX_ENTRIES 8u       // entries per target the policy looks at (the sent ring holds 5)
#define RM_PEND_SEND_DELAY_MS 10500u   // queued command goes out this long after the sync reply verified

// ---- entry state -------------------------------------------------------------------------------------
enum RmEntryState : uint8_t
{
    RM_ST_QUEUED = 0,  // handed to the TX ring, probably not on air yet
    RM_ST_WAITING,     // sent, no reply yet, inside the answer window
    RM_ST_NOANSWER,    // no reply after RM_NOANSWER_MS (or the entry outlived the key window)
    RM_ST_OK,          // verified reply "ok ..."
    RM_ST_ERR,         // verified reply "err ..."
    RM_ST_UNVERIFIED   // a reply arrived but its tag did not verify: never shown as success
};

// What the policy needs to know about one send. The caller fills it from its sent book.
struct RmPolEntry
{
    uint32_t sentMs;  // millis() at the send
    bool replied;     // some reply arrived
    bool verified;    // ... and its tag matched
    bool replyErr;    // verified reply starts with "err"
    bool expired;     // older than the reply window the caller tracks (aged out; guards the 2^32 wrap)
};

inline uint32_t rmPolicyAgeMs(uint32_t nowMs, uint32_t thenMs)
{
    return (uint32_t)(nowMs - thenMs);
}

inline RmEntryState rmPolicyState(const RmPolEntry &e, uint32_t nowMs)
{
    if (e.replied)
    {
        if (!e.verified)
            return RM_ST_UNVERIFIED;
        return e.replyErr ? RM_ST_ERR : RM_ST_OK;
    }
    if (e.expired)
        return RM_ST_NOANSWER;
    const uint32_t age = rmPolicyAgeMs(nowMs, e.sentMs);
    if (age >= RM_NOANSWER_MS)
        return RM_ST_NOANSWER;
    if (age < RM_QUEUED_MS)
        return RM_ST_QUEUED;
    return RM_ST_WAITING;
}

inline const char *rmStateName(RmEntryState s)
{
    switch (s)
    {
    case RM_ST_QUEUED:     return "queued";
    case RM_ST_WAITING:    return "waiting";
    case RM_ST_NOANSWER:   return "noanswer";
    case RM_ST_OK:         return "ok";
    case RM_ST_ERR:        return "err";
    case RM_ST_UNVERIFIED: return "unverified";
    }
    return "waiting";
}

// ---- send policy -------------------------------------------------------------------------------------
enum RmPolReason : uint8_t
{
    RM_POL_OK = 0,
    RM_POL_COOLDOWN,  // inside the 10 s spacing after the last send to this target
    RM_POL_LIMIT      // 2 unanswered sends inside 90 s: a third could lock the target
};

struct RmPolDecision
{
    bool allowed;
    RmPolReason reason;
    uint32_t retryS;  // seconds until a send is allowed again (0 when allowed); cooldown included
    uint8_t unanswered;  // unanswered sends inside the window
};

inline uint32_t rmPolicyCeilS(uint32_t ms)
{
    return (ms + 999u) / 1000u;
}

// e[0..n) = ALL entries of ONE target (any order). Unanswered = not verified, not expired, younger
// than the window. A verified err reply counts as answered: the target is alive and the key is right.
// Boundaries: age 10000 ms is out of the cooldown; age 90000 ms is out of the window.
inline RmPolDecision rmPolicyMaySend(const RmPolEntry *e, uint8_t n, uint32_t nowMs)
{
    RmPolDecision d = {true, RM_POL_OK, 0, 0};
    if (e == nullptr)
        return d;
    if (n > RM_POLICY_MAX_ENTRIES)
        n = (uint8_t)RM_POLICY_MAX_ENTRIES;

    uint32_t cooldownLeft = 0;
    uint32_t unans[RM_POLICY_MAX_ENTRIES];  // ages of the unanswered ones, sorted descending (oldest first)
    uint8_t k = 0;
    for (uint8_t i = 0; i < n; i++)
    {
        const uint32_t age = rmPolicyAgeMs(nowMs, e[i].sentMs);
        if (!e[i].expired && age < RM_POLICY_COOLDOWN_MS && RM_POLICY_COOLDOWN_MS - age > cooldownLeft)
            cooldownLeft = RM_POLICY_COOLDOWN_MS - age;
        if (e[i].verified || e[i].expired || age >= RM_POLICY_WINDOW_MS)
            continue;
        uint8_t j = k++;
        while (j > 0 && unans[j - 1] < age)
        {
            unans[j] = unans[j - 1];
            j--;
        }
        unans[j] = age;
    }
    d.unanswered = k;

    uint32_t limitLeft = 0;
    if (k >= RM_POLICY_MAX_UNANSWERED)
    {
        // the count must fall below the limit: the (k - limit + 1) oldest have to leave the window
        const uint8_t idx = (uint8_t)(k - RM_POLICY_MAX_UNANSWERED);  // 0-based among oldest-first
        limitLeft = RM_POLICY_WINDOW_MS - unans[idx];
        d.reason = RM_POL_LIMIT;
    }
    else if (cooldownLeft > 0)
        d.reason = RM_POL_COOLDOWN;

    const uint32_t left = limitLeft > cooldownLeft ? limitLeft : cooldownLeft;
    if (left > 0)
    {
        d.allowed = false;
        d.retryS = rmPolicyCeilS(left);
    }
    else
        d.reason = RM_POL_OK;
    return d;
}

// Automatic counter sync: only a sender WITHOUT a trusted clock (ctr = time would be valid otherwise)
// that has NO learnt counter mark for the target needs one. It is a condition, never a button, and
// the sync command itself never triggers another.
inline bool rmPolicyNeedSync(bool clockTrusted, bool haveTargetMark, bool cmdIsSync)
{
    return !cmdIsSync && !clockTrusted && !haveTargetMark;
}

// Chain of an automatic sync: the command waits for its sync. Pure decision, called every loop pass.
//   syncGone     the sync entry left the sent book
//   syncExpired  the sync entry is older than the key window (aged out)
//   syncVerified a reply to the sync arrived AND its tag verified
//   syncReplied  some reply arrived (verified or not); IGNORED on purpose: an unverified reply is not
//                an answer, so it must not keep the chain alive (hang found by the advisor pass)
//   ageMs        now - sync send time
//   sinceVerifiedMs  now - time the verified reply was booked (only read when syncVerified)
// SEND only RM_PEND_SEND_DELAY_MS after a VERIFIED sync reply (the target stamped its rate limiter with
// the sync). DROP when the sync is gone or expired, or when it is not verified and RM_NOANSWER_MS have
// passed, whatever replied says. WAIT otherwise. All ages are unsigned differences (millis() wrap safe).
enum RmPendAction : uint8_t
{
    RM_PEND_WAIT = 0,
    RM_PEND_SEND,
    RM_PEND_DROP
};

inline RmPendAction rmPendingDecide(bool syncVerified, bool syncReplied, bool syncExpired, bool syncGone,
                                    uint32_t ageMs, uint32_t sinceVerifiedMs)
{
    (void)syncReplied;
    if (syncGone || syncExpired)
        return RM_PEND_DROP;
    if (syncVerified)
        return sinceVerifiedMs >= RM_PEND_SEND_DELAY_MS ? RM_PEND_SEND : RM_PEND_WAIT;
    return ageMs >= RM_NOANSWER_MS ? RM_PEND_DROP : RM_PEND_WAIT;
}

// ---- reply text helpers ------------------------------------------------------------------------------
inline bool rmReplyIsErr(const char *reply)
{
    return reply != nullptr && strncmp(reply, "err", 3) == 0 && (reply[3] == '\0' || reply[3] == ' ');
}

// ---- plain-English messages --------------------------------------------------------------------------
inline const char *rmStateMessage(RmEntryState s)
{
    switch (s)
    {
    case RM_ST_QUEUED:
        return "Handed to the radio. It goes on air in a moment.";
    case RM_ST_WAITING:
        return "Sent. Waiting for the node to answer, this can take up to a minute.";
    case RM_ST_NOANSWER:
        return "No answer after 75 seconds. Check the password, the distance and that remote "
               "management is on at the other node. A third failed try locks it for 5 minutes.";
    case RM_ST_OK:
        return "Done. The node confirmed the command.";
    case RM_ST_ERR:
        return "The node received the command but could not do it.";
    case RM_ST_UNVERIFIED:
        return "An answer arrived but it could not be verified. Do not trust it.";
    }
    return "";
}

struct RmTokenMsg
{
    const char *token;
    const char *msg;
};

// Every RM error token. The first group is what a reply can carry ("err <token>") or a target counts
// silently (verdict names); the second group is what the sender itself refuses with.
inline const RmTokenMsg *rmTokenTable(size_t *n)
{
    static const RmTokenMsg t[] = {
        // reply / verdict tokens
        {"failed", "The node tried, but the setting did not change."},
        {"not output", "That pin is not set as an output on the node."},
        {"unsupported", "This board does not have that feature."},
        {"storage", "The node could not save its counter, so it did not run the command."},
        {"blocked", "The node does not accept this command or value."},
        {"rate", "Too fast. The node accepts one command every 10 seconds."},
        {"lockout", "The node is locked for 5 minutes after too many wrong tries."},
        {"replay", "The node has seen this counter already. Try again."},
        {"tag", "The password does not match the one on the node."},
        {"sync", "The counters are out of step. Check the connection first, then try again."},
        {"format", "The node could not read the message."},
        {"disabled", "Remote management is off on the node, or it has no password."},
        {"cached", "The node repeated its earlier answer."},
        // sender-side refusals
        {"limit", "Two tries to this node are still unanswered. Wait before trying again, a third "
                  "wrong try could lock it for 5 minutes."},
        {"busy", "Wait a few seconds. This node takes one command every 10 seconds."},
        {"passwd", "The password is not valid: 1 to 14 plain characters, no space at the start or end."},
        {"dst", "That call sign is not valid, or it is this node."},
        {"cmd", "That command is not allowed."},
        {"ctr", "This node ran out of counter values."},
        {"store", "This node could not save its send counter."},
        {"send", "The radio queue is full. Try again in a moment."},
        {"size", "The request was too large."},
        {"short", "The request was incomplete."},
        {"form", "The request could not be read."},
        // chain outcomes (a command queued behind an automatic sync that was then not sent)
        {"nosync", "The connection check got no answer, so your command was not sent. Check the "
                   "password, the distance and that remote management is on at the other node."},
        {"lost", "The connection check did not finish, so your command was not sent. Try again."},
    };
    if (n != nullptr)
        *n = sizeof(t) / sizeof(t[0]);
    return t;
}

// token = exact token ("not output") or a text that starts with it followed by a space or the end
// ("failed", "err failed", "not output pin"). Unknown token: a generic sentence, never empty.
inline const char *rmErrTokenMessage(const char *token)
{
    if (token == nullptr)
        return "The node reported an error.";
    if (strncmp(token, "err ", 4) == 0)
        token += 4;
    size_t n = 0;
    const RmTokenMsg *t = rmTokenTable(&n);
    for (size_t i = 0; i < n; i++)
    {
        const size_t l = strlen(t[i].token);
        if (strncmp(token, t[i].token, l) == 0 && (token[l] == '\0' || token[l] == ' '))
            return t[i].msg;
    }
    return "The node reported an error.";
}

// The table's own (static) token string for a token text, or `fallback` when it is unknown. Lets a
// caller keep a token beyond the lifetime of a stack buffer.
inline const char *rmTokenStatic(const char *token, const char *fallback)
{
    if (token == nullptr)
        return fallback;
    size_t n = 0;
    const RmTokenMsg *t = rmTokenTable(&n);
    for (size_t i = 0; i < n; i++)
        if (strcmp(token, t[i].token) == 0)
            return t[i].token;
    return fallback;
}

// The sentence for one sent entry: the state sentence, or for err the sentence of the reply's token.
inline const char *rmEntryMessage(RmEntryState s, const char *reply)
{
    if (s == RM_ST_ERR)
        return rmErrTokenMessage(reply);
    return rmStateMessage(s);
}

// ---- compact status token ----------------------------------------------------------------------------
// Fixed order, one letter per switch: upper case = on, lower case = off.
//   G gps   T track   D display   M mesh   W gateway   L led (omitted on boards without an LED)
// plus p=<cur>/<max> for the TX power in dBm. Example: "s=GtDMwL p=17/22".
#define RM_STATUS_SWITCHES 6u

struct RmSwitches
{
    bool gps, track, display, mesh, gateway, led;
    bool ledSupported;  // false: the led letter and the led= field are left out
};

struct RmStatusInfo
{
    bool haveS;                 // s= token present
    int8_t sw[RM_STATUS_SWITCHES];  // gps, track, display, mesh, gateway, led: -1 unknown, 0 off, 1 on
    bool ledSupported;          // led letter or led= present
    bool haveP;
    int cur, max;
};

// "s=GtDMwL" into out. Returns the length, 0 if it does not fit.
inline size_t rmStatusToken(char *out, size_t n, const RmSwitches &s)
{
    if (out == nullptr)
        return 0;
    const bool v[RM_STATUS_SWITCHES] = {s.gps, s.track, s.display, s.mesh, s.gateway, s.led};
    static const char up[RM_STATUS_SWITCHES + 1] = "GTDMWL";
    const size_t cnt = s.ledSupported ? RM_STATUS_SWITCHES : RM_STATUS_SWITCHES - 1u;
    if (n < 2 + cnt + 1)
        return 0;
    out[0] = 's';
    out[1] = '=';
    for (size_t i = 0; i < cnt; i++)
        out[2 + i] = v[i] ? up[i] : (char)(up[i] - 'A' + 'a');
    out[2 + cnt] = '\0';
    return 2 + cnt;
}

inline int rmClampDbm(int v)
{
    return v < -99 ? -99 : (v > 99 ? 99 : v);
}

// The whole `status` result (without the "RM1 <ctr> " frame and tag):
//   ok v=<ver> up=<min> bat=<%> heap=<kB> s=<letters> p=<cur>/<max>[ led=<0/1>]
// led= stays as the capability flag of older consumers (present only on boards with an LED). The old
// gw= and mesh= fields are gone: gateway and mesh are the W and M letters. Fields are ordered by
// value, so an (impossible) overflow would cut the redundant led= first. Returns the length written
// (always NUL-terminated; truncated, never overrun, when it does not fit n).
inline size_t rmFormatStatus(char *out, size_t n, const char *ver, uint32_t upMin, int bat, uint32_t heapKb,
                             const RmSwitches &sw, int txCur, int txMax)
{
    if (out == nullptr || n == 0)
        return 0;
    char tok[RM_STATUS_SWITCHES + 3];
    if (rmStatusToken(tok, sizeof(tok), sw) == 0)
        tok[0] = '\0';
    int w = snprintf(out, n, "ok v=%s up=%lu bat=%d heap=%lu %s p=%d/%d", ver != nullptr ? ver : "?",
                     (unsigned long)upMin, bat, (unsigned long)heapKb, tok, rmClampDbm(txCur), rmClampDbm(txMax));
    size_t len = w < 0 ? 0 : ((size_t)w >= n ? n - 1 : (size_t)w);
    if (sw.ledSupported && len < n - 1)
    {
        w = snprintf(out + len, n - len, " led=%d", sw.led ? 1 : 0);
        if (w > 0)
            len = ((size_t)w >= n - len) ? n - 1 : len + (size_t)w;
    }
    return len;
}

// Parses a status result: the compact form, or the older gw=/mesh=/led= form (then only those
// switches are known). Returns false when it is not a status result at all ("ok v=").
inline bool rmStatusParse(const char *res, RmStatusInfo &o)
{
    memset(&o, 0, sizeof(o));
    for (uint8_t i = 0; i < RM_STATUS_SWITCHES; i++)
        o.sw[i] = -1;
    if (res == nullptr || strncmp(res, "ok v=", 5) != 0)
        return false;

    const char *p = res;
    while (*p != '\0')
    {
        while (*p == ' ')
            p++;
        const char *t = p;
        while (*p != '\0' && *p != ' ')
            p++;
        const size_t l = (size_t)(p - t);
        if (l >= 3 && t[0] == 's' && t[1] == '=')
        {
            static const char up[RM_STATUS_SWITCHES + 1] = "GTDMWL";
            const size_t cnt = l - 2;
            if (cnt != RM_STATUS_SWITCHES && cnt != RM_STATUS_SWITCHES - 1u)
                return false;
            for (size_t i = 0; i < cnt; i++)
            {
                const char ch = t[2 + i];
                if (ch == up[i])
                    o.sw[i] = 1;
                else if (ch == (char)(up[i] - 'A' + 'a'))
                    o.sw[i] = 0;
                else
                    return false;
            }
            o.haveS = true;
            if (cnt == RM_STATUS_SWITCHES)
                o.ledSupported = true;
        }
        else if (l >= 5 && strncmp(t, "p=", 2) == 0)
        {
            int cur = 0, mx = 0, cn = 0;
            if (sscanf(t, "p=%d/%d%n", &cur, &mx, &cn) == 2 && (size_t)cn == l)
            {
                o.haveP = true;
                o.cur = cur;
                o.max = mx;
            }
        }
        else if (l == 5 && strncmp(t, "led=", 4) == 0 && (t[4] == '0' || t[4] == '1'))
        {
            o.ledSupported = true;
            if (o.sw[5] < 0)
                o.sw[5] = (int8_t)(t[4] - '0');
        }
        else if (l == 4 && strncmp(t, "gw=", 3) == 0 && (t[3] == '0' || t[3] == '1'))
        {
            if (o.sw[4] < 0)
                o.sw[4] = (int8_t)(t[3] - '0');
        }
        else if (l == 6 && strncmp(t, "mesh=", 5) == 0 && (t[5] == '0' || t[5] == '1'))
        {
            if (o.sw[3] < 0)
                o.sw[3] = (int8_t)(t[5] - '0');
        }
    }
    return true;
}

#endif // RM_SENDER_POLICY_H
