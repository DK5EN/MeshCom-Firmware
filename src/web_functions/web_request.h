// web_request.h -- the pure HTTP request-head parser of the node web server (FORMAL-02 wave 6).
// Header-only, no String, no Arduino: compiles natively and on ESP32 / nRF52.
// Design: docs/web-server-seams-20261127.md section 6.2; proof targets 6.8 (ESBMC, a later wave).
//
// Two parts:
//   1. WebHeadAcc / webHeadPush: the byte-wise head accumulator that replaces `web_header`,
//      `web_currentLine`, `web_cookie[256]`, `web_content_length` and the per-line `hdr_name` copy of
//      work_webpage() (web_functions.cpp, head loop :1079-1530, byte read :1086, window :1088-1089,
//      Cookie quiet flag :1099, line end :1478-1511, line too big :1514).
//   2. webReqParse: the split of the finished window into request line and the header facts
//      web_guard.h computes (request line: web_guard.h:602-623, header loop :633-722).
//
// Proof discipline: every loop has a constant bound (WEB_HEAD_MAX), every read has an explicit length,
// no allocation. WEB_HEAD_MAX / WEB_COOKIE_MAX may be lowered by -D for a bounded proof.
//
// TODAY'S BEHAVIOUR REPRODUCED (checked byte for byte against a reference copy in
// test/test_web_request):
//   - window: every byte is appended while head_len < WEB_HEAD_MAX, the rest is dropped (`cut` flag).
//   - lines: CR is never part of a line, LF ends it; an empty line at LF ends the head (WH_DONE); a
//     non-CR/LF byte arriving at line_len >= WEB_HEAD_MAX gives WH_LINE_TOO_BIG (today: 431).
//   - Cookie: a finished line whose first 7 bytes are "cookie:" (any case; a NUL among them counts as a
//     match, the String::equalsIgnoreCase loop ends there) is searched for "mcs=" from index 7 on, the first occurrence whose preceding byte is ':', ' ' or ';' wins; the pair is
//     copied up to ';' or WEB_COOKIE_MAX bytes. A later Cookie line without a match keeps the old
//     value, one with a match replaces it. The search stops at the first NUL byte at or after index 7
//     (the String::indexOf of the Arduino core is a strstr from there); the copy does not.
//   - Content-Length (lenient): a finished line starting with "content-length:" (any case) is read
//     with atol() semantics (skip isspace, optional sign, digits, saturating) from byte 15 on, only
//     while the value so far is < 0 (so a first value of 0 or more wins, a negative one is replaced).
//   - request line / header facts: the same split and the same line rules as web_guard.h.
//
// INTENTIONAL DEVIATIONS FROM TODAY (all invisible to a caller that behaves as today):
//   D-1. (withdrawn) The lenient Content-Length is a `long` with exact atol()/strtol() semantics, negative
//        values and saturation included: the value is traced (handler stubs of the request rig, and
//        sub_config_upload prints it on Serial, web_functions.cpp:905), so -5 must stay -5. Only the
//        strict value (B6) is int32 with -1 for absent or malformed.
//   D-2. Every finished line is wiped from `line` (today: only Cookie and /rmsend|login lines).
//   D-3. After WH_DONE or WH_LINE_TOO_BIG the accumulator is sticky: further pushes change nothing and
//        repeat the result. WH_LINE_TOO_BIG wipes line and head, as the 431 branch does today.
//   D-4. New judgements, computed but acting on nothing yet (B3 / B6 of section 7.2):
//        webHeadBadContentLength(), WRQ_BAD_VERSION, WRQ_BAD_CONTENT_LENGTH, WRQ_BAD_REQUEST_LINE.
//        webReqParse fills every output field it can even when it returns an error code.
//        The strict Content-Length is judged twice on purpose: the accumulator (cl_strict, WEB_HF_CL_BAD)
//        sees every finished line of the stream, also past the 1023-byte window, as the head loop does;
//        webReqParse sees only the window. B6 acts on the accumulator's judgement.
//   D-5. webReqParse clamps `len` to WEB_HEAD_MAX (the window never is longer) and reports `host` and
//        `origin` as the FIRST value (web_guard.h keeps the last). That is unobservable only as long as
//        the guard refuses host_n > 1 (WG_BAD_HOST) and origin_n > 1 (WG_CROSS_SITE) BEFORE it reads
//        the value, and a bad Sec-Fetch-Site on an exempt page returns WG_OK before Origin is read.
//        Step 10 of the seam plan must keep exactly that order.
//   D-6. NUL bytes follow the ESP32 Arduino String (memcpy-based concat: a NUL byte is stored and the
//        buffer stays terminated). The nRF52 core's concat() is strcpy-based: after a NUL byte it leaves
//        buffer[len] unset, and its String copies and substring() stop at the first NUL, so bytes after
//        a NUL can differ there (no read past the buffer: every C-string scan stops at the stored NUL).
//        The request corpus proves ESP32 semantics only; "no behaviour change" is an ESP32 statement.
//        This parser stores explicit lengths and has none of the nRF52 effects.
//
// WebReq flag fields: blank 1 when the header loop reached the blank line that ends the head (0 when the
// window ended first, was cut mid-line, or the request line was never terminated). host_n/origin_n/xmc_n
// are counts (saturating at 255), xmc_bad 1 when an X-MC value is not exactly "1" or an X-MC line is
// folded, sf_site 1 when a Sec-Fetch-Site value is neither same-origin nor none (or folded), sf_dest 0
// absent / 1 document / 2 empty / 3 other, folded is a bit set (1 Host line folded, 2 Origin line folded).
#ifndef WEB_REQUEST_H
#define WEB_REQUEST_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "web_guard.h" // webguard_detail::{isWs, lc, trim, ieq}: reused, not copied

