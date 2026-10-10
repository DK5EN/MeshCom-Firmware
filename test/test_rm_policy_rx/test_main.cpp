// RM ext W1c: the REAL sender policy / sent book (rm_sender_policy.h) against the REAL receiver
// (remote_cmd.cpp rmCheck/rmAccept) in virtual time. Regression for verdict finding 4: a sender with a
// wrong key must never lock the target, whatever the path delay jitter.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "remote_cmd.h"
#include "rm_queue.h"
#include "rm_rx_gate.h"
#include "rm_sender_policy.h"

static_assert(RM_POLICY_WINDOW_MS >= RM_REJ_WINDOW_MS + 60000u, "sender window must exceed the receiver reject window");

void setUp(void) {}
void tearDown(void) {}

static const char *DST = "DK5EN-90";
static const char *SRC = "DK5EN-14";
static const char *PW = "rightpw";

struct Frame
{
    uint32_t arriveMs;
    RmCmd cmd;
    uint32_t sentMs;
    int slot;  // index into Sim::book by sentMs match
};

struct Sim
{
    RmState rs;
    uint32_t rng;
    std::vector<RmPolEntry> book;  // newest first
    std::vector<Frame> inflight;
    uint32_t ctr;
    uint32_t counted;
    uint32_t rate;
    uint32_t sentTotal;
    uint8_t maxUnanswered;
    bool locked;
    Sim() : rng(12345), ctr(0), counted(0), rate(0), sentTotal(0), maxUnanswered(0), locked(false)
    {
        memset(&rs, 0, sizeof(rs));
        rs.strict = true;  // BF-01: these simulations model the flag-ON pair (sender policy + receiver lockout)
    }
    uint32_t rnd() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

    // sends one frame (cmd "sync" has ctr 0) with `pass`, arriving after `delayMs`
    void send(const char *pass, const char *cmd, uint32_t now, uint32_t delayMs)
    {
        uint8_t key[32];
        rmDeriveKey(pass, key);
        const bool isSync = strcmp(cmd, "sync") == 0;
        char wire[96];
        TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, isSync ? 0 : ++ctr, cmd, "", key, wire, sizeof(wire)) > 0);
        Frame f;
        TEST_ASSERT_TRUE(rmParse(wire, f.cmd));
        f.arriveMs = now + delayMs;
        f.sentMs = now;
        f.slot = 0;
        inflight.push_back(f);
        RmPolEntry e;
        memset(&e, 0, sizeof(e));
        e.sentMs = now;
        // newest first; evict via the real book rule (never a counted entry)
        int v = rmBookVictim(book.data(), (uint8_t)book.size(), (uint8_t)RM_POLICY_MAX_ENTRIES, now);
        TEST_ASSERT_TRUE(v != -1);
        if (v >= 0)
            book.erase(book.begin() + v);
        book.insert(book.begin(), e);
        sentTotal++;
    }

    // delivers every frame that has arrived by `now`, in arrival order
    void deliver(uint32_t now, const char *rightPw, uint32_t replyLossPct, bool *verifiedOut)
    {
        std::sort(inflight.begin(), inflight.end(), [](const Frame &a, const Frame &b) { return a.arriveMs < b.arriveMs; });
        while (!inflight.empty() && inflight.front().arriveMs <= now)
        {
            Frame f = inflight.front();
            inflight.erase(inflight.begin());
            const RmVerdict v = rmCheck(rs, f.cmd, DST, SRC, rightPw, 22, f.arriveMs);
            if (v == RM_OK)
            {
                rmAccept(rs, f.cmd, "ok", f.arriveMs);
            }
            if (v == RM_REJ_TAG || v == RM_REJ_REPLAY || v == RM_REJ_FORMAT || v == RM_REJ_BLOCKED)
                counted++;
            if (v == RM_REJ_RATE)
                rate++;
            if (rmSenderLocked(rs, SRC, f.arriveMs))
                locked = true;
            if ((v == RM_OK || v == RM_SYNC) && (rnd() % 100) >= replyLossPct)
            {
                for (auto &e : book)
                    if (e.sentMs == f.sentMs)
                        e.replied = e.verified = true;  // the verified reply reached the sender
                if (verifiedOut)
                    *verifiedOut = true;
            }
        }
    }
    void age(uint32_t now)
    {
        for (auto &e : book)
            if (rmPolicyAgeMs(now, e.sentMs) >= 300000u)
                e.expired = true;
    }
};

// wrong key, fastest allowed rate, 30 simulated minutes, delay base + jitter swept; chain = sync first
static uint32_t run_wrong_key(uint32_t base, uint32_t jitter, bool useSync, bool *lockedOut)
{
    Sim s;
    s.rng = 777u + base * 31u + jitter;
    for (uint32_t now = 0; now < 1800000u; now += 1000u)
    {
        s.age(now);
        s.deliver(now, PW, 0, nullptr);
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, RM_POLICY_LIMIT_UNPROVEN);
        if (d.unanswered > s.maxUnanswered)
            s.maxUnanswered = d.unanswered;
        if (d.allowed && !s.locked)
            s.send("wrongpw", useSync ? "sync" : "status", now, (base + (jitter ? s.rnd() % (jitter + 1) : 0)) * 1000u);
    }
    s.deliver(4000000u, PW, 0, nullptr);
    *lockedOut = s.locked;
    return s.counted;
}

