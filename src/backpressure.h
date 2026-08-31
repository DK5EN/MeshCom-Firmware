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

#include <stdint.h>

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

    /// How long depth must sit at QUIET_DEPTH (the water band, 1) before the
    /// episode is allowed to close. DJ8MEH 2026-08-31: an episode idled at
    /// phantom depth 1 for 8 minutes because the ring never quite reached 0;
    /// a real drain reaches CLEAR_DEPTH and closes immediately regardless of
    /// this hold, so the hold only guards against announcing "clear" while
    /// the water band is still occupied. Anti-flap 2026-08-30 (depth
    /// 2 -> 1 -> 2 inside 400 ms) needs only a few hundred ms of hold to stay
    /// covered; the advisor called 5 s already defensible, 10 s more robust
    /// against jitter, and 10 s is negligible against a multi-minute episode.
    static const uint32_t QRV_HOLD_MS = 10000;

    explicit BackPressure(int max_ring) { configure(max_ring); }

    /// (Re)bind to a ring size and drop any running episode.
    void configure(int max_ring)
    {
        max_ring_ = (max_ring > 0) ? max_ring : 1;
        reset();
    }

    void reset()
    {
        state_        = BP_QUIET;
        latch_        = BP_NOTICE_NONE;
        quiet_armed_  = false;
        quiet_since_  = 0;
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
    /// @param now_ms  caller's millis(), injected so this header stays free
    ///                of Arduino and pinnable in a host test
    BpNotice onSend(int depth, bool dropped, uint32_t now_ms)
    {
        if(depth < 0)
            depth = 0;

        // Any sighting above the water band means the ring is not (yet)
        // draining; whatever quiet-hold timer might be running no longer
        // applies and has to be re-armed from a fresh observation.
        if(depth > QUIET_DEPTH)
            quiet_armed_ = false;

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

        // depth <= QUIET_DEPTH: either genuinely clear (closes immediately)
        // or the water band (1), which the QRV_HOLD_MS hysteresis below
        // guards. The latch/state decisions above are unchanged by that
        // hold — only the closing of the episode is delayed.
        return closeOrHold(depth, now_ms);
    }

    /// Cheap per-loop drain check. Only ever produces QRV: a queue that fills
    /// from relay traffic has no sender to warn, and QRV must not become a
    /// heartbeat, so nothing else is raised from here.
    /// @param now_ms caller's millis(), see onSend().
    BpNotice poll(int depth, uint32_t now_ms)
    {
        if(depth < 0)
            depth = 0;

        if(depth > QUIET_DEPTH)
        {
            quiet_armed_ = false;
            return BP_NOTICE_NONE;
        }

        return closeOrHold(depth, now_ms);
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

    /// depth is <= QUIET_DEPTH here (the caller has already re-armed the
    /// hold on anything above it). CLEAR_DEPTH (0) means the ring is
    /// genuinely empty and closes the episode right away, same as before
    /// BP-04. QUIET_DEPTH (1, the water band) instead has to sit still for
    /// QRV_HOLD_MS: the sentinel for "the hold is running" is the bool
    /// quiet_armed_, never quiet_since_ == 0 -- millis() legitimately wraps
    /// through 0, the same lesson as s_have_marker at the top of
    /// txring_functions.cpp.
    BpNotice closeOrHold(int depth, uint32_t now_ms)
    {
        if(depth <= CLEAR_DEPTH)
        {
            quiet_armed_ = false;
            return enterQuiet();
        }

        if(!quiet_armed_)
        {
            quiet_armed_ = true;
            quiet_since_ = now_ms;
            return BP_NOTICE_NONE;
        }

        // Unsigned subtraction: correct even when now_ms has wrapped past
        // quiet_since_ (millis() rollover at ~49.7 days).
        if((uint32_t)(now_ms - quiet_since_) >= QRV_HOLD_MS)
        {
            quiet_armed_ = false;
            return enterQuiet();
        }

        return BP_NOTICE_NONE;
    }

    int      max_ring_;
    BpState  state_;
    BpNotice latch_;
    bool     quiet_armed_;
    uint32_t quiet_since_;
};

#endif // __cplusplus
