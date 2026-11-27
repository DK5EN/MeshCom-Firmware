// RM-02: protocol core of the authenticated remote management DM ("RM1").
// See remote_cmd.h and docs/concept-open-issues-20261004.md section 6.

#include "remote_cmd.h"

#include <string.h>

#include "hmac_sha256.h"
#include "rm_commands.h"
#include "rm_text.h"

static_assert(RM_MAX_ARGS == 39, "args cap is part of the wire contract (concept 2.2)");
static_assert(RM_MAX_RESULT == 108, "4+10+1+108+1+16 = 140 = the DM text limit");
static_assert(4 + 10 + 1 + RM_MAX_RESULT + 1 + 16 <= 140, "reply DM text must fit 140 chars");

namespace
{

// ---- small bounded text builder (no printf, no malloc) --------------------

// Scan bounds for strings whose size the caller knows but this file does not see. Each lies well above anything a
// caller can hand over, so the result is the same for every real input; a longer string is refused:
const size_t RM_CALL_SCAN_MAX = 32;   // dst/src callsign: node_call[10], msg_source_call[21]
const size_t RM_PASSWD_SCAN_MAX = 64; // node_passwd[15]
const size_t RM_TEXT_SCAN_MAX = 200;  // a DM text carries at most 160 characters (rmParse reads up to 200 as well)

struct Out
{
    char *p;
    size_t n;
    size_t len;
    bool ok;
};

void outInit(Out &o, char *buf, size_t n)
{
    o.p = buf;
    o.n = n;
    o.len = 0;
    o.ok = (buf != nullptr && n > 0);
}

void outStr(Out &o, const char *s)
{
    if (!o.ok)
        return;
    if (s == nullptr)
    {
        o.ok = false;
        return;
    }
    // B-RM-RANGE-01 (2026-11-26): one length, one bound check, one copy. The former loop wrote byte by byte
    // behind a counter and gave up at the first byte that left no room for the terminator; the result is
    // the same (fails iff len + n >= capacity for n > 0), except that a failed append now writes nothing
    // (outEnd() returns 0 and clears p[0] either way). A bounded model checker sees the write position as
    // one offset instead of a counter that depends on every earlier byte.
    const size_t n = strlen(s);
    if (n != 0 && o.len + n >= o.n)
    {
        o.ok = false;
        return;
    }
    memcpy(o.p + o.len, s, n);
    o.len += n;
}

// outStr() for a field of known size: the length is measured with that size as bound. Same as outStr() for a
// terminated field; an unterminated one is appended up to its size instead of being read past its end.
void outStrN(Out &o, const char *s, size_t cap)
{
    if (!o.ok)
        return;
    if (s == nullptr)
    {
        o.ok = false;
        return;
    }
    const size_t n = strnlen(s, cap);
    if (n != 0 && o.len + n >= o.n)
    {
        o.ok = false;
        return;
    }
    memcpy(o.p + o.len, s, n);
    o.len += n;
}

// a dst/src callsign within the scan bound (nullptr is handled by outStrN)
bool callFits(const char *call)
{
    return call == nullptr || strnlen(call, RM_CALL_SCAN_MAX + 1) <= RM_CALL_SCAN_MAX;
}

// Decimal text of v, no leading zeros ("0" for 0). Each digit is found by subtracting its power of ten at most nine
// times: a fixed 10 x 9 compare-and-subtract with no division and no value-dependent loop count, so a bounded model
// checker sees one small circuit instead of a chain of 32-bit dividers (B-RM-RANGE-01, 2026-11-26). Same text as the
// former v % 10 / v /= 10 loop for every v (test_rm_range).
void outU32(Out &o, uint32_t v)
{
    static const uint32_t POW10[10] = {1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                       10000u,      1000u,      100u,      10u,      1u};
    char tmp[11];
    size_t n = 0;
    for (unsigned k = 0; k < 10; k++)
    {
        char d = '0';
        for (unsigned j = 0; j < 9; j++)
            if (v >= POW10[k])
            {
                v -= POW10[k];
                d++;
            }
        if (d != '0' || n != 0 || k == 9)
            tmp[n++] = d;
    }
    tmp[n] = '\0';
    outStrN(o, tmp, sizeof(tmp));
}

size_t outEnd(Out &o)
{
    if (!o.ok)
    {
        if (o.p != nullptr && o.n > 0)
            o.p[0] = '\0';
        return 0;
    }
    o.p[o.len] = '\0';
    return o.len;
}

// ---- helpers --------------------------------------------------------------

size_t strippedLen(const char *passwd)
{
    if (passwd == nullptr)
        return 0;
    size_t n = strnlen(passwd, RM_PASSWD_SCAN_MAX + 1);
    if (n > RM_PASSWD_SCAN_MAX)
        return 0; // longer than any password field: refused like an empty secret
    while (n > 0 && passwd[n - 1] == ' ')
        n--;
    return n;
}

bool isLowerHex(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

// B-RM-RANGE-01: string helpers whose loops have a fixed bound, so a bounded model checker unrolls them exactly
// as often as the data can need, not up to its global --unwind. Same results as strcmp()/strlen() on a terminated
// string that fits.
const size_t RM_ARG_CAP = RM_MAX_ARGS + 1; // size of RmCmd::args

// s equals the string literal lit; the loop runs over lit, so its length is the bound
bool litEq(const char *s, const char *lit)
{
    size_t k = 0;
    for (; lit[k] != '\0'; k++)
        if (s[k] != lit[k])
            return false;
    return s[k] == '\0';
}

// cap = size of the array s lives in (cmd or args field)
bool isForbiddenText(const char *s, size_t cap)
{
    for (size_t k = 0; k < cap && s[k] != '\0'; k++)
    {
        if (s[k] == ';' || s[k] == '{' || s[k] == '%')
            return true;
        if (s[k] == '-' && k + 1 < cap && s[k + 1] == '-')
            return true;
    }
    return false;
}

// decimal digits only, no leading zero unless the whole token is "0", at most maxDigits
bool parseSmallInt(const char *s, unsigned maxDigits, int &out)
{
    size_t n = strnlen(s, (size_t)maxDigits + 1); // > maxDigits means too long, as strlen() would tell
    if (n == 0 || n > maxDigits)
        return false;
    if (n > 1 && s[0] == '0')
        return false;
    int v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

bool isOnOff(const char *a)
{
    return litEq(a, "on") || litEq(a, "off");
}

// allowlist of section 6.4 as a table: one row per command name, generated from rm_commands.h. Adding
// a command is one line there (plus the dispatch in rm_runtime.cpp).
enum RmArgShape : uint8_t
{
    RM_ARGS_NONE,    // no args
    RM_ARGS_ONOFF,   // "on" | "off"
    RM_ARGS_TXPOWER, // 0..maxTxPower, at most 3 digits
    RM_ARGS_SETOUT,  // "<pin> <on|off>", pin a0..a7 or b0..b7 (the console's --setout MCP pins)
    RM_ARGS_LED,     // "on" | "off" | "flash"
    RM_ARGS_CHGLED,  // none (read the effective mode, Q2) | "auto" | "on" | "off"
    // extended commands (RM ext W2-A): SYNTAX only, ranges and the strict charset are the executor's
    // job (err range / err text). Capitals are allowed here and only here (concept 2.4).
    RM_ARGS_NAME,    // none (read) | free text 1..RM_NAME_MAX (write)
    RM_ARGS_ATXT,    // none (read) | free text 1..RM_ATXT_MAX (write)
    RM_ARGS_POS,     // none (read) | "<lat> <lon> <alt>" (write)
    RM_ARGS_MH,      // row index 0..999 | callsign with optional SSID
    RM_ARGS_UPD,     // none (read) | "check" | "go <tag>", tag [A-Za-z0-9._-] 1..23 | "chan prod"|"chan dev" (only with MC_AU_DEV_CHANNEL)
    RM_ARGS_CALLS,   // none (read) | "none" | comma list of [A-Za-z0-9,-] 1..RM_MAX_ARGS (syntax only, the executor validates)
    RM_ARGS_PINS,    // none (read) | "<ow|btn|an|wind> <0..48>" (GPIO-01, no leading zero)
    RM_ARGS_GPIO,    // none (read) | "<0..48> <on|off|in>" (GPIO-01; never the master switch)
};

struct RmAllowRow
{
    const char *name;
    RmArgShape shape;
};

// One row per command of rm_commands.h (same order); the shape is the list's third column.
#define RM_ALLOW_ROW(n, k, shape) {#n, RM_ARGS_##shape},
const RmAllowRow RM_ALLOWLIST[] = {
    RM_COMMAND_LIST(RM_ALLOW_ROW)
};
#undef RM_ALLOW_ROW
static_assert(sizeof(RM_ALLOWLIST) / sizeof(RM_ALLOWLIST[0]) == RM_COMMAND_COUNT, "allowlist = command list");

bool argsMatch(RmArgShape shape, const char *a, int minTxPower, int maxTxPower)
{
    switch (shape)
    {
    case RM_ARGS_NONE:
        return a[0] == '\0';
    case RM_ARGS_ONOFF:
        return isOnOff(a);
    case RM_ARGS_LED:
        return isOnOff(a) || litEq(a, "flash");
    case RM_ARGS_CHGLED:
        return a[0] == '\0' || isOnOff(a) || litEq(a, "auto");
    case RM_ARGS_TXPOWER:
    {
        int v = 0;
        return parseSmallInt(a, 3, v) && v >= minTxPower && v <= maxTxPower;
    }
    case RM_ARGS_SETOUT:
        return strnlen(a, 4) >= 4 && (a[0] == 'a' || a[0] == 'b') && a[1] >= '0' && a[1] <= '7' && a[2] == ' ' &&
               isOnOff(a + 3);
    case RM_ARGS_NAME:
        return a[0] == '\0' || rmArgIsFreeText(a, RM_NAME_MAX);
    case RM_ARGS_ATXT:
        return a[0] == '\0' || rmArgIsFreeText(a, RM_ATXT_MAX);
    case RM_ARGS_POS:
        return a[0] == '\0' || rmArgIsPos(a);
    case RM_ARGS_MH:
    {
        // an all-digit token is a row index only (4+ digits must not pass as a callsign)
        bool digits = a[0] != '\0';
        for (size_t k = 0; k < RM_ARG_CAP && a[k] != '\0'; k++)
            digits = digits && a[k] >= '0' && a[k] <= '9';
        return digits ? rmArgIsRowIndex(a) : rmArgIsCall(a);
    }
    case RM_ARGS_CALLS:
    {
        if (a[0] == '\0')
            return true;
        for (size_t k = 0; k < RM_ARG_CAP && a[k] != '\0'; k++)
        {
            const char ch = a[k];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ',' ||
                  ch == '-'))
                return false;
        }
        return strnlen(a, RM_ARG_CAP) <= RM_MAX_ARGS;
    }
    case RM_ARGS_PINS:
    {
        if (a[0] == '\0')
            return true;
        static const char *const fns[] = {"ow ", "btn ", "an ", "wind "};
        for (const char *f : fns)
        {
            const size_t fl = strlen(f);
            int v = 0;
            if (strncmp(a, f, fl) == 0)
                return parseSmallInt(a + fl, 2, v) && v <= 48;
        }
        return false;
    }
    case RM_ARGS_GPIO:
    {
        if (a[0] == '\0')
            return true;
        // the first space must be at index 1 or 2 (a 1- or 2-digit pin); only those can be it
        const char *sp = a[1] == ' ' ? a + 1 : (a[1] != '\0' && a[2] == ' ') ? a + 2 : nullptr;
        if (sp == nullptr)
            return false;
        char num[3] = {a[0], sp - a == 2 ? a[1] : '\0', '\0'};
        int v = 0;
        return parseSmallInt(num, 2, v) && v <= 48 && (litEq(sp + 1, "on") || litEq(sp + 1, "off") || litEq(sp + 1, "in"));
    }
    case RM_ARGS_UPD:
    {
        if (a[0] == '\0' || litEq(a, "check"))
            return true;
#if defined(MC_AU_DEV_CHANNEL)
        if (litEq(a, "chan prod") || litEq(a, "chan dev"))
            return true;
#endif
        if (strncmp(a, "go ", 3) != 0)
            return false;
        const size_t n = strnlen(a + 3, 24);
        if (n < 1 || n > 23)
            return false;
        for (size_t k = 3; k < 3 + n; k++)
        {
            const char ch = a[k];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                  ch == '_' || ch == '-'))
                return false;
        }
        return true;
    }
    }
    return false;
}

