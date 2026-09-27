// Native Testsuite fuer den Stage-1-Outbox/Retry-Ladder-Kern
// (docs/dm-stage1-plan-20260914.md Abschnitte 3-4, src/dm_outbox_api.h,
// docs/pn-retry-snf-port-plan.md Abschnitt 4 "E1" -- Umstellung auf das
// offizielle XOR-Format, Modus 9 entfaellt).
//
//   pio test -e native -f test_dm_outbox
//
// Fake DmOutboxEnv: eine stellbare Uhr (g_now), ein bp-Knopf (g_bp), ein
// Transmit-Log (id/nnn/dst je Aufruf) und ein Report-Log (first_id/status/held
// je Aufruf). dmOutboxReset() leert die Tabelle und Zaehler vor jedem Test;
// dmOutboxInit() (env + Slotzahl) bleibt ueber alle Tests hinweg gleich, wie
// im echten Boot-Ablauf. Es gibt kein mint_id() mehr -- jeder Versuch 2..4
// ist die offizielle XOR-Variante von first_id (src/pn_retry.h), nie eine
// frisch erfundene id.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

#include <dm_outbox_api.h>
#include <dm_settings.h>
#include <pn_retry.h>

// --------------------------------------------------------------- fake env

static uint32_t g_now = 1000;
static int      g_bp  = 0;

static bool g_transmit_result   = true;
static bool g_transmit_ack_reentrant = false;   // one-shot: transmit() calls dmOutboxOnAck() on itself

struct TxLogEntry
{
    uint32_t    id;
    uint16_t    nnn;
    std::string dst;
};
static std::vector<TxLogEntry> g_tx_log;

struct ReportLogEntry
{
    uint32_t first_id;
    uint8_t  status;
    bool     held;   // F4: the glue's own suppress-on-wire decision key
};
static std::vector<ReportLogEntry> g_report_log;

static uint32_t fakeNow(void) { return g_now; }
static int      fakeBp(void)  { return g_bp; }

static bool fakeTransmit(const struct DmOutboxEntry *e, uint32_t msg_id)
{
    TxLogEntry t;
    t.id  = msg_id;
    t.nnn = e->nnn;
    t.dst = std::string(e->dst);
    g_tx_log.push_back(t);

    if(g_transmit_ack_reentrant)
    {
        g_transmit_ack_reentrant = false;   // one-shot
        dmOutboxOnAck(e->dst, e->nnn);
    }

    return g_transmit_result;
}

static void fakeReport(const struct DmOutboxEntry *e, uint8_t status)
{
    ReportLogEntry r;
    r.first_id = e->first_id;
    r.status   = status;
    r.held     = e->held;
    g_report_log.push_back(r);
}

static void fakeLog(const char *line) { (void)line; }

static const struct DmOutboxEnv kFakeEnv = {
    fakeNow, fakeBp, fakeTransmit, fakeReport, fakeLog
};

void setUp(void)
{
    g_now = 1000;
    g_bp  = 0;
    g_transmit_result = true;
    g_transmit_ack_reentrant = false;
    g_tx_log.clear();
    g_report_log.clear();

    dmOutboxInit(&kFakeEnv, 5);
    dmOutboxReset();
}

void tearDown(void) {}

// Advances the clock to the candidate's own next_ms and runs one loop tick.
static void advanceToDueAndTick(int slot)
{
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    g_now = e->next_ms;
    dmOutboxLoop();
}

// ----------------------------------------------------------- add / full

static void test_add_room_and_full(void)
{
    TEST_ASSERT_TRUE(dmOutboxHasRoom());

    int slots[5];
    for(int i = 0; i < 5; i++)
    {
        char dst[8];
        snprintf(dst, sizeof(dst), "OE%dAAA", i);
        slots[i] = dmOutboxAdd((uint16_t)(10 + i), dst, "hello", 5, 3,
                                (uint32_t)(1000 + i), DM_RETRY_3);
        TEST_ASSERT_TRUE(slots[i] >= 0);
    }

    TEST_ASSERT_EQUAL_INT(5, dmOutboxUsed());
    TEST_ASSERT_FALSE(dmOutboxHasRoom());

    int overflow = dmOutboxAdd(99, "OE9ZZZ", "x", 1, 3, 9999, DM_RETRY_3);
    TEST_ASSERT_EQUAL_INT(-1, overflow);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->refused_full);
    TEST_ASSERT_EQUAL_UINT32(5, dmOutboxCounters()->added);
}

