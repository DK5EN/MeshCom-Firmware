// Host test for src/stor_announce.h (SNF-GW W3, SNF-D6/D7): announce set, STOR
// datagram encoder with chunking, send timer.
//
//   pio test -e native_stor_announce

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stor_announce.h>

void setUp(void) {}
void tearDown(void) {}

static bool eligAll(const char *, void *) { return true; }
static bool eligNotX(const char *c, void *) { return strcmp(c, "X1XXX-1") != 0; }
static int eligCalls = 0;
static bool eligCount(const char *, void *ctx)
{
    eligCalls++;
    return *(bool *)ctx;
}

static StorSet build(const char *const *h, int n, const char *own = "DK5EN-90",
                     bool (*e)(const char *, void *) = eligAll)
{
    StorSet s;
    storBuildSet(s, h, n, own, e, NULL);
    return s;
}

// ---- announce set -------------------------------------------------------

void test_own_exact_call_excluded(void)
{
    const char *h[] = {"DK5EN-90", "DK5EN-92"};
    StorSet s = build(h, 2);
    TEST_ASSERT_EQUAL_INT(1, s.n);
    TEST_ASSERT_EQUAL_STRING("DK5EN-92", s.call[0]);
}

void test_own_base_other_ssid_included(void)
{
    const char *h[] = {"DK5EN-90", "DK5EN-1", "DK5EN"};
    StorSet s = build(h, 3);
    TEST_ASSERT_EQUAL_INT(2, s.n);
    TEST_ASSERT_EQUAL_STRING("DK5EN", s.call[0]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", s.call[1]);
}

void test_ineligible_excluded(void)
{
    const char *h[] = {"DK5EN-92", "X1XXX-1", "DL1ABC-5"};
    StorSet s = build(h, 3, "DK5EN-90", eligNotX);
    TEST_ASSERT_EQUAL_INT(2, s.n);
    TEST_ASSERT_EQUAL_STRING("DK5EN-92", s.call[0]);
    TEST_ASSERT_EQUAL_STRING("DL1ABC-5", s.call[1]);
}

void test_eligible_gets_ctx_and_null_means_all(void)
{
    bool yes = true, no = false;
    const char *h[] = {"A1A-1", "B1B-2"};
    StorSet s;
    eligCalls = 0;
    storBuildSet(s, h, 2, "DK5EN-90", eligCount, &no);
    TEST_ASSERT_EQUAL_INT(0, s.n);
    TEST_ASSERT_EQUAL_INT(2, eligCalls);
    storBuildSet(s, h, 2, "DK5EN-90", eligCount, &yes);
    TEST_ASSERT_EQUAL_INT(2, s.n);
    storBuildSet(s, h, 2, "DK5EN-90", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(2, s.n);
}

void test_sorted_and_deduplicated(void)
{
    const char *h[] = {"DL2B-1", "DK5EN-92", "DL2B-1", "AA1A-1", "DK5EN-92"};
    StorSet s = build(h, 5);
    TEST_ASSERT_EQUAL_INT(3, s.n);
    TEST_ASSERT_EQUAL_STRING("AA1A-1", s.call[0]);
    TEST_ASSERT_EQUAL_STRING("DK5EN-92", s.call[1]);
    TEST_ASSERT_EQUAL_STRING("DL2B-1", s.call[2]);
}

void test_set_equality_order_independent(void)
{
    const char *a[] = {"DL2B-1", "DK5EN-92", "AA1A-1"};
    const char *b[] = {"AA1A-1", "DL2B-1", "DK5EN-92", "AA1A-1"};
    const char *c[] = {"AA1A-1", "DL2B-1"};
    StorSet sa = build(a, 3), sb = build(b, 4), sc = build(c, 2);
    TEST_ASSERT_TRUE(storSetEqual(sa, sb));
    TEST_ASSERT_FALSE(storSetEqual(sa, sc));
    StorSet e1 = build(NULL, 0), e2 = build(c, 0);
    TEST_ASSERT_TRUE(storSetEqual(e1, e2));
    TEST_ASSERT_FALSE(storSetEqual(e1, sc));
}

void test_long_empty_and_semicolon_calls_rejected_not_truncated(void)
{
    const char *h[] = {"DK5EN-12345", "ABCDEFGHI", "ABCDEFGHIJ", "", "A;B-1", NULL, "OK1OK-1"};
    StorSet s = build(h, 7);
    TEST_ASSERT_EQUAL_INT(2, s.n); // 9 chars fit, 10 do not
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHI", s.call[0]);
    TEST_ASSERT_EQUAL_STRING("OK1OK-1", s.call[1]);
}

void test_set_capped_at_max_keeps_smallest(void)
{
    char names[STOR_MAX_CALLS + 10][STOR_CALL_LEN];
    const char *h[STOR_MAX_CALLS + 10];
    for (int i = 0; i < STOR_MAX_CALLS + 10; i++)
    {
        snprintf(names[i], sizeof names[i], "N%03d-1", (STOR_MAX_CALLS + 9) - i); // descending input
        h[i] = names[i];
    }
    StorSet s = build(h, STOR_MAX_CALLS + 10);
    TEST_ASSERT_EQUAL_INT(STOR_MAX_CALLS, s.n);
    TEST_ASSERT_EQUAL_STRING("N000-1", s.call[0]);
    TEST_ASSERT_EQUAL_STRING("N063-1", s.call[STOR_MAX_CALLS - 1]);
}

// ---- encoder --------------------------------------------------------------

void test_encode_example(void)
{
    const char *h[] = {"DK5EN-93", "DK5EN-92"};
    StorSet s = build(h, 2);
    char out[STOR_DATAGRAM_MAX];
    TEST_ASSERT_EQUAL_INT(1, storChunkCount(s, "DK5EN-90"));
    int len = storEncodeChunk(out, sizeof out, 0x1A2B3C4DUL, "DK5EN-90", "4.40", "a", 24, s, 1, 1);
    const char *want = "STOR1A2B3C4DDK5EN-90 4.40a24;1/1;DK5EN-92;DK5EN-93;";
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_INT((int)strlen(want), len);
}

void test_encode_empty_set(void)
{
    StorSet s = build(NULL, 0);
    char out[STOR_DATAGRAM_MAX];
    TEST_ASSERT_EQUAL_INT(1, storChunkCount(s, "DK5EN-90"));
    int len = storEncodeChunk(out, sizeof out, 0x1A2B3C4DUL, "DK5EN-90", "4.40", "a", 24, s, 1, 1);
    TEST_ASSERT_EQUAL_STRING("STOR1A2B3C4DDK5EN-90 4.40a24;1/1;", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), len);
}

void test_encode_pads_and_truncates_header_fields(void)
{
    StorSet s = build(NULL, 0);
    char out[STOR_DATAGRAM_MAX];
    storEncodeChunk(out, sizeof out, 0xAB, "DK5EN-9012", "4.4", "abc", 6, s, 1, 1);
    // gw call cut to 9, version padded to 4, sub cut to 1
    TEST_ASSERT_EQUAL_STRING("STOR000000ABDK5EN-901" "4.4 a6;1/1;", out);
}

static void makeLongSet(StorSet &s)
{
    char names[STOR_MAX_CALLS][STOR_CALL_LEN];
    const char *h[STOR_MAX_CALLS];
    for (int i = 0; i < STOR_MAX_CALLS; i++)
    {
        snprintf(names[i], sizeof names[i], "AB1CD-%03d", i); // 9 chars
        h[i] = names[i];
    }
    storBuildSet(s, h, STOR_MAX_CALLS, "DK5EN-90", eligAll, NULL);
}

void test_chunking_64_long_calls(void)
{
    StorSet s;
    makeLongSet(s);
    TEST_ASSERT_EQUAL_INT(STOR_MAX_CALLS, s.n);
    int total = storChunkCount(s, "DK5EN-90");
    TEST_ASSERT_TRUE(total >= 3);
    TEST_ASSERT_EQUAL_INT(4, total); // 21 calls of 10 bytes fit per datagram

    int seen = 0;
    for (int seq = 1; seq <= total; seq++)
    {
        char out[STOR_DATAGRAM_MAX];
        int len = storEncodeChunk(out, sizeof out, 0xDEADBEEFUL, "DK5EN-90", "4.40", "a", 999, s, seq, total);
        TEST_ASSERT_TRUE(len > 0);
        TEST_ASSERT_EQUAL_INT((int)strlen(out), len);
        TEST_ASSERT_TRUE(len + 1 <= STOR_DATAGRAM_MAX);
        char tag[16];
        snprintf(tag, sizeof tag, ";%d/%d;", seq, total);
        const char *p = strstr(out, tag);
        TEST_ASSERT_NOT_NULL(p);
        p += strlen(tag);
        while (*p)
        {
            const char *semi = strchr(p, ';');
            TEST_ASSERT_NOT_NULL(semi);
            TEST_ASSERT_TRUE(seen < s.n);
            size_t cl = (size_t)(semi - p);
            TEST_ASSERT_EQUAL_INT((int)strlen(s.call[seen]), (int)cl);
            TEST_ASSERT_EQUAL_INT(0, strncmp(p, s.call[seen], cl)); // order preserved, each once
            seen++;
            p = semi + 1;
        }
    }
    TEST_ASSERT_EQUAL_INT(s.n, seen);
}

void test_encode_rejects_bad_args(void)
{
    StorSet s;
    makeLongSet(s);
    int total = storChunkCount(s, "DK5EN-90");
    char out[STOR_DATAGRAM_MAX];
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, sizeof out, 1, "DK5EN-90", "4.40", "a", 24, s, 0, total));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, sizeof out, 1, "DK5EN-90", "4.40", "a", 24, s, total + 1, total));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, sizeof out, 1, "DK5EN-90", "4.40", "a", 24, s, 1, total - 1));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, sizeof out, 1, "DK5EN-90", "4.40", "a", -1, s, 1, total));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, sizeof out, 1, "DK5EN-90", "4.40", "a", 1000, s, 1, total));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(NULL, 10, 1, "DK5EN-90", "4.40", "a", 24, s, 1, total));
    TEST_ASSERT_EQUAL_INT(-1, storEncodeChunk(out, 40, 1, "DK5EN-90", "4.40", "a", 24, s, 1, total)); // too small
}