void test_wrong_key_at_fastest_allowed_rate_never_locks_target(void)
{
    uint32_t total = 0;
    for (uint32_t base = 0; base <= 10; base += 5)
        for (uint32_t jit = 0; jit <= 20; jit += 10)
            for (int sync = 0; sync < 2; sync++)
            {
                bool locked = false;
                total += run_wrong_key(base, jit, sync != 0, &locked);
                TEST_ASSERT_FALSE_MESSAGE(locked, "receiver locked by a wrong-key sender inside the policy");
            }
    // delay 30 s fixed, and a worst case swing of 0 / 30 s
    bool locked = false;
    total += run_wrong_key(30, 0, false, &locked);
    TEST_ASSERT_FALSE(locked);
    total += run_wrong_key(0, 30, false, &locked);
    TEST_ASSERT_FALSE(locked);
    TEST_ASSERT_TRUE_MESSAGE(total > 20, "the run must produce tag rejects (otherwise it proves nothing)");
    printf("wrong-key counted rejects over all runs: %u\n", (unsigned)total);
}

void test_right_key_proven_budget_10(void)
{
    Sim s;
    bool proven = false;
    uint32_t sent = 0;
    for (uint32_t now = 0; now < 1800000u; now += 1000u)
    {
        s.age(now);
        bool v = false;
        s.deliver(now, PW, proven ? 100 : 0, &v);  // first reply gets through, then all replies are lost
        proven = proven || v;
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now,
                                                proven ? RM_POLICY_LIMIT_PROVEN : RM_POLICY_LIMIT_UNPROVEN);
        if (d.unanswered > s.maxUnanswered)
            s.maxUnanswered = d.unanswered;
        if (d.allowed)
        {
            s.send(PW, "status", now, 1000u + (s.rnd() % 8000u));
            sent++;
        }
    }
    s.deliver(4000000u, PW, 100, nullptr);
    TEST_ASSERT_TRUE(proven);
    TEST_ASSERT_EQUAL_UINT32(0, s.counted);
    TEST_ASSERT_FALSE(s.locked);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, s.maxUnanswered);
    TEST_ASSERT_TRUE(sent > 20);
}

void test_rekey_one_shot(void)
{
    Sim s;
    RmProof pb[RM_PROOF_N];
    memset(pb, 0, sizeof(pb));
    const uint8_t fpA[4] = {1, 2, 3, 4}, fpB[4] = {9, 9, 9, 9};
    RmProof *p = rmProofSetKey(pb, DST, fpA);
    TEST_ASSERT_FALSE(p->oneShot);  // a first key arms nothing
    s.send("wrongA", "status", 0, 2000u);
    s.send("wrongA", "status", 10000u, 2000u);
    p = rmProofSetKey(pb, DST, fpB);  // re-key
    TEST_ASSERT_TRUE(p->oneShot);
    p = rmProofSetKey(pb, DST, fpB);  // same key again: no new arming, no refill
    uint32_t now = 20000u;
    RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, rmProofLimit(p), p->oneShot, false);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL(RM_POL_LIMIT, d.reason);
    TEST_ASSERT_TRUE(d.canForce);
    d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, rmProofLimit(p), p->oneShot, true);
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_TRUE(d.usedForce);
    p->oneShot = false;  // the caller disarms
    s.send("wrongB", "status", now, 2000u);
    s.deliver(now + 5000u, PW, 0, nullptr);
    // accepted consequence of decision 5: the third counted reject inside 90 s locks the target
    TEST_ASSERT_EQUAL_UINT32(3, s.counted);
    TEST_ASSERT_TRUE(s.locked);
    p = rmProofSetKey(pb, DST, fpB);
    TEST_ASSERT_FALSE(p->oneShot);  // same-key re-save never re-arms
    // a FOURTH attempt is impossible until the window has passed
    for (now = 21000u; now < 10000u + RM_POLICY_WINDOW_MS; now += 1000u)
    {
        d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, rmProofLimit(p), p->oneShot, true);
        TEST_ASSERT_FALSE(d.allowed);
        TEST_ASSERT_FALSE(d.canForce);
    }
    d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), 10000u + RM_POLICY_WINDOW_MS, 2, false, false);
    TEST_ASSERT_TRUE(d.allowed);
}

