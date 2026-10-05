// RM-02: protocol core of the authenticated remote management DM ("RM1").
// See remote_cmd.h and docs/concept-open-issues-20261004.md section 6.

#include "remote_cmd.h"

#include <string.h>

#include "hmac_sha256.h"

namespace
{

// ---- small bounded text builder (no printf, no malloc) --------------------

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
    while (*s)
    {
        if (o.len + 1 >= o.n)
        {
            o.ok = false;
            return;
        }
        o.p[o.len++] = *s++;
    }
}

void outU32(Out &o, uint32_t v)
{
    char tmp[11];
    unsigned i = sizeof(tmp);
    tmp[--i] = '\0';
    do
    {
        tmp[--i] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0);
    outStr(o, tmp + i);
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
    size_t n = strlen(passwd);
    while (n > 0 && passwd[n - 1] == ' ')
        n--;
    return n;
}

bool isLowerHex(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

bool isForbiddenText(const char *s)
{
    for (; *s; s++)
    {
        if (*s == ';' || *s == '{' || *s == '%')
            return true;
        if (*s == '-' && s[1] == '-')
            return true;
    }
    return false;
}

// decimal digits only, no leading zero unless the whole token is "0", at most maxDigits
bool parseSmallInt(const char *s, unsigned maxDigits, int &out)
{
    size_t n = strlen(s);
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
    return strcmp(a, "on") == 0 || strcmp(a, "off") == 0;
}

// allowlist of section 6.4, args included; maxTxPower bounds txpower
bool allowed(const RmCmd &c, int maxTxPower)
{
    const char *cmd = c.cmd;
    const char *a = c.args;

    if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "status") == 0 || strcmp(cmd, "sendpos") == 0 ||
        strcmp(cmd, "sendtrack") == 0 || strcmp(cmd, "sync") == 0)
        return a[0] == '\0';

    if (strcmp(cmd, "gps") == 0 || strcmp(cmd, "track") == 0 || strcmp(cmd, "display") == 0 ||
        strcmp(cmd, "gateway") == 0 || strcmp(cmd, "mesh") == 0)
        return isOnOff(a);

    if (strcmp(cmd, "txpower") == 0)
    {
        int v = 0;
        return parseSmallInt(a, 3, v) && v >= 0 && v <= maxTxPower;
    }

    if (strcmp(cmd, "setout") == 0)
    {
        // "<pin> <on|off>", pin a0..a7 or b0..b7 (the console's --setout MCP pins)
        return strlen(a) >= 4 && (a[0] == 'a' || a[0] == 'b') && a[1] >= '0' && a[1] <= '7' && a[2] == ' ' &&
               isOnOff(a + 3);
    }

    return false;
}

// computes the 16-hex tag of text under the key derived from passwd
void tagOf(const char *passwd, const char *text, char tagHex[17])
{
    uint8_t key[32];
    uint8_t mac[32];
    rmDeriveKey(passwd, key);
    hmacSha256(key, sizeof(key), reinterpret_cast<const uint8_t *>(text), strlen(text), mac);
    hexLower(mac, 8, tagHex);
    hmac_sha256_detail::wipe(key, sizeof(key));
    hmac_sha256_detail::wipe(mac, sizeof(mac));
}

// same, with the key already derived (sender side)
void tagOfKey(const uint8_t key[32], const char *text, char tagHex[17])
{
    uint8_t mac[32];
    hmacSha256(key, 32, reinterpret_cast<const uint8_t *>(text), strlen(text), mac);
    hexLower(mac, 8, tagHex);
    hmac_sha256_detail::wipe(mac, sizeof(mac));
}

bool tagEqual(const char *a, const char *b)
{
    return ctEqual(reinterpret_cast<const uint8_t *>(a), reinterpret_cast<const uint8_t *>(b), 16);
}

