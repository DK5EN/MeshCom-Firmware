// Web server request-path differential (FORMAL-02, web server wave A step 0; docs/web-server-seams-20261127.md 7.1).
//
//   pio test -e native_web_diff            compare every entry point against test/test_web_diff/golden
//   WEB_DIFF_UPDATE=1 pio test -e native_web_diff   rewrite the golden from the frozen path (review the diff!)
//
// The rig is a differential: entry "frozen" is the reference copy of the request path (frozen/), entry "live" is the
// working-tree source cut out by name (live/web_request_live.cpp, generated; the checks below run for both).
// After every edit of an extracted function of src/web_functions/web_functions.cpp, web_rm_handlers.cpp or
// src/url_decode.cpp regenerate the live copy from the repository root:
//     python3 test/test_web_diff/live/extract_live.py
// test_live_copy_is_current goes red when an extracted segment of the source is no longer verbatim in the working tree.
// The unit under test is a function pointer table. Every corpus request (corpus/*.req, built by tools/web_corpus.py)
// runs under every profile through every entry; the record of the run (handler trace, every byte written, the close,
// the fake clock) is compared with golden/<name>.txt, and the live record with the frozen one. Intended deltas
// (B1-B10 of docs/web-server-seams-20261127.md 7.2) go into kWhitelist, keyed by corpus name (B2 in kWhitelist, B1 in kWhitelistB1, B3 in kWhitelistB3 and B6 in kWhitelistB6: all profiles, 400; B4 in kWhitelistB4: 422), each row naming the profiles where the live record differs.
#include <unity.h>

#include "web_route.h" // reads no build macro: the feature mask is computed in web_functions.cpp

#include "../support/repo_file.h"
#include "rig/web_rig.h"

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace web_frozen
{
void work_webpage_frozen(void);
}

namespace web_live
{
void work_webpage_live(void);
struct LiveSrc // generated table in live/web_request_live.cpp: verbatim source text of every extracted segment
{
    const char *file;
    const char *what;
    const char *text;
};
extern const LiveSrc kLiveSrc[];
extern const unsigned long kLiveSrcCount;
extern String web_header; // the head String of the live unit: test_live_leaves_no_request_bytes reads its buffer
}

void setUp(void) {}
void tearDown(void) {}

struct WebEntry
{
    const char *name;
    void (*run)(void);
};

static const WebEntry kEntries[] = {
    {"frozen", web_frozen::work_webpage_frozen},
    {"live", web_live::work_webpage_live},
};
static const size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// Expected deltas between the frozen reference and the live entry, per corpus request AND per profile. A request whose
// live record is byte-identical to the frozen one in a profile needs no entry for it; every profile where it differs is
// named in the row (test_whitelist_rows_differ_exactly_where_listed: a listed profile that does not differ, or a
// differing profile that is not listed, fails). `profiles` is a comma list of kProfiles names.
//
// B2 (wave 7b step 2): the route is the exact request-line route. A route token elsewhere (Referer, query value, longer
// path) no longer reaches a handler: the request gets what its exact route says (the scaffold for GET /, the login
// page without a session, 404 or 405 since step 3).
static const char *const kWhyB2 =
    "B2: route token outside the exact request-line route (Referer, query value, longer path): handler no longer reached, "
    "the exact route answers (scaffold, login page, 404 or 405)";
struct WhiteB2
{
    const char *why;
    const char *profiles;
};
static const std::map<std::string, WhiteB2> kWhitelist = {
    {"grd-091-x", {kWhyB2, "open"}},
    {"mut-145-referer-root-page-info", {kWhyB2, "open,password,password+session"}},
    {"mut-146-referer-unk-page-info", {kWhyB2, "open,password,password+session"}},
    {"mut-147-query-page-info", {kWhyB2, "open,password,password+session"}},
    {"mut-148-referer-setparam", {kWhyB2, "open"}},
    {"mut-149-referer-root-setparam", {kWhyB2, "open"}},
    {"mut-150-referer-unk-setparam", {kWhyB2, "open,password,password+session"}},
    {"mut-151-query-setparam", {kWhyB2, "open,password,password+session"}},
    {"mut-152-referer-callfunction", {kWhyB2, "open"}},
    {"mut-153-referer-root-callfunction", {kWhyB2, "open"}},
    {"mut-154-referer-unk-callfunction", {kWhyB2, "open,password,password+session"}},
    {"mut-155-query-callfunction", {kWhyB2, "open,password,password+session"}},
    {"mut-160-referer-config-json", {kWhyB2, "open"}},
    {"mut-161-referer-root-config-json", {kWhyB2, "open"}},
    {"mut-162-referer-unk-config-json", {kWhyB2, "open,password,password+session"}},
    {"mut-163-query-config-json", {kWhyB2, "open,password,password+session"}},
    {"mut-164-referer-getparam", {kWhyB2, "open"}},
    {"mut-165-referer-root-getparam", {kWhyB2, "open"}},
    {"mut-166-referer-unk-getparam", {kWhyB2, "open,password,password+session"}},
    {"mut-167-query-getparam", {kWhyB2, "open,password,password+session"}},
    {"mut-168-referer-sendmessage", {kWhyB2, "open"}},
    {"mut-169-referer-root-sendmessage", {kWhyB2, "open"}},
    {"mut-170-referer-unk-sendmessage", {kWhyB2, "open,password,password+session"}},
    {"mut-171-query-sendmessage", {kWhyB2, "open,password,password+session"}},
    {"mut-172-route-page-infox", {kWhyB2, "open"}},
    {"mut-175-route-page-setupx", {kWhyB2, "open"}},
    {"mut-177-route-x-page-info", {kWhyB2, "open,password,password+session"}},
    {"mut-178-route-pinstatusx", {kWhyB2, "open,password,password+session"}},
    {"mut-184-route-rmstatus", {kWhyB2, "open,password,password+session"}},
    {"mut-186-route-screen-oledx", {kWhyB2, "open,password,password+session"}},
    {"mut-187-route-config-jsonx", {kWhyB2, "open,password,password+session"}},
    {"mut-192-route-austatusx", {kWhyB2, "open,password,password+session"}},
    {"mut-429-http-in-path", {kWhyB2, "open,password,password+session"}},
};

// B1 (wave 7b step 3): in every profile named in `profiles` the live entry answers `status` (and carries the Allow header
// line `allow` when it is not empty); the frozen record answers something else (scaffold, a handler, the old 405).
struct WhiteB1
{
    const char *why;
    const char *profiles;
    int status;
    const char *allow;
};
static const char *const kWhyB1NoRoute = "B1: no route for this path or query: 404 JSON instead of the scaffold";
static const char *const kWhyB1WrongMethod = "B1: the path has a route for other methods only: 405 + Allow instead of the scaffold or the handler";
static const char *const kWhyB1NoSession =
    "B1: no session: the gate decides on the parsed request line (not on a \"/?page=\" in a header, not on \"HTTP/1.1\" anywhere): a built route is a 401";
static const std::map<std::string, WhiteB1> kWhitelistB1 = {
    {"cap-007-favicon-ico", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"grd-093-index-js", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"grd-108-ota-upload", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"grd-109-ota-upload", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"grd-320-img-rmsend-dst-x-cmd-reboot", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"grd-322-img-rmpasswd-pw-x", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"js-003-tmpl-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"js-005-tmpl-rmpasswd", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"js-009-tmpl-rmwebpwd", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"js-014-tmpl-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"js-067-get-favicon-ico", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"js-068-get-ota-state", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"js-296-post-cfg-config-json", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-088-meth-post", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-089-meth-post-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-090-meth-post-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-093-meth-head", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-094-meth-head-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-095-meth-head-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-096-meth-head-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-097-meth-head-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-098-meth-options", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-099-meth-options-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-100-meth-options-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-101-meth-options-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-102-meth-options-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-103-meth-put", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-104-meth-put-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-105-meth-put-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-106-meth-put-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-107-meth-put-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-108-meth-delete", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-109-meth-delete-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-110-meth-delete-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-111-meth-delete-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-112-meth-delete-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-113-meth-get", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-114-meth-get-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-115-meth-get-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-116-meth-get-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-117-meth-get-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-118-meth-get", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-119-meth-get-page-info", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-120-meth-get-setparam-x-1", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-121-meth-get-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-122-meth-get-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-158-referer-unk-rmsend", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-159-query-rmsend", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-176-route-xpage-info", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-180-route-xpinstatus", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-183-route-mc-js", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-199-route-nodepasswordx", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"mut-378-rm-get-rmpasswd", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-380-rm-get-rmwebpwd", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-384-rm-get-rmsend", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-387-rm-post-rmstatus", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-389-rm-post-rmheard", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: GET"}},
    {"mut-390-rm-get-config", {kWhyB1WrongMethod, "open,password,password+session", 405, "Allow: POST"}},
    {"mut-402-head-over-window-route-late", {kWhyB1NoRoute, "open,password,password+session", 404, ""}},
    {"cap-028-rmstatus", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-029-rmnodes", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-030-rmheard", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-031-getparam-gateway", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-032-getparam-tdecktheme", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-058-pinstatus-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-059-pinstatus-http-1-0-nohdr", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-068-rmstatus-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-069-rmstatus-http-1-0-nohdr", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-418-nul-in-reqline", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-033-config", {"B1: GET on a POST-only route /config (the Referer page token no longer routes): 405 + Allow", "open,password,password+session", 405, "Allow: POST"}},
    {"js-059-get-screen-rgb565", {"B1: route not built in this board (T-Deck INSTRUMENT only): 404 instead of the scaffold", "open,password,password+session", 404, ""}},
    {"mut-185-route-rmsendx", {"B1: unknown path /rmsendx (no exact row): 404, no longer the GET /rmsend 405 branch", "open,password,password+session", 404, ""}},
};


