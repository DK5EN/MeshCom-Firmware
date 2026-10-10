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
#include <rm_commands.h>
#include <rm_queue.h>
#include <rm_rx_gate.h>
#include <rm_sender_policy.h>

void setUp(void) {}
void tearDown(void) {}

// BF-01: rmStateInit() leaves RmState::strict at 0 (strict security OFF, the default). Every test written
// against the lockout / rate limiter behaviour starts from this helper, which switches strict ON; the
// flag-OFF twins further down call rmStateInit() directly.
static void strictInit(RmState &s, uint32_t hwm)
{
    rmStateInit(s, hwm);
    s.strict = true;
}

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
    TEST_ASSERT_EQUAL_INT_MESSAGE(32, n, "command vector count drifted from tools/remote_cmd.py generate_vectors()");

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
        strictInit(s, 0);
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

    static std::string objs[64];
    const size_t n = objectsOf(json, "replies", objs, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(25, n, "reply vector count drifted from tools/remote_cmd.py generate_vectors()");

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

// The sender's entry in the reject table (a zeroed one if the sender has none). Read-only.
static const RmRejSrc &ent(const RmState &s, const char *call = SRC)
{
    static const RmRejSrc none = {};
    for (const RmRejSrc &r : s.rej)
        if (r.used && strncmp(r.call, call, sizeof(r.call) - 1) == 0)
            return r;
    return none;
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
        "RM1 1 Gps on 3f9ac2e17b0d5e44",          // mixed-case cmd (capitals in args parse since W2-A; the shape refuses "gps ON", see test_ext_args_survive_parse_build_and_check)
        "RM1 0 reboot 3f9ac2e17b0d5e44",          // ctr 0 only with sync
        "RM1 01 reboot 3f9ac2e17b0d5e44",         // leading zero
        "RM1 00 sync 3f9ac2e17b0d5e44",
        "RM1 4294967296 reboot 3f9ac2e17b0d5e44", // ctr overflow
        "RM1 99999999999 reboot 3f9ac2e17b0d5e44",
        "RM1 -1 reboot 3f9ac2e17b0d5e44",
        "RM1 x reboot 3f9ac2e17b0d5e44",
        "RM1 1 3f9ac2e17b0d5e44",                              // no command
        "RM1 1 abcdefghijklmnop 3f9ac2e17b0d5e44",             // cmd 16 chars
        "RM1 1 gps abcdefghijklmnopqrstuvwxyz0123456789abcd 3f9ac2e17b0d5e44", // args 40 chars (RM_MAX_ARGS + 1)
        "RM1 1 reboot\t3f9ac2e17b0d5e44",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "should not parse: [%s]", bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(rmParse(bad[i], c), msg);
    }
    TEST_ASSERT_FALSE(rmParse(nullptr, c));
    // the boundary on the accepted side: RM_MAX_ARGS (39) characters parse
    TEST_ASSERT_TRUE(rmParse("RM1 1 gps abcdefghijklmnopqrstuvwxyz0123456789abc 3f9ac2e17b0d5e44", c));
    TEST_ASSERT_EQUAL_UINT(RM_MAX_ARGS, strlen(c.args));
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
    strictInit(s, 0);
    ASSERT_VERDICT(RM_OK, chk(s, mk(1, "status"), 1000));

    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(1, "status")), 1000));

    // wrong password, wrong source, wrong ctr
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, rmCheck(s, mk(1, "status"), DST, SRC, "other", MAXTX, 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, rmCheck(s, mk(1, "status"), DST, "DK5EN-14", PW, MAXTX, 1000));
    strictInit(s, 0);
    RmCmd d = mk(1, "status");
    d.ctr = 2;
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, d, 1000));
}

static void test_tag_over_different_dst_rejected(void)
{
    RmState s;
    strictInit(s, 0);
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
                          {"setout", "a0 on"}, {"setout", "b7 off"}, {"setout", "a7 off"}, {"setout", "b0 on"},
                          {"led", "on"},    {"led", "off"}};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
    {
        RmState s;
        strictInit(s, 0);
        char msg[64];
        snprintf(msg, sizeof(msg), "%s %s", ok[i].cmd, ok[i].args);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", rmVerdictName(chk(s, mk(1, ok[i].cmd, ok[i].args), 1000)), msg);
    }
}

static void test_blocked_commands_rejected_even_with_valid_tag(void)
{
    const char *blocked[] = {"cleanflash", "ota-update", "dfu",     "deepsleep", "setcall", "passwd", "webpwd",
                             "btcode",     "setssid",    "setpwd",  "wifiset",   "updrepo", "updchan", "autoupdate",
                             "rm",         "remotemgmt", "stor",       "unknown", "sync2",     "Reboot",  "rebootx"};
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
    {
        RmState s;
        strictInit(s, 0);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("blocked", rmVerdictName(chk(s, mk(1, blocked[i]), 1000)), blocked[i]);
        strictInit(s, 0);
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
                           {"setout", "a12 on"}, {"setout", "a2 onn"}, {"sync", "x"},
                           {"led", ""},       {"led", "blink"}, {"led", "1"},       {"led", "ON"},
                           {"led", "on off"}, {"led", " on"}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        RmState s;
        strictInit(s, 0);
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
    strictInit(s, 0);
    ASSERT_VERDICT(RM_OK, rmCheck(s, mk(1, "txpower", "22"), DST, SRC, PW, 22, 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, rmCheck(s, mk(1, "txpower", "23"), DST, SRC, PW, 22, 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, rmCheck(s, mk(1, "txpower", "22"), DST, SRC, PW, 20, 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_OK, rmCheck(s, mk(1, "txpower", "20"), DST, SRC, PW, 20, 1000));
    strictInit(s, 0);
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
        strictInit(s, 0);
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
    strictInit(s, 0);
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
    strictInit(s, 42);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(42, "status"), 1000));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(1, "status"), 100000));
    ASSERT_VERDICT(RM_OK, chk(s, mk(43, "status"), 200000));
}

static void test_same_ctr_and_tag_within_10_min_is_cached(void)
{
    RmState s;
    strictInit(s, 0);
    RmCmd c = mk(7, "reboot");
    ASSERT_VERDICT(RM_OK, chk(s, c, 5000));
    rmAccept(s, c, "ok rebooting", 5000);

    // immediately and repeatedly: cached, not rate limited, no lockout drift
    for (int i = 0; i < 10; i++)
        ASSERT_VERDICT(RM_CACHED, chk(s, c, 5000 + (uint32_t)i * 1000));
    ASSERT_VERDICT(RM_CACHED, chk(s, c, 5000 + RM_CACHE_MS));
    TEST_ASSERT_EQUAL_UINT8(0, ent(s).rejCount);
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
    strictInit(s, 0);
    RmCmd c = mk(7, "reboot");
    ASSERT_VERDICT(RM_OK, chk(s, c, 5000));
    rmAccept(s, c, "ok rebooting", 5000);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, c, 5000 + RM_CACHE_MS + 1));
}

static void test_same_ctr_with_other_tag_is_not_cached(void)
{
    RmState s;
    strictInit(s, 0);
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
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "reboot"), 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "status"), 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(5, "sync"), 1000));
    strictInit(s, 0);
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 1000));
}

static void test_sync_does_not_move_hwm_and_is_rate_limited(void)
{
    RmState s;
    strictInit(s, 42);
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 1000));
    TEST_ASSERT_EQUAL_UINT32(42, s.hwm);
    // a second sync inside 60 s is rate limited (own limiter), after 60 s fine
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(0, "sync"), 6000));
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(0, "sync"), 11000));
    // a sync never blocks a command: the command limiter is untouched by it
    ASSERT_VERDICT(RM_OK, chk(s, mk(43, "status"), 12000));
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 1000 + RM_SYNC_RATE_MS));
    // sync with a wrong tag is rejected
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(0, "sync")), 100000));
}

// Finding 2 (extended-commands-verdict): a captured sync frame replayed every few seconds used to
// stamp the command limiter on every acceptance, so every genuine command was rate-rejected.
static void test_sync_replay_does_not_block_commands(void)
{
    RmState s;
    strictInit(s, 0);
    const uint32_t t0 = 100000;
    const RmCmd sync = mk(0, "sync");
    ASSERT_VERDICT(RM_SYNC, chk(s, sync, t0));
    uint32_t ctr = 0;
    for (uint32_t dt = 1000; dt < RM_SYNC_RATE_MS; dt += 10000)
    {
        // identical replay of the captured frame: silent rate reject
        ASSERT_VERDICT(RM_REJ_RATE, chk(s, sync, t0 + dt));
        // the genuine command with a fresh counter at the same moment goes through
        const RmCmd c = mk(++ctr, "status");
        ASSERT_VERDICT(RM_OK, chk(s, c, t0 + dt));
        rmAccept(s, c, "ok", t0 + dt);
        TEST_ASSERT_EQUAL_UINT8(0, ent(s).rejCount);
        TEST_ASSERT_FALSE(ent(s).lockActive);
    }
    TEST_ASSERT_EQUAL_UINT32(6, ctr);
    // once the 60 s are over the same frame verifies again, still without moving hwm
    ASSERT_VERDICT(RM_SYNC, chk(s, sync, t0 + RM_SYNC_RATE_MS));
    TEST_ASSERT_EQUAL_UINT32(ctr, s.hwm);
}

static void test_sync_limiter_60s(void)
{
    RmState s;
    strictInit(s, 0);
    const uint32_t t0 = 5000;
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), t0));
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(0, "sync"), t0 + RM_SYNC_RATE_MS - 1));
    // the rate reject does not restart the window
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), t0 + RM_SYNC_RATE_MS));
    TEST_ASSERT_EQUAL_UINT8(0, ent(s).rejCount);
    TEST_ASSERT_FALSE(ent(s).lockActive);

    // a sync at nowMs == 0 counts as accepted (haveSync, not a zero timestamp)
    RmState z;
    strictInit(z, 0);
    ASSERT_VERDICT(RM_SYNC, chk(z, mk(0, "sync"), 0));
    ASSERT_VERDICT(RM_REJ_RATE, chk(z, mk(0, "sync"), RM_SYNC_RATE_MS - 1));
    ASSERT_VERDICT(RM_SYNC, chk(z, mk(0, "sync"), RM_SYNC_RATE_MS));

    // millis() wrap between the two syncs
    RmState w;
    strictInit(w, 0);
    const uint32_t tw = 0xFFFFFFFFu - 20000u;
    ASSERT_VERDICT(RM_SYNC, chk(w, mk(0, "sync"), tw));
    ASSERT_VERDICT(RM_REJ_RATE, chk(w, mk(0, "sync"), tw + 30000u));
    ASSERT_VERDICT(RM_REJ_RATE, chk(w, mk(0, "sync"), tw + (RM_SYNC_RATE_MS - 1)));
    ASSERT_VERDICT(RM_SYNC, chk(w, mk(0, "sync"), tw + RM_SYNC_RATE_MS));
}

