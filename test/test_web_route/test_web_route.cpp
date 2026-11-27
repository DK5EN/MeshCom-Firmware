// Route table web_route.h (FORMAL-02 wave 7, docs/web-server-seams-20261127.md 6.3, 7.2 B1/B2).
//
//   pio test -e native_web_route
//
// 1. Unit cases per row (exact match, case, trailing bytes, wrong method, selector forms).
// 2. Equivalence with today's router on the request corpus of test/test_web_diff/: for every corpus request the golden
//    record (profile "open") says which handler today's router ran; webRouteMatch must name the same handler. The
//    requests where it does not, only because of B1 (unknown path or method: today the scaffold or a branch of its
//    own, target 404/405), B2 (a non-exact match reaches a handler today), B3 (a request line webReqParse refuses:
//    target 400), ENV (build variant) or CORPUS (a request no browser sends), are listed in kDeltas below, each with
//    the B-ID and the reason. The test asserts the table is exact: no unlisted difference, no stale entry, and every
//    label agrees with the verdict of webReqParse (B3 and CORPUS rows are refused, all others parse WRQ_OK).
#include <unity.h>

#include "../support/repo_file.h"
#include "web_route.h"

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------------------------------- helpers

static WebSpan sp(const char *s)
{
    WebSpan r;
    r.p = s;
    r.n = (uint16_t)strlen(s);
    return r;
}

// route of "METHOD target HTTP/1.1": through webReqParse, as the dispatcher will
static uint8_t routeOf(const char *method, const char *target)
{
    std::string rl = std::string(method) + " " + target + " HTTP/1.1\r\n\r\n";
    WebReq rq;
    webReqParse(rl.data(), rl.size(), false, &rq);
    return webRouteMatch(rq.method, rq.path, rq.query);
}
static uint8_t G(const char *target) { return routeOf("GET", target); }

// ---------------------------------------------------------------------------------------------------- 1. unit

static void test_table_shape(void)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    TEST_ASSERT_EQUAL_UINT8(WEB_ROUTE_ROWS_FULL, n);
    // every route 1..WR_COUNT-1 has a row, no route has two rows with the same (path, key, val, method bit)
    bool seen[WR_COUNT];
    for (int i = 0; i < WR_COUNT; i++)
        seen[i] = false;
    for (uint8_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE(rows[i].route > WR_NONE && rows[i].route < WR_COUNT);
        TEST_ASSERT_FALSE_MESSAGE(seen[rows[i].route], rows[i].path);
        seen[rows[i].route] = true;
        TEST_ASSERT_TRUE(rows[i].methods != 0);
        TEST_ASSERT_EQUAL_CHAR('/', rows[i].path[0]);
        TEST_ASSERT_TRUE(strlen(rows[i].path) < WEB_ROUTE_STR_MAX);
        if (rows[i].key != NULL)
            TEST_ASSERT_TRUE(strlen(rows[i].key) < WEB_ROUTE_STR_MAX);
        if (rows[i].val != NULL)
            TEST_ASSERT_TRUE(strlen(rows[i].val) < WEB_ROUTE_STR_MAX);
        for (uint8_t j = 0; j < i; j++)
            TEST_ASSERT_FALSE_MESSAGE(strcmp(rows[i].path, rows[j].path) == 0 &&
                                          (rows[i].key == NULL) == (rows[j].key == NULL) &&
                                          (rows[i].key == NULL || strcmp(rows[i].key, rows[j].key) == 0) &&
                                          (rows[i].val == NULL) == (rows[j].val == NULL) &&
                                          (rows[i].val == NULL || strcmp(rows[i].val, rows[j].val) == 0) &&
                                          (rows[i].methods & rows[j].methods) != 0,
                                      rows[i].path);
    }
    for (int r = 1; r < WR_COUNT; r++)
        TEST_ASSERT_TRUE_MESSAGE(seen[r], "route without a row");
}

// each row hit by its own canonical request, with its own route
static void test_every_row_is_reachable(void)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    for (uint8_t i = 0; i < n; i++)
    {
        std::string target = rows[i].path;
        if (rows[i].key != NULL && rows[i].key[0] != 0)
        {
            target += "?";
            target += rows[i].key;
            if (rows[i].key[strlen(rows[i].key) - 1] != '=' && rows[i].val != NULL)
                target += "=";
            if (rows[i].val != NULL)
                target += rows[i].val;
        }
        const WebMethod m = (rows[i].methods & WRM_GET) ? WM_GET : WM_POST;
        std::string rl = std::string(m == WM_GET ? "GET" : "POST") + " " + target + " HTTP/1.1\r\n\r\n";
        WebReq rq;
        webReqParse(rl.data(), rl.size(), false, &rq);
        TEST_ASSERT_EQUAL_MESSAGE(rows[i].route, webRouteMatch(rq.method, rq.path, rq.query), target.c_str());
    }
}

