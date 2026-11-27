// Concrete request heads for M_PARSE_CORPUS (included by harness.cpp). Every input is a constant, every observable field of WebReq is stated.
// Under ESBMC this is symbolic execution without a free choice: an exhaustive enumeration of the listed inputs (memory safety of every read,
// the spans, the verdict), not a proof over a class of inputs. The symbolic jobs of the group cover the classes.
// Columns of CASE: lit, cut, res, method, minor, target.n, path.n, host_n, host.n, origin_n, origin.n, xmc_n, xmc_bad, sf_site, sf_dest, folded, blank,
// content_length (strict, -1 absent). host / origin spans are the first value. Every input is at most WEB_HEAD_MAX = 64 bytes unless a PAD case says so.
#define R_OK WRQ_OK
#define R_RL WRQ_BAD_REQUEST_LINE
#define R_VER WRQ_BAD_VERSION
#define R_CL WRQ_BAD_CONTENT_LENGTH

struct Ex
{
    int res, method, minor, tn, pn, hn, hl, on, ol, xn, xb, ss, sd, fo, bl;
    long cl;
};

static void expectReq(const WebReq &q, WebParseRes res, bool cut, const Ex &x)
{
    assert((int)res == x.res);
    assert(q.cut == cut);
    assert((int)q.method == x.method && (int)q.minor == x.minor);
    assert((int)q.target.n == x.tn && (int)q.path.n == x.pn);
    assert((int)q.host_n == x.hn && (int)q.host.n == x.hl);
    assert((int)q.origin_n == x.on && (int)q.origin.n == x.ol);
    assert((int)q.xmc_n == x.xn && (int)q.xmc_bad == x.xb);
    assert((int)q.sf_site == x.ss && (int)q.sf_dest == x.sd);
    assert((int)q.folded == x.fo && (int)q.blank == x.bl);
    assert((long)q.content_length == x.cl);
}

static void one(const char *s, size_t n, bool cut, const Ex &x)
{
    assert(n <= (size_t)WEB_HEAD_MAX); // a plain case never relies on the clamp (PAD cases do)
    char *buf = new char[n > 0 ? n : 1];
    for (size_t i = 0; i < n; i++)
        buf[i] = s[i];
    WebReq q;
    WebParseRes res = webReqParse(buf, n, cut, &q);
    checkReq(buf, n, q, res);
    expectReq(q, res, cut, x);
    delete[] buf;
}

// prefix, then pad bytes up to `total` bytes (total may exceed WEB_HEAD_MAX: the parser clamps)
static void onePad(const char *pre, size_t pn, char pad, size_t total, bool cut, const Ex &x)
{
    char *buf = new char[total];
    for (size_t i = 0; i < total; i++)
        buf[i] = i < pn ? pre[i] : pad;
    WebReq q;
    WebParseRes res = webReqParse(buf, total, cut, &q);
    checkReq(buf, total, q, res);
    expectReq(q, res, cut, x);
    delete[] buf;
}

#define CASE(lit, cut, ...)                       \
    do                                            \
    {                                             \
        const Ex ex_ = {__VA_ARGS__};             \
        one(lit, sizeof(lit) - 1, cut, ex_);      \
    } while (0)
#define PAD(pre, pad, total, cut, ...)                       \
    do                                                       \
    {                                                        \
        const Ex ex_ = {__VA_ARGS__};                        \
        onePad(pre, sizeof(pre) - 1, pad, total, cut, ex_);  \
    } while (0)
#define RLN "GET / HTTP/1.1\n"

