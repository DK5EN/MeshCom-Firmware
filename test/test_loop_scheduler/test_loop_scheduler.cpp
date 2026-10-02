// Twin test for the D1-10 loop scheduler (src/loop_scheduler.h/.cpp).
//
//   pio test -e native_loop_scheduler -f test_loop_scheduler
//
// Only loop_scheduler.cpp is compiled from src (see platformio.ini). Every
// symbol its table references -- the timer globals, the shared enabled()
// functions' inputs, and every per-platform action/enabled() function -- is
// defined right here instead of pulling in the real
// src/esp32/loop_actions_esp32.cpp / src/nrf52/loop_actions_nrf52.cpp (which
// would drag in real radio/sensor driver calls this native build has no
// business linking). This is a mechanics test: it proves loopSchedulerRun()
// fires/resets/gates correctly, not that any one sensor read is correct
// (the pio board builds are what prove the real bodies still compile and
// link under their original #if guards).

#include <unity.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Arduino.h>
#include <configuration.h>

#include <loop_scheduler.h>
#include <loop_functions_extern.h>

#include "../support/teleport_clock.h"

// ---- Timer globals the table's entries point at ---------------------
// "the EXISTING global (never a new variable)" -- in production these are
// each platform main's own; here the test IS the platform for these symbols.
unsigned long retransmit_timer = 0;
unsigned long mcp_refresh_timer = 0;
unsigned long BattTimeWait = 0;
unsigned long BMP3TimeWait = 0;
unsigned long MCU811TimeWait = 0;
unsigned long INA226TimeWait = 0;
// heapMonTimer itself is defined in loop_scheduler.cpp (the one file
// compiled from src here), not here.

// ---- Inputs to the SHARED enabled() functions (loop_scheduler.cpp) -----
bool bBMP3ON = false;
bool bmp3_found = false;
std::atomic<bool> tx_is_active{false};
is_receiving_t is_receiving{false};

// ---- Stub action/enabled() functions -----------------------------------
// Call counters, one per table entry, plus settable "enabled" flags for the
// per-platform entries (retransmit/heapMon/mcu811/ina226) so a test can
// exercise design point (4) without touching production globals.
struct Counts
{
    int retransmit = 0;
    int mcpRefresh = 0;
    int heapMon = 0;
    int bmp3 = 0;
    int mcu811 = 0;
    int ina226 = 0;
    int battCheck = 0;
};
static Counts g_counts;

static bool g_retransmitEnabled = true;
static bool g_heapMonEnabled = true;
static bool g_mcu811Enabled = true;
static bool g_ina226Enabled = true;

// Design point (3): an action that advances the fake clock, to tell
// reset_before apart from reset_after. 0 unless a test opts in.
static unsigned long g_actionAdvanceMs = 0;

bool loopEnabled_retransmit(void) { return g_retransmitEnabled; }
void loopAction_retransmit(void) { g_counts.retransmit++; mc_test_advance_millis(g_actionAdvanceMs); }

#if defined(ENABLE_MCP23017)
void loopAction_mcpRefresh(void) { g_counts.mcpRefresh++; }
#endif

bool loopEnabled_heapMon(void) { return g_heapMonEnabled; }
void loopAction_heapMon(void) { g_counts.heapMon++; }

#if defined(ENABLE_BMP390)
void loopAction_bmp3(void) { g_counts.bmp3++; }
#endif

#if defined(ENABLE_MC811)
bool loopEnabled_mcu811(void) { return g_mcu811Enabled; }
void loopAction_mcu811(void) { g_counts.mcu811++; }
#endif

#if defined(ENABLE_INA226)
bool loopEnabled_ina226(void) { return g_ina226Enabled; }
void loopAction_ina226(void) { g_counts.ina226++; }
#endif

void loopAction_battCheck(void) { g_counts.battCheck++; }

// ---- Fixture -------------------------------------------------------------

