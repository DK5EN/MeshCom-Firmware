// BP-01 / TM-37: the sender-facing back-pressure state machine.
//
// src/backpressure.h holds nothing but the decision: given the TX ring depth
// and whether addTxRingEntry() dropped the frame, which notice must go out on
// the originating transport. No Arduino, no globals, no I/O — so every rule the
// operator wrote down in BACKLOG BP-01 is pinnable here, deterministically:
//
//   * the 80 %-of-MAX_RING threshold, computed per board (10 / 20 / 30),
//   * BP-05 (operator decision 2026-08-31): QRS fires at a fixed depth of 5
//     on every board, not at depth > 1 — the gateway field measurement
//     DK5EN-98 showed baseline depth 1-4 in ordinary operation, and the old
//     rule warned about that baseline three times in 5.5 minutes,
//   * QRS_MIN_USER_MSGS (operator decision 2026-09-01): depth 5 alone is
//     still mostly relay/ACK traffic on a gateway, so QRS additionally needs
//     the sender's third own message on a ring that sits at/above the line;
//     a dip below the line restarts that count,
//   * one QRS per episode under a burst, not one per message (TM-21),
//   * the refusal band (QRT holds until the ring is genuinely clear),
//   * QTA when the ring actually threw a message away,
//   * QRV exactly once, and only if the episode reached QRT or QTA — a
//     QRS-only episode (BP-05) closes silently, and a quiet node never gets
//     one either,
//   * a full cycle re-arms the whole thing,
//   * BP-04: depth 1 (the water band) needs QRV_HOLD_MS of uninterrupted
//     quiet before QRV, so a phantom-depth episode (DJ8MEH 2026-08-31, an
//     episode idling at depth 1 for 8 minutes) does not close on a single
//     lucky sample. now_ms is passed by the caller, never read from a clock
//     here -- Arduino-free, so the hysteresis itself is pinnable below.
//
//   pio test -e native -f test_backpressure

#include <unity.h>

#include <backpressure.h>

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

// ---- thresholds -----------------------------------------------------------

// ---- WQ-02: QRS forecast for the web GUI ----------------------------------

// Empty or baseline ring: three own messages, each must land at/above 5,
// so the third lands at 7 regardless of how far below the line the ring sits.
static void test_qrs_forecast_on_a_quiet_ring_is_line_plus_two(void)
{
    BackPressure bp(20);
    TEST_ASSERT_EQUAL_INT(7, bp.qrsForecastDepth(0));
    TEST_ASSERT_EQUAL_INT(7, bp.qrsForecastDepth(4));
}

// Ring already above the line with foreign traffic: the marker slides right
// with the fill, three own messages on top of what is there now.
static void test_qrs_forecast_slides_with_foreign_fill(void)
{
    BackPressure bp(20);
    TEST_ASSERT_EQUAL_INT(8,  bp.qrsForecastDepth(5));
    TEST_ASSERT_EQUAL_INT(13, bp.qrsForecastDepth(10));
}

// Own messages already counted pull the marker back left; after the count is
// complete the very next message fires, i.e. depth + 1.
static void test_qrs_forecast_moves_left_as_own_msgs_accumulate(void)
{
    BackPressure bp(20);
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(9, false, 0));
    TEST_ASSERT_EQUAL_INT(1, bp.userMsgsCounted());
    TEST_ASSERT_EQUAL_INT(11, bp.qrsForecastDepth(9));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(10, false, 0));
    TEST_ASSERT_EQUAL_INT(11, bp.qrsForecastDepth(10));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(11, false, 0));
    TEST_ASSERT_EQUAL_INT(12, bp.qrsForecastDepth(11));
}

// A dip below the line restarts the count and the forecast with it.
static void test_qrs_forecast_resets_below_the_line(void)
{
    BackPressure bp(20);
    bp.onSend(9, false, 0);
    bp.onSend(10, false, 0);
    bp.poll(3, 0);
    TEST_ASSERT_EQUAL_INT(0, bp.userMsgsCounted());
    TEST_ASSERT_EQUAL_INT(7, bp.qrsForecastDepth(3));
}

