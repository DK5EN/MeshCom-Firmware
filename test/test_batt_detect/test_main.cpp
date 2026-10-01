// BAT-01 (BACKLOG.md sec. 3.8n): Heltec V3's battery % jumped wildly with no battery
// attached -- the floating VBAT divider node reads pure ADC noise (measured on-device,
// TM-38: 844 samples over 16 min, raw readings 3716-4886 mV, up to 1.17 V sample-to-sample
// at the 500 ms read_batt() cadence). battDetectUpdate()/battDetectReset() (batt_functions.h)
// are the pure decision core that turns that signature into a hysteresis-gated
// present/absent verdict -- no Arduino calls, so they run natively against synthetic mV
// series here instead of against real hardware.
//
// Battery consolidation wave 3: the detector now lives in src/batt_pipeline.h (batt_functions.h
// includes it, names unchanged), so the tests above run against the shared detector. The second
// half of this file covers the sample core batt_functions.cpp wires around it
// (battFeedSample(): detector on the raw sample -> dt based EMA -> "no reading" rules, and
// battLowVoltage(), battHardwarePresent(), mv_to_percent(), battSampleCount()).
#include <unity.h>

#include <stdio.h>

#include "batt_functions.h"
#include "../support/teleport_clock.h"

// Single-cell Li-Ion band used throughout (matches the T-Deck/T-Deck Plus/E213/E290/
// wireless-paper boards this detector actually ships on -- fBattMax ~4.2 V): the same
// band read_batt() derives from fBattMax*BATT_DETECT_{MIN,MAX}_BAND_FACTOR.
static const float kMinMv = 4200.0f * BATT_DETECT_MIN_BAND_FACTOR;   // 2310
static const float kMaxMv = 4200.0f * BATT_DETECT_MAX_BAND_FACTOR;   // 4830

static batt_detect_state_t g_state;

void setUp(void)
{
    battDetectReset(&g_state);
    battPipelineReset();
}

void tearDown(void) {}

// Drives g_state to the absent verdict using the backlog's own floating-pin signature
// (alternating 3716/4886 mV, a 1170 mV transition each step -- well over
// BATT_DETECT_MAX_DELTA_MV). The very first update only seeds "lastMv" (no delta to
// compare against yet), so it takes BATT_DETECT_ABSENT_STREAK+1 calls total to trip.
static void driveToAbsent(void)
{
    const float swing[] = {3716.0f, 4886.0f};
    for(int i = 0; i <= BATT_DETECT_ABSENT_STREAK; i++)
        battDetectUpdate(&g_state, swing[i % 2], kMinMv, kMaxMv);
}

// ---------------------------------------------------------------------- Scenarios

// Backlog scenario itself: the floating Heltec V3 pin swinging 3716/4886 mV every read.
// Each transition is a 1170 mV jump (over BATT_DETECT_MAX_DELTA_MV=250), and 4886 mV alone
// is already over the plausible band's top (4830). The verdict must flip to absent within
// a small, bounded number of samples.
static void test_noisy_floating_pin_geht_auf_absent(void)
{
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_ABSENT_STREAK, g_state.implausibleStreak);
}

// A real cell at rest: small jitter only (+-10 mV), well inside both the delta and the
// band test. Verdict must stay present indefinitely.
static void test_stabile_zelle_bleibt_present(void)
{
    const float jitter[] = {3900.0f, 3895.0f, 3905.0f, 3898.0f, 3903.0f, 3897.0f};
    bool present = true;

    for(int cycle = 0; cycle < 30; cycle++)
        present = battDetectUpdate(&g_state, jitter[cycle % 6], kMinMv, kMaxMv);

    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
}

// A slow discharge ramp (5 mV/sample, far under the 250 mV delta threshold) must never
// trip -- this is the normal-operation case the detector must not false-positive on.
static void test_entlade_rampe_bleibt_present(void)
{
    bool present = true;
    float mv = 4100.0f;

    for(int i = 0; i < 160 && mv > 3300.0f; i++, mv -= 5.0f)
        present = battDetectUpdate(&g_state, mv, kMinMv, kMaxMv);

    TEST_ASSERT_TRUE(present);
}

