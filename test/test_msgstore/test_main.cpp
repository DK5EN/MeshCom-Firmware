// Native Testsuite fuer den Store-Node-Kern (docs/dm-stage3-wave-plan-
// 20260914.md, stage 3). Fake MsgStoreEnv: settable clock, a scripted
// heard table, bp/util knobs, deterministic random_between(lo,..)=lo and
// a deliver() that records calls and can be told to fail.
//
//   pio test -e native -f test_msgstore

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <msgstore_api.h>

// ------------------------------------------------------------- fake env

static uint32_t g_now = 0;
static char     g_own[MSGSTORE_CALL_MAX] = "DK5EN";

struct HeardEntry
{
    char    call[MSGSTORE_CALL_MAX];
    int32_t age_ms;
};
static HeardEntry g_heard[8];
static int        g_heard_count = 0;

static int     g_bp   = 0;
static uint8_t g_util = 0;

static int      g_deliver_calls  = 0;
static bool     g_deliver_result = true;
static char     g_deliver_last_src[MSGSTORE_CALL_MAX];
static char     g_deliver_last_dst[MSGSTORE_CALL_MAX];
static uint16_t g_deliver_last_nnn = 0;
static char     g_deliver_last_payload[MSGSTORE_PAYLOAD_MAX + 1];

// F4 re-entrancy fixture: when set, fakeDeliver() calls msgstoreOnAck() for
// the exact entry it was just handed, from inside deliver() itself -- the
// same shape as the nRF52 LORA task purging a slot while the loop task's
// deliver() is still mid-flight (docs/review/fable-dm-stage3-verdict-
// 20260914.md, Finding 4).
static bool g_deliver_reentrant_ack = false;

// Jitter-range fixture (verdict "Tests" remarks): normally the fake returns
// lo (exact timing in every other test); set true to check the hi bound too,
// so a swapped (lo, hi) or an off-by-one in the real glueRandomBetween()
// would show up here even though it never does in the lo-only tests above.
static bool g_random_return_hi = false;

static void heardClear(void) { g_heard_count = 0; }

static void heardSet(const char *call, int32_t age_ms)
{
    for(int i = 0; i < g_heard_count; i++)
    {
        if(strcmp(g_heard[i].call, call) == 0)
        {
            g_heard[i].age_ms = age_ms;
            return;
        }
    }
    strncpy(g_heard[g_heard_count].call, call, sizeof(g_heard[0].call) - 1);
    g_heard[g_heard_count].call[sizeof(g_heard[0].call) - 1] = 0;
    g_heard[g_heard_count].age_ms = age_ms;
    g_heard_count++;
}

static uint32_t fakeNowMs(void) { return g_now; }
static const char *fakeOwnCall(void) { return g_own; }

static int32_t fakeHeardAgeMs(const char *call)
{
    if(call == NULL)
        return -1;
    for(int i = 0; i < g_heard_count; i++)
        if(strcmp(g_heard[i].call, call) == 0)
            return g_heard[i].age_ms;
    return -1;
}

static int     fakeBpState(void) { return g_bp; }
static uint8_t fakeUtilPct(void) { return g_util; }

// Deterministic: the low end of the range by default, so ladder timing in
// the tests is exact instead of merely bounded; g_random_return_hi flips it
// to the high end for the one test that pins the upper edge.
static uint32_t fakeRandomBetween(uint32_t lo, uint32_t hi) { return g_random_return_hi ? hi : lo; }

static bool fakeDeliver(const struct MsgStoreEntry *e)
{
    g_deliver_calls++;
    if(e != NULL)
    {
        strncpy(g_deliver_last_src, e->src, sizeof(g_deliver_last_src) - 1);
        g_deliver_last_src[sizeof(g_deliver_last_src) - 1] = 0;
        strncpy(g_deliver_last_dst, e->dst, sizeof(g_deliver_last_dst) - 1);
        g_deliver_last_dst[sizeof(g_deliver_last_dst) - 1] = 0;
        g_deliver_last_nnn = e->nnn;
        strncpy(g_deliver_last_payload, e->payload, sizeof(g_deliver_last_payload) - 1);
        g_deliver_last_payload[sizeof(g_deliver_last_payload) - 1] = 0;

        // F4 fixture: mutate the very slot deliver() was just handed, from
        // inside deliver() -- e.g. the ack for this DM arrives and is
        // processed by the LORA task while the loop task is still building
        // the delivery frame.
        if(g_deliver_reentrant_ack)
            msgstoreOnAck(e->dst, e->src, e->nnn);
    }
    return g_deliver_result;
}

static void fakeLog(const char *line) { (void)line; }