// ---- timer ---------------------------------------------------------------

static StorSet setOf(const char *a, const char *b = NULL)
{
    const char *h[2];
    int n = 0;
    h[n++] = a;
    if (b)
        h[n++] = b;
    return build(h, n);
}

void test_timer_first_send_then_quiet_inside_period(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet s = setOf("DK5EN-92");
    TEST_ASSERT_TRUE(storTimerDue(t, s, 1000, true));
    storTimerSent(t, s, 1000);
    TEST_ASSERT_FALSE(storTimerDue(t, s, 1000, true));
    TEST_ASSERT_FALSE(storTimerDue(t, s, 1000 + STOR_PERIOD_MS - 1, true));
}

void test_timer_resend_after_period(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet s = setOf("DK5EN-92");
    storTimerSent(t, s, 5000);
    TEST_ASSERT_TRUE(storTimerDue(t, s, 5000 + STOR_PERIOD_MS, true));
    storTimerSent(t, s, 5000 + STOR_PERIOD_MS);
    TEST_ASSERT_FALSE(storTimerDue(t, s, 5000 + STOR_PERIOD_MS + 1, true));
}

void test_timer_empty_set_is_sent_too(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet e = build(NULL, 0);
    TEST_ASSERT_TRUE(storTimerDue(t, e, 0, true));
}