static void test_command_does_not_block_sync(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd c = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, c, 100000));
    rmAccept(s, c, "ok", 100000);
    // right after an accepted command (inside the 10 s command spacing) a sync is still answered
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 100001));
    // and the sync did not open a command window either: the command limiter still runs from the command
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(2, "status"), 105000));
    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), 110000));
}

static void test_sync_leaves_cache_window_alone(void)
{
    RmState s;
    strictInit(s, 0);
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
    strictInit(s, 0);
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

// Operator unlock (own password change): drops the lockout and the reject count, nothing else.
static void test_receiver_unlock_clears_lock_only(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd first = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, first, 1000));
    rmAccept(s, first, "ok", 1000); // gives hwm / lastCtr / lastReply / rate state real values

    const RmCmd bad = withBadTag(mk(2, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 12000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 13000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 14000)); // third reject arms the lockout
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, mk(2, "status"), 15000));
    TEST_ASSERT_TRUE(ent(s).lockActive);

    // expected after-image: the before-image with only lockActive / rejCount cleared
    RmState expect;
    memcpy(&expect, &s, sizeof(s));
    memset(expect.rej, 0, sizeof(expect.rej));

    rmReceiverUnlock(s);
    TEST_ASSERT_FALSE(ent(s).lockActive);
    TEST_ASSERT_EQUAL_UINT8(0, ent(s).rejCount);
    TEST_ASSERT_EQUAL_UINT32(1, s.lastCtr);
    TEST_ASSERT_EQUAL_STRING("ok", s.lastReply);
    TEST_ASSERT_EQUAL_MEMORY(&expect, &s, sizeof(s)); // hwm, lastCtr, lastReply, rate and sync state untouched

    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), 16000)); // a valid command passes at once, no 5 min wait

    // the count restarted: two further wrong tags do not lock again
    const RmCmd bad3 = withBadTag(mk(3, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad3, 30000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad3, 31000));
    TEST_ASSERT_FALSE(ent(s).lockActive);
    ASSERT_VERDICT(RM_OK, chk(s, mk(3, "status"), 32000));
}

static void test_three_rejects_lock_out_for_5_minutes_then_recover(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd bad = withBadTag(mk(1, "status"));

    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 10000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 20000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 30000)); // third reject arms the lockout
    TEST_ASSERT_TRUE(ent(s).lockActive);

    RmCmd good = mk(1, "status");
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, 31000));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, good, 30000 + RM_LOCKOUT_MS - 1));
    ASSERT_VERDICT(RM_OK, chk(s, good, 30000 + RM_LOCKOUT_MS));
    TEST_ASSERT_FALSE(ent(s).lockActive);
}

static void test_authenticated_replay_is_not_counted(void)
{
    RmState s;
    strictInit(s, 9); // hwm 9: ctr <= 9 with a valid tag is a stale counter
    for (uint32_t i = 0; i < 3; i++)
    {
        ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(5, "status"), 1000 + i * 1000));
        TEST_ASSERT_EQUAL_UINT8(0, ent(s).rejCount);
        TEST_ASSERT_FALSE(ent(s).lockActive);
    }
    ASSERT_VERDICT(RM_OK, chk(s, mk(10, "status"), 5000));
}

static void test_reject_window_boundary(void)
{
    const uint32_t t0 = 100000;
    RmState a;
    strictInit(a, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(a, withBadTag(mk(1, "status")), t0));
    ASSERT_VERDICT(RM_REJ_TAG, chk(a, withBadTag(mk(1, "status")), t0 + 1));
    // exactly RM_REJ_WINDOW_MS after the window start: a new window, strike 1 of the new one
    ASSERT_VERDICT(RM_REJ_TAG, chk(a, withBadTag(mk(1, "status")), t0 + RM_REJ_WINDOW_MS));
    TEST_ASSERT_FALSE(ent(a).lockActive);
    RmState b;
    strictInit(b, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(b, withBadTag(mk(1, "status")), t0));
    ASSERT_VERDICT(RM_REJ_TAG, chk(b, withBadTag(mk(1, "status")), t0 + 1));
    ASSERT_VERDICT(RM_REJ_TAG, chk(b, withBadTag(mk(1, "status")), t0 + RM_REJ_WINDOW_MS - 1));
    TEST_ASSERT_TRUE(ent(b).lockActive);
}

static void test_lockout_counts_any_reject_reason(void)
{
    RmState s;
    strictInit(s, 9);
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "status"), 1000)); // replay no longer counts (valid tag), format does
    ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(10, "dfu"), 2000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, mk(10, "status", "", "OTHER"), 3000));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, mk(10, "status"), 4000));

    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(1, "cleanflash"), 1000));
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "reboot"), 2000));
    RmCmd a = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, a, 3000)); // two rejects only so far
    rmAccept(s, a, "ok", 3000);
    // advisor N1: a rate reject needs a valid tag and does not count towards
    // the lockout (a SysOp sending sync + command inside 10 s must not lock
    // themselves out)
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(2, "status"), 4000));
    TEST_ASSERT_FALSE(ent(s).lockActive);
    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), 20000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(3, "status")), 21000)); // third counted reject
    ASSERT_VERDICT(RM_REJ_LOCKOUT, chk(s, mk(3, "status"), 22000));
}

static void test_reject_window_expires_after_90_seconds(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd bad = withBadTag(mk(1, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 10000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 20000));
    // window started at 10000; 100 s later the count starts over
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 110001));
    TEST_ASSERT_FALSE(ent(s).lockActive);
    ASSERT_VERDICT(RM_OK, chk(s, mk(1, "status"), 111000));
}

static void test_successful_commands_do_not_clear_the_reject_count(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd bad = withBadTag(mk(2, "status"));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 1000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 2000));
    RmCmd good = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, good, 3000));
    rmAccept(s, good, "ok", 3000);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 4000));
    TEST_ASSERT_TRUE(ent(s).lockActive);
}

// ---- disabled -------------------------------------------------------------

static void test_empty_password_disables_rm(void)
{
    RmState s;
    strictInit(s, 0);
    // a command "validly" tagged with the all-zero key must not pass either
    RmCmd c = mk(1, "status", "", DST, SRC, "");
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "", MAXTX, 1000));
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "              ", MAXTX, 1000));
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, nullptr, MAXTX, 1000));
    // disabled is not a lockout trigger
    for (int i = 0; i < 10; i++)
        ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "", MAXTX, 1000 + (uint32_t)i));
    TEST_ASSERT_FALSE(ent(s).lockActive);
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

    char longResult[RM_MAX_RESULT + 2]; // one past the cap (was 100 when the cap was 63)
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

// DRY-06: the reply canonical is built in ONE place (rmReplyCanonical) for signer and verifier. These pin the
// exact bytes (tag computed independently: HMAC-SHA256 under sha256("secret"), first 8 bytes), so a refactor
// cannot change the on-air format.
static void test_reply_canonical_and_frame_are_pinned(void)
{
    char canon[200];
    const size_t cl = rmReplyCanonical(42, DST, SRC, "ok rebooting", canon, sizeof(canon));
    TEST_ASSERT_EQUAL_STRING("RM1R|DK5EN-90|DK5EN-1|42|ok rebooting", canon);
    TEST_ASSERT_EQUAL_UINT(strlen(canon), cl);
    TEST_ASSERT_EQUAL_UINT(0, rmReplyCanonical(42, DST, SRC, "ok rebooting", canon, 20)); // does not fit
    TEST_ASSERT_EQUAL_UINT(0, rmReplyCanonical(42, nullptr, SRC, "ok", canon, sizeof(canon)));

    RmCmd c = mk(42, "reboot");
    char out[160];
    rmReply(c, "ok rebooting", DST, SRC, PW, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("RM1 42 ok rebooting 52a704f71e8733f9", out);
}

static void test_reply_signed_verifies_and_any_byte_change_fails(void)
{
    RmCmd c = mk(42, "reboot");
    char frame[160];
    TEST_ASSERT_TRUE(rmReply(c, "ok rebooting", DST, SRC, PW, frame, sizeof(frame)) > 0);
    uint8_t key[32];
    rmDeriveKey(PW, key);
    char res[RM_MAX_RESULT + 1];
    TEST_ASSERT_TRUE(rmVerifyReply(frame, DST, SRC, 42, key, res, sizeof(res)));
    TEST_ASSERT_EQUAL_STRING("ok rebooting", res);

    // every byte of the frame flipped in turn: never verifies
    const size_t len = strlen(frame);
    for (size_t i = 0; i < len; i++)
    {
        char bad[160];
        memcpy(bad, frame, len + 1);
        bad[i] = (char)(bad[i] ^ 0x01);
        char r2[RM_MAX_RESULT + 1];
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(bad, DST, SRC, 42, key, r2, sizeof(r2)), bad);
    }
    // every signed field of the canonical changed on the verifier side
    TEST_ASSERT_FALSE(rmVerifyReply(frame, "DK5EN-91", SRC, 42, key, res, sizeof(res)));
    TEST_ASSERT_FALSE(rmVerifyReply(frame, DST, "DK5EN-2", 42, key, res, sizeof(res)));
    TEST_ASSERT_FALSE(rmVerifyReply(frame, DST, SRC, 43, key, res, sizeof(res)));
    uint8_t other[32];
    rmDeriveKey("secreT", other);
    TEST_ASSERT_FALSE(rmVerifyReply(frame, DST, SRC, 42, other, res, sizeof(res)));
}

