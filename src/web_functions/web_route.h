// web_route.h -- the route table of the web server (FORMAL-02 wave 7, docs/web-server-seams-20261127.md 6.3).
//
// Header-only, no String, no Arduino, no heap: compiles natively and on ESP32/nRF52. Wired into work_webpage for a
// parsed request (B2 exact match, wave 7b step 2; B1 404/405 instead of the scaffold, step 3); a request the parser
// refuses (B3 request line, B6 Content-Length) is a 400 right after the guard since step 4 and never reaches the router (the
// legacy indexOf()/startsWith() chain is gone since the end of wave 7b). The rule (7.2 B1, B2):
//   - the path equals a table path exactly (byte compare, case-sensitive);
//   - for the rows with a selector the first query pair decides (page=<name>, sendmessage, getmessages, ...);
//   - the method must be in the row's mask;
//   - no other byte of the request is read (no header text, no Referer, no query value beyond the selector).
// Anything else is WR_NONE (the dispatcher answers 404 / 405, B1: webRouteRefusal). Constant loop bounds, explicit lengths, no
// recursion, no strlen on a span: the shape the ESBMC proof of 6.8 needs.
//
// Every row names the branch of today's router it replaces (web_functions.cpp line numbers, state 2026-11-27).
// The rows are listed unconditionally: availability per board (/mcau.js and /austatus ESP32 only :999 / :1371,
// /?page=mailbox ENABLE_MSGSTORE :1454, /screen.rgb565 T-Deck INSTRUMENT :1403, /screen.oled OLED_SHOT_WEB :1409) stays
// the dispatcher's business: a route whose feature is not built is answered 404 like WR_NONE.
//
// DECISIONS FOR THE WIRING WAVE (operator, 2026-11-27; the table itself holds no policy, the dispatcher applies these;
// (a)-(c) are wired since step 3, (d) is not yet):
//   (a) No session (a password is set and the cookie is not live): a PAGE-SHAPED request, webRouteIsPageShaped() (GET,
//       path "/", first query key "page"), gets the login page whether the page name is known or not (as today: the
//       answer reveals nothing). Every other WR_NONE gets 404 (unknown path or selector) or 405 (known path, wrong
//       method); no body that tells whether a route exists beyond that.
//   (b) HEAD is not in any method mask: it gets 405 with an Allow header built from the row's method mask (GET for
//       pages, POST for /rmsend and POST /config) (B1; today HEAD reaches the handlers).
//   (c) The dispatcher wipes the request head (web_header) and the request line (wg_path) on EVERY exit after the head is
//       complete (guard 403, AU-busy 503, asset, login nonce / login / logout / legacy password, dispatch; one helper,
//       web_wipe_request), not tied to a route: a request such as GET /rmsend?pw=x (405) must leave no "pw" behind in any
//       buffer. test_web_diff (test_live_leaves_no_request_bytes) checks both buffers after every corpus request.
//   (d) "/?nodepassword", "/?nodepassword=" and "/?nodepassword=<pw>" are ONE route, WR_LEGACY_PW; the handler branches
//       on the value: missing or empty -> logout and cookie clear (today :1268); non-empty -> wipe and 403 plaintext
//       (today :1277), the value is neither used nor logged. "/?nodepassword&x" (today the scaffold, :1268 compares the
//       whole line) now reaches the same handler and logs out: B2, pinned by a unit assertion in test_web_route (the
//       corpus has no such request, so there is no kDeltas row).
#ifndef WEB_ROUTE_H
#define WEB_ROUTE_H

#include <stddef.h>
#include <stdint.h>

#include "web_request.h" // WebSpan, WebMethod

#ifndef WEB_ROUTE_STR_MAX
#define WEB_ROUTE_STR_MAX 24 // longest table literal + 1 (path, key, value): the constant bound of every compare loop
#endif
static_assert(WEB_ROUTE_STR_MAX == WEB_LIT_MAX, "route compare bound and the shared literal bound must agree");