// Hysteresis against a single noise spike while present: one out-of-band/large-delta
// sample must not trip ABSENT_STREAK on its own. Its exit back to baseline is itself a
// large delta (still counted implausible -- the streak needs one more sample after that
// to fully clear), but the whole event only ever reaches streak=2, nowhere near
// BATT_DETECT_ABSENT_STREAK(6).
static void test_einzelner_ausreisser_kippt_nicht(void)
{
    battDetectUpdate(&g_state, 3900.0f, kMinMv, kMaxMv);
    bool present = battDetectUpdate(&g_state, 3900.0f, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);

    present = battDetectUpdate(&g_state, 4900.0f, kMinMv, kMaxMv);   // the spike
    TEST_ASSERT_TRUE(present);

    present = battDetectUpdate(&g_state, 3900.0f, kMinMv, kMaxMv);   // exiting the spike: still a jump
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(2, g_state.implausibleStreak);

    present = battDetectUpdate(&g_state, 3900.0f, kMinMv, kMaxMv);   // genuinely back to normal
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
}

// Hysteresis boundary, absent-side: exactly BATT_DETECT_ABSENT_STREAK-1 implausible
// transitions must NOT flip the verdict; the next one must.
static void test_absent_streak_grenzwert(void)
{
    battDetectUpdate(&g_state, 3716.0f, kMinMv, kMaxMv);   // seed, no streak yet

    bool present = true;
    for(int i = 0; i < BATT_DETECT_ABSENT_STREAK - 1; i++)
        present = battDetectUpdate(&g_state, (i % 2 == 0) ? 4886.0f : 3716.0f, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_ABSENT_STREAK - 1, g_state.implausibleStreak);

    // continue the alternation (the loop above ended on 4886, so the next value must
    // differ enough to still count as a jump, not repeat it)
    present = battDetectUpdate(&g_state, 3716.0f, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
}

// Hysteresis boundary, recovery-side (battery plugged back in): once absent, exactly
// BATT_DETECT_PRESENT_STREAK-1 plausible samples must NOT flip back; the next one must.
static void test_present_streak_grenzwert_bei_erholung(void)
{
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);

    // battery reconnected: the reading now holds steady wherever the last noisy sample
    // happened to land (delta 0 from there -> unambiguously plausible from the first call).
    float stableMv = g_state.lastMv;

    bool present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);   // plausibleStreak -> 1
    TEST_ASSERT_FALSE(present);

    for(int i = 1; i < BATT_DETECT_PRESENT_STREAK - 1; i++)
        present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_PRESENT_STREAK - 1, g_state.plausibleStreak);

    present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
}

// battDetectReset() must restore the fail-safe "present" assumption, not carry over a
// tripped verdict from a previous state instance's lifetime (e.g. after --batt factor
// re-inits the ADC path).
static void test_reset_stellt_failsafe_present_wieder_her(void)
{
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);

    battDetectReset(&g_state);
    TEST_ASSERT_TRUE(g_state.present);
    TEST_ASSERT_FALSE(g_state.haveLast);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
    TEST_ASSERT_EQUAL_INT(0, g_state.plausibleStreak);
}

// A 2S pack (TBEAM_1W/E22, fBattMax ~8.2 V) must not false-positive on its own legitimate
// resting voltage just because it is outside the single-cell band used elsewhere in this
// file -- read_batt() derives the band from fBattMax, so the caller passes a wider one.
static void test_2s_pack_band_bleibt_present(void)
{
    const float band2sMin = 8200.0f * BATT_DETECT_MIN_BAND_FACTOR;
    const float band2sMax = 8200.0f * BATT_DETECT_MAX_BAND_FACTOR;
    const float jitter[] = {7400.0f, 7390.0f, 7410.0f, 7395.0f};
    bool present = true;

    for(int cycle = 0; cycle < 20; cycle++)
        present = battDetectUpdate(&g_state, jitter[cycle % 4], band2sMin, band2sMax);

    TEST_ASSERT_TRUE(present);
}