#if CORPUS == 1
// request line: methods, targets, versions (every position of "HTTP/1.1" damaged once), unterminated, cut
static void corpus()
{
    //   res   meth       min tn pn hn hl on ol xn xb ss sd fo bl cl
    CASE("GET / HTTP/1.1\r\n\r\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1.0\n\n", false, R_OK, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("POST /a?b=c HTTP/1.1\r\nHost: x\r\n\r\n", false, R_OK, WM_POST, 1, 6, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("HEAD / HTTP/1.1\n", false, R_OK, WM_HEAD, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
    CASE("OPTIONS * HTTP/1.1\r\n\r\n", false, R_RL, WM_OPTIONS, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);    // B3: asterisk-form not served
    CASE("PUT /x HTTP/1.1\n\n", false, R_OK, WM_OTHER, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("get / HTTP/1.1\n\n", false, R_OK, WM_OTHER, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1.1", false, R_RL, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);         // request line never terminated
    CASE("GET / HTTP/1.1\r", false, R_RL, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);       // CR, no LF
    CASE("GET /\n", false, R_VER, WM_GET, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);               // no version: sp2 == sp1
    CASE("GET\n", false, R_RL, WM_GET, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);                  // no space at all
    CASE("\n", false, R_RL, WM_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);                    // empty request line
    CASE(" / HTTP/1.1\n", false, R_RL, WM_NONE, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);         // empty method
    CASE("GET  HTTP/1.1\n\n", false, R_RL, WM_GET, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);      // empty target
    CASE("GET / HTTP/1.2\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/2\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / http/1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);    // case sensitive
    CASE("GET / HTTP/1.11\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / xTTP/1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HxTP/1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTxP/1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTx/1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTPx1.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/x.1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1x1\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1.x\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1.1\r\r\n\n", false, R_VER, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1); // only one CR is dropped
    CASE("GET /a b HTTP/1.1\n\n", false, R_RL, WM_GET, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);   // B3: blank inside the target
    CASE("GET  /?page=info  HTTP/1.1\n\n", false, R_RL, WM_GET, 0, 13, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1); // B3: double blanks (corpus mut-427)
    CASE("GET ?page=info HTTP/1.1\n\n", false, R_RL, WM_GET, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);   // B3: target without a leading slash
    CASE("GET x HTTP/1.1\n\n", false, R_RL, WM_GET, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET / HTTP/1.1 \n\n", false, R_VER, WM_GET, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);    // trailing blank: the version is empty, its verdict wins
    CASE("GET /? HTTP/1.1\n\n", false, R_OK, WM_GET, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET /a?b?c HTTP/1.1\n\n", false, R_OK, WM_GET, 1, 6, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE("GET /\0a HTTP/1.1\n\n", false, R_OK, WM_GET, 1, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);  // NUL inside the target
    CASE("GET / HTTP/1.1\nHost: z", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1); // unterminated last line, window not full
    CASE("GET / HTTP/1.1\n\r", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);      // today: a lone CR as last line counts as blank
    // window full (WEB_HEAD_MAX = 64, 70 bytes arrive, cut flag set): the last line is cut mid-line and judged by nothing
    PAD("GET / HTTP/1.1\nHost:", 'a', 70, true, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
    PAD("GET / HTTP/1.1\nContent-Length: ", '7', 70, true, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
}
#elif CORPUS == 9
static void corpus()
{
    CASE("GET / HTTP/1.1\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
}
#elif CORPUS == 2
// header lines: Host, Origin, X-MC, Sec-Fetch-Site/-Dest, obs-fold after each, no colon, blank line, CR handling
static void corpus()
{
    //   res   meth       min tn pn hn hl on ol xn xb ss sd fo bl cl
    CASE(RLN "Host: a\nhost: b\n\n", false, R_OK, WM_GET, 1, 1, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1); // the first value is reported
    CASE(RLN "hOsT:\ta \t\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Host : a\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);          // name with a blank is another name
    CASE(RLN "Host: a\r\n\r\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Host: a\r\r\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 1, -1);      // one CR dropped, the other is value
    CASE(RLN "Host: a:b\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Origin: http://x\nORIGIN: y\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 2, 8, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "X-MC: 1\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -1);
    CASE(RLN "X-MC: 2\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, -1);
    CASE(RLN "X-MC:1\nx-mc: 1\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, -1);
    CASE(RLN "X-MC: 11\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, -1);
    CASE(RLN "X-MC:\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, -1);
    CASE(RLN "Host: a\n b\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, -1);      // fold of Host
    CASE(RLN "Host: a\n b\n\tc\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, -1);
    CASE(RLN "Origin: o\n\tp\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 2, 1, -1);   // fold of Origin
    CASE(RLN "X-MC: 1\n 1\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, -1);      // fold of X-MC
    CASE(RLN "Sec-Fetch-Site: none\n x\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: document\n x\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, -1);
    CASE(RLN "A: b\n c\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);          // fold after an unknown header: nothing
    CASE(RLN "foo\n x\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);          // fold after a colon-less line: nothing
    CASE(RLN " x\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);               // fold with no header before it
    CASE(RLN "Sec-Fetch-Site: same-origin\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "SEC-FETCH-SITE: SAME-ORIGIN\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Site: none\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Site: cross-site\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Site:\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Site:  none \t\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Site: none\nSec-Fetch-Site: cross-site\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: document\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: empty\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: iframe\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: empty\nSec-Fetch-Dest: document\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: document\nSec-Fetch-Dest: empty\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, -1);
    CASE(RLN "Sec-Fetch-Dest: empty\nSec-Fetch-Dest: iframe\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, -1);
    CASE(RLN "foo\nHost: a\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN ": a\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);               // empty name
    CASE(RLN "Host: a\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1);            // no blank line yet
    CASE(RLN "Cookie: mcs=1\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);    // not a fact of the parser
    CASE(RLN "\nHost: a\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);          // the blank line ends the head
    CASE(RLN "Sec-Fetch-Site: same-origin\n b\nHost: a\n\n", false, R_OK, WM_GET, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, -1);
    // the header facts are filled even when the request line is bad
    CASE("GET / HTTP/9\nHost: a\n\n", false, R_VER, WM_GET, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, -1);
}
#elif CORPUS == 3
// Content-Length (strict judgement of webReqParse) and the window limit
static void corpus()
{
    //   res   meth       min tn pn hn hl on ol xn xb ss sd fo bl cl
    CASE(RLN "Content-Length: 5\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "content-length:0\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0);
    CASE(RLN "CONTENT-LENGTH:  12 \t\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 12);
    CASE(RLN "Content-Length: abc\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: -1\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: +5\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: 65535\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 65535);
    CASE(RLN "Content-Length: 65536\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: 99999999999999999999\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: 00005\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: 5 5\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length:\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    CASE(RLN "Content-Length: 5\r\n\r\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: 5\nContent-Length: 5\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: 5\nContent-Length: 05\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: 5\nContent-Length: 6\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: 5\nContent-Length: x\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length: x\nContent-Length: 5\n\n", false, R_CL, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 5);
    CASE(RLN "Content-Length : 5\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);  // name with a blank: no Content-Length
    CASE(RLN "Content-Length: 7\n x\n\n", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 7); // a fold after it changes nothing
    // a bad request line wins over a bad Content-Length (the verdict of the request line is kept)
    CASE("GET /\nContent-Length: x\n\n", false, R_VER, WM_GET, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -1);
    // window: 64 bytes, a Content-Length line cut by the window is not judged, the same line complete is
    PAD("GET / HTTP/1.1\nContent-Length: ", '7', 64, true, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
    CASE("GET / HTTP/1.1\nContent-Length: 7", false, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7);
    PAD("GET / HTTP/1.1\nContent-Length: 7\n", 'x', 64, true, R_OK, WM_GET, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7);
}
#endif