bool allowed(const RmCmd &c, int minTxPower, int maxTxPower)
{
    // B-RM-RANGE-01: litEq() runs over the table name, so each comparison is bounded by that name's length.
    // Same result as strcmp() for a terminated cmd.
    for (const RmAllowRow &r : RM_ALLOWLIST)
        if (litEq(c.cmd, r.name))
            return argsMatch(r.shape, c.args, minTxPower, maxTxPower);
    return false;
}

// computes the 16-hex tag of text under the key derived from passwd
// len = the length the canonical builder returned (no second scan of text)
void tagOf(const char *passwd, const char *text, size_t len, char tagHex[17])
{
    uint8_t key[32];
    uint8_t mac[32];
    rmDeriveKey(passwd, key);
    hmacSha256(key, sizeof(key), reinterpret_cast<const uint8_t *>(text), len, mac);
    hexLower(mac, 8, tagHex);
    hmac_sha256_detail::wipe(key, sizeof(key));
    hmac_sha256_detail::wipe(mac, sizeof(mac));
}

// same, with the key already derived (sender side)
void tagOfKey(const uint8_t key[32], const char *text, size_t len, char tagHex[17])
{
    uint8_t mac[32];
    hmacSha256(key, 32, reinterpret_cast<const uint8_t *>(text), len, mac);
    hexLower(mac, 8, tagHex);
    hmac_sha256_detail::wipe(mac, sizeof(mac));
}

