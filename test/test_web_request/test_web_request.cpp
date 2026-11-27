// Host test for src/web_functions/web_request.h -- the pure request-head parser (FORMAL-02 wave 6).
//
//   pio test -e native_web_request   (host only, no hardware)
//
// 1. Reference model: ref_* below is a String-free copy of TODAY's head loop of work_webpage()
//    (web_functions.cpp :1086-1089 window, :1099 cookie quiet flag, :1478-1511 line end, :1514 line
//    too big). webHeadPush is compared with it byte by byte: window, line in progress (so every
//    finished line), cookie, Content-Length, over every request of test/test_web_diff/corpus/*.req and
//    over generated edge cases.
// 2. webReqParse unit cases (method, version, '?' split, spans, cut flag, header facts).
// 3. B6 (strict Content-Length) cases.

#include <unity.h>
#include "../support/repo_file.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include <web_functions/web_request.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- reference model (today's loop)

struct Ref
{
    std::string head, line, cookie;
    bool quiet = false, done = false, big = false, cut = false;
    long cl = -1;
};

static const size_t kHeaderMax = 1023;

// Arduino String::equalsIgnoreCase(lit), literally as both cores write it (ESP32 WString.cpp :534,
// nRF52 WString.cpp :478): equal length, then `while (*p1)` over the String's own buffer -- the loop
// ends at the first NUL of the line, so a line with a NUL inside the compared range matches.
static bool ref_ieq(const std::string &s, const char *lit)
{
    if (s.size() != strlen(lit))
        return false;
    if (s.empty())
        return true;
    const char *p1 = s.c_str();
    const char *p2 = lit;
    while (*p1)
    {
        if (tolower((unsigned char)*p1++) != tolower((unsigned char)*p2++))
            return false;
    }
    return true;
}

// Arduino String::indexOf(s, from): strstr on the buffer, so it ends at the first NUL
static int ref_indexOf(const std::string &str, const char *needle, size_t from)
{
    if (from >= str.size())
        return -1;
    const char *hit = strstr(str.c_str() + from, needle);
    return hit == NULL ? -1 : (int)(hit - str.c_str());
}

// returns true when the loop ends (blank line or too big line)
static bool ref_push(Ref &r, char c)
{
    if (r.head.size() < kHeaderMax)
        r.head += c;
    else
        r.cut = true;
    if (r.line.size() == 7 && ref_ieq(r.line, "cookie:"))
        r.quiet = true;
    if (c == '\n')
    {
        if (r.line.empty())
        {
            r.done = true;
            return true;
        }
        if (r.cl < 0)
        {
            // String hdr_name = line; hdr_name.toLowerCase() (loop `for (p; *p; p++)`: ends at the first NUL);
            // hdr_name.startsWith("content-length:") (length check, then strncmp over 15 bytes)
            std::string h = r.line;
            for (char *p = &h[0]; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            if (h.size() >= 15 && strncmp(h.c_str(), "content-length:", 15) == 0)
                r.cl = atol(r.line.c_str() + 15); // String::substring(15).toInt(): atol stops at a NUL
        }
        if (r.quiet)
        {
            r.quiet = false;
            int k = 6;
            while ((k = ref_indexOf(r.line, "mcs=", (size_t)k + 1)) >= 0 &&
                   !(r.line[k - 1] == ':' || r.line[k - 1] == ' ' || r.line[k - 1] == ';'))
                ;
            if (k >= 0)
            {
                std::string ck;
                for (; k < (int)r.line.size() && r.line[k] != ';' && ck.size() < 255; k++)
                    ck += r.line[k];
                r.cookie = ck;
            }
        }
        r.line.clear();
    }
    else if (c != '\r')
    {
        if (r.line.size() >= kHeaderMax)
        {
            r.line.clear();
            r.head.clear(); // the 431 branch wipes the window
            r.big = true;
            return true;
        }
        r.line += c;
    }
    return false;
}

// today's value is the host atol() itself (long, saturating, negatives kept): the parser must be identical
static long ref_cl(long v) { return v; }

// TEST_ASSERT_EQUAL_MEMORY refuses a length of 0
#define MEM_EQ(exp, act, len, msg) \
    do                             \
    {                              \
        if ((len) > 0)             \
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(exp, act, len, msg); \
    } while (0)

static WebHeadAcc *g_acc = NULL; // never on the stack

static size_t g_streams = 0, g_bytes = 0;

// feed the stream into both, compare after every byte; returns the accumulator window
static void compare_stream(const std::string &s, const char *what)
{
    static char msg[300];
    Ref r;
    webHeadInit(g_acc);
    WebHeadAcc &a = *g_acc;
    bool ended = false;
    size_t i = 0;
    for (; i < s.size() && !ended; i++)
    {
        const char c = s[i];
        // the line in progress before the byte is the line a LF finishes
        snprintf(msg, sizeof(msg), "%s: line before byte %zu", what, i);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(r.line.size(), a.line_len, msg);
        MEM_EQ(r.line.data(), a.line, r.line.size(), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)a.line[a.line_len], msg);

        ended = ref_push(r, c);
        const WebHeadRes res = webHeadPush(&a, c);

        snprintf(msg, sizeof(msg), "%s: after byte %zu (0x%02x)", what, i, (unsigned)(uint8_t)c);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(r.head.size(), a.head_len, msg);
        MEM_EQ(r.head.data(), a.head, r.head.size(), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)a.head[a.head_len], msg);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(r.cookie.size(), a.cookie_len, msg);
        MEM_EQ(r.cookie.data(), a.cookie, r.cookie.size(), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)a.cookie[a.cookie_len], msg);
        TEST_ASSERT_TRUE_MESSAGE(ref_cl(r.cl) == webHeadContentLength(&a), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(r.cut ? 1 : 0, (a.flags & WEB_HF_CUT) ? 1 : 0, msg);
        TEST_ASSERT_TRUE_MESSAGE(a.head_len <= WEB_HEAD_MAX && a.line_len <= WEB_HEAD_MAX, msg);
        const WebHeadRes want = r.big ? WH_LINE_TOO_BIG : (r.done ? WH_DONE : WH_MORE);
        TEST_ASSERT_EQUAL_INT_MESSAGE(want, res, msg);
        g_bytes++;
    }
    g_streams++;
    if (ended)
    { // sticky: more bytes change nothing
        const std::string head(a.head, a.head_len), ck(a.cookie, a.cookie_len);
        const long cl = a.content_length;
        const WebHeadRes again = r.big ? WH_LINE_TOO_BIG : WH_DONE;
        for (int k = 0; k < 5; k++)
        {
            TEST_ASSERT_EQUAL_INT(again, webHeadPush(&a, "x\n\r:\n\n"[k]));
            TEST_ASSERT_EQUAL_UINT(head.size(), a.head_len);
            if (!head.empty()) TEST_ASSERT_EQUAL_MEMORY(head.data(), a.head, head.size());
            TEST_ASSERT_EQUAL_UINT(ck.size(), a.cookie_len);
            TEST_ASSERT_TRUE(cl == a.content_length);
        }
    }
}