void setUp(void)
{
    mc_test_set_millis(0);

    retransmit_timer = 0;
    mcp_refresh_timer = 0;
    BattTimeWait = 0;
    heapMonTimer = 0;
    BMP3TimeWait = 0;
    MCU811TimeWait = 0;
    INA226TimeWait = 0;

    // Default every entry ENABLED, matching a node with every sensor
    // present and the radio idle -- individual tests flip these off.
    bBMP3ON = true;
    bmp3_found = true;
    tx_is_active = false;
    is_receiving = false;
    g_retransmitEnabled = true;
    g_heapMonEnabled = true;
    g_mcu811Enabled = true;
    g_ina226Enabled = true;

    g_actionAdvanceMs = 0;
    g_counts = Counts{};
}

void tearDown(void) {}

// ---- (1) nothing fires at boot -------------------------------------------

static void test_no_entry_fires_at_boot(void)
{
    loopSchedulerRun(millis());

    TEST_ASSERT_EQUAL(0, g_counts.retransmit);
    TEST_ASSERT_EQUAL(0, g_counts.mcpRefresh);
    TEST_ASSERT_EQUAL(0, g_counts.heapMon);
    TEST_ASSERT_EQUAL(0, g_counts.bmp3);
    TEST_ASSERT_EQUAL(0, g_counts.mcu811);
    TEST_ASSERT_EQUAL(0, g_counts.ina226);
    TEST_ASSERT_EQUAL(0, g_counts.battCheck);
}

// ---- (2) each entry fires exactly once at its own interval ---------------

static void test_retransmit_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_retransmit());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.retransmit);

    // Same pass again immediately: timer was just reset, must not re-fire.
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.retransmit);

    // One interval later: fires again, exactly once.
    mc_test_advance_millis(loopInterval_retransmit());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(2, g_counts.retransmit);
}

static void test_mcp_refresh_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_mcpRefresh());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.mcpRefresh);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.mcpRefresh);
}

static void test_heap_mon_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_heapMon());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.heapMon);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.heapMon);
}

static void test_bmp3_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_bmp3());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.bmp3);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.bmp3);
}

static void test_mcu811_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_mcu811());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.mcu811);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.mcu811);
}

static void test_ina226_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_ina226());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.ina226);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.ina226);
}

static void test_batt_check_fires_once_per_interval(void)
{
    mc_test_set_millis(loopInterval_battCheck());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.battCheck);
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.battCheck);
}

// ---- (3) reset_before vs reset_after semantics ----------------------------
// Every migrated entry is reset_before=false (old code reset at the bottom
// of its body). Prove the runner honours that: if the action itself
// advances the clock (as loopAction_retransmit can here), reset_after means
// the timer lands on the POST-action clock, not the pre-action `now`.

static void test_reset_after_uses_post_action_clock(void)
{
    g_actionAdvanceMs = 100;

    mc_test_set_millis(loopInterval_retransmit());
    unsigned long now = millis();
    loopSchedulerRun(now);

    TEST_ASSERT_EQUAL(1, g_counts.retransmit);
    // reset_before=false -> *timer = millis() AFTER the action ran, i.e.
    // now + 100, not `now` itself.
    TEST_ASSERT_EQUAL_UINT32(now + 100, retransmit_timer);
}

// ---- (4) enabled()==false suppresses firing AND the reset -----------------

static void test_disabled_entry_does_not_fire_or_reset(void)
{
    g_retransmitEnabled = false;

    mc_test_set_millis(loopInterval_retransmit() * 3); // well past the interval
    loopSchedulerRun(millis());

    TEST_ASSERT_EQUAL(0, g_counts.retransmit);
    TEST_ASSERT_EQUAL_UINT32(0, retransmit_timer); // never touched

    // Re-enabling and running again fires immediately (timer was never
    // reset, so it is still "overdue").
    g_retransmitEnabled = true;
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.retransmit);
}

// enabled() also gates the SHARED bmp3/battCheck guards, not just the
// per-platform stub flags above -- exercise those too.

static void test_bmp3_enabled_gate_via_shared_guard(void)
{
    bmp3_found = false; // loopEnabled_bmp3() == bBMP3ON && bmp3_found

    mc_test_set_millis(loopInterval_bmp3());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(0, g_counts.bmp3);
    TEST_ASSERT_EQUAL_UINT32(0, BMP3TimeWait);
}

static void test_batt_check_enabled_gate_via_tx_guard(void)
{
    tx_is_active = true; // loopEnabled_battCheck() == !tx_is_active && !is_receiving

    mc_test_set_millis(loopInterval_battCheck());
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(0, g_counts.battCheck);
    TEST_ASSERT_EQUAL_UINT32(0, BattTimeWait);

    tx_is_active = false;
    loopSchedulerRun(millis());
    TEST_ASSERT_EQUAL(1, g_counts.battCheck);
}

