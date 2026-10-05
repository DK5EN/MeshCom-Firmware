// Host test for src/web_functions/web_guard.h -- the pure request guard of the node web server
// (RM GUI, contract C3). Header-only, no Arduino.
//
//   pio test -e native_web_guard -f test_web_guard   (host only, no hardware)
//
// Layout: table-driven Host and Sec-Fetch-Site matrices, header parsing edge cases (case,
// folding, duplicates, truncation at the server's 1023 byte window, CRLF games), the helper
// predicates, and three regression tests named after the verified attacks.

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <web_functions/web_guard.h>

void setUp(void) {}
void tearDown(void) {}

#define CRLF "\r\n"
#define XMC "X-MC: 1" // the custom header of every state-changing request

static WebGuardVerdict check(const std::string &hdr, const char *path)
{
    return webGuardCheck(hdr.data(), hdr.size(), path);
}

// request line + header lines (each given WITHOUT line end) + blank line
static std::string req(const char *requestLine, std::initializer_list<const char *> headers)
{
    std::string s = requestLine;
    s += CRLF;
    for (const char *h : headers)
    {
        s += h;
        s += CRLF;
    }
    s += CRLF;
    return s;
}

static const char *vname(WebGuardVerdict v)
{
    return v == WG_OK ? "WG_OK" : (v == WG_BAD_HOST ? "WG_BAD_HOST" : "WG_CROSS_SITE");
}

static void expect(WebGuardVerdict want, const std::string &hdr, const char *path, const char *what)
{
    WebGuardVerdict got = check(hdr, path);
    char msg[300];
    snprintf(msg, sizeof(msg), "%s: want %s got %s", what, vname(want), vname(got));
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)want, (int)got, msg);
}

// ---------------------------------------------------------------- Host

static void host_case(const char *host, WebGuardVerdict want)
{
    std::string h = std::string("Host: ") + host;
    std::string r = req("GET /setparam/?manualcommand=--info HTTP/1.1", {h.c_str(), XMC});
    expect(want, r, "/setparam/", host);
}

void test_host_rebinding_names_rejected(void)
{
    static const char *const bad[] = {
        "attacker.com",
        "evil.local.attacker.com",
        "192.168.68.71.evil.com",
        "0x7f000001",
        "1.2.3",
        "1.2.3.4.5",
        "256.1.1.1",
        "010.1.1.1",
        "192.168.68.71:99999",
        "192.168.68.71:",
        "192.168.68.71:80:80",
        "192.168.68.71 evil.com",
        "evil.com:80",
        "localhost.evil.com",
        "dk5en-1.localx",
        "2130706433",
        "fritz.box.attacker.com",
        "evil.lan.attacker.com",
        "evil.internal.com",
        "x.home.arpa.evil.com",
        ".lan",
        "a_b",
        "a b",
        "fritz.box",
        ".local",
        "a..local",
        "[fe80::1",
        "[fe80::1]x",
        "[fe80::1]:",
        "[zz::1]",
        "[fe80::1%25eth0]",
        "[1]",
        "",
        "   ",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        host_case(bad[i], WG_BAD_HOST);
    }
}

void test_host_good_names_accepted(void)
{
    static const char *const good[] = {
        "192.168.68.71",
        "192.168.68.71:80",
        "127.0.0.1:8080",
        "0.0.0.0",
        "255.255.255.255",
        "dk5en-1.local",
        "DK5EN-1.LOCAL:80",
        "dk5en-1.local.",
        "a.b.local",
        "[fe80::1]:80",
        "[fe80::1]",
        "[::1]",
        "[::ffff:192.168.1.1]:8080",
        "localhost",
        "LOCALHOST:8080",
        "local",
        "dk5en-1",
        "DK5EN-1:8080",
        "node",
        "dk5en-1.fritz.box",
        "DK5EN-1.FRITZ.BOX:80",
        "a.lan",
        "dk5en-1.home.arpa",
        "printer.internal",
        "localhost.",
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        host_case(good[i], WG_OK);
}

void test_host_value_trimmed(void)
{
    expect(WG_OK, req("GET / HTTP/1.1", {"Host:   192.168.68.71  "}), "/", "padded");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host:\t192.168.68.71"}), "/", "tab");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host:192.168.68.71"}), "/", "no space");
}

void test_host_name_case_variants(void)
{
    expect(WG_OK, req("GET /setparam/?a=b HTTP/1.1", {"HOST: 192.168.68.71", XMC}), "/setparam/", "HOST");
    expect(WG_OK, req("GET /setparam/?a=b HTTP/1.1", {"hOsT: 192.168.68.71", XMC}), "/setparam/", "hOsT");
    expect(WG_BAD_HOST, req("GET /setparam/?a=b HTTP/1.1", {"HOST: evil.com"}), "/setparam/", "HOST evil");
    // whitespace before the colon is not a Host header: missing Host on HTTP/1.1
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host : 192.168.68.71"}), "/", "space before colon");
    // a header whose NAME merely starts with Host
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Hostname: 192.168.68.71"}), "/", "Hostname is not Host");
}