// B3 (wave 7b step 4 part B3): the request line is not exactly `METHOD SP target SP HTTP/1.x` (webReqParse: WRQ_BAD_REQUEST_LINE /
// WRQ_BAD_VERSION). The live entry answers 400 right after the guard in EVERY profile (also with a session, also behind the
// AU session), the frozen record answers whatever the legacy indexOf() chain made of it (a handler, the scaffold, 404, 405,
// the login page, 401). Rows carry only the reason: profiles are all of kProfiles, the status is 400 with the JSON body
// {"ok":false,"err":"bad request"}.
static const char *const kWhyB3Version = "B3: version other than HTTP/1.0 or HTTP/1.1: 400";
static const char *const kWhyB3Line = "B3: request line without method, version or single blanks (no version, tab, blank prefix, double blank, no target): 400";
static const char *const kWhyB3Blank = "B3: raw blank inside the target (query, page name): 400";
static const std::map<std::string, const char *> kWhitelistB3 = {
    {"js-018-pageq-info", kWhyB3Blank},
    {"js-020-pageq-position", kWhyB3Blank},
    {"js-022-pageq-wx", kWhyB3Blank},
    {"js-024-pageq-setup", kWhyB3Blank},
    {"js-026-pageq-messages", kWhyB3Blank},
    {"js-028-pageq-mailbox", kWhyB3Blank},
    {"js-030-pageq-rxlog", kWhyB3Blank},
    {"js-032-pageq-path", kWhyB3Blank},
    {"js-034-pageq-mheard", kWhyB3Blank},
    {"js-036-pageq-neighbours", kWhyB3Blank},
    {"js-038-pageq-spectrum", kWhyB3Blank},
    {"js-040-pageq-mcp23017", kWhyB3Blank},
    {"js-042-pageq-pins", kWhyB3Blank},
    {"js-044-pageq-remote", kWhyB3Blank},
    {"js-046-pageq-nosuchpage", kWhyB3Blank},
    {"js-048-pageq", kWhyB3Blank},
    {"js-255-setval", kWhyB3Blank},
    {"mut-005-http-2-0", kWhyB3Version},
    {"mut-007-http-1-1x", kWhyB3Version},
    {"mut-009-nover", kWhyB3Line},
    {"mut-014-page-info-http-2-0", kWhyB3Version},
    {"mut-016-page-info-http-1-1x", kWhyB3Version},
    {"mut-018-page-info-nover", kWhyB3Line},
    {"mut-023-setparam-onewire-on-http-2-0", kWhyB3Version},
    {"mut-025-setparam-onewire-on-http-1-1x", kWhyB3Version},
    {"mut-027-setparam-onewire-on-nover", kWhyB3Line},
    {"mut-033-getparam-gateway-http-2-0", kWhyB3Version},
    {"mut-035-getparam-gateway-http-1-1x", kWhyB3Version},
    {"mut-037-getparam-gateway-nover", kWhyB3Line},
    {"mut-042-callfunction-sendpos-http-2-0", kWhyB3Version},
    {"mut-044-callfunction-sendpos-http-1-1x", kWhyB3Version},
    {"mut-046-callfunction-sendpos-nover", kWhyB3Line},
    {"mut-052-sendmessage-tocall-9999-message-hi-20the", kWhyB3Version},
    {"mut-054-sendmessage-tocall-9999-message-hi-20the", kWhyB3Version},
    {"mut-056-sendmessage-tocall-9999-message-hi-20the", kWhyB3Line},
    {"mut-062-pinstatus-http-2-0", kWhyB3Version},
    {"mut-064-pinstatus-http-1-1x", kWhyB3Version},
    {"mut-066-pinstatus-nover", kWhyB3Line},
    {"mut-072-rmstatus-http-2-0", kWhyB3Version},
    {"mut-074-rmstatus-http-1-1x", kWhyB3Version},
    {"mut-076-rmstatus-nover", kWhyB3Line},
    {"mut-082-mc-js-http-2-0", kWhyB3Version},
    {"mut-084-mc-js-http-1-1x", kWhyB3Version},
    {"mut-086-mc-js-nover", kWhyB3Line},
    {"mut-123-meth-none", kWhyB3Line},
    {"mut-124-meth-none-page-info", kWhyB3Line},
    {"mut-125-meth-none-setparam-x-1", kWhyB3Line},
    {"mut-126-meth-none-rmsend", kWhyB3Line},
    {"mut-127-meth-none-config", kWhyB3Line},
    {"mut-188-route-post-config", kWhyB3Line},
    {"mut-426-space-prefix", kWhyB3Line},
    {"mut-427-double-space", kWhyB3Line},
    {"mut-428-tab-sep", kWhyB3Line},
    {"mut-432-http-in-msg-raw", kWhyB3Blank},
    {"mut-435-setparam-no-http", kWhyB3Line},
    {"mut-436-setparam-http-first", kWhyB3Line},
    {"mut-437-path-only", kWhyB3Line},
};

// B6 (wave 7b step 4 part B6): Content-Length is not plain digits, a second Content-Length differs, or the value is over 65535
// (webReqParse: WRQ_BAD_CONTENT_LENGTH). Same answer as B3: 400 right after the guard in every profile; the frozen record
// answers what the legacy chain and its lenient length made of it. Rows carry only the reason (profiles: all of kProfiles).
static const char *const kWhyB6 = "B6: Content-Length not plain digits, a second differing value, or over 65535: 400";
static const std::map<std::string, const char *> kWhitelistB6 = {
    {"mut-337-cl-rmsend-content-length-5", kWhyB6},
    {"mut-338-cl-config-content-length-5", kWhyB6},
    {"mut-339-cl-rmsend-content-length-12abc", kWhyB6},
    {"mut-340-cl-config-content-length-12abc", kWhyB6},
    {"mut-341-cl-rmsend-content-length-abc", kWhyB6},
    {"mut-342-cl-config-content-length-abc", kWhyB6},
    {"mut-343-cl-rmsend-content-length-99999999999", kWhyB6},
    {"mut-344-cl-config-content-length-99999999999", kWhyB6},
    {"mut-345-cl-rmsend-content-length-2147483648", kWhyB6},
    {"mut-346-cl-config-content-length-2147483648", kWhyB6},
    {"mut-347-cl-rmsend-content-length-4294967296", kWhyB6},
    {"mut-348-cl-config-content-length-4294967296", kWhyB6},
    {"mut-359-cl-rmsend-content-length-65536", kWhyB6},
    {"mut-360-cl-config-content-length-65536", kWhyB6},
    {"mut-361-cl-rmsend-content-length", kWhyB6},
    {"mut-362-cl-config-content-length", kWhyB6},
    {"mut-368-cl-dup-diff", kWhyB6},
    {"mut-369-cl-dup-diff2", kWhyB6},
    {"mut-370-cl-huge-nobody", kWhyB6},
    {"mut-371-cl-huge-nobody__closed", kWhyB6},
};

