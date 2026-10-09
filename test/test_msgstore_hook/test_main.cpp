// Native tests for src/msgstore_hook.h (SNF-GW-01): the shared store/ack
// decision of OnRxDone and the GATE handlers, and the source-path call match
// used by the echo guard.
//
//   pio test -e native_msgstore_hook

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <msgstore_hook.h>

void setUp(void) {}
void tearDown(void) {}

struct ClassCase
{
    const char *name;
    const char *src;
    const char *dst;
    const char *payload;
    bool isGroup, peer, pnRepeat, eligible;
    MboxAction action;
    uint16_t nnn;
    size_t textLen;
};

// Baseline: an eligible DM "Hallo{123" -> STORE, nnn 123, text length 5.
static const ClassCase kCases[] = {
    // --- STORE
    { "plain DM",              "DK5EN-1", "DK5EN",  "Hallo{123",        false, false, false, true,  MBOX_STORE, 123, 5 },
    { "DM with SSID dst",      "DK5EN-1", "DK5EN-7","Hi{1",             false, false, false, true,  MBOX_STORE, 1,   2 },
    { "text contains colon",   "DK5EN-1", "DK5EN",  "a:b c{999",        false, false, false, true,  MBOX_STORE, 999, 5 },
    { "one-char text",         "DK5EN-1", "DK5EN",  "x{5",              false, false, false, true,  MBOX_STORE, 5,   1 },
    { "nnn trailing non-digit","DK5EN-1", "DK5EN",  "Hallo{42abc",      false, false, false, true,  MBOX_STORE, 42,  5 },
    { "RM1 not at start",      "DK5EN-1", "DK5EN",  "xRM1 foo{7",       false, false, false, true,  MBOX_STORE, 7,   8 },
    { "RM1 without space",     "DK5EN-1", "DK5EN",  "RM1x{7",           false, false, false, true,  MBOX_STORE, 7,   4 },
    { "rej at index 0 only",   "DK5EN-1", "DK5EN",  ":rej{7",           false, false, false, true,  MBOX_STORE, 7,   4 },
    // --- exclusions
    { "broadcast *",           "DK5EN-1", "*",      "Hallo{123",        false, false, false, true,  MBOX_NONE,  0,   0 },
    { "group dst",             "DK5EN-1", "9",      "Hallo{123",        true,  false, false, true,  MBOX_NONE,  0,   0 },
    { ":rej reject frame",     "DK5EN-1", "DK5EN",  "Hallo :rej{123",   false, false, false, true,  MBOX_NONE,  0,   0 },
    { "{ping} control",        "DK5EN-1", "DK5EN",  "{ping}",           false, false, false, true,  MBOX_NONE,  0,   0 },
    { "{MCP} control",         "DK5EN-1", "DK5EN",  "{MCP}x{12",        false, false, false, true,  MBOX_NONE,  0,   0 },
    { "{ prefix only",         "DK5EN-1", "DK5EN",  "{1",               false, false, false, true,  MBOX_NONE,  0,   0 },
    { "peer delivery",         "DK5EN-1", "DK5EN",  "Hallo{123",        false, true,  false, true,  MBOX_NONE,  0,   0 },
    { "PN repeat",             "DK5EN-1", "DK5EN",  "Hallo{123",        false, false, true,  true,  MBOX_NONE,  0,   0 },
    { "not eligible",          "DK5EN-1", "DK5EN",  "Hallo{123",        false, false, false, false, MBOX_NONE,  0,   0 },
    { "RM1 command DM",        "DK5EN-1", "DK5EN",  "RM1 reboot{123",   false, false, false, true,  MBOX_NONE,  0,   0 },
    { "missing {NNN",          "DK5EN-1", "DK5EN",  "Hallo ohne tag",   false, false, false, true,  MBOX_NONE,  0,   0 },
    { "empty payload",         "DK5EN-1", "DK5EN",  "",                 false, false, false, true,  MBOX_NONE,  0,   0 },
    // --- ACK (no eligibility / group / peer / pn tests, as in OnRxDone today)
    { "ack plain",             "DK5EN-1", "DK5EN",  "x:ack123",         false, false, false, true,  MBOX_ACK,   123, 0 },
    { "ack not eligible",      "DK5EN-1", "DK5EN",  "x:ack77",          false, false, false, false, MBOX_ACK,   77,  0 },
    { "ack to group",          "DK5EN-1", "9",      "x:ack9",           true,  false, false, false, MBOX_ACK,   9,   0 },
    { "ack to broadcast",      "DK5EN-1", "*",      "x:ack5",           false, false, false, false, MBOX_ACK,   5,   0 },
    { "ack with peer+pn",      "DK5EN-1", "DK5EN",  "x:ack88",          false, true,  true,  true,  MBOX_ACK,   88,  0 },
    { "ack with { prefix",     "DK5EN-1", "DK5EN",  "{x:ack4",          false, false, false, true,  MBOX_ACK,   4,   0 },
    { "ack with RM1 prefix",   "DK5EN-1", "DK5EN",  "RM1 :ack6",        false, false, false, true,  MBOX_ACK,   6,   0 },
    { "ack no digits",         "DK5EN-1", "DK5EN",  "x:ack",            false, false, false, true,  MBOX_ACK,   0,   0 },
    { "ack beats store tag",   "DK5EN-1", "DK5EN",  "x:ack12{34",       false, false, false, true,  MBOX_ACK,   12,  0 },
    // :ack at index 0 is not an ack today; falls through to the DM rules.
    { "ack at index 0",        "DK5EN-1", "DK5EN",  ":ack12{34",        false, false, false, true,  MBOX_STORE, 34,  6 },
    { "ack at index 0 no tag", "DK5EN-1", "DK5EN",  ":ack12",           false, false, false, true,  MBOX_NONE,  0,   0 },
    // --- WLNK-1 / APRS2SOTA service calls
    { "a: ack to WLNK-1",      "HB9VQQ-1","WLNK-1", "ack1944",          false, false, false, true,  MBOX_ACK,   1944, 0 },
    { "a: ack0007 zero-padded", "HB9VQQ-1","WLNK-1", "ack0007",          false, false, false, true,  MBOX_ACK,   7,   0 },
    { "a: ack WLNK-1 not elig","HB9VQQ-1","WLNK-1", "ack7",             false, false, false, false, MBOX_ACK,   7,   0 },
    { "b: src WLNK-1",         "WLNK-1",  "HB9VQQ-1","Login [356]:{1944",false, false, false, true,  MBOX_NONE,  0,   0 },
    { "c: dst WLNK-1",         "HB9VQQ-1","WLNK-1", "xyz{123",          false, false, false, true,  MBOX_NONE,  0,   0 },
    { "d: src APRS2SOTA",      "APRS2SOTA","HB9VQQ-1","xyz{123",        false, false, false, true,  MBOX_NONE,  0,   0 },
    { "d: dst APRS2SOTA",      "HB9VQQ-1","APRS2SOTA","xyz{123",        false, false, false, true,  MBOX_NONE,  0,   0 },
    { "d: ack1944 to APRS2SOTA","HB9VQQ-1","APRS2SOTA","ack1944",       false, false, false, true,  MBOX_NONE,  0,   0 },
    { "e: ack123 to normal dst","HB9VQQ-1","DK5EN-1","ack123",          false, false, false, true,  MBOX_NONE,  0,   0 },
    { "f: ack no digits",      "HB9VQQ-1","WLNK-1", "ack",              false, false, false, true,  MBOX_NONE,  0,   0 },
    { "f: ack12x",             "HB9VQQ-1","WLNK-1", "ack12x",           false, false, false, true,  MBOX_NONE,  0,   0 },
    { "f: xack12",             "HB9VQQ-1","WLNK-1", "xack12",           false, false, false, true,  MBOX_NONE,  0,   0 },
    { "wlnk-1 lower dst stores","HB9VQQ-1","wlnk-1","xyz{123",          false, false, false, true,  MBOX_STORE, 123, 3 },
};