void test_host_missing_http11_rejected_http10_accepted(void)
{
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"User-Agent: x"}), "/", "no Host, 1.1");
    expect(WG_BAD_HOST, req("GET /setparam/?a=b HTTP/1.1", {}), "/setparam/", "no headers at all, 1.1");
    expect(WG_OK, req("GET / HTTP/1.0", {"User-Agent: x"}), "/", "no Host, complete HTTP/1.0");
    // not complete (no blank line) and not truncated: lenient unit-test input, still not "legacy"
    expect(WG_BAD_HOST, std::string("GET / HTTP/1.0" CRLF "User-Agent: x" CRLF), "/", "1.0 without blank line");
    // a missing version token is not HTTP/1.0
    expect(WG_BAD_HOST, req("GET /", {}), "/", "no version");
    // 1.0 with a bad Host is still bad
    expect(WG_BAD_HOST, req("GET / HTTP/1.0", {"Host: evil.com"}), "/", "1.0 with evil Host");
}

void test_host_duplicates_and_folding(void)
{
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Host: evil.com"}), "/", "dup good then evil");
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: evil.com", "Host: 192.168.68.71"}), "/", "dup evil then good");
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Host: 192.168.68.71"}), "/", "dup identical");
    // obs-fold of the Host value: ambiguous, refused
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: 192.168.68.71", " .evil.com"}), "/", "folded Host");
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "\t.evil.com"}), "/", "tab-folded Host");
    // a fold on ANOTHER header does not touch Host
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "X-A: b", " continued"}), "/", "fold elsewhere");
}

// ---------------------------------------------------------------- Sec-Fetch-Site

struct FsCase
{
    const char *site;       // Sec-Fetch-Site value, NULL = header absent
    WebGuardVerdict wantState; // for a state-changing path
};

static const FsCase fsMatrix[] = {
    {NULL, WG_OK},           // curl, python tools, old browsers
    {"same-origin", WG_OK},
    {"none", WG_OK},
    {"NONE", WG_OK},
    {"cross-site", WG_CROSS_SITE},
    {"same-site", WG_CROSS_SITE},
    {"Cross-Site", WG_CROSS_SITE},
    {"SAME-SITE", WG_CROSS_SITE},
    {"", WG_CROSS_SITE},               // present but empty
    {"some-future-value", WG_CROSS_SITE}, // unknown: fail closed
    {"same-origin, cross-site", WG_CROSS_SITE},
};

static void run_fs_matrix(const char *line, const char *path, bool xmc)
{
    for (size_t i = 0; i < sizeof(fsMatrix) / sizeof(fsMatrix[0]); i++)
    {
        std::string sf = fsMatrix[i].site ? std::string("Sec-Fetch-Site: ") + fsMatrix[i].site : std::string("X-Other: 1");
        std::string r = req(line, {"Host: 192.168.68.71", sf.c_str(), xmc ? XMC : "X-Other: 2", "Sec-Fetch-Mode: cors", "Sec-Fetch-Dest: empty"});
        char what[160];
        snprintf(what, sizeof(what), "%s site=%s", path, fsMatrix[i].site ? fsMatrix[i].site : "(absent)");
        expect(fsMatrix[i].wantState, r, path, what);
    }
}

// state-changing paths carry X-MC (the real clients do): only Sec-Fetch-Site decides
void test_fetch_site_matrix_setparam(void) { run_fs_matrix("GET /setparam/?manualcommand=--info HTTP/1.1", "/setparam/", true); }
void test_fetch_site_matrix_callfunction(void) { run_fs_matrix("GET /callfunction/?otaupdate HTTP/1.1", "/callfunction/", true); }
void test_fetch_site_matrix_rmsend(void) { run_fs_matrix("POST /rmsend HTTP/1.1", "/rmsend", true); }
void test_fetch_site_matrix_post_config(void) { run_fs_matrix("POST /config HTTP/1.1", "/config", true); }
void test_fetch_site_matrix_rmpasswd_rmnodes(void)
{
    run_fs_matrix("POST /rmpasswd HTTP/1.1", "/rmpasswd", true);
    run_fs_matrix("POST /rmnodes HTTP/1.1", "/rmnodes", true);
}
void test_fetch_site_matrix_sendmessage_login(void)
{
    run_fs_matrix("GET /?sendmessage=hi HTTP/1.1", "/?sendmessage=hi", true);
    run_fs_matrix("GET /?nodepassword=pw HTTP/1.1", "/?nodepassword=pw", true);
}
// read-only GETs need no X-MC
void test_fetch_site_matrix_read_only(void)
{
    run_fs_matrix("GET /config.json HTTP/1.1", "/config.json", false);
    run_fs_matrix("GET /rmstatus HTTP/1.1", "/rmstatus", false);
    run_fs_matrix("GET /rmnodes HTTP/1.1", "/rmnodes", false);
    run_fs_matrix("GET /rmheard HTTP/1.1", "/rmheard", false);
    run_fs_matrix("GET /getparam/?x=y HTTP/1.1", "/getparam/", false);
    run_fs_matrix("GET /?getmessages HTTP/1.1", "/?getmessages", false);
}

