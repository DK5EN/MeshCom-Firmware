// Native Testsuite fuer den Stage-1-Outbox/Retry-Ladder-Kern
// (docs/dm-stage1-plan-20260914.md Abschnitte 3-4, src/dm_outbox_api.h).
//
//   pio test -e native -f test_dm_outbox
//
// Fake DmOutboxEnv: eine stellbare Uhr (g_now), ein bp-Knopf (g_bp), ein
// skriptbares mint_id() (fortlaufend ab einer hohen Basis, damit es nie mit
// einer Test-first_id kollidiert), ein Transmit-Log (id/nnn/dst je Aufruf)
// und ein Report-Log (first_id/status/held je Aufruf). dmOutboxReset() leert die
// Tabelle und Zaehler vor jedem Test; dmOutboxInit() (env + Slotzahl) bleibt
// ueber alle Tests hinweg gleich, wie im echten Boot-Ablauf.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

#include <dm_outbox_api.h>
#include <dm_settings.h>

// --------------------------------------------------------------- fake env

static uint32_t g_now = 1000;
static int      g_bp  = 0;

static uint32_t g_mint_next = 900000UL;   // never collides with a test's own first_id range

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
static uint32_t fakeMint(void){ return g_mint_next++; }

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
    fakeNow, fakeBp, fakeMint, fakeTransmit, fakeReport, fakeLog
};

void setUp(void)
{
    g_now = 1000;
    g_bp  = 0;
    g_mint_next = 900000UL;
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
                                (uint32_t)(1000 + i), DM_RETRY_9);
        TEST_ASSERT_TRUE(slots[i] >= 0);
    }

    TEST_ASSERT_EQUAL_INT(5, dmOutboxUsed());
    TEST_ASSERT_FALSE(dmOutboxHasRoom());

    int overflow = dmOutboxAdd(99, "OE9ZZZ", "x", 1, 3, 9999, DM_RETRY_9);
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
    TEST_ASSERT_EQUAL_UINT8(3, e->max_attempts);
    TEST_ASSERT_EQUAL_INT(DMOB_LADDER, e->state);
    TEST_ASSERT_FALSE(e->echo_seen);
    TEST_ASSERT_FALSE(e->held);
}

// --------------------------------------------------------- schedule offsets

// Runs a mode-9 entry to completion without ever registering an echo, so
// attempt 2 is the one same-id retry and every attempt from 3 on is fresh
// -- exercises the exact offset table in one pass.
static void test_schedule_offsets_9(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(1, "OE1AAA", "p", 1, 3, 100, DM_RETRY_9);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);

    static const uint32_t kExpectedMs[8] = {40000, 80000, 180000, 220000, 260000, 360000, 400000, 440000};

    for(int k = 0; k < 8; k++)
    {
        TEST_ASSERT_EQUAL_UINT32(kExpectedMs[k], e->next_ms);

        // Not yet due one ms earlier: no transmission.
        g_now = e->next_ms - 1;
        dmOutboxLoop();
        TEST_ASSERT_EQUAL_INT((int)(k), (int)g_tx_log.size());

        advanceToDueAndTick(slot);
        TEST_ASSERT_EQUAL_INT((int)(k + 1), (int)g_tx_log.size());

        if(k == 0)
            TEST_ASSERT_EQUAL_UINT32(100, g_tx_log.back().id);   // attempt 2: same id, no echo seen
        else
            TEST_ASSERT_TRUE(g_tx_log.back().id >= 900000UL);    // attempt 3..9: fresh

        if(k < 7)
        {
            e = dmOutboxEntry(slot);
            TEST_ASSERT_NOT_NULL(e);
            TEST_ASSERT_EQUAL_INT(DMOB_LADDER, e->state);
        }
    }

    // Attempt 9 (the last) just fired -- give-up, reported, not yet freed.
    e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT(DMOB_DONE_GIVEUP, e->state);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->gaveup);
    TEST_ASSERT_EQUAL_INT(1, (int)g_report_log.size());
    TEST_ASSERT_EQUAL_UINT32(100, g_report_log[0].first_id);
    TEST_ASSERT_EQUAL_UINT8(0x03, g_report_log[0].status);

    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(7, dmOutboxCounters()->fresh_attempts);

    // Freed only on the NEXT tick.
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
    TEST_ASSERT_NULL(dmOutboxEntry(slot));
}

static void test_schedule_offsets_3(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(2, "OE1BBB", "p", 1, 3, 200, DM_RETRY_3);
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);

    TEST_ASSERT_EQUAL_UINT32(40000, e->next_ms);
    advanceToDueAndTick(slot);   // attempt 2, same id (no echo)
    TEST_ASSERT_EQUAL_UINT32(200, g_tx_log.back().id);

    e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_INT(DMOB_LADDER, e->state);
    TEST_ASSERT_EQUAL_UINT32(80000, e->next_ms);

    advanceToDueAndTick(slot);   // attempt 3, fresh, last of mode 3 -> give-up
    TEST_ASSERT_TRUE(g_tx_log.back().id >= 900000UL);

    e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_INT(DMOB_DONE_GIVEUP, e->state);
    TEST_ASSERT_EQUAL_INT(1, (int)g_report_log.size());
    TEST_ASSERT_EQUAL_UINT8(0x03, g_report_log[0].status);
}