static void test_add_off_mode_is_rejected_without_touching_counters(void)
{
    int slot = dmOutboxAdd(1, "OE1AAA", "hi", 2, 3, 100, DM_RETRY_OFF);
    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->added);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->refused_full);
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
}

static void test_add_populates_entry_fields(void)
{
    g_now = 5000;
    int slot = dmOutboxAdd(42, "OE1KBC-5", "Hallo Welt", 10, 3, 555000, DM_RETRY_3);
    TEST_ASSERT_TRUE(slot >= 0);

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(42, e->nnn);
    TEST_ASSERT_EQUAL_STRING("OE1KBC-5", e->dst);
    TEST_ASSERT_EQUAL_STRING("Hallo Welt", e->payload);
    TEST_ASSERT_EQUAL_UINT16(10, e->plen);
    TEST_ASSERT_EQUAL_UINT8(3, e->max_hop);
    TEST_ASSERT_EQUAL_UINT32(555000, e->first_id);
    TEST_ASSERT_EQUAL_UINT32(555000, e->last_id);
    TEST_ASSERT_EQUAL_UINT32(5000, e->first_ms);
    TEST_ASSERT_EQUAL_UINT32(5000 + 40000UL, e->next_ms);
    TEST_ASSERT_EQUAL_UINT8(1, e->attempt);
    // Mode 3 now means "at most 4 transmissions" (attempt 1 + 3 XOR
    // retries), since mode 9 (and its own max_attempts value) is retired
    // (docs/pn-retry-snf-port-plan.md section 4 "E1").
    TEST_ASSERT_EQUAL_UINT8(4, e->max_attempts);
    TEST_ASSERT_EQUAL_INT(DMOB_LADDER, e->state);
    TEST_ASSERT_FALSE(e->echo_seen);
    TEST_ASSERT_FALSE(e->held);
}

// --------------------------------------------------------- schedule / ids

// Regression (fails on the old mode-9 code, which minted a fresh millis()-ish
// id from attempt 3 on and kept first_id for attempt 2 when no echo had been
// seen): every retry attempt 2..4 must be EXACTLY the official XOR variant
// of the ORIGINAL id (first_id ^ (k << 10), k = 1..3 -- src/pn_retry.h), and
// must never equal first_id again. Also pins the schedule (40/80/120 s) and
// that exactly 4 transmissions happen before give-up.
static void test_retry_ids_are_xor_variants_and_schedule_is_40_80_120(void)
{
    g_now = 0;
    const uint32_t first_id = 100;
    int slot = dmOutboxAdd(1, "OE1AAA", "p", 1, 3, first_id, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);

    static const uint32_t kExpectedMs[3] = {40000, 80000, 120000};

    for(int k = 0; k < 3; k++)
    {
        TEST_ASSERT_EQUAL_UINT32(kExpectedMs[k], e->next_ms);

        // Not yet due one ms earlier: no transmission.
        g_now = e->next_ms - 1;
        dmOutboxLoop();
        TEST_ASSERT_EQUAL_INT(k, (int)g_tx_log.size());

        advanceToDueAndTick(slot);
        TEST_ASSERT_EQUAL_INT(k + 1, (int)g_tx_log.size());

        // Exactly the XOR variant for this attempt (k+1 in pn_retry.h's
        // convention: attempt 2 -> k=1, attempt 3 -> k=2, attempt 4 -> k=3),
        // computed from the ORIGINAL id, and never first_id itself.
        uint32_t expected_id = first_id ^ ((uint32_t)(k + 1) << 10);
        TEST_ASSERT_EQUAL_UINT32(expected_id, g_tx_log.back().id);
        TEST_ASSERT_TRUE(g_tx_log.back().id != first_id);
        TEST_ASSERT_EQUAL_UINT32(pnRetryCore(first_id), pnRetryCore(g_tx_log.back().id));

        if(k < 2)
        {
            e = dmOutboxEntry(slot);
            TEST_ASSERT_NOT_NULL(e);
            TEST_ASSERT_EQUAL_INT(DMOB_LADDER, e->state);
        }
    }

    // Attempt 4 (the last) just fired -- give-up, reported, not yet freed.
    e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(DMOB_DONE_GIVEUP, e->state);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->gaveup);
    TEST_ASSERT_EQUAL_INT(1, (int)g_report_log.size());
    TEST_ASSERT_EQUAL_UINT32(first_id, g_report_log[0].first_id);
    TEST_ASSERT_EQUAL_UINT8(0x03, g_report_log[0].status);

    // Exactly 4 transmissions total (attempt 1 happened outside the outbox,
    // so only the 3 XOR retries show up in g_tx_log/fresh_attempts here).
    TEST_ASSERT_EQUAL_INT(3, (int)g_tx_log.size());
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(3, dmOutboxCounters()->fresh_attempts);

    // Freed only on the NEXT tick.
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
    TEST_ASSERT_NULL(dmOutboxEntry(slot));

    // No 5th attempt ever happens, no matter how long the clock runs.
    g_now += 10000000UL;
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(3, (int)g_tx_log.size());
}