static const struct MsgStoreEnv g_env = {
    fakeNowMs, fakeOwnCall, fakeHeardAgeMs, fakeBpState, fakeUtilPct,
    fakeRandomBetween, fakeDeliver, fakeLog
};

void setUp(void)
{
    msgstoreReset();
    msgstoreInit(&g_env);

    g_now = 1000;
    strcpy(g_own, "DK5EN");
    heardClear();
    g_bp   = 0;
    g_util = 0;
    g_deliver_calls  = 0;
    g_deliver_result = true;
    g_deliver_last_src[0] = 0;
    g_deliver_last_dst[0] = 0;
    g_deliver_last_nnn    = 0;
    g_deliver_last_payload[0] = 0;
    g_deliver_reentrant_ack = false;
    g_random_return_hi     = false;
}

void tearDown(void) {}

// ------------------------------------------------------------ eligibility

static void test_off_mode_never_eligible(void)
{
    msgstoreConfigure(MSGSTORE_OFF, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);
    TEST_ASSERT_FALSE(msgstoreEligible("DK5EN-9"));
    TEST_ASSERT_FALSE(msgstoreEligible(NULL));
}

static void test_own_mode_matches_any_ssid_of_base_call(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);
    TEST_ASSERT_TRUE(msgstoreEligible("DK5EN"));
    TEST_ASSERT_TRUE(msgstoreEligible("DK5EN-9"));
    TEST_ASSERT_FALSE(msgstoreEligible("OE1XYZ"));
    TEST_ASSERT_FALSE(msgstoreEligible("OE1XYZ-9"));
}

static void test_list_mode_with_and_without_ssid(void)
{
    msgstoreConfigure(MSGSTORE_LIST, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);
    msgstoreSetList("OE1ABC,OE1XYZ-5");

    // Bare entry ("OE1ABC") matches any SSID of that base call.
    TEST_ASSERT_TRUE(msgstoreEligible("OE1ABC"));
    TEST_ASSERT_TRUE(msgstoreEligible("OE1ABC-9"));

    // Entry with an explicit SSID matches that SSID only.
    TEST_ASSERT_TRUE(msgstoreEligible("OE1XYZ-5"));
    TEST_ASSERT_FALSE(msgstoreEligible("OE1XYZ"));
    TEST_ASSERT_FALSE(msgstoreEligible("OE1XYZ-6"));

    TEST_ASSERT_FALSE(msgstoreEligible("OE1QQQ"));
}

static void test_heard_mode_window_edge(void)
{
    msgstoreConfigure(MSGSTORE_HEARD, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);

    heardSet("OE1ABC", 100);
    TEST_ASSERT_TRUE(msgstoreEligible("OE1ABC"));

    heardSet("OE1EDGE", MSGSTORE_HEARD_WINDOW_MS - 1);
    TEST_ASSERT_TRUE(msgstoreEligible("OE1EDGE"));

    heardSet("OE1EDGE2", MSGSTORE_HEARD_WINDOW_MS);
    TEST_ASSERT_FALSE(msgstoreEligible("OE1EDGE2"));

    // never heard
    TEST_ASSERT_FALSE(msgstoreEligible("OE1NEVER"));
}

// -------------------------------------------------- store/refresh/replace

static void test_store_new_entry(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(slot >= 0);

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("OE1ABC", e->src);
    TEST_ASSERT_EQUAL_STRING("DK5EN-9", e->dst);
    TEST_ASSERT_EQUAL_UINT16(5, e->nnn);
    TEST_ASSERT_EQUAL_STRING("hallo", e->payload);
    TEST_ASSERT_EQUAL_UINT16(5, e->plen);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT8(0, e->attempt);
    TEST_ASSERT_EQUAL_UINT8(0, e->cycles);
    TEST_ASSERT_EQUAL_UINT32(1000, e->stored_ms);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->stored);
}

static void test_store_rejects_own_call_as_src_or_dst(void)
{
    TEST_ASSERT_EQUAL(-1, msgstoreStore("DK5EN", "OE1ABC", 1, "hi", 2));
    TEST_ASSERT_EQUAL(-1, msgstoreStore("OE1ABC", "DK5EN", 1, "hi", 2));
    TEST_ASSERT_EQUAL(0, msgstoreCounters()->stored);
}

static void test_store_rejects_bad_length(void)
{
    TEST_ASSERT_EQUAL(-1, msgstoreStore("OE1ABC", "DK5EN-9", 1, "hi", 0));
    char big[MSGSTORE_PAYLOAD_MAX + 2];
    memset(big, 'x', sizeof(big));
    TEST_ASSERT_EQUAL(-1, msgstoreStore("OE1ABC", "DK5EN-9", 1, big, sizeof(big)));
}