bool tagEqual(const char *a, const char *b)
{
    return ctEqual(reinterpret_cast<const uint8_t *>(a), reinterpret_cast<const uint8_t *>(b), 16);
}

// Reject table (per sender, see RmRejSrc). A call is compared and stored as its first
// sizeof(RmRejSrc::call) - 1 = 9 characters (a callsign-SSID is at most 9); nullptr is the empty call.
const size_t REJ_CALL_LEN = sizeof(RmRejSrc::call) - 1;

bool rejLocked(const RmRejSrc &r, uint32_t now)
{
    return r.lockActive && (int32_t)(now - r.lockUntilMs) < 0;
}

// The sender's entry, nullptr if it has none. Never creates one.
const RmRejSrc *rejFindConst(const RmState &s, const char *src)
{
    const char *call = src ? src : "";
    for (const RmRejSrc &r : s.rej)
        if (r.used && strncmp(r.call, call, REJ_CALL_LEN) == 0)
            return &r;
    return nullptr;
}

RmRejSrc *rejFind(RmState &s, const char *src)
{
    return const_cast<RmRejSrc *>(rejFindConst(s, src));
}

// The sender's entry, created if needed. A full table gives up, in this order: an unused entry, the
// entry with the oldest lastMs that is not locked at `now` (an expired lock counts as not locked),
// and only if all are locked the one with the oldest lastMs.
RmRejSrc *rejSlot(RmState &s, const char *src, uint32_t now)
{
    RmRejSrc *r = rejFind(s, src);
    if (r)
        return r;
    RmRejSrc *unused = nullptr, *oldOpen = nullptr, *oldAny = nullptr;
    for (RmRejSrc &e : s.rej)
    {
        if (!e.used)
        {
            unused = &e;
            break;
        }
        if (!oldAny || (uint32_t)(now - e.lastMs) > (uint32_t)(now - oldAny->lastMs))
            oldAny = &e;
        if (!rejLocked(e, now) && (!oldOpen || (uint32_t)(now - e.lastMs) > (uint32_t)(now - oldOpen->lastMs)))
            oldOpen = &e;
    }
    r = unused ? unused : (oldOpen ? oldOpen : oldAny);
    memset(r, 0, sizeof(*r));
    r->used = true;
    strncpy(r->call, src ? src : "", REJ_CALL_LEN);
    return r;
}

