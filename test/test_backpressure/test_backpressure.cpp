// BP-01 / TM-37: the sender-facing back-pressure state machine.
//
// src/backpressure.h holds nothing but the decision: given the TX ring depth
// and whether addTxRingEntry() dropped the frame, which notice must go out on
// the originating transport. No Arduino, no globals, no I/O — so every rule the
// operator wrote down in BACKLOG BP-01 is pinnable here, deterministically:
//
//   * the 80 %-of-MAX_RING threshold, computed per board (10 / 20 / 30),
//   * one QRS per episode under a burst, not one per message (TM-21),
//   * the refusal band (QRT holds until the ring is genuinely clear),
//   * QTA when the ring actually threw a message away,
//   * QRV exactly once, and only after QRS/QRT/QTA — never for a quiet node,
//   * a full cycle re-arms the whole thing.
//
//   pio test -e native -f test_backpressure

#include <unity.h>

#include <backpressure.h>

void setUp(void) {}
void tearDown(void) {}

// ---- thresholds -----------------------------------------------------------

// 80 % of MAX_RING, per board: T-Beam 10, Heltec/T-Deck/RAK 20, ESP32 classic 30.
// A hardcoded 16 would make the T-Beam warn at 160 % and the classic ESP32 at 53 %.
static void test_threshold_is_80_percent_of_max_ring(void)
{
    TEST_ASSERT_EQUAL_INT(8,  BackPressure(10).refuseThreshold());
    TEST_ASSERT_EQUAL_INT(16, BackPressure(20).refuseThreshold());
    TEST_ASSERT_EQUAL_INT(24, BackPressure(30).refuseThreshold());
}

// The threshold must stay above the quiet band even for absurd ring sizes,
// or "clear" and "refusing" would be the same depth and the machine would
// oscillate on every single message.
static void test_threshold_never_collides_with_quiet_band(void)
{
    for(int max_ring = 1; max_ring <= 4; max_ring++)
    {
        BackPressure bp(max_ring);
        TEST_ASSERT_TRUE_MESSAGE(bp.refuseThreshold() > BackPressure::QUIET_DEPTH,
                                 "refuse threshold must sit above the quiet band");
    }
}

// ---- quiet band -----------------------------------------------------------

// One entry in flight is the normal state of a working node, not congestion.
static void test_quiet_node_says_nothing(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(0, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false));
    TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    TEST_ASSERT_FALSE(bp.refusing());
}

// The one rule that keeps QRV from becoming a heartbeat: a node that was never
// under pressure never announces that it is ready.
static void test_no_qrv_without_a_preceding_notice(void)
{
    BackPressure bp(20);

    for(int i = 0; i < 50; i++)
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(0));

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1));
}

// ---- QRS ------------------------------------------------------------------

static void test_qrs_fires_once_above_depth_one(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(2, false));
    TEST_ASSERT_EQUAL_INT(BP_QRS, bp.state());
    TEST_ASSERT_FALSE(bp.refusing());
}

// TM-21's lesson: a QRS for every message of a burst is itself a flood.
static void test_single_qrs_per_episode_under_a_burst(void)
{
    BackPressure bp(20);
    int qrs_count = 0;

    // Walk the whole band below the refusal threshold, twice over, the way a
    // burst does when the radio drains a slot between two enqueues.
    for(int pass = 0; pass < 2; pass++)
    {
        for(int depth = 2; depth < bp.refuseThreshold(); depth++)
        {
            if(bp.onSend(depth, false) == BP_NOTICE_QRS)
                qrs_count++;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrs_count, "QRS must be one per episode, not one per message");
}

// ---- QRT ------------------------------------------------------------------

static void test_qrt_at_the_threshold_and_refusal_band(void)
{
    BackPressure bp(20);   // threshold 16

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(2, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(15, false));
    TEST_ASSERT_FALSE_MESSAGE(bp.refusing(), "one below the threshold still accepts");

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false));
    TEST_ASSERT_EQUAL_INT(BP_QRT, bp.state());
    TEST_ASSERT_TRUE(bp.refusing());
}

// QRT without a preceding QRS: a single enqueue can jump straight into the band.
static void test_qrt_without_preceding_qrs(void)
{
    BackPressure bp(10);   // threshold 8

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(9, false));
    TEST_ASSERT_TRUE(bp.refusing());
}

// Hysteresis: once the node has said "stop", it stays stopped until the ring is
// genuinely clear. Releasing at the threshold would flip accept/refuse on every
// message and produce a notice per message.
static void test_qrt_holds_until_the_quiet_band(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false));

    for(int depth = 15; depth > BackPressure::CLEAR_DEPTH; depth--)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(depth), "poll must stay silent above the quiet band");
        TEST_ASSERT_TRUE_MESSAGE(bp.refusing(), "QRT must hold all the way down to the quiet band");
    }

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(BackPressure::CLEAR_DEPTH));
    TEST_ASSERT_FALSE(bp.refusing());
}