// B3 (wave 7b step 5a): HTTP/1.0 keeps working for GET and setparam (seams 7.2 B3). setparam / getparam read name and value
// from the parsed query, so the version text of an HTTP/1.0 request line no longer lands in the value (setparam, D3) or
// in the name (getparam: the frozen copy cut at " HTTP/1.1" and found none). `profiles` as in B1; `trace` is the exact
// first trace line of the live entry in the open profile (the handler call with the clean name / value, status 200); a
// listed profile with a password and no session answers 401 as the B1 rows say (the frozen entry answers the login page
// scaffold). A profile that is not listed must be byte-identical.
struct WhiteB3Http10
{
    const char *why;
    const char *profiles;
    const char *trace;
    size_t at = 0; // index of that line in the trace (sendmessage: webTxBeginChunked and setMsgOrigin come first)
};
static const char *const kWhyB3Http10 = "B3: HTTP/1.0 request line: the version text no longer lands in the name / value of the parameter handler";
static const std::map<std::string, WhiteB3Http10> kWhitelistB3Http10 = {
    {"mut-020-setparam-onewire-on-http-1-0", {kWhyB3Http10, "open,password,password+session", "webSetup_setParam name='onewire' value='on'"}},
    {"mut-029-getparam-gateway-http-1-0", {kWhyB3Http10, "open,password,password+session", "webSetup_getParam name='gateway'"}},
    {"mut-030-getparam-gateway-http-1-0-nohdr", {kWhyB3Http10, "open,password,password+session", "webSetup_getParam name='gateway'"}},
    // step 5c: call_function and send_message read the parsed query; the version and header text no longer land in the name / message
    {"mut-039-callfunction-sendpos-http-1-0", {kWhyB3Http10, "open,password,password+session", "webFunctionCall name='sendpos' param=''"}},
    {"mut-048-sendmessage-tocall-9999-message-hi-20the", {kWhyB3Http10, "open,password,password+session", "sendMessage len=15 text=':{9999}hi there'", 2}},
};

// B4 (wave 7b step 5b): /setparam/?<query> without '=' (also "?" and a request without '?') answers 422 with
// {"returncode":1,"<name>":""} and calls no setter; `profiles` as in B1 (a password profile without a session answers 401 in both
// entries, so it is not listed); `name` is the key of the answer (the decoded, lower-cased query, empty for none).
struct WhiteB4
{
    const char *profiles;
    const char *name;
};
static const std::map<std::string, WhiteB4> kWhitelistB4 = {
    {"js-001-tmpl-setparam", {"open", ""}},
    {"mut-128-noeq-setparam-webserver", {"open", "webserver"}},
    {"mut-129-noeq-setparam", {"open", ""}},
    {"mut-130-noeq-setparam", {"open", ""}},
};

// B7 (wave 7b step 5b): a setparam value with "--" (or a leading "&&", or over 200 bytes) answers 422 {"returncode":1,"<name>":""} and
// calls no setter; the guard 403 stays first. `profiles` as in B1; `name` is the key of the answer.
static const std::map<std::string, WhiteB4> kWhitelistB7 = {
    {"grd-121-setparam-setcall-x-20-update-20install", {"open", "setcall"}},
};

static bool isWhitelisted(const std::string &n) { return kWhitelistB7.count(n) != 0 || kWhitelistB4.count(n) != 0 || kWhitelist.count(n) != 0 || kWhitelistB1.count(n) != 0 || kWhitelistB3.count(n) != 0 || kWhitelistB3Http10.count(n) != 0 || kWhitelistB6.count(n) != 0; }

static const WebProfile kProfiles[] = {
    {"open", "", "", false, false},
    {"password", "secret-pw", "", false, false},
    {"au-session+webhost", "", "node.example.org", true, false},
    {"password+session", "secret-pw", "", false, true},
};
static const size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

static const char *const kCorpusDir = "test/test_web_diff/corpus/";
static const char *const kGoldenDir = "test/test_web_diff/golden/";

static std::vector<std::string> corpusNames()
{
    std::vector<std::string> v;
    std::istringstream in(repo_slurp("test/test_web_diff/corpus/INDEX.txt"));
    for (std::string l; std::getline(in, l);)
        if (!l.empty())
            v.push_back(l);
    return v;
}

static bool closedVariant(const std::string &name)
{
    const std::string suffix = "__closed";
    return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// one corpus request under every profile, through one entry point
static size_t gRuns[sizeof(kEntries) / sizeof(kEntries[0])]; // runs per entry through recordAll (proof the entry ran)

static std::string recordAll(const WebEntry &e, const std::string &name, const std::string &bytes)
{
    std::string all;
    for (size_t p = 0; p < kProfileCount; p++)
    {
        webRigBegin(bytes, closedVariant(name), kProfiles[p]);
        gRuns[&e - kEntries]++;
        e.run();
        all += std::string("== profile ") + kProfiles[p].name + "\n" + webRigRecord();
    }
    return all;
}

static std::string readCorpus(const std::string &name)
{
    return repo_slurp((std::string(kCorpusDir) + name + ".req").c_str());
}

static bool updateMode()
{
    const char *u = getenv("WEB_DIFF_UPDATE");
    return u != NULL && u[0] == '1';
}

static void firstDiff(const std::string &want, const std::string &got, char *msg, size_t n)
{
    size_t i = 0;
    while (i < want.size() && i < got.size() && want[i] == got[i])
        i++;
    size_t ls = want.rfind('\n', i ? i - 1 : 0);
    ls = (ls == std::string::npos) ? 0 : ls + 1;
    snprintf(msg, n, "first difference at byte %zu, golden: '%s' / got: '%s'", i,
             webRigEsc(want.substr(ls, 70)).c_str(), webRigEsc(got.substr(ls, 70)).c_str());
}

// ---------------------------------------------------------------------------------------------- tests

// the corpus is the input of everything below: assert it is the size and mix the plan asks for
static void test_corpus_has_minimum_counts(void)
{
    const std::vector<std::string> names = corpusNames();
    std::map<std::string, int> bySource;
    size_t closed = 0;
    for (const std::string &n : names)
    {
        bySource[n.substr(0, n.find('-'))]++;
        closed += closedVariant(n) ? 1 : 0;
        std::string unused;
        TEST_ASSERT_TRUE_MESSAGE(repo_try_slurp((std::string(kCorpusDir) + n + ".req").c_str(), unused), n.c_str());
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1000, (int)names.size());
    TEST_ASSERT_EQUAL_INT_MESSAGE(34, bySource["cap"], "the real capture has 34 requests");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(250, bySource["js"]);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(100, bySource["doc"]);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(300, bySource["grd"]);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(400, bySource["mut"]);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(10, (int)closed);
}

// the golden set is complete: one file per corpus request, no stale ones
static void test_golden_set_is_complete(void)
{
    if (updateMode())
        TEST_IGNORE_MESSAGE("update mode");
    const std::vector<std::string> names = corpusNames();
    std::string idx;
    for (const std::string &n : names)
    {
        std::string g;
        TEST_ASSERT_TRUE_MESSAGE(repo_try_slurp((std::string(kGoldenDir) + n + ".txt").c_str(), g), n.c_str());
        TEST_ASSERT_TRUE_MESSAGE(g.size() > 100, n.c_str());
    }
    std::string list;
    TEST_ASSERT_TRUE(repo_try_slurp("test/test_web_diff/golden/INDEX.txt", list));
    TEST_ASSERT_EQUAL_STRING(repo_slurp("test/test_web_diff/corpus/INDEX.txt").c_str(), list.c_str());
}

// every entry point against the golden (frozen today: the golden is the frozen path's own record)
static void test_entries_match_golden(void)
{
    const std::vector<std::string> names = corpusNames();
    for (size_t k = 0; k < kEntryCount; k++)
    {
        int bad = 0;
        char first[512] = "";
        for (const std::string &n : names)
        {
            const std::string got = recordAll(kEntries[k], n, readCorpus(n));
            const std::string path = std::string(kGoldenDir) + n + ".txt";
            if (updateMode() && k == 0)
            {
                FILE *f = repo_fopen(path.c_str(), "wb");
                TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
                fwrite(got.data(), 1, got.size(), f);
                fclose(f);
                continue;
            }
            const std::string want = repo_slurp(path.c_str());
            // the frozen entry must match its golden byte for byte; the whitelist only covers later entries
            if (want != got && (k == 0 || !isWhitelisted(n)))
            {
                if (bad++ == 0)
                {
                    char d[400];
                    firstDiff(want, got, d, sizeof(d));
                    snprintf(first, sizeof(first), "%s: %s: %s", kEntries[k].name, n.c_str(), d);
                }
            }
        }
        if (updateMode() && k == 0)
        {
            FILE *f = repo_fopen("test/test_web_diff/golden/INDEX.txt", "wb");
            TEST_ASSERT_NOT_NULL(f);
            const std::string idx = repo_slurp("test/test_web_diff/corpus/INDEX.txt");
            fwrite(idx.data(), 1, idx.size(), f);
            fclose(f);
            continue;
        }
        char head[600];
        snprintf(head, sizeof(head), "%d of %zu requests differ; %s", bad, names.size(), first);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, head);
    }
}

