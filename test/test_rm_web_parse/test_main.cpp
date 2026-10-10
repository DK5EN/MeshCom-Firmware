// Host test for src/web_functions/web_rm_parse.h -- pure parsing/validation of the Remote page POST
// bodies (RM GUI W2a, contract C4). Header-only, no Arduino.
//
//   tools: pio test -e native_rm_web_parse   (host only, no hardware)
//
// Layout: /rmpasswd, /rmnodes and /rmsend as accept/reject tables (percent-encoded specials, %00,
// duplicate and unknown keys, slot edge cases, password limits, body size limits, '+' literal), the
// in-place contract, and a LEAK test with a positive control (no error token or diagnostic ever
// contains the canary password).

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <remote_cmd.h>
#include <rm_sender_policy.h>
#include <web_functions/web_rm_parse.h>

static_assert(RM_FORM_ARGS_MAX == RM_MAX_ARGS, "the web form args cap must equal the wire args cap");

void setUp(void) {}
void tearDown(void) {}

// the parsers work in place: every case gets a fresh writable copy
struct Buf
{
    char b[512];
    explicit Buf(const std::string &s)
    {
        memset(b, 0, sizeof b);
        strncpy(b, s.c_str(), sizeof b - 1);
    }
};

static bool tokenIsKnown(const char *t)
{
    static const char *const ok[] = {"size", "form", "act", "slot", "call", "pw", "cmd"};
    for (const char *k : ok)
        if (strcmp(t, k) == 0)
            return true;
    return false;
}

static bool contains(const char *hay, const char *needle) { return strstr(hay, needle) != nullptr; }

// byte scan over a whole buffer (the in-place parse leaves NULs between the pairs)
static bool containsRaw(const char *buf, size_t len, const char *needle)
{
    const size_t n = strlen(needle);
    for (size_t i = 0; i + n <= len; i++)
        if (memcmp(buf + i, needle, n) == 0)
            return true;
    return false;
}

// ------------------------------------------------------------------------------------------ /rmpasswd

struct PwdCase
{
    const char *body;
    const char *err; // nullptr = accept
    bool clear;
    const char *pw;
};

static void test_passwd_table(void)
{
    static const PwdCase cases[] = {
        {"act=set&pw=hunter2", nullptr, false, "hunter2"},
        {"pw=hunter2&act=set", nullptr, false, "hunter2"}, // order does not matter
        {"act=clear", nullptr, true, ""},
        {"act=set&pw=a", nullptr, false, "a"},
        {"act=set&pw=12345678901234", nullptr, false, "12345678901234"},           // 14 bytes
        {"act=set&pw=a%20b", nullptr, false, "a b"},                               // inner space
        {"act=set&pw=a%26b%3Dc%25d", nullptr, false, "a&b=c%d"},                  // encoded & = %
        {"act=set&pw=%7e%21%22%23", nullptr, false, "~!\"#"},                      // encoded specials
        {"act=set&pw=a+b", nullptr, false, "a+b"},                                 // '+' stays literal
        {"act=set&pw=100%", nullptr, false, "100%"},                               // lone % stays literal
        {"act=set&pw=%4", nullptr, false, "%4"},                                   // truncated escape stays literal
        {"act=set&pw=a%zzb", nullptr, false, "a%zzb"},                             // non-hex escape stays literal
        // reject
        {"act=set&pw=123456789012345", RM_ERR_PW, false, ""},                      // 15 bytes
        {"act=set&pw=%20abc", RM_ERR_PW, false, ""},                               // leading space
        {"act=set&pw=abc%20", RM_ERR_PW, false, ""},                               // trailing space
        {"act=set&pw=none", RM_ERR_PW, false, ""},                                 // reserved
        {"act=set&pw=", RM_ERR_PW, false, ""},                                     // empty
        {"act=set", RM_ERR_PW, false, ""},                                         // missing
        {"act=set&pw=caf%C3%A9", RM_ERR_PW, false, ""},                            // UTF-8
        {"act=set&pw=%7f", RM_ERR_FORM, false, ""},                                // DEL
        {"act=set&pw=a%00b", RM_ERR_FORM, false, ""},                              // NUL
        {"act=set&pw=a%0ab", RM_ERR_FORM, false, ""},                              // LF
        {"act=set&pw=a%0Db", RM_ERR_FORM, false, ""},                              // CR
        {"act=set&pw=a\tb", RM_ERR_FORM, false, ""},                               // raw control byte
        {"act=clear&pw=hunter2", RM_ERR_FORM, false, ""},                          // password in a clear
        {"act=wipe&pw=hunter2", RM_ERR_ACT, false, ""},
        {"act=SET&pw=hunter2", RM_ERR_ACT, false, ""},                             // case-sensitive
        {"act=&pw=hunter2", RM_ERR_ACT, false, ""},
        {"pw=hunter2", RM_ERR_ACT, false, ""},
        {"act=set&act=clear&pw=x", RM_ERR_FORM, false, ""},                        // duplicate key
        {"act=set&pw=x&pw=y", RM_ERR_FORM, false, ""},
        {"act=set&pw=x&extra=1", RM_ERR_FORM, false, ""},                          // unknown key
        {"act=set&PW=x", RM_ERR_FORM, false, ""},                                  // keys are case-sensitive
        {"act=set&pw=x&", RM_ERR_FORM, false, ""},                                 // empty pair
        {"act=set&&pw=x", RM_ERR_FORM, false, ""},
        {"act=set&pw", RM_ERR_FORM, false, ""},                                    // no '='
        {"=set", RM_ERR_FORM, false, ""},                                          // empty key
        {"", RM_ERR_FORM, false, ""},
        {"&", RM_ERR_FORM, false, ""},
    };
    for (const PwdCase &c : cases)
    {
        Buf b(c.body);
        RmPwdReq r = rmParsePasswdBody(b.b);
        char msg[300];
        snprintf(msg, sizeof msg, "body '%s'", c.body);
        if (c.err == nullptr)
        {
            TEST_ASSERT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(c.clear ? 1 : 0, r.clear ? 1 : 0, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.pw, r.pw, msg);
        }
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.err, r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.pw, msg);
            TEST_ASSERT_FALSE_MESSAGE(r.clear, msg);
        }
    }
}