// ---------------------------------------------------------------- corpus

static std::vector<std::string> corpus_files()
{
    std::vector<std::string> names;
    const std::string dir = repo_path("test/test_web_diff/corpus");
    DIR *d = opendir(dir.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(d, "corpus directory test/test_web_diff/corpus not readable");
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".req") == 0)
            names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

static void test_corpus_matches_reference(void)
{
    const std::vector<std::string> names = corpus_files();
    TEST_ASSERT_TRUE_MESSAGE(names.size() >= 100, "corpus unexpectedly small");
    size_t withCookie = 0, withCl = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        const std::string rel = "test/test_web_diff/corpus/" + names[i];
        const std::string req = repo_slurp(rel.c_str());
        compare_stream(req, names[i].c_str());
        if (g_acc->cookie_len > 0)
            withCookie++;
        if (g_acc->content_length >= 0)
            withCl++;
    }
    printf("corpus requests compared: %zu (with cookie %zu, with content-length %zu)\n", names.size(), withCookie,
           withCl);
    TEST_ASSERT_TRUE(withCl > 0);
}

// the window of every corpus request, parsed: spans inside the buffer, split equal to an independent split
static void check_spans(const char *buf, size_t len, const WebReq &q)
{
    const WebSpan sp[] = {q.target, q.path, q.query, q.host, q.origin};
    for (size_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(sp[i].p >= buf && sp[i].p + sp[i].n <= buf + len);
    }
    TEST_ASSERT_TRUE(q.path.n <= q.target.n);
    TEST_ASSERT_TRUE(q.path.p == q.target.p);
    if (q.path.n < q.target.n)
    {
        TEST_ASSERT_EQUAL_CHAR('?', q.target.p[q.path.n]);
        TEST_ASSERT_TRUE(q.query.p == q.target.p + q.path.n + 1);
        TEST_ASSERT_EQUAL_UINT(q.target.n - q.path.n - 1, q.query.n);
    }
    else
        TEST_ASSERT_EQUAL_UINT(0, q.query.n);
    TEST_ASSERT_TRUE(q.method <= WM_OTHER);
    TEST_ASSERT_TRUE(q.content_length >= -1);
}

static void test_corpus_request_line_split(void)
{
    const std::vector<std::string> names = corpus_files();
    size_t ok = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        const std::string rel = "test/test_web_diff/corpus/" + names[i];
        const std::string req = repo_slurp(rel.c_str());
        webHeadInit(g_acc);
        for (size_t k = 0; k < req.size(); k++)
            if (webHeadPush(g_acc, req[k]) != WH_MORE)
                break;
        WebReq q;
        const WebParseRes pr = webReqParse(g_acc->head, g_acc->head_len, (g_acc->flags & WEB_HF_CUT) != 0, &q);
        check_spans(g_acc->head, g_acc->head_len, q);
        // independent split of the first line, the way web_functions.cpp :1110 and web_guard.h do
        const std::string w(g_acc->head, g_acc->head_len);
        size_t nl = w.find('\n');
        if (nl == std::string::npos)
        {
            TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, pr);
            continue;
        }
        std::string rl = w.substr(0, nl);
        if (!rl.empty() && rl.back() == '\r')
            rl.pop_back();
        size_t a = rl.find(' '), b = rl.rfind(' ');
        if (a != std::string::npos && b > a)
        {
            TEST_ASSERT_EQUAL_STRING(rl.substr(a + 1, b - a - 1).c_str(), std::string(q.target.p, q.target.n).c_str());
            const std::string ver = rl.substr(b + 1);
            if (ver == "HTTP/1.1" || ver == "HTTP/1.0")
                ok++;
        }
    }
    printf("corpus request lines with HTTP/1.0|1.1: %zu of %zu\n", ok, names.size());
    TEST_ASSERT_TRUE(ok > 0);
}

// ---------------------------------------------------------------- generated edge cases