// ---- (5) millis() rollover -------------------------------------------

static void test_rollover_across_uint32_wrap(void)
{
    // retransmit_timer parked just before the wrap; now just after it.
    // (uint32_t)(now - timer) must still compute the true elapsed time
    // across the wrap, exactly like it does anywhere else this idiom is
    // used on a 32-bit millis() counter.
    retransmit_timer = 0xFFFFFF00UL;                  // 4294967040
    uint32_t now = (uint32_t)(0xFFFFFF00UL + loopInterval_retransmit()); // wraps

    loopSchedulerRun(now);

    TEST_ASSERT_EQUAL(1, g_counts.retransmit);
}

static void test_no_rollover_false_fire_just_before_interval(void)
{
    retransmit_timer = 0xFFFFFF00UL;
    uint32_t now = (uint32_t)(0xFFFFFF00UL + loopInterval_retransmit() - 1); // 1 ms short, wraps too

    loopSchedulerRun(now);

    TEST_ASSERT_EQUAL(0, g_counts.retransmit);
}

// ---- (6) table sanity -------------------------------------------------

static void test_table_has_seven_unique_named_entries(void)
{
    size_t n = 0;
    const LoopTimerEntry* table = loopSchedulerTable(&n);

    TEST_ASSERT_EQUAL_MESSAGE(7, (int)n,
        "env:native_loop_scheduler enables every sensor flag (stubs/"
        "configuration.h), so all 7 migrated entries must be present: "
        "retransmit, mcpRefresh, battCheck, heapMon, bmp3, mcu811, ina226");

    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_NOT_NULL(table[i].name);
        TEST_ASSERT_NOT_NULL(table[i].timer);
        TEST_ASSERT_NOT_NULL(table[i].interval_ms);
        TEST_ASSERT_NOT_NULL(table[i].action);

        for (size_t j = i + 1; j < n; j++)
        {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcmp(table[i].name, table[j].name),
                "duplicate entry name in the loop scheduler table");
        }
    }
}

static void test_battcheck_and_ina226_intervals_are_the_2026_09_11_unification(void)
{
    // BACKLOG, DRY unification operator decision 2026-09-11: INA226TimeWait's
    // cadence unified at 60 s, matching the nRF52 side. BattTimeWait was
    // slowed to 30 s then, which the battery consolidation (concept
    // 2026-09-23, wave 3) reverted as a neo regression: battCheck is now the
    // 100 ms TICK of the sampler in batt_pipeline.h (the 1 s / 30 s sampling
    // cadence is decided by battSchedTick(), not by the table). Pin the
    // decided numbers so a future change to either cadence is a deliberate
    // table edit, not a silent drift.
    TEST_ASSERT_EQUAL_UINT32(100,   loopInterval_battCheck());
    TEST_ASSERT_EQUAL_UINT32(60000, loopInterval_ina226());
    // Advisor (2026-09-17): the cadence tests above compare against the
    // same accessor on both sides, so a wrong constant would pass them.
    // Pin every interval to its literal from the old inline predicates.
    TEST_ASSERT_EQUAL_UINT32(2000,  loopInterval_retransmit());
    TEST_ASSERT_EQUAL_UINT32(5000,  loopInterval_mcpRefresh());
    TEST_ASSERT_EQUAL_UINT32(60000, loopInterval_heapMon());
    TEST_ASSERT_EQUAL_UINT32(60000, loopInterval_bmp3());
    TEST_ASSERT_EQUAL_UINT32(60000, loopInterval_mcu811());
}


// ---- (7) millis() teleportation -------------------------------------------
// The scheduler compares `(uint32_t)(now - *timer) >= interval` and resets
// with `*timer = millis()`. Put the clock just before the uint32 wrap, across
// it and on big forward hops and keep ticking (1 ms steps, the real code, a
// virtual clock): every entry must keep firing at exactly its interval, never
// burst, never stall.

struct TeleTrace
{
    // table order as g_counts: retransmit, mcpRefresh, battCheck, heapMon, bmp3, mcu811, ina226
    std::vector<uint32_t> t[7];
};