void test_page_load_exception(void)
{
    // top-level cross-site navigation (a link on another site): allowed
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: navigate", "Sec-Fetch-Dest: document"}), "/", "GET / navigation");
    expect(WG_OK, req("GET /?page=setup HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/?page=setup", "page=setup navigation");
    expect(WG_OK, req("GET /?page=remote HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-site", "Sec-Fetch-Dest: document"}), "/?page=remote", "same-site page");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site"}), "/", "no Dest header");
    // anything that is not a document navigation
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: image"}), "/", "img of /");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: iframe"}), "/", "iframe");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: script"}), "/", "script");
    expect(WG_CROSS_SITE, req("GET /?page=setup HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: cors", "Sec-Fetch-Dest: empty"}), "/?page=setup", "cross-site fetch of a page (readable under ACAO *)");
    // method, target and path must all say "page load"
    expect(WG_CROSS_SITE, req("POST / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/", "POST /");
    expect(WG_CROSS_SITE, req("GET /?page=setup&x=1 HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/?page=setup", "extra query");
    expect(WG_CROSS_SITE, req("GET /?page=a/setparam/?manualcommand=--reboot HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/?page=a/setparam/?manualcommand=--reboot", "route smuggled behind a page name");
    expect(WG_CROSS_SITE, req("GET /setparam/?manualcommand=--reboot HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/setparam/", "navigation to /setparam/");
    expect(WG_CROSS_SITE, req("GET /?nodepassword= HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/?nodepassword=", "logout navigation");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/setparam/", "caller path disagrees");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), NULL, "NULL path");
    // the server routes by indexOf over the WHOLE header: a Referer can route a plain GET /
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document", "Referer: http://evil.example/setparam/?manualcommand=--reboot"}), "/", "Referer carries /setparam/");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document", "Referer: http://evil.example/callfunction/?otaupdate"}), "/", "Referer carries /callfunction/");
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document", "Referer: http://evil.example/config.json"}), "/", "Referer carries /config");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document", "Referer: http://example.org/"}), "/", "harmless Referer");
    // the exception never rescues a bad Host
    expect(WG_BAD_HOST, req("GET / HTTP/1.1", {"Host: evil.com", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: document"}), "/", "page load with rebinding Host");
}

void test_fetch_site_header_name_case(void)
{
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "SEC-FETCH-SITE: cross-site"}), "/setparam/", "upper-case name");
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "sec-fetch-site:cross-site"}), "/setparam/", "lower-case, no space");
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "sEc-FeTcH-sItE:\tcross-site"}), "/setparam/", "mixed case, tab");
    expect(WG_OK, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", XMC, "SEC-FETCH-SITE: SAME-ORIGIN"}), "/setparam/", "upper-case same-origin");
}

void test_fetch_site_duplicates_and_folding(void)
{
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-origin", "Sec-Fetch-Site: cross-site"}), "/rmsend", "ok then cross");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Site: same-origin"}), "/rmsend", "cross then ok");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-origin", " cross-site"}), "/rmsend", "folded value");
    // a folded line that LOOKS like the header is a continuation of the previous one, not a header
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", XMC, "X-A: b", " Sec-Fetch-Site: cross-site"}), "/rmsend", "folded pseudo header");
}

// ---------------------------------------------------------------- Origin fallback

void test_origin_fallback_without_fetch_metadata(void)
{
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://evil.example"}), "/rmsend", "foreign Origin");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: null"}), "/rmsend", "Origin null");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71:8080"}), "/rmsend", "port differs");
    expect(WG_CROSS_SITE, req("GET /?page=setup HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://evil.example"}), "/?page=setup", "foreign Origin on a page fetch");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71", "Origin: http://evil.example"}), "/rmsend", "dup Origin");
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71"}), "/rmsend", "own Origin");
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: DK5EN-1.local:80", "Origin: http://dk5en-1.local:80"}), "/rmsend", "own Origin with port, case");
    // Sec-Fetch-Site says same-origin: Origin is not consulted
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-origin", "Origin: http://192.168.68.71"}), "/rmsend", "same-origin with Origin");
}

// ---------------------------------------------------------------- parsing edge cases

void test_empty_and_degenerate_input(void)
{
    TEST_ASSERT_EQUAL_INT(WG_BAD_HOST, webGuardCheck(NULL, 0, "/"));
    TEST_ASSERT_EQUAL_INT(WG_BAD_HOST, webGuardCheck("", 0, "/"));
    TEST_ASSERT_EQUAL_INT(WG_BAD_HOST, webGuardCheck("GET / HTTP/1.1", 0, "/")); // length wins over the pointer
    TEST_ASSERT_EQUAL_INT(WG_BAD_HOST, webGuardCheck(NULL, 10, "/"));
    expect(WG_BAD_HOST, "\r\n", "/", "just CRLF");
    expect(WG_BAD_HOST, "\n\n\n", "/", "just LF");
    expect(WG_BAD_HOST, "Host: 192.168.68.71\r\n\r\n", "/", "headers without a request line: Host is read as the request line");
    expect(WG_BAD_HOST, "GET", "/", "three bytes");
    expect(WG_BAD_HOST, std::string(1, '\0'), "/", "NUL");
    // NULL path: the request-line target still says /setparam
    expect(WG_OK, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", XMC}), NULL, "NULL path, with X-MC");
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71"}), NULL, "NULL path, no X-MC");
}

void test_bare_lf_line_endings(void)
{
    expect(WG_OK, "GET /setparam/?a=b HTTP/1.1\nHost: 192.168.68.71\nX-MC: 1\n\n", "/setparam/", "bare LF, good");
    expect(WG_BAD_HOST, "GET /setparam/?a=b HTTP/1.1\nHost: evil.com\n\n", "/setparam/", "bare LF, evil");
    expect(WG_CROSS_SITE, "GET /setparam/?a=b HTTP/1.1\nHost: 192.168.68.71\nSec-Fetch-Site: cross-site\n\n", "/setparam/", "bare LF, cross-site");
}

void test_header_like_text_in_query_value_is_inert(void)
{
    // the request line is never read as a header: a victim URL cannot spoof Host or Sec-Fetch-Site
    expect(WG_OK, req("GET /setparam/?manualcommand=Host:evil.com HTTP/1.1", {"Host: 192.168.68.71", XMC}), "/setparam/", "Host: in query");
    expect(WG_OK, req("GET /setparam/?manualcommand=Sec-Fetch-Site:cross-site HTTP/1.1", {"Host: 192.168.68.71", XMC}), "/setparam/", "cross-site in query, curl style");
    expect(WG_CROSS_SITE, req("GET /setparam/?x=Sec-Fetch-Site:same-origin HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site"}), "/setparam/", "same-origin in query does not rescue a cross-site header");
    expect(WG_BAD_HOST, req("GET /setparam/?x=Host:192.168.68.71 HTTP/1.1", {"Host: evil.com"}), "/setparam/", "good Host in query does not rescue a bad header");
    expect(WG_OK, req("GET /?page=x HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-origin"}), "/?page=x", "plain");
    // percent-encoded newlines stay literal text
    expect(WG_CROSS_SITE, req("GET /setparam/?x=%0d%0aSec-Fetch-Site:%20same-origin HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site"}), "/setparam/", "%0d%0a in query");
}

