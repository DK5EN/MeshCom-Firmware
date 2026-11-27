// test_web_query -- src/web_functions/web_query.h (FORMAL-02 wave 7a, seams doc 6.4).
// (1) unit cases per function; (2) comparison against String-free copies of today's extraction code
// (web_functions.cpp call_function ~:5031, setparam ~:5106, getparam ~:5237, send_message ~:4875,
// sub_page_neighbours `indexOf("&x=")`) over every test_web_diff corpus request and generated cases. Each query is
// compared only with the handler its PATH selects (today's router: /setparam/, /callfunction/, /getparam/,
// "/?sendmessage", "/?page=neighbours"), so every count below is a count of requests that reach that handler.
// Every difference must be in kDiffTable (ID + count); the table is asserted exact.
//
// url_decode.cpp is compiled by including it here (env native_web_query has test_build_src = no).
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <unity.h>

#include "../../src/url_decode.cpp"
#include "../../src/web_functions/web_query.h"
#include "../support/repo_file.h"

void setUp() {}
void tearDown() {}

static WebSpan sp(const char *s)
{
    WebSpan r;
    r.p = s;
    r.n = (uint16_t)strlen(s);
    return r;
}
static std::string str(WebSpan s) { return std::string(s.p ? s.p : "", s.n); }

// ---------- (1) unit cases ----------
void test_first(void)
{
    WebSpan k, v;
    TEST_ASSERT_FALSE(webQueryFirst(sp(""), &k, &v));
    TEST_ASSERT_FALSE(webQueryFirst(sp("setcall"), &k, &v)); // D2
    TEST_ASSERT_TRUE(webQueryFirst(sp("a=b"), &k, &v));
    TEST_ASSERT_EQUAL_STRING("a", str(k).c_str());
    TEST_ASSERT_EQUAL_STRING("b", str(v).c_str());
    TEST_ASSERT_TRUE(webQueryFirst(sp("a=b=c&d=e"), &k, &v)); // '=' and '&' stay in the value
    TEST_ASSERT_EQUAL_STRING("a", str(k).c_str());
    TEST_ASSERT_EQUAL_STRING("b=c&d=e", str(v).c_str());
    TEST_ASSERT_TRUE(webQueryFirst(sp("a="), &k, &v));
    TEST_ASSERT_EQUAL_UINT(0, v.n);
    TEST_ASSERT_TRUE(webQueryFirst(sp("=x"), &k, &v));
    TEST_ASSERT_EQUAL_UINT(0, k.n);
    TEST_ASSERT_EQUAL_STRING("x", str(v).c_str());
    WebSpan nul = {NULL, 0};
    TEST_ASSERT_FALSE(webQueryFirst(nul, &k, &v));
}

void test_get(void)
{
    WebSpan v;
    TEST_ASSERT_FALSE(webQueryGet(sp(""), "x", &v));
    TEST_ASSERT_FALSE(webQueryGet(sp("page=n"), "x", &v));
    TEST_ASSERT_TRUE(webQueryGet(sp("page=n&x=64&y=1"), "x", &v));
    TEST_ASSERT_EQUAL_STRING("64", str(v).c_str());
    TEST_ASSERT_TRUE(webQueryGet(sp("x=1"), "x", &v)); // first field
    TEST_ASSERT_EQUAL_STRING("1", str(v).c_str());
    TEST_ASSERT_FALSE(webQueryGet(sp("xx=1"), "x", &v)); // key is a prefix of another key
    TEST_ASSERT_FALSE(webQueryGet(sp("a=1&xx=2"), "x", &v));
    TEST_ASSERT_FALSE(webQueryGet(sp("a=1&x"), "x", &v)); // no '='
    TEST_ASSERT_TRUE(webQueryGet(sp("a=1&x="), "x", &v)); // empty value
    TEST_ASSERT_EQUAL_UINT(0, v.n);
    TEST_ASSERT_TRUE(webQueryGet(sp("x=1&x=2"), "x", &v)); // repeated: first
    TEST_ASSERT_EQUAL_STRING("1", str(v).c_str());
    TEST_ASSERT_FALSE(webQueryGet(sp("ax=1"), "x", &v)); // not after '&'
    TEST_ASSERT_FALSE(webQueryGet(sp("a=x=1"), "x", &v)); // '=' in a value is not a field start
    TEST_ASSERT_FALSE(webQueryGet(sp("x=1"), "", &v));
    TEST_ASSERT_FALSE(webQueryGet(sp("x=1"), NULL, &v));
    TEST_ASSERT_FALSE(webQueryGet(sp("x"), "x", &v)); // exact end, nothing after the key
    TEST_ASSERT_TRUE(webQueryGet(sp("tocall=A&message=hi"), "message", &v));
    TEST_ASSERT_EQUAL_STRING("hi", str(v).c_str());
}