static size_t g_generated = 0;
static void gen(const std::string &s, const char *what)
{
    compare_stream(s, what);
    g_generated++;
}

static std::string rep(char c, size_t n) { return std::string(n, c); }

static void test_window_edges(void)
{
    const char *pre = "GET /";
    // total stream length 1021..1026 around the window, with and without a terminating blank line
    for (size_t total = 1020; total <= 1026; total++)
    {
        std::string s = pre;
        s += rep('a', total - s.size() - 2);
        s += "\r\n";
        gen(s, "window crlf end");
        gen(s + "\r\n", "window blank");
        gen(s + "Host: x\r\n\r\n", "window host");
    }
    // window exactly 1022 / 1023 / 1024 / 2000 bytes made of short lines (no line is too long)
    for (size_t total : {1022u, 1023u, 1024u, 2000u})
    {
        std::string s = "GET / HTTP/1.1\r\n";
        while (s.size() < total)
            s += "X-Pad: 0123456789abcdef0123456789\r\n";
        s.resize(total);
        gen(s, "window padded");
        gen(s + "\r\nContent-Length: 77\r\nCookie: mcs=late\r\n\r\n", "window padded then late headers");
    }
}

static void test_long_lines(void)
{
    for (size_t n : {1021u, 1022u, 1023u, 1024u, 1025u, 1500u, 3000u})
    {
        gen("GET /" + rep('b', n) + " HTTP/1.1\r\n\r\n", "long request line");
        gen("GET / HTTP/1.1\r\nX: " + rep('c', n) + "\r\n\r\n", "long header line");
        gen("GET / HTTP/1.1\r\nCookie: mcs=" + rep('d', n) + "\r\n\r\n", "long cookie line");
        gen("GET / HTTP/1.1\r\nContent-Length: " + rep('7', n) + "\r\n\r\n", "long content-length line");
        gen(rep('e', n), "long line without end");
        gen(rep('e', n) + "\n\n", "long line then LF");
        // a CR pad does not count into the line
        gen("GET / HTTP/1.1\r\nX: " + rep('\r', n) + "v\r\n\r\n", "CR pad");
    }
    // exactly at the line cap: 1023 chars accepted, the 1024th refused (not the LF)
    const std::string l1023 = rep('x', 1023);
    gen(l1023 + "\n\n", "line 1023");
    gen(l1023 + "x\n\n", "line 1024");
    gen(l1023 + "\r\n\r\n", "line 1023 crlf");
    gen(l1023 + "\r" + "x", "line 1023 CR x");
}

static void test_line_end_games(void)
{
    gen("", "empty");
    gen("\n", "bare LF first");
    gen("\r\n", "CRLF first");
    gen("\r\r\n", "CR CR LF first");
    gen("GET / HTTP/1.1\n\n", "bare LF");
    gen("GET / HTTP/1.1\r\r\n\r\r\n", "double CR");
    gen("GET / HTTP/1.1\rHost: x\r\n\r\n", "CR without LF");
    gen("GET / HTTP/1.1\r\nHost: x\rContent-Length: 5\r\n\r\n", "CR joins lines");
    gen("GET / HTTP/1.1\r\n\n", "CRLF then bare LF blank");
    gen("GET / HTTP/1.1\r\n\r", "blank line CR only");
    gen("GET / HTTP/1.1\r\n\r\nContent-Length: 5\r\n", "bytes after the blank line");
    gen("GET / HTTP/1.1\r\n\n\nContent-Length: 5\r\n\r\n", "two blank lines");
    gen(std::string("GET /a\0b HTTP/1.1\r\n\r\n", 21), "NUL in line");
    gen("GET / HTTP/1.1\r\nA: b\r\n \r\n\r\n", "line of one space");
    gen("GET / HTTP/1.1\r\nA: b\r\n\t\r\n\r\n", "line of one tab");
}