#ifndef WEB_HEAD_MAX
#define WEB_HEAD_MAX 1023 // = WEB_HEADER_MAX in web_functions.cpp = WEB_GUARD_HEADER_MAX
#endif
#ifndef WEB_COOKIE_MAX
#define WEB_COOKIE_MAX 255 // sizeof(web_cookie) - 1
#endif
#ifndef WEB_CL_MAX
#define WEB_CL_MAX 65535 // strict Content-Length cap (B6); the body reader applies its own, lower cap
#endif

struct WebSpan
{
    const char *p;
    uint16_t n;
}; // never owns, never NUL terminated

enum WebMethod : uint8_t
{
    WM_NONE = 0,
    WM_GET,
    WM_POST,
    WM_HEAD,
    WM_OPTIONS,
    WM_OTHER
};
enum WebHeadRes : uint8_t
{
    WH_MORE = 0,
    WH_DONE,
    WH_LINE_TOO_BIG
};
enum WebParseRes : uint8_t
{
    WRQ_OK = 0,
    WRQ_BAD_REQUEST_LINE,
    WRQ_BAD_VERSION,
    WRQ_BAD_CONTENT_LENGTH
};

// WebHeadAcc::flags
#define WEB_HF_CUT 0x01u    // a byte was dropped from the window
#define WEB_HF_QUIET 0x02u  // the current line is a Cookie line (today web_line_quiet)
#define WEB_HF_DONE 0x04u   // blank line seen
#define WEB_HF_BIG 0x08u    // a line exceeded WEB_HEAD_MAX
#define WEB_HF_CL_BAD 0x10u // strict Content-Length judgement: malformed, over-large or differing

struct WebHeadAcc
{
    char head[WEB_HEAD_MAX + 1];     // the window (today web_header), NUL terminated
    char line[WEB_HEAD_MAX + 1];     // the line in progress (today web_currentLine), NUL terminated
    char cookie[WEB_COOKIE_MAX + 1]; // captured "mcs=..." pair, NUL terminated
    uint16_t head_len, line_len, cookie_len;
    long content_length;    // lenient value as today (atol, any negative value possible), -1 absent
    int32_t cl_strict;      // strict value of the first Content-Length line, -1 none
    uint8_t flags;
}; // about 2.4 kB, allocated once at server start (heap), never on the stack

struct WebReq
{
    uint8_t method, minor;
    WebSpan target, path, query;
    WebSpan host, origin;
    uint8_t host_n, origin_n, xmc_n, xmc_bad, sf_site, sf_dest, folded;
    uint8_t blank; // 1 when the blank line that ends the head was seen
    int32_t content_length; // strict value of the first Content-Length line, -1 absent
    bool cut;
};