static void test_passwd_null_body(void)
{
    RmPwdReq r = rmParsePasswdBody(nullptr);
    TEST_ASSERT_EQUAL_STRING(RM_ERR_FORM, r.err);
}

// ------------------------------------------------------------------------------------------ /rmnodes

struct NodesCase
{
    const char *body;
    const char *err; // nullptr = accept
    RmNodesAct act;
    int slot;
    const char *call;
    const char *pw;
};

static void test_nodes_table(void)
{
    static const NodesCase cases[] = {
        {"act=save&slot=0&call=DK5EN-1&pw=hunter2", nullptr, RMN_SAVE, 0, "DK5EN-1", "hunter2"},
        {"act=save&slot=2&call=dk5en-12&pw=hunter2", nullptr, RMN_SAVE, 2, "DK5EN-12", "hunter2"}, // upper-cased
        {"act=save&slot=%31&call=OE1ABC-15&pw=a%20b", nullptr, RMN_SAVE, 1, "OE1ABC-15", "a b"},  // %31 = '1'
        {"slot=1&pw=x&act=save&call=DL1AB-3", nullptr, RMN_SAVE, 1, "DL1AB-3", "x"},               // any order
        {"act=del&slot=0", nullptr, RMN_DEL, 0, "", ""},
        {"act=del&slot=2", nullptr, RMN_DEL, 2, "", ""},
        {"act=forget", nullptr, RMN_FORGET, -1, "", ""},
        // slot
        {"act=save&slot=3&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&slot=-1&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&slot=1a&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&slot=01&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&slot=%201&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&slot=&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=save&call=DK5EN-1&pw=x", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=del&slot=9", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=del&slot=1.0", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        {"act=del", RM_ERR_SLOT, RMN_NONE, -1, "", ""},
        // call
        {"act=save&slot=0&call=DK5EN&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},                    // no SSID
        {"act=save&slot=0&call=DK5EN-123&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},                // SSID 3 digits
        {"act=save&slot=0&call=DK5EN-1%2F&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},               // '/'
        {"act=save&slot=0&call=OE1ABC-123456&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},            // too long
        {"act=save&slot=0&call=&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK%C3%84-1&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},               // UTF-8
        {"act=save&slot=0&call=DK5EN-1%20&pw=x", RM_ERR_CALL, RMN_NONE, -1, "", ""},               // trailing space
        // password
        {"act=save&slot=0&call=DK5EN-1&pw=123456789012345", RM_ERR_PW, RMN_NONE, -1, "", ""},      // 15 bytes
        {"act=save&slot=0&call=DK5EN-1&pw=12345678901234", nullptr, RMN_SAVE, 0, "DK5EN-1", "12345678901234"},
        {"act=save&slot=0&call=DK5EN-1&pw=%20x", RM_ERR_PW, RMN_NONE, -1, "", ""},                 // leading space
        {"act=save&slot=0&call=DK5EN-1&pw=x%20", RM_ERR_PW, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK5EN-1&pw=none", RM_ERR_PW, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK5EN-1&pw=", RM_ERR_PW, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK5EN-1", RM_ERR_PW, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK5EN-1&pw=a%00b", RM_ERR_FORM, RMN_NONE, -1, "", ""},             // %00
        // act and structure
        {"act=erase&slot=0", RM_ERR_ACT, RMN_NONE, -1, "", ""},
        {"slot=0", RM_ERR_ACT, RMN_NONE, -1, "", ""},
        {"act=del&slot=0&pw=x", RM_ERR_FORM, RMN_NONE, -1, "", ""},                                // stray key
        {"act=del&slot=0&call=DK5EN-1", RM_ERR_FORM, RMN_NONE, -1, "", ""},
        {"act=forget&slot=0", RM_ERR_FORM, RMN_NONE, -1, "", ""},
        {"act=forget&pw=x", RM_ERR_FORM, RMN_NONE, -1, "", ""},
        {"act=save&act=del&slot=0", RM_ERR_FORM, RMN_NONE, -1, "", ""},                            // duplicate
        {"act=save&slot=0&slot=1&call=DK5EN-1&pw=x", RM_ERR_FORM, RMN_NONE, -1, "", ""},
        {"act=save&slot=0&call=DK5EN-1&pw=x&dst=DK5EN-2", RM_ERR_FORM, RMN_NONE, -1, "", ""},     // unknown
        {"", RM_ERR_FORM, RMN_NONE, -1, "", ""},
    };
    for (const NodesCase &c : cases)
    {
        Buf b(c.body);
        RmNodesReq r = rmParseNodesBody(b.b);
        char msg[300];
        snprintf(msg, sizeof msg, "body '%s'", c.body);
        if (c.err == nullptr)
        {
            TEST_ASSERT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.act, (int)r.act, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(c.slot, r.slot, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.call, r.call, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.pw, r.pw, msg);
        }
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.err, r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)RMN_NONE, (int)r.act, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.call, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.pw, msg);
        }
    }
}

// ------------------------------------------------------------------------------------------ /rmsend

struct SendCase
{
    const char *body;
    const char *err; // nullptr = accept
    int slot;
    const char *dst;
    const char *pw;
    const char *cmd;
    const char *args;
};

// optional force=1|0 field (both forms); anything else is the "form" error and force stays false
struct ForceCase
{
    const char *body;
    const char *err; // nullptr = accept
    bool force;
};

static void test_send_force_field(void)
{
    static const ForceCase cases[] = {
        // slot form
        {"slot=0&cmd=gps", nullptr, false},                  // absent
        {"slot=0&cmd=gps&force=1", nullptr, true},
        {"slot=0&cmd=gps&force=0", nullptr, false},
        {"force=1&slot=2&cmd=txpower&args=7", nullptr, true}, // field order is free
        {"slot=0&cmd=gps&force=2", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=1&force=1", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=0&force=1", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=true", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=01", RM_ERR_FORM, false},
        {"slot=0&cmd=gps&force=%31", nullptr, true},         // decoded before the value check
        // dst form
        {"dst=DK5EN-1&pw=x&cmd=gps", nullptr, false},
        {"dst=DK5EN-1&pw=x&cmd=gps&args=on&force=1", nullptr, true},
        {"dst=DK5EN-1&pw=x&cmd=gps&force=0", nullptr, false},
        {"dst=DK5EN-1&pw=x&cmd=gps&force=2", RM_ERR_FORM, false},
        {"dst=DK5EN-1&pw=x&cmd=gps&force=", RM_ERR_FORM, false},
        {"dst=DK5EN-1&pw=x&cmd=gps&force=1&force=1", RM_ERR_FORM, false},
        // a bad force wins over a later field error (form-level check), a good force never hides one
        {"slot=3&cmd=gps&force=1", RM_ERR_SLOT, false},
        {"dst=DK5EN&pw=x&cmd=gps&force=1", RM_ERR_CALL, false},
    };
    for (const ForceCase &c : cases)
    {
        Buf b(c.body);
        RmSendReq r = rmParseSendBody(b.b);
        char msg[300];
        snprintf(msg, sizeof msg, "body '%s'", c.body);
        if (c.err == nullptr)
            TEST_ASSERT_NULL_MESSAGE(r.err, msg);
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.err, r.err, msg);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(c.force ? 1 : 0, r.force ? 1 : 0, msg);
    }
}

static void test_send_call_guard_and_args_case(void)
{
    struct CallCase
    {
        const char *body;
        const char *err; // nullptr = ok
        int slot;
        const char *call; // expected r.call ("" when absent / on error)
        const char *cmd;
        const char *args;
    };
    const CallCase cases[] = {
        // call present, case folded like dst
        {"slot=1&cmd=reboot&call=DK5EN-12", nullptr, 1, "DK5EN-12", "reboot", ""},
        {"slot=1&cmd=reboot&call=dk5en-12", nullptr, 1, "DK5EN-12", "reboot", ""},
        {"slot=2&cmd=reboot&call=%64k5en-12&force=1", nullptr, 2, "DK5EN-12", "reboot", ""}, // %64 = 'd'
        {"call=OE1ABC-15&slot=0&cmd=gps&args=on", nullptr, 0, "OE1ABC-15", "gps", "on"},     // field order free
        // absent: today's behaviour, call is ""
        {"slot=1&cmd=reboot", nullptr, 1, "", "reboot", ""},
        // empty, duplicate, too long, invalid characters: "call" / "form"
        {"slot=1&cmd=reboot&call=", RM_ERR_CALL, -1, "", "", ""},
        {"slot=1&cmd=reboot&call=DK5EN-1&call=DK5EN-1", RM_ERR_FORM, -1, "", "", ""},
        {"slot=1&cmd=reboot&call=DK5EN-1234", RM_ERR_CALL, -1, "", "", ""},   // 10 > RM_CALL_MAX
        {"slot=1&cmd=reboot&call=DK5%20N-1", RM_ERR_CALL, -1, "", "", ""},    // space
        {"slot=1&cmd=reboot&call=DK5EN%2F1", RM_ERR_CALL, -1, "", "", ""},    // '/'
        {"slot=1&cmd=reboot&call=%3Cb%3E", RM_ERR_CALL, -1, "", "", ""},      // <b>
        {"slot=1&cmd=reboot&call=DK5EN%00", RM_ERR_FORM, -1, "", "", ""},     // control byte
        // call with the dst form is two forms in one request
        {"dst=DK5EN-1&pw=x&cmd=reboot&call=DK5EN-1", RM_ERR_FORM, -1, "", "", ""},
        // a bad slot still wins by its own token; call does not hide a cmd error
        {"slot=3&cmd=reboot&call=DK5EN-1", RM_ERR_SLOT, -1, "", "", ""},
        {"slot=1&cmd=&call=DK5EN-1", RM_ERR_CMD, -1, "", "", ""},
        // args case: as typed on the wire, cmd still folded
        {"slot=0&cmd=gps&args=Martin", nullptr, 0, "", "gps", "Martin"},
        {"slot=0&cmd=gps&args=%4Dartin", nullptr, 0, "", "gps", "Martin"},
        {"slot=0&cmd=GPS&args=ON", nullptr, 0, "", "gps", "ON"},
        {"dst=dk5en-1&pw=x&cmd=SetName&args=Hans%20MuLLer", nullptr, -1, "", "setname", "Hans MuLLer"},
    };
    for (const CallCase &c : cases)
    {
        Buf b(c.body);
        RmSendReq r = rmParseSendBody(b.b);
        char msg[300];
        snprintf(msg, sizeof msg, "body '%s'", c.body);
        if (c.err == nullptr)
        {
            TEST_ASSERT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(c.slot, r.slot, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.call, r.call, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.cmd, r.cmd, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.args, r.args, msg);
        }
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.err, r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, r.slot, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.call, msg); // no input byte on the error path
        }
    }
}