static void test_accept_stores_result_and_truncates(void)
{
    RmState s;
    strictInit(s, 0);
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
    strictInit(s, 0);
    RmCmd a = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, a, t0));
    rmAccept(s, a, "ok", t0);
    ASSERT_VERDICT(RM_REJ_RATE, chk(s, mk(2, "status"), t0 + 5000)); // wrapped: 4984
    ASSERT_VERDICT(RM_OK, chk(s, mk(2, "status"), t0 + RM_RATE_MS)); // exactly 10 s
    ASSERT_VERDICT(RM_CACHED, chk(s, a, t0 + 20000));
    ASSERT_VERDICT(RM_CACHED, chk(s, a, t0 + RM_CACHE_MS));
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, a, t0 + RM_CACHE_MS + 1));

    // lockout armed just before the wrap, ends after it
    strictInit(s, 0);
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
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u + 40000));
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, bad, 0xFFFFD000u + 80000));
    TEST_ASSERT_TRUE(ent(s).lockActive);
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
        strictInit(s, 0);
        TEST_ASSERT_NOT_EQUAL(RM_OK, rmCheck(s, c, "DK5EN-90", "DK5EN-1", "secret", 22, 1000));
    }
}

// ---- RM-09: sender side (builder, reply verifier, allowlist predicate, reply queue) -------------

static void test_build_command_reproduces_every_vector(void)
{
    std::string json;
    if (!read_repo_file("tools/tests/remote_cmd_vectors.json", json))
        TEST_FAIL_MESSAGE("tools/tests/remote_cmd_vectors.json not found");

    static std::string objs[64];
    const size_t n = objectsOf(json, "commands", objs, 64);
    TEST_ASSERT_EQUAL_INT(32, n);

    for (size_t i = 0; i < n; i++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "build vector %u", (unsigned)i);

        std::string passwd, dst, src, cmd, args, dm;
        uint32_t ctr = 0;
        TEST_ASSERT_TRUE_MESSAGE(jsonString(objs[i], "passwd", passwd) && jsonString(objs[i], "dst", dst) &&
                                     jsonString(objs[i], "src", src) && jsonString(objs[i], "cmd", cmd) &&
                                     jsonString(objs[i], "args", args) && jsonString(objs[i], "dm_text", dm) &&
                                     jsonUint(objs[i], "ctr", ctr),
                                 msg);

        uint8_t key[32];
        rmDeriveKey(passwd.c_str(), key);
        char out[160];
        const size_t len = rmBuildCommand(dst.c_str(), src.c_str(), ctr, cmd.c_str(), args.c_str(), key, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT_MESSAGE(dm.size(), len, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(dm.c_str(), out, msg);

        // what the builder emits is what the receiver accepts
        RmCmd c;
        TEST_ASSERT_TRUE_MESSAGE(rmParse(out, c), msg);
        RmState st;
        strictInit(st, 0);
        const RmVerdict v = rmCheck(st, c, dst.c_str(), src.c_str(), passwd.c_str(), 22, 1000000);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.ctr == 0 ? "sync" : "ok", rmVerdictName(v), msg);

        // and every vector command passes the public allowlist predicate
        TEST_ASSERT_TRUE_MESSAGE(rmCommandAllowed(cmd.c_str(), args.c_str(), 22), msg);
    }
    printf("[vectors] commands built: %u/%u\n", (unsigned)n, (unsigned)n);
}