namespace webreq_detail
{

inline bool isSpaceC(char c) // isspace() of the C locale, what atol() skips
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// atol() as the Arduino cores call it (strtol): leading isspace, optional sign, digits; on overflow it
// saturates at LONG_MIN / LONG_MAX (32 bit on ESP32/nRF52, 64 bit on the native host, as the frozen copy).
// Reads at most n bytes and stops at the first NUL (the String buffer is a C string). The overflow test
// compares with constants only (no division by a variable), so the bounded proof stays linear.
inline long atolSat(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && i < (size_t)WEB_HEAD_MAX && isSpaceC(s[i]))
        i++;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-'))
    {
        neg = (s[i] == '-');
        i++;
    }
    const unsigned long lim = neg ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;
    const unsigned long lim10 = lim / 10UL, limd = lim % 10UL;
    unsigned long v = 0;
    bool over = false;
    for (size_t k = 0; i < n && k < (size_t)WEB_HEAD_MAX; i++, k++)
    {
        if (s[i] < '0' || s[i] > '9')
            break;
        const unsigned long d = (unsigned long)(s[i] - '0');
        if (over || v > lim10 || (v == lim10 && d > limd))
            over = true;
        else
            v = v * 10UL + d;
    }
    if (over)
        return neg ? LONG_MIN : LONG_MAX;
    if (neg)
        return v == lim ? LONG_MIN : -(long)v;
    return (long)v;
}

// Strict Content-Length value: trimmed, digits only, not empty, <= WEB_CL_MAX. -1 when malformed.
inline int32_t strictCl(const char *s, size_t n)
{
    webguard_detail::Span v = webguard_detail::trim(webguard_detail::mk(s, n));
    if (v.n == 0)
        return -1;
    int32_t val = 0;
    for (size_t i = 0; i < v.n && i < (size_t)WEB_HEAD_MAX; i++)
    {
        if (v.p[i] < '0' || v.p[i] > '9')
            return -1;
        val = val * 10 + (v.p[i] - '0');
        if (val > WEB_CL_MAX)
            return -1;
    }
    return val;
}

// String::equalsIgnoreCase("cookie:") of a 7 byte line (both cores, WString.cpp): after the length check
// the loop is `while (*p1)` over the line, so it ends at the first NUL of the line and a NUL inside the
// 7 bytes counts as a match ("\0AAAAAA", "c\0xxxxx", "cookie\0" all start a Cookie line today).
inline bool startsCookie(const char *l)
{
    static const char lit[] = "cookie:";
    for (size_t i = 0; i < 7; i++)
    {
        if (l[i] == 0)
            return true;
        if (webguard_detail::lc(l[i]) != lit[i])
            return false;
    }
    return true;
}

inline bool startsContentLength(const char *l, size_t n)
{
    static const char lit[] = "content-length:";
    if (n < 15)
        return false;
    for (size_t i = 0; i < 15; i++)
        if (webguard_detail::lc(l[i]) != lit[i])
            return false;
    return true;
}

// a finished, non-empty line: Content-Length and Cookie capture
inline void lineDone(WebHeadAcc *a)
{
    const size_t n = a->line_len;
    if (startsContentLength(a->line, n))
    {
        if (a->content_length < 0)
            a->content_length = atolSat(a->line + 15, n - 15);
        const int32_t sv = strictCl(a->line + 15, n - 15);
        if (sv < 0)
            a->flags |= WEB_HF_CL_BAD;
        else if (a->cl_strict < 0)
            a->cl_strict = sv;
        else if (a->cl_strict != sv)
            a->flags |= WEB_HF_CL_BAD;
    }
    if (a->flags & WEB_HF_QUIET)
    {
        a->flags &= (uint8_t)~WEB_HF_QUIET;
        // indexOf("mcs=", from >= 7) of the Arduino String is a strstr(buffer + from): it ends at the first
        // NUL AT OR AFTER index 7 (a NUL inside the first 7 bytes does not stop it); the line has >= 7 bytes
        size_t eff = 7;
        while (eff < n && a->line[eff] != 0)
            eff++;
        size_t k = 7;
        bool found = false;
        for (; k + 4 <= eff && k < (size_t)WEB_HEAD_MAX; k++)
        {
            if (a->line[k] == 'm' && a->line[k + 1] == 'c' && a->line[k + 2] == 's' && a->line[k + 3] == '=' &&
                (a->line[k - 1] == ':' || a->line[k - 1] == ' ' || a->line[k - 1] == ';'))
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            a->cookie_len = 0;
            for (; k < n && a->line[k] != ';' && a->cookie_len < WEB_COOKIE_MAX; k++)
                a->cookie[a->cookie_len++] = a->line[k];
            a->cookie[a->cookie_len] = 0;
        }
    }
    for (size_t i = 0; i <= n && i <= (size_t)WEB_HEAD_MAX; i++)
        a->line[i] = 0; // D-2
    a->line_len = 0;
}

} // namespace webreq_detail