static void test_cookie_cases(void)
{
    const char *hs[] = {"Cookie", "cookie", "COOKIE", "CoOkIe", "Cookie ", "xCookie", "Cookies", "Cooki"};
    for (size_t i = 0; i < sizeof(hs) / sizeof(hs[0]); i++)
        for (const char *v : {" mcs=abc", "mcs=abc", "\tmcs=abc", " a=b; mcs=abc; c=d", " a=b;mcs=abc", " amcs=abc",
                              " a=mcs=abc", " mcs=a;b=c", " mcs=", " MCS=abc", " mcs", " mc", " xmcs=1; mcs=2",
                              " a=b mcs=1", " mcs=1 mcs=2", "mcs=a:mcs=b", " ;mcs=z", ":mcs=y"})
        {
            const std::string line = std::string(hs[i]) + ":" + v;
            gen("GET / HTTP/1.1\r\n" + line + "\r\n\r\n", "cookie variant");
            gen("GET / HTTP/1.1\r\n" + line + "\r\n" + line + "x\r\n\r\n", "cookie twice");
            gen("GET / HTTP/1.1\r\n" + line + "\n\n", "cookie bare LF");
        }
    // the byte before "mcs=" is the first byte of the value (index 7) or any other char
    for (int c = 0; c < 256; c++)
    {
        std::string s = "GET / HTTP/1.1\r\nCookie:";
        s += (char)c;
        s += "mcs=v\r\n\r\n";
        if (c != '\n' && c != '\r' && c != 0)
            gen(s, "byte before mcs=");
    }
    // later Cookie lines keep or replace the first value
    gen("GET / HTTP/1.1\r\nCookie: mcs=first\r\nCookie: other=1\r\n\r\n", "second cookie without mcs");
    gen("GET / HTTP/1.1\r\nCookie: mcs=first\r\nCookie: mcs=second\r\n\r\n", "second cookie with mcs");
    gen("GET / HTTP/1.1\r\nCookie: a=b\r\nCookie: mcs=late\r\n\r\n", "mcs on the second");
    // cookie cap 255
    for (size_t n : {250u, 251u, 252u, 253u, 254u, 255u, 256u, 300u, 900u})
    {
        gen("GET / HTTP/1.1\r\nCookie: mcs=" + rep('k', n) + "\r\n\r\n", "cookie length");
        gen("GET / HTTP/1.1\r\nCookie: mcs=" + rep('k', n) + "; x=1\r\n\r\n", "cookie length then ;");
    }
    // behind the window
    gen("GET /" + rep('p', 1100) + " HTTP/1.1\r\n", "no CRLF after long line only");
    std::string pad = "GET / HTTP/1.1\r\n";
    while (pad.size() < 1500)
        pad += "X-Pad: 0123456789abcdef0123456789abcdef\r\n";
    gen(pad + "Cookie: foo=1; mcs=behind\r\n\r\n", "cookie behind the window");
    // NUL inside the cookie line: the search ends, the copy does not
    {
        static const char t[] = "GET / HTTP/1.1\r\nCookie: a\0 mcs=1\r\n\r\n";
        gen(std::string(t, sizeof(t) - 1), "NUL before mcs=");
    }
    {
        static const char t[] = "GET / HTTP/1.1\r\nCookie: mcs=a\0b;c\r\n\r\n";
        gen(std::string(t, sizeof(t) - 1), "NUL inside the cookie");
    }
    // NUL inside the first 7 bytes: String::equalsIgnoreCase ends its loop at the first NUL of the line,
    // so a 7 byte prefix with a NUL counts as "cookie:" today; indexOf then searches from index 7 up to the
    // first NUL at or after index 7
    {
        static const char a[] = "GET / HTTP/1.1\r\n\0AAAAAA mcs=abc\r\n\r\n";
        gen(std::string(a, sizeof(a) - 1), "NUL prefix 0AAAAAA");
        static const char b[] = "GET / HTTP/1.1\r\nc\0xxxxx mcs=abc\r\n\r\n";
        gen(std::string(b, sizeof(b) - 1), "NUL prefix c0xxxxx");
        static const char c[] = "GET / HTTP/1.1\r\ncookie\0 mcs=abc\r\n\r\n";
        gen(std::string(c, sizeof(c) - 1), "NUL prefix cookie0");
        static const char d[] = "GET / HTTP/1.1\r\ncookie\0mcs=abc\r\n\r\n"; // mcs= starts at index 7
        gen(std::string(d, sizeof(d) - 1), "NUL prefix cookie0, mcs= at 7");
    }
    for (int p = 0; p <= 8; p++)
    {
        const std::string base = "Cookie: ab mcs=abc; x=1";
        const std::string tails[] = {"", " mcs=def", std::string("\0 mcs=ghi", 9), "; mcs=jkl"};
        for (size_t t = 0; t < sizeof(tails) / sizeof(tails[0]); t++)
        {
            std::string rep_line = base + tails[t]; // NUL replaces the byte at p
            rep_line[p] = '\0';
            std::string ins_line = base + tails[t]; // NUL inserted before the byte at p
            ins_line.insert((size_t)p, 1, '\0');
            std::string eq_line = "AAAAAAA mcs=abc" + tails[t]; // 7 byte prefix of no cookie, NUL at p
            if (p < 7)
                eq_line[p] = '\0';
            for (const std::string &ln : {rep_line, ins_line, eq_line})
            {
                gen("GET / HTTP/1.1\r\n" + ln + "\r\n\r\n", "NUL in cookie line, CRLF");
                gen("GET / HTTP/1.1\r\n" + ln + "\n\n", "NUL in cookie line, LF");
                gen("GET / HTTP/1.1\r\nCookie: mcs=first\r\n" + ln + "\r\n\r\n", "NUL cookie line after a cookie");
            }
        }
    }
    // cookie with the 7th byte already split by CR
    gen("GET / HTTP/1.1\r\nCoo\rkie: mcs=1\r\n\r\n", "CR inside the name");
    gen("GET / HTTP/1.1\r\nCookie:\r mcs=1\r\n\r\n", "CR after the colon");
}