static void test_build_command_limits(void)
{
    uint8_t key[32];
    rmDeriveKey("secret", key);
    char out[160];
    char tiny[10];
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(DST, SRC, 1, "reboot", "", key, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(nullptr, SRC, 1, "reboot", "", key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(DST, SRC, 1, "", "", key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(DST, SRC, 1, "averyveryverylongcmd", "", key, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(DST, SRC, 1, "reboot", "01234567890123456789012345678901234567890", key, out, sizeof(out)));
    // args == nullptr is "no args"
    TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, 1, "reboot", nullptr, key, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("RM1 1 reboot 18f287b79c551022", out);
}

static void test_command_allowed_predicate(void)
{
    TEST_ASSERT_TRUE(rmCommandAllowed("status", "", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("status", nullptr, 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("sync", "", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("gps", "on", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("led", "on", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("led", "off", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("led", "", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("led", "blink", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("led", "1", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("setout", "b7 off", 22));
    TEST_ASSERT_TRUE(rmCommandAllowed("txpower", "22", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("txpower", "23", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("txpower", "10", 5));
    TEST_ASSERT_FALSE(rmCommandAllowed("status", "x", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("gps", "", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("gps", "ON", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("GPS", "on", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("setout", "c1 on", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("setout", "a2  on", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("setout", "a2 on ", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("wifi", "on", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("setpasswd", "x", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("", "", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed(nullptr, "", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("gps", "on;reboot", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("reboot", "--now", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("averyveryverylongcmd", "", 22));
}

// DRY-07: the allowlist is generated from rm_commands.h. The expected list is pinned LITERALLY here (comparing
// the allowlist with the macro expansion alone would be a tautology): adding, removing or renaming a command
// needs this list changed on purpose, and an ex-command must stop being allowed.
static void test_allowlist_is_the_pinned_command_list(void)
{
    static const char *const kPinned[] = {
        "reboot", "status", "sendpos", "sendtrack", "sync", "gps", "track", "display", "led", "gateway", "mesh",
        "txpower", "setout", "radio", "sens", "txq", "mbox", "maxhop", "name", "atxt", "pos", "mh"};
    const size_t n = sizeof(kPinned) / sizeof(kPinned[0]);
    TEST_ASSERT_EQUAL_UINT(22, n);
    TEST_ASSERT_EQUAL_UINT(22, RM_COMMAND_COUNT);

    // the list itself, name by name and in order, with a sample argument that fits the shape in column 3
    size_t i = 0;
#define CHECK_ROW(nm, kind, shape)                                                                               \
    do                                                                                                           \
    {                                                                                                            \
        TEST_ASSERT_TRUE_MESSAGE(i < n, #nm);                                                                    \
        TEST_ASSERT_EQUAL_STRING(kPinned[i], #nm);                                                               \
        const char *good = !strcmp(#shape, "ONOFF")     ? "on"                                                  \
                           : !strcmp(#shape, "TXPOWER") ? "10"                                                   \
                           : !strcmp(#shape, "SETOUT")  ? "a0 on"                                                \
                           : !strcmp(#shape, "MH")      ? "5"                                                    \
                                                        : "";                                                    \
        TEST_ASSERT_TRUE_MESSAGE(rmCommandAllowed(#nm, good, 22), #nm);                                          \
        /* a stray argument never passes a no-argument shape, and a missing one never passes on/off */         \
        if (!strcmp(#shape, "NONE"))                                                                             \
            TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(#nm, "x", 22), #nm);                                      \
        if (!strcmp(#shape, "ONOFF"))                                                                            \
            TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(#nm, "", 22), #nm);                                       \
        i++;                                                                                                     \
    } while (0);
    RM_COMMAND_LIST(CHECK_ROW)
#undef CHECK_ROW
    TEST_ASSERT_EQUAL_UINT(n, i);

    // by kind: 5 actions, 6 toggles, 2 parametrised, 6 reads, 3 read/write
    TEST_ASSERT_EQUAL_UINT(6, rmCountWhere(rmKindIsToggle));
    TEST_ASSERT_EQUAL_UINT(9, rmCountWhere(rmKindIsRead));
    TEST_ASSERT_EQUAL_UINT(3, rmCountWhere(rmKindIsWrite));
    TEST_ASSERT_EQUAL_INT(RM_CK_TOGGLE, rmCommandKind("led"));
    TEST_ASSERT_EQUAL_INT(RM_CK_RW, rmCommandKind("pos"));
    TEST_ASSERT_EQUAL_INT(RM_CK_READ, rmCommandKind("mh"));
    TEST_ASSERT_EQUAL_INT(-1, rmCommandKind("rmstrictsecurity"));

    // and the console-only switches are not on it
    const char *const kBlocked[] = {"rmstrictsecurity", "remotemgmt", "passwd", "setpasswd", "wifi", "rm", "stat",
                                    "namex", "rebootx", ""};
    for (const char *b : kBlocked)
        TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(b, "", 22), b);
}

// DRY-04: the free-text limits of the allowlist are RM_NAME_MAX / RM_ATXT_MAX (19 / 39 today)
static void test_allowlist_free_text_limits(void)
{
    TEST_ASSERT_EQUAL_UINT(19, RM_NAME_MAX);
    TEST_ASSERT_EQUAL_UINT(39, RM_ATXT_MAX);
    char b[64];
    memset(b, 'a', sizeof b);
    b[19] = 0;
    TEST_ASSERT_TRUE(rmCommandAllowed("name", b, 22));
    b[19] = 'a';
    b[20] = 0;
    TEST_ASSERT_FALSE(rmCommandAllowed("name", b, 22));
    memset(b, 'a', sizeof b);
    b[39] = 0;
    TEST_ASSERT_TRUE(rmCommandAllowed("atxt", b, 22));
    b[39] = 'a';
    b[40] = 0;
    TEST_ASSERT_FALSE(rmCommandAllowed("atxt", b, 22));
}

static void test_verify_reply_vectors_and_tampering(void)
{
    std::string json;
    if (!read_repo_file("tools/tests/remote_cmd_vectors.json", json))
        TEST_FAIL_MESSAGE("tools/tests/remote_cmd_vectors.json not found");

    static std::string objs[64];
    const size_t n = objectsOf(json, "replies", objs, 64);
    TEST_ASSERT_EQUAL_INT(25, n);

    for (size_t i = 0; i < n; i++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "verify reply vector %u", (unsigned)i);

        std::string passwd, dst, src, result, text;
        uint32_t ctr = 0;
        TEST_ASSERT_TRUE_MESSAGE(jsonString(objs[i], "passwd", passwd) && jsonString(objs[i], "dst", dst) &&
                                     jsonString(objs[i], "src", src) && jsonString(objs[i], "result", result) &&
                                     jsonString(objs[i], "reply_text", text) && jsonUint(objs[i], "ctr", ctr),
                                 msg);
        uint8_t key[32];
        rmDeriveKey(passwd.c_str(), key);

        char got[RM_MAX_RESULT + 1];
        memset(got, 'x', sizeof(got));
        TEST_ASSERT_TRUE_MESSAGE(rmVerifyReply(text.c_str(), dst.c_str(), src.c_str(), ctr, key, got, sizeof(got)), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(result.c_str(), got, msg);

        // wrong ctr, wrong dst, wrong src, wrong key
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(text.c_str(), dst.c_str(), src.c_str(), ctr + 1, key, got, sizeof(got)), msg);
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(text.c_str(), "DK5EN-99", src.c_str(), ctr, key, got, sizeof(got)), msg);
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(text.c_str(), dst.c_str(), "DK5EN-99", ctr, key, got, sizeof(got)), msg);
        uint8_t other[32];
        rmDeriveKey("not-the-secret", other);
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(text.c_str(), dst.c_str(), src.c_str(), ctr, other, got, sizeof(got)), msg);

        // tampered status: every byte of the result part flipped once
        for (size_t k = 4; k + 17 < text.size(); k++)
        {
            std::string t = text;
            t[k] = (t[k] == 'z') ? 'y' : (char)(t[k] + 1);
            got[0] = 'x';
            got[1] = '\0';
            TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(t.c_str(), dst.c_str(), src.c_str(), ctr, key, got, sizeof(got)), msg);
            TEST_ASSERT_EQUAL_CHAR_MESSAGE('x', got[0], "result must stay untouched on failure");
        }
        // tampered tag
        {
            std::string t = text;
            t[t.size() - 1] = (t[t.size() - 1] == '0') ? '1' : '0';
            TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(t.c_str(), dst.c_str(), src.c_str(), ctr, key, got, sizeof(got)), msg);
        }
        // result buffer too small
        char small[3];
        TEST_ASSERT_FALSE_MESSAGE(rmVerifyReply(text.c_str(), dst.c_str(), src.c_str(), ctr, key, small, sizeof(small)), msg);
    }
    printf("[vectors] replies verified: %u/%u\n", (unsigned)n, (unsigned)n);
}

static void test_verify_reply_rejects_malformed_and_commands(void)
{
    uint8_t key[32];
    rmDeriveKey("secret", key);
    char got[80];
    // a COMMAND is never a reply, even with a valid tag over its own canonical
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 1 reboot 18f287b79c551022", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 1 ok rebooting", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 1 ok rebooting facf04f881d89cdeX", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 01 ok rebooting facf04f881d89cde", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM2 1 ok rebooting facf04f881d89cde", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 1 ok rebooting FACF04F881D89CDE", DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply(nullptr, DST, SRC, 1, key, got, sizeof(got)));
    TEST_ASSERT_FALSE(rmVerifyReply("RM1 1 ok rebooting facf04f881d89cde", DST, SRC, 1, key, nullptr, 0));
    // build -> reply -> verify round trip through the core
    RmCmd c;
    memset(&c, 0, sizeof(c));
    c.ctr = 77;
    char wire[160];
    TEST_ASSERT_TRUE(rmReply(c, "ok txpower=2", DST, SRC, "secret", wire, sizeof(wire)) > 0);
    TEST_ASSERT_TRUE(rmVerifyReply(wire, DST, SRC, 77, key, got, sizeof(got)));
    TEST_ASSERT_EQUAL_STRING("ok txpower=2", got);
}

static void test_reply_queue_is_separate_from_command_queue(void)
{
    char src[RM_QUEUE_SRC_LEN], text[RM_QUEUE_TEXT_LEN];
    rmQueueReset();
    TEST_ASSERT_EQUAL_UINT8(0, rmReplyQueueCount());
    TEST_ASSERT_TRUE(rmReplyPush("DK5EN-90", "RM1 1 ok rebooting facf04f881d89cde"));
    TEST_ASSERT_TRUE(rmQueuePush("DK5EN-1", "RM1 2 status c70d9ec28da06758"));
    TEST_ASSERT_EQUAL_UINT8(1, rmReplyQueueCount());
    TEST_ASSERT_EQUAL_UINT8(1, rmQueueCount());
    TEST_ASSERT_TRUE(rmReplyPush("DK5EN-92", "RM1 3 ok x aaaaaaaaaaaaaaaa"));
    TEST_ASSERT_FALSE(rmReplyPush("DK5EN-93", "RM1 4 ok x aaaaaaaaaaaaaaaa")); // drop newest
    TEST_ASSERT_TRUE(rmReplyPop(src, sizeof(src), text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", src);
    TEST_ASSERT_EQUAL_STRING("RM1 1 ok rebooting facf04f881d89cde", text);
    TEST_ASSERT_TRUE(rmQueuePop(src, sizeof(src), text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", src);
    TEST_ASSERT_TRUE(rmReplyPop(src, sizeof(src), text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("DK5EN-92", src);
    TEST_ASSERT_FALSE(rmReplyPop(src, sizeof(src), text, sizeof(text)));
    TEST_ASSERT_FALSE(rmReplyPush(nullptr, "RM1 1 ok a b"));
    TEST_ASSERT_FALSE(rmReplyPush("TOOLONGCALL", "RM1 1 ok a b"));
    rmqReplyWanted() = true;
    TEST_ASSERT_TRUE(rmqReplyWanted());
    rmqReplyWanted() = false;
    rmQueueReset();
}

// ---- W0b: buffers, sanitiser, allowlist table, capability, status vectors -----------------------------

static std::string repeat(char ch, size_t n) { return std::string(n, ch); }

static void test_long_result_does_not_touch_lock_state(void)
{
    RmState s;
    strictInit(s, 0);
    s.rej[0].used = true;
    strcpy(s.rej[0].call, SRC);
    s.rej[0].rejCount = 2;
    s.rej[0].lockActive = true;
    s.rej[0].lockUntilMs = 0xA5A5A5A5u;
    const uint8_t rej = s.rej[0].rejCount, lock = (uint8_t)s.rej[0].lockActive;
    const uint32_t until = s.rej[0].lockUntilMs;
    const std::string r = "ok " + repeat('x', RM_MAX_RESULT - 3);
    TEST_ASSERT_EQUAL_UINT(RM_MAX_RESULT, r.size());
    rmAccept(s, mk(5, "status"), r.c_str(), 1000);
    // byte compares: host clang -O1 hides an overwrite behind bool reads
    TEST_ASSERT_EQUAL_UINT8(rej, *(const uint8_t *)&s.rej[0].rejCount);
    TEST_ASSERT_EQUAL_UINT8(lock, *(const uint8_t *)&s.rej[0].lockActive);
    TEST_ASSERT_EQUAL_MEMORY(&until, &s.rej[0].lockUntilMs, sizeof(until));
    TEST_ASSERT_EQUAL_UINT(RM_MAX_RESULT, strlen(s.lastReply));
    TEST_ASSERT_EQUAL_STRING(r.c_str(), s.lastReply);
}

static void test_reply_wire_is_140_and_109_is_refused(void)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    const std::string r = "ok " + repeat('y', RM_MAX_RESULT - 3);
    RmCmd c = mk(4294967295u, "status");
    char wire[160];
    const size_t n = rmReply(c, r.c_str(), DST, SRC, PW, wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT(140, n);
    char back[RM_MAX_RESULT + 1];
    TEST_ASSERT_TRUE(rmVerifyReply(wire, DST, SRC, 4294967295u, key, back, sizeof(back)));
    TEST_ASSERT_EQUAL_STRING(r.c_str(), back);
    // 109 chars: refused on both sides (rmReply builds nothing, rmVerifyReply rejects a hand-built frame)
    const std::string r9 = r + "z";
    TEST_ASSERT_EQUAL_UINT(0, rmReply(c, r9.c_str(), DST, SRC, PW, wire, sizeof(wire)));
    char frame[200];
    snprintf(frame, sizeof(frame), "RM1 4294967295 %s 0123456789abcdef", r9.c_str());
    TEST_ASSERT_FALSE(rmVerifyReply(frame, DST, SRC, 4294967295u, key, back, sizeof(back)));
}

static void test_args_39_roundtrip_and_40_refused(void)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    const std::string a39 = repeat('a', RM_MAX_ARGS), a40 = repeat('a', RM_MAX_ARGS + 1);
    char wire[160];
    // no allowlisted command takes this yet: the length gate is checked by the parser, the verdict stays "not allowed"
    TEST_ASSERT_FALSE(rmCommandAllowed("setout", a39.c_str(), 22));
    TEST_ASSERT_EQUAL_UINT(0, rmBuildCommand(DST, SRC, 9, "status", a40.c_str(), key, wire, sizeof(wire)));
    const size_t n = rmBuildCommand(DST, SRC, 9, "status", a39.c_str(), key, wire, sizeof(wire));
    TEST_ASSERT_TRUE(n > 0 && n <= 96);
    RmCmd c;
    TEST_ASSERT_TRUE(rmParse(wire, c));
    TEST_ASSERT_EQUAL_STRING(a39.c_str(), c.args);
    RmState s;
    strictInit(s, 0);
    TEST_ASSERT_EQUAL_INT(RM_REJ_BLOCKED, rmCheck(s, c, DST, SRC, PW, 22, 1000)); // args on a no-args command
    std::string frame = std::string("RM1 9 status ") + a40 + " 0123456789abcdef";
    TEST_ASSERT_FALSE(rmParse(frame.c_str(), c));
}

static void test_sanitiser_vectors(void)
{
    struct { const char *in, *out; } v[] = {
        {"<>\"'&", "?????"}, {"a:ack5", "a?ack5"}, {"{", "?"}, {"}", "?"}, {";", "?"}, {"%", "?"}, {"|", "?"},
        {"ok caf\xC3\xA9 \x80\xFF\x01\x7F", "ok caf?? ????"},
    };
    for (auto &t : v)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", t.in);
        rmSanitizeResult(buf);
        TEST_ASSERT_EQUAL_STRING(t.out, buf);
    }
    char all[128] = " -./=+_@?(),*#";
    for (char ch = 'a'; ch <= 'z'; ch++) strncat(all, &ch, 1);
    for (char ch = 'A'; ch <= 'Z'; ch++) strncat(all, &ch, 1);
    for (char ch = '0'; ch <= '9'; ch++) strncat(all, &ch, 1);
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", all);
    TEST_ASSERT_EQUAL_UINT(strlen(all), rmSanitizeResult(copy));
    TEST_ASSERT_EQUAL_STRING(all, copy);
}

static void test_allowlist_table_equivalence(void)
{
    struct { const char *cmd, *args; bool ok; } v[] = {
        {"reboot", "", true}, {"reboot", "x", false},     {"status", "", true},   {"status", "on", false},
        {"sendpos", "", true}, {"sendpos", "1", false},   {"sendtrack", "", true}, {"sendtrack", "on", false},
        {"sync", "", true},   {"sync", "1", false},       {"gps", "on", true},    {"gps", "", false},
        {"track", "off", true}, {"track", "ON", false},   {"display", "on", true}, {"display", "1", false},
        {"led", "off", true}, {"led", "on off", false},   {"gateway", "on", true}, {"gateway", "no", false},
        {"mesh", "off", true}, {"mesh", "", false},       {"txpower", "0", true}, {"txpower", "22", true},
        {"txpower", "23", false}, {"txpower", "05", false}, {"txpower", "-1", false}, {"txpower", "1234", false},
        {"setout", "a0 on", true}, {"setout", "b7 off", true}, {"setout", "a8 on", false}, {"setout", "c1 on", false},
        {"setout", "a1on", false}, {"setout", "a1 x", false}, {"setout", "a1 on ", false}, {"nope", "", false},
        {"Reboot", "", false},
    };
    for (auto &t : v)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s '%s'", t.cmd, t.args);
        TEST_ASSERT_EQUAL_MESSAGE(t.ok, rmCommandAllowed(t.cmd, t.args, 22), msg);
    }
}

// ---- W2-A: the nine extended names, syntax only (concept 4.4) ------------------------------------------

static void test_ext_allowlist_accepts_and_rejects(void)
{
    struct { const char *cmd, *args; bool ok; } v[] = {
        {"radio", "", true}, {"radio", "x", false}, {"sens", "", true}, {"sens", "1", false},
        {"txq", "", true}, {"txq", "x", false}, {"mbox", "", true}, {"mbox", "0", false},
        {"maxhop", "", true}, {"maxhop", "on", false},
        {"name", "", true}, {"name", "Martin", true}, {"name", "martin", true}, {"name", "A", true},
        {"name", "Max Mustermann", true}, {"name", "1234567890123456789", true},
        {"name", "12345678901234567890", false}, // 20 chars
        {"name", "a{b", false}, {"name", "a|b", false}, {"name", "a:b", false}, {"name", "a}b", false},
        {"name", "a;b", false}, {"name", "a%b", false}, {"name", "a--b", false},
        {"atxt", "", true}, {"atxt", "MeshCom Garten", true}, {"atxt", "a{b", false}, {"atxt", "a|b", false},
        {"atxt", "a:b", false}, {"atxt", "123456789012345678901234567890123456789", true}, // 39
        {"atxt", "1234567890123456789012345678901234567890", false},                        // 40
        {"pos", "", true}, {"pos", "48.40812 11.73812 492", true}, {"pos", "-48.4 -11.7 3", true},
        {"pos", "1 2", false}, {"pos", "a b c", false}, {"pos", "1 2 3 4", false}, {"pos", "48.4,11.7,492", false},
        {"mh", "0", true}, {"mh", "999", true}, {"mh", "1000", false}, {"mh", "", false},
        {"mh", "DK5EN-98", true}, {"mh", "dk5en-98", true}, {"mh", "DK5EN", true}, {"mh", "DK5EN-98 x", false},
        {"mh", "DK5EN-", false}, {"mh", "DK5EN-123", false}, {"mh", "DK5|EN", false},
        // the old shapes keep their exact lower-case grammar
        {"gps", "ON", false}, {"txpower", "1O", false}, {"setout", "A0 on", false}, {"setout", "a0 ON", false},
        {"Radio", "", false}, {"RADIO", "", false},
    };
    for (auto &t : v)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s '%s'", t.cmd, t.args);
        TEST_ASSERT_EQUAL_MESSAGE(t.ok, rmCommandAllowed(t.cmd, t.args, 22), msg);
    }
}

static void test_ext_args_survive_parse_build_and_check(void)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    const char *cases[][2] = {{"name", "Martin"}, {"atxt", "MeshCom Garten"}, {"mh", "DK5EN-98"}, {"pos", "48.40812 11.73812 492"}};
    for (auto &t : cases)
    {
        char wire[160];
        const size_t n = rmBuildCommand(DST, SRC, 7, t[0], t[1], key, wire, sizeof(wire));
        TEST_ASSERT_TRUE_MESSAGE(n > 0, t[0]);
        RmCmd c;
        TEST_ASSERT_TRUE_MESSAGE(rmParse(wire, c), t[0]);
        TEST_ASSERT_EQUAL_STRING(t[0], c.cmd);
        TEST_ASSERT_EQUAL_STRING(t[1], c.args); // capitals kept byte for byte
        RmState s;
        strictInit(s, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RM_OK, rmCheck(s, c, DST, SRC, PW, 22, 1000), t[0]);
    }
    // a capital in the command name or in the tag region stays refused on the wire
    RmCmd c;
    TEST_ASSERT_FALSE(rmParse("RM1 7 Name Martin 0123456789abcdef", c));
    TEST_ASSERT_FALSE(rmParse("RM1 7 name Martin 0123456789abcdeF", c));
    char w0[160];
    TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, 7, "Name", "Martin", key, w0, sizeof(w0)) > 0);
    TEST_ASSERT_FALSE(rmParse(w0, c)); // the receiver never sees a capital in the command name
    // old lower-case-only shape: "gps ON" is a counted BLOCKED reject
    char wire[160];
    TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, 8, "gps", "ON", key, wire, sizeof(wire)) > 0);
    TEST_ASSERT_TRUE(rmParse(wire, c));
    TEST_ASSERT_EQUAL_STRING("ON", c.args); // the parser keeps the bytes, the shape refuses them
    RmState s;
    strictInit(s, 0);
    TEST_ASSERT_EQUAL_INT(RM_REJ_BLOCKED, rmCheck(s, c, DST, SRC, PW, 22, 1000));
}

// the canonical string joins fields with '|' and the (cmd, args) split is the first space: no args text can
// forge another split or another field
static void test_ext_separator_cannot_be_injected_through_args(void)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    char wire[160];
    TEST_ASSERT_FALSE(rmCommandAllowed("name", "x|RM1|a|b|9|reboot", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("name", "Max|Muster", 22));
    TEST_ASSERT_FALSE(rmCommandAllowed("atxt", "|", 22));
    // a correctly tagged frame with a separator in the text is still a counted BLOCKED reject
    TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, 5, "name", "Max|Muster", key, wire, sizeof(wire)) > 0);
    RmCmd bad;
    TEST_ASSERT_TRUE(rmParse(wire, bad));
    RmState bs;
    strictInit(bs, 0);
    TEST_ASSERT_EQUAL_INT(RM_REJ_BLOCKED, rmCheck(bs, bad, DST, SRC, PW, 22, 1000));
    // a second command smuggled through the args of a free-text command is a different tag on a different
    // canonical: (name, "x reboot") and (name x, reboot) cannot collide because only ONE space-split exists
    RmCmd a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.ctr = b.ctr = 5;
    strcpy(a.cmd, "name");
    strcpy(a.args, "x reboot");
    strcpy(b.cmd, "name x");
    strcpy(b.args, "reboot");
    char ca[96], cb[96];
    TEST_ASSERT_TRUE(rmCanonical(a, DST, SRC, ca, sizeof(ca)) > 0);
    TEST_ASSERT_TRUE(rmCanonical(b, DST, SRC, cb, sizeof(cb)) > 0);
    // identical canonical strings, but cmd "name x" is never allowlisted and rmParse never yields a cmd with a space
    TEST_ASSERT_FALSE(rmCommandAllowed("name x", "reboot", 22));
    RmCmd p;
    TEST_ASSERT_TRUE(rmParse("RM1 5 name x reboot 0123456789abcdef", p));
    TEST_ASSERT_EQUAL_STRING("name", p.cmd);
    TEST_ASSERT_EQUAL_STRING("x reboot", p.args);
    TEST_ASSERT_TRUE(strchr(ca, '|') != nullptr);
    // only the 5 fixed '|' separators exist in the canonical string
    int bars = 0;
    for (const char *q = ca; *q; q++)
        bars += (*q == '|');
    TEST_ASSERT_EQUAL_INT(4, bars);
}

static void test_legacy_replies_stay_within_63_and_cap_token(void)
{
    // worst-case status and the sync reply of the existing commands stay readable by older operators
    char res[RM_MAX_RESULT + 1];
    RmSwitches sw;
    memset(&sw, 1, sizeof(sw));
    // D9: " pmin=<n>" (up to 9 chars) takes the worst status past the 63 older operators accept, but it
    // stays inside the wire cap, so the reply is delivered and a current parser reads the floor.
    // A typical status (first weeks of uptime, RAK-class floor 2) still fits 63:
    rmFormatStatus(res, sizeof(res), "4.40a", 417u, 87, 212, sw, 17, 22, 2);
    TEST_ASSERT_TRUE(strlen(res) <= RM_LEGACY_RESULT_MAX);
    rmFormatStatus(res, sizeof(res), "4.40A", 71582u, 100, 9999, sw, -20, 22, -20);
    TEST_ASSERT_TRUE(strlen(res) <= RM_MAX_RESULT);
    printf("status worst case with pmin: %u of %d (legacy %d)\n", (unsigned)strlen(res), RM_MAX_RESULT,
           RM_LEGACY_RESULT_MAX);
    TEST_ASSERT_TRUE(strlen(res) > RM_LEGACY_RESULT_MAX); // KNOWN, escalated: 70 of 63 legacy
    TEST_ASSERT_NOT_NULL(strstr(res, " p=-20/22 pmin=-20"));
    snprintf(res, sizeof(res), "ok ctr=%lu v=%s%s rm=%d", 4294967295ul, "4.40a", "", RM_CAP_LEVEL);
    TEST_ASSERT_TRUE(strlen(res) <= RM_LEGACY_RESULT_MAX);
    TEST_ASSERT_EQUAL_INT(2, rmCapLevel(res));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=42 v=4.40a"));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=42 v=4.40a rm="));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=42 v=4.40a rm=x"));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel(nullptr));
    // every existing reply form passes the sanitiser unchanged
    const char *ok[] = {"ok rebooting", "err range", "err storage", "ok ctr=42 v=4.40a rm=2",
                        "ok v=4.40a up=417 bat=0 heap=115 s=gtdMwl p=2/22 led=0", "ok gps=on", "ok setout a2 on"};
    for (const char *r : ok)
    {
        char b[RM_MAX_RESULT + 1];
        snprintf(b, sizeof(b), "%s", r);
        rmSanitizeResult(b);
        TEST_ASSERT_EQUAL_STRING(r, b);
    }
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=1 v=4.40a rm=12"));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=1 v=4.40a rm=2x"));
    TEST_ASSERT_EQUAL_INT(0, rmCapLevel("ok ctr=1 v=rm=2a"));
}

static void test_status_vectors_parse(void)
{
    std::string json;
    TEST_ASSERT_TRUE(read_repo_file("tools/tests/remote_cmd_vectors.json", json));
    static std::string objs[64];
    const size_t n = objectsOf(json, "replies", objs, 64);
    int seen = 0, caps = 0;
    for (size_t i = 0; i < n; i++)
    {
        std::string res;
        TEST_ASSERT_TRUE(jsonString(objs[i], "result", res));
        if (objs[i].find("\"status_sw\"") != std::string::npos)
        {
            RmStatusInfo st;
            TEST_ASSERT_TRUE(rmStatusParse(res.c_str(), st));
            std::string sw;
            TEST_ASSERT_TRUE(jsonString(objs[i], "status_sw", sw));
            TEST_ASSERT_EQUAL_UINT(11, sw.size());
            for (int j = 0; j < 6; j++)
            {
                const int e = sw[j * 2] == '-' ? -1 : sw[j * 2] - '0';
                TEST_ASSERT_EQUAL_INT(e, st.sw[j]);
            }
            TEST_ASSERT_TRUE(st.haveP);
            seen++;
        }
        if (objs[i].find("\"cap\"") != std::string::npos)
        {
            TEST_ASSERT_EQUAL_INT(RM_CAP_LEVEL, rmCapLevel(res.c_str()));
            caps++;
        }
    }
    TEST_ASSERT_EQUAL_INT(3, seen);
    TEST_ASSERT_EQUAL_INT(1, caps);
}

// ---- per-sender reject counter and lockout ------------------------------------------------------

// One frame from `call` (right or wrong tag, status command) through rmCheck.
static RmVerdict fromSender(RmState &s, const char *call, bool wrongTag, uint32_t ctr, uint32_t now)
{
    RmCmd c = mk(ctr, "status", "", DST, call);
    if (wrongTag)
        c = withBadTag(c);
    return rmCheck(s, c, DST, call, PW, MAXTX, now);
}

static void strikes3(RmState &s, const char *call, uint32_t t)
{
    for (uint32_t i = 0; i < 3; i++)
        ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call, true, 1, t + i));
}

static void test_lockout_is_per_sender(void)
{
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", 1000);
    ASSERT_VERDICT(RM_REJ_LOCKOUT, fromSender(s, "DK5EN-14", false, 1, 1003));
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", 1003));
    // another sender in the same second is not affected
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-92", false, 1, 1003));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-92", 1003));
    TEST_ASSERT_EQUAL_UINT8(1, rmLockedSenders(s, 1003, nullptr));
}

static void test_rejects_of_different_senders_do_not_add_up(void)
{
    RmState s;
    strictInit(s, 0);
    const char *calls[6] = {"DK5EN-11", "DK5EN-12", "DK5EN-13", "DK5EN-14", "DK5EN-15", "DK5EN-16"};
    for (uint32_t round = 0; round < 2; round++)
        for (uint32_t i = 0; i < 6; i++)
            ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, calls[i], true, 1, 1000 + round * 1000 + i));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 3000, nullptr));
    for (uint32_t i = 0; i < 6; i++)
    {
        TEST_ASSERT_FALSE(rmSenderLocked(s, calls[i], 3000));
        TEST_ASSERT_EQUAL_UINT8(2, ent(s, calls[i]).rejCount);
    }
}

static void test_sender_lock_expires_and_window_is_per_sender(void)
{
    const uint32_t t0 = 100000;
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", t0); // A locked until t0 + 2 + RM_LOCKOUT_MS
    // B starts its own window later; A's lock and A's (long past) window do not touch it
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-92", true, 1, t0 + 100000));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-92", true, 1, t0 + 100001));
    TEST_ASSERT_EQUAL_UINT8(2, ent(s, "DK5EN-92").rejCount);
    // third strike inside B's window (A's window started 90 s+ earlier) locks B
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-92", true, 1, t0 + 100000 + RM_REJ_WINDOW_MS - 1));
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-92", t0 + 190000));

    // A's lock lapses after RM_LOCKOUT_MS, B's (started 90 s later) is still on
    const uint32_t aEnd = t0 + 2 + RM_LOCKOUT_MS;
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", aEnd - 1));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", aEnd));
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-92", aEnd));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, fromSender(s, "DK5EN-92", false, 1, aEnd));
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-14", false, 1, aEnd));
    TEST_ASSERT_FALSE(ent(s, "DK5EN-14").lockActive); // cleared on its next frame
}

static void test_reject_table_replacement_order(void)
{
    char call[RM_REJ_SOURCES + 2][10];
    for (int i = 0; i < RM_REJ_SOURCES + 2; i++)
        snprintf(call[i], sizeof(call[i]), "DK5EN-%d", 21 + i);

    // a locked sender survives while an unlocked one exists, the oldest unlocked one is replaced
    RmState s;
    strictInit(s, 0);
    strikes3(s, call[0], 1000); // oldest, but locked
    for (int i = 1; i < RM_REJ_SOURCES; i++)
        ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call[i], true, 1, 1000 + 1000 * (uint32_t)i));
    for (int i = 0; i < RM_REJ_SOURCES; i++)
        TEST_ASSERT_TRUE(ent(s, call[i]).used);
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call[RM_REJ_SOURCES], true, 1, 7000));
    TEST_ASSERT_TRUE(ent(s, call[0]).lockActive);
    TEST_ASSERT_TRUE(rmSenderLocked(s, call[0], 7000));
    TEST_ASSERT_FALSE(ent(s, call[1]).used); // oldest unlocked one gone
    TEST_ASSERT_EQUAL_UINT8(1, ent(s, call[2]).rejCount);
    TEST_ASSERT_EQUAL_UINT8(1, ent(s, call[RM_REJ_SOURCES]).rejCount);

    // an expired lock counts as unlocked: now call[0] (oldest lastMs) is the one to go
    const uint32_t late = 1002 + RM_LOCKOUT_MS + 10;
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call[RM_REJ_SOURCES + 1], true, 1, late));
    TEST_ASSERT_FALSE(ent(s, call[0]).used);

    // all locked: the one with the oldest lastMs is replaced
    RmState t;
    strictInit(t, 0);
    for (int i = 0; i < RM_REJ_SOURCES; i++)
        strikes3(t, call[i], 1000 + 1000 * (uint32_t)i);
    TEST_ASSERT_EQUAL_UINT8(RM_REJ_SOURCES, rmLockedSenders(t, 8000, nullptr));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(t, call[RM_REJ_SOURCES], true, 1, 10000));
    TEST_ASSERT_FALSE(ent(t, call[0]).used);
    TEST_ASSERT_TRUE(ent(t, call[1]).lockActive);
    TEST_ASSERT_EQUAL_UINT8(1, ent(t, call[RM_REJ_SOURCES]).rejCount);
    TEST_ASSERT_EQUAL_UINT8(RM_REJ_SOURCES - 1, rmLockedSenders(t, 10000, nullptr));
}

static void test_similar_calls_are_distinct_senders(void)
{
    RmState s;
    strictInit(s, 0);
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-1", true, 1, 1000));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-12", true, 1, 1001));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-1", true, 1, 1002));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-12", true, 1, 1003));
    TEST_ASSERT_EQUAL_UINT8(2, ent(s, "DK5EN-1").rejCount);
    TEST_ASSERT_EQUAL_UINT8(2, ent(s, "DK5EN-12").rejCount);
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 1004, nullptr));
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-1", true, 1, 1004));
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-1", 1005));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-12", 1005));
}