// Runtime sequence of rmForgetTarget() + re-add (rm_runtime.cpp), same pure calls in the same order:
//   per send: rmProofSetKey(fp) -> rmPolicyMaySend(book, now, rmProofLimit(p), p->oneShot, force) -> [send]
//   forget:   rmProofForget(pb, dst); the book entries are NOT touched (they keep counting)
void test_forget_does_not_refill_budget(void)
{
    const uint8_t fpA[4] = {1, 2, 3, 4}, fpB[4] = {9, 9, 9, 9};
    {
        // (a) delete + re-add with a new wrong key: proof and one-shot are gone, the book still counts
        Sim s;
        RmProof pb[RM_PROOF_N];
        memset(pb, 0, sizeof(pb));
        RmProof *p = rmProofSetKey(pb, DST, fpA);
        s.send("wrongA", "status", 0, 2000u);
        s.send("wrongA", "status", 10000u, 2000u);
        rmProofForget(pb, DST);  // rmForgetTarget(): entries stay
        p = rmProofSetKey(pb, DST, fpB);  // re-add: a FIRST key arms nothing
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_FALSE(p->oneShot);
        for (uint32_t now = 20000u; now < RM_POLICY_WINDOW_MS; now += 1000u)
        {
            RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, rmProofLimit(p), p->oneShot, true);
            TEST_ASSERT_FALSE(d.allowed);
            TEST_ASSERT_EQUAL(RM_POL_LIMIT, d.reason);
            TEST_ASSERT_FALSE(d.canForce);
            TEST_ASSERT_FALSE(d.usedForce);
        }
        s.deliver(30000u, PW, 0, nullptr);
        TEST_ASSERT_TRUE(s.counted <= 3);
        TEST_ASSERT_FALSE(s.locked);
    }
    {
        // (b) only the password changes (no forget): exactly the one-shot, nothing without force
        Sim s;
        RmProof pb[RM_PROOF_N];
        memset(pb, 0, sizeof(pb));
        RmProof *p = rmProofSetKey(pb, DST, fpA);
        s.send("wrongA", "status", 0, 2000u);
        s.send("wrongA", "status", 10000u, 2000u);
        p = rmProofSetKey(pb, DST, fpB);
        TEST_ASSERT_TRUE(p->oneShot);
        RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), 20000u, rmProofLimit(p), p->oneShot, false);
        TEST_ASSERT_FALSE(d.allowed);
        TEST_ASSERT_TRUE(d.canForce);
        d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), 20000u, rmProofLimit(p), p->oneShot, true);
        TEST_ASSERT_TRUE(d.allowed);
        TEST_ASSERT_TRUE(d.usedForce);
        p->oneShot = false;
        s.send("wrongB", "status", 20000u, 2000u);
        d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), 30000u, rmProofLimit(p), p->oneShot, true);
        TEST_ASSERT_FALSE(d.allowed);
        TEST_ASSERT_FALSE(d.canForce);
        s.deliver(40000u, PW, 0, nullptr);
        TEST_ASSERT_TRUE(s.counted <= 3);
    }
}

// ---- reordering: the right key must never lock the target (advisor D1 / D2) -----------------------
// Frames are sent as the policy allows (10 s apart); the path delay alternates 30 s / 0 s (or is drawn
// from a seeded generator, 0..30 s) so slow frames are overtaken by faster ones. A reply travels back
// with its frame's own delay. The sender limit is what the real proof book gives: 10 only when proven
// AND the target reported cap >= 2 in its sync reply.
// oldReceiverModel: the test itself counts a strike for every RM_REJ_REPLAY verdict and for every
// counted reject (what a target that still runs the pre-fix receiver does) and asserts fewer than
// RM_REJ_LIMIT inside any 90 s window. The real rmCheck stays the NEW receiver in both modes.
static void run_right_key_reordered(bool oldReceiverModel, bool randomDelay, uint8_t cap)
{
    RmProof pb[RM_PROOF_N];
    memset(pb, 0, sizeof(pb));
    const uint8_t fp[4] = {1, 2, 3, 4};
    RmProof *proof = rmProofSetKey(pb, DST, fp);
    rmProofSetCap(pb, DST, fp, cap);
    uint32_t rnd = 20261006u;
    uint8_t maxLimit = 0;
    Sim s;
    struct Reply { uint32_t at; uint32_t sentMs; };
    std::vector<Reply> replies;
    std::vector<uint32_t> strikes;
    uint32_t n = 0;
    uint32_t accepted = 0;
    for (uint32_t now = 0; now < 600000u; now += 1000u)
    {
        // replies that reached the sender: the entry becomes verified (the key is proven)
        for (size_t i = 0; i < replies.size();)
        {
            if (replies[i].at <= now)
            {
                for (auto &e : s.book)
                    if (e.sentMs == replies[i].sentMs)
                        e.replied = e.verified = true;
                replies.erase(replies.begin() + i);
            }
            else
                i++;
        }
        // frames that reached the target, in arrival order
        std::sort(s.inflight.begin(), s.inflight.end(), [](const Frame &a, const Frame &b) { return a.arriveMs < b.arriveMs; });
        while (!s.inflight.empty() && s.inflight.front().arriveMs <= now)
        {
            Frame f = s.inflight.front();
            s.inflight.erase(s.inflight.begin());
            const RmVerdict v = rmCheck(s.rs, f.cmd, DST, SRC, PW, 22, f.arriveMs);
            if (v == RM_OK)
            {
                rmAccept(s.rs, f.cmd, "ok", f.arriveMs);
                accepted++;
                replies.push_back({f.arriveMs + (f.arriveMs - f.sentMs), f.sentMs});
            }
            if (oldReceiverModel && (v == RM_REJ_REPLAY || v == RM_REJ_FORMAT || v == RM_REJ_TAG || v == RM_REJ_BLOCKED))
                strikes.push_back(f.arriveMs);
            if (rmSenderLocked(s.rs, SRC, f.arriveMs))
                s.locked = true;
        }
        s.age(now);
        bool proven = false;
        for (const auto &e : s.book)
            proven = proven || e.verified;
        if (proven)
            rmProofVerified(pb, DST, fp);
        const uint8_t limit = rmProofLimit(proof);
        if (limit > maxLimit)
            maxLimit = limit;
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, limit);
        if (d.allowed)
        {
            uint32_t delay = (n++ % 2 == 0) ? 30000u : 0u;
            if (randomDelay)
            {
                rnd = rnd * 1664525u + 1013904223u;
                delay = ((rnd >> 8) % 31u) * 1000u;
            }
            s.send(PW, "status", now, delay);
        }
    }
    TEST_ASSERT_EQUAL_UINT8(cap >= 2 ? RM_POLICY_LIMIT_PROVEN : RM_POLICY_LIMIT_UNPROVEN, maxLimit);
    TEST_ASSERT_FALSE(s.locked);
    TEST_ASSERT_TRUE(accepted > 0);
    // old receiver: never RM_REJ_LIMIT strikes inside one 90 s window
    for (size_t i = 0; i + RM_REJ_LIMIT - 1 < strikes.size(); i++)
        TEST_ASSERT_TRUE(strikes[i + RM_REJ_LIMIT - 1] - strikes[i] >= RM_REJ_WINDOW_MS);
}