static void test_refresh_same_payload_resets_to_held(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // HELD -> ARMED

    g_now = 2000;
    int slot2 = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_EQUAL(slot, slot2);

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT32(2000, e->stored_ms);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->refreshed);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->stored);   // unchanged by the refresh
}

static void test_replace_different_payload_resets_cycle(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_now = 3000;
    int slot2 = msgstoreStore("OE1ABC", "DK5EN-9", 5, "servus", 6);
    TEST_ASSERT_EQUAL(slot, slot2);

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_STRING("servus", e->payload);
    TEST_ASSERT_EQUAL_UINT16(6, e->plen);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT8(0, e->attempt);
    TEST_ASSERT_EQUAL_UINT32(2, msgstoreCounters()->stored);
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->refreshed);
}

static void test_drop_when_full_never_evicts(void)
{
    msgstoreConfigure(MSGSTORE_OWN, 1, MSGSTORE_HOLD_DEFAULT_H);

    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(slot >= 0);

    int slot2 = msgstoreStore("OE1DEF", "DK5EN-8", 2, "b", 1);
    TEST_ASSERT_EQUAL(-1, slot2);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->dropped_slots);

    // the original entry is untouched
    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("OE1ABC", e->src);
}

// ---------------------------------------------------------------- ack purge

static void test_ack_wrong_acker_does_not_purge(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    msgstoreOnAck("SOMEONE-ELSE", "OE1ABC", 5);

    TEST_ASSERT_NOT_NULL(msgstoreEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->purged_ack);
}

static void test_ack_correct_acker_purges(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    msgstoreOnAck("DK5EN-9", "OE1ABC", 5);

    TEST_ASSERT_NULL(msgstoreEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->purged_ack);
}

// ----------------------------------------------------------------- presence

static void test_presence_before_60s_ignored(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);   // stored_ms = 1000
    g_now = 1000 + 60000 - 1;
    msgstorePresence("DK5EN-9");

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
}

static void test_presence_after_60s_arms_with_jitter(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);   // stored_ms = 1000
    g_now = 1000 + 60000;
    msgstorePresence("DK5EN-9");

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    // fakeRandomBetween returns lo == MSGSTORE_JITTER_MIN_MS
    TEST_ASSERT_EQUAL_UINT32(g_now + MSGSTORE_JITTER_MIN_MS, e->next_ms);
}

// Verdict "Tests" remark: a fake random_between() that always returns lo
// (every test above) would not catch a swapped (lo, hi) or an off-by-one in
// the real glueRandomBetween(); pin the upper edge too.
static void test_presence_jitter_uses_hi_bound(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);   // stored_ms = 1000

    g_random_return_hi = true;
    g_now = 1000 + 60000;
    msgstorePresence("DK5EN-9");

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    TEST_ASSERT_EQUAL_UINT32(g_now + MSGSTORE_JITTER_MAX_MS, e->next_ms);
}

// Presence only ever mutates a HELD entry (msgstore.cpp: `if(state !=
// MSGSTORE_HELD) continue;`) -- these three pin that down explicitly for
// every other live state (verdict "Tests" remark: no case existed for any
// of them).
static void test_presence_while_armed_does_not_restart_jitter(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);   // stored_ms = 1000
    g_now = 1000 + 60000;
    msgstorePresence("DK5EN-9");   // HELD -> ARMED

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    uint32_t armed_next = e->next_ms;

    g_now += 1000;   // still short of the jitter
    msgstorePresence("DK5EN-9");   // a second presence trigger while ARMED

    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    TEST_ASSERT_EQUAL_UINT32(armed_next, e->next_ms);   // jitter not restarted
}

static void test_presence_while_ladder_is_ignored(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));
    msgstoreLoop();   // ARMED -> LADDER, attempt 1

    const struct MsgStoreEntry *e0 = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e0->state);
    uint32_t ladder_next    = e0->next_ms;
    uint8_t  ladder_attempt = e0->attempt;

    msgstorePresence("DK5EN-9");

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e->state);
    TEST_ASSERT_EQUAL_UINT32(ladder_next, e->next_ms);
    TEST_ASSERT_EQUAL_UINT8(ladder_attempt, e->attempt);
}