static void test_uncounted_verdicts_create_no_entry(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd first = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, first, 1000));
    rmAccept(s, first, "ok", 1000);
    strikes3(s, "DK5EN-8", 1100);
    RmRejSrc before[RM_REJ_SOURCES];
    memcpy(before, s.rej, sizeof(before));

    ASSERT_VERDICT(RM_CACHED, chk(s, first, 2000));
    ASSERT_VERDICT(RM_REJ_REPLAY, fromSender(s, "DK5EN-9", false, 1, 2000));
    ASSERT_VERDICT(RM_REJ_RATE, fromSender(s, "DK5EN-9", false, 2, 2000));
    ASSERT_VERDICT(RM_REJ_LOCKOUT, fromSender(s, "DK5EN-8", false, 3, 2000));
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, mk(4, "status", "", DST, "DK5EN-7"), DST, "DK5EN-7", "", MAXTX, 2000));
    TEST_ASSERT_EQUAL_MEMORY(before, s.rej, sizeof(before));
    for (int i = 0; i < RM_REJ_SOURCES; i++)
        TEST_ASSERT_EQUAL(before[i].used, s.rej[i].used);
}

static void test_receiver_unlock_clears_every_sender(void)
{
    RmState s;
    strictInit(s, 0);
    const RmCmd first = mk(1, "status");
    ASSERT_VERDICT(RM_OK, chk(s, first, 1000));
    rmAccept(s, first, "ok", 1000);
    strikes3(s, "DK5EN-14", 12000);
    strikes3(s, "DK5EN-92", 12010);
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-5", true, 1, 12020)); // a counting, unlocked one too
    TEST_ASSERT_EQUAL_UINT8(2, rmLockedSenders(s, 13000, nullptr));

    const RmState was = s;
    rmReceiverUnlock(s);
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 13000, nullptr));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", 13000));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-92", 13000));
    const RmRejSrc zero[RM_REJ_SOURCES] = {};
    TEST_ASSERT_EQUAL_MEMORY(zero, s.rej, sizeof(zero));
    TEST_ASSERT_EQUAL_UINT32(was.hwm, s.hwm);
    TEST_ASSERT_EQUAL_UINT32(was.lastCtr, s.lastCtr);
    TEST_ASSERT_EQUAL_STRING(was.lastReply, s.lastReply);
    TEST_ASSERT_EQUAL_STRING(was.lastTag, s.lastTag);
    TEST_ASSERT_EQUAL_UINT32(was.lastAcceptMs, s.lastAcceptMs);
    TEST_ASSERT_EQUAL_UINT32(was.lastRateMs, s.lastRateMs);
    TEST_ASSERT_EQUAL_UINT32(was.lastSyncMs, s.lastSyncMs);
    TEST_ASSERT_EQUAL(was.haveLast, s.haveLast);
    TEST_ASSERT_EQUAL(was.haveRate, s.haveRate);
    TEST_ASSERT_EQUAL(was.haveSync, s.haveSync);
}