static const int kNEnt = 7;

static uint32_t teleInterval(int e)
{
    switch (e)
    {
    case 0: return loopInterval_retransmit();
    case 1: return loopInterval_mcpRefresh();
    case 2: return loopInterval_battCheck();
    case 3: return loopInterval_heapMon();
    case 4: return loopInterval_bmp3();
    case 5: return loopInterval_mcu811();
    default: return loopInterval_ina226();
    }
}

static unsigned long *teleTimer(int e)
{
    unsigned long *tm[kNEnt] = {&retransmit_timer, &mcp_refresh_timer, &BattTimeWait, &heapMonTimer,
                                &BMP3TimeWait, &MCU811TimeWait, &INA226TimeWait};
    return tm[e];
}

static const char *teleName(int e)
{
    static const char *nm[kNEnt] = {"retransmit", "mcpRefresh", "battCheck", "heapMon", "bmp3", "mcu811", "ina226"};
    return nm[e];
}

static void teleSnap(int out[kNEnt])
{
    out[0] = g_counts.retransmit;
    out[1] = g_counts.mcpRefresh;
    out[2] = g_counts.battCheck;
    out[3] = g_counts.heapMon;
    out[4] = g_counts.bmp3;
    out[5] = g_counts.mcu811;
    out[6] = g_counts.ina226;
}

// One scheduler pass at `now`, mirrored into the stub millis(); records the
// time of every fire and fails on a burst (one entry twice in one pass).
static void teleStep(uint32_t now, TeleTrace &tr)
{
    int before[kNEnt], after[kNEnt];
    teleSnap(before);
    mc_test_set_millis(now);
    loopSchedulerRun(now);
    teleSnap(after);
    for (int e = 0; e < kNEnt; e++)
    {
        int d = after[e] - before[e];
        TEST_ASSERT_TRUE_MESSAGE(d == 0 || d == 1, teleName(e));   // never twice in one pass
        if (d == 1)
            tr.t[e].push_back(now);
    }
}

static void teleDrive(TeleportClock &c, uint64_t span_ms, TeleTrace &tr)
{
    c.tick(span_ms, 1, [&](uint32_t now) { teleStep(now, tr); });
}

// Fire times must be exactly first, first+I, first+2I, ... (mod 2^32).
static void teleAssertSteadyGaps(const TeleTrace &tr, int e)
{
    for (size_t i = 1; i < tr.t[e].size(); i++)
    {
        uint32_t gap = (uint32_t)(tr.t[e][i] - tr.t[e][i - 1]);
        char msg[96];
        snprintf(msg, sizeof(msg), "%s gap #%u", teleName(e), (unsigned)i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(teleInterval(e), gap, msg);
    }
}

// Timers parked mid-cycle (each at a different phase) k ms before the wrap,
// then ticked across it: the fire times must be exactly timer + n*interval,
// for k inside one 100 ms battCheck tick, inside the 2 s / 30 s windows and
// right on the wrap.
static void test_teleport_midcycle_across_wrap_exact_fire_times(void)
{
    const uint32_t ks[] = {0, 1, 50, 99, 100, 101, 1999, 2000, 2001, 29999, 30000, 30001};
    for (unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        setUp();
        TeleportClock c;
        const uint32_t T0 = TeleportClock::kWrapMinus(ks[ki]);
        c.teleport_to(T0);

        uint32_t timer0[kNEnt];
        for (int e = 0; e < kNEnt; e++)
        {
            uint32_t phase = (uint32_t)((uint64_t)teleInterval(e) * 3 / 7);   // 3/7 into the cycle
            timer0[e] = T0 - phase;
            *teleTimer(e) = timer0[e];
        }

        TeleTrace tr;
        teleDrive(c, 130000, tr);   // crosses the wrap for every k above

        for (int e = 0; e < kNEnt; e++)
        {
            const uint32_t I = teleInterval(e);
            size_t want = 0;
            for (uint32_t t = 1;; t++)    // count expected fires inside the 130 s span
            {
                uint64_t off = (uint64_t)(I - (uint32_t)((uint64_t)I * 3 / 7)) + (uint64_t)(t - 1) * I;
                if (off > 130000) break;
                want++;
            }
            char msg[96];
            snprintf(msg, sizeof(msg), "%s k=%u fire count", teleName(e), (unsigned)ks[ki]);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)want, (uint32_t)tr.t[e].size(), msg);
            for (size_t i = 0; i < tr.t[e].size(); i++)
            {
                uint32_t expect = (uint32_t)(timer0[e] + (uint32_t)(i + 1) * I);
                snprintf(msg, sizeof(msg), "%s k=%u fire #%u", teleName(e), (unsigned)ks[ki], (unsigned)i);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(expect, tr.t[e][i], msg);
            }
        }
    }
}