// ----------------------------------------------------- sample core (batt_functions.cpp)

// pack max 4.1 V (BAT_MAX_VOLTAGE of the single-cell boards), in mV
static const float kPackMv = 4100.0f;

// Feeds `n` identical raw samples `periodMs` apart starting at t0; returns the last reported value.
static float feedConst(float rawMv, int n, uint32_t periodMs, uint32_t t0, uint32_t *tEnd = nullptr)
{
    float out = 0.0f;
    uint32_t t = t0;
    for(int i = 0; i < n; i++, t += periodMs)
        out = battFeedSample(rawMv, kPackMv, t);
    if(tEnd) *tEnd = t;
    return out;
}

// The old path seeded the filter with fBattMax (a fake full cell) and ramped down from there;
// the pipeline seeds with the first sample, so a real 3.9 V cell reads 3.9 V from the first second.
static void test_core_erstes_sample_seedet_ema(void)
{
    float mv = battFeedSample(3900.0f, kPackMv, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3900.0f, mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3900.0f, battFilteredMv());
    TEST_ASSERT_FALSE(battSettled());
}

// EMA tau is 30 s of REAL time: after one tau a 100 mV step is 63.2 % through, whatever the
// sample cadence (1 s fixed profile, 100 ms tick, 30 s switched profile -> 1 sample per tau).
static void test_core_ema_tau_ist_kadenzunabhaengig(void)
{
    const uint32_t periods[] = {100, 1000, 5000};
    for(size_t i = 0; i < sizeof(periods) / sizeof(periods[0]); i++)
    {
        battPipelineReset();
        uint32_t t = 0;
        feedConst(3900.0f, 1, periods[i], 0, &t);
        uint32_t tEnd = 0;
        // step to 4000 mV (100 mV: under the 250 mV detector delta), then run for exactly 30 s
        float mv = feedConst(4000.0f, (int)(30000 / periods[i]), periods[i], t, &tEnd);
        TEST_ASSERT_FLOAT_WITHIN(2.0f, 3900.0f + 100.0f * 0.632f, mv);
    }
}

// battSampleCount(): once per real sample, monotonic (init_batt()/reset does not rewind it)
static void test_core_sample_zaehler(void)
{
    uint32_t n0 = battSampleCount();
    feedConst(3900.0f, 5, 1000, 0);
    TEST_ASSERT_EQUAL_UINT32(n0 + 5, battSampleCount());
    battPipelineReset();
    TEST_ASSERT_EQUAL_UINT32(n0 + 5, battSampleCount());
}

// Floating pin: detector flips to absent after ABSENT_STREAK samples, the value becomes
// "no reading" (0) and battHardwarePresent() turns false (no /B= on air).
static void test_core_floating_pin_meldet_no_reading(void)
{
    TEST_ASSERT_TRUE(battHardwarePresent());   // before the first sample: fail-safe present

    battFeedSample(3900.0f, kPackMv, 0);
    TEST_ASSERT_TRUE(battHardwarePresent());

    const float swing[] = {3716.0f, 4886.0f};
    float mv = 1.0f;
    uint32_t t = 1000;
    for(int i = 0; i <= BATT_DETECT_ABSENT_STREAK + 1; i++, t += 1000)
        mv = battFeedSample(swing[i % 2], kPackMv, t);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv);
    TEST_ASSERT_FALSE(battDetected());
    TEST_ASSERT_FALSE(battHardwarePresent());
    TEST_ASSERT_FALSE(battLowVoltage(3300.0f));
}