enum WebRoute : uint8_t
{
    WR_NONE = 0,
    WR_SCAFFOLD,
    WR_ASSET_CSS,
    WR_ASSET_JS,
    WR_ASSET_RMJS,
    WR_ASSET_AUJS,
    WR_LOGIN_NONCE,
    WR_LOGIN,
    WR_LOGOUT,
    WR_LEGACY_PW,
    WR_RM_SEND,
    WR_RM_STATUS,
    WR_RM_PASSWD,
    WR_WEB_PASSWD,
    WR_RM_NODES_GET,
    WR_RM_NODES_POST,
    WR_RM_HEARD,
    WR_PIN_STATUS,
    WR_PIN_ASSIGN,
    WR_GPIO_STATUS,
    WR_AU_STATUS,
    WR_SCREEN_RGB565,
    WR_SCREEN_OLED,
    WR_CALLFUNCTION,
    WR_SENDMESSAGE,
    WR_GETMESSAGES,
    WR_SETPARAM,
    WR_GETPARAM,
    WR_CONFIG_GET,
    WR_CONFIG_POST,
    WR_PAGE_BASE, // one route per page from here
    WR_PAGE_SETUP = WR_PAGE_BASE,
    WR_PAGE_POSITION,
    WR_PAGE_WX,
    WR_PAGE_MHEARD,
    WR_PAGE_NEIGHBOURS,
    WR_PAGE_MESSAGES,
    WR_PAGE_MAILBOX,
    WR_PAGE_RXLOG,
    WR_PAGE_PATH,
    WR_PAGE_SPECTRUM,
    WR_PAGE_MCP23017,
    WR_PAGE_PINS,
    WR_PAGE_INFO,
    WR_PAGE_REMOTE,
    WR_COUNT
};

// PAGE: the dispatcher sends the 200 TEXT head; NO_AUTH: served before the auth gate; BODY: needs Content-Length;
// STATE: mirrors webGuardIsStateChanging (any POST, /setparam, /callfunction, /?sendmessage, /?login=, /?logout,
// /?nodepassword, /rmsend, /rmpasswd)
enum : uint8_t
{
    WRF_PAGE = 1,
    WRF_NO_AUTH = 2,
    WRF_BODY = 4,
    WRF_STATE = 8
};

// method masks: bit (1 << WebMethod)
enum : uint8_t
{
    WRM_GET = (uint8_t)(1u << WM_GET),
    WRM_POST = (uint8_t)(1u << WM_POST)
};

// key/val select on the FIRST query pair of the row's path:
//   key NULL        the query is ignored
//   key ""          the query must be empty
//   key "name="     the query starts with "name=" (any value, also empty)
//   key "name"      the first pair is "name" alone or "name=<val>", followed by end or '&'; val NULL = any value,
//                   val set = exactly that value
struct WebRouteDef
{
    const char *path;
    const char *key;
    const char *val;
    uint8_t methods;
    uint8_t route;
    uint8_t flags;
};

#define WEB_ROUTE_ROWS_FULL 43
#ifdef WEB_ROUTE_PROOF
#define WEB_ROUTE_ROWS 6 // proof tier: the first six rows
#else
#define WEB_ROUTE_ROWS WEB_ROUTE_ROWS_FULL
#endif