static void test_scaffold_and_selectors(void)
{
    TEST_ASSERT_EQUAL(WR_SCAFFOLD, G("/"));
    TEST_ASSERT_EQUAL(WR_SCAFFOLD, G("/?")); // empty query
    TEST_ASSERT_EQUAL(WR_NONE, G("/?x=1"));  // B1: today the scaffold
    TEST_ASSERT_EQUAL(WR_NONE, G("/?&"));
    TEST_ASSERT_EQUAL(WR_PAGE_INFO, G("/?page=info"));
    TEST_ASSERT_EQUAL(WR_PAGE_INFO, G("/?page=info&x=1")); // the first pair decides, the rest is not read
    TEST_ASSERT_EQUAL(WR_NONE, G("/?x=1&page=info"));       // B2: not the first pair
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page=infoX"));          // B2: trailing bytes
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page=inf"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page="));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page=Info")); // case sensitive
    TEST_ASSERT_EQUAL(WR_NONE, G("/?Page=info"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?pageX=info"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page=nosuchpage"));
    TEST_ASSERT_EQUAL(WR_PAGE_MCP23017, G("/?page=mcp23017"));
    TEST_ASSERT_EQUAL(WR_PAGE_REMOTE, G("/?page=remote"));
    TEST_ASSERT_EQUAL(WR_PAGE_NEIGHBOURS, G("/?page=neighbours&x=3"));
    // page names are not prefixes of each other in the table order that matters today (path / pins / position)
    TEST_ASSERT_EQUAL(WR_PAGE_PATH, G("/?page=path"));
    TEST_ASSERT_EQUAL(WR_PAGE_PINS, G("/?page=pins"));
    TEST_ASSERT_EQUAL(WR_PAGE_POSITION, G("/?page=position"));
    TEST_ASSERT_EQUAL(WR_PAGE_SETUP, G("/?page=setup"));
    TEST_ASSERT_EQUAL(WR_PAGE_WX, G("/?page=wx"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?page=wxx"));
}

// decision (a): a page-shaped request is answered with the login page whether the page is known or not
static bool shaped(const char *method, const char *target)
{
    std::string rl = std::string(method) + " " + target + " HTTP/1.1\r\n\r\n";
    WebReq rq;
    webReqParse(rl.data(), rl.size(), false, &rq);
    return webRouteIsPageShaped(rq.method, rq.path, rq.query);
}

static void test_page_shaped(void)
{
    // every table page is page-shaped, and so is a page the table does not know
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    for (uint8_t i = 0; i < n; i++)
        if (rows[i].route >= WR_PAGE_BASE)
            TEST_ASSERT_TRUE_MESSAGE(shaped("GET", (std::string("/?page=") + rows[i].val).c_str()), rows[i].val);
    TEST_ASSERT_TRUE(shaped("GET", "/?page=nosuchpage"));
    TEST_ASSERT_TRUE(shaped("GET", "/?page="));
    TEST_ASSERT_TRUE(shaped("GET", "/?page"));
    TEST_ASSERT_TRUE(shaped("GET", "/?page=info&x=1"));
    TEST_ASSERT_TRUE(shaped("GET", "/?page&x=1"));
    TEST_ASSERT_TRUE(shaped("GET", "/?page=infoX")); // unknown page name: still page-shaped
    // not page-shaped: other method, other path, other first key
    TEST_ASSERT_FALSE(shaped("POST", "/?page=info"));
    TEST_ASSERT_FALSE(shaped("HEAD", "/?page=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/x?page=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/rmsend?page=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/?x=1&page=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/?pagex=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/?Page=info"));
    TEST_ASSERT_FALSE(shaped("GET", "/"));
    TEST_ASSERT_FALSE(shaped("GET", "/?"));
    TEST_ASSERT_FALSE(shaped("GET", "/?login=x"));
    TEST_ASSERT_FALSE(shaped("GET", "/mc.js"));
    // a page route is page-shaped (the dispatcher never needs the helper for a known page, but it must not disagree)
    TEST_ASSERT_TRUE(shaped("GET", "/?page=setup") && G("/?page=setup") == WR_PAGE_SETUP);
    // spans of length 0 and a bad method value
    WebSpan e = sp("");
    TEST_ASSERT_FALSE(webRouteIsPageShaped(WM_GET, e, e));
    TEST_ASSERT_FALSE(webRouteIsPageShaped(WM_NONE, sp("/"), sp("page=info")));
    TEST_ASSERT_FALSE(webRouteIsPageShaped(200, sp("/"), sp("page=info")));
}

static void test_login_family(void)
{
    TEST_ASSERT_EQUAL(WR_LOGIN_NONCE, G("/?loginnonce"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?loginnonceX")); // B2
    TEST_ASSERT_EQUAL(WR_LOGIN, G("/?login=abc.def"));
    TEST_ASSERT_EQUAL(WR_LOGIN, G("/?login="));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?login")); // no '=': not the login route
    TEST_ASSERT_EQUAL(WR_LOGOUT, G("/?logout"));
    TEST_ASSERT_EQUAL(WR_LOGOUT, G("/?logout=1"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?logoutX")); // B2
    TEST_ASSERT_EQUAL(WR_LEGACY_PW, G("/?nodepassword"));
    TEST_ASSERT_EQUAL(WR_LEGACY_PW, G("/?nodepassword="));
    TEST_ASSERT_EQUAL(WR_LEGACY_PW, G("/?nodepassword=secret"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?nodepasswordX=1"));
    // B2 (decision d): today the scaffold (:1268 compares the whole line), now the one legacy route, handled as logout
    TEST_ASSERT_EQUAL(WR_LEGACY_PW, G("/?nodepassword&x=1"));
    TEST_ASSERT_EQUAL(WR_LEGACY_PW, G("/?nodepassword=&x=1"));
    TEST_ASSERT_EQUAL(WR_SENDMESSAGE, G("/?sendmessage&tocall=9999&message=hi"));
    TEST_ASSERT_EQUAL(WR_SENDMESSAGE, G("/?sendmessage"));
    TEST_ASSERT_EQUAL(WR_SENDMESSAGE, G("/?sendmessage=hi"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?sendmessagex"));
    TEST_ASSERT_EQUAL(WR_GETMESSAGES, G("/?getmessages"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?getmessagesX"));
}

static void test_exact_paths_and_case(void)
{
    TEST_ASSERT_EQUAL(WR_PIN_STATUS, G("/pinstatus"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/pinstatusX"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/pinstatu"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/PinStatus"));
    TEST_ASSERT_EQUAL(WR_NONE, G("pinstatus"));
    TEST_ASSERT_EQUAL(WR_NONE, G("//pinstatus"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/x/pinstatus"));
    TEST_ASSERT_EQUAL(WR_PIN_STATUS, G("/pinstatus?v=1")); // the query of a non-selector row is ignored
    TEST_ASSERT_EQUAL(WR_PIN_ASSIGN, G("/pinassign"));
    TEST_ASSERT_EQUAL(WR_GPIO_STATUS, G("/gpiostatus"));
    TEST_ASSERT_EQUAL(WR_AU_STATUS, G("/austatus"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/austatus/"));
    TEST_ASSERT_EQUAL(WR_SCREEN_RGB565, G("/screen.rgb565"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/screen.rgb565x"));
    TEST_ASSERT_EQUAL(WR_SCREEN_OLED, G("/screen.oled"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/screen.oledx"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/screenXoled")); // '.' is a literal byte, not a wildcard
    TEST_ASSERT_EQUAL(WR_ASSET_CSS, G("/mc.css?v=c9cedb8b"));
    TEST_ASSERT_EQUAL(WR_ASSET_JS, G("/mc.js"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/mc.jsX"));
    TEST_ASSERT_EQUAL(WR_ASSET_RMJS, G("/mcrm.js?v=1"));
    TEST_ASSERT_EQUAL(WR_ASSET_AUJS, G("/mcau.js"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/mc.JS"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/favicon.ico"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/ota/state"));
    TEST_ASSERT_EQUAL(WR_NONE, G(""));
    TEST_ASSERT_EQUAL(WR_NONE, G("/index.html"));
}

static void test_api_prefix_rows_keep_the_trailing_slash(void)
{
    TEST_ASSERT_EQUAL(WR_SETPARAM, G("/setparam/?display=on"));
    TEST_ASSERT_EQUAL(WR_SETPARAM, G("/setparam/"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/setparam"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/setparam/x/?a=b"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/SETPARAM/?a=b"));
    TEST_ASSERT_EQUAL(WR_GETPARAM, G("/getparam/?gateway"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/getparam?gateway"));
    TEST_ASSERT_EQUAL(WR_CALLFUNCTION, G("/callfunction/?sendpos=1"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/callfunction?sendpos=1"));
    // a route token in a query value never changes the route
    TEST_ASSERT_EQUAL(WR_NONE, G("/x?r=/setparam/?a=b"));
    TEST_ASSERT_EQUAL(WR_SCAFFOLD, G("/"));
    TEST_ASSERT_EQUAL(WR_NONE, G("/?r=/callfunction/"));
}

static void test_method_masks(void)
{
    TEST_ASSERT_EQUAL(WR_RM_SEND, routeOf("POST", "/rmsend"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("GET", "/rmsend")); // B1: today a 405 branch
    TEST_ASSERT_EQUAL(WR_RM_PASSWD, routeOf("POST", "/rmpasswd"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("GET", "/rmpasswd"));
    TEST_ASSERT_EQUAL(WR_WEB_PASSWD, routeOf("POST", "/rmwebpwd"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("GET", "/rmwebpwd"));
    TEST_ASSERT_EQUAL(WR_RM_NODES_GET, routeOf("GET", "/rmnodes"));
    TEST_ASSERT_EQUAL(WR_RM_NODES_POST, routeOf("POST", "/rmnodes"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("HEAD", "/rmnodes"));
    TEST_ASSERT_EQUAL(WR_RM_STATUS, routeOf("GET", "/rmstatus"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("POST", "/rmstatus"));
    TEST_ASSERT_EQUAL(WR_RM_HEARD, routeOf("GET", "/rmheard"));
    TEST_ASSERT_EQUAL(WR_CONFIG_GET, routeOf("GET", "/config.json"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("POST", "/config.json"));
    TEST_ASSERT_EQUAL(WR_CONFIG_POST, routeOf("POST", "/config"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("GET", "/config"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("OPTIONS", "/"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("OPTIONS", "/setparam/?a=b"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("DELETE", "/"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("POST", "/"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("POST", "/?page=info"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("get", "/"));
    TEST_ASSERT_EQUAL(WR_NONE, routeOf("", "/"));
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(WM_NONE, sp("/"), sp("")));
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(200, sp("/"), sp(""))); // out-of-range method value
}

static void test_span_edges(void)
{
    // the span is not NUL terminated: bytes after n are not read
    const char buf[] = "/pinstatusX";
    WebSpan p;
    p.p = buf;
    p.n = 10;
    WebSpan q;
    q.p = "";
    q.n = 0;
    TEST_ASSERT_EQUAL(WR_PIN_STATUS, webRouteMatch(WM_GET, p, q));
    p.n = 11;
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(WM_GET, p, q));
    // selector bytes after n are not read either
    const char qb[] = "page=infoX";
    WebSpan pq;
    pq.p = qb;
    pq.n = 9;
    WebSpan root = sp("/");
    TEST_ASSERT_EQUAL(WR_PAGE_INFO, webRouteMatch(WM_GET, root, pq));
    pq.n = 10;
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(WM_GET, root, pq));
    // a path longer than the literal bound
    char big[400];
    memset(big, 'a', sizeof(big));
    big[0] = '/';
    WebSpan bp;
    bp.p = big;
    bp.n = (uint16_t)sizeof(big);
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(WM_GET, bp, q));
    // an embedded NUL in the path is a byte like any other
    WebSpan nul;
    nul.p = "/pinstatus\0X";
    nul.n = 12;
    TEST_ASSERT_EQUAL(WR_NONE, webRouteMatch(WM_GET, nul, q));
}

static void test_flags(void)
{
    TEST_ASSERT_EQUAL_UINT8(WRF_PAGE, webRouteFlags(WR_PAGE_INFO));
    TEST_ASSERT_EQUAL_UINT8(WRF_PAGE, webRouteFlags(WR_PAGE_REMOTE));
    TEST_ASSERT_EQUAL_UINT8(WRF_NO_AUTH, webRouteFlags(WR_ASSET_JS));
    TEST_ASSERT_EQUAL_UINT8(WRF_NO_AUTH, webRouteFlags(WR_LOGIN_NONCE));
    TEST_ASSERT_EQUAL_UINT8(WRF_NO_AUTH | WRF_STATE, webRouteFlags(WR_LOGIN));
    TEST_ASSERT_EQUAL_UINT8(WRF_BODY | WRF_STATE, webRouteFlags(WR_RM_SEND));
    TEST_ASSERT_EQUAL_UINT8(WRF_BODY | WRF_STATE, webRouteFlags(WR_CONFIG_POST));
    TEST_ASSERT_EQUAL_UINT8(0, webRouteFlags(WR_RM_NODES_GET));
    TEST_ASSERT_EQUAL_UINT8(0, webRouteFlags(WR_NONE));
    TEST_ASSERT_EQUAL_UINT8(0, webRouteFlags(WR_COUNT));
}

// WRF_STATE mirrors webGuardIsStateChanging: for the canonical request of every row, the guard says "state changing"
// exactly when the row has WRF_STATE (a POST always is)
static void test_state_flag_mirrors_the_guard(void)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    for (uint8_t i = 0; i < n; i++)
    {
        std::string target = rows[i].path;
        if (rows[i].key != NULL && rows[i].key[0] != 0)
        {
            target += "?";
            target += rows[i].key;
            if (rows[i].key[strlen(rows[i].key) - 1] != '=' && rows[i].val != NULL)
                target += "=";
            if (rows[i].val != NULL)
                target += rows[i].val;
        }
        const bool post = (rows[i].methods & WRM_POST) != 0;
        const bool guard = webGuardIsStateChanging(target.c_str(), post);
        TEST_ASSERT_EQUAL_MESSAGE(guard ? 1 : 0, (rows[i].flags & WRF_STATE) ? 1 : 0, target.c_str());
    }
}

// ---------------------------------------------------------------------------------------- 2. corpus equivalence

static const char *const kCorpusDir = "test/test_web_diff/corpus/";
static const char *const kGoldenDir = "test/test_web_diff/golden/";

// the handler class the golden record shows for today's router, or a "skip:" reason when the request never reached the
// router (guard 403, 431, 503, nothing written) or left no evidence of a handler
static bool startsWith(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
static bool has(const std::string &s, const char *p) { return s.find(p) != std::string::npos; }

static std::string openProfile(const std::string &golden)
{
    const std::string head = "== profile open\n";
    size_t a = golden.find(head);
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, "golden without profile open");
    a += head.size();
    size_t b = golden.find("== profile ", a);
    return golden.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

static std::string handlerClass(const std::string &name)
{
    static const struct
    {
        const char *trace;
        const char *cls;
    } map[] = {
        {"deliver_scaffold", "scaffold"},        {"sub_page_unknown", "page_unknown"},
        {"sub_rm_send", "rm_send"},              {"sub_rm_status", "rm_status"},
        {"sub_rm_passwd", "rm_passwd"},          {"sub_web_passwd", "web_passwd"},
        {"sub_rm_nodes_get", "rm_nodes_get"},    {"sub_rm_nodes_post", "rm_nodes_post"},
        {"sub_rm_heard", "rm_heard"},            {"sub_pin_status", "pin_status"},
        {"sub_pin_assign", "pin_assign"},        {"sub_gpio_status", "gpio_status"},
        {"sub_au_status", "au_status"},          {"sub_screen_oled", "screen_oled"},
        {"sub_screen_rgb565", "screen_rgb565"},  {"webFunctionCall", "callfunction"},
        {"msgstore", "callfunction"},            {"sendMessage", "sendmessage"},
        {"sub_content_messages", "getmessages"}, {"webSetup_setParam", "setparam"},
        {"webSetup_getParam", "getparam"},       {"sub_config_download", "config_get"},
        {"sub_config_upload", "config_post"},
    };
    const std::string o = openProfile(repo_slurp((std::string(kGoldenDir) + name + ".txt").c_str()));
    const size_t t0 = o.find("trace:\n");
    const size_t t1 = o.find("serial:");
    TEST_ASSERT_TRUE(t0 != std::string::npos && t1 != std::string::npos);
    std::istringstream tr(o.substr(t0 + 7, t1 - t0 - 7));
    for (std::string l; std::getline(tr, l);)
    {
        size_t i = 0;
        while (i < l.size() && l[i] == ' ')
            i++;
        l = l.substr(i);
        if (l.empty() || startsWith(l, "webTxBeginChunked") || startsWith(l, "setMsgOrigin"))
            continue;
        if (startsWith(l, "sub_page_"))
        {
            std::string p = l.substr(9);
            p = p.substr(0, p.find_first_of(" ("));
            return p == "unknown" ? "page_unknown" : "page:" + p;
        }
        for (size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++)
            if (startsWith(l, map[k].trace))
                return map[k].cls;
        return "skip:unmapped trace " + l;
    }
    // no handler in the trace: classify by what was written
    if (has(o, "out: 0 bytes"))
        return "skip:nothing written";
    if (has(o, "Content-Encoding: gzip"))
    {
        if (has(o, "| CSS"))
            return "asset:css";
        if (has(o, "| JS"))
            return "asset:js";
        if (has(o, "| RM"))
            return "asset:rm";
        if (has(o, "| AU"))
            return "asset:au";
        return "skip:unknown asset";
    }
    if (has(o, "HTTP/1.1 403 Forbidden") && has(o, "\"err\":\"forbidden\""))
        return "skip:guard 403";
    if (has(o, "HTTP/1.1 431 "))
        return "skip:431";
    if (has(o, "HTTP/1.1 503 "))
        return "skip:503";
    if (has(o, "HTTP/1.1 405 ") && has(o, "use POST"))
        return "rm_send405";
    if (has(o, "\"err\":\"plaintext\""))
        return "logout"; // /?nodepassword=<pw>: the legacy branch of the login family
    if (has(o, "Set-Cookie: mcs=;"))
        return "logout";
    if (has(o, "sendmessage failed") || has(o, "sendmessage ok"))
        return "sendmessage";
    if (has(o, "\"err\":\"throttled\""))
        return "nonce";
    if (has(o, "Set-Cookie: mcs="))
        return "login";
    if (has(o, "\"err\":\"") && has(o, "HTTP/1.1 403 "))
        return "login"; // webAuthLogin refused: the only other 403 JSON of the login family
    // loginnonce answers a bare 32 character text body
    {
        std::istringstream in(o);
        for (std::string l; std::getline(in, l);)
            if (l.size() == 34 && startsWith(l, "| ") && l.find_first_not_of("0123456789abcdef", 2) == std::string::npos)
                return "nonce";
    }
    return "skip:no evidence";
}

static const char *const kPageNames[] = {"setup",  "position", "wx",   "mheard",   "neighbours", "messages", "mailbox",
                                         "rxlog",  "path",     "spectrum", "mcp23017", "pins",   "info",     "remote"};

static std::string routeClass(uint8_t r)
{
    switch (r)
    {
    case WR_NONE: return "none";
    case WR_SCAFFOLD: return "scaffold";
    case WR_ASSET_CSS: return "asset:css";
    case WR_ASSET_JS: return "asset:js";
    case WR_ASSET_RMJS: return "asset:rm";
    case WR_ASSET_AUJS: return "asset:au";
    case WR_LOGIN_NONCE: return "nonce";
    case WR_LOGIN: return "login";
    case WR_LOGOUT: return "logout";
    case WR_LEGACY_PW: return "logout";
    case WR_RM_SEND: return "rm_send";
    case WR_RM_STATUS: return "rm_status";
    case WR_RM_PASSWD: return "rm_passwd";
    case WR_WEB_PASSWD: return "web_passwd";
    case WR_RM_NODES_GET: return "rm_nodes_get";
    case WR_RM_NODES_POST: return "rm_nodes_post";
    case WR_RM_HEARD: return "rm_heard";
    case WR_PIN_STATUS: return "pin_status";
    case WR_PIN_ASSIGN: return "pin_assign";
    case WR_GPIO_STATUS: return "gpio_status";
    case WR_AU_STATUS: return "au_status";
    case WR_SCREEN_RGB565: return "screen_rgb565";
    case WR_SCREEN_OLED: return "screen_oled";
    case WR_CALLFUNCTION: return "callfunction";
    case WR_SENDMESSAGE: return "sendmessage";
    case WR_GETMESSAGES: return "getmessages";
    case WR_SETPARAM: return "setparam";
    case WR_GETPARAM: return "getparam";
    case WR_CONFIG_GET: return "config_get";
    case WR_CONFIG_POST: return "config_post";
    default: break;
    }
    if (r >= WR_PAGE_BASE && r < WR_COUNT)
        return std::string("page:") + kPageNames[r - WR_PAGE_BASE];
    return "invalid";
}

struct Cmp
{
    std::string name, line, today, target;
    uint8_t method;
    WebSpan path, query;
    int pr; // webReqParse verdict of the request
};

static std::string firstLine(const std::string &req)
{
    size_t e = req.find_first_of("\r\n");
    std::string l = req.substr(0, e == std::string::npos ? req.size() : e);
    return l.size() > 90 ? l.substr(0, 90) : l;
}

struct Corpus
{
    size_t total = 0, skipped = 0, identical = 0, outcomeSame = 0;
    std::map<std::string, size_t> skipReasons;
    std::map<std::string, std::string> skipNames; // reason -> last name
    std::vector<Cmp> diffs;
    std::set<uint8_t> hit; // routes with at least one identical request
    // same route as today, but webReqParse refuses the request (the wiring answers 400 before routing): name -> verdict
    std::map<std::string, int> refusedSame;
};

static Corpus runCorpus()
{
    Corpus c;
    std::istringstream idx(repo_slurp("test/test_web_diff/corpus/INDEX.txt"));
    for (std::string name; std::getline(idx, name);)
    {
        if (name.empty())
            continue;
        c.total++;
        const std::string today = handlerClass(name);
        if (startsWith(today, "skip:"))
        {
            c.skipped++;
            c.skipReasons[today]++;
            c.skipNames[today] += name + " ";
            continue;
        }
        const std::string req = repo_slurp((std::string(kCorpusDir) + name + ".req").c_str());
        WebReq rq;
        const WebParseRes pr = webReqParse(req.data(), req.size(), false, &rq);
        const uint8_t r = webRouteMatch(rq.method, rq.path, rq.query);
        const std::string target = routeClass(r);
        if (target == today && pr != WRQ_OK)
        {
            c.refusedSame[name] = (int)pr; // not identical: the wiring answers 400 (B3 / B6) before any route
            continue;
        }
        if (target == today)
        {
            c.identical++;
            c.hit.insert(r);
            continue;
        }
        // an unknown page: today the 404 page, target WR_NONE = 404 as well (same outcome, not a delta)
        if (today == "page_unknown" && r == WR_NONE && rq.path.n == 1 && rq.path.p[0] == '/' && rq.query.n >= 5 &&
            memcmp(rq.query.p, "page=", 5) == 0 && rq.method == WM_GET)
        {
            c.outcomeSame++;
            continue;
        }
        Cmp d;
        d.name = name;
        d.line = firstLine(req);
        d.today = today;
        d.target = target;
        d.method = rq.method;
        d.path = rq.path; // only .n is used later: the pointer dies with `req`
        d.query = rq.query;
        d.pr = (int)pr;
        c.diffs.push_back(d);
    }
    return c;
}

// Requests where webRouteMatch and today's router differ. Every row: the corpus request, the B-ID of
// docs/web-server-seams-20261127.md 7.2 (B1 unknown path/method, B2 non-exact match, B3 no version / bad request
// line, ENV build variant, CORPUS a request no browser sends), the handler class today's router ran, the route the table returns, and the reason.
struct Delta
{
    const char *name, *bid, *today;
    uint8_t target;
    const char *reason;
};
static const Delta kDeltas[] = {
    // clang-format off
    {"cap-007-favicon-ico", "B1", "scaffold", WR_NONE, "unknown path /favicon.ico: today the scaffold, target 404"},
    {"cap-033-config", "B2", "page:setup", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"grd-091-x", "B2", "setparam", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"grd-093-index-js", "B1", "scaffold", WR_NONE, "unknown path /index.js: today the scaffold, target 404"},
    {"grd-108-ota-upload", "B1", "scaffold", WR_NONE, "method POST on /ota/upload: today the scaffold, target 405"},
    {"grd-109-ota-upload", "B1", "scaffold", WR_NONE, "method POST on /ota/upload: today the scaffold, target 405"},
    {"grd-320-img-rmsend-dst-x-cmd-reboot", "B1", "rm_send405", WR_NONE, "GET /rmsend: today its own 405 branch, target the generic 405 (same status)"},
    {"grd-322-img-rmpasswd-pw-x", "B1", "scaffold", WR_NONE, "GET on a POST-only route /rmpasswd: today the scaffold, target 405"},
    {"js-003-tmpl-config", "B1", "scaffold", WR_NONE, "GET on a POST-only route /config: today the scaffold, target 405"},
    {"js-005-tmpl-rmpasswd", "B1", "scaffold", WR_NONE, "GET on a POST-only route /rmpasswd: today the scaffold, target 405"},
    {"js-009-tmpl-rmwebpwd", "B1", "scaffold", WR_NONE, "GET on a POST-only route /rmwebpwd: today the scaffold, target 405"},
    {"js-014-tmpl-rmsend", "B1", "rm_send405", WR_NONE, "GET /rmsend: today its own 405 branch, target the generic 405 (same status)"},
    {"js-018-pageq-info", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-020-pageq-position", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-022-pageq-wx", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-024-pageq-setup", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-026-pageq-messages", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-028-pageq-mailbox", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-030-pageq-rxlog", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-032-pageq-path", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-034-pageq-mheard", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-036-pageq-neighbours", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-038-pageq-spectrum", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-040-pageq-mcp23017", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-042-pageq-pins", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-044-pageq-remote", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-046-pageq-nosuchpage", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-048-pageq", "CORPUS", "scaffold", WR_NONE, "corpus artifact: the generator kept the unresolved relative URL of xhttp.open(\"GET\",\"?page=\"+page) (web_functions.cpp ~:1606), so the request line is GET ?page=..., which no browser sends; the parser refuses it (400), today the scaffold"},
    {"js-059-get-screen-rgb565", "ENV", "scaffold", WR_SCREEN_RGB565, "route exists only in T-Deck INSTRUMENT builds; the rig has none, so today shows the scaffold"},
    {"js-067-get-favicon-ico", "B1", "scaffold", WR_NONE, "unknown path /favicon.ico: today the scaffold, target 404"},
    {"js-068-get-ota-state", "B1", "scaffold", WR_NONE, "unknown path /ota/state: today the scaffold, target 404"},
    {"js-296-post-cfg-config-json", "B1", "config_get", WR_NONE, "method POST not in the row mask (today any method reaches the handler)"},
    {"mut-009-nover", "B3", "scaffold", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-018-page-info-nover", "B3", "page:info", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-027-setparam-onewire-on-nover", "B3", "setparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-037-getparam-gateway-nover", "B3", "getparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-046-callfunction-sendpos-nover", "B3", "callfunction", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-056-sendmessage-tocall-9999-message-hi-20the", "B3", "sendmessage", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-066-pinstatus-nover", "B3", "pin_status", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-076-rmstatus-nover", "B3", "rm_status", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-086-mc-js-nover", "B3", "asset:js", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-088-meth-post", "B1", "scaffold", WR_NONE, "method POST not allowed on /: today the scaffold, target 405"},
    {"mut-089-meth-post-page-info", "B1", "page:info", WR_NONE, "method POST not in the row mask (today any method reaches the handler)"},
    {"mut-090-meth-post-setparam-x-1", "B1", "setparam", WR_NONE, "method POST not in the row mask (today any method reaches the handler)"},
    {"mut-093-meth-head", "B1", "scaffold", WR_NONE, "method HEAD not allowed on /: today the scaffold, target 405"},
    {"mut-094-meth-head-page-info", "B1", "page:info", WR_NONE, "method HEAD not in the row mask (today any method reaches the handler)"},
    {"mut-095-meth-head-setparam-x-1", "B1", "setparam", WR_NONE, "method HEAD not in the row mask (today any method reaches the handler)"},
    {"mut-096-meth-head-rmsend", "B1", "scaffold", WR_NONE, "method HEAD not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-097-meth-head-config", "B1", "scaffold", WR_NONE, "method HEAD not allowed on /config: today the scaffold, target 405"},
    {"mut-098-meth-options", "B1", "scaffold", WR_NONE, "method OPTIONS not allowed on /: today the scaffold, target 405"},
    {"mut-099-meth-options-page-info", "B1", "page:info", WR_NONE, "method OPTIONS not in the row mask (today any method reaches the handler)"},
    {"mut-100-meth-options-setparam-x-1", "B1", "setparam", WR_NONE, "method OPTIONS not in the row mask (today any method reaches the handler)"},
    {"mut-101-meth-options-rmsend", "B1", "scaffold", WR_NONE, "method OPTIONS not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-102-meth-options-config", "B1", "scaffold", WR_NONE, "method OPTIONS not allowed on /config: today the scaffold, target 405"},
    {"mut-103-meth-put", "B1", "scaffold", WR_NONE, "method PUT not allowed on /: today the scaffold, target 405"},
    {"mut-104-meth-put-page-info", "B1", "page:info", WR_NONE, "method PUT not in the row mask (today any method reaches the handler)"},
    {"mut-105-meth-put-setparam-x-1", "B1", "setparam", WR_NONE, "method PUT not in the row mask (today any method reaches the handler)"},
    {"mut-106-meth-put-rmsend", "B1", "scaffold", WR_NONE, "method PUT not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-107-meth-put-config", "B1", "scaffold", WR_NONE, "method PUT not allowed on /config: today the scaffold, target 405"},
    {"mut-108-meth-delete", "B1", "scaffold", WR_NONE, "method DELETE not allowed on /: today the scaffold, target 405"},
    {"mut-109-meth-delete-page-info", "B1", "page:info", WR_NONE, "method DELETE not in the row mask (today any method reaches the handler)"},
    {"mut-110-meth-delete-setparam-x-1", "B1", "setparam", WR_NONE, "method DELETE not in the row mask (today any method reaches the handler)"},
    {"mut-111-meth-delete-rmsend", "B1", "scaffold", WR_NONE, "method DELETE not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-112-meth-delete-config", "B1", "scaffold", WR_NONE, "method DELETE not allowed on /config: today the scaffold, target 405"},
    {"mut-113-meth-get", "B1", "scaffold", WR_NONE, "method get not allowed on /: today the scaffold, target 405"},
    {"mut-114-meth-get-page-info", "B1", "page:info", WR_NONE, "method get not in the row mask (today any method reaches the handler)"},
    {"mut-115-meth-get-setparam-x-1", "B1", "setparam", WR_NONE, "method get not in the row mask (today any method reaches the handler)"},
    {"mut-116-meth-get-rmsend", "B1", "scaffold", WR_NONE, "method get not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-117-meth-get-config", "B1", "scaffold", WR_NONE, "method get not allowed on /config: today the scaffold, target 405"},
    {"mut-118-meth-get", "B1", "scaffold", WR_NONE, "method GET/ not allowed on /: today the scaffold, target 405"},
    {"mut-119-meth-get-page-info", "B1", "page:info", WR_NONE, "method GET/ not in the row mask (today any method reaches the handler)"},
    {"mut-120-meth-get-setparam-x-1", "B1", "setparam", WR_NONE, "method GET/ not in the row mask (today any method reaches the handler)"},
    {"mut-121-meth-get-rmsend", "B1", "scaffold", WR_NONE, "method GET/ not allowed on /rmsend: today the scaffold, target 405"},
    {"mut-122-meth-get-config", "B1", "scaffold", WR_NONE, "method GET/ not allowed on /config: today the scaffold, target 405"},
    {"mut-123-meth-none", "B3", "scaffold", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-124-meth-none-page-info", "B3", "page:info", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-125-meth-none-setparam-x-1", "B3", "setparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-126-meth-none-rmsend", "B3", "scaffold", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-127-meth-none-config", "B3", "scaffold", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-145-referer-root-page-info", "B2", "page:info", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-146-referer-unk-page-info", "B2", "page:info", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-147-query-page-info", "B2", "page:info", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-148-referer-setparam", "B2", "setparam", WR_PAGE_WX, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-149-referer-root-setparam", "B2", "setparam", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-150-referer-unk-setparam", "B2", "setparam", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-151-query-setparam", "B2", "setparam", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-152-referer-callfunction", "B2", "callfunction", WR_PAGE_WX, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-153-referer-root-callfunction", "B2", "callfunction", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-154-referer-unk-callfunction", "B2", "callfunction", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-155-query-callfunction", "B2", "callfunction", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-158-referer-unk-rmsend", "B1", "scaffold", WR_NONE, "unknown path /nothing: today the scaffold, target 404"},
    {"mut-159-query-rmsend", "B1", "scaffold", WR_NONE, "unknown path /nothing: today the scaffold, target 404"},
    {"mut-160-referer-config-json", "B2", "config_get", WR_PAGE_WX, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-161-referer-root-config-json", "B2", "config_get", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-162-referer-unk-config-json", "B2", "config_get", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-163-query-config-json", "B2", "config_get", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-164-referer-getparam", "B2", "getparam", WR_PAGE_WX, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-165-referer-root-getparam", "B2", "getparam", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-166-referer-unk-getparam", "B2", "getparam", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-167-query-getparam", "B2", "getparam", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-168-referer-sendmessage", "B2", "sendmessage", WR_PAGE_WX, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-169-referer-root-sendmessage", "B2", "sendmessage", WR_SCAFFOLD, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-170-referer-unk-sendmessage", "B2", "sendmessage", WR_NONE, "route token in the Referer header (today indexOf over the whole header); target routes the request line only"},
    {"mut-171-query-sendmessage", "B2", "sendmessage", WR_NONE, "route token in a query value (today indexOf over the whole header)"},
    {"mut-172-route-page-infox", "B2", "page:info", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-175-route-page-setupx", "B2", "page:setup", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-176-route-xpage-info", "B1", "scaffold", WR_NONE, "unknown query on /: today the scaffold, target 404"},
    {"mut-177-route-x-page-info", "B2", "page:info", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-178-route-pinstatusx", "B2", "pin_status", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-180-route-xpinstatus", "B1", "scaffold", WR_NONE, "unknown path /xpinstatus: today the scaffold, target 404"},
    {"mut-181-route-mc-jsx", "B2", "asset:js", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-182-route-mc-js-x", "B2", "asset:js", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-183-route-mc-js", "B1", "scaffold", WR_NONE, "unknown path /MC.JS: today the scaffold, target 404"},
    {"mut-184-route-rmstatus", "B2", "rm_status", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-185-route-rmsendx", "B2", "rm_send405", WR_NONE, "path /rmsend with trailing bytes: today startsWith reaches the 405 branch, target 404"},
    {"mut-186-route-screen-oledx", "B2", "screen_oled", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-187-route-config-jsonx", "B2", "config_get", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-188-route-post-config", "B3", "config_post", WR_NONE, "blank inside the target (GET /POST /config): the parser refuses the request line, target 400; today indexOf finds \"POST /config\" in it"},
    {"mut-192-route-austatusx", "B2", "au_status", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-193-route-loginnoncex", "B2", "nonce", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-196-route-logoutx", "B2", "logout", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-199-route-nodepasswordx", "B1", "scaffold", WR_NONE, "unknown query on /: today the scaffold, target 404"},
    {"mut-378-rm-get-rmpasswd", "B1", "scaffold", WR_NONE, "GET on a POST-only route /rmpasswd: today the scaffold, target 405"},
    {"mut-380-rm-get-rmwebpwd", "B1", "scaffold", WR_NONE, "GET on a POST-only route /rmwebpwd: today the scaffold, target 405"},
    {"mut-384-rm-get-rmsend", "B1", "rm_send405", WR_NONE, "GET /rmsend: today its own 405 branch, target the generic 405 (same status)"},
    {"mut-387-rm-post-rmstatus", "B1", "scaffold", WR_NONE, "method POST not allowed on /rmstatus: today the scaffold, target 405"},
    {"mut-389-rm-post-rmheard", "B1", "scaffold", WR_NONE, "method POST not allowed on /rmheard: today the scaffold, target 405"},
    {"mut-390-rm-get-config", "B1", "scaffold", WR_NONE, "GET on a POST-only route /config: today the scaffold, target 405"},
    {"mut-402-head-over-window-route-late", "B1", "scaffold", WR_NONE, "unknown path /nothing: today the scaffold, target 404"},
    {"mut-426-space-prefix", "B3", "page:info", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-427-double-space", "B3", "page:info", WR_NONE, "double blanks in the request line (target \" /\" after the split): the parser refuses it, target 400; today indexOf still finds the token"},
    {"mut-428-tab-sep", "B3", "page:info", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-429-http-in-path", "B2", "page:info", WR_NONE, "path or selector is not an exact match (trailing bytes or extra segment): today indexOf/startsWith finds the token"},
    {"mut-435-setparam-no-http", "B3", "setparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-436-setparam-http-first", "B3", "setparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    {"mut-437-path-only", "B3", "setparam", WR_NONE, "request line without version or method: the parser yields no target, today routed by indexOf (target 400)"},
    // clang-format on
};
static const size_t kDeltaCount = sizeof(kDeltas) / sizeof(kDeltas[0]);


// Requests that route like today but that webReqParse refuses: the wiring answers 400 before any route (B3 bad
// version / bad request line, B6 bad Content-Length, CORPUS a request no browser sends). Exact: no unlisted entry.
struct Refused
{
    const char *name, *bid;
    int pr;
    const char *reason;
};
static const Refused kRefused[] = {
    // clang-format off
    {"js-255-setval", "CORPUS", 1, "raw blank in the query: the GUI sends encodeURIComponent(value) (mc.js:99), the generator did not encode"},
    {"mut-005-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-007-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-014-page-info-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-016-page-info-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-023-setparam-onewire-on-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-025-setparam-onewire-on-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-033-getparam-gateway-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-035-getparam-gateway-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-042-callfunction-sendpos-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-044-callfunction-sendpos-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-052-sendmessage-tocall-9999-message-hi-20the", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-054-sendmessage-tocall-9999-message-hi-20the", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-062-pinstatus-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-064-pinstatus-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-072-rmstatus-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-074-rmstatus-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-082-mc-js-http-2-0", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-084-mc-js-http-1-1x", "B3", 2, "version other than HTTP/1.0 or HTTP/1.1: 400"},
    {"mut-337-cl-rmsend-content-length-5", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-338-cl-config-content-length-5", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-339-cl-rmsend-content-length-12abc", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-340-cl-config-content-length-12abc", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-341-cl-rmsend-content-length-abc", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-342-cl-config-content-length-abc", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-343-cl-rmsend-content-length-99999999999", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-344-cl-config-content-length-99999999999", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-345-cl-rmsend-content-length-2147483648", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-346-cl-config-content-length-2147483648", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-347-cl-rmsend-content-length-4294967296", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-348-cl-config-content-length-4294967296", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-359-cl-rmsend-content-length-65536", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-360-cl-config-content-length-65536", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-361-cl-rmsend-content-length", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-362-cl-config-content-length", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-368-cl-dup-diff", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-369-cl-dup-diff2", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-370-cl-huge-nobody", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-371-cl-huge-nobody__closed", "B6", 3, "Content-Length not plain digits, over 65535 or a second differing value: 400 before any handler"},
    {"mut-432-http-in-msg-raw", "B3", 1, "raw blank inside the target (request line no longer split by the last blank): 400"},
    // clang-format on
};
static const size_t kRefusedCount = sizeof(kRefused) / sizeof(kRefused[0]);

static void test_corpus_refused_requests_are_listed(void)
{
    Corpus c = runCorpus();
    size_t b3 = 0, b6 = 0, corp = 0;
    std::set<std::string> seen;
    for (size_t k = 0; k < kRefusedCount; k++)
    {
        TEST_ASSERT_TRUE_MESSAGE(seen.insert(kRefused[k].name).second, kRefused[k].name); // no duplicate row
        auto it = c.refusedSame.find(kRefused[k].name);
        TEST_ASSERT_TRUE_MESSAGE(it != c.refusedSame.end(), kRefused[k].name); // no stale entry
        TEST_ASSERT_EQUAL_INT_MESSAGE(kRefused[k].pr, it->second, kRefused[k].name);
        const std::string b = kRefused[k].bid;
        if (b == "B3")
        {
            b3++;
            TEST_ASSERT_TRUE_MESSAGE(it->second == WRQ_BAD_REQUEST_LINE || it->second == WRQ_BAD_VERSION, kRefused[k].name);
        }
        else if (b == "B6")
        {
            b6++;
            TEST_ASSERT_EQUAL_INT_MESSAGE(WRQ_BAD_CONTENT_LENGTH, it->second, kRefused[k].name);
        }
        else if (b == "CORPUS")
            corp++;
        else
            TEST_FAIL_MESSAGE(kRefused[k].name);
    }
    TEST_ASSERT_EQUAL_UINT32(c.refusedSame.size(), kRefusedCount); // no unlisted entry
    printf("kRefused by ID: B3 %zu B6 %zu CORPUS %zu\n", b3, b6, corp);
    TEST_ASSERT_EQUAL_UINT32(19, b3);
    TEST_ASSERT_EQUAL_UINT32(20, b6);
    TEST_ASSERT_EQUAL_UINT32(1, corp);
}

static void test_corpus_equals_todays_router_except_listed_deltas(void)
{
    Corpus c = runCorpus();
    // the compared set is large enough to mean something
    TEST_ASSERT_TRUE(c.total >= 1200);
    TEST_ASSERT_TRUE(c.identical >= 700);
    TEST_ASSERT_EQUAL_UINT32(c.total, c.skipped + c.identical + c.outcomeSame + c.diffs.size() + c.refusedSame.size());
    std::map<std::string, const Cmp *> actual;
    for (size_t i = 0; i < c.diffs.size(); i++)
        actual[c.diffs[i].name] = &c.diffs[i];
    // no unlisted difference
    for (size_t i = 0; i < c.diffs.size(); i++)
    {
        bool listed = false;
        for (size_t k = 0; k < kDeltaCount; k++)
            listed = listed || c.diffs[i].name == kDeltas[k].name;
        if (!listed)
        {
            static char msg[300];
            snprintf(msg, sizeof(msg), "unlisted difference: %s: today %s, table %s: %s", c.diffs[i].name.c_str(),
                     c.diffs[i].today.c_str(), c.diffs[i].target.c_str(), c.diffs[i].line.c_str());
            TEST_FAIL_MESSAGE(msg);
        }
    }
    // no stale entry, every entry states what really happens
    size_t b1 = 0, b2 = 0, b3 = 0, env = 0, corp = 0;
    std::set<std::string> names;
    for (size_t k = 0; k < kDeltaCount; k++)
    {
        TEST_ASSERT_TRUE_MESSAGE(names.insert(kDeltas[k].name).second, kDeltas[k].name); // no duplicate row
        auto it = actual.find(kDeltas[k].name);
        TEST_ASSERT_TRUE_MESSAGE(it != actual.end(), kDeltas[k].name); // stale: no difference any more
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kDeltas[k].today, it->second->today.c_str(), kDeltas[k].name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(routeClass(kDeltas[k].target).c_str(), it->second->target.c_str(), kDeltas[k].name);
        TEST_ASSERT_TRUE_MESSAGE(kDeltas[k].reason[0] != 0, kDeltas[k].name);
        const std::string b = kDeltas[k].bid;
        if (b == "B1")
            b1++;
        else if (b == "B2")
            b2++;
        else if (b == "B3")
            b3++;
        else if (b == "ENV")
            env++;
        else if (b == "CORPUS")
            corp++;
        else
            TEST_FAIL_MESSAGE(kDeltas[k].name);
        // the label agrees with the parser: B3 is a request line webReqParse refuses (400), a corpus artifact is the
        // relative target "GET ?page=..." (also refused), every other row is a request the parser accepts
        const int pr = it->second->pr;
        if (b == "B3")
            TEST_ASSERT_TRUE_MESSAGE(pr == WRQ_BAD_REQUEST_LINE || pr == WRQ_BAD_VERSION, kDeltas[k].name);
        else if (b == "CORPUS")
            TEST_ASSERT_TRUE_MESSAGE(pr == WRQ_BAD_REQUEST_LINE && it->second->path.n == 0 &&
                                         it->second->line.compare(0, 7, "GET ?pa") == 0,
                                     kDeltas[k].name);
        else
            TEST_ASSERT_EQUAL_INT_MESSAGE(WRQ_OK, pr, kDeltas[k].name);
    }
    TEST_ASSERT_EQUAL_UINT32(c.diffs.size(), kDeltaCount);
    // the counts per B-ID are pinned
    printf("kDeltas by B-ID: B1 %zu B2 %zu B3 %zu ENV %zu CORPUS %zu\n", b1, b2, b3, env, corp);
    TEST_ASSERT_EQUAL_UINT32(59, b1);
    TEST_ASSERT_EQUAL_UINT32(39, b2);
    TEST_ASSERT_EQUAL_UINT32(21, b3);
    TEST_ASSERT_EQUAL_UINT32(1, env);
    TEST_ASSERT_EQUAL_UINT32(16, corp);
    // the pinned shape of the comparison (printed by the report, pinned here)
    TEST_ASSERT_EQUAL_UINT32(1247, c.total);
    TEST_ASSERT_EQUAL_UINT32(290, c.skipped);
    TEST_ASSERT_EQUAL_UINT32(777, c.identical);
    TEST_ASSERT_EQUAL_UINT32(40, c.refusedSame.size());
    TEST_ASSERT_EQUAL_UINT32(4, c.outcomeSame);
}

// the corpus reaches (identically) every route that is not a build variant or a login that needs a password profile
static void test_corpus_reaches_every_route(void)
{
    Corpus c = runCorpus();
    std::string missing;
    for (int r = WR_SCAFFOLD; r < WR_COUNT; r++)
        if (c.hit.find((uint8_t)r) == c.hit.end())
            missing += routeClass((uint8_t)r) + "(" + std::to_string(r) + ") ";
    TEST_ASSERT_EQUAL_STRING_MESSAGE("screen_rgb565(21) ", missing.c_str(), "routes without an identical corpus request");
}

static void test_corpus_dump(void)
{
    const char *path = getenv("WEB_ROUTE_DUMP"); // maintenance: write the differences as table rows to this file
    if (path == NULL)
        return;
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    Corpus c = runCorpus();
    fprintf(f, "CORPUS total=%zu skipped=%zu identical=%zu outcomeSame=%zu diffs=%zu\n", c.total, c.skipped,
            c.identical, c.outcomeSame, c.diffs.size());
    for (auto &kv : c.skipReasons)
        fprintf(f, "SKIP %zu %s : %s\n", kv.second, kv.first.c_str(), c.skipNames[kv.first].substr(0, 400).c_str());
    for (auto &d : c.diffs)
        fprintf(f, "DELTA|%s|%s|%s|%s|pr=%d\n", d.name.c_str(), d.today.c_str(), d.target.c_str(), d.line.c_str(), d.pr);
    for (auto &kv : c.refusedSame)
        fprintf(f, "REFUSED|%s|pr=%d\n", kv.first.c_str(), kv.second);
    fclose(f);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_shape);
    RUN_TEST(test_every_row_is_reachable);
    RUN_TEST(test_scaffold_and_selectors);
    RUN_TEST(test_page_shaped);
    RUN_TEST(test_login_family);
    RUN_TEST(test_exact_paths_and_case);
    RUN_TEST(test_api_prefix_rows_keep_the_trailing_slash);
    RUN_TEST(test_method_masks);
    RUN_TEST(test_span_edges);
    RUN_TEST(test_flags);
    RUN_TEST(test_state_flag_mirrors_the_guard);
    RUN_TEST(test_corpus_equals_todays_router_except_listed_deltas);
    RUN_TEST(test_corpus_refused_requests_are_listed);
    RUN_TEST(test_corpus_reaches_every_route);
    RUN_TEST(test_corpus_dump);
    return UNITY_END();
}