// The cell comes back: the EMA restarts on the first plausible sample (not an average with the
// floating-pin noise) and the settle rule starts over.
static void test_core_akku_zurueck_reseedet_ema(void)
{
    const float swing[] = {3716.0f, 4886.0f};
    uint32_t t = 0;
    for(int i = 0; i <= BATT_DETECT_ABSENT_STREAK + 1; i++, t += 1000)
        battFeedSample(swing[i % 2], kPackMv, t);
    TEST_ASSERT_FALSE(battDetected());

    // the first stable sample still jumps from the last noisy one (implausible), so the
    // plausible streak reaches PRESENT_STREAK one sample later
    float mv = 0.0f;
    for(int i = 0; i <= BATT_DETECT_PRESENT_STREAK; i++, t += 1000)
        mv = battFeedSample(3800.0f, kPackMv, t);

    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3800.0f, mv);
    TEST_ASSERT_FALSE(battSettled());
    TEST_ASSERT_TRUE(battHardwarePresent());
}

// Filtered value < 1000 mV reports 0 ("no reading": T-Deck header shows USB, /B= suppressed).
// A small pack max (1500 mV) keeps the detector's band around 900 mV so this isolates the rule.
static void test_core_unter_1000mv_ist_no_reading(void)
{
    float mv = 1.0f;
    for(int i = 0; i < 4; i++)
        mv = battFeedSample(900.0f, 1500.0f, 1000u * (uint32_t)i);
    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv);
    TEST_ASSERT_FALSE(battHardwarePresent());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv_to_percent(mv));
}

