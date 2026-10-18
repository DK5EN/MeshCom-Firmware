// Gateway registration state decision logic (src/gw_reg_state.h, header-only).
//
//   pio test -e native_gw_reg_state

#include <unity.h>

#include "gw_reg_state.h"

void setUp(void) {}
void tearDown(void) {}

static bool blk(const char *s) { return gwRegIsBlockReply((const uint8_t *)s, strlen(s)); }

static void test_block_at_start(void) { TEST_ASSERT_TRUE(blk("BLOCK 12345")); }
static void test_block_in_middle(void) { TEST_ASSERT_TRUE(blk("xx BLOCK yy")); }
static void test_block_at_end(void) { TEST_ASSERT_TRUE(blk("reply: BLOCK")); }
static void test_block_absent(void) { TEST_ASSERT_FALSE(blk("OK registered")); }
static void test_block_lowercase_no_match(void) { TEST_ASSERT_FALSE(blk("block Block bLOCK")); }
static void test_block_partial_no_match(void) { TEST_ASSERT_FALSE(blk("BLOC")); }

static void test_block_after_nul(void)
{
    const uint8_t p[] = {'a', 0, 'b', 0, 'B', 'L', 'O', 'C', 'K', 0};
    TEST_ASSERT_TRUE(gwRegIsBlockReply(p, sizeof(p)));
}

static void test_block_len_limits_scan(void)
{
    const uint8_t p[] = {'B', 'L', 'O', 'C', 'K'};
    TEST_ASSERT_FALSE(gwRegIsBlockReply(p, 4));
}

static void test_block_len_zero_and_null(void)
{
    const uint8_t p[] = {'B', 'L', 'O', 'C', 'K'};
    TEST_ASSERT_FALSE(gwRegIsBlockReply(p, 0));
    TEST_ASSERT_FALSE(gwRegIsBlockReply(NULL, 5));
    TEST_ASSERT_FALSE(gwRegIsBlockReply(NULL, 0));
}

static void test_source_equal(void) { TEST_ASSERT_TRUE(gwRegSourceOk(0x0A000001u, 0x0A000001u)); }
static void test_source_different(void) { TEST_ASSERT_FALSE(gwRegSourceOk(0x0A000002u, 0x0A000001u)); }
static void test_source_zero(void)
{
    TEST_ASSERT_FALSE(gwRegSourceOk(0, 0));
    TEST_ASSERT_FALSE(gwRegSourceOk(0, 0x0A000001u));
}

static void test_window_inside(void) { TEST_ASSERT_TRUE(gwRegInWindow(1500, 1000, true)); }
static void test_window_boundary(void)
{
    TEST_ASSERT_TRUE(gwRegInWindow(10999, 1000, true));
    TEST_ASSERT_FALSE(gwRegInWindow(11000, 1000, true));
}
static void test_window_outside(void) { TEST_ASSERT_FALSE(gwRegInWindow(60000, 1000, true)); }
static void test_window_millis_wrap(void)
{
    TEST_ASSERT_TRUE(gwRegInWindow(500, 0xFFFFFF00u, true));    // 756 ms across the wrap
    TEST_ASSERT_FALSE(gwRegInWindow(20000, 0xFFFFFF00u, true));
}
static void test_window_no_keep_sent(void) { TEST_ASSERT_FALSE(gwRegInWindow(1000, 1000, false)); }
static void test_window_now_before_keep(void)
{
    // now < lastKeep without a wrap is a huge unsigned difference: outside
    TEST_ASSERT_FALSE(gwRegInWindow(1000, 5000, true));
}

static void test_should_enter_combined(void)
{
    const uint8_t p[] = {'B', 'L', 'O', 'C', 'K'};
    TEST_ASSERT_TRUE(gwRegShouldEnter(7, 7, 2000, 1000, true, p, 5));
    TEST_ASSERT_FALSE(gwRegShouldEnter(8, 7, 2000, 1000, true, p, 5));
    TEST_ASSERT_FALSE(gwRegShouldEnter(7, 7, 20000, 1000, true, p, 5));
    TEST_ASSERT_FALSE(gwRegShouldEnter(7, 7, 2000, 1000, false, p, 5));
    TEST_ASSERT_FALSE(gwRegShouldEnter(7, 7, 2000, 1000, true, p, 4));
}