inline void webHeadInit(WebHeadAcc *a)
{
    memset(a, 0, sizeof(*a));
    a->content_length = -1;
    a->cl_strict = -1;
}

inline WebHeadRes webHeadPush(WebHeadAcc *a, char c)
{
    using namespace webreq_detail;
    if (a->flags & WEB_HF_BIG)
        return WH_LINE_TOO_BIG; // D-3
    if (a->flags & WEB_HF_DONE)
        return WH_DONE;

    if (a->head_len < WEB_HEAD_MAX)
    {
        a->head[a->head_len++] = c;
        a->head[a->head_len] = 0;
    }
    else
        a->flags |= WEB_HF_CUT;

    if (a->line_len == 7 && startsCookie(a->line))
        a->flags |= WEB_HF_QUIET;

    if (c == '\n')
    {
        if (a->line_len == 0)
        {
            a->flags |= WEB_HF_DONE;
            return WH_DONE;
        }
        lineDone(a);
    }
    else if (c != '\r')
    {
        if (a->line_len >= WEB_HEAD_MAX)
        {
            for (size_t i = 0; i <= (size_t)WEB_HEAD_MAX; i++)
            {
                a->line[i] = 0;
                a->head[i] = 0;
            }
            a->line_len = 0;
            a->head_len = 0;
            a->flags |= WEB_HF_BIG;
            return WH_LINE_TOO_BIG;
        }
        a->line[a->line_len++] = c;
        a->line[a->line_len] = 0;
    }
    return WH_MORE;
}

inline WebSpan webHeadCookie(const WebHeadAcc *a)
{
    WebSpan s;
    s.p = a->cookie;
    s.n = a->cookie_len;
    return s;
}

inline long webHeadContentLength(const WebHeadAcc *a) { return a->content_length; }

inline bool webHeadBadContentLength(const WebHeadAcc *a) { return (a->flags & WEB_HF_CL_BAD) != 0; }

namespace webreq_detail
{
inline WebSpan toSpan(webguard_detail::Span s)
{
    WebSpan r;
    r.p = s.p;
    r.n = (uint16_t)s.n;
    return r;
}
inline uint8_t inc(uint8_t n) { return n < 255 ? (uint8_t)(n + 1) : n; }
} // namespace webreq_detail