// -------------------------------------------------------------- echo gate

// The echo gate is retired: echo_seen is informational only and never
// changes which id an attempt uses (dm_outbox.cpp always computes
// first_id ^ (k << 10)). This is exercised implicitly by the schedule test
// above never seeing a same-id attempt; here we pin the informational side:
// OnEcho still matches and flags an entry, whether it is handed the
// original id, an XOR variant already sent, or a variant NEVER sent (the
// receive-path fold-back in lora_functions.cpp can hand back any of the
// three).
static void test_echo_matches_any_xor_variant_via_core(void)
{
    g_now = 0;
    const uint32_t first_id = 400;
    int slot = dmOutboxAdd(4, "OE1DDD", "p", 1, 3, first_id, DM_RETRY_3);

    // A variant that was never transmitted (k=3, attempt 4's id) still
    // matches by pnRetryCore -- this is the regression the old
    // first_id/last_id equality check could not have supported without
    // the core mask.
    uint32_t never_sent_variant = first_id ^ (3u << 10);
    dmOutboxOnEcho(never_sent_variant);

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_TRUE(e->echo_seen);

    // ...and it did NOT change the next attempt's id: still the k=1 variant.
    advanceToDueAndTick(slot);
    TEST_ASSERT_EQUAL_UINT32(first_id ^ (1u << 10), g_tx_log.back().id);
}

static void test_echo_unknown_id_is_ignored(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(6, "OE1FFF", "p", 1, 3, 600, DM_RETRY_3);
    dmOutboxOnEcho(123456789UL);   // matches nothing (different core)
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_FALSE(e->echo_seen);
    (void)slot;
}

static void test_echo_zero_id_is_ignored(void)
{
    int slot = dmOutboxAdd(61, "OE1FGH", "p", 1, 3, 700000, DM_RETRY_3);
    dmOutboxOnEcho(0);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_FALSE(e->echo_seen);
}

// ------------------------------------------------------------------- ack

static void test_ack_right_dst_stops_and_frees_without_report(void)
{
    int slot = dmOutboxAdd(7, "OE1GGG", "p", 1, 3, 700, DM_RETRY_3);
    (void)slot;

    bool stopped = dmOutboxOnAck("OE1GGG", 7);
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->acked);
    TEST_ASSERT_TRUE(g_report_log.empty());   // documented choice: OnAck never reports
}

static void test_ack_wrong_dst_is_ignored(void)
{
    int slot = dmOutboxAdd(8, "OE1HHH", "p", 1, 3, 800, DM_RETRY_3);
    bool stopped = dmOutboxOnAck("OE1ZZZ", 8);
    TEST_ASSERT_FALSE(stopped);
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
    TEST_ASSERT_NOT_NULL(dmOutboxEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->acked);
}

