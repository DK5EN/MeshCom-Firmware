// AppLink login core (docs/applink-api-contract.md sections 3, 6, 8).
// Header-only, Arduino-free, no heap. Needs src/hmac_sha256.cpp linked.
#ifndef APPLINK_AUTH_H
#define APPLINK_AUTH_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>    // snprintf (alHwLabel)
#include <string.h>
#include "hmac_sha256.h"

#define APPLINK_NONCE_LEN 16
#define APPLINK_MAC_LABEL "MCAPPLINK1"
#define APPLINK_MAC_LABEL_LEN 10
#define APPLINK_LOGIN_TIMEOUT_MS 10000
#define APPLINK_HB_INTERVAL_MS 15000
#define APPLINK_SILENCE_MS 45000
#define APPLINK_LOCK_FAILS 5
#define APPLINK_LOCK_WINDOW_MS 60000
#define APPLINK_LOCK_MS 60000

// key = password without trailing spaces (node_passwd is space padded); empty -> false
static inline bool alAuthExpectedMac(const char *password, const uint8_t nonce[APPLINK_NONCE_LEN], uint8_t out[32])
{
    if (!password || !nonce || !out)
        return false;
    size_t klen = strlen(password);
    while (klen > 0 && password[klen - 1] == ' ')
        klen--;
    if (klen == 0)
        return false;
    uint8_t msg[APPLINK_MAC_LABEL_LEN + APPLINK_NONCE_LEN];
    memcpy(msg, APPLINK_MAC_LABEL, APPLINK_MAC_LABEL_LEN);
    memcpy(msg + APPLINK_MAC_LABEL_LEN, nonce, APPLINK_NONCE_LEN);
    hmacSha256(reinterpret_cast<const uint8_t *>(password), klen, msg, sizeof(msg), out);
    return true;
}

// --- bounded JSON writer ------------------------------------------------------

struct AlW
{
    char *p;
    size_t cap;
    size_t n;
    bool bad;
};

static inline void alwCh(AlW *w, char c)
{
    if (w->n + 1 < w->cap)
        w->p[w->n] = c;
    else
        w->bad = true;
    w->n++;
}

static inline void alwStr(AlW *w, const char *s)
{
    while (s && *s)
        alwCh(w, *s++);
}

// JSON string body: escape " and \, drop control chars
static inline void alwEsc(AlW *w, const char *s)
{
    for (; s && *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f)
            continue;
        if (c == '"' || c == '\\')
            alwCh(w, '\\');
        alwCh(w, (char)c);
    }
}