static void test_classify_table(void)
{
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
    {
        const ClassCase &c = kCases[i];
        MboxDecision d = mboxClassify(c.src, c.dst, c.payload, c.isGroup, c.peer, c.pnRepeat, c.eligible);
        char msg[96];
        snprintf(msg, sizeof(msg), "case '%s' action", c.name);
        TEST_ASSERT_EQUAL_MESSAGE((int)c.action, (int)d.action, msg);
        snprintf(msg, sizeof(msg), "case '%s' nnn", c.name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(c.nnn, d.nnn, msg);
        snprintf(msg, sizeof(msg), "case '%s' textLen", c.name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)c.textLen, (uint32_t)d.textLen, msg);
    }
}

static void test_rm1_prefix_never_stored(void)
{
    // Regression for the one new exclusion: remote-management command DMs
    // (concept section 6) must never enter the mailbox, even when every other
    // condition says STORE.
    MboxDecision d = mboxClassify("DK5EN-1", "DK5EN", "RM1 set --x{55", false, false, false, true);
    TEST_ASSERT_EQUAL((int)MBOX_NONE, (int)d.action);
    // The same text without the RM1 prefix is stored.
    d = mboxClassify("DK5EN-1", "DK5EN", "RM2 set --x{55", false, false, false, true);
    TEST_ASSERT_EQUAL((int)MBOX_STORE, (int)d.action);
}