void test_crlf_injection_attempts(void)
{
    // a smuggled second Host
    expect(WG_BAD_HOST, std::string("GET / HTTP/1.1\r\nHost: 192.168.68.71\r\nHost: evil.com\r\n\r\n"), "/", "injected Host");
    expect(WG_BAD_HOST, std::string("GET / HTTP/1.1\r\nHost: evil.com\r\nHost: 192.168.68.71\r\n\r\n"), "/", "injected good Host after evil");
    // CR alone is not a line end: a value containing CR is not a hostname
    expect(WG_BAD_HOST, std::string("GET / HTTP/1.1\r\nHost: 192.168.68.71\rHost: evil.com\r\n\r\n"), "/", "bare CR inside the value");
    // NUL in the value
    static const char nulReq[] = "GET / HTTP/1.1\r\nHost: 192.168.68.71\0evil.com\r\n\r\n";
    expect(WG_BAD_HOST, std::string(nulReq, sizeof(nulReq) - 1), "/", "NUL in Host");
    // injected spoof of an acceptable Sec-Fetch-Site next to the real one: any bad value wins
    expect(WG_CROSS_SITE, std::string("GET /setparam/?a=b HTTP/1.1\r\nHost: 192.168.68.71\r\nSec-Fetch-Site: cross-site\r\nSec-Fetch-Site: same-origin\r\n\r\n"), "/setparam/", "spoofed same-origin after real cross-site");
    // header text after the blank line is body, not header
    expect(WG_OK, std::string("POST /rmsend HTTP/1.1\r\nHost: 192.168.68.71\r\nX-MC: 1\r\n\r\nSec-Fetch-Site: cross-site\r\n\r\n"), "/rmsend", "body text after blank line is ignored");
    expect(WG_CROSS_SITE, std::string("POST /rmsend HTTP/1.1\r\nHost: 192.168.68.71\r\n\r\nX-MC: 1\r\n\r\n"), "/rmsend", "X-MC in the body does not count");
    expect(WG_BAD_HOST, std::string("POST /rmsend HTTP/1.1\r\n\r\nHost: 192.168.68.71\r\n\r\n"), "/rmsend", "Host in the body does not count");
}

void test_long_complete_header_is_judged_in_full(void)
{
    // 100 kB header, complete: Sec-Fetch-Site at the very end is found
    std::string pad(100000, 'A');
    std::string pad2 = "X-Pad: " + pad;
    std::string r = req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", pad2.c_str(), "Sec-Fetch-Site: cross-site"});
    expect(WG_CROSS_SITE, r, "/setparam/", "100 kB complete, cross-site at the end");
    r = req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", pad2.c_str(), XMC});
    expect(WG_OK, r, "/setparam/", "100 kB complete, X-MC at the end");
    r = req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", pad2.c_str()});
    expect(WG_CROSS_SITE, r, "/setparam/", "100 kB complete, no X-MC");
    // a header just under the window, complete, with Sec-Fetch-Site late
    std::string p2 = "X-Pad: " + std::string(850, 'B');
    r = req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", p2.c_str(), "Sec-Fetch-Site: same-origin", XMC});
    TEST_ASSERT_TRUE(r.size() < (size_t)WEB_GUARD_HEADER_MAX);
    expect(WG_OK, r, "/rmsend", "just under the window");
}

// the server keeps WEB_GUARD_HEADER_MAX bytes; emulate its cut
static std::string serverWindow(const std::string &full)
{
    return full.substr(0, full.size() < (size_t)WEB_GUARD_HEADER_MAX ? full.size() : (size_t)WEB_GUARD_HEADER_MAX);
}

