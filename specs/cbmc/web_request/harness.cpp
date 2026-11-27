// FORMAL-02 wave 6' harness: the head parser src/web_functions/web_request.h (header only, compiled as is; limits lowered by -D).
// Surface: the first bytes a LAN client sends to the node web server (untrusted TCP stream), accumulated by webHeadPush and split by webReqParse.
// Modes (one -D per job):
//   M_PUSH_SMALL   webHeadPush over STREAM symbolic bytes (any byte, WEB_HEAD_MAX=8): no write outside head/line/cookie, head_len/line_len/cookie_len
//                  within their caps, NUL terminators in place, cl_strict in [-1, WEB_CL_MAX], sticky after DONE / LINE_TOO_BIG (same result, state frozen),
//                  result/flag agreement, a LINE_TOO_BIG wipes line and head
//   M_PUSH_EDGE    one line of L in {7,8,9} symbolic non-CR/LF bytes + LF with WEB_HEAD_MAX=8: 7 and 8 bytes give MORE (cut flag exactly at 8),
//                  the 9th byte gives LINE_TOO_BIG (cap-1 / cap / cap+1; the real caps need unwind 1025)
//   M_PUSH_COOKIE  "Cookie:" + 8 symbolic bytes (NUL allowed, not CR/LF) + LF: the captured pair equals a reference model (first "mcs=" at k >= 7
//                  that ends before the first NUL at or after index 7 and whose predecessor is ':' ' ' or ';'; copy up to ';' or WEB_COOKIE_MAX bytes,
//                  NUL bytes are copied); nothing captured otherwise
//   M_PUSH_CL      two lines "content-length:" + 2 symbolic bytes: lenient value and strict judgement equal a reference model
//   M_ATOL_SHORT   atolSat on 0..6 symbolic bytes (exact buffer, no terminator) equals a 64 bit reference model
//   M_ATOL_SAT     atolSat on 12 bytes (sign/digit, 11 digits): saturates exactly at LONG_MIN / LONG_MAX
//   M_STRICT       strictCl on 0..8 symbolic bytes equals a reference model, result -1 or in [0, WEB_CL_MAX]
//   M_PARSE_ANY   webReqParse on PMIN..PLEN symbolic bytes (exact heap buffer) drawn from the small constant alphabet ALPHA (-D, index into a table,
//                  no 256 value bytes); LEAD_LF fixes byte 0 to LF (empty request line, the rest is header lines). Generic checks (spans inside
//                  [buf, buf+min(len,WEB_HEAD_MAX)), path/query shape, method in the enum, WRQ_OK only for a terminated request line ending in
//                  HTTP/1.0|1.1 and a target that starts with '/' without a blank (B3 request-line shape)), q.cut == cut; NOHDR: no header fact (alphabet cannot spell a header name); MODEL_BLANK: blank equals a closed-form
//                  reference (an empty terminated line after the request line, or a lone CR as unterminated last line below the window)
//   M_PARSE_METHOD  one of GET POST HEAD OPTIONS PUT, optionally one byte replaced by 'x', then one blank (unterminated): method equals the enum, exact
//   M_PARSE_TARGET  "GET " + 0..3 symbolic target bytes {/ ? a blank} + " x" + optional CR + LF: target / path / query spans exact, res by target.n
//   M_PARSE_HDR   "\n" (empty request line) + Host: or X-MC: (case of the first letter symbolic) + 2 symbolic value bytes + LF: host span (trimmed,
//                  one final CR dropped), host_n / xmc_n, xmc_bad exact
//   M_PARSE_CORPUS  a table of concrete request heads (CORPUS=1 request line, 2 header lines, 3 Content-Length and limits) with every observable
//                  field of WebReq stated; concrete execution, no solver choice: an exhaustive enumeration of the listed inputs, not a proof over a class
// Witnesses (assertion must fail): WITNESS_BIG, WITNESS_COOKIE, WITNESS_OK
// Native sanity run (no ESBMC): -DNATIVE with native_shim.cpp (random stand-in for nondet).
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "web_request.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