static void test_classify_null_safe(void)
{
    MboxDecision d = mboxClassify("DK5EN-1", 0, "a{1", false, false, false, true);
    TEST_ASSERT_EQUAL((int)MBOX_NONE, (int)d.action);
    d = mboxClassify("DK5EN-1", "DK5EN", 0, false, false, false, true);
    TEST_ASSERT_EQUAL((int)MBOX_NONE, (int)d.action);
}

static void test_text_len_truncates_like_lora_path(void)
{
    // The call site truncates the payload copy to textLen; the stored text is
    // everything before "{NNN".
    const char *payload = "Hallo Welt{321";
    MboxDecision d = mboxClassify("DK5EN-1", "DK5EN", payload, false, false, false, true);
    TEST_ASSERT_EQUAL((int)MBOX_STORE, (int)d.action);
    char buf[64];
    mcSet(buf, sizeof(buf), payload);
    mcTruncate(buf, sizeof(buf), d.textLen);
    TEST_ASSERT_EQUAL_STRING("Hallo Welt", buf);
    TEST_ASSERT_EQUAL_UINT16(321, d.nnn);
}

struct PathCase
{
    const char *name;
    const char *path;
    const char *call;
    bool expect;
};

static const PathCase kPathCases[] = {
    { "only element",          "DK5EN-1",                  "DK5EN-1",  true  },
    { "first element",         "DK5EN-1,OE1ABC-2,DL1X",    "DK5EN-1",  true  },
    { "middle element",        "OE1ABC-2,DK5EN-1,DL1X",    "DK5EN-1",  true  },
    { "last element",          "OE1ABC-2,DL1X,DK5EN-1",    "DK5EN-1",  true  },
    { "prefix not matched",    "DK5EN-12",                 "DK5EN-1",  false },
    { "prefix mid not matched","A,DK5EN-12,B",             "DK5EN-1",  false },
    { "suffix not matched",    "XDK5EN-1",                 "DK5EN-1",  false },
    { "suffix mid not matched","A,XDK5EN-1,B",             "DK5EN-1",  false },
    { "longer call not in path","DK5EN-1",                 "DK5EN-12", false },
    { "SSID included",         "DK5EN,OE1ABC",             "DK5EN-1",  false },
    { "no SSID vs SSID",       "DK5EN-1",                  "DK5EN",    false },
    { "case sensitive",        "dk5en-1,B",                "DK5EN-1",  false },
    { "empty path",            "",                         "DK5EN-1",  false },
    { "empty call",            "A,B",                      "",         false },
    { "empty elements",        ",,DK5EN-1,,",              "DK5EN-1",  true  },
    { "trailing comma",        "A,",                       "A",        true  },
    { "absent",                "A,B,C",                    "D",        false },
};

static void test_path_has_call_table(void)
{
    for (size_t i = 0; i < sizeof(kPathCases) / sizeof(kPathCases[0]); i++)
    {
        const PathCase &c = kPathCases[i];
        char msg[96];
        snprintf(msg, sizeof(msg), "path case '%s'", c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.expect, mboxPathHasCall(c.path, c.call), msg);
    }
}

static void test_path_has_call_null_safe(void)
{
    TEST_ASSERT_FALSE(mboxPathHasCall(0, "A"));
    TEST_ASSERT_FALSE(mboxPathHasCall("A", 0));
}

static void test_service_call_helper(void)
{
    TEST_ASSERT_TRUE(mboxIsServiceCall("WLNK-1"));
    TEST_ASSERT_TRUE(mboxIsServiceCall("APRS2SOTA"));
    TEST_ASSERT_FALSE(mboxIsServiceCall("WLNK-10"));
    TEST_ASSERT_FALSE(mboxIsServiceCall("WLNK-"));
    TEST_ASSERT_FALSE(mboxIsServiceCall("wlnk-1"));
    TEST_ASSERT_FALSE(mboxIsServiceCall("aprs2sota"));
    TEST_ASSERT_FALSE(mboxIsServiceCall(""));
    TEST_ASSERT_FALSE(mboxIsServiceCall(0));
    TEST_ASSERT_FALSE(mboxIsServiceCall("DK5EN-1"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_classify_table);
    RUN_TEST(test_rm1_prefix_never_stored);
    RUN_TEST(test_classify_null_safe);
    RUN_TEST(test_text_len_truncates_like_lora_path);
    RUN_TEST(test_path_has_call_table);
    RUN_TEST(test_path_has_call_null_safe);
    RUN_TEST(test_service_call_helper);
    return UNITY_END();
}