// -------------------------------------------------------------- echo gate

static void test_echo_gate_no_relay_keeps_same_id(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(3, "OE1CCC", "p", 1, 3, 300, DM_RETRY_3);
    // No dmOutboxOnEcho() call: attempt 1 was never heard relayed.
    advanceToDueAndTick(slot);

    TEST_ASSERT_EQUAL_UINT32(300, g_tx_log.back().id);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->fresh_attempts);
}

static void test_echo_gate_relay_heard_goes_fresh(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(4, "OE1DDD", "p", 1, 3, 400, DM_RETRY_3);
    dmOutboxOnEcho(400);   // attempt 1 heard relayed before attempt 2 is due
    advanceToDueAndTick(slot);

    TEST_ASSERT_TRUE(g_tx_log.back().id >= 900000UL);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->fresh_attempts);
}

static void test_echo_on_last_id_also_gates(void)
{
    // F7 (fable-dm-stage1-verdict-20260914.md): the original version of
    // this test echoed id 500, which is ALSO first_id (no attempt had gone
    // fresh yet), so it passed even with the `last_id` disjunct deleted
    // from dmOutboxOnEcho() -- it never actually exercised that branch.
    // Attempt 3 always mints fresh (only attempt 2's id choice depends on
    // echo_seen -- dm_outbox.cpp), so two ticks with no echo in between
    // leave last_id diverged from first_id while echo_seen is still false;
    // an echo of THAT fresh id must still set echo_seen. fails-before: with
    // `&& last_id != msg_id` removed from the match guard, this echo call
    // would not match (first_id != msg_id here) and the assertion below
    // would fail.
    g_now = 0;
    int slot = dmOutboxAdd(5, "OE1EEE", "p", 1, 3, 500, DM_RETRY_9);
    advanceToDueAndTick(slot);   // attempt 2, same id (no echo) -> last_id == 500
    advanceToDueAndTick(slot);   // attempt 3, always fresh -> last_id != first_id

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_TRUE(e->last_id != 500);   // fresh id, diverged from first_id
    TEST_ASSERT_FALSE(e->echo_seen);

    dmOutboxOnEcho(e->last_id);
    e = dmOutboxEntry(slot);
    TEST_ASSERT_TRUE(e->echo_seen);
}

static void test_echo_unknown_id_is_ignored(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(6, "OE1FFF", "p", 1, 3, 600, DM_RETRY_3);
    dmOutboxOnEcho(123456789UL);   // matches nothing
    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_FALSE(e->echo_seen);
    (void)slot;
}

// ------------------------------------------------------------------- ack

static void test_ack_right_dst_stops_and_frees_without_report(void)
{
    int slot = dmOutboxAdd(7, "OE1GGG", "p", 1, 3, 700, DM_RETRY_9);
    (void)slot;

    bool stopped = dmOutboxOnAck("OE1GGG", 7);
    TEST_ASSERT_TRUE(stopped);
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->acked);
    TEST_ASSERT_TRUE(g_report_log.empty());   // documented choice: OnAck never reports
}

static void test_ack_wrong_dst_is_ignored(void)
{
    int slot = dmOutboxAdd(8, "OE1HHH", "p", 1, 3, 800, DM_RETRY_9);
    bool stopped = dmOutboxOnAck("OE1ZZZ", 8);
    TEST_ASSERT_FALSE(stopped);
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
    TEST_ASSERT_NOT_NULL(dmOutboxEntry(slot));
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->acked);
}

static void test_ack_unknown_nnn_is_ignored(void)
{
    dmOutboxAdd(9, "OE1III", "p", 1, 3, 900, DM_RETRY_9);
    bool stopped = dmOutboxOnAck("OE1III", 999);
    TEST_ASSERT_FALSE(stopped);
    TEST_ASSERT_EQUAL_INT(1, dmOutboxUsed());
}

static void test_ack_after_attempt_1_2_and_8(void)
{
    // After attempt 1 (right after Add, no loop tick yet).
    {
        int slot = dmOutboxAdd(10, "OE1AA1", "p", 1, 3, 1100, DM_RETRY_9);
        (void)slot;
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA1", 10));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
    }

    // After attempt 2.
    {
        g_now = 0;
        int slot = dmOutboxAdd(11, "OE1AA2", "p", 1, 3, 1200, DM_RETRY_9);
        advanceToDueAndTick(slot);
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA2", 11));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
    }

    // After attempt 8 (one short of give-up on a 9-mode ladder).
    {
        g_now = 0;
        int slot = dmOutboxAdd(12, "OE1AA3", "p", 1, 3, 1300, DM_RETRY_9);
        for(int i = 0; i < 7; i++)
            advanceToDueAndTick(slot);   // attempts 2..8
        const struct DmOutboxEntry *e = dmOutboxEntry(slot);
        TEST_ASSERT_EQUAL_UINT8(8, e->attempt);
        TEST_ASSERT_TRUE(dmOutboxOnAck("OE1AA3", 12));
        TEST_ASSERT_NULL(dmOutboxEntry(slot));
        // The give-up on attempt 9 never happens now.
        g_now = e->next_ms;
        dmOutboxLoop();
        TEST_ASSERT_TRUE(g_report_log.empty());
    }
}