static void test_presence_during_cooldown_ignored_until_next_ms(void)
{
    static const uint32_t kGaps[8] = {40000, 40000, 100000, 40000, 40000, 100000, 40000, 40000};

    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    for(int attempt = 1; attempt <= 9; attempt++)
    {
        msgstoreLoop();
        if(attempt < 9)
            g_now += kGaps[attempt - 1];
    }

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_COOLDOWN, e->state);
    uint32_t cooldown_next = e->next_ms;

    msgstorePresence("DK5EN-9");   // ignored: state is COOLDOWN, not HELD

    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_COOLDOWN, e->state);
    TEST_ASSERT_EQUAL_UINT32(cooldown_next, e->next_ms);

    // Only elapsing to next_ms (via msgstoreLoop(), not presence) releases it.
    g_now = cooldown_next;
    msgstoreLoop();

    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
}

// -------------------------------------------------------------- peer cancel

static void test_peer_cancel_from_armed(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // -> ARMED

    msgstoreOnPeerDelivery("OE1ABC", 5);

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->cancelled_peer);
}

static void test_peer_cancel_from_ladder(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));
    msgstoreLoop();   // ARMED -> LADDER, attempt 1

    const struct MsgStoreEntry *e0 = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e0->state);

    msgstoreOnPeerDelivery("OE1ABC", 5);

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->cancelled_peer);
}

// F6 (docs/review/fable-dm-stage3-verdict-20260914.md): "a peer's hop-0
// delivery must not be stored" is a hook-*order* fix in
// src/lora_functions.cpp (compute the peer-delivery signature once, gate
// the store hook on it before falling through to msgstoreOnPeerDelivery()).
// msgstore.cpp's msgstoreStore() has no notion of rly_hop or a path comma at
// all -- that is exactly why the bug could exist despite msgstoreOnPeerDelivery()
// itself being correct (test_peer_cancel_from_armed/_ladder above). Not
// unit-testable at this level; covered by reading src/lora_functions.cpp's
// hook order (the peer-delivery test now runs before the store hook's
// condition, see the `!bMboxPeerDelivery` guard).

// ------------------------------------------------------------ deliver args

// Verdict "Tests" remark: g_deliver_last_src/dst/nnn were captured but never
// asserted -- nothing proved deliver() got the entry it was supposed to.
static void test_deliver_call_receives_entry_fields(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 42, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // HELD -> ARMED, next_ms = now

    msgstoreLoop();   // the ladder's first attempt calls deliver()

    TEST_ASSERT_EQUAL(1, g_deliver_calls);
    TEST_ASSERT_EQUAL_STRING("OE1ABC", g_deliver_last_src);
    TEST_ASSERT_EQUAL_STRING("DK5EN-9", g_deliver_last_dst);
    TEST_ASSERT_EQUAL_UINT16(42, g_deliver_last_nnn);
    TEST_ASSERT_EQUAL_STRING("hallo", g_deliver_last_payload);
}

// ---------------------------------------------------------- re-entrancy (F4)

// The nRF52 LORA task can run msgstoreOnAck()/msgstoreStore()/
// msgstoreOnPeerDelivery()/msgstorePresence() on the very slot the loop
// task's deliver() is mid-flight on. Modelled here by a deliver() that calls
// back into the core for its own entry before returning -- the slot must be
// left exactly as the hook left it, never resurrected by the post-deliver
// ladder step.
static void test_reentrant_ack_during_deliver_keeps_slot_free(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // ARMED

    g_deliver_reentrant_ack = true;
    msgstoreLoop();   // deliver() acks this exact entry from inside itself

    TEST_ASSERT_NULL(msgstoreEntry(slot));                        // stays FREE, not resurrected into COOLDOWN
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->purged_ack);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);   // the frame is still on the ring
}

// ------------------------------------------------------------- millis wrap

// F3 (docs/review/fable-dm-stage3-verdict-20260914.md): every next_ms
// comparison must read correctly across the 49.7-day uint32_t wrap. Each
// case below pins one of the raw comparisons the verdict named, constructed
// so that a plain (unsigned) `>`/`>=`/`<` -- which is what shipped in
// 59f21e5b -- gives the wrong answer while the signed-difference idiom
// gives the right one.

// The due check shared by msgstoreLoop()'s candidate pick (ARMED and LADDER
// alike -- both go through the same code path).
static void test_wrap_due_check_across_wrap(void)
{
    // Store right at the pre-wrap timestamp too -- storetime expiry
    // (stored_ms-based) is on a separate, already wrap-safe check, but a
    // stored_ms left at setUp()'s default 1000 would make the storetime
    // pass see ~49.7 days elapsed once "now" wraps around to 100 and free
    // the entry before the due check under test ever runs.
    g_now = 0xFFFFFFF0UL;
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // ARMED, next_ms = 0xFFFFFFF0 (pre-wrap)

    g_now = 100;   // wrapped: physically ~116 ms after next_ms, not before it
    msgstoreLoop();

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT8(1, e->attempt);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e->state);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);
}