// a second entry must equal the first one on every request (frozen vs live), except whitelisted names
static void test_entries_agree_with_the_first(void)
{
    TEST_ASSERT_TRUE_MESSAGE(kEntryCount >= 2, "the live entry must stay wired");
    const std::vector<std::string> names = corpusNames();
    for (size_t k = 1; k < kEntryCount; k++)
    {
        size_t whitelisted = 0;
        for (const std::string &n : names)
        {
            const std::string bytes = readCorpus(n);
            const bool same = recordAll(kEntries[0], n, bytes) == recordAll(kEntries[k], n, bytes);
            if (const char *dump = getenv("WEB_DIFF_DUMP_DIFFS")) // maintenance: list every differing request, no asserts
            {
                if (!same)
                    if (FILE *f = fopen(dump, "a"))
                    {
                        fprintf(f, "%s\n", n.c_str());
                        fclose(f);
                    }
                continue;
            }
            if (isWhitelisted(n))
            {
                // a whitelisted delta must really differ, else the entry is stale and would hide a regression
                TEST_ASSERT_FALSE_MESSAGE(same, (std::string("whitelisted but identical (stale entry): ") + n).c_str());
                whitelisted++;
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(same, n.c_str());
        }
        TEST_ASSERT_EQUAL_size_t_MESSAGE(kWhitelist.size() + kWhitelistB1.size() + kWhitelistB3.size() + kWhitelistB3Http10.size() + kWhitelistB6.size() + kWhitelistB4.size() + kWhitelistB7.size(), whitelisted, "every whitelist entry must be a corpus request");
    }
}

// staleness: the generated live copy must still match the source it was cut from. Every extracted segment is
// stored verbatim in web_live::kLiveSrc; it has to occur exactly once in its file of the working tree.
// kLiveSources names those files as literals in this suite, so tools/regression_select.py (which follows the
// string literals of a suite that reads files) runs native_web_diff when one of them changes.
static const char *const kLiveSources[] = {
    "src/web_functions/web_functions.cpp",
    "src/web_functions/web_rm_handlers.cpp",
    "src/url_decode.cpp",
};

static void test_live_copy_is_current(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(10, web_live::kLiveSrcCount);
    for (unsigned long i = 0; i < web_live::kLiveSrcCount; i++)
    {
        const web_live::LiveSrc &l = web_live::kLiveSrc[i];
        bool listed = false;
        for (const char *s : kLiveSources)
            listed = listed || std::string(s) == l.file;
        TEST_ASSERT_TRUE_MESSAGE(listed, (std::string("live source not in kLiveSources: ") + l.file).c_str());
        const std::string file = repo_slurp(l.file);
        const std::string text = l.text;
        const std::string msg = std::string("stale live copy, regenerate with python3 test/test_web_diff/live/extract_live.py: ") + l.what;
        const size_t at = file.find(text);
        TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, msg.c_str());
        TEST_ASSERT_TRUE_MESSAGE(file.find(text, at + 1) == std::string::npos, msg.c_str());
    }
}

// every entry really ran: runs per entry = corpus requests x profiles (printed)
static void test_every_entry_ran(void)
{
    const size_t want = corpusNames().size() * kProfileCount;
    for (size_t k = 0; k < kEntryCount; k++)
    {
        char m[120];
        snprintf(m, sizeof(m), "entry %s: %zu runs (want at least %zu)", kEntries[k].name, gRuns[k], want);
        TEST_MESSAGE(m);
        TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(want, gRuns[k], m);
    }
}

// every whitelist key names a corpus request (runs even while only the frozen entry is wired)
static void test_whitelist_names_corpus_requests(void)
{
    const std::vector<std::string> names = corpusNames();
    const std::set<std::string> have(names.begin(), names.end());
    for (const auto &w : kWhitelist)
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("whitelist names no corpus request: ") + w.first).c_str());
    for (const auto &w : kWhitelistB1)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B1 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(strncmp(w.second.why, "B1:", 3) == 0, w.first.c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelistB3.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelistB6.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelistB3Http10.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
    }
    for (const auto &w : kWhitelistB3Http10)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B3 HTTP/1.0 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0 && kWhitelistB1.count(w.first) == 0 && kWhitelistB3.count(w.first) == 0 && kWhitelistB6.count(w.first) == 0,
                                 (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(strncmp(w.second.why, "B3:", 3) == 0, w.first.c_str());
    }
    for (const auto &w : kWhitelistB4)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B4 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0 && kWhitelistB1.count(w.first) == 0 && kWhitelistB3.count(w.first) == 0 && kWhitelistB3Http10.count(w.first) == 0 && kWhitelistB6.count(w.first) == 0,
                                 (std::string("request in both whitelists: ") + w.first).c_str());
    }
    for (const auto &w : kWhitelistB7)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B7 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0 && kWhitelistB1.count(w.first) == 0 && kWhitelistB3.count(w.first) == 0 && kWhitelistB3Http10.count(w.first) == 0 &&
                                     kWhitelistB6.count(w.first) == 0 && kWhitelistB4.count(w.first) == 0,
                                 (std::string("request in both whitelists: ") + w.first).c_str());
    }
    for (const auto &w : kWhitelistB6)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B6 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0 && kWhitelistB1.count(w.first) == 0 && kWhitelistB3.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(strncmp(w.second, "B6:", 3) == 0, w.first.c_str());
    }
    for (const auto &w : kWhitelistB3)
    {
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("B3 whitelist names no corpus request: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(kWhitelist.count(w.first) == 0, (std::string("request in both whitelists: ") + w.first).c_str());
        TEST_ASSERT_TRUE_MESSAGE(strncmp(w.second, "B3:", 3) == 0, w.first.c_str());
    }
}

// splits a comma list of profile names; every name must be a kProfiles name (a typo would silently mute a profile)
static std::set<std::string> profileSet(const char *list, const std::string &row)
{
    std::set<std::string> out;
    std::string cur;
    for (const char *c = list;; c++)
    {
        if (*c == ',' || *c == 0)
        {
            bool known = false;
            for (size_t p = 0; p < kProfileCount; p++)
                known = known || cur == kProfiles[p].name;
            TEST_ASSERT_TRUE_MESSAGE(known, (row + ": unknown profile '" + cur + "' in the whitelist row").c_str());
            TEST_ASSERT_TRUE_MESSAGE(out.insert(cur).second, (row + ": profile listed twice: " + cur).c_str());
            cur.clear();
            if (*c == 0)
                break;
        }
        else
            cur += *c;
    }
    return out;
}

static std::string joinSet(const std::set<std::string> &s)
{
    std::string o;
    for (const std::string &x : s)
        o += (o.empty() ? "" : ",") + x;
    return o.empty() ? "(none)" : o;
}

// the profiles where the live record differs from the frozen one (kProfiles names); with `liveOut` also the bytes the live
// entry wrote per profile (webRig() holds only the last run)
static std::set<std::string> differingProfiles(const std::string &name, std::map<std::string, std::string> *liveOut)
{
    std::set<std::string> d;
    const std::string bytes = readCorpus(name);
    for (size_t p = 0; p < kProfileCount; p++)
    {
        webRigBegin(bytes, closedVariant(name), kProfiles[p]);
        kEntries[0].run();
        const std::string frozen = webRigRecord();
        webRigBegin(bytes, closedVariant(name), kProfiles[p]);
        kEntries[1].run();
        if (frozen != webRigRecord())
            d.insert(kProfiles[p].name);
        if (liveOut)
            (*liveOut)[kProfiles[p].name] = webRig().out;
    }
    return d;
}

// every whitelist row (B2 and B1): the set of profiles where live differs from frozen is exactly the set the row lists
// (a whitelist row mutes the frozen/live comparison of the request in ALL profiles, so a profile that is not named must be
// byte-identical, and a named one must really differ); for B1 every listed profile answers the status of the row, and
// the Allow line of a 405
static void test_whitelist_rows_differ_exactly_where_listed(void)
{
    TEST_ASSERT_TRUE_MESSAGE(kEntryCount >= 2, "the live entry must stay wired");
    for (const auto &w : kWhitelist)
    {
        const std::set<std::string> want = profileSet(w.second.profiles, w.first);
        const std::set<std::string> got = differingProfiles(w.first, NULL);
        const std::string m = "B2 " + w.first + ": live differs from frozen in [" + joinSet(got) + "], the row lists [" + joinSet(want) + "]";
        TEST_ASSERT_TRUE_MESSAGE(want == got, m.c_str());
    }
    for (const auto &w : kWhitelistB1)
    {
        const std::set<std::string> want = profileSet(w.second.profiles, w.first);
        std::map<std::string, std::string> live;
        const std::set<std::string> got = differingProfiles(w.first, &live);
        const std::string m = "B1 " + w.first + ": live differs from frozen in [" + joinSet(got) + "], the row lists [" + joinSet(want) + "]";
        TEST_ASSERT_TRUE_MESSAGE(want == got, m.c_str());
        char status[40];
        snprintf(status, sizeof(status), "HTTP/1.1 %d ", w.second.status);
        for (const std::string &prof : want)
        {
            const std::string &out = live[prof];
            const std::string am = "B1 " + w.first + " [" + prof + "]: " + webRigEsc(out.substr(0, 300));
            TEST_ASSERT_TRUE_MESSAGE(out.compare(0, strlen(status), status) == 0, am.c_str());
            const size_t head_end = out.find("\r\n\r\n"); // the header block ends at the first empty line (println = CR LF)
            const std::string head = out.substr(0, head_end == std::string::npos ? out.size() : head_end);
            if (w.second.allow[0] != 0)
                TEST_ASSERT_TRUE_MESSAGE(head.find(std::string("\r\n") + w.second.allow + "\r\n") != std::string::npos, am.c_str());
            else
                TEST_ASSERT_TRUE_MESSAGE(head.find("Allow:") == std::string::npos, am.c_str());
        }
    }
}