// The table. Rows with the same path differ in method (rmnodes) or selector. The comment names today's branch.
inline const WebRouteDef *webRouteRows(uint8_t *n)
{
    static const WebRouteDef rows[WEB_ROUTE_ROWS_FULL] = {
        // clang-format off
        {"/",             "",           NULL,         WRM_GET,  WR_SCAFFOLD,       0},                                  // :1503 else -> deliver_scaffold; :1302 / gate scaffold
        {"/mc.css",       NULL,         NULL,         WRM_GET,  WR_ASSET_CSS,      WRF_NO_AUTH},                        // deliver_web_asset :983 startsWith("GET /mc.css")
        {"/mc.js",        NULL,         NULL,         WRM_GET,  WR_ASSET_JS,       WRF_NO_AUTH},                        // :988
        {"/mcrm.js",      NULL,         NULL,         WRM_GET,  WR_ASSET_RMJS,     WRF_NO_AUTH},                        // :993
        {"/mcau.js",      NULL,         NULL,         WRM_GET,  WR_ASSET_AUJS,     WRF_NO_AUTH},                        // :999 (ESP32 only)
        {"/",             "loginnonce", NULL,         WRM_GET,  WR_LOGIN_NONCE,    WRF_NO_AUTH},                        // :1217 startsWith("/?loginnonce")
        {"/",             "login=",     NULL,         WRM_GET,  WR_LOGIN,          WRF_NO_AUTH | WRF_STATE},            // :1233 startsWith("/?login=")
        {"/",             "logout",     NULL,         WRM_GET,  WR_LOGOUT,         WRF_NO_AUTH | WRF_STATE},            // :1268 startsWith("/?logout")
        {"/",             "nodepassword", NULL,       WRM_GET,  WR_LEGACY_PW,      WRF_NO_AUTH | WRF_STATE},            // :1268 "/?nodepassword[=]" (logout) and :1275 "/?nodepassword=<pw>" (403 plaintext)
        {"/rmsend",       NULL,         NULL,         WRM_POST, WR_RM_SEND,        WRF_BODY | WRF_STATE},               // :1319 startsWith("POST /rmsend"); :1323 "GET /rmsend" (405) becomes the generic 405 (B1)
        {"/rmstatus",     NULL,         NULL,         WRM_GET,  WR_RM_STATUS,      0},                                  // :1329
        {"/rmpasswd",     NULL,         NULL,         WRM_POST, WR_RM_PASSWD,      WRF_BODY | WRF_STATE},               // :1333
        {"/rmwebpwd",     NULL,         NULL,         WRM_POST, WR_WEB_PASSWD,     WRF_BODY | WRF_STATE},               // :1337
        {"/rmnodes",      NULL,         NULL,         WRM_GET,  WR_RM_NODES_GET,   0},                                  // :1353
        {"/rmnodes",      NULL,         NULL,         WRM_POST, WR_RM_NODES_POST,  WRF_BODY | WRF_STATE},               // :1357
        {"/rmheard",      NULL,         NULL,         WRM_GET,  WR_RM_HEARD,       0},                                  // :1361
        {"/pinstatus",    NULL,         NULL,         WRM_GET,  WR_PIN_STATUS,     0},                                  // :1341
        {"/pinassign",    NULL,         NULL,         WRM_GET,  WR_PIN_ASSIGN,     0},                                  // :1345
        {"/gpiostatus",   NULL,         NULL,         WRM_GET,  WR_GPIO_STATUS,    0},                                  // :1349
        {"/austatus",     NULL,         NULL,         WRM_GET,  WR_AU_STATUS,      0},                                  // :1371 (ESP32 only)
        {"/screen.rgb565", NULL,        NULL,         WRM_GET,  WR_SCREEN_RGB565,  0},                                  // :1403 (T-Deck INSTRUMENT only)
        {"/screen.oled",  NULL,         NULL,         WRM_GET,  WR_SCREEN_OLED,    0},                                  // :1409 (OLED_SHOT_WEB only)
        {"/callfunction/", NULL,        NULL,         WRM_GET,  WR_CALLFUNCTION,   WRF_STATE},                          // :1377 indexOf("/callfunction/")
        {"/",             "sendmessage", NULL,        WRM_GET,  WR_SENDMESSAGE,    WRF_STATE},                          // :1382 indexOf("/?sendmessage")
        {"/",             "getmessages", NULL,        WRM_GET,  WR_GETMESSAGES,    0},                                  // :1387 indexOf("/?getmessages")
        {"/setparam/",    NULL,         NULL,         WRM_GET,  WR_SETPARAM,       WRF_STATE},                          // :1392 indexOf("/setparam/")
        {"/getparam/",    NULL,         NULL,         WRM_GET,  WR_GETPARAM,       0},                                  // :1397 indexOf("/getparam/")
        {"/config.json",  NULL,         NULL,         WRM_GET,  WR_CONFIG_GET,     0},                                  // :1414 indexOf("/config.json")
        {"/config",       NULL,         NULL,         WRM_POST, WR_CONFIG_POST,    WRF_BODY | WRF_STATE},               // :1419 indexOf("POST /config")
        {"/",             "page",       "setup",      WRM_GET,  WR_PAGE_SETUP,     WRF_PAGE},                           // :1424
        {"/",             "page",       "position",   WRM_GET,  WR_PAGE_POSITION,  WRF_PAGE},                           // :1429
        {"/",             "page",       "wx",         WRM_GET,  WR_PAGE_WX,        WRF_PAGE},                           // :1434
        {"/",             "page",       "mheard",     WRM_GET,  WR_PAGE_MHEARD,    WRF_PAGE},                           // :1439
        {"/",             "page",       "neighbours", WRM_GET,  WR_PAGE_NEIGHBOURS, WRF_PAGE},                          // :1444
        {"/",             "page",       "messages",   WRM_GET,  WR_PAGE_MESSAGES,  WRF_PAGE},                           // :1449
        {"/",             "page",       "mailbox",    WRM_GET,  WR_PAGE_MAILBOX,   WRF_PAGE},                           // :1455
        {"/",             "page",       "rxlog",      WRM_GET,  WR_PAGE_RXLOG,     WRF_PAGE},                           // :1461
        {"/",             "page",       "path",       WRM_GET,  WR_PAGE_PATH,      WRF_PAGE},                           // :1466
        {"/",             "page",       "spectrum",   WRM_GET,  WR_PAGE_SPECTRUM,  WRF_PAGE},                           // :1471
        {"/",             "page",       "mcp23017",   WRM_GET,  WR_PAGE_MCP23017,  WRF_PAGE},                           // :1476 (the second mcp23017 branch :1481 is dead, D9)
        {"/",             "page",       "pins",       WRM_GET,  WR_PAGE_PINS,      WRF_PAGE},                           // :1486
        {"/",             "page",       "info",       WRM_GET,  WR_PAGE_INFO,      WRF_PAGE},                           // :1491
        {"/",             "page",       "remote",     WRM_GET,  WR_PAGE_REMOTE,    WRF_PAGE},                           // :1365 (the guard's remote page, WEB_GUARD)
        // clang-format on
    };
    *n = WEB_ROUTE_ROWS;
    return rows;
}