// FNV-1a over "cmd args". Both fields are read bounded by their size, so an unterminated field (a FORMAT reject)
// is safe.
uint32_t cmdArgsHash(const RmCmd &c)
{
    uint32_t h = 2166136261u;
    const size_t nc = strnlen(c.cmd, sizeof(c.cmd));
    const size_t na = strnlen(c.args, sizeof(c.args));
    for (size_t i = 0; i < nc; i++)
        h = (h ^ (uint8_t)c.cmd[i]) * 16777619u;
    h = (h ^ (uint8_t)' ') * 16777619u;
    for (size_t i = 0; i < na; i++)
        h = (h ^ (uint8_t)c.args[i]) * 16777619u;
    return h;
}

// "Same guess = one strike": a counted reject whose frame (ctr + 16 tag chars + hash of "cmd args") equals one of
// the frames that sender already struck with in the current window (RmRejSrc::frame) adds no strike (ring copies /
// repeats of one command are one guess; a key guesser gains nothing from repeating a frame). A different ctr, tag,
// cmd or args counts as before.
RmVerdict reject(RmState &s, const char *src, RmVerdict v, uint32_t now, const RmCmd &c)
{
    // BF-01: without strict security nothing is punished. The caller still gets the verdict (and counts it
    // in its own statistics), but no strike is recorded and no lock can arm.
    if (!s.strict)
        return v;
    RmRejSrc *r = rejSlot(s, src, now);
    RmRejFrame f;
    memset(&f, 0, sizeof(f));
    f.ctr = c.ctr;
    f.cmdArgsHash = cmdArgsHash(c);
    memcpy(f.tag, c.tag, strnlen(c.tag, sizeof(f.tag))); // safe for a tag field that is not NUL-terminated
    const bool windowOpen = r->rejCount != 0 && (uint32_t)(now - r->rejWindowMs) < RM_REJ_WINDOW_MS;
    if (windowOpen)
        for (uint8_t i = 0; i < r->nFrames; i++)
            if (r->frame[i].ctr == f.ctr && r->frame[i].cmdArgsHash == f.cmdArgsHash && tagEqual(r->frame[i].tag, f.tag))
                return v;
    if (!windowOpen)
    {
        r->rejCount = 1;
        r->rejWindowMs = now;
        r->nFrames = 0;
        r->nextFrame = 0;
    }
    else if (r->rejCount < 255)
        r->rejCount++;
    r->lastMs = now;
    r->frame[r->nextFrame] = f;
    r->nextFrame = (uint8_t)((r->nextFrame + 1) % RM_REJ_FRAMES);
    if (r->nFrames < RM_REJ_FRAMES)
        r->nFrames++;

    if (r->rejCount >= RM_REJ_LIMIT)
    {
        r->nFrames = 0;
        r->nextFrame = 0;
        memset(r->frame, 0, sizeof(r->frame));
        r->lockActive = true;
        r->lockUntilMs = now + RM_LOCKOUT_MS;
        r->rejCount = 0;
    }
    return v;
}

} // namespace

const char *rmVerdictName(RmVerdict v)
{
    switch (v)
    {
    case RM_OK:
        return "ok";
    case RM_CACHED:
        return "cached";
    case RM_SYNC:
        return "sync";
    case RM_REJ_FORMAT:
        return "format";
    case RM_REJ_TAG:
        return "tag";
    case RM_REJ_REPLAY:
        return "replay";
    case RM_REJ_BLOCKED:
        return "blocked";
    case RM_REJ_RATE:
        return "rate";
    case RM_REJ_LOCKOUT:
        return "lockout";
    case RM_REJ_DISABLED:
        return "disabled";
    case RM_REFUSED:
        return "refused";
    }
    return "?";
}