RmVerdict reject(RmState &s, RmVerdict v, uint32_t now)
{
    if (s.rejCount == 0 || (uint32_t)(now - s.rejWindowMs) > RM_REJ_WINDOW_MS)
    {
        s.rejCount = 1;
        s.rejWindowMs = now;
    }
    else if (s.rejCount < 255)
        s.rejCount++;

    if (s.rejCount >= RM_REJ_LIMIT)
    {
        s.lockActive = true;
        s.lockUntilMs = now + RM_LOCKOUT_MS;
        s.rejCount = 0;
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
    }
    return "?";
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

    for (size_t i = 3; i < len; i++) // "RM1" itself is upper case
    {
        const unsigned char ch = (unsigned char)text[i];
        if (ch < 0x20 || ch > 0x7e || (ch >= 'A' && ch <= 'Z'))
            return false;
    }
    if (text[len - 1] == ' ')
        return false;

    // tag: last token, exactly 16 lower-case hex preceded by a single space
    const size_t tagPos = len - 16;
    if (text[tagPos - 1] != ' ')
        return false;
    for (size_t i = tagPos; i < len; i++)
        if (!isLowerHex(text[i]))
            return false;

    // ctr
    size_t i = 4;
    const size_t ctrStart = i;
    uint64_t ctr = 0;
    while (i < tagPos - 1 && text[i] >= '0' && text[i] <= '9')
    {
        ctr = ctr * 10 + (uint64_t)(text[i] - '0');
        if (ctr > 0xFFFFFFFFULL)
            return false;
        i++;
    }
    const size_t ctrLen = i - ctrStart;
    if (ctrLen == 0 || (ctrLen > 1 && text[ctrStart] == '0'))
        return false;
    if (i >= tagPos - 1 || text[i] != ' ')
        return false;
    i++;

    // body between ctr and tag: "<cmd>[ <args>]", single spaces
    const size_t bodyEnd = tagPos - 1; // index of the space before the tag
    if (i >= bodyEnd || text[i] == ' ')
        return false;
    size_t cl = 0;
    while (i < bodyEnd && text[i] != ' ')
    {
        if (cl + 1 >= sizeof(out.cmd))
            return false;
        out.cmd[cl++] = text[i++];
    }
    if (i < bodyEnd)
    {
        i++; // the single space before args
        if (i >= bodyEnd || text[i] == ' ')
            return false;
        size_t al = 0;
        while (i < bodyEnd)
        {
            if (al + 1 >= sizeof(out.args))
                return false;
            if (text[i] == ' ' && text[i + 1] == ' ')
                return false;
            out.args[al++] = text[i++];
        }
    }

    out.ctr = (uint32_t)ctr;
    if (out.ctr == 0 && strcmp(out.cmd, "sync") != 0)
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

size_t rmCanonical(const RmCmd &c, const char *dst, const char *src, char *out, size_t n)
{
    Out o;
    outInit(o, out, n);
    outStr(o, "RM1|");
    outStr(o, dst);
    outStr(o, "|");
    outStr(o, src);
    outStr(o, "|");
    outU32(o, c.ctr);
    outStr(o, "|");
    outStr(o, c.cmd);
    if (c.args[0] != '\0')
    {
        outStr(o, " ");
        outStr(o, c.args);
    }
    return outEnd(o);
}

void rmStateInit(RmState &s, uint32_t hwm)
{
    memset(&s, 0, sizeof(s));
    s.hwm = hwm;
}

RmVerdict rmCheck(RmState &s, const RmCmd &c, const char *dst, const char *src, const char *passwd,
                  int maxTxPower, uint32_t nowMs)
{
    if (strippedLen(passwd) == 0)
        return RM_REJ_DISABLED;

    if (s.lockActive)
    {
        if ((int32_t)(nowMs - s.lockUntilMs) < 0)
            return RM_REJ_LOCKOUT;
        s.lockActive = false;
        s.rejCount = 0;
    }

    if (dst == nullptr || src == nullptr || c.tag[16] != '\0' || c.cmd[sizeof(c.cmd) - 1] != '\0' ||
        c.args[sizeof(c.args) - 1] != '\0')
        return reject(s, RM_REJ_FORMAT, nowMs);

    const bool isSync = strcmp(c.cmd, "sync") == 0;
    if ((c.ctr == 0) != isSync)
        return reject(s, RM_REJ_FORMAT, nowMs);

    if (isForbiddenText(c.cmd) || isForbiddenText(c.args) || !allowed(c, maxTxPower))
        return reject(s, RM_REJ_BLOCKED, nowMs);

    char canon[160];
    if (rmCanonical(c, dst, src, canon, sizeof(canon)) == 0)
        return reject(s, RM_REJ_FORMAT, nowMs);
    char want[17];
    tagOf(passwd, canon, want);
    if (!tagEqual(want, c.tag))
        return reject(s, RM_REJ_TAG, nowMs);

    if (!isSync)
    {
        if (s.haveLast && c.ctr == s.lastCtr && tagEqual(s.lastTag, c.tag) &&
            (uint32_t)(nowMs - s.lastAcceptMs) <= RM_CACHE_MS)
            return RM_CACHED;
        if (c.ctr <= s.hwm)
            return reject(s, RM_REJ_REPLAY, nowMs);
    }

    // Only an authenticated sender reaches this point; a rate reject is not an
    // attack signal and does not count towards the lockout (advisor RM W1 N1).
    if (s.haveRate && (uint32_t)(nowMs - s.lastRateMs) < RM_RATE_MS)
        return RM_REJ_RATE;

    if (isSync)
    {
        s.lastRateMs = nowMs;
        s.haveRate = true;
        return RM_SYNC;
    }
    return RM_OK;
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
        n = strlen(result);
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
        strlen(result) > RM_MAX_RESULT)
        return 0;

    char canon[200];
    Out o;
    outInit(o, canon, sizeof(canon));
    outStr(o, "RM1R|");
    outStr(o, dst);
    outStr(o, "|");
    outStr(o, src);
    outStr(o, "|");
    outU32(o, c.ctr);
    outStr(o, "|");
    outStr(o, result);
    if (outEnd(o) == 0)
        return 0;

    char rtag[17];
    tagOf(passwd, canon, rtag);

    Out r;
    outInit(r, out, n);
    outStr(r, "RM1 ");
    outU32(r, c.ctr);
    outStr(r, " ");
    outStr(r, result);
    outStr(r, " ");
    outStr(r, rtag);
    return outEnd(r);
}