// The candidate-vs-candidate compare: two due entries, one scheduled just
// before the wrap (chronologically earlier) and one just after (numerically
// smaller but chronologically later) -- the earlier one must still win.
static void test_wrap_candidate_pick_across_wrap(void)
{
    int a = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    int b = msgstoreStore("OE1DEF", "DK5EN-8", 2, "b", 1);

    g_now = 0xFFFFFFF0UL;
    TEST_ASSERT_TRUE(msgstoreDeliverNow(a));   // a.next_ms = 0xFFFFFFF0 -- chronologically earlier

    g_now = 50;   // wrapped forward 66 ms
    TEST_ASSERT_TRUE(msgstoreDeliverNow(b));   // b.next_ms = 50 -- chronologically later

    g_now = 1000;   // both due by now
    msgstoreLoop();

    TEST_ASSERT_EQUAL_STRING("DK5EN-9", g_deliver_last_dst);   // a picked, not b

    TEST_ASSERT_EQUAL_UINT8(1, msgstoreEntry(a)->attempt);
    TEST_ASSERT_EQUAL_UINT8(0, msgstoreEntry(b)->attempt);
}

// COOLDOWN release: next_ms computed shortly before the wrap, "now" read
// shortly after it -- this is the verdict's failure scenario (a), a live
// entry riding out 49 days stuck in COOLDOWN instead of releasing to HELD.
static void test_wrap_cooldown_release_across_wrap(void)
{
    static const uint32_t kGaps[8] = {40000, 40000, 100000, 40000, 40000, 100000, 40000, 40000};

    const uint32_t cooldown_next = 0xFFFFFFF0UL;
    // Back out the 8-gap ladder (440000 ms) and the cooldown addition so the
    // COOLDOWN's next_ms lands exactly on cooldown_next.
    uint32_t g_now0 = cooldown_next - MSGSTORE_COOLDOWN_MS - 440000UL;

    g_now = g_now0;
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // ARMED, next_ms = g_now0

    for(int attempt = 1; attempt <= 9; attempt++)
    {
        msgstoreLoop();
        if(attempt < 9)
            g_now += kGaps[attempt - 1];
    }

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_COOLDOWN, e->state);
    TEST_ASSERT_EQUAL_UINT32(cooldown_next, e->next_ms);

    g_now = 100;   // wrapped: physically ~116 ms past cooldown_next
    msgstoreLoop();

    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
}

// storetime expiry was already wrap-safe in 59f21e5b (plain unsigned
// subtraction is inherently wrap-correct) -- a regression test all the same,
// exercised right across the boundary.
static void test_wrap_storetime_drop_across_wrap(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, 1);   // 1 h hold

    g_now = 0xFFFFFFF0UL;
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);   // stored_ms = 0xFFFFFFF0
    TEST_ASSERT_NOT_NULL(msgstoreEntry(slot));

    g_now = (uint32_t)(0xFFFFFFF0UL + 3600000UL);   // exactly 1 h later, wrapped
    msgstoreLoop();

    TEST_ASSERT_NULL(msgstoreEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->dropped_storetime);
}

static void test_wrap_next_action_in_ms_across_wrap(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);

    g_now = 0xFFFFFFF0UL;
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));   // next_ms = 0xFFFFFFF0

    g_now = 50;   // wrapped forward 66 ms -- next_ms is now in the past
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreNextActionInMs());

    g_now = 0xFFFFFFF0UL - 1000UL;   // 1000 ms before due, no wrap yet
    TEST_ASSERT_EQUAL_UINT32(1000, msgstoreNextActionInMs());
}

// ------------------------------------------------------------- ladder timing