// Shared token parsers of the command frame ("RM1 <ctr> <cmd...> <tag>") and the reply frame
// ("RM1 <ctr> <result> <tag>"): the tag is the last token, exactly 16 lower-case hex preceded by one space.
static bool rmTagTokenValid(const char *text, size_t len)
{
    if (len < 17)
        return false;
    const size_t tagPos = len - 16;
    if (text[tagPos - 1] != ' ')
        return false;
    for (size_t i = tagPos; i < len; i++)
        if (!isLowerHex(text[i]))
            return false;
    return true;
}

// ctr token starting at text[i], digits only up to (excluding) limit: decimal, no leading zero, fits uint32.
// On success ctr is set and next is the index of the first byte after the digits.
static bool rmCtrToken(const char *text, size_t i, size_t limit, uint32_t &ctr, size_t &next)
{
    const size_t start = i;
    uint64_t v = 0;
    while (i < limit && text[i] >= '0' && text[i] <= '9')
    {
        v = v * 10 + (uint64_t)(text[i] - '0');
        if (v > 0xFFFFFFFFULL)
            return false;
        i++;
    }
    const size_t digits = i - start;
    if (digits == 0 || (digits > 1 && text[start] == '0'))
        return false;
    ctr = (uint32_t)v;
    next = i;
    return true;
}

bool rmParse(const char *text, RmCmd &out)
{
    memset(&out, 0, sizeof(out));
    if (text == nullptr)
        return false;

    // bounded length: a DM carries at most 160 characters
    size_t len = 0;
    while (len < 200 && text[len])
        len++;
    if (len >= 200 || len < 4 + 1 + 1 + 1 + 1 + 16 || strncmp(text, "RM1 ", 4) != 0)
        return false;

    // printable ASCII only. Capitals are refused in the command name and the tag (checked below); the
    // args may carry them where an allowlist shape allows it (free text, callsign), nothing is case-folded
    for (size_t i = 3; i < len; i++) // "RM1" itself is upper case
    {
        const unsigned char ch = (unsigned char)text[i];
        if (ch < 0x20 || ch > 0x7e)
            return false;
    }
    if (text[len - 1] == ' ')
        return false;

    // tag: last token, exactly 16 lower-case hex preceded by a single space
    if (!rmTagTokenValid(text, len))
        return false;
    const size_t tagPos = len - 16;

    // ctr
    size_t i = 4;
    uint32_t ctr = 0;
    if (!rmCtrToken(text, i, tagPos - 1, ctr, i))
        return false;
    if (i >= tagPos - 1 || text[i] != ' ')
        return false;
    i++;

    // body between ctr and tag: "<cmd>[ <args>]", single spaces
    const size_t bodyEnd = tagPos - 1; // index of the space before the tag
    if (i >= bodyEnd || text[i] == ' ')
        return false;
    // B-RM-RANGE-01: cmd and args are located as ranges of text and copied once each; the checks are the
    // former per-byte ones (cmd 1..15 bytes without capitals, args 1..RM_MAX_ARGS bytes without a double
    // space). A frame rejected by these checks now leaves cmd and args all zero (they used to hold the bytes
    // copied so far; no caller reads `out` after a false return).
    const size_t cmdStart = i;
    while (i < bodyEnd && text[i] != ' ')
    {
        if (i - cmdStart + 1 >= sizeof(out.cmd) || (text[i] >= 'A' && text[i] <= 'Z'))
            return false;
        i++;
    }
    const size_t cmdLen = i - cmdStart;
    size_t argStart = bodyEnd, argLen = 0;
    if (i < bodyEnd)
    {
        i++; // the single space before args
        if (i >= bodyEnd || text[i] == ' ')
            return false;
        argStart = i;
        argLen = bodyEnd - i;
        if (argLen + 1 > sizeof(out.args))
            return false;
        for (size_t k = argStart; k < bodyEnd; k++)
            if (text[k] == ' ' && text[k + 1] == ' ')
                return false;
    }
    memcpy(out.cmd, text + cmdStart, cmdLen);
    if (argLen != 0)
        memcpy(out.args, text + argStart, argLen);

    out.ctr = ctr;
    if (out.ctr == 0 && !litEq(out.cmd, "sync"))
        return false;
    memcpy(out.tag, text + tagPos, 16);
    out.tag[16] = '\0';
    return true;
}

void rmDeriveKey(const char *passwd, uint8_t key[32])
{
    const size_t n = strippedLen(passwd);
    if (n == 0)
    {
        memset(key, 0, 32);
        return;
    }
    sha256(reinterpret_cast<const uint8_t *>(passwd), n, key);
}

// RM_CANONICAL_EXTERNAL: the bounded proofs of rmCheck() replace this builder by its contract
// (specs/cbmc/remote_cmd/harness.cpp -DCANON_CONTRACT) and prove the builder on its own (canon.check). Firmware builds
// never define it.
#ifndef RM_CANONICAL_EXTERNAL
size_t rmCanonical(const RmCmd &c, const char *dst, const char *src, char *out, size_t n)
{
    Out o;
    outInit(o, out, n);
    if (!callFits(dst) || !callFits(src))
        o.ok = false;
    outStr(o, "RM1|");
    outStrN(o, dst, RM_CALL_SCAN_MAX);
    outStr(o, "|");
    outStrN(o, src, RM_CALL_SCAN_MAX);
    outStr(o, "|");
    outU32(o, c.ctr);
    outStr(o, "|");
    outStrN(o, c.cmd, sizeof(c.cmd));
    if (c.args[0] != '\0')
    {
        outStr(o, " ");
        outStrN(o, c.args, sizeof(c.args));
    }
    return outEnd(o);
}
#endif