void test_right_key_reordered_frames_never_lock(void)
{
    // (i) NEW receiver (the real rmCheck: a valid-tag stale counter is no strike) + sender limit 10 (proven, cap 2)
    run_right_key_reordered(false, false, 2);
    run_right_key_reordered(false, true, 2);
    // (ii) OLD receiver model (every REPLAY counts) + the limit the policy gives a cap-0 target: 2 although proven
    run_right_key_reordered(true, false, 0);
    run_right_key_reordered(true, true, 0);
}

// D2: wrong key, the first frame is the slowest and the last the fastest; the sender frees an entry
// at age >= RM_POLICY_WINDOW_MS from booking, the target's window is measured between ARRIVALS.
static void run_wrong_key_edge(uint32_t firstDelayMs)
{
    Sim s;
    uint32_t n = 0;
    for (uint32_t now = 0; now < 600000u; now += 1000u)
    {
        s.age(now);
        s.deliver(now, PW, 0, nullptr);
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now, RM_POLICY_LIMIT_UNPROVEN);
        if (d.allowed)
        {
            uint32_t delay = 0;
            if (n == 0)
                delay = firstDelayMs;
            else if (n == 1 && firstDelayMs >= 5000u)
                delay = firstDelayMs - 5000u;
            s.send("wrongpw", "status", now, delay);
            n++;
        }
    }
    s.deliver(4000000u, PW, 0, nullptr);
    TEST_ASSERT_FALSE(s.locked);
}

void test_wrong_key_window_edge_30s_delay(void)
{
    // the reviewer's sequence: f1 sent 0 (30 s), f2 sent 10 s (25 s); the third send must not be
    // allowed before the sender window has passed, and then it is not a 3rd strike inside 90 s
    {
        Sim s;
        s.send("wrongpw", "status", 0u, 30000u);
        s.send("wrongpw", "status", 10000u, 25000u);
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), 120000u, RM_POLICY_LIMIT_UNPROVEN);
        TEST_ASSERT_FALSE(d.allowed);
        TEST_ASSERT_EQUAL(RM_POL_LIMIT, d.reason);
        s.send("wrongpw", "status", 150000u, 0u);
        s.deliver(200000u, PW, 0, nullptr);
        TEST_ASSERT_EQUAL_UINT32(3, s.counted);
        TEST_ASSERT_FALSE(s.locked);
    }
    for (uint32_t d0 = 0; d0 <= 30000u; d0 += 1000u)
        run_wrong_key_edge(d0);
}

// ---- RM-PROOF: the TARGET gets a new password, the sender still holds the old, proven key -----------
// Bench 2026-10-06: five old-key frames inside the proven budget of 10 locked DK5EN-1 for 5 minutes.
// The sender sends as fast as rmPolicyLimit() allows for 30 simulated minutes; the target must never lock.
static void run_target_rekeyed(uint32_t base, uint32_t jitter)
{
    Sim s;
    RmProof pb[RM_PROOF_N];
    memset(pb, 0, sizeof(pb));
    const uint8_t fp[4] = {7, 7, 7, 7};
    RmProof *p = rmProofSetKey(pb, DST, fp);
    // phase 1: right key, replies arrive: proven, capability 2
    bool v = false;
    s.send(PW, "status", 0, 1000u);
    s.deliver(2000u, PW, 0, &v);
    TEST_ASSERT_TRUE(v);
    rmProofVerified(pb, DST, fp);
    rmProofSetCap(pb, DST, fp, 2);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmPolicyLimit(p, s.book.data(), (uint8_t)s.book.size(), 3000u));
    // phase 2: the target now has another password; the sender keeps signing with the old one
    uint32_t sent = 0;
    for (uint32_t now = 20000u; now < 1800000u; now += 1000u)
    {
        s.age(now);
        s.deliver(now, "newpw", 0, nullptr);
        TEST_ASSERT_FALSE_MESSAGE(s.locked, "target locked by a proven sender after the target was re-keyed");
        const RmPolDecision d = rmPolicyMaySend(s.book.data(), (uint8_t)s.book.size(), now,
                                                rmPolicyLimit(p, s.book.data(), (uint8_t)s.book.size(), now), p->oneShot, false);
        if (d.allowed)
        {
            s.send(PW, "status", now, base + (jitter ? s.rnd() % jitter : 0));
            sent++;
        }
    }
    s.deliver(4000000u, "newpw", 0, nullptr);
    TEST_ASSERT_FALSE(s.locked);
    TEST_ASSERT_TRUE_MESSAGE(sent > 10 && s.counted > 10, "the run must produce tag rejects");
}