// mv_to_percent(): 0 for 0 mV (no reading, not "USB = 100 %" any more), otherwise battPercent()
// scaled to node_maxv (fBattMax defaults to BAT_MAX_VOLTAGE, 4.1 V here).
static void test_core_mv_to_percent(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv_to_percent(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv_to_percent(999.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, mv_to_percent(4100.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, mv_to_percent(4300.0f));
    TEST_ASSERT_EQUAL_FLOAT((float)battPercent(3800.0f, 4100.0f), mv_to_percent(3800.0f));
    TEST_ASSERT_TRUE(mv_to_percent(3900.0f) > mv_to_percent(3700.0f));
    TEST_ASSERT_TRUE(mv_to_percent(3300.0f) < 5.0f);
}

// Low-voltage rule: only on the settled EMA (3 tau = 90 s AND 8 samples), never on a bad first
// sample, only inside 1 V < EMA <= threshold.
static void test_core_low_voltage_nur_settled(void)
{
    uint32_t t = 0;
    for(int i = 0; i < 60; i++, t += 1000)                 // 60 s at 3.25 V: not settled yet
        battFeedSample(3250.0f, kPackMv, t);
    TEST_ASSERT_FALSE(battSettled());
    TEST_ASSERT_FALSE(battLowVoltage(3300.0f));

    for(int i = 0; i < 40; i++, t += 1000)                 // 100 s total: settled
        battFeedSample(3250.0f, kPackMv, t);
    TEST_ASSERT_TRUE(battSettled());
    TEST_ASSERT_TRUE(battLowVoltage(3300.0f));
    TEST_ASSERT_FALSE(battLowVoltage(3200.0f));            // above threshold: no
}

// A sagging FIRST sample (boot on a weak cell) must not put the node to sleep: it seeds the EMA,
// but the settle rule keeps battLowVoltage() false until the filter has washed the seed out.
static void test_core_schlechtes_erstes_sample_loest_nichts_aus(void)
{
    uint32_t t = 0;
    battFeedSample(3000.0f, kPackMv, t);
    t += 1000;
    for(int i = 0; i < 6; i++, t += 1000)                  // 8 samples in total, only 7 s
        battFeedSample(3900.0f, kPackMv, t);
    TEST_ASSERT_FALSE(battSettled());
    TEST_ASSERT_FALSE(battLowVoltage(3300.0f));
}

// A battery at a healthy voltage is never "low", settled or not.
static void test_core_gesunder_akku_nie_low(void)
{
    feedConst(3900.0f, 200, 1000, 0);
    TEST_ASSERT_TRUE(battSettled());
    TEST_ASSERT_FALSE(battLowVoltage(3300.0f));
}

// ------------------------------------------------- BAT-03: window spread, switched divider
// Heltec V3 without a cell, measured 2026-10-01 (docs/batt-nocell-campaign-20261001.md): the divider
// sits on the charger-output sawtooth, one single read per window lands anywhere in 3.7-4.9 V. This
// series is --battprobe LONG cycle 1 (divider on 3 s, one read every 100 ms, x 4.18 mV/count); it
// stands in for one read per 30 s window at a random phase (pack max 4.2 V -> band 2310..4830 mV).
static const float kNoCellSeries[] = {
    4694, 4368, 4176, 4009, 3850, 3724, 4782, 4585, 4368, 4209, 4030, 3908, 3741, 4811, 4623, 4410,
    4201, 4084, 3917, 3779, 4853, 4640, 4460, 4251, 4109, 3929, 3787, 4874, 4577, 4389, 4226
};
static const int kNoCellN = (int)(sizeof(kNoCellSeries) / sizeof(kNoCellSeries[0]));
static const float kNoCellSpreadMv = 937.0f;   // BURST cycle 1, reads 50-57 (t = 100-114 ms)
static const float kSwitchedMaxMv  = 4200.0f;

// Shipped bug, documented: without the window spread the same series ends "present" with a
// nonzero value (the single reads are inside the band, the 30 s deltas rarely exceed 250 mV).
static void test_batt03_no_cell_ohne_spread_bleibt_present_bug(void)
{
    float mv = 0.0f;
    uint32_t t = 0;
    for(int i = 0; i < kNoCellN; i++, t += 30000)
        mv = battFeedSample(kNoCellSeries[i], kSwitchedMaxMv, t);

    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_TRUE(battHardwarePresent());
    TEST_ASSERT_TRUE(mv > 0.0f);
}

// Fixed: the same series with the 937 mV window spread is "no reading" after ABSENT_STREAK samples.
static void test_batt03_no_cell_mit_spread_geht_auf_absent(void)
{
    float mv = 1.0f;
    uint32_t t = 0;
    for(int i = 0; i < BATT_DETECT_ABSENT_STREAK - 1; i++, t += 30000)
        mv = battFeedSampleSpread(kNoCellSeries[i], kNoCellSpreadMv, kSwitchedMaxMv, t);
    TEST_ASSERT_TRUE(battHardwarePresent());   // one sample short of the streak: still the fail-safe
    TEST_ASSERT_TRUE(mv > 0.0f);

    mv = battFeedSampleSpread(kNoCellSeries[BATT_DETECT_ABSENT_STREAK - 1], kNoCellSpreadMv, kSwitchedMaxMv, t);
    t += 30000;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv);
    TEST_ASSERT_FALSE(battDetected());
    TEST_ASSERT_FALSE(battHardwarePresent());

    for(int i = BATT_DETECT_ABSENT_STREAK; i < kNoCellN; i++, t += 30000)
        mv = battFeedSampleSpread(kNoCellSeries[i], kNoCellSpreadMv, kSwitchedMaxMv, t);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mv);
    TEST_ASSERT_FALSE(battHardwarePresent());
}

// A real cell on the switched divider: reads 3958-4034 mV, window spread <= 120 mV (measured worst
// case 113 mV) -> stays present, reports a value. Spread 120 is far under the 300 mV limit.
static void test_batt03_zelle_mit_kleinem_spread_bleibt_present(void)
{
    const float cell[] = {3958, 3981, 4012, 3990, 3965, 4034, 4002, 3975, 3996, 4021};
    const float spread[] = {20, 50, 113, 0, 35, 120, 60, 10, 80, 45};
    float mv = 0.0f;
    uint32_t t = 0;
    for(int i = 0; i < 60; i++, t += 30000)
        mv = battFeedSampleSpread(cell[i % 10], spread[i % 10], kSwitchedMaxMv, t);

    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_TRUE(battHardwarePresent());
    TEST_ASSERT_TRUE(mv > 3900.0f && mv < 4100.0f);
}