size_t rmReplyCanonical(uint32_t ctr, const char *dst, const char *src, const char *result, char *out, size_t n)
{
    Out o;
    outInit(o, out, n);
    if (!callFits(dst) || !callFits(src) || (result != nullptr && strnlen(result, RM_MAX_RESULT + 1) > RM_MAX_RESULT))
        o.ok = false;
    outStr(o, "RM1R|");
    outStrN(o, dst, RM_CALL_SCAN_MAX);
    outStr(o, "|");
    outStrN(o, src, RM_CALL_SCAN_MAX);
    outStr(o, "|");
    outU32(o, ctr);
    outStr(o, "|");
    outStrN(o, result, RM_MAX_RESULT);
    return outEnd(o);
}

void rmStateInit(RmState &s, uint32_t hwm)
{
    memset(&s, 0, sizeof(s));
    s.hwm = hwm;
}

void rmReceiverUnlock(RmState &s)
{
    memset(s.rej, 0, sizeof(s.rej));
}

void rmRejSweep(RmState &s, uint32_t nowMs)
{
    if (!s.strict)
    {
        // BF-01: strict security is off (also: just switched off) -- no strike or lock outlives it
        memset(s.rej, 0, sizeof(s.rej));
        return;
    }
    for (RmRejSrc &r : s.rej)
    {
        if (!r.used)
            continue;
        if (r.lockActive)
        {
            if (!rejLocked(r, nowMs))
                memset(&r, 0, sizeof(r)); // lock over: the sender starts fresh
        }
        else if (r.rejCount == 0 || (uint32_t)(nowMs - r.rejWindowMs) >= RM_REJ_WINDOW_MS)
            memset(&r, 0, sizeof(r)); // window over: these strikes can never complete a lockout
    }
}

bool rmSenderLocked(const RmState &s, const char *src, uint32_t nowMs)
{
    const RmRejSrc *r = rejFindConst(s, src);
    return r != nullptr && rejLocked(*r, nowMs);
}

uint8_t rmLockedSenders(const RmState &s, uint32_t nowMs, uint32_t *maxRemainMs)
{
    uint8_t n = 0;
    uint32_t mx = 0;
    for (const RmRejSrc &r : s.rej)
        if (r.used && rejLocked(r, nowMs))
        {
            n++;
            const uint32_t rem = (uint32_t)(r.lockUntilMs - nowMs);
            if (rem > mx)
                mx = rem;
        }
    if (maxRemainMs)
        *maxRemainMs = mx;
    return n;
}

namespace
{
// rmCheck() without the strike: every path returns its verdict, and rmCheck() books a strike for the counted ones
// (FORMAT, TAG, BLOCKED) at its single exit. One reject() call instead of one per check keeps the code a bounded model
// checker unrolls small (B-RM-RANGE-01, 2026-11-26); the order of effects is unchanged, reject() was always the last
// step of a counted path.
RmVerdict checkFrame(RmState &s, const RmCmd &c, const char *dst, const char *src, const char *passwd,
                     int minTxPower, int maxTxPower, uint32_t nowMs)
{
    if (strippedLen(passwd) == 0)
        return RM_REJ_DISABLED;

    // The sweep frees every expired lock, so a sender still flagged lockActive afterwards is locked
    // at nowMs (an "expired lock" branch here would be unreachable). Without strict security the sweep
    // empties the table and nothing can lock (BF-01).
    rmRejSweep(s, nowMs);
    if (s.strict && rmSenderLocked(s, src, nowMs))
        return RM_REJ_LOCKOUT;

    if (dst == nullptr || src == nullptr || c.tag[16] != '\0' || c.cmd[sizeof(c.cmd) - 1] != '\0' ||
        c.args[sizeof(c.args) - 1] != '\0')
        return RM_REJ_FORMAT;

    const bool isSync = litEq(c.cmd, "sync");
    if ((c.ctr == 0) != isSync)
        return RM_REJ_FORMAT;

    char canon[160]; // "RM1|<9>|<9>|<10>|<15>|<39>" <= 90
    const size_t cl = rmCanonical(c, dst, src, canon, sizeof(canon));
    if (cl == 0)
        return RM_REJ_FORMAT;
    char want[17];
    tagOf(passwd, canon, cl, want);
    if (!tagEqual(want, c.tag))
        return RM_REJ_TAG;

    if (!isSync)
    {
        if (s.haveLast && c.ctr == s.lastCtr && tagEqual(s.lastTag, c.tag) &&
            (uint32_t)(nowMs - s.lastAcceptMs) <= RM_CACHE_MS)
            return RM_CACHED;
        // A valid tag with a stale counter is NOT counted towards the lockout: the lockout throttles
        // key guessing, and a valid tag is not a guess. A replayed or overtaken (reordered) frame
        // reveals nothing and executes nothing; counting it let a legitimate sender with path-delay
        // jitter, or anyone replaying one sniffed frame three times, lock the node.
        if (c.ctr <= s.hwm)
            return RM_REJ_REPLAY;
    }

    // Only an authenticated sender reaches this point; a rate reject is not an
    // attack signal and does not count towards the lockout (advisor RM W1 N1).
    // A sync has its own limiter: a replayed sync frame (ctr 0 skips the replay check) must
    // neither starve genuine commands nor be answered more than once per RM_SYNC_RATE_MS.
    // BF-01 (D2): both limiters are active only with strict security on.
    if (isSync)
    {
        // a sync carries no arguments; with a valid tag but args it is a malformed frame, not an answerable
        // command (no counter to consume), so it stays a strike
        if (!allowed(c, minTxPower, maxTxPower))
            return RM_REJ_BLOCKED;
        if (s.strict && s.haveSync && (uint32_t)(nowMs - s.lastSyncMs) < RM_SYNC_RATE_MS)
            return RM_REJ_RATE;
        s.lastSyncMs = nowMs;
        s.haveSync = true;
        return RM_SYNC;
    }

    if (s.strict && s.haveRate && (uint32_t)(nowMs - s.lastRateMs) < RM_RATE_MS)
        return RM_REJ_RATE;

    // RM-STRIKE: the allowlist is decided last. The tag is valid here, so the sender is authenticated and a
    // refusal is an answer (RM_REFUSED, no strike), not an attack signal. The caller consumes the counter.
    if (isForbiddenText(c.cmd, sizeof(c.cmd)) || isForbiddenText(c.args, sizeof(c.args)) || !allowed(c, minTxPower, maxTxPower))
        return RM_REFUSED;
    return RM_OK;
}
} // namespace