#ifdef NATIVE
extern "C" int nd_range(int lo, int hi);
extern "C" unsigned char nd_notcrlf(int allow_nul);
#define NDR(lo, hi) nd_range(lo, hi)
#else
static int ndr_(int lo, int hi)
{
    int v = nondet_int();
    __ESBMC_assume(v >= lo && v <= hi);
    return v;
}
static unsigned char nd_notcrlf_(bool allow_nul)
{
    unsigned char c = nondet_uchar();
    __ESBMC_assume(c != '\r' && c != '\n');
    if (!allow_nul)
        __ESBMC_assume(c != 0);
    return c;
}
#define NDR(lo, hi) ndr_(lo, hi)
#define nd_notcrlf(a) nd_notcrlf_(a)
#endif

// one symbol of a constant alphabet: a small index into the table, not a free byte
static char pick(const char *tab, int n)
{
    int i = NDR(0, n - 1);
    return tab[i];
}
#define PICK(t) pick(t, (int)sizeof(t))
// alphabets of the parse jobs (each lists what the job's CHECKS promise)
static const char AL_ANY[] = {' ', '\t', '\r', '\n', '/', '?', 'G', 'E', 'T', 'x', 0};   // no ':'
static const char AL_COLON[] = {' ', '\t', '\r', '\n', ':', 'x', 0};
static const char AL_LINES[] = {'a', ':', ' ', '\r', '\n'};
static const char AL_CLAMP[] = {'a', ' ', '\r', '\n'};
static const char AL_TARGET[] = {'/', '?', 'a', ' '};
static const char AL_VAL[] = {' ', '\t', '1', 'x', '\r'};

#ifndef PMIN
#define PMIN 0
#endif
#ifndef STREAM
#define STREAM 10
#endif
#ifndef PLEN
#define PLEN 14
#endif
#ifndef PLEN_MAX
#define PLEN_MAX PLEN
#endif

static WebHeadAcc g_a;

static void inv(const WebHeadAcc &a)
{
    assert(a.head_len <= WEB_HEAD_MAX);
    assert(a.line_len <= WEB_HEAD_MAX);
    assert(a.cookie_len <= WEB_COOKIE_MAX);
    assert(a.head[a.head_len] == 0);
    assert(a.line[a.line_len] == 0);
    assert(a.cookie[a.cookie_len] == 0);
    assert(a.cl_strict >= -1 && a.cl_strict <= WEB_CL_MAX);
    if (a.flags & WEB_HF_BIG)
    {
        // the secret wipe: every byte of head and line, terminator included (WEB_HEAD_MAX + 1 bytes each). Checked in full in the jobs that can
        // reach WH_LINE_TOO_BIG (STREAM / EDGE); the cookie and Content-Length jobs feed lines shorter than WEB_HEAD_MAX and keep the cheap test
        assert(a.head_len == 0 && a.line_len == 0);
#if defined(M_PUSH_SMALL) || defined(M_PUSH_EDGE)
        for (int i = 0; i <= WEB_HEAD_MAX; i++)
        {
            assert(a.head[i] == 0);
            assert(a.line[i] == 0);
        }
#else
        assert(a.head[0] == 0 && a.line[0] == 0);
#endif
    }
    if (a.flags & WEB_HF_DONE)
        assert(a.line_len == 0);
}