static void test_call_same(void) { TEST_ASSERT_FALSE(gwRegCallChanged("DK5EN-1", "DK5EN-1")); }
static void test_call_case_space_only(void)
{
    TEST_ASSERT_FALSE(gwRegCallChanged("dk5en-1", "DK5EN-1"));
    TEST_ASSERT_FALSE(gwRegCallChanged(" DK5EN-1 ", "DK5EN-1"));
}
static void test_call_different_ssid(void) { TEST_ASSERT_TRUE(gwRegCallChanged("DK5EN-1", "DK5EN-2")); }
static void test_call_different_base(void) { TEST_ASSERT_TRUE(gwRegCallChanged("DK5EN-1", "DL1ABC-1")); }
static void test_call_empty_stored_fail_closed(void)
{
    TEST_ASSERT_FALSE(gwRegCallChanged("", "DK5EN-1"));
    TEST_ASSERT_FALSE(gwRegCallChanged(NULL, "DK5EN-1"));
}
static void test_call_empty_current_fail_closed(void)
{
    // a not-yet-set / blank current callsign must not wipe a valid registration
    TEST_ASSERT_FALSE(gwRegCallChanged("DK5EN-1", ""));
    TEST_ASSERT_FALSE(gwRegCallChanged("DK5EN-1", NULL));
    TEST_ASSERT_FALSE(gwRegCallChanged("DK5EN-1", "   "));
}
static void test_evaluate_keeps_state_on_empty_current(void)
{
    GwRegState s = {0, {0}};
    gwRegEnter(s, "DK5EN-1");
    TEST_ASSERT_FALSE(gwRegEvaluate(s, ""));
    TEST_ASSERT_EQUAL_INT(1, s.flag);
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", s.call);
}

static void test_enter_sets_flag_and_call(void)
{
    GwRegState s = {0, {0}};
    gwRegEnter(s, " dk5en-1");
    TEST_ASSERT_EQUAL_INT(1, s.flag);
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", s.call);
}

static void test_enter_truncates_long_call(void)
{
    GwRegState s = {0, {0}};
    gwRegEnter(s, "ABCDEFGHIJKLMNOP-12");
    TEST_ASSERT_EQUAL_INT(1, s.flag);
    TEST_ASSERT_EQUAL_UINT(GW_REG_CALL_LEN - 1, strlen(s.call));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHI", s.call);
}

static void test_evaluate_clears_on_change(void)
{
    GwRegState s = {0, {0}};
    gwRegEnter(s, "DK5EN-1");
    TEST_ASSERT_FALSE(gwRegEvaluate(s, "dk5en-1"));
    TEST_ASSERT_EQUAL_INT(1, s.flag);
    TEST_ASSERT_TRUE(gwRegEvaluate(s, "DK5EN-2"));
    TEST_ASSERT_EQUAL_INT(0, s.flag);
    TEST_ASSERT_EQUAL_STRING("", s.call);
    TEST_ASSERT_FALSE(gwRegEvaluate(s, "DK5EN-3"));   // already clear
}

static void test_evaluate_empty_record_keeps_state(void)
{
    GwRegState s = {1, {0}};
    TEST_ASSERT_FALSE(gwRegEvaluate(s, "DK5EN-1"));
    TEST_ASSERT_EQUAL_INT(1, s.flag);
}

static bool oth(const char *s) { return gwRegFrameIsOther((const uint8_t *)s, strlen(s)); }

static void test_frame_known_indicators_not_other(void)
{
    TEST_ASSERT_FALSE(oth("GATE....BLOCK"));
    TEST_ASSERT_FALSE(oth("CONFDK5EN-1 BLOCK"));
    TEST_ASSERT_FALSE(oth("BEATBLOCK server"));
    TEST_ASSERT_FALSE(oth("GATE"));
}

static void test_frame_unknown_indicator_is_other(void)
{
    TEST_ASSERT_TRUE(oth("BLOCK"));
    TEST_ASSERT_TRUE(oth("xxxxBLOCK"));
    TEST_ASSERT_TRUE(oth("gate BLOCK"));   // case matters
    TEST_ASSERT_TRUE(oth("ABC"));          // shorter than the indicator
    TEST_ASSERT_FALSE(gwRegFrameIsOther(NULL, 8));
    TEST_ASSERT_FALSE(gwRegFrameIsOther((const uint8_t *)"BLOCK", 0));
}