// Never past QRT: from there the refusal band answers, not QRS.
static void test_qrs_forecast_clamps_at_qrt(void)
{
    TEST_ASSERT_EQUAL_INT(16, BackPressure(20).qrsForecastDepth(15));
    TEST_ASSERT_EQUAL_INT(8,  BackPressure(10).qrsForecastDepth(9));
}

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

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(0, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    TEST_ASSERT_FALSE(bp.refusing());
}

// The one rule that keeps QRV from becoming a heartbeat: a node that was never
// under pressure never announces that it is ready.
static void test_no_qrv_without_a_preceding_notice(void)
{
    BackPressure bp(20);

    for(int i = 0; i < 50; i++)
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(0, (uint32_t)(1000 + i)));

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false, 2000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, 2001));
}

// ---- QRS ------------------------------------------------------------------

// BP-05 (operator decision 2026-08-31): the gateway field measurement
// (DK5EN-98, 5.5 min of ordinary operation, no burst anywhere in the log)
// showed baseline ring depth sitting at 1-4, mode 2 -- the old rule (QRS
// above depth 1) warned about that baseline three times. QRS now fires only
// at the fixed threshold of 5, never below it -- and (2026-09-01,
// QRS_MIN_USER_MSGS) only on the sender's third own message there.
static void test_qrs_fires_once_at_qrs_threshold(void)
{
    BackPressure bp(20);   // qrsThreshold() == 5

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(2, false, 1001));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(3, false, 1002));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(4, false, 1003));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1004));   // 1st own msg at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(6, false, 1005));   // 2nd
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(7, false, 1006));   // 3rd: QRS
    TEST_ASSERT_EQUAL_INT(BP_QRS, bp.state());
    TEST_ASSERT_FALSE(bp.refusing());
}

// QRS_MIN_USER_MSGS (operator decision 2026-09-01): a gateway idling at
// depth 4 from relay/ACK traffic handed the very first typed message a QRS
// for a queue the sender had not built. The first two own messages that
// find the ring at/above the line stay silent; the third raises QRS.
static void test_qrs_needs_three_own_messages_at_the_line(void)
{
    BackPressure bp(20);   // qrsThreshold() == 5

    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(5, false, 1000),
                                  "1st own message on a ring the mesh filled: silent");
    TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(5, false, 1001),
                                  "2nd own message: still silent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_QRS, bp.onSend(5, false, 1002),
                                  "3rd own message on a full ring: QRS");
    TEST_ASSERT_EQUAL_INT(BP_QRS, bp.state());
}

// The count is the sender's own run on a full ring: once the ring has been
// seen below the line again, the run is over and starts from zero. A sender
// whose messages drain between keystrokes never gets a QRS.
static void test_qrs_own_message_count_restarts_below_the_line(void)
{
    BackPressure bp(20);   // qrsThreshold() == 5

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(6, false, 1001));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(4, false, 1002));   // radio drained: run over
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(5, false, 1003),
                                  "count restarted: this is own message #1 again");
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(6, false, 1004));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(7, false, 1005));
}

// The drain poll sees the dip too: in the field the ring drains between two
// typed messages while nothing is enqueued, so poll() must end the run the
// same way onSend() does. A sighting at/above the line in poll() leaves the
// count alone.
static void test_qrs_own_message_count_restarts_via_poll(void)
{
    BackPressure bp(20);   // qrsThreshold() == 5

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(6, false, 1001));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(5, 1002));            // still at the line: count kept
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(4, 1003));            // drained below it: run over
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(5, false, 1004),
                                  "poll() saw depth 4: this is own message #1 again");
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(6, false, 1005));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(7, false, 1006));
}

