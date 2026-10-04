// RM-02: src/remote_cmd.cpp (RM1 command DM core) against the vectors that
// tools/remote_cmd.py generates (tools/tests/remote_cmd_vectors.json) plus the
// regression cases of docs/concept-open-issues-20261004.md section 6.6 W1.
//
//   pio test -e native_remote_cmd

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>

#include <hmac_sha256.h>
#include <remote_cmd.h>

void setUp(void) {}
void tearDown(void) {}

// ---- vector file ----------------------------------------------------------

// __FILE__ is relative under PlatformIO's native runner, so walk up from the
// working directory until the test file and platformio.ini sit side by side.
static bool file_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

static bool read_repo_file(const char *rel, std::string &out)
{
    const std::string self = "test/test_remote_cmd/test_main.cpp";
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == nullptr)
        return false;
    std::string dir(cwd);
    for (int hops = 0; hops < 16; hops++)
    {
        if (file_exists(dir + "/" + self) && file_exists(dir + "/platformio.ini"))
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            return false;
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str(), std::ios::binary);
    if (!f.good())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Minimal reader for the flat objects of the vector file: no nesting inside an
// object, string values with \" and \\ escapes, numbers without quotes.
static bool jsonString(const std::string &obj, const char *key, std::string &out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos)
        return false;
    p = obj.find(':', p + needle.size());
    if (p == std::string::npos)
        return false;
    p = obj.find('"', p);
    if (p == std::string::npos)
        return false;
    out.clear();
    for (p++; p < obj.size() && obj[p] != '"'; p++)
    {
        if (obj[p] == '\\' && p + 1 < obj.size())
        {
            p++;
            out += (obj[p] == 'n') ? '\n' : obj[p];
        }
        else
            out += obj[p];
    }
    return true;
}

static bool jsonUint(const std::string &obj, const char *key, uint32_t &out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = obj.find(needle);
    if (p == std::string::npos)
        return false;
    p = obj.find(':', p + needle.size());
    if (p == std::string::npos)
        return false;
    p++;
    while (p < obj.size() && obj[p] == ' ')
        p++;
    uint64_t v = 0;
    bool any = false;
    for (; p < obj.size() && obj[p] >= '0' && obj[p] <= '9'; p++)
    {
        v = v * 10 + (uint64_t)(obj[p] - '0');
        any = true;
    }
    out = (uint32_t)v;
    return any;
}

// objects of one array: text between the '[' after "section" and its ']'
static size_t objectsOf(const std::string &json, const char *section, std::string *objs, size_t cap)
{
    const std::string needle = std::string("\"") + section + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos)
        return 0;
    p = json.find('[', p);
    const size_t end = json.find(']', p);
    size_t n = 0;
    while (p != std::string::npos && n < cap)
    {
        const size_t a = json.find('{', p);
        if (a == std::string::npos || a > end)
            break;
        const size_t b = json.find('}', a);
        objs[n++] = json.substr(a, b - a + 1);
        p = b;
    }
    return n;
}