void test_target_rekeyed_proven_sender_never_locks(void)
{
    run_target_rekeyed(2000u, 0);
    run_target_rekeyed(0, 30000u);
    run_target_rekeyed(30000u, 0);
}

// the proof comes back with the next verified reply, and losses that are not in a row keep the budget
void test_proof_streak_rule(void)
{
    RmProof pb[RM_PROOF_N];
    memset(pb, 0, sizeof(pb));
    const uint8_t fp[4] = {1, 1, 1, 1};
    RmProof *p = rmProofSetKey(pb, DST, fp);
    rmProofVerified(pb, DST, fp);
    rmProofSetCap(pb, DST, fp, 2);
    RmPolEntry e[6];
    memset(e, 0, sizeof(e));
    const uint32_t now = 100000u;
    // lost, answered, lost, answered, lost (newest): streak 1, budget stays 10
    for (int i = 0; i < 5; i++)
    {
        e[i].sentMs = now - 10000u * (uint32_t)(i + 1);
        e[i].replied = e[i].verified = (i % 2) == 1;
    }
    TEST_ASSERT_EQUAL_UINT8(1, rmPolicyUnverifiedStreak(e, 5, now));
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmPolicyLimit(p, e, 5, now));
    // two in a row without a verified reply (the newest two): budget 2
    e[1].replied = e[1].verified = false;
    e[2].replied = e[2].verified = true;
    TEST_ASSERT_EQUAL_UINT8(2, rmPolicyUnverifiedStreak(e, 5, now));
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmPolicyLimit(p, e, 5, now));
    // an unverified reply is no answer
    e[0].replied = true;
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmPolicyLimit(p, e, 5, now));
    // the newest one verifies: the proof is back
    e[0].verified = true;
    TEST_ASSERT_EQUAL_UINT8(0, rmPolicyUnverifiedStreak(e, 5, now));
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmPolicyLimit(p, e, 5, now));
    // expired entries do not count, an unproven target stays at 2, no entries = 0
    e[0].verified = e[0].replied = false;
    e[0].expired = e[1].expired = true;
    TEST_ASSERT_EQUAL_UINT8(0, rmPolicyUnverifiedStreak(e, 5, now));
    TEST_ASSERT_EQUAL_UINT8(0, rmPolicyUnverifiedStreak(nullptr, 0, now));
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmPolicyLimit(nullptr, e, 5, now));
}

// ---- per-sender lockout: junk under other calls never locks the operator --------------------------
// Operator decision 2026-10-06: the reject counter is keyed on the sender's callsign-SSID. Twenty
// other calls send wrong-tag frames as fast as they like (from the internet side nothing slows them
// down) for 30 simulated minutes while the operator, with the right key, sends a command every 15 s.
// Every one of the operator's commands must be accepted; the operator is never locked.
void test_junk_from_other_senders_never_locks_the_operator(void)
{
    RmState rs;
    rmStateInit(rs, 0);
    rs.strict = true; // BF-01: the per-sender lockout exists only with strict security on
    uint8_t good[32], bad[32];
    rmDeriveKey(PW, good);
    rmDeriveKey("guess", bad);
    uint32_t ctr = 0, accepted = 0, sent = 0, junk = 0, junkLocked = 0, rng = 99;
    for (uint32_t now = 1000u; now < 1800000u; now += 500u)
    {
        // junk: two frames per second, rotating over 20 foreign calls
        char call[10], wire[96];
        rng = rng * 1664525u + 1013904223u;
        snprintf(call, sizeof(call), "XX%uXX-%u", (unsigned)((rng >> 8) % 10), (unsigned)((rng >> 16) % 2));
        RmCmd j;
        TEST_ASSERT_TRUE(rmBuildCommand(DST, call, 1000u + junk, "status", "", bad, wire, sizeof(wire)) > 0);
        TEST_ASSERT_TRUE(rmParse(wire, j));
        const RmVerdict jv = rmCheck(rs, j, DST, call, PW, 22, now);
        TEST_ASSERT_TRUE(jv == RM_REJ_TAG || jv == RM_REJ_LOCKOUT);
        junk++;
        junkLocked += (jv == RM_REJ_LOCKOUT);

        if (now % 15000u == 1000u)
        {
            RmCmd c;
            TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, ++ctr, "status", "", good, wire, sizeof(wire)) > 0);
            TEST_ASSERT_TRUE(rmParse(wire, c));
            const RmVerdict v = rmCheck(rs, c, DST, SRC, PW, 22, now);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", rmVerdictName(v), "the operator's command was not accepted");
            rmAccept(rs, c, "ok", now);
            accepted++;
            sent++;
        }
        TEST_ASSERT_FALSE(rmSenderLocked(rs, SRC, now));
    }
    TEST_ASSERT_EQUAL_UINT32(sent, accepted);
    TEST_ASSERT_TRUE(sent > 100 && junk > 3000);
    TEST_ASSERT_TRUE_MESSAGE(junkLocked > 0, "the junk senders themselves must run into their own lockout");
}