static void test_ack_unknown_nnn_is_ignored(void)
{
    dmOutboxAdd(9, "OE1III", "p", 1, 3, 900, DM_RETRY_3);
    bool stopped = dmOutboxOnAck("OE1III", 999);
    TEST_ASSERT_FALSE(stopped);
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
}

static void test_ack_after_attempt_1_2_and_3(void)
{
    // After attempt 1 (right after Add, no loop tick yet).
    {
        int slot = dmOutboxAdd(10, "OE1AA1", "p", 1, 3, 1100, DM_RETRY_3);
        (void)slot;
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA1", 10));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
    }

    // After attempt 2.
    {
        g_now = 0;
        int slot = dmOutboxAdd(11, "OE1AA2", "p", 1, 3, 1200, DM_RETRY_3);
        advanceToDueAndTick(slot);
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA2", 11));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
    }

    // After attempt 3 (one short of give-up on the 4-attempt ladder).
    {
        g_now = 0;
        int slot = dmOutboxAdd(12, "OE1AA3", "p", 1, 3, 1300, DM_RETRY_3);
        for(int i = 0; i < 2; i++)
            advanceToDueAndTick(slot);   // attempts 2..3
        const struct DmOutboxEntry *e = dmOutboxEntry(slot);
        TEST_ASSERT_EQUAL_UINT8(3, e->attempt);
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA3", 12));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
        // The give-up on attempt 4 never happens now.
        g_now = e->next_ms;
        dmOutboxLoop();
        TEST_ASSERT_TRUE(g_report_log.empty());
    }
}

// F1 (fable-dm-stage1-verdict-20260914.md, T2): the fix moved
// dmOutboxOnAck() out from behind the CALL SITE's checkOwnTx() gate
// (lora_functions.cpp/udp_functions.cpp/nrf_eth.cpp) -- the core itself
// (dm_outbox.cpp) never knew about own_msg_id[] and was already keyed on
// (dst, nnn) alone (Confirmation 2 in the verdict: "Core -- CONFIRMED").
// This test pins that core-side guarantee natively: run a ladder to its
// last attempt (last_id an XOR variant, never first_id), then ack on
// (dst, nnn) alone -- the outbox must still stop, and dmOutboxLoop() must
// never call report() (0x03 give-up) for this entry afterwards, no matter
// how much later it is asked to run again.
static void test_f1_ack_on_nnn_dst_stops_ladder_after_xor_variant(void)
{
    g_now = 0;
    const uint32_t first_id = 6000;
    int slot = dmOutboxAdd(60, "OE1XXX", "p", 1, 3, first_id, DM_RETRY_3);
    for(int i = 0; i < 2; i++)
        advanceToDueAndTick(slot);   // attempts 2..3: two XOR variants sent

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_TRUE(e->last_id != e->first_id);
    TEST_ASSERT_EQUAL_UINT32(first_id ^ (2u << 10), e->last_id);   // attempt 3's variant

    bool stopped = dmOutboxOnAck("OE1XXX", 60);
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_NULL(dmOutboxEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->acked);

    // Give-up must never fire for this entry now: it is FREE, dmOutboxLoop()
    // finds nothing due for it even well past every remaining attempt's
    // schedule.
    g_now += 500000;
    dmOutboxLoop();
    TEST_ASSERT_TRUE(g_report_log.empty());
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->gaveup);
}

// ----------------------------------------------------------------- held

static void test_held_suppresses_giveup_report_but_still_counts(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(13, "OE1JJJ", "p", 1, 3, 1400, DM_RETRY_3);
    dmOutboxOnHeld(13);

    advanceToDueAndTick(slot);   // attempt 2
    advanceToDueAndTick(slot);   // attempt 3
    advanceToDueAndTick(slot);   // attempt 4 -> give-up

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_INT(DMOB_DONE_GIVEUP, e->state);
    TEST_ASSERT_TRUE(e->held);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->gaveup);

    // F4 (fable-dm-stage1-verdict-20260914.md): report() now runs for EVERY
    // give-up, held or not -- dm_outbox_api.h's report() contract says
    // "caller decides the held case"; the core no longer decides it by
    // skipping the call. The glue (dm_outbox_glue.cpp) is the one that must
    // not put a 0x03 on the wire for a held message and the one that counts
    // dmstat_giveup_held -- this fake env does neither, it only logs every
    // call, so what this test pins is that report() ran WITH e->held ==
    // true, which is exactly what the glue's suppress decision is keyed on.
    TEST_ASSERT_EQUAL_INT(1, (int)g_report_log.size());
    TEST_ASSERT_EQUAL_UINT32(1400, g_report_log[0].first_id);
    TEST_ASSERT_EQUAL_UINT8(0x03, g_report_log[0].status);
    TEST_ASSERT_TRUE(g_report_log[0].held);
}