void test_truncated_header_fails_closed(void)
{
    // padding in the request line pushes Host and Sec-Fetch-Site out of the window
    std::string q(1100, 'A');
    std::string line = "GET /setparam/?manualcommand=--reboot&pad=" + q + " HTTP/1.1";
    std::string full = req(line.c_str(), {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: image"});
    std::string cut = serverWindow(full);
    TEST_ASSERT_EQUAL_UINT((size_t)WEB_GUARD_HEADER_MAX, cut.size());
    expect(WG_BAD_HOST, cut, "/setparam/", "request line longer than the window");

    // padding header pushes Sec-Fetch-Site out, Host survives
    std::string p = "X-Pad: " + std::string(1000, 'C');
    full = req("GET /setparam/?manualcommand=--reboot HTTP/1.1", {"Host: 192.168.68.71", p.c_str(), "Sec-Fetch-Site: cross-site", "Sec-Fetch-Dest: image"});
    cut = serverWindow(full);
    expect(WG_CROSS_SITE, cut, "/setparam/", "X-MC and Sec-Fetch-Site beyond the window, Host inside");
    // a cut header on a state-changing path without X-MC seen fails closed, even with a matching Origin
    full = req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71", p.c_str(), "X-MC: 1"});
    expect(WG_CROSS_SITE, serverWindow(full), "/rmsend", "Origin inside, X-MC beyond the window");
    // X-MC seen inside the window of a long request: fine
    full = req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "X-MC: 1", p.c_str(), "Origin: http://evil.example"});
    expect(WG_OK, serverWindow(full), "/rmsend", "X-MC inside, hostile Origin beyond the window (browsers cannot reorder)");
    // read-only GET: the guard sees what the router sees, nothing to prove
    full = req("GET /rmstatus HTTP/1.1", {"Host: 192.168.68.71", p.c_str(), "Sec-Fetch-Site: cross-site"});
    expect(WG_OK, serverWindow(full), "/rmstatus", "read-only GET, cut header");
    // a read-only target cannot hide a state token beyond the window: the router never sees it either

    // the window cuts INSIDE the Host value: "192.168.68.71.evil.com" looks like an IP up to the cut
    std::string evil = "Host: 192.168.68.71.evil.com";
    std::string rl = "GET /setparam/?manualcommand=--reboot&pad=";
    size_t fill = (size_t)WEB_GUARD_HEADER_MAX - rl.size() - strlen(" HTTP/1.1\r\n") - strlen("Host: 192.168.68.71");
    std::string line2 = rl + std::string(fill, 'D') + " HTTP/1.1";
    full = req(line2.c_str(), {evil.c_str(), "Sec-Fetch-Site: cross-site"});
    cut = serverWindow(full);
    TEST_ASSERT_TRUE(cut.find("192.168.68.71") != std::string::npos && cut.find(".evil") == std::string::npos);
    expect(WG_BAD_HOST, cut, "/setparam/", "cut inside the Host value");

    // a request that is long but whose Sec-Fetch-Site IS inside the window works (browser, same-origin)
    std::string p3 = "X-Pad: " + std::string(600, 'E');
    full = req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Sec-Fetch-Site: same-origin", "X-MC: 1", p3.c_str(), p3.c_str()});
    expect(WG_OK, serverWindow(full), "/rmsend", "X-MC seen before the cut");
}

// ---------------------------------------------------------------- helper predicates

void test_is_state_changing(void)
{
    static const char *const yes[] = {"/setparam/?manualcommand=--reboot", "/callfunction/?otaupdate", "/rmsend", "/rmpasswd",
                                      "/?sendmessage=x", "/?nodepassword=pw", "/?nodepassword=", "/?page=a/setparam/?x"};
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(webGuardIsStateChanging(yes[i]), yes[i]);
    static const char *const no[] = {"/", "/?page=setup", "/?page=remote", "/getparam/?x=y", "/?getmessages", "/config.json",
                                     "/rmstatus", "/rmnodes", "/rmheard", "/index.js", ""};
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(webGuardIsStateChanging(no[i]), no[i]);
    TEST_ASSERT_FALSE(webGuardIsStateChanging(NULL));
    // anything POST, incl. POST /rmnodes and POST /config
    TEST_ASSERT_TRUE(webGuardIsStateChanging("/", true));
    TEST_ASSERT_TRUE(webGuardIsStateChanging("/rmnodes", true));
    TEST_ASSERT_TRUE(webGuardIsStateChanging("/config", true));
    TEST_ASSERT_TRUE(webGuardIsStateChanging(NULL, true));
}

void test_cors_allowed(void)
{
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/config"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/config.json"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/rmstatus"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/rmnodes"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/rmheard"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/setparam/?a=b"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/callfunction/?a"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/rmsend"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/rmpasswd"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/?sendmessage=x"));
    TEST_ASSERT_FALSE(webGuardCorsAllowed("/", true)); // POST
    TEST_ASSERT_TRUE(webGuardCorsAllowed("/"));
    TEST_ASSERT_TRUE(webGuardCorsAllowed("/?page=info"));
    TEST_ASSERT_TRUE(webGuardCorsAllowed("/getparam/?x=y"));
    TEST_ASSERT_TRUE(webGuardCorsAllowed(NULL));
}

// ---------------------------------------------------------------- X-MC / Origin proof (plain http)

static const char *const IMG_UA = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36";
static const char *const IMG_ACCEPT = "Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8";

// What a browser really sends for an <img> to http://<LAN IP>/...: NO Sec-Fetch-*, NO Origin.
static std::string imgGet(const char *target, const char *extra)
{
    std::string line = std::string("GET ") + target + " HTTP/1.1";
    return req(line.c_str(), {"Host: 192.168.68.71", "Connection: keep-alive", IMG_UA, IMG_ACCEPT,
                              "Referer: http://attacker.example/", "Accept-Encoding: gzip, deflate", extra});
}

void test_img_csrf_matrix_without_fetch_metadata(void)
{
    static const char *const targets[] = {
        "/setparam/?manualcommand=--passwd%20x", "/setparam/?manualcommand=--reboot", "/callfunction/?otaupdate",
        "/?sendmessage=hi", "/?nodepassword=", "/rmsend?dst=X&cmd=reboot", "/rmpasswd?pw=x"};
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++)
    {
        expect(WG_CROSS_SITE, imgGet(targets[i], "X-Other: 1"), targets[i], targets[i]);
        // the same request from our own scaffold JS carries X-MC
        expect(WG_OK, imgGet(targets[i], XMC), targets[i], targets[i]);
    }
}