static void test_vectors_commands_reproduced(void)
{
    std::string json;
    if (!read_repo_file("tools/tests/remote_cmd_vectors.json", json))
        TEST_FAIL_MESSAGE("tools/tests/remote_cmd_vectors.json not found");

    static std::string objs[64];
    const size_t n = objectsOf(json, "commands", objs, 64);
    TEST_ASSERT_EQUAL_INT_MESSAGE(17, n, "command vector count drifted from tools/remote_cmd.py generate_vectors()");

    for (size_t i = 0; i < n; i++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "command vector %u", (unsigned)i);

        std::string passwd, dst, src, cmd, args, canonical, keyHex, tag, dm;
        uint32_t ctr = 0;
        TEST_ASSERT_TRUE_MESSAGE(jsonString(objs[i], "passwd", passwd) && jsonString(objs[i], "dst", dst) &&
                                     jsonString(objs[i], "src", src) && jsonString(objs[i], "cmd", cmd) &&
                                     jsonString(objs[i], "args", args) &&
                                     jsonString(objs[i], "canonical", canonical) &&
                                     jsonString(objs[i], "key_hex", keyHex) && jsonString(objs[i], "tag", tag) &&
                                     jsonString(objs[i], "dm_text", dm) && jsonUint(objs[i], "ctr", ctr),
                                 msg);

        uint8_t key[32];
        char keyOut[65];
        rmDeriveKey(passwd.c_str(), key);
        hexLower(key, 32, keyOut);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(keyHex.c_str(), keyOut, msg);

        RmCmd c;
        TEST_ASSERT_TRUE_MESSAGE(rmParse(dm.c_str(), c), msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(ctr, c.ctr, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cmd.c_str(), c.cmd, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(args.c_str(), c.args, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(tag.c_str(), c.tag, msg);

        char canon[160];
        const size_t cl = rmCanonical(c, dst.c_str(), src.c_str(), canon, sizeof(canon));
        TEST_ASSERT_EQUAL_UINT_MESSAGE(canonical.size(), cl, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(canonical.c_str(), canon, msg);

        // tag independently through the HMAC primitive
        uint8_t mac[32];
        char hex[65];
        hmacSha256(key, 32, reinterpret_cast<const uint8_t *>(canon), cl, mac);
        hexLower(mac, 8, hex);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(tag.c_str(), hex, msg);

        // and end to end through rmCheck on a fresh state
        RmState s;
        rmStateInit(s, 0);
        const RmVerdict v = rmCheck(s, c, dst.c_str(), src.c_str(), passwd.c_str(), 22, 1000000);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.ctr == 0 ? "sync" : "ok", rmVerdictName(v), msg);
    }
    printf("[vectors] commands reproduced: %u/%u\n", (unsigned)n, (unsigned)n);
}

static void test_vectors_replies_reproduced(void)
{
    std::string json;
    if (!read_repo_file("tools/tests/remote_cmd_vectors.json", json))
        TEST_FAIL_MESSAGE("tools/tests/remote_cmd_vectors.json not found");

    static std::string objs[32];
    const size_t n = objectsOf(json, "replies", objs, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, n, "reply vector count drifted from tools/remote_cmd.py generate_vectors()");

    for (size_t i = 0; i < n; i++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "reply vector %u", (unsigned)i);

        std::string passwd, dst, src, result, canonical, rtag, text;
        uint32_t ctr = 0;
        TEST_ASSERT_TRUE_MESSAGE(jsonString(objs[i], "passwd", passwd) && jsonString(objs[i], "dst", dst) &&
                                     jsonString(objs[i], "src", src) && jsonString(objs[i], "result", result) &&
                                     jsonString(objs[i], "reply_canonical", canonical) &&
                                     jsonString(objs[i], "reply_tag", rtag) &&
                                     jsonString(objs[i], "reply_text", text) && jsonUint(objs[i], "ctr", ctr),
                                 msg);

        RmCmd c;
        memset(&c, 0, sizeof(c));
        c.ctr = ctr;
        char out[160];
        const size_t len = rmReply(c, result.c_str(), dst.c_str(), src.c_str(), passwd.c_str(), out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT_MESSAGE(text.size(), len, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text.c_str(), out, msg);
        TEST_ASSERT_TRUE_MESSAGE(text.size() >= 17 && strcmp(text.c_str() + text.size() - 16, rtag.c_str()) == 0, msg);

        // tag over the stated canonical string
        uint8_t key[32], mac[32];
        char hex[65];
        rmDeriveKey(passwd.c_str(), key);
        hmacSha256(key, 32, reinterpret_cast<const uint8_t *>(canonical.c_str()), canonical.size(), mac);
        hexLower(mac, 8, hex);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(rtag.c_str(), hex, msg);
    }
    printf("[vectors] replies reproduced: %u/%u\n", (unsigned)n, (unsigned)n);
}

// ---- helpers --------------------------------------------------------------

static const char *DST = "DK5EN-90";
static const char *SRC = "DK5EN-1";
static const char *PW = "secret";
static const int MAXTX = 22;

// builds a validly tagged command (RmCmd directly, no text round trip)
static RmCmd mk(uint32_t ctr, const char *cmd, const char *args = "", const char *dst = DST, const char *src = SRC,
                const char *pw = PW)
{
    RmCmd c;
    memset(&c, 0, sizeof(c));
    c.ctr = ctr;
    snprintf(c.cmd, sizeof(c.cmd), "%s", cmd);
    snprintf(c.args, sizeof(c.args), "%s", args);
    char canon[160];
    rmCanonical(c, dst, src, canon, sizeof(canon));
    uint8_t key[32], mac[32];
    rmDeriveKey(pw, key);
    hmacSha256(key, 32, reinterpret_cast<const uint8_t *>(canon), strlen(canon), mac);
    hexLower(mac, 8, c.tag);
    return c;
}

static RmCmd withBadTag(RmCmd c)
{
    c.tag[0] = (c.tag[0] == '0') ? '1' : '0';
    return c;
}

static RmVerdict chk(RmState &s, const RmCmd &c, uint32_t now)
{
    return rmCheck(s, c, DST, SRC, PW, MAXTX, now);
}

#define ASSERT_VERDICT(want, got) TEST_ASSERT_EQUAL_STRING(rmVerdictName(want), rmVerdictName(got))

// ---- parse ----------------------------------------------------------------

static void test_parse_accepts_wellformed(void)
{
    RmCmd c;
    TEST_ASSERT_TRUE(rmParse("RM1 42 reboot 3f9ac2e17b0d5e44", c));
    TEST_ASSERT_EQUAL_UINT32(42, c.ctr);
    TEST_ASSERT_EQUAL_STRING("reboot", c.cmd);
    TEST_ASSERT_EQUAL_STRING("", c.args);
    TEST_ASSERT_EQUAL_STRING("3f9ac2e17b0d5e44", c.tag);

    TEST_ASSERT_TRUE(rmParse("RM1 43 setout a2 on 0123456789abcdef", c));
    TEST_ASSERT_EQUAL_STRING("setout", c.cmd);
    TEST_ASSERT_EQUAL_STRING("a2 on", c.args);

    TEST_ASSERT_TRUE(rmParse("RM1 4294967295 status 0123456789abcdef", c));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, c.ctr);

    TEST_ASSERT_TRUE(rmParse("RM1 0 sync 0123456789abcdef", c));
    TEST_ASSERT_EQUAL_UINT32(0, c.ctr);
}

static void test_parse_rejects_malformed(void)
{
    RmCmd c;
    const char *bad[] = {
        "",
        "RM1",
        "RM1 ",
        "rm1 1 reboot 3f9ac2e17b0d5e44",  // prefix case
        "RM2 1 reboot 3f9ac2e17b0d5e44",  // version
        "RM1  1 reboot 3f9ac2e17b0d5e44", // double space
        "RM1 1  reboot 3f9ac2e17b0d5e44",
        "RM1 1 reboot  3f9ac2e17b0d5e44",
        "RM1 1 gps  on 3f9ac2e17b0d5e44",
        "RM1 1 reboot 3f9ac2e17b0d5e44 ",         // trailing space
        " RM1 1 reboot 3f9ac2e17b0d5e44",         // leading space
        "RM1 1 reboot 3f9ac2e17b0d5e4",           // tag 15
        "RM1 1 reboot 3f9ac2e17b0d5e444",         // tag 17
        "RM1 1 reboot 3F9AC2E17B0D5E44",          // upper-case tag
        "RM1 1 reboot 3f9ac2e17b0d5e4g",          // non-hex
        "RM1 1 REBOOT 3f9ac2e17b0d5e44",          // upper-case cmd
        "RM1 1 gps ON 3f9ac2e17b0d5e44",          // upper-case args
        "RM1 0 reboot 3f9ac2e17b0d5e44",          // ctr 0 only with sync
        "RM1 01 reboot 3f9ac2e17b0d5e44",         // leading zero
        "RM1 00 sync 3f9ac2e17b0d5e44",
        "RM1 4294967296 reboot 3f9ac2e17b0d5e44", // ctr overflow
        "RM1 99999999999 reboot 3f9ac2e17b0d5e44",
        "RM1 -1 reboot 3f9ac2e17b0d5e44",
        "RM1 x reboot 3f9ac2e17b0d5e44",
        "RM1 1 3f9ac2e17b0d5e44",                              // no command
        "RM1 1 abcdefghijklmnop 3f9ac2e17b0d5e44",             // cmd 16 chars
        "RM1 1 gps abcdefghijklmnopqrstuvwx 3f9ac2e17b0d5e44", // args 24 chars
        "RM1 1 reboot\t3f9ac2e17b0d5e44",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "should not parse: [%s]", bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(rmParse(bad[i], c), msg);
    }
    TEST_ASSERT_FALSE(rmParse(nullptr, c));
}

// ---- key and tag ----------------------------------------------------------

static void test_key_strips_trailing_spaces_only(void)
{
    uint8_t a[32], b[32], z[32], lead[32];
    rmDeriveKey("secret", a);
    rmDeriveKey("secret      ", b);
    rmDeriveKey(" secret", lead);
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32);
    TEST_ASSERT_TRUE(memcmp(a, lead, 32) != 0);

    memset(z, 0, 32);
    rmDeriveKey("", a);
    TEST_ASSERT_EQUAL_MEMORY(z, a, 32);
    rmDeriveKey("     ", a);
    TEST_ASSERT_EQUAL_MEMORY(z, a, 32);
    rmDeriveKey(nullptr, a);
    TEST_ASSERT_EQUAL_MEMORY(z, a, 32);
}