static void test_send_worst_case_slot_body_fits(void)
{
    // slot form, every field fully %-encoded: slot + cmd(15) + args(39) + force + call(9)
    std::string s = "slot=0&cmd=";
    for (int i = 0; i < RM_FORM_CMD_MAX; i++)
        s += "%61";
    s += "&args=";
    for (int i = 0; i < RM_FORM_ARGS_MAX; i++)
        s += "%41";
    s += "&force=1&call=";
    s += "%4F%45%31%41%42%43%2D%31%35"; // "OE1ABC-15", the longest valid call
    TEST_ASSERT_TRUE_MESSAGE(s.size() <= (size_t)RM_FORM_BODY_MAX, "worst-case slot form must fit RM_FORM_BODY_MAX");
    Buf b(s.c_str());
    RmSendReq r = rmParseSendBody(b.b);
    TEST_ASSERT_NULL(r.err);
    TEST_ASSERT_EQUAL_INT(39, (int)strlen(r.args));
    TEST_ASSERT_EQUAL_STRING("OE1ABC-15", r.call);
}

static void test_call_eq(void)
{
    TEST_ASSERT_TRUE(rmCallEq("DK5EN-12", "dk5en-12"));
    TEST_ASSERT_TRUE(rmCallEq("dk5en-12", "DK5EN-12"));
    TEST_ASSERT_TRUE(rmCallEq("OE1ABC-15", "OE1ABC-15"));
    TEST_ASSERT_FALSE(rmCallEq("DK5EN-1", "DK5EN-12")); // prefix
    TEST_ASSERT_FALSE(rmCallEq("DK5EN-12", "DK5EN-1"));
    TEST_ASSERT_FALSE(rmCallEq("DK5EN-12", "DK5EN-13"));
    TEST_ASSERT_FALSE(rmCallEq("", "DK5EN-1"));
    TEST_ASSERT_TRUE(rmCallEq("", ""));
    TEST_ASSERT_FALSE(rmCallEq(nullptr, "A"));
    TEST_ASSERT_FALSE(rmCallEq("A", nullptr));
}