inline WebParseRes webReqParse(const char *buf, size_t len, bool cut, WebReq *out)
{
    using namespace webguard_detail;
    using namespace webreq_detail;

    memset(out, 0, sizeof(*out));
    out->content_length = -1;
    out->cut = cut;
    out->method = WM_NONE;
    WebSpan nil;
    nil.p = buf;
    nil.n = 0;
    out->target = out->path = out->query = out->host = out->origin = nil;
    if (buf == NULL)
        return WRQ_BAD_REQUEST_LINE;
    if (len > (size_t)WEB_HEAD_MAX)
        len = WEB_HEAD_MAX; // D-5

    // ---- request line ----
    size_t rlEnd = 0;
    while (rlEnd < len && buf[rlEnd] != '\n')
        rlEnd++;
    const bool rlTerminated = rlEnd < len;
    Span rl = mk(buf, rlEnd);
    if (rl.n > 0 && rl.p[rl.n - 1] == '\r')
        rl.n--;

    size_t sp1 = rl.n, sp2 = rl.n;
    for (size_t i = 0; i < rl.n; i++)
        if (rl.p[i] == ' ')
        {
            sp1 = i;
            break;
        }
    for (size_t i = rl.n; i > 0; i--)
        if (rl.p[i - 1] == ' ')
        {
            sp2 = i - 1;
            break;
        }
    Span method = mk(rl.p, sp1);
    Span target = mk(rl.p, 0);
    Span version = mk(rl.p, 0);
    if (sp1 < rl.n && sp2 > sp1)
    {
        target = mk(rl.p + sp1 + 1, sp2 - sp1 - 1);
        version = mk(rl.p + sp2 + 1, rl.n - sp2 - 1);
    }
    if (method.n == 0)
        out->method = WM_NONE;
    else if (method.n == 3 && memcmp(method.p, "GET", 3) == 0)
        out->method = WM_GET;
    else if (method.n == 4 && memcmp(method.p, "POST", 4) == 0)
        out->method = WM_POST;
    else if (method.n == 4 && memcmp(method.p, "HEAD", 4) == 0)
        out->method = WM_HEAD;
    else if (method.n == 7 && memcmp(method.p, "OPTIONS", 7) == 0)
        out->method = WM_OPTIONS;
    else
        out->method = WM_OTHER;

    out->target = toSpan(target);
    size_t q = target.n;
    for (size_t i = 0; i < target.n; i++)
        if (target.p[i] == '?')
        {
            q = i;
            break;
        }
    out->path = toSpan(mk(target.p, q));
    out->query = toSpan(q < target.n ? mk(target.p + q + 1, target.n - q - 1) : mk(target.p + target.n, 0));

    WebParseRes res = WRQ_OK;
    if (!rlTerminated || sp1 >= rl.n || method.n == 0)
        res = WRQ_BAD_REQUEST_LINE;
    else if (sp2 == sp1)
        res = WRQ_BAD_VERSION; // "METHOD target" without a version
    else if (target.n == 0)
        res = WRQ_BAD_REQUEST_LINE;
    else if (version.n == 8 && memcmp(version.p, "HTTP/1.0", 8) == 0)
        out->minor = 0;
    else if (version.n == 8 && memcmp(version.p, "HTTP/1.1", 8) == 0)
        out->minor = 1;
    else
        res = WRQ_BAD_VERSION;

    // ---- header lines ----
    int last = 0; // 0 other, 1 host, 2 sec-fetch-site, 3 dest, 4 origin, 5 x-mc
    bool clBad = false;
    size_t pos = rlTerminated ? rlEnd + 1 : len;
    for (size_t guardLines = 0; pos < len && guardLines < (size_t)WEB_HEAD_MAX; guardLines++)
    {
        size_t e = pos;
        while (e < len && buf[e] != '\n')
            e++;
        const bool terminated = e < len;
        if (!terminated && len >= (size_t)WEB_HEAD_MAX)
            break; // cut mid-line by the window: judge nothing from it
        size_t lineEnd = e;
        if (lineEnd > pos && buf[lineEnd - 1] == '\r')
            lineEnd--;
        Span line = mk(buf + pos, lineEnd - pos);
        pos = terminated ? e + 1 : len;

        if (line.n == 0)
        {
            out->blank = 1;
            break; // blank line: end of the head
        }
        if (isWs(line.p[0]))
        { // obs-fold: continuation of the previous header
            switch (last)
            {
            case 1:
                out->folded |= 1;
                break;
            case 2:
                out->sf_site = 1;
                break;
            case 3:
                out->sf_dest = 3;
                break;
            case 4:
                out->folded |= 2;
                break;
            case 5:
                out->xmc_bad = 1;
                break;
            default:
                break;
            }
            continue;
        }
        size_t colon = line.n;
        for (size_t i = 0; i < line.n; i++)
            if (line.p[i] == ':')
            {
                colon = i;
                break;
            }
        if (colon == line.n)
        {
            last = 0;
            continue;
        }
        Span name = mk(line.p, colon);
        Span val = trim(mk(line.p + colon + 1, line.n - colon - 1));
        last = 0;
        if (ieq(name, "host"))
        {
            if (out->host_n == 0)
                out->host = toSpan(val);
            out->host_n = inc(out->host_n);
            last = 1;
        }
        else if (ieq(name, "sec-fetch-site"))
        {
            if (!(ieq(val, "same-origin") || ieq(val, "none")))
                out->sf_site = 1;
            last = 2;
        }
        else if (ieq(name, "sec-fetch-dest"))
        {
            uint8_t d = ieq(val, "document") ? 1 : (ieq(val, "empty") ? 2 : 3);
            if (out->sf_dest == 0 || d > out->sf_dest)
                out->sf_dest = d;
            last = 3;
        }
        else if (ieq(name, "origin"))
        {
            if (out->origin_n == 0)
                out->origin = toSpan(val);
            out->origin_n = inc(out->origin_n);
            last = 4;
        }
        else if (ieq(name, "x-mc"))
        {
            out->xmc_n = inc(out->xmc_n);
            if (!(val.n == 1 && val.p[0] == '1'))
                out->xmc_bad = 1;
            last = 5;
        }
        else if (ieq(name, "content-length"))
        {
            const int32_t sv = strictCl(val.p, val.n);
            if (sv < 0)
                clBad = true;
            else if (out->content_length < 0)
                out->content_length = sv;
            else if (out->content_length != sv)
                clBad = true;
        }
    }
    if (res == WRQ_OK && clBad)
        res = WRQ_BAD_CONTENT_LENGTH;
    return res;
}

#endif // WEB_REQUEST_H