static void test_canonical_bounds(void)
{
    RmCmd c = mk(7, "gps", "on");
    char out[64];
    TEST_ASSERT_EQUAL_UINT(strlen("RM1|DK5EN-90|DK5EN-1|7|gps on"), rmCanonical(c, DST, SRC, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("RM1|DK5EN-90|DK5EN-1|7|gps on", out);
    TEST_ASSERT_EQUAL_UINT(0, rmCanonical(c, DST, SRC, out, 10));
    TEST_ASSERT_EQUAL_UINT(0, rmCanonical(c, nullptr, SRC, out, sizeof(out)));
}

static void test_valid_tag_accepted_and_wrong_tag_rejected(void)
{
    RmState s;
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_OK, chk(s, mk(1, "status"), 1000));

    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(1, "status")), 1000));

    // wrong password, wrong source, wrong ctr
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, rmCheck(s, mk(1, "status"), DST, SRC, "other", MAXTX, 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, rmCheck(s, mk(1, "status"), DST, "DK5EN-14", PW, MAXTX, 1000));
    rmStateInit(s, 0);
    RmCmd d = mk(1, "status");
    d.ctr = 2;
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, d, 1000));
}

static void test_tag_over_different_dst_rejected(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(1, "reboot", "", "DK5EN-91"); // sibling node sharing the password
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, c, 1000));
}