static void test_frame_entry_decision_by_indicator(void)
{
    // the combination the frame handlers use: bucket check, then the full entry decision
    const char *known[] = {"GATE..BLOCK..", "CONFDK5EN-1 BLOCK", "BEATBLOCK"};
    for (size_t i = 0; i < 3; i++)
    {
        const uint8_t *p = (const uint8_t *)known[i];
        bool enter = gwRegFrameIsOther(p, strlen(known[i])) &&
                     gwRegShouldEnter(7, 7, 2000, 1000, true, p, strlen(known[i]));
        TEST_ASSERT_FALSE(enter);
    }
    const char *unk = "ERR BLOCK";
    TEST_ASSERT_TRUE(gwRegFrameIsOther((const uint8_t *)unk, strlen(unk)) &&
                     gwRegShouldEnter(7, 7, 2000, 1000, true, (const uint8_t *)unk, strlen(unk)));
}

static void test_call_default_fail_closed(void)
{
    TEST_ASSERT_FALSE(gwRegCallChanged("DK5EN-1", "XX0XXX-00"));
    TEST_ASSERT_FALSE(gwRegCallChanged("XX0XXX-00", "DK5EN-1"));
    TEST_ASSERT_FALSE(gwRegCallChanged("xx0xxx-00", "DK5EN-1"));
    GwRegState s = {0, {0}};
    gwRegEnter(s, "DK5EN-1");
    TEST_ASSERT_FALSE(gwRegEvaluate(s, "XX0XXX-00"));
    TEST_ASSERT_EQUAL_INT(1, s.flag);
}

// An unconfigured node (default or empty callsign) never enters: a callsign set later could not
// clear a state stored under the default call.
static void test_enter_refused_without_callsign(void)
{
    GwRegState s = {0, {0}};
    gwRegEnter(s, "XX0XXX-00");
    TEST_ASSERT_EQUAL_INT(0, s.flag);
    TEST_ASSERT_EQUAL_STRING("", s.call);
    gwRegEnter(s, "");
    TEST_ASSERT_EQUAL_INT(0, s.flag);
    gwRegEnter(s, "DK5EN-1");
    TEST_ASSERT_EQUAL_INT(1, s.flag);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_block_at_start);
    RUN_TEST(test_block_in_middle);
    RUN_TEST(test_block_at_end);
    RUN_TEST(test_block_absent);
    RUN_TEST(test_block_lowercase_no_match);
    RUN_TEST(test_block_partial_no_match);
    RUN_TEST(test_block_after_nul);
    RUN_TEST(test_block_len_limits_scan);
    RUN_TEST(test_block_len_zero_and_null);
    RUN_TEST(test_source_equal);
    RUN_TEST(test_source_different);
    RUN_TEST(test_source_zero);
    RUN_TEST(test_window_inside);
    RUN_TEST(test_window_boundary);
    RUN_TEST(test_window_outside);
    RUN_TEST(test_window_millis_wrap);
    RUN_TEST(test_window_no_keep_sent);
    RUN_TEST(test_window_now_before_keep);
    RUN_TEST(test_should_enter_combined);
    RUN_TEST(test_call_same);
    RUN_TEST(test_call_case_space_only);
    RUN_TEST(test_call_different_ssid);
    RUN_TEST(test_call_different_base);
    RUN_TEST(test_call_empty_stored_fail_closed);
    RUN_TEST(test_call_empty_current_fail_closed);
    RUN_TEST(test_evaluate_keeps_state_on_empty_current);
    RUN_TEST(test_enter_sets_flag_and_call);
    RUN_TEST(test_enter_truncates_long_call);
    RUN_TEST(test_evaluate_clears_on_change);
    RUN_TEST(test_evaluate_empty_record_keeps_state);
    RUN_TEST(test_frame_known_indicators_not_other);
    RUN_TEST(test_frame_unknown_indicator_is_other);
    RUN_TEST(test_frame_entry_decision_by_indicator);
    RUN_TEST(test_call_default_fail_closed);
    RUN_TEST(test_enter_refused_without_callsign);
    return UNITY_END();
}