static void test_send_table(void)
{
    static const SendCase cases[] = {
        // dst form (as today)
        {"dst=dk5en-1&pw=hunter2&cmd=reboot&args=", nullptr, -1, "DK5EN-1", "hunter2", "reboot", ""},
        {"dst=DK5EN-1&pw=hunter2&cmd=reboot", nullptr, -1, "DK5EN-1", "hunter2", "reboot", ""},        // args optional
        {"dst=DK5EN-1&pw=a%20b%26c&cmd=TXPOWER&args=10", nullptr, -1, "DK5EN-1", "a b&c", "txpower", "10"},
        {"dst=DK5EN-1&pw=a+b&cmd=gps&args=ON", nullptr, -1, "DK5EN-1", "a+b", "gps", "ON"},             // '+' literal; args keep their case (W2-D)
        {"cmd=gps&args=on%20x&dst=DK5EN-1&pw=x", nullptr, -1, "DK5EN-1", "x", "gps", "on x"},
        // trailing space in the dst-form password is rmSendCommand()'s business (it strips), not ours
        {"dst=DK5EN-1&pw=abc%20&cmd=gps", nullptr, -1, "DK5EN-1", "abc ", "gps", ""},
        // slot form
        {"slot=0&cmd=Status&args=", nullptr, 0, "", "", "status", ""},
        {"slot=2&cmd=txpower&args=7", nullptr, 2, "", "", "txpower", "7"},
        {"slot=%32&cmd=gps", nullptr, 2, "", "", "gps", ""},
        // slot rejects
        {"slot=3&cmd=status", RM_ERR_SLOT, -1, "", "", "", ""},
        {"slot=-1&cmd=status", RM_ERR_SLOT, -1, "", "", "", ""},
        {"slot=1a&cmd=status", RM_ERR_SLOT, -1, "", "", "", ""},
        {"slot=&cmd=status", RM_ERR_SLOT, -1, "", "", "", ""},
        {"slot=1&dst=DK5EN-1&cmd=status", RM_ERR_FORM, -1, "", "", "", ""},                             // two forms
        {"slot=1&pw=x&cmd=status", RM_ERR_FORM, -1, "", "", "", ""},
        // dst form rejects
        {"dst=DK5EN&pw=x&cmd=gps", RM_ERR_CALL, -1, "", "", "", ""},
        {"dst=&pw=x&cmd=gps", RM_ERR_CALL, -1, "", "", "", ""},
        {"pw=x&cmd=gps", RM_ERR_CALL, -1, "", "", "", ""},
        {"dst=OE1ABC-123456&pw=x&cmd=gps", RM_ERR_CALL, -1, "", "", "", ""},
        {"dst=DK5EN-1&cmd=gps", RM_ERR_PW, -1, "", "", "", ""},
        {"dst=DK5EN-1&pw=&cmd=gps", RM_ERR_PW, -1, "", "", "", ""},
        {"dst=DK5EN-1&pw=a%00b&cmd=gps", RM_ERR_FORM, -1, "", "", "", ""},
        // cmd/args
        {"slot=0", RM_ERR_CMD, -1, "", "", "", ""},
        {"slot=0&cmd=", RM_ERR_CMD, -1, "", "", "", ""},
        {"slot=0&cmd=1234567890123456", RM_ERR_CMD, -1, "", "", "", ""},                                // 16 bytes
        {"slot=0&cmd=status&args=1234567890123456789012345678901234567890", RM_ERR_CMD, -1, "", "", "", ""}, // 40 bytes
        {"slot=0&cmd=123456789012345", nullptr, 0, "", "", "123456789012345", ""},                      // 15 bytes
        {"slot=0&cmd=gps&args=12345678901234567890123", nullptr, 0, "", "", "gps", "12345678901234567890123"},   // 23 bytes
        {"slot=0&cmd=gps&args=123456789012345678901234567890123456789", nullptr, 0, "", "", "gps", "123456789012345678901234567890123456789"}, // 39 bytes (RM_FORM_ARGS_MAX)
        {"slot=0&cmd=gps%7f", RM_ERR_FORM, -1, "", "", "", ""},
        {"slot=0&cmd=gps&args=on%00", RM_ERR_FORM, -1, "", "", "", ""},
        {"slot=0&cmd=caf%C3%A9", RM_ERR_CMD, -1, "", "", "", ""},                                       // high bytes
        // structure
        {"slot=0&cmd=gps&cmd=mesh", RM_ERR_FORM, -1, "", "", "", ""},
        {"slot=0&slot=1&cmd=gps", RM_ERR_FORM, -1, "", "", "", ""},
        {"slot=0&cmd=gps&extra=1", RM_ERR_FORM, -1, "", "", "", ""},
        {"slot=0&cmd=gps&", RM_ERR_FORM, -1, "", "", "", ""},
        {"cmd=gps", RM_ERR_CALL, -1, "", "", "", ""},
        {"", RM_ERR_FORM, -1, "", "", "", ""},
    };
    for (const SendCase &c : cases)
    {
        Buf b(c.body);
        RmSendReq r = rmParseSendBody(b.b);
        char msg[300];
        snprintf(msg, sizeof msg, "body '%s'", c.body);
        if (c.err == nullptr)
        {
            TEST_ASSERT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(c.slot, r.slot, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.dst, r.dst, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.pw, r.pw, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.cmd, r.cmd, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.args, r.args, msg);
        }
        else
        {
            TEST_ASSERT_NOT_NULL_MESSAGE(r.err, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.err, r.err, msg);
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, r.slot, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.dst, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.pw, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.cmd, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.args, msg);
        }
    }
}