// QRT and QTA are real events (refused / lost message) and are NOT gated by
// the own-message count: the first typed message into the refusal band or a
// drop must still be reported at once. reset() clears the count like the rest
// of the episode state.
static void test_qrt_qta_and_reset_ignore_the_own_message_count(void)
{
    BackPressure bp(20);   // refuseThreshold() 16

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 1000));
    bp.reset();
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true, 1001));
    bp.reset();

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1002));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1003));
    bp.reset();
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(5, false, 1004),
                                  "reset() cleared the count: own message #1 again");
}

// TM-21's lesson: a QRS for every message of a burst is itself a flood.
// Unaffected by BP-05: depths 2-4 are silent (below qrsThreshold()) and
// depth 5 raises the one QRS the assertion below expects; the walk-twice
// pattern still proves the latch, not the count, suppresses the repeat.
static void test_single_qrs_per_episode_under_a_burst(void)
{
    BackPressure bp(20);
    int qrs_count = 0;
    uint32_t t = 1000;

    // Walk the whole band below the refusal threshold, twice over, the way a
    // burst does when the radio drains a slot between two enqueues.
    for(int pass = 0; pass < 2; pass++)
    {
        for(int depth = 2; depth < bp.refuseThreshold(); depth++)
        {
            if(bp.onSend(depth, false, t++) == BP_NOTICE_QRS)
                qrs_count++;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrs_count, "QRS must be one per episode, not one per message");
}

// ---- QRT ------------------------------------------------------------------

static void test_qrt_at_the_threshold_and_refusal_band(void)
{
    BackPressure bp(20);   // threshold 16, qrsThreshold() 5

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 998));    // own msgs 1-2 at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 999));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(15, false, 1001));
    TEST_ASSERT_FALSE_MESSAGE(bp.refusing(), "one below the threshold still accepts");

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 1002));
    TEST_ASSERT_EQUAL_INT(BP_QRT, bp.state());
    TEST_ASSERT_TRUE(bp.refusing());
}

// QRT without a preceding QRS: a single enqueue can jump straight into the band.
static void test_qrt_without_preceding_qrs(void)
{
    BackPressure bp(10);   // threshold 8

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(9, false, 1000));
    TEST_ASSERT_TRUE(bp.refusing());
}

// Hysteresis: once the node has said "stop", it stays stopped until the ring is
// genuinely clear. Releasing at the threshold would flip accept/refuse on every
// message and produce a notice per message. Depths above the water band never
// even touch the BP-04 hold (they disarm it and return immediately); only the
// final CLEAR_DEPTH poll closes the episode, same as before BP-04.
static void test_qrt_holds_until_the_quiet_band(void)
{
    BackPressure bp(20);
    uint32_t t = 1000;

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, t++));

    for(int depth = 15; depth > BackPressure::CLEAR_DEPTH; depth--)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(depth, t++), "poll must stay silent above the quiet band");
        TEST_ASSERT_TRUE_MESSAGE(bp.refusing(), "QRT must hold all the way down to the quiet band");
    }

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(BackPressure::CLEAR_DEPTH, t));
    TEST_ASSERT_FALSE(bp.refusing());
}

// BP-07: the EPISODE notice (bp.onSend()) must still not re-announce itself
// per refused message -- that half of the name is unchanged. But onRefuse()
// itself is no longer part of that latch: it is BpNack now, a DIFFERENT
// vocabulary (backpressure.h), and it must fire BP_NACK_QRT for every single
// refusal, not just the first. Before BP-07 onRefuse() returned BpNotice and
// was provably always BP_NOTICE_NONE (docs/backpressure-flow-control.md
// chapter 8, finding L1) -- this is the fixed contract, pinned both ways so
// a regression on either half goes red.
static void test_refusal_announces_once_per_episode(void)
{
    BackPressure bp(20);
    int qrt_count = 0;
    int nack_count = 0;

    if(bp.onSend(16, false, 1000) == BP_NOTICE_QRT)
        qrt_count++;

    for(int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(bp.refusing());
        if(bp.onRefuse() == BP_NACK_QRT)
            nack_count++;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrt_count, "QRT episode notice is one per state transition, not one per refused message");
    TEST_ASSERT_EQUAL_INT_MESSAGE(20, nack_count, "BP-07: onRefuse() nacks EVERY refused message, unlike the latched episode notice");
}