RmVerdict rmCheck(RmState &s, const RmCmd &c, const char *dst, const char *src, const char *passwd,
                  int minTxPower, int maxTxPower, uint32_t nowMs)
{
    const RmVerdict v = checkFrame(s, c, dst, src, passwd, minTxPower, maxTxPower, nowMs);
    if (v == RM_REJ_FORMAT || v == RM_REJ_TAG || v == RM_REJ_BLOCKED)
        return reject(s, src, v, nowMs, c);
    return v;
}

size_t rmRefusalText(const RmCmd &c, int minTxPower, int maxTxPower, char *out, size_t n)
{
    if (out == nullptr || n == 0)
        return 0;
    Out o;
    outInit(o, out, n);
    if (litEq(c.cmd, "txpower") && !argsMatch(RM_ARGS_TXPOWER, c.args, minTxPower, maxTxPower))
    {
        outStr(o, "err range ");
        outU32(o, minTxPower > 0 ? (uint32_t)minTxPower : 0u); // remote floor is >= 0 by contract
        outStr(o, "..");
        outU32(o, maxTxPower > 0 ? (uint32_t)maxTxPower : 0u);
    }
    else
        outStr(o, "err blocked");
    return outEnd(o);
}

uint32_t rmAccept(RmState &s, const RmCmd &c, const char *result, uint32_t nowMs)
{
    if (c.ctr > s.hwm)
        s.hwm = c.ctr;
    s.lastCtr = c.ctr;
    s.lastAcceptMs = nowMs;
    s.lastRateMs = nowMs;
    s.haveRate = true;
    s.haveLast = true;
    memcpy(s.lastTag, c.tag, sizeof(s.lastTag));
    s.lastTag[16] = '\0';

    size_t n = 0;
    if (result != nullptr)
    {
        n = strnlen(result, RM_MAX_RESULT + 1);
        if (n > RM_MAX_RESULT)
            n = RM_MAX_RESULT;
        memcpy(s.lastReply, result, n);
    }
    s.lastReply[n] = '\0';
    return s.hwm;
}

size_t rmReply(const RmCmd &c, const char *result, const char *dst, const char *src, const char *passwd,
               char *out, size_t n)
{
    if (result == nullptr || dst == nullptr || src == nullptr || strippedLen(passwd) == 0 ||
        strnlen(result, RM_MAX_RESULT + 1) > RM_MAX_RESULT)
        return 0;

    char canon[200];
    const size_t cl = rmReplyCanonical(c.ctr, dst, src, result, canon, sizeof(canon));
    if (cl == 0)
        return 0;

    char rtag[17];
    tagOf(passwd, canon, cl, rtag);

    Out r;
    outInit(r, out, n);
    outStr(r, "RM1 ");
    outU32(r, c.ctr);
    outStr(r, " ");
    outStrN(r, result, RM_MAX_RESULT);
    outStr(r, " ");
    outStrN(r, rtag, sizeof(rtag));
    return outEnd(r);
}