// One bad window (spread over the limit) between good ones is a single implausible sample: far
// below ABSENT_STREAK, the verdict does not flip and the value keeps reporting.
static void test_batt03_ein_schlechtes_fenster_kippt_nicht(void)
{
    uint32_t t = 0;
    float mv = 0.0f;
    for(int i = 0; i < 10; i++, t += 30000)
        mv = battFeedSampleSpread(3990.0f, 30.0f, kSwitchedMaxMv, t);
    TEST_ASSERT_TRUE(mv > 0.0f);

    mv = battFeedSampleSpread(3990.0f, 900.0f, kSwitchedMaxMv, t);   // the bad window
    t += 30000;
    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_TRUE(battHardwarePresent());
    TEST_ASSERT_TRUE(mv > 0.0f);

    for(int i = 0; i < 10; i++, t += 30000)
        mv = battFeedSampleSpread(3990.0f, 30.0f, kSwitchedMaxMv, t);
    TEST_ASSERT_TRUE(battDetected());
    TEST_ASSERT_TRUE(battHardwarePresent());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 3990.0f, mv);
}

// battFeedSample() is battFeedSampleSpread(..., BATT_DETECT_SPREAD_NONE, ...): identical results.
static void test_batt03_feedsample_delegiert_ohne_spread(void)
{
    float a[10], b[10];
    for(int i = 0; i < 10; i++)
        a[i] = battFeedSample(3900.0f + 10.0f*i, kSwitchedMaxMv, 1000u*(uint32_t)i);
    battPipelineReset();
    for(int i = 0; i < 10; i++)
        b[i] = battFeedSampleSpread(3900.0f + 10.0f*i, BATT_DETECT_SPREAD_NONE, kSwitchedMaxMv, 1000u*(uint32_t)i);
    for(int i = 0; i < 10; i++)
        TEST_ASSERT_EQUAL_FLOAT(a[i], b[i]);
}


// ----------------------------------------------------- millis() teleportation
// The full sample core (battFeedSample: detector on the raw sample -> dt EMA ->
// "no reading" rules -> settle rule) with the clock put just before the uint32
// wrap, across it, and on big forward hops (test/support/teleport_clock.h).

struct CoreRun
{
    float mv[130];
    float filt[130];
    bool settled[130];
    bool low[130];
};

// 130 samples, 1 s apart, starting at absolute millis() t0: 3250 mV for ten
// samples, then 3200 mV (both plausible, both below a 3300 mV threshold).
static void coreRun(uint32_t t0, CoreRun *r)
{
    battPipelineReset();
    TeleportClock c;
    c.teleport_to(t0);
    for(int i = 0; i < 130; i++)
    {
        r->mv[i] = battFeedSample(i < 10 ? 3250.0f : 3200.0f, kPackMv, c.now_ms);
        r->filt[i] = battFilteredMv();
        r->settled[i] = battSettled();
        r->low[i] = battLowVoltage(3300.0f);
        c.advance(1000);
    }
}

// Across the wrap the whole core must behave exactly as in a run without one,
// and the low-voltage decision may come at no other sample than the one where
// the settle rule (3 tau = 90 s, 8 samples) is met.
static void test_teleport_core_across_wrap_matches_unwrapped_run(void)
{
    static CoreRun ref, w;
    coreRun(100000u, &ref);

    const uint32_t ks[] = {0, 1, 500, 999, 1000, 1001, 45000, 89999, 90000, 119999};
    for(unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        coreRun(TeleportClock::kWrapMinus(ks[ki]), &w);
        int firstSettled = -1, firstLow = -1;
        for(int i = 0; i < 130; i++)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "k=%u i=%d", (unsigned)ks[ki], i);
            TEST_ASSERT_EQUAL_FLOAT_MESSAGE(ref.mv[i], w.mv[i], msg);
            TEST_ASSERT_EQUAL_FLOAT_MESSAGE(ref.filt[i], w.filt[i], msg);
            TEST_ASSERT_EQUAL_MESSAGE(ref.settled[i], w.settled[i], msg);
            TEST_ASSERT_EQUAL_MESSAGE(ref.low[i], w.low[i], msg);
            if(firstSettled < 0 && w.settled[i]) firstSettled = i;
            if(firstLow < 0 && w.low[i]) firstLow = i;
        }
        TEST_ASSERT_EQUAL_INT(90, firstSettled);
        TEST_ASSERT_EQUAL_INT(90, firstLow);
    }
}

