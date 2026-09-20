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
#include <cstring>

#include <Arduino.h>
#include <configuration.h>

#include <loop_scheduler.h>
#include <loop_functions_extern.h>

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
    // BACKLOG, DRY unification operator decision 2026-09-11: BattTimeWait
    // slowed to 30 s and INA226TimeWait's cadence unified with it at 60 s,
    // matching the nRF52 side. Pin the decided numbers so a future change
    // to either cadence is a deliberate table edit, not a silent drift.
    TEST_ASSERT_EQUAL_UINT32(30000, loopInterval_battCheck());
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

    return UNITY_END();
}