bool rmIsReply(const char *text)
{
    if (text == nullptr || strncmp(text, "RM1 ", 4) != 0)
        return false;
    const char *p = text + 4;
    while (p < text + RM_TEXT_SCAN_MAX && *p >= '0' && *p <= '9')
        p++;
    if (p == text + 4 || *p != ' ')
        return false;
    return strncmp(p + 1, "ok ", 3) == 0 || strncmp(p + 1, "err ", 4) == 0;
}

bool rmCommandAllowed(const char *cmd, const char *args, int minTxPower, int maxTxPower)
{
    if (cmd == nullptr)
        return false;
    if (args == nullptr)
        args = "";
    RmCmd c;
    memset(&c, 0, sizeof(c));
    const size_t cl = strnlen(cmd, sizeof(c.cmd));
    const size_t al = strnlen(args, sizeof(c.args));
    if (cl == 0 || cl >= sizeof(c.cmd) || al >= sizeof(c.args))
        return false;
    memcpy(c.cmd, cmd, cl);
    memcpy(c.args, args, al);
    // wire form: lower case command name, printable ASCII args (capitals only where a shape allows them,
    // allowed() decides), single spaces (rmParse); anything else could never be received
    for (size_t k = 0; k < cl; k++)
        if (cmd[k] < 'a' || cmd[k] > 'z')
            return false;
    for (size_t k = 0; k < al; k++)
        if ((unsigned char)args[k] < 0x20 || (unsigned char)args[k] > 0x7e ||
            (args[k] == ' ' && (args[k + 1] == ' ' || args[k + 1] == '\0' || k == 0)))
            return false;
    return !isForbiddenText(c.cmd, sizeof(c.cmd)) && !isForbiddenText(c.args, sizeof(c.args)) && allowed(c, minTxPower, maxTxPower);
}

size_t rmBuildCommand(const char *dst, const char *src, uint32_t ctr, const char *cmd, const char *args,
                      const uint8_t key[32], char *out, size_t n)
{
    if (dst == nullptr || src == nullptr || cmd == nullptr || key == nullptr || out == nullptr || n == 0)
        return 0;
    if (args == nullptr)
        args = "";
    RmCmd c;
    memset(&c, 0, sizeof(c));
    const size_t cl = strnlen(cmd, sizeof(c.cmd));
    const size_t al = strnlen(args, sizeof(c.args));
    if (cl == 0 || cl >= sizeof(c.cmd) || al >= sizeof(c.args))
        return 0;
    c.ctr = ctr;
    memcpy(c.cmd, cmd, cl);
    memcpy(c.args, args, al);

    char canon[160]; // "RM1|<9>|<9>|<10>|<15>|<39>" <= 90
    const size_t canonLen = rmCanonical(c, dst, src, canon, sizeof(canon));
    if (canonLen == 0)
        return 0;
    char tag[17];
    tagOfKey(key, canon, canonLen, tag);

    Out o;
    outInit(o, out, n);
    outStr(o, "RM1 ");
    outU32(o, ctr);
    outStr(o, " ");
    outStrN(o, c.cmd, sizeof(c.cmd));
    if (al != 0)
    {
        outStr(o, " ");
        outStrN(o, c.args, sizeof(c.args));
    }
    outStr(o, " ");
    outStrN(o, tag, sizeof(tag));
    return outEnd(o);
}

bool rmVerifyReply(const char *text, const char *dst, const char *src, uint32_t ctr, const uint8_t key[32],
                   char *result, size_t n)
{
    if (text == nullptr || dst == nullptr || src == nullptr || key == nullptr || result == nullptr || n == 0)
        return false;
    if (!rmIsReply(text))
        return false;

    const size_t len = strnlen(text, 161);
    if (len < 4 + 1 + 1 + 3 + 1 + 16 || len > 160)
        return false;

    // "RM1 " <ctr> " " <result> " " <16 hex>
    if (!rmTagTokenValid(text, len))
        return false;
    const size_t tagPos = len - 16;

    size_t i = 4;
    uint32_t got = 0;
    if (!rmCtrToken(text, i, tagPos - 1, got, i) || text[i] != ' ' || got != ctr)
        return false;
    i++;

    const size_t resLen = (tagPos - 1) - i; // result runs up to the space before the tag
    if (tagPos - 1 <= i || resLen > RM_MAX_RESULT || resLen + 1 > n)
        return false;

    char res[RM_MAX_RESULT + 1];
    memcpy(res, text + i, resLen);
    res[resLen] = '\0';
    char canon[200];
    const size_t cl = rmReplyCanonical(ctr, dst, src, res, canon, sizeof(canon));
    if (cl == 0)
        return false;

    char want[17];
    tagOfKey(key, canon, cl, want);
    if (!tagEqual(want, text + tagPos))
        return false;

    memcpy(result, res, resLen + 1);
    return true;
}

size_t rmSanitizeResult(char *result)
{
    size_t n = 0;
    for (; result[n]; n++)
    {
        const unsigned char ch = (unsigned char)result[n];
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == ' ' || (ch != 0 && strchr("-./=+_@?(),*#", ch) != nullptr);
        if (!ok)
            result[n] = '?';
    }
    return n;
}