void test_tail(void)
{
    WebSpan v;
    TEST_ASSERT_FALSE(webQueryTail(sp(""), "message", &v));
    TEST_ASSERT_TRUE(webQueryTail(sp("p=s&tocall=A&message=a&b=c=d"), "message", &v));
    TEST_ASSERT_EQUAL_STRING("a&b=c=d", str(v).c_str()); // '&' in the value
    TEST_ASSERT_TRUE(webQueryTail(sp("message="), "message", &v));
    TEST_ASSERT_EQUAL_UINT(0, v.n);
    TEST_ASSERT_FALSE(webQueryTail(sp("messagex=1"), "message", &v));
    TEST_ASSERT_TRUE(webQueryTail(sp("messagex=1&message=2&message=3"), "message", &v));
    TEST_ASSERT_EQUAL_STRING("2&message=3", str(v).c_str()); // first match
    TEST_ASSERT_TRUE(webQueryTail(sp("a=1&b=2"), "b", &v));
    TEST_ASSERT_EQUAL_STRING("2", str(v).c_str());
}

void test_decode(void)
{
    char o[8];
    // exact fit, one over
    TEST_ASSERT_EQUAL_UINT(4, webFieldDecode(sp("abcd"), o, 4, URLD_LEGACY));
    TEST_ASSERT_EQUAL_STRING("abcd", o);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, webFieldDecode(sp("abcde"), o, 4, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT((size_t)-1, webFieldDecode(sp("abcde"), o, 4, URLD_STRICT));
    // raw longer than cap but decoded fits
    TEST_ASSERT_EQUAL_UINT(3, webFieldDecode(sp("%41%42%43"), o, 4, URLD_LEGACY));
    TEST_ASSERT_EQUAL_STRING("ABC", o);
    // empty
    TEST_ASSERT_EQUAL_UINT(0, webFieldDecode(sp(""), o, 4, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT(0, o[0]);
    WebSpan nul = {NULL, 0};
    TEST_ASSERT_EQUAL_UINT(0, webFieldDecode(nul, o, 4, URLD_LEGACY));
    // cap 0
    TEST_ASSERT_EQUAL_UINT(0, webFieldDecode(sp(""), o, 0, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT((size_t)-1, webFieldDecode(sp("a"), o, 0, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT(0, webFieldDecode(sp("a"), o, 0, URLD_TRUNC));
    // policies: LEGACY '+' is a space, STRICT keeps '+', STRICT rejects %00, LEGACY drops it
    TEST_ASSERT_EQUAL_UINT(3, webFieldDecode(sp("a+b"), o, 7, URLD_LEGACY));
    TEST_ASSERT_EQUAL_STRING("a b", o);
    TEST_ASSERT_EQUAL_UINT(3, webFieldDecode(sp("a+b"), o, 7, URLD_STRICT));
    TEST_ASSERT_EQUAL_STRING("a+b", o);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, webFieldDecode(sp("a%00b"), o, 7, URLD_STRICT));
    TEST_ASSERT_EQUAL_UINT(2, webFieldDecode(sp("a%00b"), o, 7, URLD_LEGACY));
    // TRUNC: cut at cap, never inside a UTF-8 sequence
    TEST_ASSERT_EQUAL_UINT(4, webFieldDecode(sp("abcdefg"), o, 4, URLD_TRUNC));
    TEST_ASSERT_EQUAL_STRING("abcd", o);
    TEST_ASSERT_EQUAL_UINT(2, webFieldDecode(sp("ab%C3%A4"), o, 3, URLD_TRUNC)); // 2-byte char would be cut: dropped
    TEST_ASSERT_EQUAL_STRING("ab", o);
    TEST_ASSERT_EQUAL_UINT(2, webFieldDecode(sp("ab%C3%A4"), o, 2, URLD_TRUNC));
    TEST_ASSERT_EQUAL_UINT(4, webFieldDecode(sp("ab%C3%A4"), o, 4, URLD_TRUNC)); // fits exactly
    TEST_ASSERT_EQUAL_UINT(2, webFieldDecode(sp("ab%E2%82%AC"), o, 4, URLD_TRUNC)); // 3-byte char cut at 4 -> before it
    TEST_ASSERT_EQUAL_STRING("ab", o);
    // long raw input: no temporary buffer, the cap alone decides
    std::string big(700, 'a');
    WebSpan b = {big.data(), (uint16_t)big.size()};
    char o2[16];
    o2[0] = 'x';
    TEST_ASSERT_EQUAL_UINT((size_t)-1, webFieldDecode(b, o2, 8, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT(0, o2[0]); // no partial value left behind
    TEST_ASSERT_EQUAL_UINT(8, webFieldDecode(b, o2, 8, URLD_TRUNC));
    TEST_ASSERT_EQUAL_STRING("aaaaaaaa", o2);
    // long raw, short decoded (the old 640-byte raw cap is gone)
    std::string pc;
    for (int i = 0; i < 300; i++)
        pc += "%41";
    WebSpan p = {pc.data(), (uint16_t)pc.size()};
    char o3[301];
    TEST_ASSERT_EQUAL_UINT(300, webFieldDecode(p, o3, 300, URLD_LEGACY));
    TEST_ASSERT_EQUAL_UINT(0, strspn(o3, "A") != 300);
}

void test_until(void)
{
    WebSpan v;
    TEST_ASSERT_FALSE(webQueryUntil(sp(""), "tocall", "&message", &v));
    TEST_ASSERT_FALSE(webQueryUntil(sp("sendmessage&message=m"), "tocall", "&message", &v));
    TEST_ASSERT_TRUE(webQueryUntil(sp("sendmessage&tocall=A&message=hi"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_STRING("A", str(v).c_str());
    // today's rule: ends at "&message", not at the next '&'
    TEST_ASSERT_TRUE(webQueryUntil(sp("sendmessage&tocall=A&x=1&message=hi"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_STRING("A&x=1", str(v).c_str());
    TEST_ASSERT_TRUE(webQueryGet(sp("sendmessage&tocall=A&x=1&message=hi"), "tocall", &v));
    TEST_ASSERT_EQUAL_STRING("A", str(v).c_str()); // webQueryGet would cut earlier
    // stop absent: to the end
    TEST_ASSERT_TRUE(webQueryUntil(sp("tocall=A&x=1"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_STRING("A&x=1", str(v).c_str());
    // stop is a prefix match, as indexOf("&message") is ("&messagex" ends it too)
    TEST_ASSERT_TRUE(webQueryUntil(sp("a&tocall=A&messagex=1&message=2"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_STRING("A", str(v).c_str());
    // empty value, stop right behind the '='
    TEST_ASSERT_TRUE(webQueryUntil(sp("a&tocall=&message=2"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_UINT(0, v.n);
    // stop BEFORE the key is not looked at (searched from the value start)
    TEST_ASSERT_TRUE(webQueryUntil(sp("a&message=m&tocall=B"), "tocall", "&message", &v));
    TEST_ASSERT_EQUAL_STRING("B", str(v).c_str());
    // NULL / empty stop: to the end; key longer than the literal bound never matches
    TEST_ASSERT_TRUE(webQueryUntil(sp("tocall=A&b=1"), "tocall", NULL, &v));
    TEST_ASSERT_EQUAL_STRING("A&b=1", str(v).c_str());
    TEST_ASSERT_TRUE(webQueryUntil(sp("tocall=A&b=1"), "tocall", "", &v));
    TEST_ASSERT_EQUAL_STRING("A&b=1", str(v).c_str());
    std::string lk(WEB_LIT_MAX + 1, 'k');
    std::string lq = lk + "=1";
    TEST_ASSERT_FALSE(webQueryGet(sp(lq.c_str()), lk.c_str(), &v));
}

// ---------- (2) reference = today's extraction on a query ----------
static std::string refDecode(const std::string &s)
{
    std::vector<char> b(s.begin(), s.end());
    b.push_back(0);
    urlDecode(b.data(), s.size(), URLD_LEGACY); // decodeURLPercentCoding(), web_functions.cpp:1542
    return std::string(b.data());
}
static std::string trim(std::string s)
{
    size_t a = 0, e = s.size();
    while (a < e && (unsigned char)s[a] <= ' ')
        a++;
    while (e > a && (unsigned char)s[e - 1] <= ' ')
        e--;
    return s.substr(a, e - a);
}
// setparam :5107-5108: substring(0, indexOf("=")) / substring(indexOf("=")+1); -1 clamps to the whole string
static void refSetparam(const std::string &q, std::string &name, std::string &val)
{
    size_t i = q.find('=');
    name = refDecode(i == std::string::npos ? q : q.substr(0, i));
    val = refDecode(i == std::string::npos ? q : q.substr(i + 1));
}
// call_function / getparam: `indexOf("=") > 0` split, else the whole text is the name
static void refCall(const std::string &q, std::string &name, std::string &par)
{
    size_t i = q.find('=');
    if (i != std::string::npos && i > 0)
    {
        name = refDecode(q.substr(0, i));
        par = refDecode(q.substr(i + 1));
    }
    else
    {
        name = refDecode(q);
        par = "";
    }
}

// difference kinds, counted per handler that the request path selects
struct DiffCount
{
    int b4, b7_setparam, b7_call, b7_tocall, b7_message, swapped;
    int routed[5]; // setparam, callfunction, getparam, sendmessage, neighbours
};
static DiffCount g_corpus = {0, 0, 0, 0, 0, 0, {0, 0, 0, 0, 0}}, g_gen = {0, 0, 0, 0, 0, 0, {0, 0, 0, 0, 0}};

struct Case
{
    std::string path, query;
};

// setparam: name cap 32, value cap 200
static void cmpSetparam(const std::string &q, DiffCount *dc)
{
    WebSpan Q;
    Q.p = q.data();
    Q.n = (uint16_t)q.size();
    WebSpan k, v;
    char nb[33], vb[201];
    std::string rn, rv;
    refSetparam(q, rn, rv);
    if (!webQueryFirst(Q, &k, &v))
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, (int)q.find('='), q.c_str());
        dc->b4++; // B4 / D2: no '=' -> 422 instead of name == value
        return;
    }
    size_t nl = webFieldDecode(k, nb, 32, URLD_LEGACY);
    TEST_ASSERT_NOT_EQUAL_MESSAGE((size_t)-1, nl, q.c_str()); // no name over 32 in the inputs
    TEST_ASSERT_EQUAL_STRING_MESSAGE(rn.c_str(), nb, q.c_str());
    size_t vl = webFieldDecode(v, vb, 200, URLD_LEGACY);
    if (vl == (size_t)-1)
    {
        TEST_ASSERT_TRUE_MESSAGE(rv.size() > 200, q.c_str());
        dc->b7_setparam++; // B7 / D11: over 200 bytes -> 422 instead of silent truncation
    }
    else
        TEST_ASSERT_EQUAL_STRING_MESSAGE(rv.c_str(), vb, q.c_str());
}

// call_function / getparam: the wrapper keeps today's "no '=' (or '=' first) means the whole text is the name".
// getparam reads the name only (the value is ignored there), so it has no value cap and no B7 row.
static void cmpCall(const std::string &q, bool withValue, DiffCount *dc)
{
    WebSpan Q;
    Q.p = q.data();
    Q.n = (uint16_t)q.size();
    WebSpan k, v;
    char nb[33], pb[65];
    std::string rn, rp;
    refCall(q, rn, rp);
    std::string tn, tp;
    if (webQueryFirst(Q, &k, &v) && k.n > 0)
    {
        size_t nl = webFieldDecode(k, nb, 32, URLD_LEGACY);
        TEST_ASSERT_NOT_EQUAL_MESSAGE((size_t)-1, nl, q.c_str());
        tn = nb;
        if (withValue)
        {
            size_t pl = webFieldDecode(v, pb, 64, URLD_LEGACY);
            if (pl == (size_t)-1)
            {
                TEST_ASSERT_TRUE_MESSAGE(rp.size() > 64, q.c_str());
                dc->b7_call++;
                tp = rp; // not compared
            }
            else
                tp = pb;
        }
        else
            tp = rp;
    }
    else
    {
        tn = refDecode(q);
        tp = "";
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(rn.c_str(), tn.c_str(), q.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(rp.c_str(), tp.c_str(), q.c_str());
}

// send_message :4875-4882 on the query of "/?sendmessage...". Today needs "&tocall=" and "&message=" (else "sendmessage failed"),
// tocall = substring(indexOf("&tocall=")+8, indexOf("&message")), message = substring(indexOf("&message=")+9).
// The module: webQueryUntil(tocall, stop "&message") + webQueryTail(message). It must succeed exactly when today does.
static void cmpSend(const std::string &q, DiffCount *dc)
{
    WebSpan Q;
    Q.p = q.data();
    Q.n = (uint16_t)q.size();
    WebSpan g, t;
    const size_t tp = q.find("&tocall="), mp = q.find("&message=");
    const bool todayOk = tp != std::string::npos && mp != std::string::npos;
    const bool modOk = webQueryUntil(Q, "tocall", "&message", &g) && webQueryTail(Q, "message", &t);
    TEST_ASSERT_EQUAL_INT_MESSAGE(todayOk, modOk, q.c_str()); // missing field: both fail ("sendmessage failed" / 422)
    if (!todayOk)
        return;
    // "&message=" present implies "&message" present: today's indexOf never returns -1 here
    const size_t mm = q.find("&message");
    TEST_ASSERT_TRUE_MESSAGE(mm != std::string::npos && mm <= mp, q.c_str());
    // what the module must return: from after "&tocall=" to the next "&message" after it, else to the end
    const size_t me = q.find("&message", tp + 8);
    const std::string expTocall = q.substr(tp + 8, (me == std::string::npos ? q.size() : me) - (tp + 8));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expTocall.c_str(), str(g).c_str(), q.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(q.substr(mp + 9).c_str(), str(t).c_str(), q.c_str());
    const std::string rm = q.substr(mp + 9);
    std::string rt;
    if (mm >= tp + 8)
        rt = q.substr(tp + 8, mm - (tp + 8));
    else
    {
        // "&message" comes before "&tocall=": substring(left > right) swaps its bounds, today's tocall is the text
        // from that "&message" up to and including "&tocall=" (garbage in the destination). The module takes the value.
        rt = q.substr(mm, tp + 8 - mm);
        TEST_ASSERT_TRUE_MESSAGE(rt != expTocall, q.c_str());
        dc->swapped++;
    }
    char tb[17], mb[153];
    size_t tl = webFieldDecode(g, tb, 16, URLD_LEGACY);
    size_t ml = webFieldDecode(t, mb, 152, URLD_LEGACY);
    if (tl == (size_t)-1)
    {
        TEST_ASSERT_TRUE_MESSAGE(refDecode(expTocall).size() > 16, q.c_str());
        dc->b7_tocall++;
    }
    else if (mm >= tp + 8)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(trim(refDecode(rt)).c_str(), trim(tb).c_str(), q.c_str());
    else
        TEST_ASSERT_EQUAL_STRING_MESSAGE(trim(refDecode(expTocall)).c_str(), trim(tb).c_str(), q.c_str());
    if (ml == (size_t)-1)
    {
        TEST_ASSERT_TRUE_MESSAGE(refDecode(rm).size() > 152, q.c_str());
        dc->b7_message++;
    }
    else
        TEST_ASSERT_EQUAL_STRING_MESSAGE(trim(refDecode(rm)).c_str(), trim(mb).c_str(), q.c_str());
}

// sub_page_neighbours: indexOf("&x=") then toInt (atol)
static void cmpNeighbours(const std::string &q)
{
    WebSpan Q;
    Q.p = q.data();
    Q.n = (uint16_t)q.size();
    size_t xp = q.find("&x=");
    long rx = xp == std::string::npos ? -1 : atol(q.c_str() + xp + 3);
    WebSpan g;
    long tx = -1;
    if (xp != std::string::npos)
    {
        TEST_ASSERT_TRUE_MESSAGE(webQueryGet(Q, "x", &g), q.c_str());
        std::string s = str(g);
        tx = atol(s.c_str());
    }
    else
        TEST_ASSERT_FALSE_MESSAGE(webQueryGet(Q, "x", &g), q.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(rx, tx, q.c_str());
}

static bool startsWith(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

// today's router (web_functions.cpp:1377-1400, :1444) on the parsed path: the handler this request reaches, -1 none
static void compareCase(const Case &c, DiffCount *dc)
{
    if (c.path == "/setparam/")
    {
        dc->routed[0]++;
        cmpSetparam(c.query, dc);
    }
    else if (c.path == "/callfunction/")
    {
        dc->routed[1]++;
        cmpCall(c.query, true, dc);
    }
    else if (c.path == "/getparam/")
    {
        dc->routed[2]++;
        cmpCall(c.query, false, dc);
    }
    else if (c.path == "/" && startsWith(c.query, "sendmessage"))
    {
        dc->routed[3]++;
        cmpSend(c.query, dc);
    }
    else if (c.path == "/" && startsWith(c.query, "page=neighbours"))
    {
        dc->routed[4]++;
        cmpNeighbours(c.query);
    }
}

static std::vector<Case> corpusCases()
{
    std::vector<Case> out;
    std::string dir = repo_path("test/test_web_diff/corpus");
    DIR *d = opendir(dir.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(d, "corpus dir");
    std::vector<std::string> names;
    while (struct dirent *e = readdir(d))
    {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".req") == 0)
            names.push_back(n);
    }
    closedir(d);
    for (size_t i = 0; i < names.size(); i++)
    {
        std::string req = repo_slurp(("test/test_web_diff/corpus/" + names[i]).c_str());
        WebReq r;
        webReqParse(req.data(), req.size(), false, &r);
        Case c;
        c.path = str(r.path);
        c.query = str(r.query);
        out.push_back(c);
    }
    return out;
}

static std::vector<Case> generated()
{
    // queries every name/value handler sees, run against /setparam/, /callfunction/ and /getparam/
    std::vector<std::string> nv;
    nv.push_back("");
    nv.push_back("setcall");
    nv.push_back("=v");
    nv.push_back("a=b");
    nv.push_back("a=");
    nv.push_back("a=b=c");
    nv.push_back("a=b&c=d");
    nv.push_back("mcset=%C3%A4+x");
    nv.push_back("k=%zz");
    nv.push_back("k=%00x");
    nv.push_back("k=a%0D%0Ab");
    nv.push_back("reboot");
    nv.push_back("k=" + std::string(64, 'a'));
    nv.push_back("k=" + std::string(65, 'a'));   // B7 for callfunction only
    nv.push_back("k=" + std::string(199, 'a'));
    nv.push_back("k=" + std::string(200, 'a'));
    nv.push_back("k=" + std::string(201, 'a'));  // B7 setparam and callfunction
    nv.push_back("k=" + std::string(300, 'a'));  // B7 setparam and callfunction
    std::string pct;
    for (int i = 0; i < 100; i++)
        pct += "%41";
    nv.push_back("k=" + pct); // raw 300, decoded 100: fits setparam, B7 for callfunction
    pct.clear();
    for (int i = 0; i < 250; i++)
        pct += "%41";
    nv.push_back("k=" + pct); // raw 750, decoded 250: B7 for both
    std::vector<Case> g;
    static const char *const nvPaths[3] = {"/setparam/", "/callfunction/", "/getparam/"};
    for (size_t i = 0; i < nv.size(); i++)
        for (int p = 0; p < 3; p++)
        {
            Case c;
            c.path = nvPaths[p];
            c.query = nv[i];
            g.push_back(c);
        }
    // "/?sendmessage&tocall=...&message=..."
    std::vector<std::string> sm;
    sm.push_back("sendmessage&tocall=dk5en&message=hello%20world");
    sm.push_back("sendmessage&tocall=&message=hi");
    sm.push_back("sendmessage&tocall=a%20b&message=a&b=c=d");
    sm.push_back("sendmessage&tocall=X&messagex=1&message=2"); // "&messagex" ends tocall, as indexOf("&message") does today
    sm.push_back("sendmessage&tocall=X&message=");
    sm.push_back("sendmessage&tocall=A&x=1&message=hi");       // today and target: tocall "A&x=1"
    sm.push_back("sendmessage&message=m");                     // no tocall: both fail
    sm.push_back("sendmessage&tocall=A");                      // no message: both fail
    sm.push_back("sendmessage&tocall=A&messagefoo");           // "&message" without '=': both fail
    sm.push_back("sendmessage");
    sm.push_back("sendmessage&message=m&tocall=B");            // swapped: B11 row (proposed, seams 7.2)
    sm.push_back("sendmessage&message=a&tocall=B&message=c");  // swapped, message repeated
    sm.push_back("sendmessage&tocall=A&message=m&tocall=B");
    sm.push_back("sendmessage&tocall=" + std::string(16, 'T') + "&message=" + std::string(152, 'm'));
    sm.push_back("sendmessage&tocall=" + std::string(17, 'T') + "&message=m");               // B7 tocall
    sm.push_back("sendmessage&tocall=T&message=" + std::string(153, 'm'));                    // B7 message
    sm.push_back("sendmessage&tocall=T&message=" + std::string(60, 'm') + "%C3%A4");
    sm.push_back("sendmessage&message=" + std::string(20, 'm') + "&tocall=" + std::string(17, 'T')); // swapped and B7 tocall
    for (size_t i = 0; i < sm.size(); i++)
    {
        Case c;
        c.path = "/";
        c.query = sm[i];
        g.push_back(c);
    }
    // "/?page=neighbours&x=..."
    std::vector<std::string> nb;
    nb.push_back("page=neighbours");
    nb.push_back("page=neighbours&x=64");
    nb.push_back("page=neighbours&x=abc");
    nb.push_back("page=neighbours&x=-5&y=2");
    nb.push_back("page=neighbours&xx=5");
    nb.push_back("page=neighbours&x=");
    nb.push_back("page=neighbours&y=1&x=128");
    for (size_t i = 0; i < nb.size(); i++)
    {
        Case c;
        c.path = "/";
        c.query = nb[i];
        g.push_back(c);
    }
    return g;
}

void test_reference(void)
{
    std::vector<Case> c = corpusCases();
    TEST_ASSERT_TRUE(c.size() > 1000);
    for (size_t i = 0; i < c.size(); i++)
        compareCase(c[i], &g_corpus);
    std::vector<Case> g = generated();
    for (size_t i = 0; i < g.size(); i++)
        compareCase(g[i], &g_gen);

    // Requests that reach each handler (setparam, callfunction, getparam, sendmessage, neighbours): the denominators of the table.
    const int kRoutedCorpus[5] = {346, 84, 143, 63, 2};
    const int kRoutedGen[5] = {20, 20, 20, 18, 7};
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(kRoutedCorpus[i], g_corpus.routed[i], "routed corpus");
        TEST_ASSERT_EQUAL_INT_MESSAGE(kRoutedGen[i], g_gen.routed[i], "routed generated");
    }

    // Every difference to today's behaviour, by ID. Counts are exact; any other kind of difference fails in the cmp*() above.
    struct DiffRow
    {
        const char *id;
        const char *what;
        int corpus, generated;
    };
    const DiffRow kDiffTable[] = {
        {"B4/D2", "setparam without '=' answers 422 (name == value today; for a target without '?' today reads garbage offsets)", 4, 3},
        {"B7/D11 setparam", "value over 200 decoded bytes answers 422 (cut silently today)", 4, 3},
        {"B7/D11 callfunction", "parameter over 64 decoded bytes answers 422 (getparam ignores the value: no row)", 0, 7},
        {"B7/D11 tocall", "tocall over 16 decoded bytes answers 422", 0, 2},
        {"B7/D11 message", "message over 152 decoded bytes answers 422 (utf8-safe cut at 150 today)", 2, 1},
        {"B11 swapped (proposed)", "&message before &tocall=: today tocall = text from '&message' to the end of '&tocall=' (substring swaps its bounds, garbage in the destination); target = the tocall value", 1, 3},
    };
    const int gotCorpus[6] = {g_corpus.b4, g_corpus.b7_setparam, g_corpus.b7_call, g_corpus.b7_tocall, g_corpus.b7_message, g_corpus.swapped};
    const int gotGen[6] = {g_gen.b4, g_gen.b7_setparam, g_gen.b7_call, g_gen.b7_tocall, g_gen.b7_message, g_gen.swapped};
    for (int i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(kDiffTable[i].corpus, gotCorpus[i], kDiffTable[i].id);
        TEST_ASSERT_EQUAL_INT_MESSAGE(kDiffTable[i].generated, gotGen[i], kDiffTable[i].id);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first);
    RUN_TEST(test_get);
    RUN_TEST(test_tail);
    RUN_TEST(test_until);
    RUN_TEST(test_decode);
    RUN_TEST(test_reference);
    return UNITY_END();
}