// ---- BF-01: the flag pair --------------------------------------------------------------------------
// D3: with the sender's flag OFF the whole sender policy is skipped (budget, cooldown, one-shot).
void test_off_sender_policy_allows_every_send(void)
{
    std::vector<RmPolEntry> book;
    for (uint32_t i = 0; i < RM_POLICY_MAX_ENTRIES; i++)
    {
        RmPolEntry e;
        memset(&e, 0, sizeof(e));
        e.sentMs = 50000u + i;  // twelve unanswered sends within the last second: budget AND cooldown hit
        book.push_back(e);
    }
    const uint32_t now = 50100u;
    const RmPolDecision on = rmPolicyMaySend(book.data(), (uint8_t)book.size(), now, RM_POLICY_LIMIT_UNPROVEN, true, true, true);
    TEST_ASSERT_TRUE_MESSAGE(on.usedForce || !on.allowed, "flag ON must still limit this book");
    const RmPolDecision limited = rmPolicyMaySend(book.data(), (uint8_t)book.size(), now, RM_POLICY_LIMIT_UNPROVEN, false, false, true);
    TEST_ASSERT_FALSE(limited.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, limited.reason);
    const RmPolDecision off = rmPolicyMaySend(book.data(), (uint8_t)book.size(), now, RM_POLICY_LIMIT_UNPROVEN, true, true, false);
    TEST_ASSERT_TRUE(off.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_OK, off.reason);
    TEST_ASSERT_EQUAL_UINT32(0, off.retryS);
    TEST_ASSERT_FALSE(off.canForce);
    TEST_ASSERT_FALSE_MESSAGE(off.usedForce, "the one-shot must not be spent when the policy is off");
}

// Wrong-key frames from one call, one per second, straight into rmCheck; returns the first index that
// hit the lockout, or -1.
static int flood_wrong_key(RmState &rs, uint32_t frames)
{
    uint8_t bad[32];
    rmDeriveKey("guess", bad);
    for (uint32_t i = 0; i < frames; i++)
    {
        char wire[96];
        RmCmd c;
        TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, i + 1, "status", "", bad, wire, sizeof(wire)) > 0);
        TEST_ASSERT_TRUE(rmParse(wire, c));
        const RmVerdict v = rmCheck(rs, c, DST, SRC, PW, 22, 1000u + i * 1000u);
        if (v == RM_REJ_LOCKOUT)
            return (int)i;
        TEST_ASSERT_EQUAL_STRING("tag", rmVerdictName(v));
    }
    return -1;
}

// D3 consequence (documented in the --rmstrictsecurity help): a flag-OFF sender does not hold back, so
// it can push a flag-ON target into its lockout with its own wrong key.
void test_off_sender_can_push_an_on_target_into_lockout(void)
{
    RmState rs;
    rmStateInit(rs, 0);
    rs.strict = true;
    TEST_ASSERT_EQUAL_INT(3, flood_wrong_key(rs, 60)); // strikes at 0, 1, 2; the fourth frame meets the lock
}

// D1/D2: both sides OFF, an unbounded wrong-key stream is answered by silence (REJ_TAG) and never locks.
void test_both_off_wrong_key_stream_never_locks(void)
{
    RmState rs;
    rmStateInit(rs, 0);
    TEST_ASSERT_EQUAL_INT(-1, flood_wrong_key(rs, 1800));
    TEST_ASSERT_EQUAL_UINT8(0, rmLockedSenders(rs, 1800000u, nullptr));
}

// ---- CR-02: a stale learnt counter mark recovers without a reboot -----------------------------------------------
// Two senders manage ONE node with the same password. A has a trusted clock (ctr = unix time, about 1.79e9) and
// raises the target's HWM. B has no clock and a learnt mark of 5000: its ctr = mark + 1 is a replay for the target
// (silent on air, no reply). Before CR-02 the mark was never dropped, so B was dead until a reboot. The mini sender
// below is the glue of rm_runtime.cpp sendKeyImpl() with the same pure calls in the same order:
//   rmPeerMarkStale(book) -> drop the mark -> [rmPolicyNeedSync: no clock, no mark] sync, then the command.
struct MarkSender
{
    const char *call;
    bool haveMark;
    uint32_t mark;
    uint32_t lastSent;
    std::vector<RmPolEntry> book;  // newest first
    uint32_t syncs;
    uint32_t accepted;
    uint32_t replays;
    MarkSender(const char *c, uint32_t m) : call(c), haveMark(true), mark(m), lastSent(0), syncs(0), accepted(0), replays(0) {}
};