// A nack is per-message bookkeeping only -- it must not touch the episode
// machinery. If it did, a burst of refusals could re-arm or disturb the
// latch/state the episode notice depends on for its "once per transition"
// guarantee above.
static void test_onrefuse_does_not_touch_latch_or_state(void)
{
    BackPressure bp(20);

    bp.onSend(16, false, 1000);   // opens the QRT episode
    BpNotice latch_before = bp.latch();
    BpState  state_before = bp.state();

    for(int i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL_INT(BP_NACK_QRT, bp.onRefuse());

    TEST_ASSERT_EQUAL_INT_MESSAGE(latch_before, bp.latch(), "onRefuse() must not move the episode latch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(state_before, bp.state(), "onRefuse() must not move the episode state");
}

// Once the ring genuinely drains and the episode closes (QRV), refusing()
// must go false -- and with it, the sender-facing reason to ever call
// onRefuse() again. This is the state machine half of "no nack once quiet";
// the wiring half (sendMessage() only calls onRefuse() while refusing() is
// true) lives in loop_functions.cpp and is exercised end-to-end in
// test/test_bp_regression.
static void test_no_nack_reason_after_enter_quiet(void)
{
    BackPressure bp(20);

    bp.onSend(16, false, 1000);
    TEST_ASSERT_TRUE(bp.refusing());

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(BackPressure::CLEAR_DEPTH, 2000));
    TEST_ASSERT_FALSE_MESSAGE(bp.refusing(), "episode closed: no more reason to refuse, hence no more nack");
}

// ---- QTA ------------------------------------------------------------------

static void test_qta_on_drop_and_only_once(void)
{
    BackPressure bp(20);
    int qta_count = 0;

    for(int i = 0; i < 6; i++)          // the bench saw 6x RING_DROP_NEW in one flood
    {
        if(bp.onSend(19, true, (uint32_t)(1000 + i)) == BP_NOTICE_QTA)
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

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(18, false, 1001));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(19, true, 1002));
}

// A drop while only QRS had been sent must still be reported.
static void test_qta_after_qrs(void)
{
    BackPressure bp(20);   // qrsThreshold() 5

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 998));    // own msgs 1-2 at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 999));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true, 1001));
}

// ---- BP-05: fixed QRS threshold, QRV only after QRT/QTA -------------------
//
// Operator decision 2026-08-31, driven by a DK5EN-98 gateway field
// measurement (5.5 min of ordinary operation, no burst anywhere in the log):
// baseline ring depth sat at 1-4 (mode 2) and the old QRS rule (depth > 1)
// produced three false QRS/QRV pairs against that baseline. Fix: QRS now
// fires at a fixed depth of 5 on every board, and QRV only closes an episode
// that actually reached QRT or QTA -- a QRS-only episode (the queue merely
// touched 5 and drained again without ever refusing) closes silently.

// a) The field-measured baseline walk itself, replayed against the fix:
// depths 1 -> 2 -> 3 -> 2 -> 4 -> 2 -> 1 must not raise a single notice --
// this is exactly the pattern that produced three QRS/QRV pairs before.
static void test_baseline_load_produces_no_notice(void)
{
    BackPressure bp(20);
    const int depths[] = { 1, 2, 3, 2, 4, 2, 1 };
    uint32_t t = 1000;

    for(size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(depths[i], false, t),
                                      "gateway baseline (1-4) must not raise a notice");
        t += 137;   // arbitrary spacing; the fix does not depend on timing
    }

    TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
}