// B3 and B6 rows: live differs from frozen in every profile and answers 400 with the bad-request JSON body
static void checkRows400(const char *tag, const std::map<std::string, const char *> &rows)
{
    for (const auto &w : rows)
    {
        std::map<std::string, std::string> live;
        const std::set<std::string> got = differingProfiles(w.first, &live);
        std::set<std::string> all;
        for (size_t p = 0; p < kProfileCount; p++)
            all.insert(kProfiles[p].name);
        const std::string m = std::string(tag) + " " + w.first + ": live differs from frozen in [" + joinSet(got) + "], the row lists [" + joinSet(all) + "]";
        TEST_ASSERT_TRUE_MESSAGE(got == all, m.c_str());
        for (const std::string &prof : all)
        {
            const std::string &out = live[prof];
            const std::string am = std::string(tag) + " " + w.first + " [" + prof + "]: " + webRigEsc(out.substr(0, 300));
            TEST_ASSERT_TRUE_MESSAGE(out.compare(0, 12, "HTTP/1.1 400") == 0, am.c_str());
            TEST_ASSERT_TRUE_MESSAGE(out.find("{\"ok\":false,\"err\":\"bad request\"}") != std::string::npos, am.c_str());
        }
    }
}
// B3 HTTP/1.0 rows: live differs from frozen in exactly the listed profiles; in the open profile the live entry answers 200 and
// calls the handler with the clean name / value, in a password profile without a session it answers 401
static void test_b3_http10_rows_keep_working(void)
{
    for (const auto &w : kWhitelistB3Http10)
    {
        const std::set<std::string> want = profileSet(w.second.profiles, w.first);
        std::map<std::string, std::string> live;
        const std::set<std::string> got = differingProfiles(w.first, &live);
        const std::string m = "B3 HTTP/1.0 " + w.first + ": live differs from frozen in [" + joinSet(got) + "], the row lists [" + joinSet(want) + "]";
        TEST_ASSERT_TRUE_MESSAGE(want == got, m.c_str());
        for (const std::string &prof : want)
        {
            const std::string am = "B3 HTTP/1.0 " + w.first + " [" + prof + "]: " + webRigEsc(live[prof].substr(0, 300));
            if (prof == "open")
            {
                TEST_ASSERT_TRUE_MESSAGE(live[prof].compare(0, 12, "HTTP/1.1 200") == 0, am.c_str());
                webRigBegin(readCorpus(w.first), closedVariant(w.first), kProfiles[0]);
                kEntries[1].run();
                TEST_ASSERT_EQUAL_STRING_MESSAGE(w.second.trace, webRig().trace.at(w.second.at).c_str(), am.c_str());
            }
            else
                TEST_ASSERT_TRUE_MESSAGE(live[prof].compare(0, 12, "HTTP/1.1 401") == 0, am.c_str());
        }
    }
}
// B4 rows: live differs from frozen in exactly the listed profiles and answers 422 with the empty-value JSON, no setter call
static void checkRows422(const char *tag, const std::map<std::string, WhiteB4> &rows)
{
    for (const auto &w : rows)
    {
        const std::set<std::string> want = profileSet(w.second.profiles, w.first);
        std::map<std::string, std::string> live;
        const std::set<std::string> got = differingProfiles(w.first, &live);
        const std::string m = std::string(tag) + " " + w.first + ": live differs from frozen in [" + joinSet(got) + "], the row lists [" + joinSet(want) + "]";
        TEST_ASSERT_TRUE_MESSAGE(want == got, m.c_str());
        for (const std::string &prof : want)
        {
            const std::string am = std::string(tag) + " " + w.first + " [" + prof + "]: " + webRigEsc(live[prof].substr(0, 300));
            TEST_ASSERT_TRUE_MESSAGE(live[prof].compare(0, 12, "HTTP/1.1 422") == 0, am.c_str());
            TEST_ASSERT_TRUE_MESSAGE(live[prof].find(std::string("{\"returncode\":1,\"") + w.second.name + "\":\"\"}") != std::string::npos, am.c_str());
        }
        for (size_t p = 0; p < kProfileCount; p++)
        {
            if (want.count(kProfiles[p].name) == 0)
                continue;
            webRigBegin(readCorpus(w.first), closedVariant(w.first), kProfiles[p]);
            kEntries[1].run();
            for (const std::string &t : webRig().trace)
                TEST_ASSERT_TRUE_MESSAGE(t.find("webSetup_setParam") == std::string::npos, (std::string(tag) + " " + w.first + ": setter called").c_str());
        }
    }
}
static void test_b4_rows_answer_422_and_set_nothing(void) { checkRows422("B4", kWhitelistB4); }
static void test_b7_rows_answer_422_and_set_nothing(void) { checkRows422("B7", kWhitelistB7); }
static void test_b3_rows_answer_400_in_every_profile(void) { checkRows400("B3", kWhitelistB3); }
static void test_b6_rows_answer_400_in_every_profile(void) { checkRows400("B6", kWhitelistB6); }

// B6 past the window: the parser judges only the 1023-byte window, the accumulator every finished head line. A bad
// Content-Length after the window is a 400 too (web_request.h D-4: B6 acts on the accumulator's judgement); the frozen
// reference routes both requests with the lenient value. The corpus has no head this long, so the rows are built here.
static void test_b6_content_length_past_the_window(void)
{
    const WebProfile &open = kProfiles[0];
    const std::string pad = std::string("X-Pad-A: ") + std::string(500, 'a') + "\r\nX-Pad-B: " + std::string(500, 'b') + "\r\n";
    const std::string reqs[2] = {
        "POST /rmsend HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n" + pad + "Content-Length: 12abc\r\n\r\nhello world!",
        "POST /rmsend HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\nContent-Length: 5\r\n" + pad + "Content-Length: 900\r\n\r\nhello",
    };
    for (const std::string &req : reqs)
    {
        TEST_ASSERT_TRUE(req.find("\r\n\r\n") > 1023); // the second Content-Length line lies past the window
        webRigBegin(req, false, open);
        kEntries[0].run();
        {
            std::string tr;
            for (const auto &t : webRig().trace)
                tr += t + " | ";
            const std::string fm = "frozen: " + webRigEsc(webRig().out.substr(0, 120)) + " trace: " + tr;
            TEST_ASSERT_TRUE_MESSAGE(tr.find("sub_rm_send") != std::string::npos, fm.c_str()); // routed with the lenient value
        }
        webRigBegin(req, false, open);
        kEntries[1].run();
        const std::string out = webRig().out;
        std::string ltr;
        for (const auto &t : webRig().trace)
            ltr += t + " | ";
        const std::string lm = "live: " + webRigEsc(out.substr(0, 120)) + " trace: " + ltr;
        TEST_ASSERT_TRUE_MESSAGE(out.compare(0, 13, "HTTP/1.1 400 ") == 0, lm.c_str());
        TEST_ASSERT_TRUE(out.find("{\"ok\":false,\"err\":\"bad request\"}") != std::string::npos);
        TEST_ASSERT_TRUE(webRig().trace.empty() || webRig().trace.at(0).find("content_length=") == std::string::npos);
    }
}