static RmVerdict deliverFrame(RmState &rs, const char *from, const char *cmd, uint32_t ctr, uint32_t now)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    char wire[96];
    TEST_ASSERT_TRUE(rmBuildCommand(DST, from, ctr, cmd, "", key, wire, sizeof(wire)) > 0);
    RmCmd c;
    TEST_ASSERT_TRUE(rmParse(wire, c));
    const RmVerdict v = rmCheck(rs, c, DST, from, PW, 22, now);
    if (v == RM_OK)
        rmAccept(rs, c, "ok", now);
    return v;
}

static void bookAdd(std::vector<RmPolEntry> &book, uint32_t sentMs, bool isSync, bool answered)
{
    RmPolEntry e;
    memset(&e, 0, sizeof(e));
    e.sentMs = sentMs;
    e.isSync = isSync;
    e.replied = e.verified = answered;
    const int v = rmBookVictim(book.data(), (uint8_t)book.size(), (uint8_t)RM_POLICY_MAX_ENTRIES, sentMs);
    TEST_ASSERT_TRUE(v != -1);
    if (v >= 0)
        book.erase(book.begin() + v);
    book.insert(book.begin(), e);
}

// One send of the clockless sender. recovery=false is the code before CR-02 (the mark is never dropped).
static void markSenderSend(RmState &rs, MarkSender &b, uint32_t now, bool recovery, bool strict)
{
    if (recovery && b.haveMark && rmPeerMarkStale(b.book.data(), (uint8_t)b.book.size(), now))
        b.haveMark = false;
    const RmPolDecision d = rmPolicyMaySend(b.book.data(), (uint8_t)b.book.size(), now, RM_POLICY_LIMIT_UNPROVEN, false,
                                            false, strict);
    if (!d.allowed)
        return;
    uint32_t at = now;
    if (!b.haveMark)  // rmPolicyNeedSync(clock = false, mark = false, isSync = false)
    {
        TEST_ASSERT_TRUE(rmPolicyNeedSync(false, false, false));
        b.syncs++;
        TEST_ASSERT_EQUAL_INT(RM_SYNC, deliverFrame(rs, b.call, "sync", 0, now));
        bookAdd(b.book, now, true, true);  // the sync reply verified
        b.mark = rs.hwm;                   // peerSet() from the sync result
        b.haveMark = true;
        at = now + RM_PEND_SEND_DELAY_MS;  // the queued command goes out this long after the verified sync
    }
    b.lastSent = (b.lastSent + 1 > b.mark + 1) ? b.lastSent + 1 : b.mark + 1;  // max(last sent + 1, mark + 1)
    const RmVerdict v = deliverFrame(rs, b.call, "status", b.lastSent, at);
    if (v == RM_OK)
    {
        bookAdd(b.book, at, false, true);
        b.mark = b.lastSent;
        b.accepted++;
    }
    else
    {
        TEST_ASSERT_EQUAL_INT(RM_REJ_REPLAY, v);  // silent on air: no reply ever comes
        bookAdd(b.book, at, false, false);
        b.replays++;
    }
}

// A (clock) and B (mark 5000): B sends every 80 s. Returns B for inspection.
static MarkSender runTwoSenders(bool recovery, bool strict, uint32_t attempts, uint32_t *aAccepted)
{
    RmState rs;
    rmStateInit(rs, 0);
    rs.strict = strict;
    MarkSender b("DK5EN-15", 5000u);
    TEST_ASSERT_EQUAL_INT(RM_OK, deliverFrame(rs, SRC, "status", 5000u, 500u));  // B's mark was learnt from this
    *aAccepted = 0;
    // A (trusted clock): ctr = unix time
    if (deliverFrame(rs, SRC, "status", 1790000000u, 12000u) == RM_OK)
        (*aAccepted)++;
    for (uint32_t i = 0; i < attempts; i++)
        markSenderSend(rs, b, 20000u + i * 80000u, recovery, strict);
    // A is unaffected and still gets through afterwards
    if (deliverFrame(rs, SRC, "status", 1790000000u + 20000u + attempts * 80u + 5u, 20000u + attempts * 80000u + 30000u) == RM_OK)
        (*aAccepted)++;
    return b;
}

void test_cr02_before_stale_mark_never_recovers(void)
{
    uint32_t a = 0;
    const MarkSender b = runTwoSenders(false, false, 12, &a);  // the code before CR-02
    TEST_ASSERT_EQUAL_UINT32(2, a);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, b.accepted, "without CR-02 the clockless sender is dead for good");
    TEST_ASSERT_EQUAL_UINT32(12, b.replays);
    TEST_ASSERT_EQUAL_UINT32(0, b.syncs);
}