// ---- allowlist ------------------------------------------------------------

struct CmdArgs
{
    const char *cmd;
    const char *args;
};

static void test_allowlist_accepts_every_command(void)
{
    const CmdArgs ok[] = {{"reboot", ""},   {"status", ""},    {"sendpos", ""},    {"sendtrack", ""},
                          {"gps", "on"},    {"gps", "off"},    {"track", "on"},    {"track", "off"},
                          {"display", "on"}, {"display", "off"}, {"gateway", "on"}, {"gateway", "off"},
                          {"mesh", "on"},   {"mesh", "off"},   {"txpower", "0"},   {"txpower", "22"},
                          {"setout", "a0 on"}, {"setout", "b7 off"}, {"setout", "a7 off"}, {"setout", "b0 on"}};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
    {
        RmState s;
        rmStateInit(s, 0);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s %s", ok[i].cmd, ok[i].args);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", rmVerdictName(chk(s, mk(1, ok[i].cmd, ok[i].args), 1000)), msg);
    }
}

static void test_blocked_commands_rejected_even_with_valid_tag(void)
{
    const char *blocked[] = {"cleanflash", "ota-update", "dfu",     "deepsleep", "setcall", "passwd", "webpwd",
                             "btcode",     "setssid",    "setpwd",  "wifiset",   "updrepo", "updchan", "autoupdate",
                             "rm",         "stor",       "unknown", "sync2",     "Reboot",  "rebootx"};
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
    {
        RmState s;
        rmStateInit(s, 0);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("blocked", rmVerdictName(chk(s, mk(1, blocked[i]), 1000)), blocked[i]);
        rmStateInit(s, 0);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("blocked", rmVerdictName(chk(s, mk(1, blocked[i], "x"), 1000)), blocked[i]);
    }
}

static void test_bad_args_rejected(void)
{
    const CmdArgs bad[] = {{"reboot", "now"}, {"status", "x"},   {"gps", ""},        {"gps", "ONN"},
                           {"gps", "on off"}, {"gps", "1"},      {"mesh", "on "},    {"txpower", ""},
                           {"txpower", "-1"}, {"txpower", "2x"}, {"txpower", "02"},  {"txpower", "+2"},
                           {"txpower", "2 3"}, {"setout", ""},   {"setout", "1"},    {"setout", "2 1"},
                           {"setout", "1 0"}, {"setout", "8 1"}, {"setout", "a8 on"}, {"setout", "c0 on"},
                           {"setout", "2"}, {"setout", "a2 on1"}, {"setout", "A2 on"}, {"setout", "a2 ON"},
                           {"setout", "a2 1"}, {"setout", "a2"}, {"setout", "a2  on"}, {"setout", "a on"},
                           {"setout", "a12 on"}, {"setout", "a2 onn"}, {"sync", "x"}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        RmState s;
        rmStateInit(s, 0);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s [%s]", bad[i].cmd, bad[i].args);
        const bool isSync = strcmp(bad[i].cmd, "sync") == 0;
        const RmVerdict v = chk(s, mk(isSync ? 0 : 1, bad[i].cmd, bad[i].args), 1000);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("blocked", rmVerdictName(v), msg);
    }
}