// b) The threshold itself: nothing below it, exactly one QRS at it (on the
// third own message, QRS_MIN_USER_MSGS), nothing new while climbing further
// (the existing latch, proven elsewhere).
static void test_qrs_exactly_at_the_threshold(void)
{
    BackPressure bp(20);   // qrsThreshold() == 5
    uint32_t t = 1000;

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(4, false, t++));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, t++));   // own msgs 1-2 at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, t++));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(5, false, t++));
    TEST_ASSERT_EQUAL_INT(BP_QRS, bp.state());

    for(int depth = 6; depth <= 15; depth++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.onSend(depth, false, t++),
                                       "QRS already latched this episode, must not repeat");
}

// c) A QRS-only episode closes silently: no QRV, and the latch is really
// cleared (not merely skipped) so the next episode can raise its own QRS.
static void test_qrs_only_episode_closes_silently(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 998));    // own msgs 1-2 at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 999));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(0, 1001),
                                   "QRS-only episode never reached QRT/QTA, no QRV to give");
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
    TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1998));   // own msgs 1-2 of the next run
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1999));
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_QRS, bp.onSend(5, false, 2000),
                                   "latch was cleared: the next episode can QRS again");
}

// d) The counterpart: an episode that reached QTA still gets its QRV --
// QRV gating is about the *latch level*, not about which path opened it.
static void test_qta_only_episode_gets_qrv(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(20, true, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(0, 1001));
}

// e) qrsThreshold() clamp, same style as refuseThreshold()'s: on a
// pathologically small ring QRS_MIN_DEPTH (5) would collide with or exceed
// refuseThreshold(), so it clamps to refuseThreshold() - 1 there; on every
// real ring size it is a no-op and qrsThreshold() is the flat 5.
static void test_qrs_threshold_clamp(void)
{
    BackPressure bp6(6);
    TEST_ASSERT_EQUAL_INT(4, bp6.refuseThreshold());
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, bp6.qrsThreshold(), "clamped to refuseThreshold() - 1");

    BackPressure bp20(20);
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, bp20.qrsThreshold(), "unclamped on a real ring size");
}

// ---- QRV and re-arming ----------------------------------------------------

// BP-05: a QRS-only episode no longer gets a QRV (see
// test_qrs_only_episode_closes_silently below), so this now has to open its
// episode with QRT to still exercise "QRV exactly once" at all.
static void test_qrv_exactly_once_after_a_qrt_episode(void)
{
    BackPressure bp(20);   // threshold 16

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 1000));
    // depth 1 is below the release line but not clear: no flapping
    // QRT/QRV/QRT. This only arms the BP-04 hold -- it was already NONE
    // before BP-04 and stays NONE now, the hold just adds a reason on top.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, 1001));
    // depth 2 sits in the BP-05 gap band (below qrsThreshold()) but still
    // above the quiet band -- disarms the hold, changes nothing else, and
    // the still-open QRT state keeps refusing.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(2, false, 1002));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(0, 1003));

    for(int i = 0; i < 10; i++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(0, (uint32_t)(1004 + i)), "QRV closes the episode, it does not repeat");
}

// The drain can also be observed by the next enqueue rather than by the poll.
// depth 0 always closes immediately -- the BP-04 hold only guards depth 1.
static void test_qrv_via_onsend_when_the_queue_drained(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(1, false, 1001));
    TEST_ASSERT_TRUE(bp.refusing());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.onSend(0, false, 1002));
    TEST_ASSERT_FALSE(bp.refusing());
}

static void test_full_cycle_rearms(void)
{
    BackPressure bp(30);   // threshold 24, qrsThreshold() 5
    uint32_t t = 1000;

    for(int cycle = 0; cycle < 3; cycle++)
    {
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, t++));   // own msgs 1-2 at the line
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, t++));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.onSend(5, false, t++));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(24, false, t++));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(30, true, t++));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(0, t++));
        TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
        TEST_ASSERT_EQUAL_INT(BP_QUIET, bp.state());
    }
}

// reset() must leave no half-open episode behind (used by configure()).
static void test_reset_clears_the_episode(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 1000));
    bp.reset();

    TEST_ASSERT_FALSE(bp.refusing());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_NONE, bp.poll(0, 2000), "no QRV for an episode that was reset away");
}