// Run normally, then teleport (a huge forward hop to just before the wrap,
// and hops of 1 h, 2^31-1, 2^31, 2^31+1, 2^32-1 ms). An entry that is
// overdue fires ONCE on the first pass (the timer is reset to millis()
// afterwards, so no catch-up burst), and every entry resumes at exactly its
// interval; none may stay silent longer than one interval after the hop.
static void teleForwardJumpCase(bool absolute, uint32_t param)
{
    setUp();
    TeleportClock c;
    c.teleport_to(5000);
    TeleTrace warm;
    teleDrive(c, 70000, warm);   // steady state: every entry has fired at least once
    for (int e = 0; e < kNEnt; e++)
        TEST_ASSERT_TRUE_MESSAGE(warm.t[e].size() >= 1, teleName(e));

    if (absolute)
        c.teleport_to(param);
    else
        c.advance(param);
    const uint32_t jumpAt = c.now_ms;

    TeleTrace tr;
    teleDrive(c, 130000, tr);

    for (int e = 0; e < kNEnt; e++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s never fires after the jump (stall)", teleName(e));
        TEST_ASSERT_TRUE_MESSAGE(!tr.t[e].empty(), msg);
        snprintf(msg, sizeof(msg), "%s first fire later than one interval after the jump", teleName(e));
        TEST_ASSERT_TRUE_MESSAGE((uint32_t)(tr.t[e][0] - jumpAt) <= teleInterval(e), msg);
        teleAssertSteadyGaps(tr, e);
        snprintf(msg, sizeof(msg), "%s fires too rarely", teleName(e));
        TEST_ASSERT_TRUE_MESSAGE(tr.t[e].size() >= 130000 / teleInterval(e) - 1, msg);
    }
}

static void test_teleport_run_then_hop_to_just_before_wrap(void)
{
    const uint32_t ks[] = {0, 50, 99, 100, 1999, 2000, 29999, 30000, 59999};
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++)
        teleForwardJumpCase(true, TeleportClock::kWrapMinus(ks[i]));
}

static void test_teleport_forward_jump_1h(void)         { teleForwardJumpCase(false, TeleportClock::kHour); }
static void test_teleport_forward_jump_2p31_minus_1(void) { teleForwardJumpCase(false, 0x7FFFFFFFu); }
static void test_teleport_forward_jump_2p31(void)       { teleForwardJumpCase(false, 0x80000000u); }
static void test_teleport_forward_jump_2p31_plus_1(void) { teleForwardJumpCase(false, 0x80000001u); }
static void test_teleport_forward_jump_2p32_minus_1(void) { teleForwardJumpCase(false, 0xFFFFFFFFu); }

// Boot seed: esp32_main.cpp ~2317 `if (BattTimeWait == 0) BattTimeWait =
// millis() - 30000;` (nrf52_main.cpp ~1362: - 31000). millis() is uint32_t
// on the target, so the subtraction wraps to just under 2^32 at a small boot
// millis; the first battCheck must fire on the FIRST pass (no 49-day stall)
// and then keep its 100 ms cadence.
static void teleBootSeedCase(uint32_t seedMs)
{
    const uint32_t boots[] = {0, 1, 50, 1000, 29999, 30000, 30001, 31000, 60000};
    for (unsigned i = 0; i < sizeof(boots) / sizeof(boots[0]); i++)
    {
        setUp();
        TeleportClock c;
        c.teleport_to(boots[i]);
        mc_test_set_millis(c.now_ms);

        // target arithmetic: unsigned long is 32 bit there
        if (BattTimeWait == 0)
            BattTimeWait = (uint32_t)(millis() - seedMs);

        TeleTrace tr;
        teleStep(c.now_ms, tr);   // first pass
        char msg[64];
        snprintf(msg, sizeof(msg), "boot=%u seed=%u first pass", (unsigned)boots[i], (unsigned)seedMs);
        TEST_ASSERT_EQUAL_MESSAGE(1, g_counts.battCheck, msg);
        TEST_ASSERT_EQUAL_UINT32(c.now_ms, (uint32_t)BattTimeWait);   // timer reset to millis()

        teleDrive(c, 1000, tr);
        TEST_ASSERT_EQUAL_MESSAGE(1 + 10, (int)tr.t[2].size(), msg);   // 100 ms cadence afterwards
        teleAssertSteadyGaps(tr, 2);
    }
}