static void test_txpower_above_max_rejected(void)
{
    RmState s;
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_OK, rmCheck(s, mk(1, "txpower", "22"), DST, SRC, PW, 22, 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, rmCheck(s, mk(1, "txpower", "23"), DST, SRC, PW, 22, 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, rmCheck(s, mk(1, "txpower", "22"), DST, SRC, PW, 20, 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_OK, rmCheck(s, mk(1, "txpower", "20"), DST, SRC, PW, 20, 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, rmCheck(s, mk(1, "txpower", "0"), DST, SRC, PW, -1, 1000));
}

static void test_chained_and_forbidden_payload_rejected(void)
{
    // "--a --b" parses (syntax is fine) and is then blocked by rmCheck
    RmCmd c;
    TEST_ASSERT_TRUE(rmParse("RM1 5 reboot --a --b 0123456789abcdef", c));
    TEST_ASSERT_EQUAL_STRING("--a --b", c.args);

    const CmdArgs payloads[] = {{"reboot", "--a --b"}, {"reboot", "--x"},  {"gps", "on--x"},
                                {"gps", "on;x"},       {"gps", "on{"},     {"gps", "o%6e"},
                                {"status", ";"},       {"--reboot", ""},   {"reboot--", ""},
                                {"{reboot", ""},       {"txpower", "2{3"}, {"setout", "a1 on;"}};
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++)
    {
        RmState s;
        rmStateInit(s, 0);
        char msg[64];
        snprintf(msg, sizeof(msg), "[%s] [%s]", payloads[i].cmd, payloads[i].args);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("blocked", rmVerdictName(chk(s, mk(1, payloads[i].cmd, payloads[i].args), 1000)),
                                         msg);
    }
}

// ---- counter, cache, sync -------------------------------------------------

static void test_replayed_counter_rejected(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(5, "reboot");
    ASSERT_VERDICT(RM_OK, chk(s, c, 1000));
    TEST_ASSERT_EQUAL_UINT32(5, rmAccept(s, c, "ok rebooting", 1000));

    // an older counter with a valid tag: replay (not cached, other ctr)
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(4, "status"), 200000));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(1, "status"), 400000));
    // next counter works
    ASSERT_VERDICT(RM_OK, chk(s, mk(6, "status"), 600000));
    // a counter gap is fine and moves the mark
    RmCmd g = mk(100, "status");
    ASSERT_VERDICT(RM_OK, chk(s, g, 1000000));
    TEST_ASSERT_EQUAL_UINT32(100, rmAccept(s, g, "ok", 1000000));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(99, "status"), 1100000));
}

static void test_persisted_hwm_is_the_floor(void)
{
    RmState s;
    rmStateInit(s, 42);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(42, "status"), 1000));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(1, "status"), 100000));
    ASSERT_VERDICT(RM_OK, chk(s, mk(43, "status"), 200000));
}

static void test_same_ctr_and_tag_within_10_min_is_cached(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(7, "reboot");
    ASSERT_VERDICT(RM_OK, chk(s, c, 5000));
    rmAccept(s, c, "ok rebooting", 5000);

    // immediately and repeatedly: cached, not rate limited, no lockout drift
    for (int i = 0; i < 10; i++)
        ASSERT_VERDICT(RM_CACHED, chk(s, c, 5000 + (uint32_t)i * 1000));
    ASSERT_VERDICT(RM_CACHED, chk(s, c, 5000 + RM_CACHE_MS));
    TEST_ASSERT_EQUAL_UINT8(0, s.rejCount);
    TEST_ASSERT_EQUAL_UINT32(7, s.hwm);

    // the cached result re-sends the identical reply
    char a[160], b[160];
    const size_t la = rmReply(c, "ok rebooting", DST, SRC, PW, a, sizeof(a));
    const size_t lb = rmReply(c, s.lastReply, DST, SRC, PW, b, sizeof(b));
    TEST_ASSERT_TRUE(la > 0);
    TEST_ASSERT_EQUAL_UINT(la, lb);
    TEST_ASSERT_EQUAL_STRING(a, b);
}

static void test_same_ctr_after_10_min_is_replay(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(7, "reboot");
    ASSERT_VERDICT(RM_OK, chk(s, c, 5000));
    rmAccept(s, c, "ok rebooting", 5000);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, c, 5000 + RM_CACHE_MS + 1));
}

static void test_same_ctr_with_other_tag_is_not_cached(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(7, "reboot");
    rmAccept(s, c, "ok rebooting", 5000);
    // different command under the same ctr (valid tag for that command)
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(7, "status"), 6000));
    // forged tag under the same ctr
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(c), 7000));
}

static void test_ctr_zero_only_with_sync(void)
{
    RmState s;
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "reboot"), 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "status"), 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(5, "sync"), 1000));
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 1000));
}