static bool isSp(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

// 64 bit reference of atol() for at most 12 bytes (a 12 digit value fits), saturated to the width of long
static long refAtol(const unsigned char *s, int n)
{
    int i = 0;
    while (i < n && isSp(s[i]))
        i++;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-'))
    {
        neg = (s[i] == '-');
        i++;
    }
    long long v = 0;
    for (; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            break;
        v = v * 10 + (s[i] - '0');
    }
    long long r = neg ? -v : v;
    if (r > (long long)LONG_MAX)
        r = LONG_MAX;
    if (r < (long long)LONG_MIN)
        r = LONG_MIN;
    return (long)r;
}

// reference of the strict value: trim blanks, digits only, not empty, <= WEB_CL_MAX (at most 12 bytes)
static int refStrict(const unsigned char *s, int n)
{
    int a = 0, b = n;
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
        b--;
    if (a == b)
        return -1;
    long long v = 0;
    for (int i = a; i < b; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v > WEB_CL_MAX ? -1 : (int)v;
}

static void feedLine(const unsigned char *l, int n)
{
    for (int i = 0; i < n; i++)
    {
        WebHeadRes r = webHeadPush(&g_a, (char)l[i]);
        inv(g_a);
        assert(r == WH_MORE);
    }
}

// the window the parser looks at: len clamped to WEB_HEAD_MAX (D-5)
static size_t effLen(size_t len) { return len > (size_t)WEB_HEAD_MAX ? (size_t)WEB_HEAD_MAX : len; }

// a span lies inside [buf, buf+len); no pointer difference, an equality walk over the offsets
static void inside(const WebSpan &s, const char *buf, size_t len)
{
    bool ok = false;
    for (size_t o = 0; o <= (size_t)PLEN_MAX; o++)
    {
        if (o > len)
            break;
        if (s.p == buf + o && s.n <= len - o)
            ok = true;
    }
    assert(ok);
}

static void checkReq(const char *buf, size_t len0, const WebReq &q, WebParseRes res)
{
    const size_t len = effLen(len0);
    assert(res <= WRQ_BAD_CONTENT_LENGTH);
    inside(q.target, buf, len);
    inside(q.path, buf, len);
    inside(q.query, buf, len);
    inside(q.host, buf, len);
    inside(q.origin, buf, len);
    assert(q.method <= WM_OTHER);
    assert(q.minor <= 1);
    assert(q.content_length == -1 || (q.content_length >= 0 && q.content_length <= WEB_CL_MAX));
    assert(q.blank <= 1);
    // path is the prefix of target up to the first '?', query follows it and the '?'
    assert(q.path.p == q.target.p && q.path.n <= q.target.n);
    if (q.path.n < q.target.n)
    {
        assert(q.target.p[q.path.n] == '?');
        assert(q.query.p == q.path.p + q.path.n + 1);
        assert(q.query.n == q.target.n - q.path.n - 1);
    }
    else
        assert(q.query.n == 0);
    if (q.host_n == 0)
        assert(q.host.n == 0);
    if (q.origin_n == 0)
        assert(q.origin.n == 0);
    if (res == WRQ_OK)
    {
        // a version-less or foreign-version request line never gets WRQ_OK: the line is terminated and ends in " HTTP/1.0" or " HTTP/1.1"
        size_t e = 0;
        while (e < len && buf[e] != '\n')
            e++;
        assert(e < len);
        size_t rl = e;
        if (rl > 0 && buf[rl - 1] == '\r')
            rl--;
        assert(rl >= 12 && rl <= len);
        assert(buf[rl - 9] == ' ');
        assert(memcmp(buf + rl - 8, "HTTP/1.", 7) == 0);
        assert(buf[rl - 1] == '0' || buf[rl - 1] == '1');
        assert(q.minor == (uint8_t)(buf[rl - 1] - '0'));
        assert(q.target.n > 0 && q.method != WM_NONE);
        // B3 request-line shape: origin-form target (starts with '/', no blank inside); with the exact two-blank layout the version
        // blank is the only blank after the method's
        assert(q.target.p[0] == '/');
        for (size_t i = 0; i < (size_t)PLEN_MAX; i++)
            if (i < q.target.n)
                assert(q.target.p[i] != ' ');
    }
#ifdef WITNESS_OK
    assert(res != WRQ_OK);
#endif
}

// no header fact: the alphabet of the job cannot spell a header name the parser knows
static void noHeaderFacts(const WebReq &q)
{
    assert(q.host_n == 0 && q.origin_n == 0 && q.xmc_n == 0 && q.xmc_bad == 0);
    assert(q.sf_site == 0 && q.sf_dest == 0 && q.folded == 0 && q.content_length == -1);
    assert(q.host.n == 0 && q.origin.n == 0);
}

// closed-form reference of WebReq::blank, independent of the parser's loop: f = index of the LF that ends the request line; blank is set by an LF
// at i > f that follows an LF (empty line) or a CR that follows an LF (CR-only line), or by an unterminated last line that is a lone CR while the
// window is not full (the parser drops the CR and sees an empty line; with a full window the line counts as cut and is not judged)
static bool blankRef(const char *buf, size_t eff)
{
    size_t f = 0;
    while (f < eff && buf[f] != '\n')
        f++;
    if (f >= eff)
        return false;
    for (size_t i = 1; i <= (size_t)PLEN_MAX; i++)
    {
        if (i >= eff)
            break;
        if (i > f && buf[i] == '\n' && (buf[i - 1] == '\n' || (buf[i - 1] == '\r' && i >= f + 2 && buf[i - 2] == '\n')))
            return true;
    }
    if (eff < (size_t)WEB_HEAD_MAX && eff >= f + 2 && buf[eff - 1] == '\r' && buf[eff - 2] == '\n')
        return true;
    return false;
}

#ifdef M_PARSE_CORPUS
#include "corpus.h"
#endif

int main()
{
#if defined(M_PUSH_SMALL)
    unsigned char s[STREAM];
    for (int i = 0; i < STREAM; i++)
        s[i] = nondet_uchar();
    webHeadInit(&g_a);
    bool sticky = false;
    WebHeadRes last = WH_MORE;
    bool sawBig = false;
    for (int i = 0; i < STREAM; i++)
    {
        const uint16_t h = g_a.head_len, l = g_a.line_len, c = g_a.cookie_len;
        const uint8_t f = g_a.flags;
        WebHeadRes r = webHeadPush(&g_a, (char)s[i]);
        inv(g_a);
        if (sticky)
        {
            assert(r == last);
            assert(g_a.head_len == h && g_a.line_len == l && g_a.cookie_len == c && g_a.flags == f);
        }
        if (r == WH_DONE)
            assert(g_a.flags & WEB_HF_DONE);
        if (r == WH_LINE_TOO_BIG)
        {
            assert(g_a.flags & WEB_HF_BIG);
            sawBig = true;
        }
        if (r != WH_MORE)
        {
            sticky = true;
            last = r;
        }
    }
    (void)sawBig;
#ifdef WITNESS_BIG
    assert(!sawBig);
#endif

#elif defined(M_PUSH_EDGE)
    // WEB_HEAD_MAX = 8: lines of 7 (cap-1), 8 (cap), 9 (cap+1) bytes
    int L = NDR(7, 9);
    unsigned char b[9];
    for (int i = 0; i < 9; i++)
        b[i] = nd_notcrlf(1);
    webHeadInit(&g_a);
    for (int i = 0; i < 9; i++)
    {
        if (i >= L)
            break;
        WebHeadRes r = webHeadPush(&g_a, (char)b[i]);
        inv(g_a);
        if (L == 9 && i == 8)
        {
            assert(r == WH_LINE_TOO_BIG); // the 9th byte of a line is one too many
            assert(g_a.head_len == 0 && g_a.line_len == 0);
        }
        else
        {
            assert(r == WH_MORE);
            assert(g_a.line_len == (uint16_t)(i + 1));
        }
    }
    if (L < 9)
    {
        WebHeadRes r = webHeadPush(&g_a, '\n');
        inv(g_a);
        assert(r == WH_MORE && g_a.line_len == 0);
        assert(((g_a.flags & WEB_HF_CUT) != 0) == (L == 8)); // the LF no longer fits the window only after 8 line bytes
        assert(g_a.head_len == 8);
    }
    else
    {
        WebHeadRes r = webHeadPush(&g_a, 'x');
        assert(r == WH_LINE_TOO_BIG && g_a.head_len == 0 && g_a.line_len == 0);
        inv(g_a);
    }

#elif defined(M_PUSH_COOKIE)
    // WEB_HEAD_MAX = 16, WEB_COOKIE_MAX = 5; the line has 15 bytes, v[7..14] symbolic: any byte but CR and LF (NUL allowed)
    unsigned char v[15];
    memcpy(v, "Cookie:", 7);
    for (int i = 7; i < 15; i++)
        v[i] = nd_notcrlf(1);
    webHeadInit(&g_a);
    feedLine(v, 15);
    WebHeadRes r = webHeadPush(&g_a, '\n');
    inv(g_a);
    assert(r == WH_MORE);
    // the search ends at the first NUL at or after index 7 (String::indexOf is a strstr from there); the copy does not stop at NUL
    int eff = 7;
    while (eff < 15 && v[eff] != 0)
        eff++;
    int fk = -1;
    for (int k = 7; k + 4 <= eff; k++)
        if (fk < 0 && v[k] == 'm' && v[k + 1] == 'c' && v[k + 2] == 's' && v[k + 3] == '=' && (v[k - 1] == ':' || v[k - 1] == ' ' || v[k - 1] == ';'))
            fk = k;
    if (fk < 0)
        assert(g_a.cookie_len == 0);
    else
    {
        int n = 0;
        while (fk + n < 15 && v[fk + n] != ';' && n < WEB_COOKIE_MAX)
            n++;
        assert(g_a.cookie_len == n);
        for (int i = 0; i < 5; i++)
            if (i < n)
                assert((unsigned char)g_a.cookie[i] == v[fk + i]);
        assert(g_a.cookie[0] == 'm');
    }
    WebSpan sp = webHeadCookie(&g_a);
    assert(sp.p == g_a.cookie && sp.n == g_a.cookie_len);
#ifdef WITNESS_COOKIE
    assert(g_a.cookie_len == 0);
#endif

#elif defined(M_PUSH_CL)
    // WEB_HEAD_MAX = 20: two lines "content-length:" + 2 bytes (not CR/LF; digits, blanks, signs, NUL, anything)
    unsigned char v1[2], v2[2];
    for (int i = 0; i < 2; i++)
    {
        v1[i] = nd_notcrlf(1);
        v2[i] = nd_notcrlf(1);
    }
    unsigned char l1[17], l2[17];
    memcpy(l1, "content-length:", 15);
    memcpy(l2, "Content-Length:", 15);
    memcpy(l1 + 15, v1, 2);
    memcpy(l2 + 15, v2, 2);
    webHeadInit(&g_a);
    assert(webHeadContentLength(&g_a) == -1 && !webHeadBadContentLength(&g_a));
    feedLine(l1, 17);
    assert(webHeadPush(&g_a, '\n') == WH_MORE);
    inv(g_a);
    feedLine(l2, 17);
    assert(webHeadPush(&g_a, '\n') == WH_MORE);
    inv(g_a);
    long cl = -1;
    int cs = -1;
    bool bad = false;
    const unsigned char *vv[2] = {v1, v2};
    for (int j = 0; j < 2; j++)
    {
        if (cl < 0)
            cl = refAtol(vv[j], 2);
        int sv = refStrict(vv[j], 2);
        if (sv < 0)
            bad = true;
        else if (cs < 0)
            cs = sv;
        else if (cs != sv)
            bad = true;
    }
    assert(webHeadContentLength(&g_a) == cl);
    assert(g_a.cl_strict == cs);
    assert(webHeadBadContentLength(&g_a) == bad);

#elif defined(M_ATOL_SHORT)
    int n = NDR(0, 6);
    unsigned char *b = new unsigned char[n > 0 ? n : 1];
    for (int i = 0; i < 6; i++)
        if (i < n)
            b[i] = nondet_uchar();
    long r = webreq_detail::atolSat((const char *)b, (size_t)n);
    assert(r == refAtol(b, n));

#elif defined(M_ATOL_SAT)
    unsigned char b[12];
    b[0] = (unsigned char)NDR(0, 11); // 0..9 digit, 10 '+', 11 '-'
    b[0] = b[0] < 10 ? (unsigned char)('0' + b[0]) : (b[0] == 10 ? '+' : '-');
    for (int i = 1; i < 12; i++)
        b[i] = (unsigned char)NDR('0', '9');
    unsigned char *p = new unsigned char[12];
    for (int i = 0; i < 12; i++)
        p[i] = b[i];
    long r = webreq_detail::atolSat((const char *)p, 12);
    assert(r == refAtol(b, 12));

#elif defined(M_STRICT)
    int n = NDR(0, 8);
    unsigned char *b = new unsigned char[n > 0 ? n : 1];
    for (int i = 0; i < 8; i++)
        if (i < n)
            b[i] = nondet_uchar();
    int32_t r = webreq_detail::strictCl((const char *)b, (size_t)n);
    assert(r == -1 || (r >= 0 && r <= WEB_CL_MAX));
    assert(r == refStrict(b, n));

#elif defined(M_PARSE_ANY)
    int len = NDR(PMIN, PLEN);
    char *buf = new char[len > 0 ? len : 1];
    for (int i = 0; i < PLEN; i++)
        if (i < len)
        {
#ifdef LEAD_LF
            buf[i] = (i == 0) ? '\n' : PICK(ALPHA);
#else
            buf[i] = PICK(ALPHA);
#endif
        }
    bool cut = nondet_int() != 0;
    WebReq q;
    WebParseRes res = webReqParse(buf, (size_t)len, cut, &q);
    checkReq(buf, (size_t)len, q, res);
    assert(q.cut == cut);
#ifdef NOHDR
    noHeaderFacts(q);
#endif
#ifdef MODEL_BLANK
    assert((q.blank != 0) == blankRef(buf, effLen((size_t)len)));
#endif

#elif defined(M_PARSE_METHOD)
    // a method word, optionally one byte of it replaced by 'x' (never a valid word then), then one blank: the request line has no LF
    static const char *const words[5] = {"GET", "POST", "HEAD", "OPTIONS", "PUT"};
    static const int wlen[5] = {3, 4, 4, 7, 3};
    static const int wmeth[5] = {WM_GET, WM_POST, WM_HEAD, WM_OPTIONS, WM_OTHER};
    const int wi = NDR(0, 4);
    const int wl = wlen[wi];
    const int cp = NDR(0, 7); // 0..6: damage this position if it is inside the word, 7: none
    char tmp[8];
    for (int i = 0; i < 7; i++)
        if (i < wl)
            tmp[i] = (i == cp) ? 'x' : words[wi][i];
    tmp[wl] = ' ';
    const int len = wl + 1;
    char *buf = new char[len];
    for (int i = 0; i < 8; i++)
        if (i < len)
            buf[i] = tmp[i];
    WebReq q;
    WebParseRes res = webReqParse(buf, (size_t)len, false, &q);
    checkReq(buf, (size_t)len, q, res);
    const bool damaged = cp < wl;
    assert(q.method == (damaged ? (int)WM_OTHER : wmeth[wi]));
    assert(res == WRQ_BAD_REQUEST_LINE); // never terminated
    assert(q.target.n == 0 && q.path.n == 0 && q.query.n == 0 && q.blank == 0);

#elif defined(M_PARSE_TARGET)
    // "GET " + tl target bytes + " x" + optional CR + LF; the last blank of the line is the one before the 'x'
#ifndef TL_LO
#define TL_LO 0
#endif
#ifndef TL_HI
#define TL_HI 3
#endif
    const int tl = NDR(TL_LO, TL_HI); // split across jobs (pass_parse_target 0..2, pass_parse_target3 3): one job over 15 GB
    const bool cr = nondet_int() != 0;
    char tmp[12];
    memcpy(tmp, "GET ", 4);
    char tg[3];
    for (int i = 0; i < 3; i++)
    {
        tg[i] = PICK(AL_TARGET);
        if (i < tl)
            tmp[4 + i] = tg[i];
    }
    int n = 4 + tl;
    tmp[n++] = ' ';
    tmp[n++] = 'x';
    if (cr)
        tmp[n++] = '\r';
    tmp[n++] = '\n';
    char *buf = new char[n];
    for (int i = 0; i < 12; i++)
        if (i < n)
            buf[i] = tmp[i];
    WebReq q;
    WebParseRes res = webReqParse(buf, (size_t)n, false, &q);
    checkReq(buf, (size_t)n, q, res);
    int pl = tl; // reference: the target is exactly tg[0..tl), the path ends at the first '?'
    for (int i = 2; i >= 0; i--)
        if (i < tl && tg[i] == '?')
            pl = i;
    assert(q.method == WM_GET && q.minor == 0);
    assert(q.target.p == buf + 4 && q.target.n == (uint16_t)tl);
    assert(q.path.p == buf + 4 && q.path.n == (uint16_t)pl);
    assert(pl == tl ? q.query.n == 0 : (q.query.p == buf + 5 + pl && q.query.n == (uint16_t)(tl - pl - 1)));
    assert(res == (tl == 0 ? WRQ_BAD_REQUEST_LINE : WRQ_BAD_VERSION)); // version "x"
    assert(q.blank == 0);
    noHeaderFacts(q);

#elif defined(M_PARSE_HDR)
    // "\n" (empty request line, BAD_REQUEST_LINE, the header lines are read all the same) + name + 2 value bytes + LF
    const int ni = NDR(0, 1);
    const bool low = nondet_int() != 0; // lower-case first letter of the name
    char tmp[9];
    int n = 0;
    tmp[n++] = '\n';
    const int nl = 5;
    tmp[n++] = ni == 0 ? (low ? 'h' : 'H') : (low ? 'x' : 'X');
    tmp[n++] = ni == 0 ? 'o' : '-';
    tmp[n++] = ni == 0 ? 's' : 'M';
    tmp[n++] = ni == 0 ? 't' : 'C';
    tmp[n++] = ':';
    char v[2];
    for (int i = 0; i < 2; i++)
    {
        v[i] = PICK(AL_VAL);
        tmp[n++] = v[i];
    }
    tmp[n++] = '\n';
    char *buf = new char[n];
    for (int i = 0; i < 9; i++)
        if (i < n)
            buf[i] = tmp[i];
    WebReq q;
    WebParseRes res = webReqParse(buf, (size_t)n, false, &q);
    checkReq(buf, (size_t)n, q, res);
    // reference: one final CR of the line is dropped, then blanks and tabs are trimmed on both sides
    int vend = 2;
    if (v[1] == '\r')
        vend = 1;
    int a = 0;
    while (a < vend && (v[a] == ' ' || v[a] == '\t'))
        a++;
    int b = vend;
    while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t'))
        b--;
    assert(res == WRQ_BAD_REQUEST_LINE && q.method == WM_NONE && q.blank == 0);
    assert(q.origin_n == 0 && q.sf_site == 0 && q.sf_dest == 0 && q.folded == 0 && q.content_length == -1);
    if (ni == 0)
    {
        assert(q.host_n == 1 && q.xmc_n == 0 && q.xmc_bad == 0);
        assert(q.host.p == buf + 1 + nl + a && q.host.n == (uint16_t)(b - a));
    }
    else
    {
        assert(q.host_n == 0 && q.xmc_n == 1);
        assert(q.xmc_bad == ((b - a == 1 && v[a] == '1') ? 0 : 1));
    }

#elif defined(M_PARSE_CORPUS)
    corpus();
#else
#error "no mode"
#endif
    return 0;
}
