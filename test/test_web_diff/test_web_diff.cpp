// Web server request-path differential (FORMAL-02, web server wave A step 0; docs/web-server-seams-20261127.md 7.1).
//
//   pio test -e native_web_diff            compare every entry point against test/test_web_diff/golden
//   WEB_DIFF_UPDATE=1 pio test -e native_web_diff   rewrite the golden from the frozen path (review the diff!)
//
// Today this is a baseline snapshot, not yet a differential: the new decoders are proven by test_url_decode,
// test_rm_web_parse and test_hex_digit against frozen copies. The rig compares old against new once the live
// entry below is wired (web wave A step 3).
// The unit under test is a function pointer table. Today it holds the frozen copy of the request path
// (frozen/web_request_frozen.cpp: work_webpage_frozen, entry of the head loop, the guard, the router, the
// body readers and the setparam/getparam/callfunction/sendmessage handlers, handlers stubbed). Every corpus
// request (corpus/*.req, built by tools/web_corpus.py) runs under every profile; the record of the run
// (handler trace, every byte written, the close, the fake clock) is compared with golden/<name>.txt.
//
// TODO(web-diff-live): wire the live request path as a second entry of kEntries ("live"):
//   1. the refactored code must be natively compilable: a TU with `void work_webpage(void)` (or its
//      replacement) that reads the global `web_client` (rig/web_rig.h) and calls its collaborators by the
//      names in stubs/web_handler_stubs.h;
//   2. that TU #includes stubs/web_handler_stubs.h inside its own namespace with the WEB_RIG_* macros
//      bound to its body readers (see the MID block of frozen/extract.sh), so the traces are identical;
//   3. add its sources to build_src_filter / the suite directory and list it here. The golden check and the
//      frozen-vs-live check below then run for it with no other change. Intended deltas (B1-B10 of
//      docs/web-server-seams-20261127.md 7.2) go into kWhitelist, keyed by corpus name.
#include <unity.h>

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

void setUp(void) {}
void tearDown(void) {}

struct WebEntry
{
    const char *name;
    void (*run)(void);
};

static const WebEntry kEntries[] = {
    {"frozen", web_frozen::work_webpage_frozen},
    // TODO(web-diff-live): {"live", <live request entry>},
};
static const size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// Expected deltas between the frozen reference and a later entry: corpus name -> reason. Empty today.
static const std::map<std::string, const char *> kWhitelist = {};

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
static std::string recordAll(const WebEntry &e, const std::string &name, const std::string &bytes)
{
    std::string all;
    for (size_t p = 0; p < kProfileCount; p++)
    {
        webRigBegin(bytes, closedVariant(name), kProfiles[p]);
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
            if (want != got && (k == 0 || kWhitelist.find(n) == kWhitelist.end()))
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
    if (kEntryCount < 2)
        TEST_IGNORE_MESSAGE("only the frozen entry is wired (TODO web-diff-live)");
    const std::vector<std::string> names = corpusNames();
    for (size_t k = 1; k < kEntryCount; k++)
    {
        size_t whitelisted = 0;
        for (const std::string &n : names)
        {
            const std::string bytes = readCorpus(n);
            const bool same = recordAll(kEntries[0], n, bytes) == recordAll(kEntries[k], n, bytes);
            if (kWhitelist.find(n) != kWhitelist.end())
            {
                // a whitelisted delta must really differ, else the entry is stale and would hide a regression
                TEST_ASSERT_FALSE_MESSAGE(same, (std::string("whitelisted but identical (stale entry): ") + n).c_str());
                whitelisted++;
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(same, n.c_str());
        }
        TEST_ASSERT_EQUAL_size_t_MESSAGE(kWhitelist.size(), whitelisted, "every whitelist entry must be a corpus request");
    }
}

// every whitelist key names a corpus request (runs even while only the frozen entry is wired)
static void test_whitelist_names_corpus_requests(void)
{
    const std::vector<std::string> names = corpusNames();
    const std::set<std::string> have(names.begin(), names.end());
    for (const auto &w : kWhitelist)
        TEST_ASSERT_TRUE_MESSAGE(have.count(w.first) == 1, (std::string("whitelist names no corpus request: ") + w.first).c_str());
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

// the config buffer cap used by the stubs is the one of the source
static void test_config_json_max_is_the_source_value(void)
{
    const std::string h = repo_slurp("src/config_json.h");
    TEST_ASSERT_TRUE_MESSAGE(h.find("CONFIG_JSON_MAX 6144") != std::string::npos, "CONFIG_JSON_MAX changed: update -D CONFIG_JSON_MAX in the env");
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
    RUN_TEST(test_corpus_reaches_the_handlers);
    return UNITY_END();
}