static void test_sync_does_not_move_hwm_and_is_rate_limited(void)
{
    RmState s;
    rmStateInit(s, 42);
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 1000));
    TEST_ASSERT_EQUAL_UINT32(42, s.hwm);
    // a second sync inside 10 s is rate limited, after 10 s fine
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(0, "sync"), 6000));
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 11000));
    // a command right after a sync is rate limited too
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(43, "status"), 12000));
    ASSERT_VERDICT(RM_OK, chk(s, mk(43, "status"), 21000));
    // sync with a wrong tag is rejected
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(0, "sync")), 100000));
}

static void test_sync_leaves_cache_window_alone(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(7, "reboot");
    rmAccept(s, c, "ok rebooting", 1000);
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 500000));
    // the window still ends 10 min after the acceptance, not after the sync
    ASSERT_VERDICT(RM_CACHED, chk(s, c, 1000 + RM_CACHE_MS));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, c, 1000 + RM_CACHE_MS + 1));
}

// ---- rate limit and lockout -----------------------------------------------

static void test_rate_limit_one_per_10_seconds(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd a = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, a, 100000));
    rmAccept(s, a, "ok", 100000);

    RmCmd b = mk(2, "status");
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, b, 105000));
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, b, 109999));
    // a rate reject does not burn the counter
    ASSERT_VERDICT(RM_OK, chk(s, b, 110000));
    TEST_ASSERT_EQUAL_UINT32(1, s.hwm);
}

static void test_three_rejects_lock_out_for_5_minutes_then_recover(void)
{
    RmState s;
    rmStateInit(s, 0);
    const RmCmd bad = withBadTag(mk(1, "status"));

    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 10000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 20000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 30000)); // third reject arms the lockout
    TEST_ASSERT_TRUE(s.lockActive);

    RmCmd good = mk(1, "status");
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, 31000));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, 30000 + RM_LOCKOUT_MS - 1));
    ASSERT_VERDICT(RM_OK, chk(s, good, 30000 + RM_LOCKOUT_MS));
    TEST_ASSERT_FALSE(s.lockActive);
}

static void test_lockout_counts_any_reject_reason(void)
{
    RmState s;
    rmStateInit(s, 9);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(1, "status"), 1000));
    ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(10, "dfu"), 2000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, mk(10, "status", "", "OTHER"), 3000));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, mk(10, "status"), 4000));

    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(1, "cleanflash"), 1000));
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "reboot"), 2000));
    RmCmd a = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, a, 3000)); // two rejects only so far
    rmAccept(s, a, "ok", 3000);
    // advisor N1: a rate reject needs a valid tag and does not count towards
    // the lockout (a SysOp sending sync + command inside 10 s must not lock
    // themselves out)
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(2, "status"), 4000));
    TEST_ASSERT_FALSE(s.lockActive);
    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), 20000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(3, "status")), 21000)); // third counted reject
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, mk(3, "status"), 22000));
}

static void test_reject_window_expires_after_90_seconds(void)
{
    RmState s;
    rmStateInit(s, 0);
    const RmCmd bad = withBadTag(mk(1, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 10000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 20000));
    // window started at 10000; 100 s later the count starts over
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 110001));
    TEST_ASSERT_FALSE(s.lockActive);
    ASSERT_VERDICT(RM_OK, chk(s, mk(1, "status"), 111000));
}

static void test_successful_commands_do_not_clear_the_reject_count(void)
{
    RmState s;
    rmStateInit(s, 0);
    const RmCmd bad = withBadTag(mk(2, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 1000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 2000));
    RmCmd good = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, good, 3000));
    rmAccept(s, good, "ok", 3000);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 4000));
    TEST_ASSERT_TRUE(s.lockActive);
}

// ---- disabled -------------------------------------------------------------

static void test_empty_password_disables_rm(void)
{
    RmState s;
    rmStateInit(s, 0);
    // a command "validly" tagged with the all-zero key must not pass either
    RmCmd c = mk(1, "status", "", DST, SRC, "");
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "", MAXTX, 1000));
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "              ", MAXTX, 1000));
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, nullptr, MAXTX, 1000));
    // disabled is not a lockout trigger
    for (int i = 0; i < 10; i++)
        ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "", MAXTX, 1000 + (uint32_t)i));
    TEST_ASSERT_FALSE(s.lockActive);
    char out[64];
    TEST_ASSERT_EQUAL_UINT(0, rmReply(c, "ok", DST, SRC, "", out, sizeof(out)));
}

// ---- reply ----------------------------------------------------------------