void test_cross_origin_post_without_xmc_rejected(void)
{
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Content-Length: 40", "Content-Type: text/plain", "Origin: http://attacker.com", "Referer: http://attacker.com/x"}), "/rmsend", "cross-origin POST");
    expect(WG_CROSS_SITE, req("POST /rmpasswd HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://attacker.com"}), "/rmpasswd", "cross-origin POST rmpasswd");
    expect(WG_CROSS_SITE, req("POST /config HTTP/1.1", {"Host: 192.168.68.71", "Origin: null"}), "/config", "sandboxed/opaque Origin");
    // a hostile Origin is refused even if the attacker could add X-MC
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://attacker.com", XMC}), "/rmsend", "cross-origin POST with X-MC");
    // a POST with neither proof (form post from curl without the header, or a form-less client)
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Content-Length: 40"}), "/rmsend", "POST with no proof");
    expect(WG_CROSS_SITE, req("POST /rmnodes HTTP/1.1", {"Host: 192.168.68.71"}), "/rmnodes", "POST /rmnodes, no proof");
    expect(WG_CROSS_SITE, req("POST / HTTP/1.1", {"Host: 192.168.68.71"}), "/", "any POST");
    expect(WG_CROSS_SITE, req("OPTIONS /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71:81", "Access-Control-Request-Headers: x-mc"}), "/rmsend", "preflight");
    expect(WG_CROSS_SITE, req("OPTIONS /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Access-Control-Request-Headers: x-mc"}), "/rmsend", "preflight, no Origin");
}

void test_same_origin_post_with_matching_origin_passes(void)
{
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71:80", "Origin: http://192.168.68.71:80", "Content-Length: 40"}), "/rmsend", "Origin equals Host incl. port");
    expect(WG_OK, req("POST /rmpasswd HTTP/1.1", {"Host: DK5EN-1.local", "Origin: http://dk5en-1.local"}), "/rmpasswd", "case-insensitive .local");
    expect(WG_OK, req("POST /rmnodes HTTP/1.1", {"Host: [fe80::1]:8080", "Origin: http://[fe80::1]:8080"}), "/rmnodes", "IPv6 literal");
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: https://192.168.68.71"}), "/rmsend", "scheme is ignored");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71:80", "Origin: http://192.168.68.71"}), "/rmsend", "port spelled differently: not equal");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71.attacker.com"}), "/rmsend", "Origin is a longer name");
    expect(WG_CROSS_SITE, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "Origin: http://192.168.68.71", "Origin: http://192.168.68.71"}), "/rmsend", "duplicate Origin");
}

void test_read_only_gets_need_no_proof(void)
{
    static const char *const ro[] = {"/rmstatus", "/config.json", "/rmnodes", "/rmheard", "/getparam/?x=y", "/?getmessages",
                                     "/?page=setup", "/?page=remote", "/", "/favicon.ico", "/style.css"};
    for (size_t i = 0; i < sizeof(ro) / sizeof(ro[0]); i++)
    {
        std::string line = std::string("GET ") + ro[i] + " HTTP/1.1";
        expect(WG_OK, req(line.c_str(), {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Accept: */*"}), ro[i], ro[i]);
        expect(WG_OK, req(line.c_str(), {"Host: dk5en-1.local", "Referer: http://attacker.example/"}), ro[i], ro[i]);
    }
}

void test_xmc_header_name_case_and_value(void)
{
    static const char *const good[] = {"X-MC: 1", "x-mc: 1", "X-Mc:1", "X-MC:\t1", "x-MC:   1  ", "X-MC:\t \t1\t"};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        expect(WG_OK, req("GET /setparam/?manualcommand=--info HTTP/1.1", {"Host: 192.168.68.71", good[i]}), "/setparam/", good[i]);
    static const char *const bad[] = {"X-MC: 0", "X-MC:", "X-MC: ", "X-MC: 2", "X-MC: 11", "X-MC: 1 1", "X-MC: true", "X-MC: -1",
                                      "X-MC: 01", "X-MC: 1;", "X-MC: yes",
                                      // not the header at all
                                      "XMC: 1", "X-MC : 1", "X-MCX: 1", "X-M: 1", "X-MC-: 1", "Y-X-MC: 1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        expect(WG_CROSS_SITE, req("GET /setparam/?manualcommand=--info HTTP/1.1", {"Host: 192.168.68.71", bad[i]}), "/setparam/", bad[i]);
    // duplicates: every X-MC must be "1"
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "X-MC: 1", "X-MC: 0"}), "/setparam/", "1 then 0");
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "X-MC: 0", "X-MC: 1"}), "/setparam/", "0 then 1");
    expect(WG_OK, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "X-MC: 1", "X-MC: 1"}), "/setparam/", "1 then 1");
    // folded value
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", "X-MC: 1", " 0"}), "/setparam/", "folded X-MC");
    // text in the request line or a query value is no header
    expect(WG_CROSS_SITE, req("GET /setparam/?manualcommand=X-MC:%201 HTTP/1.1", {"Host: 192.168.68.71"}), "/setparam/", "X-MC in query");
    expect(WG_CROSS_SITE, std::string("GET /setparam/?x=1 HTTP/1.1\r\nHost: 192.168.68.71\r\n\r\nX-MC: 1\r\n\r\n"), "/setparam/", "X-MC after the blank line");
    // X-MC does not rescue a bad Host or a cross-site Sec-Fetch-Site
    expect(WG_BAD_HOST, req("GET /setparam/?a=b HTTP/1.1", {"Host: attacker.com", XMC}), "/setparam/", "X-MC with rebinding Host");
    expect(WG_CROSS_SITE, req("GET /setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71", XMC, "Sec-Fetch-Site: cross-site"}), "/setparam/", "X-MC with cross-site");
}

void test_state_change_hidden_in_other_header_is_still_state_changing(void)
{
    // the server routes by indexOf over the whole header: a Referer carrying /setparam/ routes
    // a harmless-looking GET / (the Referer path of an <img> with referrerpolicy=unsafe-url)
    expect(WG_CROSS_SITE, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Referer: http://attacker.example/setparam/?manualcommand=--passwd%20x"}), "/", "Referer smuggles /setparam/");
    expect(WG_CROSS_SITE, req("GET /?page=info HTTP/1.1", {"Host: 192.168.68.71", "Referer: http://attacker.example/callfunction/?otaupdate"}), "/?page=info", "Referer smuggles /callfunction/");
    expect(WG_CROSS_SITE, req("GET /index.js HTTP/1.1", {"Host: 192.168.68.71", "Referer: http://attacker.example/?sendmessage=x"}), "/index.js", "Referer smuggles ?sendmessage");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Referer: http://attacker.example/setparam/?manualcommand=--passwd%20x", XMC}), "/", "same with X-MC");
    expect(WG_OK, req("GET / HTTP/1.1", {"Host: 192.168.68.71", "Referer: http://attacker.example/page.html"}), "/", "harmless Referer");
    // the caller's path and the request-line target are both consulted
    expect(WG_CROSS_SITE, req("GET /index.js HTTP/1.1", {"Host: 192.168.68.71"}), "/setparam/?a=b", "caller path says setparam");
    expect(WG_CROSS_SITE, req("GET /x/setparam/?a=b HTTP/1.1", {"Host: 192.168.68.71"}), "/x", "target says setparam, caller path does not");
}

void test_curl_style_requests(void)
{
    // read-only: no header needed
    expect(WG_OK, req("GET /config.json HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Accept: */*"}), "/config.json", "curl config.json");
    expect(WG_OK, req("GET /rmstatus HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1"}), "/rmstatus", "curl rmstatus");
    // state-changing: refused on purpose without X-MC, accepted with it
    expect(WG_CROSS_SITE, req("GET /setparam/?manualcommand=--info HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Accept: */*"}), "/setparam/", "curl /setparam without X-MC");
    expect(WG_OK, req("GET /setparam/?manualcommand=--info HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Accept: */*", XMC}), "/setparam/", "curl /setparam with X-MC (curl -H 'X-MC: 1')");
    expect(WG_OK, req("POST /rmsend HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Content-Length: 40", "Content-Type: application/x-www-form-urlencoded", XMC}), "/rmsend", "curl POST /rmsend with X-MC");
}