bool rmIsReply(const char *text)
{
    if (text == nullptr || strncmp(text, "RM1 ", 4) != 0)
        return false;
    const char *p = text + 4;
    while (*p >= '0' && *p <= '9')
        p++;
    if (p == text + 4 || *p != ' ')
        return false;
    return strncmp(p + 1, "ok ", 3) == 0 || strncmp(p + 1, "err ", 4) == 0;
}

bool rmCommandAllowed(const char *cmd, const char *args, int maxTxPower)
{
    if (cmd == nullptr)
        return false;
    if (args == nullptr)
        args = "";
    RmCmd c;
    memset(&c, 0, sizeof(c));
    if (strlen(cmd) == 0 || strlen(cmd) >= sizeof(c.cmd) || strlen(args) >= sizeof(c.args))
        return false;
    memcpy(c.cmd, cmd, strlen(cmd));
    memcpy(c.args, args, strlen(args));
    // wire form is lower case ASCII, single spaces (rmParse); anything else could never be received
    for (const char *p = cmd; *p; p++)
        if (*p < 'a' || *p > 'z')
            return false;
    for (const char *p = args; *p; p++)
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e || (*p >= 'A' && *p <= 'Z') ||
            (*p == ' ' && (p[1] == ' ' || p[1] == '\0' || p == args)))
            return false;
    return !isForbiddenText(c.cmd) && !isForbiddenText(c.args) && allowed(c, maxTxPower);
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
    if (strlen(cmd) == 0 || strlen(cmd) >= sizeof(c.cmd) || strlen(args) >= sizeof(c.args))
        return 0;
    c.ctr = ctr;
    memcpy(c.cmd, cmd, strlen(cmd));
    memcpy(c.args, args, strlen(args));

    char canon[160];
    if (rmCanonical(c, dst, src, canon, sizeof(canon)) == 0)
        return 0;
    char tag[17];
    tagOfKey(key, canon, tag);

    Out o;
    outInit(o, out, n);
    outStr(o, "RM1 ");
    outU32(o, ctr);
    outStr(o, " ");
    outStr(o, cmd);
    if (args[0] != '\0')
    {
        outStr(o, " ");
        outStr(o, args);
    }
    outStr(o, " ");
    outStr(o, tag);
    return outEnd(o);
}

bool rmVerifyReply(const char *text, const char *dst, const char *src, uint32_t ctr, const uint8_t key[32],
                   char *result, size_t n)
{
    if (text == nullptr || dst == nullptr || src == nullptr || key == nullptr || result == nullptr || n == 0)
        return false;
    if (!rmIsReply(text))
        return false;

    const size_t len = strlen(text);
    if (len < 4 + 1 + 1 + 3 + 1 + 16 || len > 160)
        return false;

    // "RM1 " <ctr> " " <result> " " <16 hex>
    const size_t tagPos = len - 16;
    if (text[tagPos - 1] != ' ')
        return false;
    for (size_t i = tagPos; i < len; i++)
        if (!isLowerHex(text[i]))
            return false;

    size_t i = 4;
    uint64_t got = 0;
    const size_t ctrStart = i;
    while (text[i] >= '0' && text[i] <= '9')
    {
        got = got * 10 + (uint64_t)(text[i] - '0');
        if (got > 0xFFFFFFFFULL)
            return false;
        i++;
    }
    const size_t ctrLen = i - ctrStart;
    if (ctrLen == 0 || (ctrLen > 1 && text[ctrStart] == '0') || text[i] != ' ' || (uint32_t)got != ctr)
        return false;
    i++;

    const size_t resLen = (tagPos - 1) - i; // result runs up to the space before the tag
    if (tagPos - 1 <= i || resLen > RM_MAX_RESULT || resLen + 1 > n)
        return false;

    char canon[200];
    Out o;
    outInit(o, canon, sizeof(canon));
    outStr(o, "RM1R|");
    outStr(o, dst);
    outStr(o, "|");
    outStr(o, src);
    outStr(o, "|");
    outU32(o, ctr);
    outStr(o, "|");
    char res[RM_MAX_RESULT + 1];
    memcpy(res, text + i, resLen);
    res[resLen] = '\0';
    outStr(o, res);
    if (outEnd(o) == 0)
        return false;

    char want[17];
    tagOfKey(key, canon, want);
    if (!tagEqual(want, text + tagPos))
        return false;

    memcpy(result, res, resLen + 1);
    return true;
}