static void test_held_on_unknown_nnn_is_a_no_op(void)
{
    // F7 (fable-dm-stage1-verdict-20260914.md): the original version never
    // asserted the add succeeded -- a broken dmOutboxAdd() would have made
    // the loop below vacuous (no entry with nnn 14 to find) and the test
    // would still pass.
    int slot = dmOutboxAdd(14, "OE1KKK", "p", 1, 3, 1500, DM_RETRY_3);
    TEST_ASSERT_TRUE(slot >= 0);

    dmOutboxOnHeld(999);   // no matching entry
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(14, e->nnn);
    TEST_ASSERT_FALSE(e->held);
}

// F7 (fable-dm-stage1-verdict-20260914.md): not covered before -- "one
// enqueue per loop call" with two due entries, and the earliest-next_ms
// selection (decision 4 / dm_outbox.cpp's candidate loop).
static void test_two_due_entries_one_enqueue_per_tick_earliest_first(void)
{
    g_now = 0;
    int slotA = dmOutboxAdd(50, "OE1AAA", "p", 1, 3, 5000, DM_RETRY_3);   // due at 40000
    g_now = 100;
    int slotB = dmOutboxAdd(51, "OE1BBB", "p", 1, 3, 5100, DM_RETRY_3);   // due at 40100

    TEST_ASSERT_EQUAL_UINT32(40000, dmOutboxEntry(slotA)->next_ms);
    TEST_ASSERT_EQUAL_UINT32(40100, dmOutboxEntry(slotB)->next_ms);

    // Both due now -- exactly one attempt enqueued per dmOutboxLoop() call,
    // and it is A's (the earlier next_ms), not B's.
    g_now = 40200;
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(1, (int)g_tx_log.size());
    TEST_ASSERT_EQUAL_UINT16(50, g_tx_log.back().nnn);

    // B's turn on the next tick, still without touching A a second time.
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(2, (int)g_tx_log.size());
    TEST_ASSERT_EQUAL_UINT16(51, g_tx_log.back().nnn);

    // A third tick: nothing else due, no further enqueue.
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(2, (int)g_tx_log.size());
}

// -------------------------------------------------------------------- bp

static void test_bp_blocks_without_advancing_next_ms(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(15, "OE1LLL", "p", 1, 3, 1600, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    uint32_t due = e->next_ms;

    g_bp = 2;   // QRT
    g_now = due;
    dmOutboxLoop();
    TEST_ASSERT_TRUE(g_tx_log.empty());
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->blocked_bp);
    TEST_ASSERT_EQUAL_UINT32(due, dmOutboxEntry(slot)->next_ms);   // unchanged

    // Still blocked a tick later: same episode, no second count.
    g_now = due + 2000;
    dmOutboxLoop();
    TEST_ASSERT_TRUE(g_tx_log.empty());
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->blocked_bp);

    // Clears: the same due candidate is retried and now goes out.
    g_bp = 0;
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(1, (int)g_tx_log.size());
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->blocked_bp);
}

// ------------------------------------------------------------- transmit()==false