// Forward hop of J ms in the middle of a run of healthy samples: the core
// must resume within one sample (see test_teleport_ema_hop_* in
// test_batt_pipeline for the mechanism when J >= 2^31).
static void teleCoreHop(uint32_t J, bool firstMustFold)
{
    battPipelineReset();
    TeleportClock c;
    c.teleport_to(1000);
    for(int i = 0; i < 11; i++)
    {
        battFeedSample(3900.0f, kPackMv, c.now_ms);
        c.advance(1000);
    }
    c.advance(J - 1000u);

    float mv = battFeedSample(3800.0f, kPackMv, c.now_ms);   // the jump sample (100 mV step: no detector trip)
    TEST_ASSERT_TRUE(battDetected());
    if(firstMustFold)
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 3800.0f, mv, "a forward hop is a huge dt: alpha -> 1");

    c.advance(1000);
    mv = battFeedSample(3800.0f, kPackMv, c.now_ms);
    TEST_ASSERT_TRUE_MESSAGE(mv < 3899.0f, "core frozen: reported value did not move within one sample after the hop");

    for(int i = 0; i < 60; i++)
    {
        c.advance(1000);
        mv = battFeedSample(3800.0f, kPackMv, c.now_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE(mv < 3820.0f, "core did not converge after the hop");
    TEST_ASSERT_TRUE(battHardwarePresent());
}

static void test_teleport_core_hop_1h(void)          { teleCoreHop(TeleportClock::kHour, true); }
static void test_teleport_core_hop_2p31_minus_1(void) { teleCoreHop(0x7FFFFFFFu, true); }
static void test_teleport_core_hop_2p31(void)        { teleCoreHop(0x80000000u, false); }
static void test_teleport_core_hop_2p31_plus_1(void) { teleCoreHop(0x80000001u, false); }
static void test_teleport_core_hop_0xFFFFFFFF(void)  { teleCoreHop(0xFFFFFFFFu, false); }   // == one ms BACK

// A hop must not hand a bad first sample the low-voltage decision: seed with a
// sagging 3000 mV, hop 1 h, then a real 3900 mV: value follows at once, but 2
// samples are not "settled" and battLowVoltage() stays false until 8 samples.
static void test_teleport_core_hop_keeps_settle_rule(void)
{
    battPipelineReset();
    TeleportClock c;
    c.teleport_to(TeleportClock::kWrapMinus(500));
    battFeedSample(3000.0f, kPackMv, c.now_ms);
    c.advance(TeleportClock::kHour);
    float mv = battFeedSample(3900.0f, kPackMv, c.now_ms);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 3900.0f, mv);
    TEST_ASSERT_FALSE(battSettled());
    TEST_ASSERT_FALSE(battLowVoltage(4000.0f));   // threshold above the value: only the settle rule can say no
    for(int i = 0; i < 5; i++)
    {
        c.advance(1000);
        battFeedSample(3900.0f, kPackMv, c.now_ms);
        TEST_ASSERT_FALSE(battLowVoltage(4000.0f));
    }
    c.advance(1000);
    battFeedSample(3900.0f, kPackMv, c.now_ms);   // 8th sample
    TEST_ASSERT_TRUE(battSettled());
    TEST_ASSERT_TRUE(battLowVoltage(4000.0f));
}