void test_cr02_stale_mark_recovers_without_reboot(void)
{
    for (int strict = 0; strict < 2; strict++)
    {
        uint32_t a = 0;
        const MarkSender b = runTwoSenders(true, strict != 0, 12, &a);
        TEST_ASSERT_EQUAL_UINT32(2, a);
        // attempts 1 and 2 are replays (no reply), the third drops the mark, syncs, and goes through
        TEST_ASSERT_EQUAL_UINT32(2, b.replays);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, b.syncs, "exactly one automatic sync, then the fresh mark is used");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(10, b.accepted, "attempts 3..12 are accepted");
        TEST_ASSERT_TRUE(b.mark >= 1790000000u);
    }
}

void test_cr02_one_lost_frame_keeps_the_mark(void)
{
    // a lone dead command (target busy, RF loss) is no evidence of a stale mark: no extra sync
    RmState rs;
    rmStateInit(rs, 0);
    MarkSender b("DK5EN-15", 0u);
    b.haveMark = true;
    b.mark = 0;
    bookAdd(b.book, 1000u, false, false);  // one command, never answered
    TEST_ASSERT_FALSE(rmPeerMarkStale(b.book.data(), (uint8_t)b.book.size(), 1000u + 200000u));
    markSenderSend(rs, b, 1000u + 200000u, true, false);
    TEST_ASSERT_EQUAL_UINT32(0, b.syncs);
    TEST_ASSERT_EQUAL_UINT32(1, b.accepted);
}

// ---- CR-03: the same frame on RF and on the server path produces exactly one verdict (one reply) -------------------
// rm_runtime.cpp replies once per verdict that is RM_OK / RM_SYNC / RM_CACHED. The gate (rm_rx_gate.h, RM-DUP ring)
// consumes the second copy before the queue, so rmDrain() sees one frame and rmCheck() runs once.
static uint32_t drainVerdicts(RmState &rs, uint32_t now, RmVerdict *first)
{
    char src[RM_QUEUE_SRC_LEN], text[RM_QUEUE_TEXT_LEN];
    uint32_t verdicts = 0;
    while (rmQueuePop(src, sizeof(src), text, sizeof(text)))
    {
        RmCmd c;
        TEST_ASSERT_TRUE(rmParse(text, c));
        const RmVerdict v = rmCheck(rs, c, DST, src, PW, 22, now);
        if (verdicts == 0 && first != nullptr)
            *first = v;
        if (v == RM_OK)
            rmAccept(rs, c, "ok", now);
        verdicts++;
    }
    return verdicts;
}

void test_cr03_dual_path_frame_yields_exactly_one_verdict(void)
{
    uint8_t key[32];
    rmDeriveKey(PW, key);
    char wire[96];
    TEST_ASSERT_TRUE(rmBuildCommand(DST, SRC, 7000u, "status", "", key, wire, sizeof(wire)) > 0);
    RmState rs;
    rmStateInit(rs, 0);
    rmQueueReset();

    // server copy first, the RF copy 12 s later (same source, same message id, same text): both consumed
    TEST_ASSERT_TRUE(rmRxTryQueue(SRC, wire, true, 0x2A5B6C7Du, 5000u));
    TEST_ASSERT_TRUE(rmRxTryQueue(SRC, wire, true, 0x2A5B6C7Du, 17000u));
    RmVerdict first = RM_REJ_FORMAT;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, drainVerdicts(rs, 17000u, &first), "second copy must not reach rmCheck()");
    TEST_ASSERT_EQUAL_INT(RM_OK, first);

    // the ring forgets after 60 s (RM_SEEN_MS): a copy that late is a re-send and meets the RM_CACHED limiter
    TEST_ASSERT_TRUE(rmRxTryQueue(SRC, wire, true, 0x2A5B6C7Du, 5000u + RM_SEEN_MS + 1u));
    first = RM_OK;
    TEST_ASSERT_EQUAL_UINT32(1, drainVerdicts(rs, 5000u + RM_SEEN_MS + 1u, &first));
    TEST_ASSERT_EQUAL_INT(RM_CACHED, first);
    rmQueueReset();
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_wrong_key_at_fastest_allowed_rate_never_locks_target);
    RUN_TEST(test_right_key_proven_budget_10);
    RUN_TEST(test_rekey_one_shot);
    RUN_TEST(test_forget_does_not_refill_budget);
    RUN_TEST(test_right_key_reordered_frames_never_lock);
    RUN_TEST(test_wrong_key_window_edge_30s_delay);
    RUN_TEST(test_target_rekeyed_proven_sender_never_locks);
    RUN_TEST(test_proof_streak_rule);
    RUN_TEST(test_junk_from_other_senders_never_locks_the_operator);
    RUN_TEST(test_off_sender_policy_allows_every_send);
    RUN_TEST(test_off_sender_can_push_an_on_target_into_lockout);
    RUN_TEST(test_both_off_wrong_key_stream_never_locks);
    RUN_TEST(test_cr02_before_stale_mark_never_recovers);
    RUN_TEST(test_cr02_stale_mark_recovers_without_reboot);
    RUN_TEST(test_cr02_one_lost_frame_keeps_the_mark);
    RUN_TEST(test_cr03_dual_path_frame_yields_exactly_one_verdict);
    return UNITY_END();
}