// The refusal must not silently re-announce itself per refused message.
static void test_refusal_announces_once_per_episode(void)
{
    BackPressure bp(20);
    int qrt_count = 0;

    if(bp.onSend(16, false) == BP_NOTICE_QRT)
        qrt_count++;

    for(int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(bp.refusing());
        if(bp.onRefuse() == BP_NOTICE_QRT)
            qrt_count++;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrt_count, "QRT is one per state transition, not one per refused message");
}

// ---- QTA ------------------------------------------------------------------

static void test_qta_on_drop_and_only_once(void)
{
    BackPressure bp(20);
    int qta_count = 0;

    for(int i = 0; i < 6; i++)          // the bench saw 6x RING_DROP_NEW in one flood
    {
        if(bp.onSend(19, true) == BP_NOTICE_QTA)
            qta_count++;
    }

    TEST_ASSERT_EQUAL_INT(1, qta_count);
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.latch());
    TEST_ASSERT_TRUE_MESSAGE(bp.refusing(), "a drop implies the refusal state");
}

// A drop outranks the plain threshold notice: the sender must learn that the
// message is gone, not merely that the buffer is full.
static void test_qta_outranks_qrt_and_is_not_followed_by_qrt(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(18, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(19, true));
}

// A drop while only QRS had been sent must still be reported.
static void test_qta_after_qrs(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(3, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true));
}

// ---- QRV and re-arming ----------------------------------------------------

static void test_qrv_exactly_once_after_a_notice(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(2, false));
    // depth 1 is below the QRS line but not clear: no flapping QRS/QRV/QRS
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(2, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(0));

    for(int i = 0; i < 10; i++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(0), "QRV closes the episode, it does not repeat");
}

// The drain can also be observed by the next enqueue rather than by the poll.
static void test_qrv_via_onsend_when_the_queue_drained(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false));
    TEST_ASSERT_TRUE(bp.refusing());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.onSend(0, false));
    TEST_ASSERT_FALSE(bp.refusing());
}

static void test_full_cycle_rearms(void)
{
    BackPressure bp(30);   // threshold 24

    for(int cycle = 0; cycle < 3; cycle++)
    {
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(2, false));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(24, false));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(30, true));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(0));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
        TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    }
}

// reset() must leave no half-open episode behind (used by configure()).
static void test_reset_clears_the_episode(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false));
    bp.reset();

    TEST_ASSERT_FALSE(bp.refusing());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(0), "no QRV for an episode that was reset away");
}

// ---- wording --------------------------------------------------------------

// The texts are the operator's, verbatim from the BACKLOG BP-01 table; the
// bench asserts on them, so a silent reword must break the build's test run.
static void test_notice_wording(void)
{
    TEST_ASSERT_EQUAL_STRING("QRS", bpNoticeCode(BP_NOTICE_QRS));
    TEST_ASSERT_EQUAL_STRING("QRT", bpNoticeCode(BP_NOTICE_QRT));
    TEST_ASSERT_EQUAL_STRING("QTA", bpNoticeCode(BP_NOTICE_QTA));
    TEST_ASSERT_EQUAL_STRING("QRV", bpNoticeCode(BP_NOTICE_QRV));
    TEST_ASSERT_EQUAL_STRING("",    bpNoticeCode(BP_NOTICE_NONE));

    TEST_ASSERT_EQUAL_STRING("QRS - slow down, TX buffer is filling", bpNoticeText(BP_NOTICE_QRS));
    TEST_ASSERT_EQUAL_STRING("QRT - stopping to accept new messages, TX buffer full", bpNoticeText(BP_NOTICE_QRT));
    TEST_ASSERT_EQUAL_STRING("QTA - message discarded, TX buffer full", bpNoticeText(BP_NOTICE_QTA));
    TEST_ASSERT_EQUAL_STRING("QRV - ready again, TX buffer clear", bpNoticeText(BP_NOTICE_QRV));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_threshold_is_80_percent_of_max_ring);
    RUN_TEST(test_threshold_never_collides_with_quiet_band);

    RUN_TEST(test_quiet_node_says_nothing);
    RUN_TEST(test_no_qrv_without_a_preceding_notice);

    RUN_TEST(test_qrs_fires_once_above_depth_one);
    RUN_TEST(test_single_qrs_per_episode_under_a_burst);

    RUN_TEST(test_qrt_at_the_threshold_and_refusal_band);
    RUN_TEST(test_qrt_without_preceding_qrs);
    RUN_TEST(test_qrt_holds_until_the_quiet_band);
    RUN_TEST(test_refusal_announces_once_per_episode);

    RUN_TEST(test_qta_on_drop_and_only_once);
    RUN_TEST(test_qta_outranks_qrt_and_is_not_followed_by_qrt);
    RUN_TEST(test_qta_after_qrs);

    RUN_TEST(test_qrv_exactly_once_after_a_notice);
    RUN_TEST(test_qrv_via_onsend_when_the_queue_drained);
    RUN_TEST(test_full_cycle_rearms);
    RUN_TEST(test_reset_clears_the_episode);

    RUN_TEST(test_notice_wording);

    return UNITY_END();
}