static void test_sender_lock_survives_millis_wrap(void)
{
    const uint32_t t0 = 0xFFFFFFFFu - 1000u; // lock set 1 s before millis() wraps
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", t0);
    const uint32_t afterWrap = t0 + 2000u; // numerically small now
    TEST_ASSERT_TRUE(afterWrap < t0);
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", afterWrap));
    uint32_t remain = 0;
    TEST_ASSERT_EQUAL_UINT8(1, rmLockedSenders(s, afterWrap, &remain));
    TEST_ASSERT_EQUAL_UINT32(RM_LOCKOUT_MS - 1998u, remain);
    ASSERT_VERDICT(RM_REJ_LOCKOUT, fromSender(s, "DK5EN-14", false, 1, afterWrap));
    const uint32_t end = t0 + 2u + RM_LOCKOUT_MS;
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", end - 1));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", end));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, end, &remain));
    TEST_ASSERT_EQUAL_UINT32(0, remain);
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-14", false, 1, end));
}

// A lock or a count nobody touches again must not come back to life when millis() has run half way
// round (2^31 ms, 24.8 days): the signed "still locked" compare turns true again there. Any later
// frame, from any sender, ages the whole table (advisor finding, per-sender state made it reachable).
static void test_stale_lock_does_not_return_after_half_a_millis_turn(void)
{
    const uint32_t t0 = 5000u;
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", t0);
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", t0 + 10u));
    // the locked sender stays silent; ANOTHER sender's frame after the lock ran out ages the table
    const uint32_t later = t0 + RM_LOCKOUT_MS + 60000u;
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-92", false, 1, later));
    // 24.9 days after the lock: not locked, not reported, and the sender is served
    const uint32_t farLater = t0 + RM_LOCKOUT_MS + 0x80000000u + 1000u;
    uint32_t remain = 77;
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", farLater));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, farLater, &remain));
    TEST_ASSERT_EQUAL_UINT32(0, remain);
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-14", false, 2, farLater));
}