static void test_teleport_boot_seed_esp32_minus_30000(void) { teleBootSeedCase(30000); }
static void test_teleport_boot_seed_nrf52_minus_31000(void) { teleBootSeedCase(31000); }

// enabled() flips off across the wrap (radio busy) and back on: the entry
// stays overdue while gated, fires exactly once when re-enabled, no burst.
static void test_teleport_gated_entry_across_wrap_fires_once_on_release(void)
{
    setUp();
    TeleportClock c;
    c.teleport_to(TeleportClock::kWrapMinus(2500));
    BattTimeWait = c.now_ms;
    TeleTrace tr;
    teleDrive(c, 1000, tr);
    const size_t before = tr.t[2].size();
    TEST_ASSERT_EQUAL_UINT32(10, (uint32_t)before);

    tx_is_active = true;
    teleDrive(c, 5000, tr);            // covers the wrap; gated
    TEST_ASSERT_EQUAL_UINT32((uint32_t)before, (uint32_t)tr.t[2].size());

    tx_is_active = false;
    teleDrive(c, 1000, tr);
    // overdue -> one fire on the first pass, then the 100 ms cadence
    TEST_ASSERT_EQUAL_UINT32((uint32_t)before + 1 + 9, (uint32_t)tr.t[2].size());
    // first fire right on the first pass after the release (1 ms), not an interval later
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)(tr.t[2][before] - (tr.t[2][before - 1] + 5000)));
    for (size_t i = before + 1; i < tr.t[2].size(); i++)
        TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)(tr.t[2][i] - tr.t[2][i - 1]));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_entry_fires_at_boot);

    RUN_TEST(test_retransmit_fires_once_per_interval);
    RUN_TEST(test_mcp_refresh_fires_once_per_interval);
    RUN_TEST(test_heap_mon_fires_once_per_interval);
    RUN_TEST(test_bmp3_fires_once_per_interval);
    RUN_TEST(test_mcu811_fires_once_per_interval);
    RUN_TEST(test_ina226_fires_once_per_interval);
    RUN_TEST(test_batt_check_fires_once_per_interval);

    RUN_TEST(test_reset_after_uses_post_action_clock);

    RUN_TEST(test_disabled_entry_does_not_fire_or_reset);
    RUN_TEST(test_bmp3_enabled_gate_via_shared_guard);
    RUN_TEST(test_batt_check_enabled_gate_via_tx_guard);

    RUN_TEST(test_rollover_across_uint32_wrap);
    RUN_TEST(test_no_rollover_false_fire_just_before_interval);

    RUN_TEST(test_table_has_seven_unique_named_entries);
    RUN_TEST(test_battcheck_and_ina226_intervals_are_the_2026_09_11_unification);

    RUN_TEST(test_teleport_midcycle_across_wrap_exact_fire_times);
    RUN_TEST(test_teleport_run_then_hop_to_just_before_wrap);
    RUN_TEST(test_teleport_forward_jump_1h);
    RUN_TEST(test_teleport_forward_jump_2p31_minus_1);
    RUN_TEST(test_teleport_forward_jump_2p31);
    RUN_TEST(test_teleport_forward_jump_2p31_plus_1);
    RUN_TEST(test_teleport_forward_jump_2p32_minus_1);
    RUN_TEST(test_teleport_boot_seed_esp32_minus_30000);
    RUN_TEST(test_teleport_boot_seed_nrf52_minus_31000);
    RUN_TEST(test_teleport_gated_entry_across_wrap_fires_once_on_release);

    return UNITY_END();
}