static void test_transmit_false_retries_next_tick(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(16, "OE1MMM", "p", 1, 3, 1700, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    uint32_t due = e->next_ms;

    g_transmit_result = false;
    g_now = due;
    dmOutboxLoop();

    TEST_ASSERT_EQUAL_INT(1, (int)g_tx_log.size());   // transmit() was called...
    e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(1, e->attempt);            // ...but nothing advanced
    TEST_ASSERT_EQUAL_UINT32(due, e->next_ms);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->fresh_attempts);

    g_transmit_result = true;
    dmOutboxLoop();   // still due (next_ms unchanged) -- succeeds this time
    TEST_ASSERT_EQUAL_INT(2, (int)g_tx_log.size());
    e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(2, e->attempt);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->fresh_attempts);
    // Both calls (the refused one and the accepted one) must have carried
    // the identical XOR-variant id -- transmit()==false must not have
    // caused a different id to be tried on retry.
    TEST_ASSERT_EQUAL_UINT32(g_tx_log[0].id, g_tx_log[1].id);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)1700 ^ (1u << 10), g_tx_log[1].id);
}

// --------------------------------------------------------------- wrap

static void test_wrap_across_uint32_max(void)
{
    g_now = 0xFFFFFFF0UL;   // 16 ms before the millis() wrap
    int slot = dmOutboxAdd(17, "OE1NNN", "p", 1, 3, 1800, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);

    uint32_t due = (uint32_t)(0xFFFFFFF0UL + 40000UL);   // wraps past 0xFFFFFFFF
    TEST_ASSERT_EQUAL_UINT32(due, e->next_ms);

    // One ms before due (wrap-safe: still "not due").
    g_now = (uint32_t)(due - 1);
    dmOutboxLoop();
    TEST_ASSERT_TRUE(g_tx_log.empty());

    // Exactly due, on the far side of the wrap.
    g_now = due;
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(1, (int)g_tx_log.size());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)1800 ^ (1u << 10), g_tx_log.back().id);
}

// -------------------------------------------------------- FirstIdForNnn

static void test_first_id_for_nnn(void)
{
    dmOutboxAdd(20, "OE1OOO", "p", 1, 3, 2000, DM_RETRY_3);
    TEST_ASSERT_EQUAL_UINT32(2000, dmOutboxFirstIdForNnn(20));
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxFirstIdForNnn(21));

    TEST_ASSERT_TRUE(dmOutboxOnAck("OE1OOO", 20));
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxFirstIdForNnn(20));   // freed -- no longer live
}

// -------------------------------------------------------------- FormatLine

static void test_format_line(void)
{
    int s1 = dmOutboxAdd(30, "OE1PPP", "p", 1, 3, 3000, DM_RETRY_3);
    dmOutboxAdd(31, "OE1QQQ", "p", 1, 3, 3100, DM_RETRY_3);

    dmOutboxAdd(32, "OE1RRR", "p", 1, 3, 3200, DM_RETRY_3);
    dmOutboxAdd(33, "OE1SSS", "p", 1, 3, 3300, DM_RETRY_3);
    dmOutboxAdd(34, "OE1TTT", "p", 1, 3, 3400, DM_RETRY_3);
    // table full now (5 slots) -- one more is refused.
    TEST_ASSERT_EQUAL_INT(-1, dmOutboxAdd(35, "OE1UUU", "p", 1, 3, 3500, DM_RETRY_3));

    TEST_ASSERT_TRUE(dmOutboxOnAck("OE1PPP", 30));   // acked=1, used drops to 4

    char buf[160];
    int len = dmOutboxFormatLine(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING(
        "OUTBOX used=4/5 add=5 full=1 same=0 fresh=0 ack=1 give=0 bp=0",
        buf);

    (void)s1;
}

static void test_format_line_clamped_buffer(void)
{
    char guard = 0x7F;
    TEST_ASSERT_EQUAL_INT(0, dmOutboxFormatLine(&guard, 0));
    TEST_ASSERT_EQUAL_INT(0x7F, (int)guard);
    TEST_ASSERT_EQUAL_INT(0, dmOutboxFormatLine(NULL, 100));
}

// ------------------------------------------------------- gen / reentrancy

static void test_gen_reentrancy_ack_from_transmit_no_resurrection(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(40, "OE1VVV", "p", 1, 3, 4000, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    uint32_t due = e->next_ms;

    // transmit() for attempt 2 will itself deliver the ack for (dst, nnn)
    // synchronously, before returning true -- as if the destination's
    // ack somehow raced the ladder's own bookkeeping.
    g_transmit_ack_reentrant = true;
    g_now = due;
    dmOutboxLoop();

    TEST_ASSERT_EQUAL_INT(1, (int)g_tx_log.size());          // the attempt was sent
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->acked);  // ack was counted
    TEST_ASSERT_NULL(dmOutboxEntry(slot));                   // freed by OnAck already

    // dmOutboxLoop()'s own post-transmit() bookkeeping must NOT have run:
    // no fresh-attempt count, no give-up, no second free-later state.
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->fresh_attempts);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->gaveup);
    TEST_ASSERT_TRUE(g_report_log.empty());

    // A further tick must not crash or resurrect anything.
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
}

