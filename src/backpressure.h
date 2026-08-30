#pragma once

// BP-01 (BACKLOG) / TM-37 — back-pressure to the sender, in Q-codes.
//
// The TX ring's fill level has to reach the person who is typing, on the
// transport the message came from — never over the air, because a notice that
// is radiated adds to the very congestion it reports.
//
// This header holds the decision logic only: given the ring depth and whether
// addTxRingEntry() dropped the frame, it says which notice (if any) must be
// emitted now. It is deliberately free of Arduino, of globals and of any I/O,
// so the thresholds, the hysteresis and the episode latch can be pinned by a
// host unit test (test/test_backpressure).
//
// Thresholds are derived from MAX_RING, never hardcoded: MAX_RING is 10, 20 or
// 30 depending on the board (configuration_global.h), so a fixed "16" would
// warn at a different fill level on a T-Beam than on a RAK.
//
// Episode model (operator decision 2026-08-30):
//   - QRS once when the queue starts to build (depth > 1),
//   - QRT once when it reaches 80 % of MAX_RING; new *locally originated user*
//     messages are refused from then on (relay/ACK/beacon keep flowing),
//   - QTA once when the ring actually threw a message away,
//   - QRV exactly once when the depth falls back into the quiet band —
//     and only if at least one of QRS/QRT/QTA was sent in this episode.
//     QRV is the closing bracket of a warning, not a heartbeat.
// The latch runs none -> QRS -> QRT -> QTA and only ever moves upwards inside
// an episode; that is the hysteresis that keeps a burst from producing one
// notice per message (TM-21's lesson). Returning to the quiet band clears the
// latch and re-arms the whole cycle.

#ifdef __cplusplus

/// Coarse state of the sender-facing back-pressure machine.
enum BpState
{
    BP_QUIET = 0,   ///< nothing to report; accepts
    BP_QRS   = 1,   ///< queue building; still accepts
    BP_QRT   = 2    ///< queue at/above the refusal threshold; refuses user messages
};

/// A single notice to be emitted on the originating transport.
/// The first four values are ordered: they double as the episode latch.
enum BpNotice
{
    BP_NOTICE_NONE = 0,
    BP_NOTICE_QRS  = 1,
    BP_NOTICE_QRT  = 2,
    BP_NOTICE_QTA  = 3,
    BP_NOTICE_QRV  = 4   ///< closing bracket, never latched
};

/// Where a locally originated user message came from. Set immediately before
/// sendMessage() by the caller and cleared right after; relay, ACK and beacon
/// paths never set it, so they can never be refused.
enum MsgOrigin
{
    ORIGIN_NONE = 0,
    ORIGIN_SERIAL,
    ORIGIN_BLE,
    ORIGIN_EXTUDP,
    ORIGIN_WEB,
    ORIGIN_GUI
};

/// Q-code of a notice ("QRS", "QRT", "QTA", "QRV"), "" for BP_NOTICE_NONE.
inline const char *bpNoticeCode(BpNotice n)
{
    switch(n)
    {
        case BP_NOTICE_QRS: return "QRS";
        case BP_NOTICE_QRT: return "QRT";
        case BP_NOTICE_QTA: return "QTA";
        case BP_NOTICE_QRV: return "QRV";
        default:            return "";
    }
}

/// Operator-approved wording (BACKLOG BP-01 table). Q-code first, so the
/// text stays readable for someone who does not know the Q-code list.
inline const char *bpNoticeText(BpNotice n)
{
    switch(n)
    {
        case BP_NOTICE_QRS: return "QRS - slow down, TX buffer is filling";
        case BP_NOTICE_QRT: return "QRT - stopping to accept new messages, TX buffer full";
        case BP_NOTICE_QTA: return "QTA - message discarded, TX buffer full";
        case BP_NOTICE_QRV: return "QRV - ready again, TX buffer clear";
        default:            return "";
    }
}