// Full 9-attempt ladder: 40/40/(40+60)/40/40/(40+60)/40/40, then a 1 h
// cooldown, back to HELD, and a fresh presence trigger starts a second
// cycle. Exact next_ms at every step (docs/dm-stage3-wave-plan-20260914.md,
// D5; msgstore_api.h MSGSTORE_STEP_MS/MSGSTORE_BLOCK_GAP_MS).
static void test_full_ladder_timing_and_second_cycle(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 5, "hallo", 5);   // stored_ms = 1000

    g_now = 1000 + 60000;   // 61000: presence trigger, 60 s round trip elapsed
    msgstorePresence("DK5EN-9");

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    TEST_ASSERT_EQUAL_UINT32(66000, e->next_ms);   // 61000 + jitter (lo = 5000)

    static const uint32_t kGaps[8] = {40000, 40000, 100000, 40000, 40000, 100000, 40000, 40000};

    g_now = 66000;
    for(int attempt = 1; attempt <= 9; attempt++)
    {
        msgstoreLoop();

        e = msgstoreEntry(slot);
        TEST_ASSERT_NOT_NULL(e);
        TEST_ASSERT_EQUAL_UINT8(attempt, e->attempt);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)attempt, msgstoreCounters()->delivered);

        if(attempt < 9)
        {
            TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e->state);
            uint32_t expected_next = g_now + kGaps[attempt - 1];
            TEST_ASSERT_EQUAL_UINT32(expected_next, e->next_ms);
            g_now = expected_next;
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT8(MSGSTORE_COOLDOWN, e->state);
            TEST_ASSERT_EQUAL_UINT8(1, e->cycles);
            TEST_ASSERT_EQUAL_UINT32(g_now + MSGSTORE_COOLDOWN_MS, e->next_ms);
            g_now = e->next_ms;
        }
    }

    // Cooldown elapses -> HELD, waiting for the next presence trigger.
    msgstoreLoop();
    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_HELD, e->state);
    TEST_ASSERT_EQUAL_UINT32(9, msgstoreCounters()->delivered);   // unchanged by the sweep

    // A fresh presence trigger starts a second cycle.
    msgstorePresence("DK5EN-9");
    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
}

// ----------------------------------------------------------------- node gate

static void test_gap_blocks_second_action_within_30s(void)
{
    int a = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    int b = msgstoreStore("OE1DEF", "DK5EN-8", 2, "b", 1);

    TEST_ASSERT_TRUE(msgstoreDeliverNow(a));
    msgstoreLoop();   // a delivered at g_now
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);

    TEST_ASSERT_TRUE(msgstoreDeliverNow(b));
    g_now += MSGSTORE_ACTION_GAP_MS - 1;
    msgstoreLoop();   // too soon -- blocked

    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->blocked_bp);

    g_now += 1;   // exactly the gap now
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(2, msgstoreCounters()->delivered);
}

static void test_twenty_per_hour_ceiling(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_MAX, MSGSTORE_HOLD_DEFAULT_H);

    int slots[21];
    for(int i = 0; i < 21; i++)
    {
        char src[MSGSTORE_CALL_MAX];
        char dst[MSGSTORE_CALL_MAX];
        snprintf(src, sizeof(src), "OE1A%02d", i);
        snprintf(dst, sizeof(dst), "DK5EN%d", i % 9);
        slots[i] = msgstoreStore(src, dst, (uint16_t)(100 + i), "x", 1);
        TEST_ASSERT_TRUE(slots[i] >= 0);
    }

    g_now = 1000;
    for(int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(msgstoreDeliverNow(slots[i]));
        msgstoreLoop();
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(i + 1), msgstoreCounters()->delivered);
        g_now += MSGSTORE_ACTION_GAP_MS;   // stays clear of the 30 s gap every time
    }

    // 21st action within the same trailing hour: waits on the ceiling.
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slots[20]));
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(20, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->blocked_bp);

    // an hour later it goes through
    g_now += 3600000UL;
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(21, msgstoreCounters()->delivered);
}

static void test_bp_state_blocks_and_counts(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_bp = 1;   // QRS
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->blocked_bp);

    g_bp = 0;
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);
}

static void test_util_above_max_blocks(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_util = 26;
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->blocked_bp);

    g_util = 25;   // at the ceiling, still allowed
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);
}

// F7 (docs/review/fable-dm-stage3-verdict-20260914.md): blocked_bp counts
// once per blocked *episode*, not once per refused tick -- five ticks held
// by the same gate is one episode; recovering and then blocking again is a
// second one.
static void test_blocked_episode_counted_once_across_five_ticks(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_bp = 1;   // QRT: every tick refused
    for(int i = 0; i < 5; i++)
    {
        msgstoreLoop();
        g_now += 2000;   // the firmware's 2 s tick
    }

    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->blocked_bp);   // one episode, not five

    g_bp = 0;
    msgstoreLoop();   // the gate lets it through -- episode ends
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);

    // A fresh block once the next step is due starts a second episode.
    g_now += MSGSTORE_STEP_MS;
    g_bp = 1;
    for(int i = 0; i < 3; i++)
    {
        msgstoreLoop();
        g_now += 2000;
    }
    TEST_ASSERT_EQUAL_UINT32(2, msgstoreCounters()->blocked_bp);
}

// ------------------------------------------------------------ storetime drop

static void test_storetime_drop_after_a_completed_attempt(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, 1);   // 1 h hold

    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);   // stored_ms = 1000
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));
    msgstoreLoop();   // one successful attempt -- attempt becomes 1

    g_now = 1000 + 3600000UL;
    msgstoreLoop();

    TEST_ASSERT_NULL(msgstoreEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->dropped_storetime);
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->dropped_cap);
}