// ------------------------------------------------------ dm_settings: mode 9

// docs/pn-retry-snf-port-plan.md section 4 "E1": mode 9 is retired -- the
// text form "9" must be rejected by the parser (unlike "off"/"3"). Fails
// before this wave's dm_settings.cpp change (old code accepted "9").
static void test_dmretry_mode_9_text_is_rejected(void)
{
    enum DmRetryMode mode;
    TEST_ASSERT_FALSE(dmRetryModeParse("9", &mode));
    TEST_ASSERT_TRUE(dmRetryModeParse("off", &mode));
    TEST_ASSERT_EQUAL_INT(DM_RETRY_OFF, mode);
    TEST_ASSERT_TRUE(dmRetryModeParse("3", &mode));
    TEST_ASSERT_EQUAL_INT(DM_RETRY_3, mode);
}

// dmRetryModeName() must never hand back "9" for any value the enum can
// still hold (DM_RETRY_OFF, DM_RETRY_3) -- a defensive pin now that the
// enumerator itself is gone, not a full migration test: the raw-9->3 NVS/
// LittleFS migration lives in dm_settings.cpp's ESP32/nRF52 branches, both
// `#ifndef NATIVE_BUILD` (dmSettingsLoad()/Save() are no-ops under
// NATIVE_BUILD, see dm_settings.cpp), so it is not reachable from this
// native suite -- see the wave report.
static void test_dmretry_mode_name_never_9(void)
{
    TEST_ASSERT_EQUAL_STRING("off", dmRetryModeName(DM_RETRY_OFF));
    TEST_ASSERT_EQUAL_STRING("3", dmRetryModeName(DM_RETRY_3));
}

// ------------------------------------------------------------------ main

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_add_room_and_full);
    RUN_TEST(test_add_off_mode_is_rejected_without_touching_counters);
    RUN_TEST(test_add_populates_entry_fields);

    RUN_TEST(test_retry_ids_are_xor_variants_and_schedule_is_40_80_120);

    RUN_TEST(test_echo_matches_any_xor_variant_via_core);
    RUN_TEST(test_echo_unknown_id_is_ignored);
    RUN_TEST(test_echo_zero_id_is_ignored);

    RUN_TEST(test_ack_right_dst_stops_and_frees_without_report);
    RUN_TEST(test_ack_wrong_dst_is_ignored);
    RUN_TEST(test_ack_unknown_nnn_is_ignored);
    RUN_TEST(test_ack_after_attempt_1_2_and_3);
    RUN_TEST(test_f1_ack_on_nnn_dst_stops_ladder_after_xor_variant);

    RUN_TEST(test_held_suppresses_giveup_report_but_still_counts);
    RUN_TEST(test_held_on_unknown_nnn_is_a_no_op);

    RUN_TEST(test_two_due_entries_one_enqueue_per_tick_earliest_first);

    RUN_TEST(test_bp_blocks_without_advancing_next_ms);
    RUN_TEST(test_transmit_false_retries_next_tick);
    RUN_TEST(test_wrap_across_uint32_max);

    RUN_TEST(test_first_id_for_nnn);

    RUN_TEST(test_format_line);
    RUN_TEST(test_format_line_clamped_buffer);

    RUN_TEST(test_gen_reentrancy_ack_from_transmit_no_resurrection);

    RUN_TEST(test_dmretry_mode_9_text_is_rejected);
    RUN_TEST(test_dmretry_mode_name_never_9);

    return UNITY_END();
}