class BackPressure
{
public:
    /// Depth at or below which the ring counts as clear again. One in-flight
    /// entry is the normal state of a working node, not congestion.
    static const int QUIET_DEPTH = 1;
    /// QRV ("TX buffer clear") needs the ring genuinely drained. Closing at
    /// QUIET_DEPTH made the bench flap QRS/QRV/QRS while the radio drained one
    /// frame between two typed messages (depth 2 -> 1 -> 2, 400 ms apart).
    static const int CLEAR_DEPTH = 0;

    explicit BackPressure(int max_ring) { configure(max_ring); }

    /// (Re)bind to a ring size and drop any running episode.
    void configure(int max_ring)
    {
        max_ring_ = (max_ring > 0) ? max_ring : 1;
        reset();
    }

    void reset()
    {
        state_ = BP_QUIET;
        latch_ = BP_NOTICE_NONE;
    }

    int maxRing() const { return max_ring_; }

    /// 80 % of MAX_RING — 8 of 10, 16 of 20, 24 of 30. Kept strictly above the
    /// quiet band so a pathologically small ring cannot make the two collide.
    int refuseThreshold() const
    {
        int t = (max_ring_ * 4) / 5;
        return (t > QUIET_DEPTH + 1) ? t : (QUIET_DEPTH + 1);
    }

    BpState state() const { return state_; }

    /// Highest notice already emitted in the running episode (BP_NOTICE_NONE
    /// while no episode is open).
    BpNotice latch() const { return latch_; }

    /// True while locally originated user messages must be refused.
    bool refusing() const { return state_ == BP_QRT; }

    /// A user message was refused because refusing() is true. Yields the QRT
    /// text at most once per episode — the refusal itself is logged per event,
    /// but the sender is told once per state transition, not once per message.
    BpNotice onRefuse() { return latchIfHigher(BP_NOTICE_QRT); }

    /// Feed the outcome of one enqueue attempt.
    /// @param depth   ring depth *after* the attempt
    /// @param dropped true when addTxRingEntry() returned -1
    BpNotice onSend(int depth, bool dropped)
    {
        if(depth < 0)
            depth = 0;

        // A real drop is the most specific news there is, so it outranks the
        // plain threshold notice — and it always implies the refusal state.
        if(dropped)
        {
            state_ = BP_QRT;
            return latchIfHigher(BP_NOTICE_QTA);
        }

        if(depth >= refuseThreshold())
        {
            state_ = BP_QRT;
            return latchIfHigher(BP_NOTICE_QRT);
        }

        if(depth > QUIET_DEPTH)
        {
            // Hysteresis: QRT is *not* released here. Once the node has said
            // "stop", it stays stopped until the ring is genuinely clear —
            // otherwise it would flip between accepting and refusing around
            // the threshold and produce a notice per message.
            if(state_ != BP_QRT)
                state_ = BP_QRS;
            return latchIfHigher(BP_NOTICE_QRS);
        }

        // depth 1: below the QRS line, but not "clear" -- keep the episode open
        return (depth <= CLEAR_DEPTH) ? enterQuiet() : BP_NOTICE_NONE;
    }

    /// Cheap per-loop drain check. Only ever produces QRV: a queue that fills
    /// from relay traffic has no sender to warn, and QRV must not become a
    /// heartbeat, so nothing else is raised from here.
    BpNotice poll(int depth)
    {
        if(depth < 0)
            depth = 0;

        if(depth <= CLEAR_DEPTH)
            return enterQuiet();

        return BP_NOTICE_NONE;
    }

private:
    BpNotice latchIfHigher(BpNotice n)
    {
        if(n > latch_)
        {
            latch_ = n;
            return n;
        }
        return BP_NOTICE_NONE;
    }

    /// Back in the quiet band: close the episode. QRV only if the episode
    /// actually said something — a node that was never under pressure never
    /// announces that it is ready.
    BpNotice enterQuiet()
    {
        state_ = BP_QUIET;
        if(latch_ != BP_NOTICE_NONE)
        {
            latch_ = BP_NOTICE_NONE;
            return BP_NOTICE_QRV;
        }
        return BP_NOTICE_NONE;
    }

    int      max_ring_;
    BpState  state_;
    BpNotice latch_;
};

#endif // __cplusplus