// ------------------------------------------------------------------------------------------ size

static void test_body_size_limits(void)
{
    // exactly RM_FORM_BODY_MAX bytes is parsed, one more is "size" -- on all three parsers
    std::string head = "act=set&pw=x&";
    std::string pad200 = std::string(RM_FORM_BODY_MAX, 'a');
    {
        std::string ok = "act=clear";
        Buf b(ok);
        TEST_ASSERT_NULL(rmParsePasswdBody(b.b).err);
    }
    {
        std::string s = "act=set&pw=x&z=" + std::string(RM_FORM_BODY_MAX - 15, 'a'); // 200 bytes, unknown key
        TEST_ASSERT_EQUAL_INT((int)RM_FORM_BODY_MAX, (int)s.size());
        Buf b(s);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_FORM, rmParsePasswdBody(b.b).err); // parsed (not "size"), rejected as unknown key
        std::string s2 = s + "a"; // 201
        Buf b2(s2);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_SIZE, rmParsePasswdBody(b2.b).err);
        Buf b3(s2);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_SIZE, rmParseNodesBody(b3.b).err);
        Buf b4(s2);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_SIZE, rmParseSendBody(b4.b).err);
    }
    {
        // 200 bytes, valid slot send, long args are cut by the 23 byte rule not by the body size
        std::string s = "slot=0&cmd=gps&args=" + std::string(23, 'a');
        Buf b(s);
        RmSendReq r = rmParseSendBody(b.b);
        TEST_ASSERT_NULL(r.err);
        TEST_ASSERT_EQUAL_INT(23, (int)strlen(r.args));
    }
    {
        // a 200 byte password field is rejected as a password, never truncated to a valid one
        std::string s = "act=set&pw=" + std::string(100, 'a');
        Buf b(s);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_PW, rmParsePasswdBody(b.b).err);
    }
    {
        // percent-encoding does not smuggle an over-long password past the 14 byte rule
        std::string s = "act=set&pw=";
        for (int i = 0; i < 15; i++)
            s += "%61";
        Buf b(s);
        TEST_ASSERT_EQUAL_STRING(RM_ERR_PW, rmParsePasswdBody(b.b).err);
    }
}