// the same without any frame in between: the firmware ages the table from its loop (rmRejSweep)
static void test_sweep_frees_expired_locks_and_windows(void)
{
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", 1000u);                                   // locked
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-15", true, 1, 2000u)); // one strike, window open
    rmRejSweep(s, 3000u);
    TEST_ASSERT_TRUE(ent(s, "DK5EN-14").used);
    TEST_ASSERT_TRUE(ent(s, "DK5EN-15").used);
    TEST_ASSERT_EQUAL_UINT8(1, ent(s, "DK5EN-15").rejCount);
    rmRejSweep(s, 2000u + RM_REJ_WINDOW_MS);                          // the window of -15 is over
    TEST_ASSERT_FALSE(ent(s, "DK5EN-15").used);
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", 2000u + RM_REJ_WINDOW_MS));
    rmRejSweep(s, 1002u + RM_LOCKOUT_MS + 5000u);                     // the lock of -14 is over
    TEST_ASSERT_FALSE(ent(s, "DK5EN-14").used);
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", 1002u + RM_LOCKOUT_MS + 0x80000000u + 9000u));
}

// odd sender strings: nullptr and "" share one entry, 9 characters fit, nothing overruns
static void test_reject_table_odd_sender_strings(void)
{
    RmState s;
    strictInit(s, 0);
    RmCmd c = withBadTag(mk(1, "status"));
    ASSERT_VERDICT(RM_REJ_FORMAT, rmCheck(s, c, DST, nullptr, PW, 22, 1000u));
    TEST_ASSERT_TRUE(ent(s, "").used);
    TEST_ASSERT_EQUAL_UINT8(1, ent(s, "").rejCount);
    rmCheck(s, c, DST, "", PW, 22, 1100u);
    TEST_ASSERT_EQUAL_UINT8(2, ent(s, "").rejCount);
    rmCheck(s, c, DST, "DL1ABC-12", PW, 22, 1200u);                   // 9 characters
    TEST_ASSERT_TRUE(ent(s, "DL1ABC-12").used);
    TEST_ASSERT_EQUAL_STRING("DL1ABC-12", ent(s, "DL1ABC-12").call);
    rmCheck(s, c, DST, "DL1ABC-123456789", PW, 22, 1300u);            // too long: first 9 characters
    TEST_ASSERT_EQUAL_UINT8(2, ent(s, "DL1ABC-12").rejCount);
    uint8_t used = 0;
    for (const RmRejSrc &r : s.rej)
        used += r.used ? 1 : 0;
    TEST_ASSERT_EQUAL_UINT8(2, used);
}

// ---- RM-DUP: the duplicate ring and its use in the receive gate --------------------------------------------------

static void test_seen_ring_same_source_and_id_is_a_duplicate_within_the_window(void)
{
    rmQueueReset();
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 0xEA25A006u, "a", 1000));
    TEST_ASSERT_TRUE(rmqSeenBefore("DK5EN-1", 0xEA25A006u, "a", 1000 + 59999));
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-90", 0xEA25A006u, "a", 2000)); // other source, same id
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 0xEA25A007u, "a", 2000));  // same source, other id
    TEST_ASSERT_FALSE_MESSAGE(rmqSeenBefore("DK5EN-1", 0, "a", 3000), "id 0 is unknown, never a duplicate");
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 0, "a", 3000));
}

static void test_seen_ring_same_id_with_other_text_is_not_a_duplicate(void)
{
    rmQueueReset();
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 5, "RM1 1 status aaaaaaaaaaaaaaaa", 100));
    TEST_ASSERT_FALSE_MESSAGE(rmqSeenBefore("DK5EN-1", 5, "RM1 2 status bbbbbbbbbbbbbbbb", 101),
                              "a spoofed frame with the same id must not swallow the real command");
    TEST_ASSERT_TRUE(rmqSeenBefore("DK5EN-1", 5, "RM1 2 status bbbbbbbbbbbbbbbb", 102));
}

static void test_seen_ring_clock_read_before_the_other_task_stamped_is_still_a_duplicate(void)
{
    rmQueueReset();
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 6, "a", 5000));
    TEST_ASSERT_TRUE_MESSAGE(rmqSeenBefore("DK5EN-1", 6, "a", 4999), "negative age counts as zero");
}

static void test_seen_ring_forgets_after_the_window_and_across_the_millis_wrap(void)
{
    rmQueueReset();
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 7, "a", 10));
    TEST_ASSERT_FALSE_MESSAGE(rmqSeenBefore("DK5EN-1", 7, "a", 10 + RM_SEEN_MS), "60 s later the id is new again");
    rmQueueReset();
    const uint32_t nearWrap = 0xFFFFFF00u;
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 9, "a", nearWrap));
    TEST_ASSERT_TRUE_MESSAGE(rmqSeenBefore("DK5EN-1", 9, "a", nearWrap + 1000u), "the clock wrapped, still inside the window");
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 9, "a", nearWrap + RM_SEEN_MS + 5u));
}

static void test_seen_ring_replaces_the_oldest_slot_and_reset_clears_it(void)
{
    rmQueueReset();
    for (uint32_t i = 1; i <= RM_SEEN_SLOTS; i++)
        TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", i, "a", 100 + i));
    TEST_ASSERT_TRUE(rmqSeenBefore("DK5EN-1", RM_SEEN_SLOTS, "a", 200));
    TEST_ASSERT_FALSE(rmqSeenBefore("DK5EN-1", 100, "a", 300));           // 9th id pushes out id 1
    TEST_ASSERT_FALSE_MESSAGE(rmqSeenBefore("DK5EN-1", 1, "a", 301), "id 1 was the oldest and is gone");
    rmQueueReset();
    TEST_ASSERT_FALSE_MESSAGE(rmqSeenBefore("DK5EN-1", 100, "a", 302), "reset forgets everything");
}

static void test_gate_second_copy_of_a_command_is_consumed_but_not_queued(void)
{
    char src[RM_QUEUE_SRC_LEN], text[RM_QUEUE_TEXT_LEN];
    const char *cmd = "RM1 1791300000 status 0123456789abcdef";
    rmQueueReset();
    TEST_ASSERT_TRUE(rmRxTryQueue("DK5EN-1", cmd, true, 0x91A434FCu, 5000));   // server copy
    TEST_ASSERT_TRUE_MESSAGE(rmRxTryQueue("DK5EN-1", cmd, true, 0x91A434FCu, 17000), "RF copy is consumed too");
    TEST_ASSERT_EQUAL_UINT8(1, rmQueueCount());
    TEST_ASSERT_TRUE(rmQueuePop(src, sizeof(src), text, sizeof(text)));
    TEST_ASSERT_TRUE(rmRxTryQueue("DK5EN-1", cmd, true, 0x91A434FDu, 18000)); // a fresh send
    TEST_ASSERT_EQUAL_UINT8(1, rmQueueCount());
}

static void test_gate_duplicates_do_not_touch_replies_or_disabled_nodes(void)
{
    const char *cmd = "RM1 1791300000 status 0123456789abcdef";
    const char *rep = "RM1 1791300000 ok v=4.40a 0123456789abcdef";
    rmQueueReset();
    rmqReplyWanted() = true;
    TEST_ASSERT_FALSE(rmRxTryQueue("DK5EN-1", rep, true, 77, 1000));
    TEST_ASSERT_FALSE(rmRxTryQueue("DK5EN-1", rep, true, 77, 1001)); // replies are shown whatever the id
    TEST_ASSERT_EQUAL_UINT8(2, rmReplyQueueCount());
    rmQueueReset();
    rmqReplyWanted() = false;
    TEST_ASSERT_FALSE(rmRxTryQueue("DK5EN-1", cmd, false, 88, 1000)); // RM off: ordinary text both times
    TEST_ASSERT_FALSE(rmRxTryQueue("DK5EN-1", cmd, false, 88, 1001));
    TEST_ASSERT_TRUE(rmRxTryQueue("DK5EN-1", cmd, true, 88, 1002));   // RM switched on: the id was never remembered
    TEST_ASSERT_EQUAL_UINT8(1, rmQueueCount());
    rmQueueReset();
}

// ---- BF-01: strict security OFF (the default) -----------------------------------------------------------
// With RmState::strict == 0 only correctness checks reject: format, allowlist, tag, replay counter.
// Nothing is punished and nothing waits (D2: no receiver rate limits; no lockout, also no
// per-sender strike table). The twins above run with strict ON via strictInit().

static void test_off_is_the_default_after_state_init(void)
{
    RmState s;
    memset(&s, 0xA5, sizeof(s));
    rmStateInit(s, 0);
    TEST_ASSERT_FALSE(s.strict);
}

