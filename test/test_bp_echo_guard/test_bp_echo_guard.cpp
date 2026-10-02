// BP-11: strict block on a client feeding this node's own back-pressure
// wording (a notice or a nack) back into sendMessage() as if the operator
// had typed it.
//
// bpIsOwnWording() (src/backpressure.h) is the whole rule: after leading
// spaces, text either starts with one of the two bpNackPrefix() literals or
// exactly equals one of the four bpNoticeText() literals. Exact and
// case-sensitive on purpose -- only the wording this firmware itself emits
// is caught; a text that merely quotes it mid-sentence is legitimate
// operator traffic and must still go out. Option A (operator decision
// 2026-09-04, strict block): no stripping, no offset recovery, no BpEcho
// enum -- an echo is refused whole, with BP_SEND_INVALID, and nothing else.
//
//   pio test -e native -f test_bp_echo_guard

#include <unity.h>

#include <backpressure.h>

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// ---- a: the four notice literals, bare --------------------------------

// Written out so this test also documents the exact operator-approved
// wording (BACKLOG BP-01 table) that must never reach the TX ring.
static void test_bare_notice_literals_are_own_wording(void)
{
    TEST_ASSERT_TRUE(bpIsOwnWording("QRS - slow down, TX buffer is filling"));
    TEST_ASSERT_TRUE(bpIsOwnWording("QRT - stopping to accept new messages, TX buffer full"));
    TEST_ASSERT_TRUE(bpIsOwnWording("QTA - message discarded, TX buffer full"));
    TEST_ASSERT_TRUE(bpIsOwnWording("QRV - ready again, TX buffer clear"));
}

// ---- b: drift guard -- read the wording from the accessors, not from a
// second copy here, so a reworded notice cannot silently unhook the guard --

static void test_drift_guard_notice_texts_and_nack_prefixes(void)
{
    for(int n = BP_NOTICE_QRS; n <= BP_NOTICE_QRV; n++)
    {
        TEST_ASSERT_TRUE(bpIsOwnWording(bpNoticeText((BpNotice)n)));
    }

    TEST_ASSERT_TRUE(bpIsOwnWording(bpNackPrefix(BP_NACK_QRT)));
    TEST_ASSERT_TRUE(bpIsOwnWording(bpNackPrefix(BP_NACK_QTA)));
}

// ---- c: stacked prefixes (the observed field shape) --------------------

// IZ5CND-1/-10, 2026-09-04: each re-injection through a client added one
// more "QRT NOT SENT - " prefix on top of the last. 1..5 stacked copies in
// front of each of the four notice texts must all still be caught.
static void test_stacked_prefixes_in_front_of_notice_texts(void)
{
    char buf[512];

    for(int n = BP_NOTICE_QRS; n <= BP_NOTICE_QRV; n++)
    {
        const char *notice = bpNoticeText((BpNotice)n);

        for(int stack = 1; stack <= 5; stack++)
        {
            buf[0] = '\0';
            for(int i = 0; i < stack; i++)
                strcat(buf, bpNackPrefix(BP_NACK_QRT));
            strcat(buf, notice);

            TEST_ASSERT_TRUE_MESSAGE(bpIsOwnWording(buf), buf);
        }
    }
}

// ---- d: mixed prefix + notice, the QRS notice wrapped in a QRT/QTA nack --

static void test_mixed_prefixes_and_notice(void)
{
    TEST_ASSERT_TRUE(bpIsOwnWording(
        "QRT NOT SENT - QTA NOT SENT - QRS - slow down, TX buffer is filling"));
}

// ---- e: prefix followed by real operator content ------------------------

// Option A is strict: the whole text is blocked, even though "Hallo Welt"
// after the prefix is content a client made up, not this firmware's
// wording. The content is the client's to resend on its own, cleanly.
static void test_prefix_followed_by_real_content_is_blocked(void)
{
    TEST_ASSERT_TRUE(bpIsOwnWording("QRT NOT SENT - Hallo Welt"));
}

// ---- f: the field case itself -------------------------------------------

// IZ5CND-1/-10, 2026-09-04. "{CET}2026-09-04 04:31:29" here is a payload
// marker of the time broadcast (the triggering message's destination was
// "*"), not a {ZIEL} destination prefix -- sendMessage()'s {..} parse only
// strips a leading {..} block before iCall < 11, this is text mid-message.
static void test_field_case_stacked_qrt_before_time_broadcast(void)
{
    TEST_ASSERT_TRUE(bpIsOwnWording(
        "QRT NOT SENT - QRT NOT SENT - {CET}2026-09-04 04:31:29"));
}

// ---- g: ordinary traffic, empty, nullptr ---------------------------------

static void test_ordinary_text_empty_and_null_pass(void)
{
    TEST_ASSERT_FALSE(bpIsOwnWording("Hello World 17"));
    TEST_ASSERT_FALSE(bpIsOwnWording(""));
    TEST_ASSERT_FALSE(bpIsOwnWording(nullptr));
}

// ---- h: exact match only -- mid-sentence quoting, off-by-one -------------

static void test_wording_mid_sentence_and_off_by_one_are_not_blocked(void)
{
    TEST_ASSERT_FALSE(bpIsOwnWording("Ich sah QRV - ready again, TX buffer clear"));

    char buf[128];

    // one extra trailing char
    snprintf(buf, sizeof(buf), "%sx", bpNoticeText(BP_NOTICE_QRS));
    TEST_ASSERT_FALSE_MESSAGE(bpIsOwnWording(buf), buf);

    // truncated by one char
    const char *qrs = bpNoticeText(BP_NOTICE_QRS);
    size_t len = strlen(qrs);
    snprintf(buf, sizeof(buf), "%.*s", (int)(len - 1), qrs);
    TEST_ASSERT_FALSE_MESSAGE(bpIsOwnWording(buf), buf);
}

// ---- i: leading whitespace and case ---------------------------------------

// Only leading spaces are skipped, deliberately -- a tab is left alone
// rather than guessing at every whitespace variant a client might send.
static void test_leading_spaces_case_and_tab(void)
{
    TEST_ASSERT_TRUE(bpIsOwnWording("  QRT NOT SENT - x"));
    TEST_ASSERT_FALSE(bpIsOwnWording("qrt not sent - x"));
    TEST_ASSERT_FALSE(bpIsOwnWording("\tQRT NOT SENT - x"));
}

// ---- j: nack prefix missing its trailing space ---------------------------

static void test_nack_prefix_missing_trailing_space_is_not_blocked(void)
{
    TEST_ASSERT_FALSE(bpIsOwnWording("QRT NOT SENT -x"));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_bare_notice_literals_are_own_wording);
    RUN_TEST(test_drift_guard_notice_texts_and_nack_prefixes);
    RUN_TEST(test_stacked_prefixes_in_front_of_notice_texts);
    RUN_TEST(test_mixed_prefixes_and_notice);
    RUN_TEST(test_prefix_followed_by_real_content_is_blocked);
    RUN_TEST(test_field_case_stacked_qrt_before_time_broadcast);
    RUN_TEST(test_ordinary_text_empty_and_null_pass);
    RUN_TEST(test_wording_mid_sentence_and_off_by_one_are_not_blocked);
    RUN_TEST(test_leading_spaces_case_and_tab);
    RUN_TEST(test_nack_prefix_missing_trailing_space_is_not_blocked);

    return UNITY_END();
}