static void test_reply_shape_and_limits(void)
{
    RmCmd c = mk(42, "reboot");
    char out[160];
    const size_t n = rmReply(c, "ok rebooting", DST, SRC, PW, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(strlen(out), n);
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, "RM1 42 ok rebooting ", 20));
    TEST_ASSERT_EQUAL_UINT(20 + 16, n);

    TEST_ASSERT_TRUE(rmReply(c, "err range", DST, SRC, PW, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, "RM1 42 err range ", 17));

    TEST_ASSERT_EQUAL_UINT(0, rmReply(c, "ok rebooting", DST, SRC, PW, out, 20)); // buffer too small
    TEST_ASSERT_EQUAL_UINT(0, rmReply(c, "ok rebooting", nullptr, SRC, PW, out, sizeof(out)));

    char longResult[100];
    memset(longResult, 'a', sizeof(longResult));
    longResult[sizeof(longResult) - 1] = '\0';
    TEST_ASSERT_EQUAL_UINT(0, rmReply(c, longResult, DST, SRC, PW, out, sizeof(out)));
    longResult[RM_MAX_RESULT] = '\0';
    TEST_ASSERT_TRUE(rmReply(c, longResult, DST, SRC, PW, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(strlen(out) <= 160);

    // reply tag binds dst, src, ctr and result
    char a[160], b[160];
    rmReply(c, "ok rebooting", DST, SRC, PW, a, sizeof(a));
    rmReply(c, "ok rebooting", "DK5EN-91", SRC, PW, b, sizeof(b));
    TEST_ASSERT_TRUE(strcmp(a + strlen(a) - 16, b + strlen(b) - 16) != 0);
    rmReply(c, "ok rebooting", DST, "DK5EN-14", PW, b, sizeof(b));
    TEST_ASSERT_TRUE(strcmp(a + strlen(a) - 16, b + strlen(b) - 16) != 0);
    rmReply(c, "ok reboot", DST, SRC, PW, b, sizeof(b));
    TEST_ASSERT_TRUE(strcmp(a + strlen(a) - 16, b + strlen(b) - 16) != 0);
}

static void test_accept_stores_result_and_truncates(void)
{
    RmState s;
    rmStateInit(s, 0);
    RmCmd c = mk(3, "status");
    char big[120];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_EQUAL_UINT32(3, rmAccept(s, c, big, 1000));
    TEST_ASSERT_EQUAL_UINT(RM_MAX_RESULT, strlen(s.lastReply));
    TEST_ASSERT_EQUAL_STRING(c.tag, s.lastTag);
    TEST_ASSERT_TRUE(s.haveLast);
    // accept never lowers the mark
    TEST_ASSERT_EQUAL_UINT32(3, rmAccept(s, mk(2, "status"), "ok", 2000));
}

static void test_verdict_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", rmVerdictName(RM_OK));
    TEST_ASSERT_EQUAL_STRING("cached", rmVerdictName(RM_CACHED));
    TEST_ASSERT_EQUAL_STRING("sync", rmVerdictName(RM_SYNC));
    TEST_ASSERT_EQUAL_STRING("format", rmVerdictName(RM_REJ_FORMAT));
    TEST_ASSERT_EQUAL_STRING("tag", rmVerdictName(RM_REJ_TAG));
    TEST_ASSERT_EQUAL_STRING("replay", rmVerdictName(RM_REJ_REPLAY));
    TEST_ASSERT_EQUAL_STRING("blocked", rmVerdictName(RM_REJ_BLOCKED));
    TEST_ASSERT_EQUAL_STRING("rate", rmVerdictName(RM_REJ_RATE));
    TEST_ASSERT_EQUAL_STRING("lockout", rmVerdictName(RM_REJ_LOCKOUT));
    TEST_ASSERT_EQUAL_STRING("disabled", rmVerdictName(RM_REJ_DISABLED));
}

// ---- millis() wrap --------------------------------------------------------

static void test_millis_wrap_rate_cache_and_lockout(void)
{
    const uint32_t t0 = 0xFFFFFFF0u; // 16 ms before the wrap

    // rate limit and cache window across the wrap
    RmState s;
    rmStateInit(s, 0);
    RmCmd a = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, a, t0));
    rmAccept(s, a, "ok", t0);
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(2, "status"), t0 + 5000)); // wrapped: 4984
    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), t0 + RM_RATE_MS)); // exactly 10 s
    ASSERT_VERDICT(RM_CACHED, chk(s, a, t0 + 20000));
    ASSERT_VERDICT(RM_CACHED, chk(s, a, t0 + RM_CACHE_MS));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, a, t0 + RM_CACHE_MS + 1));

    // lockout armed just before the wrap, ends after it
    rmStateInit(s, 0);
    const RmCmd bad = withBadTag(mk(1, "status"));
    const uint32_t r0 = 0xFFFFFF00u;
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, r0));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, r0 + 100));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, r0 + 200));
    RmCmd good = mk(1, "status");
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, r0 + 250));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, r0 + 200 + RM_LOCKOUT_MS - 1)); // past the wrap
    ASSERT_VERDICT(RM_OK, chk(s, good, r0 + 200 + RM_LOCKOUT_MS));

    // reject window across the wrap: 3 rejects 40 s apart straddling it
    rmStateInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u + 40000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u + 80000));
    TEST_ASSERT_TRUE(s.lockActive);
}