// decision (c) of web_route.h: the dispatcher wipes the request head (web_header) and the request line (wg_path) on EVERY
// exit after the head is complete (guard 403, AU-busy 503, asset, login, logout, dispatch). The rig String keeps the bytes of
// a shortened or destroyed String in its buffer like a real one (stubs/Arduino.h), so after every corpus request under every
// profile the live web_header buffer must hold nothing but the wipe byte, and no String may die holding the request line.
static void test_live_leaves_no_request_bytes(void)
{
    const std::vector<std::string> names = corpusNames();
    int bad = 0;
    size_t armed = 0;
    char first[400] = "";
    for (const std::string &n : names)
    {
        const std::string bytes = readCorpus(n);
        for (size_t p = 0; p < kProfileCount; p++)
        {
            webRigBegin(bytes, closedVariant(n), kProfiles[p]);
            web_live::web_header = String(); // an empty buffer: a leftover of an earlier request is not blamed on this one
            armed += webRigProbeArm(webRigRequestPath(webRig().in)) ? 1 : 0;
            kEntries[1].run();
            webRigProbeDisarm();
            const std::string img = web_live::web_header.bufferImage();
            const size_t at = img.find_first_not_of(std::string("x\0", 2));
            const bool headLeak = at != std::string::npos;
            const bool lineLeak = webRigProbeLeaks() != 0;
            if ((headLeak || lineLeak) && bad++ == 0)
            {
                std::string m = n + " [" + kProfiles[p].name + "]:";
                if (headLeak)
                    m += " web_header buffer holds request bytes '" + webRigEsc(img.substr(at, 40)) + "';";
                if (lineLeak)
                    m += " a String died holding the request line (wg_path);";
                snprintf(first, sizeof(first), "%s", m.c_str());
            }
        }
    }
    char head[500];
    snprintf(head, sizeof(head), "%d of %zu runs leave request bytes behind; first: %s", bad, names.size() * kProfileCount, first);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, head);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(1000, armed, "the request-line probe must have been armed for most runs");
}