// ---- BP-04: water-band hold (QRV_HOLD_MS) ---------------------------------
//
// DJ8MEH-RCA 2026-08-31: an episode idled at phantom ring depth 1 for
// 8 minutes -- QRV only closes at CLEAR_DEPTH (0), so a node parked at depth
// 1 never told the sender it was ready again. These pin the fix: depth 1
// (the water band) must sit still for QRV_HOLD_MS before QRV, timed from the
// caller-supplied now_ms, never from a clock read inside the header.

// a) Wasserband: no QRV before quiet_since + QRV_HOLD_MS, exactly one QRV at
// the deadline, and the episode is closed afterwards (latch empty -> NONE).
static void test_qrv_water_band_needs_the_full_hold(void)
{
    BackPressure bp(20);
    const uint32_t t0 = 1000;

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, t0));

    const uint32_t t_quiet = t0 + 1000;   // some time later the ring drains to 1

    // Kontrollrechnung: der Halt beginnt beim ersten poll(1, t_quiet) und muss
    // bis t_quiet + QRV_HOLD_MS (10000) durchhalten.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_quiet));                                        // arms
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_quiet + 1));                                     // far too early
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_quiet + 5000));                                  // still too early
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_quiet + BackPressure::QRV_HOLD_MS - 1));         // 1 ms short

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(1, t_quiet + BackPressure::QRV_HOLD_MS));              // exactly the deadline
    TEST_ASSERT_FALSE(bp.refusing());

    // Episode is closed: no second QRV, even though depth is still 1.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_quiet + BackPressure::QRV_HOLD_MS + 1));
}

// b) A depth-2 sighting in between resets the hold's clock; QRV comes at the
// new deadline, not at the old one.
static void test_qrv_water_band_hold_restarts_on_a_deeper_sighting(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 0));

    const uint32_t t1 = 1000;
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t1));           // arms at t1

    const uint32_t t2 = t1 + 3000;
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(2, t2));           // ring filled again -- disarms

    const uint32_t t3 = t2 + 500;
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t3));           // re-arms at t3, not t1

    // At the OLD deadline (t1 + QRV_HOLD_MS = 11000) the hold must still be
    // running against t3, not t1: elapsed since t3 is only 11000 - 4500 =
    // 6500 ms, well short of QRV_HOLD_MS. A header that forgot to reset
    // quiet_since_ on the depth-2 sighting would fire QRV right here.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t1 + BackPressure::QRV_HOLD_MS));

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t3 + BackPressure::QRV_HOLD_MS - 1));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(1, t3 + BackPressure::QRV_HOLD_MS));
}

// c) Anti-flap regression (2026-08-30 bench): depth 2 -> 1 -> 2 within 400 ms
// flapped QRS/QRV/QRS before BP-04. The bench depth (2) predates BP-05
// (2026-08-31), which moved the QRS line to 5 -- 2 no longer raises QRS at
// all (see test_baseline_load_produces_no_notice), so the dip is replayed
// here at 5, the current QRS line, to keep exercising the same BP-04
// mechanism: the hold must swallow the dip: no QRV in between, and going
// back up must not produce a second QRS (latch).
static void test_qrv_water_band_hold_covers_the_anti_flap_regression(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 998));    // own msgs 1-2 at the line
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 999));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS,  bp.onSend(5, false, 1000));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, 1200));            // arms, 200 ms in -- nowhere near the hold
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.onSend(5, false, 1400));   // back up 400 ms after the QRS

    TEST_ASSERT_EQUAL_INT(BP_QRS, bp.state());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRS, bp.latch());
}

// d) F12: reset() during an armed hold must not leak a late QRV at the old
// deadline once a fresh episode is running.
static void test_reset_during_an_armed_hold_leaves_no_late_qrv(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 0));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, 1000));   // arms the hold at 1000

    bp.reset();

    // Same wall-clock deadline the old (discarded) hold would have fired at.
    // If reset() left quiet_armed_/quiet_since_ standing, this poll would
    // wrongly produce an immediate QRV for an episode that was never
    // re-opened (latch is NONE, nothing was ever announced).
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, 1000 + BackPressure::QRV_HOLD_MS));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.latch());
    TEST_ASSERT_FALSE(bp.refusing());
}

