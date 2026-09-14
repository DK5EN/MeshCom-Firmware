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

// Deterministic: always the low end of the range, so ladder timing in the
// tests is exact instead of merely bounded.
static uint32_t fakeRandomBetween(uint32_t lo, uint32_t hi) { (void)hi; return lo; }

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

// -------------------------------------------------------------- format line

static void test_format_line_content(void)
{
    msgstoreConfigure(MSGSTORE_OWN, MSGSTORE_SLOTS_DEFAULT, MSGSTORE_HOLD_DEFAULT_H);

    char buf[160];
    int  len = msgstoreFormatLine(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING(
        "MBOX mode=own used=0/50 act=0/20 stored=0 refr=0 deliv=0 ack=0 dropt=0 dropc=0 drops=0 peer=0 bp=0",
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

    RUN_TEST(test_peer_cancel_from_armed);
    RUN_TEST(test_peer_cancel_from_ladder);

    RUN_TEST(test_full_ladder_timing_and_second_cycle);

    RUN_TEST(test_gap_blocks_second_action_within_30s);
    RUN_TEST(test_twenty_per_hour_ceiling);
    RUN_TEST(test_bp_state_blocks_and_counts);
    RUN_TEST(test_util_above_max_blocks);

    RUN_TEST(test_storetime_drop_after_a_completed_attempt);
    RUN_TEST(test_storetime_drop_counts_as_cap_when_never_attempted);

    RUN_TEST(test_deliver_now_from_cooldown);
    RUN_TEST(test_deliver_false_retries_without_advancing);
    RUN_TEST(test_purge_and_purge_all);

    RUN_TEST(test_format_line_content);

    RUN_TEST(test_null_arguments_are_safe);

    return UNITY_END();
}
