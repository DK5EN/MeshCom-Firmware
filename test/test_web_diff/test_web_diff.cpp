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
// (B1-B10 of docs/web-server-seams-20261127.md 7.2) go into kWhitelist, keyed by corpus name (B2 in kWhitelist, B1 in kWhitelistB1), each row naming the profiles where the live record differs.
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
    {"mut-020-setparam-onewire-on-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-029-getparam-gateway-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-030-getparam-gateway-http-1-0-nohdr", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-039-callfunction-sendpos-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-048-sendmessage-tocall-9999-message-hi-20the", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-058-pinstatus-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-059-pinstatus-http-1-0-nohdr", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-068-rmstatus-http-1-0", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-069-rmstatus-http-1-0-nohdr", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"mut-418-nul-in-reqline", {kWhyB1NoSession, "password,password+session", 401, ""}},
    {"cap-033-config", {"B1: GET on a POST-only route /config (the Referer page token no longer routes): 405 + Allow", "open,password,password+session", 405, "Allow: POST"}},
    {"js-059-get-screen-rgb565", {"B1: route not built in this board (T-Deck INSTRUMENT only): 404 instead of the scaffold", "open,password,password+session", 404, ""}},
    {"mut-185-route-rmsendx", {"B1: unknown path /rmsendx (no exact row): 404, no longer the GET /rmsend 405 branch", "open,password,password+session", 404, ""}},
};

static bool isWhitelisted(const std::string &n) { return kWhitelist.count(n) != 0 || kWhitelistB1.count(n) != 0; }

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
        TEST_ASSERT_EQUAL_size_t_MESSAGE(kWhitelist.size() + kWhitelistB1.size(), whitelisted, "every whitelist entry must be a corpus request");
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

    // no '=': the whole name is also the value (D2)
    webRigBegin(std::string("GET /setparam/?webserver HTTP/1.1\r\n") + head, false, open);
    kEntries[0].run();
    TEST_ASSERT_EQUAL_STRING("webSetup_setParam name='webserver' value='webserver'", webRig().trace.at(0).c_str());

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
    RUN_TEST(test_entries_match_golden);
    RUN_TEST(test_golden_set_is_complete);
    RUN_TEST(test_entries_agree_with_the_first);
    RUN_TEST(test_whitelist_names_corpus_requests);
    RUN_TEST(test_whitelist_rows_differ_exactly_where_listed);
    RUN_TEST(test_live_leaves_no_request_bytes);
    RUN_TEST(test_live_copy_is_current);
    RUN_TEST(test_corpus_reaches_the_handlers);
    RUN_TEST(test_every_entry_ran);
    RUN_TEST(test_every_route_is_built_in_the_rig_build);
    RUN_TEST(test_dump_mode_is_never_a_verdict);
    return UNITY_END();
}