void test_timer_change_sends_after_debounce_not_before(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92"), b = setOf("DK5EN-92", "DK5EN-93");
    storTimerSent(t, a, 0);
    TEST_ASSERT_FALSE(storTimerDue(t, b, 10000, true)); // change seen, debounce starts
    TEST_ASSERT_FALSE(storTimerDue(t, b, 10000 + STOR_DEBOUNCE_MS - 1, true));
    TEST_ASSERT_TRUE(storTimerDue(t, b, 10000 + STOR_DEBOUNCE_MS, true));
    storTimerSent(t, b, 10000 + STOR_DEBOUNCE_MS);
    TEST_ASSERT_FALSE(storTimerDue(t, b, 10000 + STOR_DEBOUNCE_MS + 1, true));
}

void test_timer_flapping_change_restarts_debounce(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92"), b = setOf("DK5EN-92", "DK5EN-93"), c = setOf("DK5EN-92", "DK5EN-94");
    storTimerSent(t, a, 0);
    TEST_ASSERT_FALSE(storTimerDue(t, b, 10000, true));
    TEST_ASSERT_FALSE(storTimerDue(t, c, 50000, true));  // second change: restart at 50000
    TEST_ASSERT_FALSE(storTimerDue(t, c, 70000, true));  // 60 s after the FIRST change, too early
    TEST_ASSERT_FALSE(storTimerDue(t, c, 50000 + STOR_DEBOUNCE_MS - 1, true));
    TEST_ASSERT_TRUE(storTimerDue(t, c, 50000 + STOR_DEBOUNCE_MS, true));
}