// e) millis() rollover: the hold must survive the wrap through UINT32_MAX,
// same as every other now_ms arithmetic in this codebase.
static void test_qrv_water_band_hold_survives_millis_rollover(void)
{
    BackPressure bp(20);

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRT, bp.onSend(16, false, 0));

    const uint32_t t_before_wrap = UINT32_MAX - 100UL;   // 4294967195
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_before_wrap));   // arms just before the wrap

    // Kontrollrechnung (mod 2^32): quiet_since_ + (QRV_HOLD_MS - 1) wraps to
    //   (4294967195 + 9999) - 4294967296 = 9898
    // and quiet_since_ + QRV_HOLD_MS wraps to 9899.
    const uint32_t t_after_wrap_short = 9898UL;   // elapsed == QRV_HOLD_MS - 1
    const uint32_t t_after_wrap_full  = 9899UL;   // elapsed == QRV_HOLD_MS

    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(1, t_after_wrap_short));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV,  bp.poll(1, t_after_wrap_full));
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

    RUN_TEST(test_qrs_fires_once_at_qrs_threshold);
    RUN_TEST(test_qrs_needs_three_own_messages_at_the_line);
    RUN_TEST(test_qrs_own_message_count_restarts_below_the_line);
    RUN_TEST(test_qrs_own_message_count_restarts_via_poll);
    RUN_TEST(test_qrt_qta_and_reset_ignore_the_own_message_count);
    RUN_TEST(test_single_qrs_per_episode_under_a_burst);

    RUN_TEST(test_qrt_at_the_threshold_and_refusal_band);
    RUN_TEST(test_qrt_without_preceding_qrs);
    RUN_TEST(test_qrt_holds_until_the_quiet_band);
    RUN_TEST(test_refusal_announces_once_per_episode);
    RUN_TEST(test_onrefuse_does_not_touch_latch_or_state);
    RUN_TEST(test_no_nack_reason_after_enter_quiet);

    RUN_TEST(test_qta_on_drop_and_only_once);
    RUN_TEST(test_qta_outranks_qrt_and_is_not_followed_by_qrt);
    RUN_TEST(test_qta_after_qrs);

    RUN_TEST(test_baseline_load_produces_no_notice);
    RUN_TEST(test_qrs_exactly_at_the_threshold);
    RUN_TEST(test_qrs_only_episode_closes_silently);
    RUN_TEST(test_qta_only_episode_gets_qrv);
    RUN_TEST(test_qrs_threshold_clamp);

    RUN_TEST(test_qrv_exactly_once_after_a_qrt_episode);
    RUN_TEST(test_qrv_via_onsend_when_the_queue_drained);
    RUN_TEST(test_full_cycle_rearms);
    RUN_TEST(test_reset_clears_the_episode);

    RUN_TEST(test_qrv_water_band_needs_the_full_hold);
    RUN_TEST(test_qrv_water_band_hold_restarts_on_a_deeper_sighting);
    RUN_TEST(test_qrv_water_band_hold_covers_the_anti_flap_regression);
    RUN_TEST(test_reset_during_an_armed_hold_leaves_no_late_qrv);
    RUN_TEST(test_qrv_water_band_hold_survives_millis_rollover);

    RUN_TEST(test_notice_wording);
    RUN_TEST(test_qrs_forecast_on_a_quiet_ring_is_line_plus_two);
    RUN_TEST(test_qrs_forecast_slides_with_foreign_fill);
    RUN_TEST(test_qrs_forecast_moves_left_as_own_msgs_accumulate);
    RUN_TEST(test_qrs_forecast_resets_below_the_line);
    RUN_TEST(test_qrs_forecast_clamps_at_qrt);

    return UNITY_END();
}