// the rig must not be vacuous: the corpus reaches many handlers and statuses (minimum counts)
static void test_corpus_reaches_the_handlers(void)
{
    const std::vector<std::string> names = corpusNames();
    std::set<std::string> handlers, statuses;
    int clampedHead = 0, bodyReads = 0, shortReads = 0, deadline = 0;
    for (const std::string &n : names)
    {
        const std::string bytes = readCorpus(n);
        for (size_t p = 0; p < kProfileCount; p++)
        {
            webRigBegin(bytes, closedVariant(n), kProfiles[p]);
            kEntries[0].run();
            const WebRigState &r = webRig();
            for (const std::string &t : r.trace)
                handlers.insert(t.substr(0, t.find_first_of(" =")));
            if (r.out.compare(0, 9, "HTTP/1.1 ") == 0)
                statuses.insert(r.out.substr(9, 3));
            if (r.stopCalls == 0)
                deadline++;
            for (const std::string &t : r.trace)
            {
                bodyReads += t.find("content_length=") != std::string::npos ? 1 : 0;
                shortReads += (t.find("err=short") != std::string::npos || t.find("rc=-3") != std::string::npos) ? 1 : 0;
            }
            if (r.out.find("header line too large") != std::string::npos)
                clampedHead++;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(30, (int)handlers.size(), "distinct handler ids reached");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(8, (int)statuses.size(), "distinct HTTP statuses");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(100, bodyReads, "body reader runs");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(10, shortReads, "short bodies");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(4, clampedHead, "431 answers");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(20, deadline, "runs ended by the 2 s head deadline (no close)");
    for (const char *h : {"sub_page_info", "sub_page_setup", "webSetup_setParam", "webSetup_getParam", "webFunctionCall",
                          "sendMessage", "sub_rm_send", "sub_config_upload", "sub_pin_status", "deliver_scaffold",
                          "sub_page_login", "sub_page_unknown", "sub_au_status", "msgstorePurge"})
        TEST_ASSERT_TRUE_MESSAGE(handlers.count(h) == 1, h);
    TEST_ASSERT_TRUE(statuses.count("200") && statuses.count("401") && statuses.count("403") && statuses.count("422") &&
                     statuses.count("431") && statuses.count("503") && statuses.count("404") && statuses.count("405"));
}

// pins that make a broken rig loud: a few requests whose full record is known
static void test_known_requests(void)
{
    const WebProfile &open = kProfiles[0];
    const char *head = "Host: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
    webRigBegin(std::string("GET /setparam/?atxt=a%20b%22c HTTP/1.1\r\n") + head, false, open);
    kEntries[0].run();
    const WebRigState &r = webRig();
    TEST_ASSERT_EQUAL_STRING("webSetup_setParam name='atxt' value='a bc'", r.trace.at(0).c_str());
    TEST_ASSERT_TRUE(r.out.find("HTTP/1.1 200 OK \n") == 0);
    TEST_ASSERT_TRUE(r.out.find("{\"returncode\":0,\"atxt\":\"a bc\"}") != std::string::npos);
    TEST_ASSERT_EQUAL_INT(1, r.stopCalls);

    // HTTP/1.0 request line: the version cut looks for " HTTP/1.1" and finds none (D3), the value keeps the version text
    webRigBegin(std::string("GET /setparam/?atxt=hi HTTP/1.0\r\n") + head, false, open);
    kEntries[0].run();
    TEST_ASSERT_TRUE(webRig().trace.at(0).find("value='hi HTTP/1.0") != std::string::npos);

    // live: the same request, the version text stays out of the value (B3, HTTP/1.0 keeps working)
    webRigBegin(std::string("GET /setparam/?atxt=hi HTTP/1.0\r\n") + head, false, open);
    kEntries[1].run();
    TEST_ASSERT_EQUAL_STRING("webSetup_setParam name='atxt' value='hi'", webRig().trace.at(0).c_str());

    // no '=': the whole name is also the value (D2)
    webRigBegin(std::string("GET /setparam/?webserver HTTP/1.1\r\n") + head, false, open);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_STRING("webSetup_setParam name='webserver' value='webserver'", webRig().trace.at(0).c_str());
    // live: refused with 422, no setter (B4)
    webRigBegin(std::string("GET /setparam/?webserver HTTP/1.1\r\n") + head, false, open);
    kEntries[1].run();
    TEST_ASSERT_TRUE(webRig().out.find("HTTP/1.1 422") == 0);
    TEST_ASSERT_TRUE(webRig().out.find("{\"returncode\":1,\"webserver\":\"\"}") != std::string::npos);

    // route token in the Referer reaches a handler (D4)
    webRigBegin(std::string("GET /nothing HTTP/1.1\r\nHost: 192.0.2.10\r\nReferer: http://192.0.2.10/?page=info\r\n\r\n"), false, open);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_STRING("sub_page_info", webRig().trace.at(1).c_str()); // [0] is send_http_header's webTxBeginChunked

    // Content-Length is read with atol and the body is handed to the body reader
    webRigBegin("POST /rmsend HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\nContent-Length: 12abc\r\n\r\nhello world!", false, open);
    kEntries[0].run();
    TEST_ASSERT_TRUE(webRig().trace.at(0).find("content_length=12 err=(none) body='hello world!'") != std::string::npos);

    // a head that never ends: no answer, the loop leaves at the 2 s deadline
    webRigBegin("GET /?page=info HTTP/1.1\r\nHost: 192.0.2.10\r\n", false, open);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_INT(0, webRig().stopCalls);
    TEST_ASSERT_TRUE(webRig().out.empty());
    TEST_ASSERT_EQUAL_UINT(2002, webRig().now - WEB_RIG_T0); // 2000 ms deadline + the polls of the last two loop checks

    // wrong Host: 403 before any routing
    webRigBegin("GET /?page=info HTTP/1.1\r\nHost: evil.com\r\n\r\n", false, open);
    kEntries[0].run();
    TEST_ASSERT_TRUE(webRig().out.find("HTTP/1.1 403 Forbidden \n") == 0);
    TEST_ASSERT_TRUE(webRig().trace.size() == 1 && webRig().trace[0] == "webTxBeginChunked");

    // password set, no session: the login page; with the session of the rig: the page
    webRigBegin("GET /?page=info HTTP/1.1\r\nHost: 192.0.2.10\r\nCookie: mcs=aaaaaaaabbbbbbbbccccccccdddddddd\r\n\r\n", false, kProfiles[1]);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_STRING("sub_page_login", webRig().trace.at(1).c_str());
    webRigBegin("GET /?page=info HTTP/1.1\r\nHost: 192.0.2.10\r\nCookie: mcs=aaaaaaaabbbbbbbbccccccccdddddddd\r\n\r\n", false, kProfiles[3]);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_STRING("sub_page_info", webRig().trace.at(1).c_str());
}

// step 5a: setparam / getparam read name and value from the parsed query. For every request below the live entry hands the
// handler exactly what the frozen copy does (frozen == live), and the text is pinned: the value is everything after the
// first '=' (a further '&' stays), no '=' makes name and value the whole query (D2), a getparam name stops at the first
// '=' unless that is its first byte, a request without '?' keeps the legacy "ram/" window (getparam; setparam refuses it, B4), the name is
// lower-cased and both are percent-decoded (LEGACY).
static void test_param_extraction_live_equals_frozen(void)
{
    struct Case
    {
        const char *line;
        const char *trace;       // the frozen entry (and the live one unless `refused` is set)
        const char *refused = nullptr; // B4: the live entry answers 422 with this body and calls no setter
    };
    static const Case cases[] = {
        {"GET /setparam/?atxt=a&b=c HTTP/1.1", "webSetup_setParam name='atxt' value='a&b=c'"},
        {"GET /setparam/?atxt=a%20b%22c HTTP/1.1", "webSetup_setParam name='atxt' value='a bc'"},
        {"GET /setparam/?ATxt=x=y HTTP/1.1", "webSetup_setParam name='atxt' value='x=y'"},
        {"GET /setparam/?atxt= HTTP/1.1", "webSetup_setParam name='atxt' value=''"},
        {"GET /setparam/?=x HTTP/1.1", "webSetup_setParam name='' value='x'"},
        {"GET /setparam/?webserver HTTP/1.1", "webSetup_setParam name='webserver' value='webserver'", "{\"returncode\":1,\"webserver\":\"\"}"},
        {"GET /setparam/?WebServer%20x HTTP/1.1", "webSetup_setParam name='webserver x' value='WebServer x'", "{\"returncode\":1,\"webserver x\":\"\"}"},
        {"GET /setparam/? HTTP/1.1", "webSetup_setParam name='' value=''", "{\"returncode\":1,\"\":\"\"}"},
        {"GET /setparam/ HTTP/1.1", "webSetup_setParam name='ram/' value='ram/'", "{\"returncode\":1,\"\":\"\"}"},
        {"GET /getparam/?gateway HTTP/1.1", "webSetup_getParam name='gateway'"},
        {"GET /getparam/?Gateway=1&x=2 HTTP/1.1", "webSetup_getParam name='gateway'"},
        {"GET /getparam/?=x HTTP/1.1", "webSetup_getParam name='=x'"},
        {"GET /getparam/?a%20b HTTP/1.1", "webSetup_getParam name='a b'"},
        {"GET /getparam/ HTTP/1.1", "webSetup_getParam name='ram/'"},
    };
    for (const Case &c : cases)
    {
        const std::string req = std::string(c.line) + "\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
        for (size_t e = 0; e < kEntryCount; e++)
        {
            webRigBegin(req, false, kProfiles[0]);
            kEntries[e].run();
            const std::string m = std::string(kEntries[e].name) + ": " + c.line;
            if (c.refused != nullptr && e == 1)
            {
                for (const std::string &t : webRig().trace)
                    TEST_ASSERT_TRUE_MESSAGE(t.find("webSetup_setParam") == std::string::npos, m.c_str());
                TEST_ASSERT_TRUE_MESSAGE(webRig().out.compare(0, 12, "HTTP/1.1 422") == 0, (m + ": " + webRigEsc(webRig().out.substr(0, 200))).c_str());
                TEST_ASSERT_TRUE_MESSAGE(webRig().out.find(c.refused) != std::string::npos, (m + ": " + webRigEsc(webRig().out.substr(0, 200))).c_str());
            }
            else
                TEST_ASSERT_EQUAL_STRING_MESSAGE(c.trace, webRig().trace.at(0).c_str(), m.c_str());
        }
    }
    // a raw NUL in the query before the first '=': today's String searches (strstr) stop at the NUL, so the text ends
    // there; the span must end there too (advisor 5a)
    const std::string nul(1, '\0');
    {
        const std::string req = "GET /setparam/?atxt" + nul + "=x HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
        webRigBegin(req, false, kProfiles[0]);
        kEntries[0].run();
        TEST_ASSERT_EQUAL_STRING("webSetup_setParam name='atxt' value='atxt'", webRig().trace.at(0).c_str());
        webRigBegin(req, false, kProfiles[0]); // live: the text ends at the NUL, no '=' left (B4)
        kEntries[1].run();
        TEST_ASSERT_TRUE(webRig().out.compare(0, 12, "HTTP/1.1 422") == 0);
        TEST_ASSERT_TRUE(webRig().out.find("{\"returncode\":1,\"atxt\":\"\"}") != std::string::npos);
    }
    {
        const std::string req = "GET /getparam/?gate" + nul + "way=1 HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
        for (size_t e = 0; e < kEntryCount; e++)
        {
            webRigBegin(req, false, kProfiles[0]);
            kEntries[e].run();
            TEST_ASSERT_EQUAL_STRING_MESSAGE("webSetup_getParam name='gate'", webRig().trace.at(0).c_str(), (std::string(kEntries[e].name) + ": NUL case").c_str());
        }
    }
}

// step 5c: call_function and send_message read name / parameter / tocall / message from the parsed query. For every request
// below the live entry hands webFunctionCall / sendMessage exactly what the frozen copy does (frozen == live, whole answer
// and trace), and the text is pinned: the reversed order "&message=..&tocall=.." keeps today's swapped substring() bounds
// (B11 will change it), "&message" without '=' stops tocall, '&' stays in the message, the message is cut to 150 bytes on
// a UTF-8 boundary, a raw NUL ends the text (strstr).
static std::string traceWithPrefix(const char *prefix)
{
    for (const std::string &t : webRig().trace)
        if (t.compare(0, strlen(prefix), prefix) == 0)
            return t;
    return "(none)";
}
static void checkSameAsFrozen(const std::string &req, const char *prefix, const char *want, const char *why)
{
    std::string out[2];
    for (size_t e = 0; e < kEntryCount; e++)
    {
        webRigBegin(req, false, kProfiles[0]);
        kEntries[e].run();
        out[e] = webRig().out;
        const std::string m = std::string(kEntries[e].name) + ": " + why + ": " + webRigEsc(out[e].substr(0, 200));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, traceWithPrefix(prefix).c_str(), m.c_str());
    }
    TEST_ASSERT_TRUE_MESSAGE(out[0] == out[1], (std::string("frozen and live answer differ: ") + why).c_str());
}
// step 5c, the three requests where frozen and live differ on purpose (advisor 5c; none is in the corpus):
//   B2: send_message reads &tocall= / &message= from the query only, never from a header line (a Referer); today a
//       header-line token made it send text cut from the request line ("ndmessage&tocall=X"), now "sendmessage failed";
//   B3: "HTTP/1.1" as text inside the query no longer cuts the callfunction name (today: the first "HTTP/1.1" anywhere);
//   NUL: blanks before a raw NUL in a callfunction value are trimmed (today the trim saw the end of the head instead).
static void checkDiffersFromFrozen(const std::string &req, const char *prefix, const char *want_frozen, const char *want_live,
                                   const char *why)
{
    const char *want[2] = {want_frozen, want_live};
    for (size_t e = 0; e < 2; e++)
    {
        webRigBegin(req, false, kProfiles[0]);
        kEntries[e].run();
        const std::string m = std::string(kEntries[e].name) + ": " + why + ": " + webRigEsc(webRig().out.substr(0, 200));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want[e], traceWithPrefix(prefix).c_str(), m.c_str());
    }
}
static void test_call_and_send_deliberate_deltas(void)
{
    const std::string nul(1, '\0');
    const std::string hdr = "\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n";
    checkDiffersFromFrozen("GET /?sendmessage&tocall=X HTTP/1.1" + hdr + "Referer: http://192.0.2.10/?&message=hi\r\n\r\n",
                           "sendMessage", "sendMessage len=22 text=':{X}ndmessage&tocall=X'", "(none)", "B2 &message= in a Referer");
    checkDiffersFromFrozen("GET /callfunction/?aHTTP/1.1=b HTTP/1.1" + hdr + "\r\n", "webFunctionCall",
                           "webFunctionCall name='a' param=''", "webFunctionCall name='aHTTP/1.1' param='b'", "B3 HTTP/1.1 in the query");
    checkDiffersFromFrozen("GET /callfunction/?a=b\t" + nul + "x HTTP/1.1" + hdr + "\r\n", "webFunctionCall",
                           "webFunctionCall name='a' param='b\\t'", "webFunctionCall name='a' param='b'", "NUL after a tab");
}