static inline void alwUint(AlW *w, uint32_t v)
{
    char t[10];
    int i = 0;
    do
    {
        t[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        alwCh(w, t[--i]);
}

static inline size_t alwEnd(AlW *w)
{
    if (w->bad || w->cap == 0)
    {
        if (w->cap)
            w->p[0] = 0;
        return 0;
    }
    w->p[w->n] = 0;
    return w->n;
}

// "hw" in the challenge: the board name the contract shows ("HELTEC_V3"), taken from the firmware's own table
// (nbrHardwareName(), the caller passes it in). Where the table has no name ("no info", empty) the board number
// is sent instead (operator decision 2026-11-21). Truncates to out_size; returns out.
static inline const char *alHwLabel(const char *name, unsigned hw, char *out, size_t out_size)
{
    if (!out || !out_size)
        return out;
    if (name && name[0] && strcmp(name, "no info") != 0)
        snprintf(out, out_size, "%s", name);
    else
        snprintf(out, out_size, "%u", hw);
    return out;
}

static inline size_t alAuthChallengeJson(const uint8_t nonce[APPLINK_NONCE_LEN], const char *call, const char *fw,
                                         const char *hw, char *out, size_t out_size)
{
    if (!out || !out_size || !nonce)
        return 0;
    AlW w = {out, out_size, 0, false};
    char hex[2 * APPLINK_NONCE_LEN + 1];
    hexLower(nonce, APPLINK_NONCE_LEN, hex);
    alwStr(&w, "{\"t\":\"chal\",\"v\":1,\"nonce\":\"");
    alwStr(&w, hex);
    alwStr(&w, "\",\"call\":\"");
    alwEsc(&w, call);
    alwStr(&w, "\",\"fw\":\"");
    alwEsc(&w, fw);
    alwStr(&w, "\",\"hw\":\"");
    alwEsc(&w, hw);
    alwStr(&w, "\"}");
    return alwEnd(&w);
}

static inline size_t alAuthOkJson(uint16_t mtu, char *out, size_t out_size)
{
    if (!out || !out_size)
        return 0;
    AlW w = {out, out_size, 0, false};
    alwStr(&w, "{\"t\":\"ok\",\"mtu\":");
    alwUint(&w, mtu);
    alwCh(&w, '}');
    return alwEnd(&w);
}

static inline size_t alAuthErrJson(const char *code, char *out, size_t out_size)
{
    if (!out || !out_size)
        return 0;
    AlW w = {out, out_size, 0, false};
    alwStr(&w, "{\"t\":\"err\",\"code\":\"");
    alwEsc(&w, code);
    alwStr(&w, "\"}");
    return alwEnd(&w);
}

static inline size_t alHbJson(char *out, size_t out_size)
{
    if (!out || !out_size)
        return 0;
    AlW w = {out, out_size, 0, false};
    alwStr(&w, "{\"t\":\"hb\"}");
    return alwEnd(&w);
}

static inline size_t alByeJson(const char *reason, char *out, size_t out_size)
{
    if (!out || !out_size)
        return 0;
    AlW w = {out, out_size, 0, false};
    alwStr(&w, "{\"t\":\"bye\",\"reason\":\"");
    alwEsc(&w, reason);
    alwStr(&w, "\"}");
    return alwEnd(&w);
}

// --- control-frame scanner ----------------------------------------------------

enum AlCtl : uint8_t
{
    AL_CTL_BAD = 0,
    AL_CTL_UNKNOWN,
    AL_CTL_AUTH,
    AL_CTL_HB
};

static inline int alHexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static inline void alSkipWs(const char *j, size_t len, size_t *i)
{
    while (*i < len && (j[*i] == ' ' || j[*i] == '\t' || j[*i] == '\r' || j[*i] == '\n'))
        (*i)++;
}

// Scan a JSON string at j[*i] == '"'. Gives the raw span (escapes not decoded).
static inline bool alScanString(const char *j, size_t len, size_t *i, size_t *start, size_t *slen)
{
    if (*i >= len || j[*i] != '"')
        return false;
    size_t s = ++(*i);
    while (*i < len && j[*i] != '"')
    {
        if (j[*i] == '\\')
            (*i)++;
        (*i)++;
    }
    if (*i >= len)
        return false;
    *start = s;
    *slen = *i - s;
    (*i)++;
    return true;
}

static inline bool alSpanIs(const char *j, size_t start, size_t slen, const char *lit)
{
    size_t n = strlen(lit);
    return slen == n && memcmp(j + start, lit, n) == 0;
}

// Skip a non-string value (number, literal, nested object/array) to the next , or }
static inline bool alSkipValue(const char *j, size_t len, size_t *i)
{
    int depth = 0;
    while (*i < len)
    {
        char c = j[*i];
        if (c == '"')
        {
            size_t a, b;
            if (!alScanString(j, len, i, &a, &b))
                return false;
            continue;
        }
        if (c == '{' || c == '[')
            depth++;
        else if (c == '}' || c == ']')
        {
            if (depth == 0)
                return true;
            depth--;
        }
        else if (c == ',' && depth == 0)
            return true;
        (*i)++;
    }
    return false;
}

static inline AlCtl alCtlParse(const char *json, size_t len, uint8_t mac_out[32])
{
    if (!json || !len)
        return AL_CTL_BAD;
    size_t i = 0;
    alSkipWs(json, len, &i);
    if (i >= len || json[i] != '{')
        return AL_CTL_BAD;
    i++;

    bool have_t = false, t_auth = false, t_hb = false;
    bool have_mac = false;
    uint8_t mac[32];

    alSkipWs(json, len, &i);
    if (i < len && json[i] == '}')
        return AL_CTL_BAD;
    for (;;)
    {
        size_t ks, kl;
        alSkipWs(json, len, &i);
        if (!alScanString(json, len, &i, &ks, &kl))
            return AL_CTL_BAD;
        alSkipWs(json, len, &i);
        if (i >= len || json[i] != ':')
            return AL_CTL_BAD;
        i++;
        alSkipWs(json, len, &i);
        if (i >= len)
            return AL_CTL_BAD;

        bool is_t = alSpanIs(json, ks, kl, "t");
        bool is_mac = alSpanIs(json, ks, kl, "mac");
        if (json[i] == '"')
        {
            size_t vs, vl;
            if (!alScanString(json, len, &i, &vs, &vl))
                return AL_CTL_BAD;
            if (is_t)
            {
                have_t = true;
                t_auth = alSpanIs(json, vs, vl, "auth");
                t_hb = alSpanIs(json, vs, vl, "hb");
            }
            else if (is_mac)
            {
                have_mac = false;
                if (vl == 64)
                {
                    have_mac = true;
                    for (size_t k = 0; k < 32; k++)
                    {
                        int hi = alHexVal(json[vs + 2 * k]);
                        int lo = alHexVal(json[vs + 2 * k + 1]);
                        if (hi < 0 || lo < 0)
                        {
                            have_mac = false;
                            break;
                        }
                        mac[k] = (uint8_t)((hi << 4) | lo);
                    }
                }
            }
        }
        else
        {
            if (is_t)
            {
                have_t = true;
                t_auth = t_hb = false;
            }
            if (is_mac)
                have_mac = false;
            if (!alSkipValue(json, len, &i))
                return AL_CTL_BAD;
        }
        alSkipWs(json, len, &i);
        if (i >= len)
            return AL_CTL_BAD;
        if (json[i] == ',')
        {
            i++;
            continue;
        }
        if (json[i] == '}')
            break;
        return AL_CTL_BAD;
    }

    if (!have_t)
        return AL_CTL_BAD;
    if (t_hb)
        return AL_CTL_HB;
    if (t_auth)
    {
        if (!have_mac || !mac_out)
            return AL_CTL_BAD;
        memcpy(mac_out, mac, 32);
        return AL_CTL_AUTH;
    }
    return AL_CTL_UNKNOWN;
}

static inline bool alAuthCheck(const uint8_t mac[32], const char *password, const uint8_t nonce[APPLINK_NONCE_LEN])
{
    uint8_t want[32];
    if (!mac || !alAuthExpectedMac(password, nonce, want))
        return false;
    return ctEqual(want, mac, 32);
}

// --- brute-force lockout ------------------------------------------------------

struct AlLockout
{
    uint32_t fail_ms[APPLINK_LOCK_FAILS];
    uint8_t n;
    uint8_t locked;
    uint32_t locked_at;
};

static inline void alLockoutInit(AlLockout *l)
{
    memset(l, 0, sizeof(*l));
}

// Forget failures older than the window. A failure is only ever compared against the 32-bit clock,
// so a stale entry looks fresh again after 2^32 ms (49.7 days); pruning on every poll (alLockoutActive
// runs on each connection attempt) keeps entries from surviving that long.
static inline void alLockoutPrune(AlLockout *l, uint32_t now_ms)
{
    uint8_t k = 0;
    for (uint8_t i = 0; i < l->n; i++)
        if ((uint32_t)(now_ms - l->fail_ms[i]) < APPLINK_LOCK_WINDOW_MS)
            l->fail_ms[k++] = l->fail_ms[i];
    l->n = k;
}

static inline bool alLockoutActive(AlLockout *l, uint32_t now_ms)
{
    if (!l->locked)
    {
        alLockoutPrune(l, now_ms);
        return false;
    }
    if ((uint32_t)(now_ms - l->locked_at) >= APPLINK_LOCK_MS)
    {
        l->locked = 0;
        l->n = 0;
        return false;
    }
    return true;
}

static inline uint32_t alLockoutRemainingMs(AlLockout *l, uint32_t now_ms)
{
    if (!alLockoutActive(l, now_ms))
        return 0;
    return APPLINK_LOCK_MS - (uint32_t)(now_ms - l->locked_at);
}

static inline void alLockoutFail(AlLockout *l, uint32_t now_ms)
{
    if (alLockoutActive(l, now_ms))
        return;
    alLockoutPrune(l, now_ms);
    l->fail_ms[l->n++] = now_ms;
    if (l->n >= APPLINK_LOCK_FAILS)
    {
        l->locked = 1;
        l->locked_at = now_ms;
    }
}

static inline void alLockoutSuccess(AlLockout *l)
{
    l->n = 0;
    l->locked = 0;
}

#endif // APPLINK_AUTH_H