void test_host_names_single_label_and_router_suffixes(void)
{
    static const char *const good[] = {"dk5en-1", "node", "a", "DK5EN-1:80", "x1", "dk5en-1.fritz.box", "a.b.fritz.box", "dk5en-1.lan", "dk5en-1.speedport.ip", "node.localdomain", "node.home",
                                       "dk5en-1.home.arpa", "node.internal", "DK5EN-1.LAN:8080", "dk5en-1.fritz.box."};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        host_case(good[i], WG_OK);
    static const char *const bad[] = {"attacker.com", "evil.local.attacker.com", "192.168.68.71.evil.com", "0x7f000001", "0X7F000001",
                                      "2130706433", "1.2.3", "dk5en-1.lan.evil.com", "dk5en-1.fritz.box.evil.com", "fritz.box",
                                      ".lan", ".fritz.box", "a..lan", "a_b", "dk5en-1.com", "x.box", "a.arpa",
                                      "home.arpa", ".home.arpa", "evil.internal.evil.com"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        host_case(bad[i], WG_BAD_HOST);
    // 63 chars is the label limit
    std::string l63(63, 'a'), l64(64, 'a');
    host_case(l63.c_str(), WG_OK);
    host_case(l64.c_str(), WG_BAD_HOST);
    host_case("lan", WG_OK); // a single label is a single label
}

// ---------------------------------------------------------------- regression tests (verified attacks)

// Attack 1: <img src="http://node/setparam/?manualcommand=--passwd x"> on any web page. The
// browser sends the victim's IP session along, the node ran every `--` command of a GET.
// The nodes are plain http://<LAN IP>: the browser sends NO Sec-Fetch-* and NO Origin for it.
void test_regression_img_csrf_setparam_get_is_rejected(void)
{
    std::string r = req("GET /setparam/?manualcommand=--passwd%20x HTTP/1.1",
                        {"Host: 192.168.68.71", "Connection: keep-alive", IMG_UA, IMG_ACCEPT,
                         "Referer: http://attacker.example/", "Accept-Encoding: gzip, deflate", "Accept-Language: en-US,en;q=0.9"});
    expect(WG_CROSS_SITE, r, "/setparam/", "img-tag CSRF, plain http, no Sec-Fetch, no Origin");
    expect(WG_CROSS_SITE, req("GET /setparam/?manualcommand=--reboot HTTP/1.1", {"Host: dk5en-1.local", IMG_ACCEPT, "Referer: http://attacker.example/"}), "/setparam/", "img-tag CSRF, .local name");
    expect(WG_CROSS_SITE, req("GET /callfunction/?otaupdate HTTP/1.1", {"Host: 192.168.68.71", IMG_ACCEPT}), "/callfunction/", "img-tag CSRF on callfunction");
    // when a browser does send fetch metadata (https front, localhost), it is rejected too
    std::string r2 = req("GET /setparam/?manualcommand=--reboot HTTP/1.1",
                         {"Host: 192.168.68.71", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: no-cors", "Sec-Fetch-Dest: image"});
    expect(WG_CROSS_SITE, r2, "/setparam/", "img-tag CSRF with Sec-Fetch-Site");
}

// Attack 2: DNS rebinding. The attacker page is same-origin for the browser (any custom header
// allowed, Origin equal to Host) but its Host header is the attacker name.
void test_regression_dns_rebinding_host_is_rejected(void)
{
    std::string r = req("POST /rmsend HTTP/1.1",
                        {"Host: rebind.attacker.example", "Connection: keep-alive", "Content-Length: 40",
                         "X-MC: 1", "Content-Type: application/json", "Origin: http://rebind.attacker.example"});
    expect(WG_BAD_HOST, r, "/rmsend", "rebinding POST with X-MC and matching Origin");
    std::string r2 = req("GET /setparam/?manualcommand=--reboot HTTP/1.1",
                         {"Host: rebind.attacker.example:8080", "X-MC: 1"});
    expect(WG_BAD_HOST, r2, "/setparam/", "rebinding GET with port");
    std::string r3 = req("GET /config.json HTTP/1.1", {"Host: 192-168-68-71.attacker.example"});
    expect(WG_BAD_HOST, r3, "/config.json", "rebinding config download");
}

// Control: the clients must keep working. urllib/curl/http.client send Host with the node IP
// or a LAN name; the tools add X-MC to state-changing calls, read-only calls need nothing.
void test_regression_curl_style_request_passes(void)
{
    std::string curl = req("GET /setparam/?manualcommand=--info HTTP/1.1",
                           {"Host: 192.168.68.71", "User-Agent: curl/8.7.1", "Accept: */*", "X-MC: 1"});
    expect(WG_OK, curl, "/setparam/", "curl with X-MC");
    std::string urllib = req("GET /callfunction/?otaupdate HTTP/1.1",
                             {"Accept-Encoding: identity", "Host: 192.168.68.71", "User-Agent: Python-urllib/3.12", "X-MC: 1", "Connection: close"});
    expect(WG_OK, urllib, "/callfunction/", "urllib (tools/webflash.py)");
    std::string post = req("POST /ota/upload HTTP/1.1",
                           {"Host: dk5en-1.local", "Content-Length: 1234", "Content-Type: multipart/form-data; boundary=x", "X-MC: 1"});
    expect(WG_OK, post, "/ota/upload", "multipart upload to a .local name");
    std::string rawsock = req("POST /ota/upload HTTP/1.1", {"Host: 192.168.68.75", "Content-Length: 99", "X-MC: 1"});
    expect(WG_OK, rawsock, "/ota/upload", "raw socket upload (ota_abort.py)");
    std::string cfg = req("GET /config.json HTTP/1.1", {"Host: 192.168.68.71", "User-Agent: curl/8.7.1"});
    expect(WG_OK, cfg, "/config.json", "header-less curl, read-only path");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_host_rebinding_names_rejected);
    RUN_TEST(test_host_good_names_accepted);
    RUN_TEST(test_host_value_trimmed);
    RUN_TEST(test_host_name_case_variants);
    RUN_TEST(test_host_missing_http11_rejected_http10_accepted);
    RUN_TEST(test_host_duplicates_and_folding);
    RUN_TEST(test_fetch_site_matrix_setparam);
    RUN_TEST(test_fetch_site_matrix_callfunction);
    RUN_TEST(test_fetch_site_matrix_rmsend);
    RUN_TEST(test_fetch_site_matrix_post_config);
    RUN_TEST(test_fetch_site_matrix_rmpasswd_rmnodes);
    RUN_TEST(test_fetch_site_matrix_sendmessage_login);
    RUN_TEST(test_fetch_site_matrix_read_only);
    RUN_TEST(test_page_load_exception);
    RUN_TEST(test_fetch_site_header_name_case);
    RUN_TEST(test_fetch_site_duplicates_and_folding);
    RUN_TEST(test_origin_fallback_without_fetch_metadata);
    RUN_TEST(test_empty_and_degenerate_input);
    RUN_TEST(test_bare_lf_line_endings);
    RUN_TEST(test_header_like_text_in_query_value_is_inert);
    RUN_TEST(test_crlf_injection_attempts);
    RUN_TEST(test_long_complete_header_is_judged_in_full);
    RUN_TEST(test_truncated_header_fails_closed);
    RUN_TEST(test_img_csrf_matrix_without_fetch_metadata);
    RUN_TEST(test_cross_origin_post_without_xmc_rejected);
    RUN_TEST(test_same_origin_post_with_matching_origin_passes);
    RUN_TEST(test_read_only_gets_need_no_proof);
    RUN_TEST(test_xmc_header_name_case_and_value);
    RUN_TEST(test_state_change_hidden_in_other_header_is_still_state_changing);
    RUN_TEST(test_curl_style_requests);
    RUN_TEST(test_host_names_single_label_and_router_suffixes);
    RUN_TEST(test_is_state_changing);
    RUN_TEST(test_cors_allowed);
    RUN_TEST(test_regression_img_csrf_setparam_get_is_rejected);
    RUN_TEST(test_regression_dns_rebinding_host_is_rejected);
    RUN_TEST(test_regression_curl_style_request_passes);
    return UNITY_END();
}