static void test_storetime_drop_counts_as_cap_when_never_attempted(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, 1);   // 1 h hold

    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);   // stored_ms = 1000
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_bp = 1;   // node gate refuses every attempt from here on
    msgstoreLoop();   // candidate picked, blocked -- capblocked flag set, attempt stays 0
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->delivered);

    g_now = 1000 + 3600000UL;
    msgstoreLoop();

    TEST_ASSERT_NULL(msgstoreEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->dropped_storetime);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->dropped_cap);
}

// --------------------------------------------------------------- operator

static void test_deliver_now_from_cooldown(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    for(int i = 0; i < 9; i++)
    {
        msgstoreLoop();
        g_now += MSGSTORE_STEP_MS + MSGSTORE_BLOCK_GAP_MS;   // always clear whichever gap is next
    }

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_COOLDOWN, e->state);

    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));
    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    TEST_ASSERT_EQUAL_UINT32(g_now, e->next_ms);
}

static void test_deliver_false_retries_without_advancing(void)
{
    int slot = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    TEST_ASSERT_TRUE(msgstoreDeliverNow(slot));

    g_deliver_result = false;
    msgstoreLoop();

    const struct MsgStoreEntry *e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_ARMED, e->state);
    TEST_ASSERT_EQUAL_UINT8(0, e->attempt);
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->delivered);
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreCounters()->blocked_bp);

    g_deliver_result = true;
    msgstoreLoop();   // same g_now, retried

    e = msgstoreEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(MSGSTORE_LADDER, e->state);
    TEST_ASSERT_EQUAL_UINT8(1, e->attempt);
    TEST_ASSERT_EQUAL_UINT32(1, msgstoreCounters()->delivered);
}

static void test_purge_and_purge_all(void)
{
    int a = msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    int b = msgstoreStore("OE1DEF", "DK5EN-8", 2, "b", 1);

    TEST_ASSERT_TRUE(msgstorePurge(a));
    TEST_ASSERT_NULL(msgstoreEntry(a));
    TEST_ASSERT_NOT_NULL(msgstoreEntry(b));

    // purging an already-free slot fails
    TEST_ASSERT_FALSE(msgstorePurge(a));

    msgstorePurgeAll();
    TEST_ASSERT_NULL(msgstoreEntry(b));
    TEST_ASSERT_EQUAL(0, msgstoreUsed());
}

// F8 (docs/review/fable-dm-stage3-verdict-20260914.md): shrinking
// --storeslots below the used range must not strand the entries in the
// slots that fall out of range -- they need to free and count, not sit
// invisible until a later raise resurrects them.
static void test_shrink_slots_frees_stranded_entries(void)
{
    msgstoreConfigure(MSGSTORE_OWN, 10, MSGSTORE_HOLD_DEFAULT_H);

    int slots[10];
    for(int i = 0; i < 10; i++)
    {
        char src[MSGSTORE_CALL_MAX];
        char dst[MSGSTORE_CALL_MAX];
        snprintf(src, sizeof(src), "OE1A%02d", i);
        snprintf(dst, sizeof(dst), "DK5EN%d", i);
        slots[i] = msgstoreStore(src, dst, (uint16_t)(200 + i), "x", 1);
        TEST_ASSERT_TRUE(slots[i] >= 0);
    }
    TEST_ASSERT_EQUAL(10, msgstoreUsed());

    msgstoreConfigure(MSGSTORE_OWN, 4, MSGSTORE_HOLD_DEFAULT_H);   // shrink to 4

    TEST_ASSERT_EQUAL(4, msgstoreUsed());
    for(int i = 0; i < 4; i++)
        TEST_ASSERT_NOT_NULL(msgstoreEntry(slots[i]));
    for(int i = 4; i < 10; i++)
        TEST_ASSERT_NULL(msgstoreEntry(slots[i]));

    TEST_ASSERT_EQUAL_UINT32(6, msgstoreCounters()->dropped_slots);
}

// -------------------------------------------------------------- format line

static void test_format_line_content(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);

    char buf[160];
    int  len = msgstoreFormatLine(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING(
        "MBOX mode=own used=0/50 act=0/20 stored=0 refr=0 deliv=0 ack=0 dropt=0 dropc=0 drops=0 peer=0 blk=0",
        buf);

    // no semicolons -- printfdeb strips them in csv mode
    TEST_ASSERT_NULL(strchr(buf, ';'));

    msgstoreStore("OE1ABC", "DK5EN-9", 1, "a", 1);
    msgstoreFormatLine(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "used=1/50"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "stored=1"));
}