static void test_content_length_cases(void)
{
    const char *vals[] = {"5",        "0",        "00",          "007",        "+5",         "-1",
                          "-5",       "-0",       "12abc",       "abc",        "",           " ",
                          " 5",       "5 ",       "  5  ",       "\t5",        "5\t",        "\t5\t",
                          "99999999999", "2147483647", "2147483648", "4294967296", "-2147483648", "-2147483649",
                          "99999999999999999999999999", "65535", "65536", "1 2", "5;6", "0x10", "1e3", "- 5", "+ 5",
                          "+-5", "--5", "\v5", "\f5", "5\v", "٣"};
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        const std::string v = vals[i];
        for (const char *name : {"Content-Length", "content-length", "CONTENT-LENGTH", "Content-length", "ContEnt-LenGth"})
        {
            gen(std::string("POST /x HTTP/1.1\r\n") + name + ":" + v + "\r\n\r\n", "content-length value");
            gen(std::string("POST /x HTTP/1.1\r\n") + name + ": " + v + "\n\n", "content-length value LF");
        }
        // second header: today's first non-negative value wins, a negative one is replaced
        for (size_t j = 0; j < sizeof(vals) / sizeof(vals[0]); j += 3)
            gen(std::string("POST /x HTTP/1.1\r\nContent-Length: ") + v + "\r\nContent-Length: " + vals[j] + "\r\n\r\n",
                "two content-length lines");
    }
    gen("POST /x HTTP/1.1\r\nContent-Length : 5\r\n\r\n", "space before colon");
    gen("POST /x HTTP/1.1\r\n Content-Length: 5\r\n\r\n", "folded line");
    gen("POST /x HTTP/1.1\r\nContent-Length:5\r\nContent-Length:6\r\nContent-Length:7\r\n\r\n", "three lines");
    gen("POST /x HTTP/1.1\r\nContent-Length:\r\nContent-Length: 6\r\n\r\n", "empty then 6");
    gen("POST /x HTTP/1.1\r\nContent-Length:", "line without end");
    gen("POST /x HTTP/1.1\r\nContent-Length", "name without colon");
    gen("POST /x HTTP/1.1\r\nContent-Length:\n", "empty value LF");
    // the 15 bytes of the name are all that is compared: longer names do not match
    gen("POST /x HTTP/1.1\r\nContent-Length-X: 5\r\n\r\n", "longer name");
    gen("POST /x HTTP/1.1\r\nX-Content-Length: 5\r\n\r\n", "prefixed name");
    // behind the window
    std::string pad = "POST /x HTTP/1.1\r\n";
    while (pad.size() < 1400)
        pad += "X-Pad: 0123456789abcdef0123456789abcdef\r\n";
    gen(pad + "Content-Length: 321\r\n\r\n", "content-length behind the window");
    // whitespace in the value, every byte value after "content-length:"
    for (int c = 1; c < 256; c++)
    {
        if (c == '\n' || c == '\r')
            continue;
        std::string s = "POST /x HTTP/1.1\r\nContent-Length:";
        s += (char)c;
        s += "42\r\n\r\n";
        gen(s, "byte before the digits");
    }
}

static void test_deterministic_fuzz(void)
{
    // fixed-seed LCG over a small alphabet that hits every special state of the loop
    static const char alpha[] = "\r\n\r\n:; ,\t=-+0123456789cookiemcs=Content-Lengthxy";
    uint32_t seed = 0x5eed1234u;
    for (int n = 0; n < 1500; n++)
    {
        std::string s;
        if (n % 3 == 0)
            s = "GET / HTTP/1.1\r\n";
        const size_t len = 20 + (n * 37) % 400;
        for (size_t i = 0; i < len; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            s += alpha[(seed >> 16) % (sizeof(alpha) - 1)];
        }
        gen(s, "fuzz");
    }
}

static void test_generated_totals(void)
{
    printf("generated streams compared (all suites above): %zu, total streams %zu, bytes %zu\n", g_generated,
           g_streams, g_bytes);
    TEST_ASSERT_TRUE(g_generated > 1000);
}

// ---------------------------------------------------------------- webReqParse

static std::string g_in; // the spans of the last parse point into this copy, so they outlive a temporary
static WebParseRes P(const std::string &in, WebReq *q, bool cut = false)
{
    g_in = in;
    const std::string &s = g_in;
    const WebParseRes r = webReqParse(s.data(), s.size(), cut, q);
    check_spans(s.data(), s.size() > WEB_HEAD_MAX ? WEB_HEAD_MAX : s.size(), *q);
    return r;
}
static std::string S(WebSpan s) { return std::string(s.p, s.n); }