namespace webroute_detail
{
// span == literal, explicit lengths: the literal's NUL must sit exactly at index an (no prefix match)
inline bool eq(const char *a, uint16_t an, const char *lit)
{
    for (uint8_t i = 0; i < WEB_ROUTE_STR_MAX; i++)
    {
        if (lit[i] == 0)
            return i == an;
        if (i >= an || a[i] != lit[i])
            return false;
    }
    return false; // literal longer than the bound: never in the table
}

// does the first query pair of q satisfy (key, val)? reads at most webLitLen(key) + webLitLen(val) + 2 bytes
inline bool selector(WebSpan q, const char *key, const char *val)
{
    if (key == NULL)
        return true;
    const uint8_t kl = (uint8_t)webLitLen(key);
    if (kl == 0)
        return q.n == 0;
    if (q.n < kl)
        return false;
    for (uint8_t i = 0; i < kl; i++)
        if (q.p[i] != key[i])
            return false;
    if (key[kl - 1] == '=')
        return true; // "name=" prefix key: the value is not read
    if (q.n == kl)
        return val == NULL; // the key alone
    const char c = q.p[kl];
    if (val == NULL)
        return c == '&' || c == '=';
    if (c != '=')
        return false;
    const uint8_t vl = (uint8_t)webLitLen(val);
    if ((uint32_t)q.n < (uint32_t)kl + 1u + vl)
        return false;
    for (uint8_t i = 0; i < vl; i++)
        if (q.p[kl + 1 + i] != val[i])
            return false;
    const uint16_t end = (uint16_t)(kl + 1 + vl);
    return q.n == end || q.p[end] == '&';
}
} // namespace webroute_detail

// returns a WebRoute: the first row whose method, path and selector match; WR_NONE for anything else.
inline uint8_t webRouteMatch(uint8_t method, WebSpan path, WebSpan query)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    if (method >= 8)
        return WR_NONE;
    const uint8_t bit = (uint8_t)(1u << method);
    for (uint8_t i = 0; i < n; i++)
    {
        if ((rows[i].methods & bit) == 0)
            continue;
        if (!webroute_detail::eq(path.p, path.n, rows[i].path))
            continue;
        if (!webroute_detail::selector(query, rows[i].key, rows[i].val))
            continue;
        return rows[i].route;
    }
    return WR_NONE;
}

// decision (a) above: the request is shaped like a page request, whether or not the page name is known. GET, path
// exactly "/", the first query pair is "page" (alone or "page=<anything>", followed by end or '&').
inline bool webRouteIsPageShaped(uint8_t method, WebSpan path, WebSpan query)
{
    if (method != WM_GET)
        return false;
    if (!webroute_detail::eq(path.p, path.n, "/"))
        return false;
    return webroute_detail::selector(query, "page", NULL);
}

// flags of a route (0 for WR_NONE and for a route without a row)
inline uint8_t webRouteFlags(uint8_t route)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    for (uint8_t i = 0; i < n; i++)
        if (rows[i].route == route)
            return rows[i].flags;
    return 0;
}

// the dispatcher's 404 page, an id the table has no row for (named after the legacy router it came from); it continues
// the WebRoute numbering after WR_COUNT, so a table id never collides with it
static constexpr uint8_t WRL_PAGE_UNKNOWN = WR_COUNT; // "/?page=" with an unknown name: 404 page (the table: WR_NONE, B1)