// Bench 2026-10-05: the SysOp node parsed RM1 replies as commands, hid them and
// locked itself out. Replies must be recognised and never treated as commands.
static void test_reply_is_recognised_and_never_parses_as_a_command(void)
{
    TEST_ASSERT_TRUE(rmIsReply("RM1 4 ok v=4.40a up=0 bat=100 heap=126 gw=0 mesh=1 ca54fd46ae80638c"));
    TEST_ASSERT_TRUE(rmIsReply("RM1 43 err range 0123456789abcdef"));
    TEST_ASSERT_TRUE(rmIsReply("RM1 0 ok ctr=4 v=4.40a 2488cf9479d2490d"));
    TEST_ASSERT_FALSE(rmIsReply("RM1 4 status 4939f03c4188e4e3"));
    TEST_ASSERT_FALSE(rmIsReply("RM1 0 sync ca30595c2d6dbbd6"));
    TEST_ASSERT_FALSE(rmIsReply("RM1 x ok a"));
    TEST_ASSERT_FALSE(rmIsReply("RM1  ok a"));
    TEST_ASSERT_FALSE(rmIsReply("RM2 4 ok a"));
    TEST_ASSERT_FALSE(rmIsReply("RM1 4 okay 0123456789abcdef"));
    TEST_ASSERT_FALSE(rmIsReply(nullptr));
    RmCmd c;
    // a reply never yields an executable command
    if (rmParse("RM1 4 ok v=4.40a 0123456789abcdef", c))
    {
        RmState s;
        rmStateInit(s, 0);
        TEST_ASSERT_NOT_EQUAL(RM_OK, rmCheck(s, c, "DK5EN-90", "DK5EN-1", "secret", 22, 1000));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_commands_reproduced);
    RUN_TEST(test_vectors_replies_reproduced);
    RUN_TEST(test_parse_accepts_wellformed);
    RUN_TEST(test_parse_rejects_malformed);
    RUN_TEST(test_key_strips_trailing_spaces_only);
    RUN_TEST(test_canonical_bounds);
    RUN_TEST(test_valid_tag_accepted_and_wrong_tag_rejected);
    RUN_TEST(test_tag_over_different_dst_rejected);
    RUN_TEST(test_allowlist_accepts_every_command);
    RUN_TEST(test_blocked_commands_rejected_even_with_valid_tag);
    RUN_TEST(test_bad_args_rejected);
    RUN_TEST(test_txpower_above_max_rejected);
    RUN_TEST(test_chained_and_forbidden_payload_rejected);
    RUN_TEST(test_replayed_counter_rejected);
    RUN_TEST(test_persisted_hwm_is_the_floor);
    RUN_TEST(test_same_ctr_and_tag_within_10_min_is_cached);
    RUN_TEST(test_same_ctr_after_10_min_is_replay);
    RUN_TEST(test_same_ctr_with_other_tag_is_not_cached);
    RUN_TEST(test_ctr_zero_only_with_sync);
    RUN_TEST(test_sync_does_not_move_hwm_and_is_rate_limited);
    RUN_TEST(test_sync_leaves_cache_window_alone);
    RUN_TEST(test_rate_limit_one_per_10_seconds);
    RUN_TEST(test_three_rejects_lock_out_for_5_minutes_then_recover);
    RUN_TEST(test_lockout_counts_any_reject_reason);
    RUN_TEST(test_reject_window_expires_after_90_seconds);
    RUN_TEST(test_successful_commands_do_not_clear_the_reject_count);
    RUN_TEST(test_empty_password_disables_rm);
    RUN_TEST(test_reply_shape_and_limits);
    RUN_TEST(test_accept_stores_result_and_truncates);
    RUN_TEST(test_verdict_names);
    RUN_TEST(test_reply_is_recognised_and_never_parses_as_a_command);
    RUN_TEST(test_millis_wrap_rate_cache_and_lockout);
    return UNITY_END();
}