static void test_call_and_send_extraction_live_equals_frozen(void)
{
    const std::string nul(1, '\0');
    const char *const tail = " HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
    struct Case
    {
        std::string line;
        std::string want;
    };
    const std::vector<Case> calls = {
        {"/callfunction/?sendpos", "webFunctionCall name='sendpos' param=''"},
        {"/callfunction/?a=b=c", "webFunctionCall name='a' param='b=c'"},
        {"/callfunction/?a=%20x%20", "webFunctionCall name='a' param=' x '"},
        {"/callfunction/?=x", "webFunctionCall name='=x' param=''"},
        {"/callfunction/?a" + nul + "=b", "webFunctionCall name='a' param=''"},
        {"/callfunction/?a=b&c=d", "webFunctionCall name='a' param='b&c=d'"},
        {"/callfunction/", "webFunctionCall name='ion/' param=''"},
    };
    for (const Case &c : calls)
        checkSameAsFrozen("GET " + c.line + tail, "webFunctionCall", c.want.c_str(), c.line.c_str());
    std::string long_msg = std::string(142, 'a') + "%C3%A4bb"; // the 2-byte char starts at byte 149 of ":{9999}" + text: cut before it
    const std::vector<Case> sends = {
        {"/?sendmessage&tocall=9999&message=hi%20there", "sendMessage len=15 text=':{9999}hi there'"},
        {"/?sendmessage&message=m&tocall=9999", "sendMessage len=34 text=':{&MESSAGE=M&TOCALL=}m&tocall=9999'"},
        {"/?sendmessage&tocall=ab&messagex=1&message=hi", "sendMessage len=7 text=':{AB}hi'"},
        {"/?sendmessage&tocall=9999&message=", "(none)"},
        {"/?sendmessage&tocall=9999&message=%20%20", "(none)"},
        {"/?sendmessage&tocall=9999&message=a&b=c", "sendMessage len=12 text=':{9999}a&b=c'"},
        {"/?sendmessage&tocall=9999&message=ab" + nul + "cd", "sendMessage len=9 text=':{9999}ab'"},
        {"/?sendmessage&tocall=&message=hi", "sendMessage len=3 text=':hi'"},
        {"/?sendmessage&message=x", "(none)"},
        {"/?sendmessage&tocall=9999&message=" + long_msg, "sendMessage len=149 text=':{9999}" + std::string(142, 'a') + "'"},
    };
    for (const Case &c : sends)
        checkSameAsFrozen("GET " + c.line + tail, "sendMessage", c.want.c_str(), c.line.substr(0, 80).c_str());
    // "sendmessage failed": the answer, same in both entries
    webRigBegin("GET /?sendmessage&message=x" + std::string(tail), false, kProfiles[0]);
    kEntries[1].run();
    TEST_ASSERT_TRUE(webRig().out.find("sendmessage failed") != std::string::npos);
}

// B7 (step 5b): setparam refuses a decoded value that contains "--" (also as the splitter sees it after the look-alike dash
// fold) or starts with "&&" (every name except manualcommand), and any decoded value over 200 bytes (manualcommand too);
// 422 {"returncode":1,"<name>":""}, no setter call. "Starts with" is judged on the decoded value as is (no trim); "&&" in
// the middle is allowed (commandAction splits on "--" only). The frozen entry accepts every one of these.
static void test_b7_value_verdicts(void)
{
    struct Case
    {
        std::string line; // request target after /setparam/?
        const char *name;
        bool refused;
    };
    const std::string v200(200, 'x'), v201(201, 'x');
    const std::vector<Case> cases = {
        {"atxt=a--b", "atxt", true},
        {"atxt=a%20--%20b", "atxt", true},
        {"atxt=--", "atxt", true},
        {"atxt=a--", "atxt", true},
        {"atxt=%26%26x", "atxt", true},
        {"atxt=&&x", "atxt", true},
        {"atxt=a&&b", "atxt", false},
        {"atxt=a%20%26%26%20b", "atxt", false},
        {"atxt=%20&&x", "atxt", false},
        {"atxt=a-b", "atxt", false},
        {"atxt=a%20%E2%80%94x", "atxt", true}, // em dash after a blank, lower-case letter: commandAction folds it to "--"
        {"atxt=a%20%E2%80%94%20b", "atxt", false}, // not followed by a letter: stays a dash
        // the setters trim (tz) and lower-case (webhost) before they build the line: the check sees the value after both
        // (advisor B7: without that, "--webhost \u2014update install" folded to a second command)
        {"webhost=%E2%80%94UPDATE%20install", "webhost", true},
        {"tz=%0D%E2%80%94reboot", "tz", true},
        {"atxt=a%20%E2%80%94X", "atxt", true},
        {"setcall=--setcall%20x", "setcall", true},
        {"manualcommand=--info", "manualcommand", false},
        {"manualcommand=&&info", "manualcommand", false},
        {"manualcommand=--info%20--save", "manualcommand", false},
        {"atxt=" + v200, "atxt", false},
        {"atxt=" + v201, "atxt", true},
        {"manualcommand=" + v200, "manualcommand", false},
        {"manualcommand=" + v201, "manualcommand", true},
        {"atxt=" + std::string(67, 'a') + "%20%20%20" + std::string(130, 'b'), "atxt", false}, // 200 decoded bytes from a longer encoded text
    };
    for (const Case &c : cases)
    {
        const std::string req = "GET /setparam/?" + c.line + " HTTP/1.1\r\nHost: 192.0.2.10\r\nX-MC: 1\r\n\r\n";
        const std::string m = std::string(c.refused ? "refused: " : "accepted: ") + c.line.substr(0, 60);
        webRigBegin(req, false, kProfiles[0]);
        kEntries[0].run();
        TEST_ASSERT_TRUE_MESSAGE(webRig().trace.at(0).find("webSetup_setParam") == 0, ("frozen: " + m).c_str());
        webRigBegin(req, false, kProfiles[0]);
        kEntries[1].run();
        const WebRigState &r = webRig();
        const std::string why = m + ": " + webRigEsc(r.out.substr(0, 160));
        if (c.refused)
        {
            for (const std::string &t : r.trace)
                TEST_ASSERT_TRUE_MESSAGE(t.find("webSetup_setParam") == std::string::npos, why.c_str());
            TEST_ASSERT_TRUE_MESSAGE(r.out.compare(0, 12, "HTTP/1.1 422") == 0, why.c_str());
            TEST_ASSERT_TRUE_MESSAGE(r.out.find(std::string("{\"returncode\":1,\"") + c.name + "\":\"\"}") != std::string::npos, why.c_str());
        }
        else
            TEST_ASSERT_TRUE_MESSAGE(r.trace.at(0).find("webSetup_setParam name='") == 0, why.c_str());
    }
}

// every route of the table is built under the macros of this build (ESP32, ENABLE_MSGSTORE, OLED_SHOT_WEB), except the
// T-Deck bench screenshot (INSTRUMENT_ENABLED and BOARD_T_DECK, which the rig does not model): the router the rig
// runs can reach every other handler, so a build flag lost from the env cannot hide a route
static void test_every_route_is_built_in_the_rig_build(void)
{
    for (uint8_t r = 0; r < WR_COUNT; r++)
    {
        TEST_ASSERT_TRUE_MESSAGE(webRouteBuiltFor(r, true, true, true, true), "all features on: every route is built");
    }
}

// the config buffer cap used by the stubs is the one of the source
static void test_config_json_max_is_the_source_value(void)
{
    const std::string h = repo_slurp("src/config_json.h");
    TEST_ASSERT_TRUE_MESSAGE(h.find("CONFIG_JSON_MAX 6144") != std::string::npos, "CONFIG_JSON_MAX changed: update -D CONFIG_JSON_MAX in the env");
}

// WEB_DIFF_DUMP_DIFFS (maintenance dump) skips the asserts of test_entries_agree_with_the_first: a run with it set must
// never end green, so a stray variable in a shell cannot pass the gate
static void test_dump_mode_is_never_a_verdict(void)
{
    TEST_ASSERT_NULL_MESSAGE(getenv("WEB_DIFF_DUMP_DIFFS"), "WEB_DIFF_DUMP_DIFFS is set: a dump run, not a verdict");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_corpus_has_minimum_counts);
    RUN_TEST(test_config_json_max_is_the_source_value);
    RUN_TEST(test_known_requests);
    RUN_TEST(test_param_extraction_live_equals_frozen);
    RUN_TEST(test_call_and_send_extraction_live_equals_frozen);
    RUN_TEST(test_call_and_send_deliberate_deltas);
    RUN_TEST(test_b7_value_verdicts);
    RUN_TEST(test_entries_match_golden);
    RUN_TEST(test_golden_set_is_complete);
    RUN_TEST(test_entries_agree_with_the_first);
    RUN_TEST(test_whitelist_names_corpus_requests);
    RUN_TEST(test_whitelist_rows_differ_exactly_where_listed);
    RUN_TEST(test_b3_rows_answer_400_in_every_profile);
    RUN_TEST(test_b3_http10_rows_keep_working);
    RUN_TEST(test_b6_rows_answer_400_in_every_profile);
    RUN_TEST(test_b4_rows_answer_422_and_set_nothing);
    RUN_TEST(test_b7_rows_answer_422_and_set_nothing);
    RUN_TEST(test_b6_content_length_past_the_window);
    RUN_TEST(test_live_leaves_no_request_bytes);
    RUN_TEST(test_live_copy_is_current);
    RUN_TEST(test_corpus_reaches_the_handlers);
    RUN_TEST(test_every_entry_ran);
    RUN_TEST(test_every_route_is_built_in_the_rig_build);
    RUN_TEST(test_dump_mode_is_never_a_verdict);
    return UNITY_END();
}