// Is the handler of a route compiled into this build? The table lists every route; a route whose feature is not built
// is answered like WR_NONE (header comment). Pure part: one flag per feature, so a unit test can set them all.
inline bool webRouteBuiltFor(uint8_t route, bool esp32, bool msgstore, bool oled_shot, bool rgb565)
{
    switch (route)
    {
    case WR_ASSET_AUJS: // GET /mcau.js
    case WR_AU_STATUS:  // GET /austatus
        return esp32;
    case WR_PAGE_MAILBOX: // GET /?page=mailbox
        return msgstore;
    case WR_SCREEN_OLED: // GET /screen.oled
        return oled_shot;
    case WR_SCREEN_RGB565: // GET /screen.rgb565
        return rgb565;
    default:
        return true;
    }
}

// The build's features as a mask. web_route.h never reads a build macro itself (an inline function that reads macros
// depends on the include order: OLED_SHOT_WEB is defined in web_oled_shot.h, after this header in web_functions.cpp).
// webRouteDecide in web_functions.cpp computes the mask from EXACTLY the conditions the router switch puts around its
// cases, after every include (test_web_route reads web_functions.cpp and checks the lines):
//   WR_AU_STATUS      switch   #if defined(ESP32)
//   WR_ASSET_AUJS     asset    #if defined(ESP32)                (deliver_web_asset, served before the router)
//   WR_SCREEN_RGB565  switch   #if INSTRUMENT_ENABLED && (defined(BOARD_T_DECK) || defined(BOARD_T_DECK_PLUS))
//   WR_SCREEN_OLED    switch   #if OLED_SHOT_WEB
//   WR_PAGE_MAILBOX   switch   #if defined(ENABLE_MSGSTORE)
static constexpr uint8_t WRB_ESP32 = 0x01, WRB_MSGSTORE = 0x02, WRB_OLED_SHOT = 0x04, WRB_RGB565 = 0x08;
static constexpr uint8_t WRB_ALL = WRB_ESP32 | WRB_MSGSTORE | WRB_OLED_SHOT | WRB_RGB565;
inline bool webRouteBuiltMask(uint8_t route, uint8_t built)
{
    return webRouteBuiltFor(route, (built & WRB_ESP32) != 0, (built & WRB_MSGSTORE) != 0, (built & WRB_OLED_SHOT) != 0,
                            (built & WRB_RGB565) != 0);
}

// Does the path (with the selector) have an exact row, for any method, whose handler is built? A route that is not
// built is no row. (Kept as the predicate form of webRouteRefusal; the dispatcher uses webRouteRefusal.)
inline bool webRouteExactOther(WebSpan path, WebSpan query, uint8_t built)
{
    for (uint8_t m = WM_GET; m <= WM_OPTIONS; m++)
    {
        const uint8_t r = webRouteMatch(m, path, query);
        if (r != WR_NONE && webRouteBuiltMask(r, built))
            return true;
    }
    return false;
}

// Answers of the dispatcher that are no table route (B1 and the no-session gate); they continue the numbering after
// WRL_PAGE_UNKNOWN, so a decision id never collides with a table id.
static constexpr uint8_t WRD_NOT_FOUND = WR_COUNT + 1;   // 404, JSON {"ok":false,"err":"not found"}
static constexpr uint8_t WRD_NOT_ALLOWED = WR_COUNT + 2; // 405 + Allow header, JSON {"ok":false,"err":"method not allowed"}
static constexpr uint8_t WRD_LOGIN_PAGE = WR_COUNT + 3;  // no session: 200 + the login page (decision (a))
static constexpr uint8_t WRD_UNAUTH = WR_COUNT + 4;      // no session: 401

// B1 refusal of a parsed request no built table route answers: the path (with the selector) has a built exact row for
// another method -> WRD_NOT_ALLOWED and *allow = the union of those rows' method masks (HEAD is in no mask); no such
// row -> WRD_NOT_FOUND and *allow = 0. allow may be NULL.
inline uint8_t webRouteRefusal(WebSpan path, WebSpan query, uint8_t built, uint8_t *allow)
{
    uint8_t n = 0;
    const WebRouteDef *rows = webRouteRows(&n);
    uint8_t mask = 0;
    for (uint8_t i = 0; i < n; i++)
    {
        if (!webroute_detail::eq(path.p, path.n, rows[i].path))
            continue;
        if (!webroute_detail::selector(query, rows[i].key, rows[i].val))
            continue;
        if (!webRouteBuiltMask(rows[i].route, built))
            continue;
        mask |= rows[i].methods;
    }
    if (allow != NULL)
        *allow = mask;
    return mask != 0 ? WRD_NOT_ALLOWED : WRD_NOT_FOUND;
}