// ----------------------------------------------------------------- held

static void test_held_suppresses_giveup_report_but_still_counts(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(13, "OE1JJJ", "p", 1, 3, 1400, DM_RETRY_3);
    dmOutboxOnHeld(13);

    advanceToDueAndTick(slot);   // attempt 2
    advanceToDueAndTick(slot);   // attempt 3 -> give-up

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

// F1 (fable-dm-stage1-verdict-20260914.md, T2): the fix moved
// dmOutboxOnAck() out from behind the CALL SITE's checkOwnTx() gate
// (lora_functions.cpp/udp_functions.cpp/nrf_eth.cpp) -- the core itself
// (dm_outbox.cpp) never knew about own_msg_id[] and was already keyed on
// (dst, nnn) alone (Confirmation 2 in the verdict: "Core -- CONFIRMED").
// This test pins that core-side guarantee natively, the way the verdict's
// "test via the return value + report path" asks: run a ladder far enough
// that last_id has moved through several fresh ids (standing in for
// first_id having rotated out of a 20-slot msg_id table a real firmware
// would keep), then ack on (dst, nnn) alone -- the outbox must still stop,
// and dmOutboxLoop() must never call report() (0x03 give-up) for this
// entry afterwards, no matter how much later it is asked to run again.
static void test_f1_ack_on_nnn_dst_stops_ladder_after_several_fresh_ids(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(60, "OE1XXX", "p", 1, 3, 6000, DM_RETRY_9);
    for(int i = 0; i < 4; i++)
        advanceToDueAndTick(slot);   // attempts 2..5: several fresh ids minted

    const struct DmOutboxEntry *e = dmOutboxEntry(slot);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_TRUE(e->last_id != e->first_id);
    TEST_ASSERT_TRUE(e->last_id >= 900000UL);   // a fresh (minted) id, not first_id

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

// -------------------------------------------------------------------- bp

static void test_bp_blocks_without_advancing_next_ms(void)
{
    g_now = 0;
    int slot = dmOutboxAdd(15, "OE1LLL", "p", 1, 3, 1600, DM_RETRY_9);
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
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->same_id_retries);

    g_transmit_result = true;
    dmOutboxLoop();   // still due (next_ms unchanged) -- succeeds this time
    TEST_ASSERT_EQUAL_INT(2, (int)g_tx_log.size());
    e = dmOutboxEntry(slot);
    TEST_ASSERT_EQUAL_UINT8(2, e->attempt);
    TEST_ASSERT_EQUAL_UINT32(1, dmOutboxCounters()->same_id_retries);
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
    TEST_ASSERT_EQUAL_UINT32(1800, g_tx_log.back().id);
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
    int s1 = dmOutboxAdd(30, "OE1PPP", "p", 1, 3, 3000, DM_RETRY_9);
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
    int slot = dmOutboxAdd(40, "OE1VVV", "p", 1, 3, 4000, DM_RETRY_9);
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
    // no same-id/fresh count, no give-up, no second free-later state.
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->same_id_retries);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->fresh_attempts);
    TEST_ASSERT_EQUAL_UINT32(0, dmOutboxCounters()->gaveup);
    TEST_ASSERT_TRUE(g_report_log.empty());

    // A further tick must not crash or resurrect anything.
    dmOutboxLoop();
    TEST_ASSERT_EQUAL_INT(0, dmOutboxUsed());
}

// ------------------------------------------------------------------ main

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_add_room_and_full);
    RUN_TEST(test_add_off_mode_is_rejected_without_touching_counters);
    RUN_TEST(test_add_populates_entry_fields);

    RUN_TEST(test_schedule_offsets_9);
    RUN_TEST(test_schedule_offsets_3);

    RUN_TEST(test_echo_gate_no_relay_keeps_same_id);
    RUN_TEST(test_echo_gate_relay_heard_goes_fresh);
    RUN_TEST(test_echo_on_last_id_also_gates);
    RUN_TEST(test_echo_unknown_id_is_ignored);

    RUN_TEST(test_ack_right_dst_stops_and_frees_without_report);
    RUN_TEST(test_ack_wrong_dst_is_ignored);
    RUN_TEST(test_ack_unknown_nnn_is_ignored);
    RUN_TEST(test_ack_after_attempt_1_2_and_8);
    RUN_TEST(test_f1_ack_on_nnn_dst_stops_ladder_after_several_fresh_ids);

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

    return UNITY_END();
}