static void test_off_wrong_tags_from_one_sender_never_lock(void)
{
    RmState s;
    rmStateInit(s, 0);
    for (uint32_t i = 0; i < 60; i++)
        ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-14", true, 1, 1000 + i * 100));
    TEST_ASSERT_FALSE(rmSenderLocked(s, "DK5EN-14", 7000));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 7000, nullptr));
    TEST_ASSERT_EQUAL_UINT8(0, ent(s, "DK5EN-14").rejCount); // no strike is recorded at all
    // the right key is still answered right away
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-14", false, 1, 7001));
}

// CR-01 / D1: the claimed callsign is unauthenticated, so an attacker rotates it. With the flag OFF no
// sender ever locks, however many callsigns and strikes there are.
static void test_off_wrong_tags_from_rotating_callsigns_never_lock(void)
{
    RmState s;
    rmStateInit(s, 0);
    uint32_t t = 1000;
    for (int n = 0; n < 12; n++) // 12 callsigns > RM_REJ_SOURCES slots
    {
        char call[12];
        snprintf(call, sizeof(call), "DL1ABC-%d", n + 1);
        for (int k = 0; k < 4; k++)
            ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call, true, 1, t++));
        // the same callsign with the right key right after its strikes: no lock
        char msg[40];
        snprintf(msg, sizeof(msg), "callsign %s", call);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(rmVerdictName(RM_OK), rmVerdictName(fromSender(s, call, false, 1, t++)), msg);
        TEST_ASSERT_FALSE_MESSAGE(rmSenderLocked(s, call, t), msg);
    }
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, t, nullptr));
}

static void test_off_back_to_back_valid_commands_one_second_apart_all_execute(void)
{
    RmState s;
    rmStateInit(s, 0);
    for (uint32_t i = 1; i <= 6; i++)
    {
        const RmCmd c = mk(i, "status");
        ASSERT_VERDICT(RM_OK, chk(s, c, 100000 + i * 1000));
        rmAccept(s, c, "ok", 100000 + i * 1000);
    }
    TEST_ASSERT_EQUAL_UINT32(6, s.hwm);
    // D2: syncs have no spacing either
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 200000));
    ASSERT_VERDICT(RM_SYNC, chk(s, mk(0, "sync"), 201000));
}

static void test_off_replay_bad_tag_blocked_and_format_are_still_rejected(void)
{
    RmState s;
    rmStateInit(s, 0);
    const RmCmd c = mk(5, "status");
    ASSERT_VERDICT(RM_OK, chk(s, c, 1000));
    rmAccept(s, c, "ok", 1000);
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(4, "status"), 2000));                  // older counter
    ASSERT_VERDICT(RM_REJ_REPLAY, chk(s, mk(5, "reboot"), 3000));                  // same counter, other command
    ASSERT_VERDICT(RM_REJ_TAG, chk(s, withBadTag(mk(6, "status")), 4000));          // bad tag
    ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(6, "factoryreset"), 5000));            // not on the allowlist
    ASSERT_VERDICT(RM_REJ_FORMAT, chk(s, mk(0, "status"), 6000));                   // ctr 0 only with sync
    ASSERT_VERDICT(RM_CACHED, chk(s, c, 7000));                                     // lost-reply recovery stays
    ASSERT_VERDICT(RM_REJ_DISABLED, rmCheck(s, c, DST, SRC, "", MAXTX, 8000));      // empty password
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 9000, nullptr));
}

// The ON twin of the rotating-callsign test: the bound of strict mode (CR-01, decided 2026-10-06) is the
// per-sender lockout; every rotated callsign locks after its own three strikes.
static void test_on_rotating_callsigns_each_lock_after_three_strikes(void)
{
    RmState s;
    strictInit(s, 0);
    uint32_t t = 1000;
    for (int n = 0; n < 12; n++)
    {
        char call[12];
        snprintf(call, sizeof(call), "DL1ABC-%d", n + 1);
        for (int k = 0; k < 3; k++)
            ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, call, true, 1, t++));
        ASSERT_VERDICT(RM_REJ_LOCKOUT, fromSender(s, call, false, 1, t++));
    }
}

// Switching the flag off must not leave a stale lock behind (the web and console switch takes effect
// on the next frame, no reboot, no manual unlock).
static void test_switching_off_releases_existing_locks(void)
{
    RmState s;
    strictInit(s, 0);
    strikes3(s, "DK5EN-14", 1000);
    TEST_ASSERT_TRUE(rmSenderLocked(s, "DK5EN-14", 1003));
    s.strict = false;
    ASSERT_VERDICT(RM_OK, fromSender(s, "DK5EN-14", false, 1, 1004));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(s, 1004, nullptr));
    // and switching it back on starts from a clean table
    s.strict = true;
    ASSERT_VERDICT(RM_REJ_TAG, fromSender(s, "DK5EN-14", true, 2, 1005));
    TEST_ASSERT_EQUAL_UINT8(1, ent(s, "DK5EN-14").rejCount);
}

// BF-01: the flag is console and web only. rmstrictsecurity is not on the allowlist, so a remote
// RM1 command can never switch it (a lockout switched off from the air would defeat the point).
static void test_rmstrictsecurity_is_not_allowlisted(void)
{
    static const char *const names[] = {"rmstrictsecurity", "rmstrict", "strict", "remotemgmt"};
    for (const char *n : names)
    {
        TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(n, "on", MAXTX), n);
        TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(n, "off", MAXTX), n);
        TEST_ASSERT_FALSE_MESSAGE(rmCommandAllowed(n, "", MAXTX), n);
        for (int strict = 0; strict < 2; strict++)
        {
            RmState s;
            rmStateInit(s, 0);
            s.strict = (strict != 0);
            ASSERT_VERDICT(RM_REJ_BLOCKED, chk(s, mk(1, n, "on"), 1000));
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_commands_reproduced);
    RUN_TEST(test_seen_ring_same_source_and_id_is_a_duplicate_within_the_window);
    RUN_TEST(test_seen_ring_same_id_with_other_text_is_not_a_duplicate);
    RUN_TEST(test_seen_ring_clock_read_before_the_other_task_stamped_is_still_a_duplicate);
    RUN_TEST(test_seen_ring_forgets_after_the_window_and_across_the_millis_wrap);
    RUN_TEST(test_seen_ring_replaces_the_oldest_slot_and_reset_clears_it);
    RUN_TEST(test_gate_second_copy_of_a_command_is_consumed_but_not_queued);
    RUN_TEST(test_gate_duplicates_do_not_touch_replies_or_disabled_nodes);
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
    RUN_TEST(test_sync_replay_does_not_block_commands);
    RUN_TEST(test_sync_limiter_60s);
    RUN_TEST(test_command_does_not_block_sync);
    RUN_TEST(test_sync_leaves_cache_window_alone);
    RUN_TEST(test_rate_limit_one_per_10_seconds);
    RUN_TEST(test_three_rejects_lock_out_for_5_minutes_then_recover);
    RUN_TEST(test_lockout_counts_any_reject_reason);
    RUN_TEST(test_authenticated_replay_is_not_counted);
    RUN_TEST(test_reject_window_boundary);
    RUN_TEST(test_reject_window_expires_after_90_seconds);
    RUN_TEST(test_successful_commands_do_not_clear_the_reject_count);
    RUN_TEST(test_empty_password_disables_rm);
    RUN_TEST(test_reply_shape_and_limits);
    RUN_TEST(test_reply_canonical_and_frame_are_pinned);
    RUN_TEST(test_reply_signed_verifies_and_any_byte_change_fails);
    RUN_TEST(test_accept_stores_result_and_truncates);
    RUN_TEST(test_verdict_names);
    RUN_TEST(test_reply_is_recognised_and_never_parses_as_a_command);
    RUN_TEST(test_millis_wrap_rate_cache_and_lockout);
    RUN_TEST(test_build_command_reproduces_every_vector);
    RUN_TEST(test_build_command_limits);
    RUN_TEST(test_command_allowed_predicate);
    RUN_TEST(test_allowlist_is_the_pinned_command_list);
    RUN_TEST(test_allowlist_free_text_limits);
    RUN_TEST(test_verify_reply_vectors_and_tampering);
    RUN_TEST(test_verify_reply_rejects_malformed_and_commands);
    RUN_TEST(test_reply_queue_is_separate_from_command_queue);
    RUN_TEST(test_long_result_does_not_touch_lock_state);
    RUN_TEST(test_reply_wire_is_140_and_109_is_refused);
    RUN_TEST(test_args_39_roundtrip_and_40_refused);
    RUN_TEST(test_sanitiser_vectors);
    RUN_TEST(test_allowlist_table_equivalence);
    RUN_TEST(test_ext_allowlist_accepts_and_rejects);
    RUN_TEST(test_ext_args_survive_parse_build_and_check);
    RUN_TEST(test_ext_separator_cannot_be_injected_through_args);
    RUN_TEST(test_legacy_replies_stay_within_63_and_cap_token);
    RUN_TEST(test_status_vectors_parse);
    RUN_TEST(test_receiver_unlock_clears_lock_only);
    RUN_TEST(test_lockout_is_per_sender);
    RUN_TEST(test_rejects_of_different_senders_do_not_add_up);
    RUN_TEST(test_sender_lock_expires_and_window_is_per_sender);
    RUN_TEST(test_reject_table_replacement_order);
    RUN_TEST(test_similar_calls_are_distinct_senders);
    RUN_TEST(test_uncounted_verdicts_create_no_entry);
    RUN_TEST(test_receiver_unlock_clears_every_sender);
    RUN_TEST(test_sender_lock_survives_millis_wrap);
    RUN_TEST(test_stale_lock_does_not_return_after_half_a_millis_turn);
    RUN_TEST(test_sweep_frees_expired_locks_and_windows);
    RUN_TEST(test_reject_table_odd_sender_strings);
    RUN_TEST(test_off_is_the_default_after_state_init);
    RUN_TEST(test_off_wrong_tags_from_one_sender_never_lock);
    RUN_TEST(test_off_wrong_tags_from_rotating_callsigns_never_lock);
    RUN_TEST(test_off_back_to_back_valid_commands_one_second_apart_all_execute);
    RUN_TEST(test_off_replay_bad_tag_blocked_and_format_are_still_rejected);
    RUN_TEST(test_on_rotating_callsigns_each_lock_after_three_strikes);
    RUN_TEST(test_switching_off_releases_existing_locks);
    RUN_TEST(test_rmstrictsecurity_is_not_allowlisted);
    return UNITY_END();
}