// "Allow: GET, POST" (no CRLF: the extra_header of send_http_header) for a method mask (bit 1 << WebMethod), into
// buf[cap]. Returns the length written (the NUL excluded); 0 with buf[0] = 0 when the mask holds no method or the
// text does not fit (never truncated).
inline size_t webRouteFormatAllow(char *buf, size_t cap, uint8_t mask)
{
    static const char *const names[] = {"GET", "POST", "HEAD", "OPTIONS"}; // WM_GET .. WM_OPTIONS
    if (buf == NULL || cap == 0)
        return 0;
    buf[0] = 0;
    size_t len = 0;
    bool first = true;
    for (uint8_t i = 0; i < 4; i++)
    {
        if ((mask & (uint8_t)(1u << (WM_GET + i))) == 0)
            continue;
        const char *parts[2] = {first ? "Allow: " : ", ", names[i]};
        for (uint8_t k = 0; k < 2; k++)
            for (const char *c = parts[k]; *c != 0; c++)
            {
                if (len + 1 >= cap)
                {
                    buf[0] = 0;
                    return 0;
                }
                buf[len++] = *c;
            }
        first = false;
    }
    buf[len] = 0;
    return len;
}

// Decision of the router for a request (wave 7b step 3, B1), pure; webRouteDecide in web_functions.cpp gathers the inputs.
//   parsed        the request head parsed (webReqParse == WRQ_OK)
//   table         webRouteMatch of the parsed request (WR_NONE when not parsed)
//   page_shaped   webRouteIsPageShaped (decision (a)): an unknown or unbuilt page is the 404 page
//   refusal       webRouteRefusal: WRD_NOT_FOUND or WRD_NOT_ALLOWED
//   built         the build's feature mask (WRB_*), computed in web_functions.cpp
// Parsed: a table route whose handler is built goes to the table (one that is not built counts as WR_NONE); a
// page-shaped request is the 404 page (WRL_PAGE_UNKNOWN); anything else is the refusal. A refused parse is a 400
// before the router reads the route; should it ever be read, it is WRD_NOT_FOUND, never a handler.
inline uint8_t webRouteDecideCore(bool parsed, uint8_t table, bool page_shaped, uint8_t refusal, uint8_t built)
{
    if (!parsed)
        return WRD_NOT_FOUND;
    if (table != WR_NONE && webRouteBuiltMask(table, built))
        return table;
    if (page_shaped)
        return WRL_PAGE_UNKNOWN;
    return refusal == WRD_NOT_ALLOWED ? WRD_NOT_ALLOWED : WRD_NOT_FOUND;
}

// Which refusals of webReqParse the wiring answers 400 (B3 request line / version since wave 7b step 4 part B3, B6 Content-Length
// since part B6; 400 JSON {"ok":false,"err":"bad request"} right after the guard, before the AU-busy 503 and every route).
// Every non-OK code is a bad request, so a refused parse never reaches the router.
inline bool webRouteBadRequest(uint8_t wrq)
{
    return wrq == WRQ_BAD_REQUEST_LINE || wrq == WRQ_BAD_VERSION || wrq == WRQ_BAD_CONTENT_LENGTH;
}

// The no-session gate of a PARSED request (a password is set and the cookie is not live); route is webRouteDecideCore's
// answer. A page-shaped request gets the login page, known or unknown page (a); GET / (WR_SCAFFOLD) the login
// scaffold; a refusal (404 / 405) stays what it is (no body tells more than that); any other built route is 401.
// Returns WRD_LOGIN_PAGE, WR_SCAFFOLD, WRD_NOT_FOUND, WRD_NOT_ALLOWED or WRD_UNAUTH.
inline uint8_t webRouteNoSession(bool page_shaped, uint8_t route)
{
    if (page_shaped)
        return WRD_LOGIN_PAGE;
    if (route == WR_SCAFFOLD || route == WRD_NOT_FOUND || route == WRD_NOT_ALLOWED)
        return route;
    return WRD_UNAUTH;
}

#endif // WEB_ROUTE_H