void test_timer_revert_cancels_pending_change(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92"), b = setOf("DK5EN-92", "DK5EN-93");
    storTimerSent(t, a, 0);
    TEST_ASSERT_FALSE(storTimerDue(t, b, 10000, true));
    TEST_ASSERT_FALSE(storTimerDue(t, a, 20000, true)); // back to the sent set
    TEST_ASSERT_FALSE(storTimerDue(t, b, 30000, true)); // new change: debounce restarts here
    TEST_ASSERT_FALSE(storTimerDue(t, b, 30000 + STOR_DEBOUNCE_MS - 1, true));
    TEST_ASSERT_TRUE(storTimerDue(t, b, 30000 + STOR_DEBOUNCE_MS, true));
}

void test_timer_disable_forgets_sent_and_never_due(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92");
    storTimerSent(t, a, 0);
    TEST_ASSERT_FALSE(storTimerDue(t, a, 10, false));
    TEST_ASSERT_FALSE(storTimerDue(t, a, STOR_PERIOD_MS * 3, false)); // not due even after many periods
    TEST_ASSERT_FALSE(t.sent);
    TEST_ASSERT_TRUE(storTimerDue(t, a, STOR_PERIOD_MS * 3 + 5, true)); // re-enable: immediately
    storTimerSent(t, a, STOR_PERIOD_MS * 3 + 5);
    TEST_ASSERT_FALSE(storTimerDue(t, a, STOR_PERIOD_MS * 3 + 6, true));
}

void test_timer_disabled_never_due_on_fresh_timer(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92");
    TEST_ASSERT_FALSE(storTimerDue(t, a, 0, false));
    TEST_ASSERT_FALSE(storTimerDue(t, a, 0xFFFFFFFFUL, false));
}

void test_timer_millis_wrap(void)
{
    StorTimer t;
    storTimerInit(t);
    StorSet a = setOf("DK5EN-92"), b = setOf("DK5EN-92", "DK5EN-93");
    uint32_t t0 = 0xFFFFFFFFUL - 30000UL; // 30 s before the wrap
    storTimerSent(t, a, t0);
    // period across the wrap
    TEST_ASSERT_FALSE(storTimerDue(t, a, t0 + (uint32_t)(STOR_PERIOD_MS - 1), true));
    TEST_ASSERT_TRUE(storTimerDue(t, a, t0 + (uint32_t)STOR_PERIOD_MS, true));
    // debounce across the wrap
    TEST_ASSERT_FALSE(storTimerDue(t, b, t0 + 10000UL, true));
    TEST_ASSERT_FALSE(storTimerDue(t, b, t0 + 10000UL + (uint32_t)STOR_DEBOUNCE_MS - 1, true));
    TEST_ASSERT_TRUE(storTimerDue(t, b, t0 + 10000UL + (uint32_t)STOR_DEBOUNCE_MS, true));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_own_exact_call_excluded);
    RUN_TEST(test_own_base_other_ssid_included);
    RUN_TEST(test_ineligible_excluded);
    RUN_TEST(test_eligible_gets_ctx_and_null_means_all);
    RUN_TEST(test_sorted_and_deduplicated);
    RUN_TEST(test_set_equality_order_independent);
    RUN_TEST(test_long_empty_and_semicolon_calls_rejected_not_truncated);
    RUN_TEST(test_set_capped_at_max_keeps_smallest);
    RUN_TEST(test_encode_example);
    RUN_TEST(test_encode_empty_set);
    RUN_TEST(test_encode_pads_and_truncates_header_fields);
    RUN_TEST(test_chunking_64_long_calls);
    RUN_TEST(test_encode_rejects_bad_args);
    RUN_TEST(test_timer_first_send_then_quiet_inside_period);
    RUN_TEST(test_timer_resend_after_period);
    RUN_TEST(test_timer_empty_set_is_sent_too);
    RUN_TEST(test_timer_change_sends_after_debounce_not_before);
    RUN_TEST(test_timer_flapping_change_restarts_debounce);
    RUN_TEST(test_timer_revert_cancels_pending_change);
    RUN_TEST(test_timer_disable_forgets_sent_and_never_due);
    RUN_TEST(test_timer_disabled_never_due_on_fresh_timer);
    RUN_TEST(test_timer_millis_wrap);
    return UNITY_END();
}