// ------------------------------------------------------------------------------------------ in place

static void test_values_point_into_body(void)
{
    Buf b("act=set&pw=hunter2");
    RmPwdReq r = rmParsePasswdBody(b.b);
    TEST_ASSERT_NULL(r.err);
    TEST_ASSERT_TRUE(r.pw >= b.b && r.pw < b.b + sizeof b.b); // no copy anywhere else
    Buf n("act=save&slot=1&call=dk5en-1&pw=x");
    RmNodesReq q = rmParseNodesBody(n.b);
    TEST_ASSERT_NULL(q.err);
    TEST_ASSERT_TRUE(q.call >= n.b && q.call < n.b + sizeof n.b);
    TEST_ASSERT_TRUE(q.pw >= n.b && q.pw < n.b + sizeof n.b);
}

// ------------------------------------------------------------------------------------------ leak

#define CANARY "Zq7!canaryPW" // 12 bytes, a valid password

// every parse with the canary in some field: the returned tokens are static members of the closed set
// and never contain it; positive control below proves the check can fail.
static void test_leak_error_tokens_never_contain_canary(void)
{
    static const char *const bodies[] = {
        // canary valid, request rejected for another reason
        "act=set&pw=" CANARY "&extra=1",
        "act=set&pw=" CANARY "&pw=" CANARY,
        "act=clear&pw=" CANARY,
        "act=wipe&pw=" CANARY,
        "act=save&slot=3&call=DK5EN-1&pw=" CANARY,
        "act=save&slot=0&call=BAD&pw=" CANARY,
        "act=del&slot=0&pw=" CANARY,
        "act=forget&pw=" CANARY,
        "dst=DK5EN-1&pw=" CANARY "&cmd=",
        "dst=BAD&pw=" CANARY "&cmd=gps",
        "slot=1&pw=" CANARY "&cmd=gps",
        "slot=9&cmd=" CANARY,
        // canary itself the reason
        "act=set&pw=" CANARY "%20",
        "act=set&pw=%20" CANARY,
        "act=set&pw=" CANARY "%00",
        "act=set&pw=" CANARY CANARY,
        "act=save&slot=0&call=DK5EN-1&pw=" CANARY CANARY,
        "act=save&slot=0&call=" CANARY "&pw=x",
        "act=save&slot=" CANARY "&call=DK5EN-1&pw=x",
        "act=" CANARY "&pw=x",
        "unknown" CANARY "=1",
        CANARY,
        "dst=DK5EN-1&pw=" CANARY "&cmd=" CANARY,
        "slot=0&cmd=gps&args=" CANARY CANARY CANARY,
    };
    int rejected = 0;
    for (const char *src : bodies)
    {
        const char *tokens[3];
        Buf b1(src), b2(src), b3(src);
        RmPwdReq p = rmParsePasswdBody(b1.b);
        RmNodesReq n = rmParseNodesBody(b2.b);
        RmSendReq s = rmParseSendBody(b3.b);
        tokens[0] = p.err;
        tokens[1] = n.err;
        tokens[2] = s.err;
        for (int i = 0; i < 3; i++)
        {
            if (tokens[i] == nullptr)
                continue; // an accepted request legitimately holds the password in the BODY buffer
            rejected++;
            char msg[300];
            snprintf(msg, sizeof msg, "body '%s' parser %d token '%s'", src, i, tokens[i]);
            TEST_ASSERT_TRUE_MESSAGE(tokenIsKnown(tokens[i]), msg);
            TEST_ASSERT_FALSE_MESSAGE(contains(tokens[i], CANARY), msg);
            TEST_ASSERT_FALSE_MESSAGE(contains(tokens[i], "canary"), msg);
        }
        // a failed parse points every value at the static empty string, not into the body
        if (p.err != nullptr)
            TEST_ASSERT_EQUAL_STRING("", p.pw);
        if (n.err != nullptr)
        {
            TEST_ASSERT_EQUAL_STRING("", n.call);
            TEST_ASSERT_EQUAL_STRING("", n.pw);
        }
        if (s.err != nullptr)
        {
            TEST_ASSERT_EQUAL_STRING("", s.dst);
            TEST_ASSERT_EQUAL_STRING("", s.pw);
            TEST_ASSERT_EQUAL_STRING("", s.cmd);
            TEST_ASSERT_EQUAL_STRING("", s.args);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(rejected > 40, "the table must exercise many rejects");
}

// positive control: the same canary check FAILS (finds it) where the password really is -- the accepted
// parse result and a deliberately leaky error string. Without this the leak test could never fail.
static void test_leak_positive_control(void)
{
    Buf b("act=set&pw=" CANARY);
    RmPwdReq r = rmParsePasswdBody(b.b);
    TEST_ASSERT_NULL(r.err);
    TEST_ASSERT_TRUE(contains(r.pw, CANARY));  // the accepted value is in the body buffer ...
    TEST_ASSERT_TRUE(containsRaw(b.b, sizeof b.b, CANARY)); // whole buffer: the parse put NULs between the pairs
    // ... and a diagnostic that echoes input is caught by the same predicate
    char leaky[64];
    snprintf(leaky, sizeof leaky, "bad pw: %s", r.pw);
    TEST_ASSERT_TRUE(contains(leaky, CANARY));
    // after the caller's wipe (volatile stores, like rm_wipe) nothing is left in the buffer
    volatile unsigned char *v = (volatile unsigned char *)b.b;
    for (size_t i = 0; i < sizeof b.b; i++)
        v[i] = 0;
    TEST_ASSERT_FALSE(containsRaw(b.b, sizeof b.b, CANARY));
}

// ------------------------------------------------------------------------------------------ messages

// DRY-02: the page shows the server's `msg`, so every token the web handlers can answer with must have
// its own sentence in rmTokenTable (not the generic fallback). The list is the closed set of the parser
// (RM_ERR_*), rmReadBody ("size", "short") and the /rmnodes handler ("store", "dup"), plus the "send"
// fallback of /rmsend. A token added to the parser without a table row fails here.
static void test_every_web_error_token_has_a_server_sentence(void)
{
    const char *const toks[] = {"size", "form", "act", "slot", "call", "pw", "cmd", "short", "store", "dup", "send"};
    const char *generic = rmErrTokenMessage("no such token at all");
    for (const char *t : toks)
    {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(rmErrTokenMessage(t), generic) != 0, t);
        // the token is a whole word of the table, so the sentence is the one the JSON `msg` carries
        TEST_ASSERT_TRUE_MESSAGE(strlen(rmErrTokenMessage(t)) < 190, t); // rm_json_str caps a string near 190
    }
    // and the parser itself only ever answers with tokens of that list (tokenIsKnown)
    Buf b("act=set&pw=");
    RmPwdReq r = rmParsePasswdBody(b.b);
    TEST_ASSERT_NOT_NULL(r.err);
    TEST_ASSERT_TRUE(tokenIsKnown(r.err));
    TEST_ASSERT_TRUE(strcmp(rmErrTokenMessage(r.err), generic) != 0);
}

// ------------------------------------------------------------------------------------------ main

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_passwd_table);
    RUN_TEST(test_passwd_null_body);
    RUN_TEST(test_nodes_table);
    RUN_TEST(test_send_table);
    RUN_TEST(test_send_force_field);
    RUN_TEST(test_send_call_guard_and_args_case);
    RUN_TEST(test_send_worst_case_slot_body_fits);
    RUN_TEST(test_call_eq);
    RUN_TEST(test_body_size_limits);
    RUN_TEST(test_values_point_into_body);
    RUN_TEST(test_leak_error_tokens_never_contain_canary);
    RUN_TEST(test_leak_positive_control);
    RUN_TEST(test_every_web_error_token_has_a_server_sentence);
    return UNITY_END();
}