// Floating pin -> "absent" -> cell returns, all across the wrap: the EMA is
// re-seeded on the first plausible sample and the settle rule restarts THEN:
// exactly 90 s (and 8 samples) after the re-seed, whichever side of the wrap
// the re-seed fell on; never earlier, never carried over from before.
static void test_teleport_core_settle_rule_restarts_across_wrap(void)
{
    const uint32_t ks[] = {5000, 20000, 26000, 30000, 40000, 60000, 100000, 150000};
    for(unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        battPipelineReset();
        TeleportClock c;
        c.teleport_to(TeleportClock::kWrapMinus(ks[ki]));

        for(int i = 0; i < 5; i++, c.advance(1000))
            battFeedSample(3900.0f, kPackMv, c.now_ms);
        TEST_ASSERT_TRUE(battDetected());

        const float swing[] = {3716.0f, 4886.0f};
        float mv = 1.0f;
        for(int i = 0; i <= BATT_DETECT_ABSENT_STREAK + 1; i++, c.advance(1000))
            mv = battFeedSample(swing[i % 2], kPackMv, c.now_ms);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, mv);
        TEST_ASSERT_FALSE(battDetected());

        // stable cell again; the first non-zero report is the re-seed
        uint32_t seedMs = 0;
        bool seeded = false;
        int settledAfterMs = -1;
        for(int i = 0; i < 260 && settledAfterMs < 0; i++, c.advance(1000))
        {
            mv = battFeedSample(3800.0f, kPackMv, c.now_ms);
            if(!seeded && mv > 0.0f)
            {
                seeded = true;
                seedMs = c.now_ms;
                TEST_ASSERT_FLOAT_WITHIN(0.5f, 3800.0f, mv);   // fresh seed, no noise averaged in
            }
            if(seeded && battSettled())
                settledAfterMs = (int)(uint32_t)(c.now_ms - seedMs);
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "k=%u", (unsigned)ks[ki]);
        TEST_ASSERT_TRUE_MESSAGE(seeded, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(90000, settledAfterMs, msg);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_noisy_floating_pin_geht_auf_absent);
    RUN_TEST(test_stabile_zelle_bleibt_present);
    RUN_TEST(test_entlade_rampe_bleibt_present);
    RUN_TEST(test_einzelner_ausreisser_kippt_nicht);
    RUN_TEST(test_absent_streak_grenzwert);
    RUN_TEST(test_present_streak_grenzwert_bei_erholung);
    RUN_TEST(test_reset_stellt_failsafe_present_wieder_her);
    RUN_TEST(test_2s_pack_band_bleibt_present);
    RUN_TEST(test_core_erstes_sample_seedet_ema);
    RUN_TEST(test_core_ema_tau_ist_kadenzunabhaengig);
    RUN_TEST(test_core_sample_zaehler);
    RUN_TEST(test_core_floating_pin_meldet_no_reading);
    RUN_TEST(test_core_akku_zurueck_reseedet_ema);
    RUN_TEST(test_core_unter_1000mv_ist_no_reading);
    RUN_TEST(test_core_mv_to_percent);
    RUN_TEST(test_core_low_voltage_nur_settled);
    RUN_TEST(test_core_schlechtes_erstes_sample_loest_nichts_aus);
    RUN_TEST(test_core_gesunder_akku_nie_low);
    RUN_TEST(test_batt03_no_cell_ohne_spread_bleibt_present_bug);
    RUN_TEST(test_batt03_no_cell_mit_spread_geht_auf_absent);
    RUN_TEST(test_batt03_zelle_mit_kleinem_spread_bleibt_present);
    RUN_TEST(test_batt03_ein_schlechtes_fenster_kippt_nicht);
    RUN_TEST(test_batt03_feedsample_delegiert_ohne_spread);
    RUN_TEST(test_teleport_core_across_wrap_matches_unwrapped_run);
    RUN_TEST(test_teleport_core_hop_1h);
    RUN_TEST(test_teleport_core_hop_2p31_minus_1);
    RUN_TEST(test_teleport_core_hop_2p31);
    RUN_TEST(test_teleport_core_hop_2p31_plus_1);
    RUN_TEST(test_teleport_core_hop_0xFFFFFFFF);
    RUN_TEST(test_teleport_core_hop_keeps_settle_rule);
    RUN_TEST(test_teleport_core_settle_rule_restarts_across_wrap);
    return UNITY_END();
}