static void test_parse_methods(void)
{
    WebReq q;
    struct
    {
        const char *m;
        int want;
    } t[] = {{"GET", WM_GET},       {"POST", WM_POST},   {"HEAD", WM_HEAD},   {"OPTIONS", WM_OPTIONS},
             {"get", WM_OTHER},     {"PUT", WM_OTHER},   {"DELETE", WM_OTHER}, {"GETX", WM_OTHER},
             {"GE", WM_OTHER},      {"POSTS", WM_OTHER}, {"", WM_NONE}};
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    {
        P(std::string(t[i].m) + " /x HTTP/1.1\r\n\r\n", &q);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].want, q.method, t[i].m);
    }
    TEST_ASSERT_EQUAL_INT(WP_OK, P("GET /x HTTP/1.1\r\nHost: h\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WM_GET, q.method);
    TEST_ASSERT_EQUAL_INT(1, q.minor);
}

static void test_parse_versions(void)
{
    WebReq q;
    TEST_ASSERT_EQUAL_INT(WP_OK, P("GET /x HTTP/1.0\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(0, q.minor);
    TEST_ASSERT_EQUAL_INT(WP_OK, P("GET /x HTTP/1.1\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(1, q.minor);
    TEST_ASSERT_EQUAL_INT(WP_OK, P("GET /x HTTP/1.1\n\n", &q));
    for (const char *v : {"HTTP/1.2", "HTTP/2.0", "HTTP/2", "HTTP/0.9", "http/1.1", "HTTP/1.11", "HTTP/1.", "HTTP/1.1 ",
                          "HTTP/1.1x", "HTTP", "1.1", "x"})
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(WP_BAD_VERSION, P(std::string("GET /x ") + v + "\r\n\r\n", &q), v);
    }
    // no version at all: "GET /x"
    TEST_ASSERT_EQUAL_INT(WP_BAD_VERSION, P("GET /x\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_UINT(0, q.target.n); // today's split: no target without a second space
    // no space, nothing, no line end
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P("GET\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P("\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P("", &q));
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P("GET /x HTTP/1.1", &q)); // never terminated
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P(" /x HTTP/1.1\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, P("GET  HTTP/1.1\r\n\r\n", &q)); // empty target
    // the version is after the LAST space: a target with a space keeps the split of web_guard.h
    TEST_ASSERT_EQUAL_INT(WP_OK, P("GET /a b HTTP/1.1\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_STRING("/a b", S(q.target).c_str());
    // NULL buffer
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, webReqParse(NULL, 0, false, &q));
}

static void test_parse_target_split(void)
{
    WebReq q;
    P("GET /a/b?x=1&y=2 HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_STRING("/a/b?x=1&y=2", S(q.target).c_str());
    TEST_ASSERT_EQUAL_STRING("/a/b", S(q.path).c_str());
    TEST_ASSERT_EQUAL_STRING("x=1&y=2", S(q.query).c_str());
    P("GET /?page=info HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_STRING("/", S(q.path).c_str());
    TEST_ASSERT_EQUAL_STRING("page=info", S(q.query).c_str());
    P("GET /x? HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_STRING("/x", S(q.path).c_str());
    TEST_ASSERT_EQUAL_UINT(0, q.query.n);
    TEST_ASSERT_TRUE(q.query.p == q.target.p + q.target.n); // after the '?'
    P("GET ? HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_UINT(0, q.path.n); // empty path
    TEST_ASSERT_EQUAL_UINT(0, q.query.n);
    TEST_ASSERT_EQUAL_UINT(1, q.target.n);
    P("GET ?a?b HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_UINT(0, q.path.n);
    TEST_ASSERT_EQUAL_STRING("a?b", S(q.query).c_str());
    P("GET /x HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_STRING("/x", S(q.path).c_str());
    TEST_ASSERT_EQUAL_UINT(0, q.query.n);
    // header-looking text in the query stays inert
    P("GET /x?h=%0d%0aHost:%20evil HTTP/1.1\r\nHost: good\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(1, q.host_n);
    TEST_ASSERT_EQUAL_STRING("good", S(q.host).c_str());
    // bare LF line ends, CR before LF stripped from the version
    TEST_ASSERT_EQUAL_INT(WP_OK, P("POST /rmsend?a=b HTTP/1.1\nHost: h\n\n", &q));
    TEST_ASSERT_EQUAL_INT(WM_POST, q.method);
}

static void test_parse_cut_and_len(void)
{
    WebReq q;
    const std::string s = "GET /x HTTP/1.1\r\nHost: h\r\n\r\n";
    P(s, &q, true);
    TEST_ASSERT_TRUE(q.cut);
    P(s, &q, false);
    TEST_ASSERT_FALSE(q.cut);
    // a window cut mid-header: the partial last line is never judged
    std::string w = "GET /x HTTP/1.1\r\nX: " + rep('a', 1000) + "\r\nHost: goo";
    w.resize(1023);
    TEST_ASSERT_EQUAL_UINT(1023, w.size());
    P(w, &q, true);
    TEST_ASSERT_EQUAL_INT(0, q.host_n);
    // a longer buffer is read only up to WEB_HEAD_MAX
    std::string big = "GET /x HTTP/1.1\r\n" + rep('x', 1200);
    P(big, &q, true);
    TEST_ASSERT_TRUE(q.target.p + q.target.n <= g_in.data() + WEB_HEAD_MAX);
    // len is honoured, not the NUL: "Host" hidden behind len
    const char raw[] = "GET /x HTTP/1.1\r\nHost: h\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(WP_OK, webReqParse(raw, 18, false, &q));
    TEST_ASSERT_EQUAL_INT(0, q.host_n);
    // every prefix of a request: no read past len (ASan) and spans inside
    const std::string full = "POST /rmsend?a=1 HTTP/1.1\r\nHost: h\r\nOrigin: http://h\r\nX-MC: 1\r\nContent-Length: 5\r\n\r\n";
    for (size_t n = 0; n <= full.size(); n++)
    {
        char *copy = (char *)malloc(n == 0 ? 1 : n); // exact size, ASan catches an overread
        memcpy(copy, full.data(), n);
        webReqParse(copy, n, false, &q);
        check_spans(copy, n, q);
        free(copy);
    }
}

static void test_parse_header_facts(void)
{
    WebReq q;
    const std::string base = "GET /x HTTP/1.1\r\n";
    P(base + "Host: a\r\nhost: b\r\nHOST:c\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(3, q.host_n);
    TEST_ASSERT_EQUAL_STRING("a", S(q.host).c_str());
    P(base + "Host:\t  h.local \t\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_STRING("h.local", S(q.host).c_str());
    P(base + "Host: a\r\n b\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(1, q.folded & 1);
    P(base + "Origin: http://a\r\n\tb\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(2, q.folded & 2);
    TEST_ASSERT_EQUAL_INT(1, q.origin_n);
    TEST_ASSERT_EQUAL_STRING("http://a", S(q.origin).c_str());
    P(base + "X-MC: 1\r\nx-mc: 1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(2, q.xmc_n);
    TEST_ASSERT_EQUAL_INT(0, q.xmc_bad);
    P(base + "X-MC: 2\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(1, q.xmc_bad);
    P(base + "X-MC: 1\r\n 1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(1, q.xmc_bad);
    P(base + "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Dest: document\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(0, q.sf_site);
    TEST_ASSERT_EQUAL_INT(1, q.sf_dest);
    P(base + "Sec-Fetch-Site: cross-site\r\nSec-Fetch-Dest: empty\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(1, q.sf_site);
    TEST_ASSERT_EQUAL_INT(2, q.sf_dest);
    P(base + "Sec-Fetch-Dest: document\r\nSec-Fetch-Dest: weird\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(3, q.sf_dest);
    P(base + "Sec-Fetch-Site: none\r\nSec-Fetch-Site: none\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(0, q.sf_site);
    // the request line is never read as a header; text after the blank line is ignored
    P("GET /x?host:%20a HTTP/1.1\r\n\r\nHost: late\r\n", &q);
    TEST_ASSERT_EQUAL_INT(0, q.host_n);
    // a line without a colon resets the fold target
    P(base + "Host: a\r\nnocolon\r\n b\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_INT(0, q.folded);
    // blank: set when the header loop reaches the blank line that ends the head
    P("GET /x HTTP/1.1\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    P("GET /x HTTP/1.1\n\n", &q);
    TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    P("GET /x HTTP/1.1\r\nHost: a\r\nX: b\r\n\r\nHost: late\r\n", &q);
    TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    TEST_ASSERT_EQUAL_INT(1, q.host_n);
    P("GET /x HTTP/1.1\r\nHost: a\r\n", &q); // window ended before the blank line
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    P("GET /x HTTP/1.1\r\nHost: a", &q); // unterminated last line
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    P("GET /x HTTP/1.1", &q); // request line never terminated
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    P("", &q);
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    // a bare CR as the unterminated last line reads as empty, as in web_guard.h's loop; the accumulator
    // never hands such a window over (the head ends only at an LF)
    P("GET /x HTTP/1.1\r\nHost: a\r\n\r", &q);
    TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    P("GET /x HTTP/1.1\r\nHost: a\r\n \r\n", &q); // a line of one space is a fold, not a blank line
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    {
        // cut window: the blank line is behind the window, never seen
        std::string w = "GET /x HTTP/1.1\r\nX: " + rep('a', 1100);
        P(w, &q, true);
        TEST_ASSERT_EQUAL_UINT8(0, q.blank);
        // the window ends exactly after the blank line
        std::string e = "GET /x HTTP/1.1\r\nX: " + rep('a', 1023 - 20 - 4) + "\r\n\r\n";
        TEST_ASSERT_EQUAL_UINT(1023, e.size());
        P(e, &q, true);
        TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    }
    // a bad request line still reports blank, the facts are filled as far as possible
    P("BADLINE\r\nHost: a\r\n\r\n", &q);
    TEST_ASSERT_EQUAL_UINT8(1, q.blank);
    TEST_ASSERT_EQUAL_INT(WP_BAD_REQUEST_LINE, webReqParse(NULL, 0, false, &q));
    TEST_ASSERT_EQUAL_UINT8(0, q.blank);
    // counts saturate
    std::string many = base;
    for (int i = 0; i < 100; i++)
        many += "X-MC: 1\r\n";
    P(many, &q, true);
    TEST_ASSERT_EQUAL_INT(100, q.xmc_n);
    many = base;
    for (int i = 0; i < 70; i++)
        many += "X-MC: 1\r\n";
    P(many, &q);
}

// ---------------------------------------------------------------- B6

static int32_t strict_acc(const std::string &s, bool *bad)
{
    webHeadInit(g_acc);
    for (size_t i = 0; i < s.size(); i++)
        if (webHeadPush(g_acc, s[i]) != WH_MORE)
            break;
    *bad = webHeadBadContentLength(g_acc);
    return g_acc->cl_strict;
}

static void test_b6_strict(void)
{
    struct
    {
        const char *v;
        int32_t strict; // -1: malformed
        long lenient;
    } t[] = {{"5", 5, 5},          {"0", 0, 0},          {"00", 0, 0},          {"007", 7, 7},
             {" 5 ", 5, 5},        {"\t5\t", 5, 5},      {"65535", 65535, 65535}, {"65536", -1, 65536},
             {"+5", -1, 5},        {"-1", -1, -1},       {"-5", -1, -5},        {"12abc", -1, 12},
             {"abc", -1, 0},       {"", -1, 0},          {"   ", -1, 0},        {"1 2", -1, 1},
             {"99999999999", -1, LONG_MAX >= 99999999999LL ? (long)99999999999LL : LONG_MAX}, {"0x10", -1, 0}, {"1e3", -1, 1},  {"5;", -1, 5}};
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    {
        const std::string req = std::string("POST /x HTTP/1.1\r\nContent-Length:") + t[i].v + "\r\n\r\n";
        bool bad;
        const int32_t sv = strict_acc(req, &bad);
        TEST_ASSERT_TRUE_MESSAGE(t[i].lenient == webHeadContentLength(g_acc), t[i].v);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].strict < 0 ? 1 : 0, bad ? 1 : 0, t[i].v);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].strict, sv, t[i].v);
        WebReq q;
        const WebParseRes pr = P(req, &q);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].strict < 0 ? WP_BAD_CONTENT_LENGTH : WP_OK, pr, t[i].v);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].strict, q.content_length, t[i].v);
    }
}

static void test_b6_duplicates(void)
{
    bool bad;
    WebReq q;
    std::string r = "POST /x HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(5, strict_acc(r, &bad));
    TEST_ASSERT_FALSE(bad);
    TEST_ASSERT_EQUAL_INT(WP_OK, P(r, &q));
    r = "POST /x HTTP/1.1\r\nContent-Length: 5\r\ncontent-length: 05\r\n\r\n";
    strict_acc(r, &bad);
    TEST_ASSERT_FALSE(bad); // same value, other spelling
    r = "POST /x HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(5, strict_acc(r, &bad));
    TEST_ASSERT_TRUE(bad);
    TEST_ASSERT_EQUAL_INT(5, webHeadContentLength(g_acc)); // lenient: first wins, as today
    TEST_ASSERT_EQUAL_INT(WP_BAD_CONTENT_LENGTH, P(r, &q));
    TEST_ASSERT_EQUAL_INT(5, q.content_length);
    r = "POST /x HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: x\r\n\r\n";
    strict_acc(r, &bad);
    TEST_ASSERT_TRUE(bad);
    r = "POST /x HTTP/1.1\r\nContent-Length: x\r\nContent-Length: 5\r\n\r\n";
    strict_acc(r, &bad);
    TEST_ASSERT_TRUE(bad);
    TEST_ASSERT_EQUAL_INT(0, webHeadContentLength(g_acc)); // lenient: "x" read as 0, stays
    TEST_ASSERT_EQUAL_INT(WP_BAD_CONTENT_LENGTH, P(r, &q));
    r = "POST /x HTTP/1.1\r\nContent-Length: -1\r\nContent-Length: 5\r\n\r\n";
    strict_acc(r, &bad);
    TEST_ASSERT_TRUE(bad);
    TEST_ASSERT_EQUAL_INT(5, webHeadContentLength(g_acc)); // lenient: -1 is replaced, as today
    // absent
    r = "GET /x HTTP/1.1\r\nHost: h\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(-1, strict_acc(r, &bad));
    TEST_ASSERT_FALSE(bad);
    TEST_ASSERT_EQUAL_INT(-1, webHeadContentLength(g_acc));
    TEST_ASSERT_EQUAL_INT(WP_OK, P(r, &q));
    TEST_ASSERT_EQUAL_INT(-1, q.content_length);
    // a bad request line wins over a bad Content-Length in the result code
    TEST_ASSERT_EQUAL_INT(WP_BAD_VERSION, P("POST /x HTTP/2.0\r\nContent-Length: x\r\n\r\n", &q));
    // folded and prefixed names are no Content-Length
    TEST_ASSERT_EQUAL_INT(WP_OK, P("POST /x HTTP/1.1\r\n Content-Length: x\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WP_OK, P("POST /x HTTP/1.1\r\nX-Content-Length: x\r\n\r\n", &q));
    TEST_ASSERT_EQUAL_INT(WP_OK, P("POST /x HTTP/1.1\r\nContent-Length : x\r\n\r\n", &q));
}

static void test_head_init_wipes(void)
{
    webHeadInit(g_acc);
    const std::string s = "GET / HTTP/1.1\r\nCookie: mcs=secret\r\nContent-Length: 9\r\nX: part";
    for (size_t i = 0; i < s.size(); i++)
        webHeadPush(g_acc, s[i]);
    TEST_ASSERT_TRUE(g_acc->cookie_len > 0 && g_acc->line_len > 0 && g_acc->content_length == 9);
    webHeadInit(g_acc);
    TEST_ASSERT_EQUAL_UINT(0, g_acc->head_len);
    TEST_ASSERT_EQUAL_UINT(0, g_acc->line_len);
    TEST_ASSERT_EQUAL_UINT(0, g_acc->cookie_len);
    TEST_ASSERT_EQUAL_INT(-1, g_acc->content_length);
    TEST_ASSERT_EQUAL_UINT(0, g_acc->flags);
    for (size_t i = 0; i < sizeof(g_acc->cookie); i++)
        TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)g_acc->cookie[i]);
    for (size_t i = 0; i < sizeof(g_acc->line); i++)
        TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)g_acc->line[i]);
    const WebSpan ck = webHeadCookie(g_acc);
    TEST_ASSERT_EQUAL_UINT(0, ck.n);
    // a finished Cookie line is wiped from `line`
    const std::string c = "Cookie: mcs=abc\r\n";
    for (size_t i = 0; i < c.size(); i++)
        webHeadPush(g_acc, c[i]);
    TEST_ASSERT_EQUAL_UINT(0, g_acc->line_len);
    for (size_t i = 0; i < sizeof(g_acc->line); i++)
        TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)g_acc->line[i]);
    TEST_ASSERT_EQUAL_STRING("mcs=abc", std::string(webHeadCookie(g_acc).p, webHeadCookie(g_acc).n).c_str());
}

int main(int, char **)
{
    g_acc = (WebHeadAcc *)calloc(1, sizeof(WebHeadAcc));
    UNITY_BEGIN();
    RUN_TEST(test_corpus_matches_reference);
    RUN_TEST(test_corpus_request_line_split);
    RUN_TEST(test_window_edges);
    RUN_TEST(test_long_lines);
    RUN_TEST(test_line_end_games);
    RUN_TEST(test_cookie_cases);
    RUN_TEST(test_content_length_cases);
    RUN_TEST(test_deterministic_fuzz);
    RUN_TEST(test_generated_totals);
    RUN_TEST(test_parse_methods);
    RUN_TEST(test_parse_versions);
    RUN_TEST(test_parse_target_split);
    RUN_TEST(test_parse_cut_and_len);
    RUN_TEST(test_parse_header_facts);
    RUN_TEST(test_b6_strict);
    RUN_TEST(test_b6_duplicates);
    RUN_TEST(test_head_init_wipes);
    const int rc = UNITY_END();
    free(g_acc);
    return rc;
}