// ------------------------------------------------------------------ NULL-safe

static void test_null_arguments_are_safe(void)
{
    TEST_ASSERT_EQUAL(-1, msgstoreStore(NULL, "DK5EN-9", 1, "a", 1));
    TEST_ASSERT_EQUAL(-1, msgstoreStore("OE1ABC", NULL, 1, "a", 1));
    TEST_ASSERT_EQUAL(-1, msgstoreStore("OE1ABC", "DK5EN-9", 1, NULL, 1));

    msgstoreOnAck(NULL, "OE1ABC", 1);
    msgstoreOnAck("DK5EN-9", NULL, 1);
    msgstorePresence(NULL);
    msgstorePresence("");
    msgstoreOnPeerDelivery(NULL, 1);

    TEST_ASSERT_FALSE(msgstoreEligible(NULL));
    TEST_ASSERT_FALSE(msgstoreEligible(""));

    TEST_ASSERT_NULL(msgstoreEntry(-1));
    TEST_ASSERT_NULL(msgstoreEntry(MSGSTORE_SLOTS_MAX));

    TEST_ASSERT_FALSE(msgstorePurge(-1));
    TEST_ASSERT_FALSE(msgstoreDeliverNow(-1));
    TEST_ASSERT_FALSE(msgstoreDeliverNow(MSGSTORE_SLOTS_MAX));

    TEST_ASSERT_EQUAL(0, msgstoreFormatLine(NULL, 10));
    char buf[8];
    TEST_ASSERT_EQUAL(0, msgstoreFormatLine(buf, 0));

    msgstoreSetList(NULL);
    TEST_ASSERT_EQUAL_STRING("", msgstoreListCsv());

    // no crash, nothing left pending
    msgstoreLoop();
    TEST_ASSERT_EQUAL_UINT32(0, msgstoreNextActionInMs());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_off_mode_never_eligible);
    RUN_TEST(test_own_mode_matches_any_ssid_of_base_call);
    RUN_TEST(test_list_mode_with_and_without_ssid);
    RUN_TEST(test_heard_mode_window_edge);

    RUN_TEST(test_store_new_entry);
    RUN_TEST(test_store_rejects_own_call_as_src_or_dst);
    RUN_TEST(test_store_rejects_bad_length);
    RUN_TEST(test_refresh_same_payload_resets_to_held);
    RUN_TEST(test_replace_different_payload_resets_cycle);
    RUN_TEST(test_drop_when_full_never_evicts);

    RUN_TEST(test_ack_wrong_acker_does_not_purge);
    RUN_TEST(test_ack_correct_acker_purges);

    RUN_TEST(test_presence_before_60s_ignored);
    RUN_TEST(test_presence_after_60s_arms_with_jitter);
    RUN_TEST(test_presence_jitter_uses_hi_bound);
    RUN_TEST(test_presence_while_armed_does_not_restart_jitter);
    RUN_TEST(test_presence_while_ladder_is_ignored);
    RUN_TEST(test_presence_during_cooldown_ignored_until_next_ms);

    RUN_TEST(test_peer_cancel_from_armed);
    RUN_TEST(test_peer_cancel_from_ladder);

    RUN_TEST(test_deliver_call_receives_entry_fields);
    RUN_TEST(test_reentrant_ack_during_deliver_keeps_slot_free);

    RUN_TEST(test_wrap_due_check_across_wrap);
    RUN_TEST(test_wrap_candidate_pick_across_wrap);
    RUN_TEST(test_wrap_cooldown_release_across_wrap);
    RUN_TEST(test_wrap_storetime_drop_across_wrap);
    RUN_TEST(test_wrap_next_action_in_ms_across_wrap);

    RUN_TEST(test_full_ladder_timing_and_second_cycle);

    RUN_TEST(test_gap_blocks_second_action_within_30s);
    RUN_TEST(test_twenty_per_hour_ceiling);
    RUN_TEST(test_bp_state_blocks_and_counts);
    RUN_TEST(test_util_above_max_blocks);
    RUN_TEST(test_blocked_episode_counted_once_across_five_ticks);

    RUN_TEST(test_storetime_drop_after_a_completed_attempt);
    RUN_TEST(test_storetime_drop_counts_as_cap_when_never_attempted);

    RUN_TEST(test_deliver_now_from_cooldown);
    RUN_TEST(test_deliver_false_retries_without_advancing);
    RUN_TEST(test_purge_and_purge_all);
    RUN_TEST(test_shrink_slots_frees_stranded_entries);

    RUN_TEST(test_format_line_content);

    RUN_TEST(test_null_arguments_are_safe);

    return UNITY_END();
}
