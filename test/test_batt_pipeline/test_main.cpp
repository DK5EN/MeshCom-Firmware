// Host tests for the shared battery pipeline (src/batt_pipeline.h), step 2 of
// docs/archive/concept-battery-consolidation-20260923.md:
//   pio test -e native_batt_pipeline -f test_batt_pipeline
//
// Groups: dt-based EMA (dt independence, seed, step response, settle rule),
// Brown block against a verbatim copy of the adc_functions.cpp formula,
// BAT-01 detector (cases copied from test/test_batt_detect), percent curve,
// scheduler.
#include <unity.h>

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "batt_pipeline.h"
#include "../support/teleport_clock.h"

void setUp(void) {}
void tearDown(void) {}

// =========================================================================
// 1. EMA
// =========================================================================

// Feed a constant sample at a fixed cadence until wall time `until_ms`,
// starting from a seed taken at t = 0. Returns the filtered value.
static float emaRunConst(uint32_t cadence_ms, uint32_t until_ms, float seed, float sample,
                         uint32_t t0 = 0)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    battEmaUpdate(&e, seed, t0);
    for (uint32_t t = cadence_ms; t <= until_ms; t += cadence_ms)
        battEmaUpdate(&e, sample, t0 + t);
    return e.value;
}

// The same tau reaches the same value at the same wall time whatever the call
// rate is. For a constant input this is exact (product of (1 - alpha_i) =
// exp(-sum dt / tau)); the neo regression (30 s instead of 500 ms) is exactly
// what this pins.
static void test_ema_dt_independence_constant_input(void)
{
    const uint32_t walls[] = {30000, 60000, 90000};
    for (unsigned w = 0; w < 3; w++)
    {
        const float ref = 4000.0f - 1000.0f * expf(-(float)walls[w] / BATT_EMA_TAU_MS_DEFAULT);
        const float a = emaRunConst(100, walls[w], 3000.0f, 4000.0f);
        const float b = emaRunConst(1000, walls[w], 3000.0f, 4000.0f);
        const float c = emaRunConst(30000, walls[w], 3000.0f, 4000.0f);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, ref, a);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, ref, b);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, ref, c);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, a, c);
    }
}

// A drifting input: 100 ms and 1 s cadence agree within the zero-order-hold
// discretisation error (about slope * dt).
static void test_ema_dt_independence_ramp_input(void)
{
    batt_ema_t a, b;
    battEmaInit(&a, BATT_EMA_TAU_MS_DEFAULT);
    battEmaInit(&b, BATT_EMA_TAU_MS_DEFAULT);
    const float slope = -0.5f;   // mV per second, a fast discharge
    for (uint32_t t = 0; t <= 120000; t += 100)
    {
        const float v = 4000.0f + slope * (float)t / 1000.0f;
        battEmaUpdate(&a, v, t);
        if (t % 1000 == 0) battEmaUpdate(&b, v, t);
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0f, a.value, b.value);
}

static void test_ema_seed_is_first_sample(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    TEST_ASSERT_FALSE(e.seeded);
    const float v = battEmaUpdate(&e, 3873.0f, 123456);
    TEST_ASSERT_TRUE(e.seeded);
    TEST_ASSERT_EQUAL_FLOAT(3873.0f, v);
    TEST_ASSERT_EQUAL_FLOAT(3873.0f, e.value);
    TEST_ASSERT_EQUAL_UINT16(1, e.samples);
    // steady input keeps it there: no ramp, no drift
    for (uint32_t t = 1000; t <= 20000; t += 1000)
        battEmaUpdate(&e, 3873.0f, 123456 + t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3873.0f, e.value);
}

static void test_ema_step_response_63_percent_at_tau(void)
{
    const float cadences[] = {100.0f, 1000.0f, 30000.0f};
    for (int i = 0; i < 3; i++)
    {
        const float v = emaRunConst((uint32_t)cadences[i], 30000, 0.0f, 1000.0f);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f * (1.0f - expf(-1.0f)), v);   // 632.1
    }
}

static void test_ema_dt_zero_or_negative_is_no_update(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    battEmaStep(&e, 4000.0f, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, battEmaAlpha(0, 30000.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, battEmaAlpha(-5, 30000.0f));
    battEmaStep(&e, 1.0f, 0);
    battEmaStep(&e, 1.0f, -100);
    TEST_ASSERT_EQUAL_FLOAT(4000.0f, e.value);
    TEST_ASSERT_EQUAL_UINT16(1, e.samples);

    // same through the millis() flavour: same timestamp, and a clock that went back
    batt_ema_t f;
    battEmaInit(&f, BATT_EMA_TAU_MS_DEFAULT);
    battEmaUpdate(&f, 4000.0f, 5000);
    battEmaUpdate(&f, 1.0f, 5000);
    battEmaUpdate(&f, 1.0f, 4000);
    TEST_ASSERT_EQUAL_FLOAT(4000.0f, f.value);
    TEST_ASSERT_EQUAL_UINT16(1, f.samples);
    TEST_ASSERT_EQUAL_UINT32(5000, f.last_ms);   // a backwards stamp must not move the anchor
}

static void test_ema_huge_dt_converges_to_sample(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    battEmaStep(&e, 3000.0f, 0);
    battEmaStep(&e, 4123.0f, 2000000000);
    TEST_ASSERT_EQUAL_FLOAT(4123.0f, e.value);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, battEmaAlpha(2000000000, 30000.0f));
    // saturating elapsed: no wrap
    battEmaStep(&e, 4123.0f, 2000000000);
    battEmaStep(&e, 4123.0f, 2000000000);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, e.elapsed_ms);
}

static void test_ema_rollover_safe(void)
{
    const uint32_t t0 = 0xFFFFFFFFu - 5000u;
    const float v = emaRunConst(1000, 30000, 0.0f, 1000.0f, t0);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 632.1f, v);
}

static void test_ema_alpha_small_dt_precision(void)
{
    // 1 - expf(-x) for x = 1/30000 loses digits in float; expm1f does not
    const double ref = -expm1(-1.0 / 30000.0);
    TEST_ASSERT_FLOAT_WITHIN((float)ref * 1e-5f, (float)ref, battEmaAlpha(1, 30000.0f));
}

// SETTLED RULE ---------------------------------------------------------------

static void test_settle_needs_3_tau_and_8_samples(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    TEST_ASSERT_FALSE(battEmaSettled(&e));   // not even seeded
    battEmaUpdate(&e, 4000.0f, 0);
    uint32_t t = 0;
    for (; t < 89000; t += 1000)
    {
        battEmaUpdate(&e, 4000.0f, t + 1000);
        if (t + 1000 < 90000) TEST_ASSERT_FALSE(battEmaSettled(&e));
    }
    battEmaUpdate(&e, 4000.0f, 90000);
    TEST_ASSERT_TRUE(battEmaSettled(&e));
}

// A stalled loop that folds in few samples must not count wall time alone.
static void test_settle_needs_enough_samples(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    for (uint32_t i = 0; i < 4; i++) battEmaUpdate(&e, 4000.0f, i * 60000u);   // 180 s, 4 samples
    TEST_ASSERT_FALSE(battEmaSettled(&e));
    for (uint32_t i = 4; i < 8; i++) battEmaUpdate(&e, 4000.0f, i * 60000u);
    TEST_ASSERT_TRUE(battEmaSettled(&e));
}

// The protection that today's fBattMax seed provides: a noisy low FIRST sample
// must not reach the deep-sleep decision.
static void test_low_voltage_blocked_until_settled_bad_first_sample(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    const float thr = 3300.0f, floor_mv = 1000.0f;
    battEmaUpdate(&e, 3000.0f, 0);   // sagging / noisy first sample, seeded as-is
    TEST_ASSERT_TRUE(e.value <= thr);
    TEST_ASSERT_FALSE(battEmaLowVoltage(&e, thr, floor_mv));   // value is low, but not settled

    // the cell is fine: real readings recover the filter before it can settle
    bool everLow = false;
    for (uint32_t t = 1000; t <= 200000; t += 1000)
    {
        battEmaUpdate(&e, 4000.0f, t);
        if (battEmaLowVoltage(&e, thr, floor_mv)) everLow = true;
    }
    TEST_ASSERT_FALSE(everLow);
    TEST_ASSERT_TRUE(e.value > thr);
}

// ...and a genuinely empty cell still gets its deep sleep, once settled.
static void test_low_voltage_fires_once_settled_on_real_empty_cell(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    uint32_t firedAt = 0;
    battEmaUpdate(&e, 3250.0f, 0);
    for (uint32_t t = 1000; t <= 200000 && firedAt == 0; t += 1000)
    {
        battEmaUpdate(&e, 3250.0f, t);
        if (battEmaLowVoltage(&e, 3300.0f, 1000.0f)) firedAt = t;
    }
    TEST_ASSERT_EQUAL_UINT32(90000, firedAt);
}

// USB / no battery (reading below the floor) never counts as "low".
static void test_low_voltage_ignores_below_floor(void)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    battEmaUpdate(&e, 0.0f, 0);
    for (uint32_t t = 1000; t <= 200000; t += 1000) battEmaUpdate(&e, 0.0f, t);
    TEST_ASSERT_TRUE(battEmaSettled(&e));
    TEST_ASSERT_FALSE(battEmaLowVoltage(&e, 3300.0f, 1000.0f));
}

// =========================================================================
// 2. Brown block, bit-identical to adc_functions.cpp
// =========================================================================
//
// VERBATIM copy of the arithmetic in loop_ADCFunctions() (adc_functions.cpp
// ~90-101) with its file-scope float globals. Do not "clean this up": the point
// is that it is the old code.
namespace old_adc
{
float ADCalpha = 0.1;
float ADCexp1 = 0.0;
float ADCexp1pre = 0.0;
float ADCexp12 = 0.0;
float ADCexp12pre = 0.0;
float ADCexp2 = 0.0;
float raw = 0;

void reset()
{
    ADCexp1 = ADCexp1pre = ADCexp12 = ADCexp12pre = ADCexp2 = 0.0;
}

void step()
{
    if (ADCexp1pre==0) {ADCexp1pre = raw;}  //langsamen Start beschleunigen
    if (ADCexp12pre==0) {ADCexp12pre = raw;}

    // Glättung berechnen
    ADCexp1 = ADCalpha * raw + (1.0-ADCalpha) * ADCexp1pre;
    ADCexp12 = ADCalpha * ADCexp1 + (1.0-ADCalpha) * ADCexp12pre;
    ADCexp2 = ((2.0-ADCalpha) * ADCexp1 - ADCexp12) / (1.0-ADCalpha);

    ADCexp1pre = ADCexp1;
    ADCexp12pre = ADCexp12;
}
}  // namespace old_adc

static bool bitsEqual(float a, float b)
{
    return memcmp(&a, &b, sizeof(float)) == 0;
}

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static void runBrownAgainstOld(float alpha, uint32_t seed, int n, bool startWithZeros)
{
    batt_brown_t b;
    battBrownReset(&b);
    old_adc::reset();
    old_adc::ADCalpha = alpha;
    uint32_t rng = seed;
    for (int i = 0; i < n; i++)
    {
        float raw;
        if (startWithZeros && i < 5) raw = 0.0f;                                    // exercises `pre == 0` re-seed
        else if (i % 97 == 50) raw = 0.0f;                                          // a real 0.0 mid-stream
        else raw = 100.0f + (float)(lcg(&rng) % 4096u) * 0.731f + (i % 200 < 100 ? 0.0f : 900.0f);
        old_adc::raw = raw;
        old_adc::step();
        const float e2 = battBrownUpdate(&b, raw, alpha);
        char msg[96];
        snprintf(msg, sizeof msg, "alpha %.3f step %d", alpha, i);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp1, b.exp1), msg);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp12, b.exp12), msg);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp2, b.exp2), msg);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp2, e2), msg);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp1pre, b.exp1pre), msg);
        TEST_ASSERT_TRUE_MESSAGE(bitsEqual(old_adc::ADCexp12pre, b.exp12pre), msg);
    }
}

static void test_brown_bit_identical_to_old_formula(void)
{
    const float alphas[] = {0.1f, 0.001f, 0.999f, 0.5f, 0.25f, 0.333f, 0.05f};
    for (unsigned i = 0; i < sizeof alphas / sizeof alphas[0]; i++)
    {
        runBrownAgainstOld(alphas[i], 12345u + i, 3000, false);
        runBrownAgainstOld(alphas[i], 777u + i, 500, true);
    }
}

// A constant input is a fixed point, and a ramp shows the lag forecast at work
// (exp2 leads exp1). Sanity, not the identity above.
static void test_brown_constant_fixed_point_and_ramp_lead(void)
{
    batt_brown_t b;
    battBrownReset(&b);
    for (int i = 0; i < 200; i++) battBrownUpdate(&b, 1234.0f, 0.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1234.0f, b.exp1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1234.0f, b.exp2);

    battBrownReset(&b);
    for (int i = 0; i < 400; i++) battBrownUpdate(&b, 1000.0f + 10.0f * (float)i, 0.2f);
    TEST_ASSERT_TRUE(b.exp2 > b.exp1);   // forecast leads the lagging first stage
}

// =========================================================================
// 3. BAT-01 detector: cases copied from test/test_batt_detect/test_main.cpp
// =========================================================================

static const float kMinMv = 4200.0f * BATT_DETECT_MIN_BAND_FACTOR;   // 2310
static const float kMaxMv = 4200.0f * BATT_DETECT_MAX_BAND_FACTOR;   // 4830

static batt_detect_state_t g_state;

static void driveToAbsent(void)
{
    const float swing[] = {3716.0f, 4886.0f};
    for (int i = 0; i <= BATT_DETECT_ABSENT_STREAK; i++)
        battDetectUpdate(&g_state, swing[i % 2], kMinMv, kMaxMv);
}

static void test_detect_noisy_floating_pin_geht_auf_absent(void)
{
    battDetectReset(&g_state);
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_ABSENT_STREAK, g_state.implausibleStreak);
}

static void test_detect_stabile_zelle_bleibt_present(void)
{
    battDetectReset(&g_state);
    const float jitter[] = {3900.0f, 3895.0f, 3905.0f, 3898.0f, 3903.0f, 3897.0f};
    bool present = true;
    for (int cycle = 0; cycle < 30; cycle++)
        present = battDetectUpdate(&g_state, jitter[cycle % 6], kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
}

static void test_detect_entlade_rampe_bleibt_present(void)
{
    battDetectReset(&g_state);
    bool present = true;
    float mv = 4100.0f;
    for (int i = 0; i < 160 && mv > 3300.0f; i++, mv -= 5.0f)
        present = battDetectUpdate(&g_state, mv, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
}

static void test_detect_einzelner_ausreisser_kippt_nicht(void)
{
    battDetectReset(&g_state);
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

static void test_detect_absent_streak_grenzwert(void)
{
    battDetectReset(&g_state);
    battDetectUpdate(&g_state, 3716.0f, kMinMv, kMaxMv);   // seed, no streak yet

    bool present = true;
    for (int i = 0; i < BATT_DETECT_ABSENT_STREAK - 1; i++)
        present = battDetectUpdate(&g_state, (i % 2 == 0) ? 4886.0f : 3716.0f, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_ABSENT_STREAK - 1, g_state.implausibleStreak);

    present = battDetectUpdate(&g_state, 3716.0f, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
}

static void test_detect_present_streak_grenzwert_bei_erholung(void)
{
    battDetectReset(&g_state);
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);

    float stableMv = g_state.lastMv;

    bool present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);   // plausibleStreak -> 1
    TEST_ASSERT_FALSE(present);

    for (int i = 1; i < BATT_DETECT_PRESENT_STREAK - 1; i++)
        present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL_INT(BATT_DETECT_PRESENT_STREAK - 1, g_state.plausibleStreak);

    present = battDetectUpdate(&g_state, stableMv, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
}

static void test_detect_reset_stellt_failsafe_present_wieder_her(void)
{
    battDetectReset(&g_state);
    driveToAbsent();
    TEST_ASSERT_FALSE(g_state.present);

    battDetectReset(&g_state);
    TEST_ASSERT_TRUE(g_state.present);
    TEST_ASSERT_FALSE(g_state.haveLast);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
    TEST_ASSERT_EQUAL_INT(0, g_state.plausibleStreak);
}

static void test_detect_2s_pack_band_bleibt_present(void)
{
    battDetectReset(&g_state);
    const float band2sMin = 8200.0f * BATT_DETECT_MIN_BAND_FACTOR;
    const float band2sMax = 8200.0f * BATT_DETECT_MAX_BAND_FACTOR;
    const float jitter[] = {7400.0f, 7390.0f, 7410.0f, 7395.0f};
    bool present = true;
    for (int cycle = 0; cycle < 20; cycle++)
        present = battDetectUpdate(&g_state, jitter[cycle % 4], band2sMin, band2sMax);
    TEST_ASSERT_TRUE(present);
}

// Constants stay what the canonical copy has (behaviour must not move).
// ---- BAT-03 window spread (soak 2026-09-29 Finding 1, measured 2026-10-01 on DK5EN-1) ----
// Heltec V3 without a cell, divider on for 3 s, one read every 100 ms (--battprobe LONG,
// cycle 1, x 4.18 mV/count): the charger-output sawtooth sampled at a random phase, which is
// what one read per 30 s window sees.
static const float kNoCellReadsMv[] = {
    4694, 4368, 4176, 4009, 3850, 3724, 4782, 4585, 4368, 4209, 4030, 3908, 3741, 4811, 4623, 4410,
    4201, 4084, 3917, 3779, 4853, 4640, 4460, 4251, 4109, 3929, 3787, 4874, 4577, 4389, 4226};
static const int kNoCellReadsN = (int)(sizeof(kNoCellReadsMv) / sizeof(kNoCellReadsMv[0]));
// One 8-read window, 2 ms apart: no cell = BURST cycle 1, samples 50-57; with a cell = shaped on
// the measured worst case, a first read after idle ~26 counts above back-to-back reads (PAIR
// windows, 113 mV incl. the first read); spread 113 mV against the 300 mV limit.
static const float kNoCellWindowMv[] = {3917, 4795, 4318, 3862, 4748, 4259, 3858, 4703};
static const float kCellWindowMv[]   = {4050, 3963, 3958, 3958, 3958, 3958, 3937, 3958};

static void test_detect_window_spread_helper(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 937.0f, battWindowSpread(kNoCellWindowMv, 8));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 113.0f, battWindowSpread(kCellWindowMv, 8));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, battWindowSpread(kCellWindowMv, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, battWindowSpread(kCellWindowMv, 0));
}

// The bug as shipped in v4.35v.09.29-neo: single reads of the no-cell sawtooth stay "present".
static void test_detect_nocell_sawtooth_single_read_bleibt_present(void)
{
    battDetectReset(&g_state);
    bool present = true;
    for (int i = 0; i < kNoCellReadsN; i++)
        present = battDetectUpdate(&g_state, kNoCellReadsMv[i], kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
}

// The fix: the same reads with the spread of their window go absent after the absent streak.
static void test_detect_nocell_sawtooth_mit_spread_geht_auf_absent(void)
{
    battDetectReset(&g_state);
    const float spread = battWindowSpread(kNoCellWindowMv, 8);
    bool present = true;
    for (int i = 0; i < BATT_DETECT_ABSENT_STREAK - 1; i++)
        present = battDetectUpdateSpread(&g_state, kNoCellReadsMv[i], spread, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);   // streak hysteresis unchanged
    for (int i = BATT_DETECT_ABSENT_STREAK - 1; i < kNoCellReadsN; i++)
        present = battDetectUpdateSpread(&g_state, kNoCellReadsMv[i], spread, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_EQUAL_INT(kNoCellReadsN, g_state.implausibleStreak);
}

static void test_detect_zelle_mit_spread_bleibt_present(void)
{
    battDetectReset(&g_state);
    const float spread = battWindowSpread(kCellWindowMv, 8);
    bool present = true;
    for (int i = 0; i < 60; i++)
        present = battDetectUpdateSpread(&g_state, kCellWindowMv[0] - (float)(i % 3), spread, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
}

// Plugging a cell in produced ~1.5 s of sawtooth (2026-10-01): one bad window must not flip.
static void test_detect_einzelnes_spread_fenster_kippt_nicht(void)
{
    battDetectReset(&g_state);
    const float ok = battWindowSpread(kCellWindowMv, 8);
    const float bad = battWindowSpread(kNoCellWindowMv, 8);
    battDetectUpdateSpread(&g_state, 3958.0f, ok, kMinMv, kMaxMv);
    bool present = battDetectUpdateSpread(&g_state, 3958.0f, bad, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(1, g_state.implausibleStreak);
    present = battDetectUpdateSpread(&g_state, 3958.0f, ok, kMinMv, kMaxMv);
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);
}

static void test_detect_spread_grenzwert(void)
{
    battDetectReset(&g_state);
    battDetectUpdateSpread(&g_state, 3958.0f, BATT_DETECT_MAX_WINDOW_SPREAD_MV, kMinMv, kMaxMv);
    TEST_ASSERT_EQUAL_INT(0, g_state.implausibleStreak);   // at the limit: plausible
    battDetectUpdateSpread(&g_state, 3958.0f, BATT_DETECT_MAX_WINDOW_SPREAD_MV + 1.0f, kMinMv, kMaxMv);
    TEST_ASSERT_EQUAL_INT(1, g_state.implausibleStreak);
}

// BATT_DETECT_SPREAD_NONE and the legacy entry points behave exactly as before.
static void test_detect_spread_none_ist_legacy(void)
{
    batt_detect_state_t a, b;
    battDetectReset(&a);
    battDetectReset(&b);
    for (int i = 0; i < kNoCellReadsN; i++)
    {
        const bool pa = battDetectUpdate(&a, kNoCellReadsMv[i], kMinMv, kMaxMv);
        const bool pb = battDetectUpdateSpread(&b, kNoCellReadsMv[i], BATT_DETECT_SPREAD_NONE, kMinMv, kMaxMv);
        TEST_ASSERT_EQUAL(pa, pb);
        TEST_ASSERT_EQUAL_INT(a.implausibleStreak, b.implausibleStreak);
        TEST_ASSERT_EQUAL_INT(a.plausibleStreak, b.plausibleStreak);
    }
}

static void test_detect_feed_spread_nutzt_singleton(void)
{
    battDetectGlobalReset();
    const float bad = battWindowSpread(kNoCellWindowMv, 8);
    bool present = true;
    for (int i = 0; i < BATT_DETECT_ABSENT_STREAK; i++)
        present = battDetectFeedSpread(3958.0f, bad, kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_FALSE(battDetected());
    battDetectGlobalReset();
}

static void test_detect_constants_unchanged(void)
{
    TEST_ASSERT_EQUAL_INT(8, BATT_DETECT_WINDOW_READS);
    TEST_ASSERT_EQUAL_INT(2, BATT_DETECT_WINDOW_STEP_MS);
    TEST_ASSERT_EQUAL_FLOAT(300.0f, BATT_DETECT_MAX_WINDOW_SPREAD_MV);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, BATT_DETECT_MAX_DELTA_MV);
    TEST_ASSERT_EQUAL_FLOAT(0.55f, BATT_DETECT_MIN_BAND_FACTOR);
    TEST_ASSERT_EQUAL_FLOAT(1.15f, BATT_DETECT_MAX_BAND_FACTOR);
    TEST_ASSERT_EQUAL_INT(6, BATT_DETECT_ABSENT_STREAK);
    TEST_ASSERT_EQUAL_INT(10, BATT_DETECT_PRESENT_STREAK);
}

// The production entry point: lazy init, fail-safe present, one shared state.
static void test_detect_feed_singleton_lazy_init(void)
{
    TEST_ASSERT_TRUE(battDetected());   // before any sample: fail-safe present
    const float swing[] = {3716.0f, 4886.0f};
    bool present = true;
    for (int i = 0; i <= BATT_DETECT_ABSENT_STREAK; i++)
        present = battDetectFeed(swing[i % 2], kMinMv, kMaxMv);
    TEST_ASSERT_FALSE(present);
    TEST_ASSERT_FALSE(battDetected());
}

// =========================================================================
// 4. Percent curve
// =========================================================================

static void test_percent_knots_1s_and_2s(void)
{
    const float frac[9] = {1.000f, 0.976f, 0.952f, 0.929f, 0.905f, 0.881f, 0.857f, 0.833f, 0.786f};
    const int pct[9] = {100, 90, 75, 60, 40, 20, 10, 5, 0};
    const float maxes[] = {4200.0f, 8400.0f, 4100.0f, 8200.0f};
    for (unsigned m = 0; m < 4; m++)
        for (int k = 0; k < 9; k++)
        {
            // half a percent of a knot's own segment is the rounding budget
            const int got = battPercent(frac[k] * maxes[m], maxes[m]);
            TEST_ASSERT_INT_WITHIN(1, pct[k], got);
        }
}

// Exact 1S values at the millivolt (for the documented knees).
static void test_percent_knees_match_old_curve(void)
{
    // old: 0 % below 0.785 max, 10 % at 0.857 max
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(0.785f * 4200.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(0.700f * 4200.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(10, battPercent(3600.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(10, battPercent(7200.0f, 8400.0f));
    TEST_ASSERT_EQUAL_UINT8(100, battPercent(4200.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(100, battPercent(8400.0f, 8400.0f));
}

static void test_percent_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, battPercent(5000.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(100, battPercent(1e9f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(0.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(-50.0f, 4200.0f));
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(3000.0f, 0.0f));    // no max configured
    TEST_ASSERT_EQUAL_UINT8(0, battPercent(3000.0f, -4200.0f));
}

static void test_percent_monotonic_and_bounded(void)
{
    const float maxes[] = {4200.0f, 4100.0f, 8400.0f, 8200.0f, 4125.0f};
    for (unsigned m = 0; m < 5; m++)
    {
        int prev = 0;
        for (int mv = 0; mv <= (int)(1.2f * maxes[m]); mv++)
        {
            const int p = battPercent((float)mv, maxes[m]);
            TEST_ASSERT_TRUE(p >= 0 && p <= 100);
            TEST_ASSERT_TRUE_MESSAGE(p >= prev, "curve must be monotonic");
            prev = p;
        }
        TEST_ASSERT_EQUAL_INT(100, prev);
    }
}

// 1S and 2S are the same curve on relative scale (+-1 % for rounding at a
// boundary).
static void test_percent_scale_invariant(void)
{
    for (int i = 700; i <= 1100; i++)
    {
        const float f = (float)i / 1000.0f;
        const int a = battPercent(f * 4200.0f, 4200.0f);
        const int b = battPercent(f * 8400.0f, 8400.0f);
        TEST_ASSERT_INT_WITHIN(1, a, b);
    }
}

// The comparison table documented in batt_pipeline.h. "old" and "lin" are
// re-computed here from the two current implementations' formulas.
static int oldCurve(float mv, float mx)   // batt_function_old.cpp mv_to_percent(), uint8_t
{
    if (mv < mx * 0.785F) return 0;
    if (mv < mx * 0.857F) { mv -= mx * 0.785F; return (uint8_t)(mv / 30); }
    if (mv > mx) return 100;
    mv -= mx * 0.857F;
    return (uint8_t)(10 + (mv * 0.15F));
}

static int linCurve(float mv, float mx_mv, float min_v)   // batt_functions.cpp mv_to_percent()
{
    float r = (mv / 1000.0f - min_v) / (mx_mv / 1000.0f - min_v) * 100.0f;
    if (r > 100.0f) r = 100.0f;
    if (r < 0.0f) r = 0.0f;
    return (int)roundf(r);
}

static void test_percent_comparison_table(void)
{
    static const int mv1[] = {4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3400, 3300};
    static const int old1[] = {100, 85, 70, 55, 40, 25, 10, 6, 3, 0};
    static const int new1[] = {100, 90, 75, 60, 40, 20, 10, 5, 3, 0};
    static const int lin1[] = {100, 89, 78, 67, 56, 44, 33, 22, 11, 0};
    for (int i = 0; i < 10; i++)
    {
        TEST_ASSERT_EQUAL_INT(old1[i], oldCurve((float)mv1[i], 4200.0f));
        TEST_ASSERT_EQUAL_INT(new1[i], battPercent((float)mv1[i], 4200.0f));
        TEST_ASSERT_EQUAL_INT(lin1[i], linCurve((float)mv1[i], 4200.0f, 3.3f));
    }
    static const int mv2[] = {8400, 8200, 8000, 7800, 7600, 7400, 7200, 7000, 6800, 6600};
    static const int old2[] = {190, 160, 130, 100, 70, 40, 10, 13, 6, 0};
    static const int new2[] = {100, 90, 75, 60, 40, 20, 10, 5, 3, 0};
    static const int lin2[] = {100, 89, 79, 68, 58, 47, 37, 26, 16, 5};
    for (int i = 0; i < 10; i++)
    {
        TEST_ASSERT_EQUAL_INT(old2[i], oldCurve((float)mv2[i], 8400.0f));
        TEST_ASSERT_EQUAL_INT(new2[i], battPercent((float)mv2[i], 8400.0f));
        TEST_ASSERT_EQUAL_INT(lin2[i], linCurve((float)mv2[i], 8400.0f, 6.5f));
    }
}

// =========================================================================
// 5. Scheduler
// =========================================================================

struct sched_event { uint32_t t; batt_sched_action_t a; };

// Tick every `tick_ms` for `duration_ms` from `t0`; `skip_mod`/`skip_ms` make
// every skip_mod-th tick late by skip_ms (loop busy with TX/RX). Returns the
// number of recorded events; max_gap reports the largest tick spacing.
static int schedRun(batt_sched_profile_t profile, uint32_t t0, uint32_t duration_ms,
                    uint32_t tick_ms, int skip_mod, uint32_t skip_ms,
                    sched_event *out, int cap, uint32_t *max_gap)
{
    batt_sched_t s;
    battSchedInit(&s, profile);
    int n = 0;
    uint32_t elapsed = 0, gap_max = 0;
    int i = 0;
    while (elapsed <= duration_ms)
    {
        const batt_sched_action_t a = battSchedTick(&s, t0 + elapsed);
        if (a != BATT_SCHED_NONE && n < cap) { out[n].t = elapsed; out[n].a = a; n++; }
        uint32_t step = tick_ms;
        if (skip_mod > 0 && (++i % skip_mod) == 0) step += skip_ms;
        if (step > gap_max) gap_max = step;
        elapsed += step;
    }
    if (max_gap) *max_gap = gap_max;
    return n;
}

static void test_sched_fixed_reads_every_second_never_arms(void)
{
    sched_event ev[128];
    const int n = schedRun(BATT_SCHED_PROFILE_FIXED, 0, 59900, 100, 0, 0, ev, 128, NULL);
    TEST_ASSERT_EQUAL_INT(60, n);
    for (int i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, ev[i].a);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)i * 1000u, ev[i].t);
    }
}

static void test_sched_fixed_period_stable_with_late_ticks(void)
{
    // every 7th tick 350 ms late: quantised and irregular, yet no drift
    sched_event ev[512];
    uint32_t gap;
    const int n = schedRun(BATT_SCHED_PROFILE_FIXED, 0, 300000, 100, 7, 350, ev, 512, &gap);
    TEST_ASSERT_EQUAL_UINT32(450, gap);
    // 300 s window: reads at deadlines 0,1000,...,300000 -> 301 (+-1 at the edge)
    TEST_ASSERT_INT_WITHIN(1, 301, n);
    for (int i = 1; i < n; i++)
    {
        const int32_t dt = (int32_t)(ev[i].t - ev[i - 1].t);
        TEST_ASSERT_TRUE(dt >= 1000 - (int32_t)gap && dt <= 1000 + (int32_t)gap);
    }
    // no drift: the i-th read never lags its ideal deadline by more than one gap
    for (int i = 0; i < n; i++)
        TEST_ASSERT_TRUE(ev[i].t >= (uint32_t)i * 1000u && ev[i].t < (uint32_t)i * 1000u + gap);
}

static void test_sched_switched_arm_read_sequence(void)
{
    sched_event ev[64];
    const int n = schedRun(BATT_SCHED_PROFILE_SWITCHED, 0, 119900, 100, 0, 0, ev, 64, NULL);
    TEST_ASSERT_EQUAL_INT(8, n);   // 4 periods x (ARM, READ)
    for (int p = 0; p < 4; p++)
    {
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, ev[2 * p].a);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)p * 30000u, ev[2 * p].t);
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, ev[2 * p + 1].a);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)p * 30000u + 100u, ev[2 * p + 1].t);
    }
}

// On-time of the divider per period <= window + one tick lateness; period
// stable; every ARM is followed by exactly one READ before the next ARM.
static void test_sched_switched_ontime_and_period_with_late_ticks(void)
{
    static sched_event ev[512];
    uint32_t gap;
    const int n = schedRun(BATT_SCHED_PROFILE_SWITCHED, 12345, 3600000, 100, 5, 900, ev, 512, &gap);
    TEST_ASSERT_EQUAL_UINT32(1000, gap);
    // ARM at 0, 30 s, ... 3570 s = 120 periods; a 121st only if a tick lands on 3600 s
    TEST_ASSERT_TRUE(n == 2 * 120 || n == 2 * 121);
    for (int p = 0; p < n / 2; p++)
    {
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, ev[2 * p].a);
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, ev[2 * p + 1].a);
        const uint32_t ontime = ev[2 * p + 1].t - ev[2 * p].t;
        TEST_ASSERT_TRUE(ontime >= BATT_SCHED_SETTLE_MS);
        TEST_ASSERT_TRUE_MESSAGE(ontime <= BATT_SCHED_SETTLE_MS + gap, "divider on-time bound");
        // ARM stays on its grid: never later than one gap past the ideal deadline
        TEST_ASSERT_TRUE(ev[2 * p].t >= (uint32_t)p * 30000u);
        TEST_ASSERT_TRUE(ev[2 * p].t < (uint32_t)p * 30000u + gap);
        if (p > 0)
        {
            const int32_t dt = (int32_t)(ev[2 * p].t - ev[2 * p - 2].t);
            TEST_ASSERT_TRUE(dt >= 30000 - (int32_t)gap && dt <= 30000 + (int32_t)gap);
        }
    }
}

// A late tick READs as soon as it comes and never answers ARM while armed,
// even when the stall is longer than a whole period.
static void test_sched_switched_late_read_never_rearms(void)
{
    batt_sched_t s;
    battSchedInit(&s, BATT_SCHED_PROFILE_SWITCHED);
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, battSchedTick(&s, 1000));
    TEST_ASSERT_TRUE(battSchedArmed(&s));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_NONE, battSchedTick(&s, 1050));   // window not over
    TEST_ASSERT_TRUE(battSchedArmed(&s));
    // loop stalled 45 s: this tick is past a whole period, must READ (not ARM)
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, battSchedTick(&s, 46000));
    TEST_ASSERT_FALSE(battSchedArmed(&s));
    // the next period is due already: ARM on the following tick, then read
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, battSchedTick(&s, 46100));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, battSchedTick(&s, 46200));
    // back on the 30 s grid (31000 was due, 61000 next), no burst
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_NONE, battSchedTick(&s, 46300));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_NONE, battSchedTick(&s, 60900));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, battSchedTick(&s, 61000));
}

// Two or more periods missed: re-anchor instead of a burst of ARMs.
static void test_sched_switched_reanchors_after_long_stall(void)
{
    batt_sched_t s;
    battSchedInit(&s, BATT_SCHED_PROFILE_SWITCHED);
    battSchedTick(&s, 0);                                               // ARM
    battSchedTick(&s, 100);                                             // READ
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, battSchedTick(&s, 100000));   // 3+ periods later
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, battSchedTick(&s, 100100));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_NONE, battSchedTick(&s, 100200));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_NONE, battSchedTick(&s, 129900));
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, battSchedTick(&s, 130000));   // anchored to 100000
}

// millis() rollover: the action sequence, relative to the start, is identical
// to a run far away from the wrap, for both profiles and with late ticks.
static void compareRollover(batt_sched_profile_t profile, uint32_t t0)
{
    static sched_event a[400], b[400];
    const int na = schedRun(profile, 0, 240000, 100, 6, 250, a, 400, NULL);
    const int nb = schedRun(profile, t0, 240000, 100, 6, 250, b, 400, NULL);
    TEST_ASSERT_TRUE(na > 8);
    TEST_ASSERT_EQUAL_INT(na, nb);
    for (int i = 0; i < na; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(a[i].t, b[i].t);
        TEST_ASSERT_EQUAL_INT(a[i].a, b[i].a);
    }
}

static void test_sched_rollover(void)
{
    // start 40 s before the wrap so the run crosses it, and right at the edge
    compareRollover(BATT_SCHED_PROFILE_FIXED, 0xFFFFFFFFu - 40000u);
    compareRollover(BATT_SCHED_PROFILE_SWITCHED, 0xFFFFFFFFu - 40000u);
    compareRollover(BATT_SCHED_PROFILE_FIXED, 0xFFFFFFFFu);
    compareRollover(BATT_SCHED_PROFILE_SWITCHED, 0xFFFFFFFFu - 50u);   // wrap inside the settle window
    compareRollover(BATT_SCHED_PROFILE_SWITCHED, 0xFFFFFFFFu - 30050u);
}

// =========================================================================
// 6. millis() teleportation (test/support/teleport_clock.h)
// =========================================================================
//
// The sampler, the EMA and the settle rule must survive millis() being put
// just before the uint32 wrap, across it, and on big forward hops. Everything
// is unsigned `now - then` except battEmaUpdate(), which reads its dt as
// int32_t: a forward hop of >= 2^31 ms looks like a backwards clock there.

struct SchedEv
{
    uint32_t t;
    batt_sched_action_t a;
};

static const uint32_t kTeleStep = 10;   // ms between ticks (the loop passes faster than the 100 ms tick)

static void schedDrive(batt_sched_t *s, TeleportClock &c, uint64_t span_ms, std::vector<SchedEv> &ev)
{
    c.tick(span_ms, kTeleStep, [&](uint32_t now) {
        batt_sched_action_t a = battSchedTick(s, now);
        if (a != BATT_SCHED_NONE)
        {
            SchedEv e;
            e.t = now;
            e.a = a;
            ev.push_back(e);
        }
    });
}

static bool nearGap(uint32_t gap, uint32_t period)
{
    return gap + kTeleStep > period && gap < period + kTeleStep;   // |gap - period| < one tick
}

// ARM/READ alternate: never two ARMs without a READ between (the divider is
// never armed again while it is armed), never a READ without an ARM before
// it (unless the run starts with the divider already on: startsArmed).
static void schedAssertAlternates(const std::vector<SchedEv> &ev, bool startsArmed)
{
    bool armed = startsArmed;
    for (size_t i = 0; i < ev.size(); i++)
    {
        if (ev[i].a == BATT_SCHED_ARM)
        {
            TEST_ASSERT_FALSE_MESSAGE(armed, "second ARM while armed");
            armed = true;
        }
        else
        {
            TEST_ASSERT_TRUE_MESSAGE(armed, "READ without ARM");
            armed = false;
        }
    }
}

// (a) SWITCHED, divider ON when the clock is teleported: the very next tick
// READs and releases (no second ARM), so the on-time is one tick, not the
// hop; afterwards ARM every 30 s and READ 100 ms after each ARM.
static void teleSwitchedArmedHop(bool absolute, uint32_t param, uint32_t preMs, uint32_t firstReadAfterMs = kTeleStep)
{
    batt_sched_t s;
    battSchedInit(&s, BATT_SCHED_PROFILE_SWITCHED);
    TeleportClock c;
    c.teleport_to(1000);

    std::vector<SchedEv> ev;
    schedDrive(&s, c, kTeleStep, ev);   // the first tick anchors and ARMs
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ev.size());
    TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, ev[0].a);
    if (preMs)
    {
        schedDrive(&s, c, preMs, ev);   // still inside the 100 ms settle window
        TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)ev.size());
    }
    TEST_ASSERT_TRUE(battSchedArmed(&s));

    if (absolute)
        c.teleport_to(param);
    else
        c.advance(param);
    const uint32_t hopNow = c.now_ms;
    ev.clear();

    schedDrive(&s, c, 70000, ev);
    TEST_ASSERT_TRUE(ev.size() >= 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(BATT_SCHED_READ, ev[0].a, "first post-hop event must be READ");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(hopNow + firstReadAfterMs, ev[0].t, "READ must come on the very first tick after the hop");
    schedAssertAlternates(ev, true);

    std::vector<uint32_t> armAt;
    for (size_t i = 1; i < ev.size(); i++)
    {
        if (ev[i].a != BATT_SCHED_ARM)
            continue;
        TEST_ASSERT_TRUE(i + 1 < ev.size());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(BATT_SCHED_SETTLE_MS, (uint32_t)(ev[i + 1].t - ev[i].t), "ARM -> READ on-time");
        armAt.push_back(ev[i].t);
    }
    // the first gap after a hop may be a one-off catch-up to the old grid
    for (size_t i = 2; i < armAt.size(); i++)
        TEST_ASSERT_TRUE_MESSAGE(nearGap((uint32_t)(armAt[i] - armAt[i - 1]), BATT_SCHED_SWITCHED_PERIOD_MS), "ARM period");
    TEST_ASSERT_TRUE_MESSAGE(armAt.size() >= 2, "sampler stalled after the hop");
}

static void test_teleport_sched_switched_armed_hop_to_just_before_wrap(void)
{
    const uint32_t ks[] = {0, 1, 50, 99, 100, 150, 29999, 30000};
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++)
    {
        teleSwitchedArmedHop(true, TeleportClock::kWrapMinus(ks[i]), 0);
        teleSwitchedArmedHop(true, TeleportClock::kWrapMinus(ks[i]), 50);
    }
}

static void test_teleport_sched_switched_armed_hop_1h(void)          { teleSwitchedArmedHop(false, TeleportClock::kHour, 50); }
static void test_teleport_sched_switched_armed_hop_2p31_minus_1(void) { teleSwitchedArmedHop(false, 0x7FFFFFFFu, 50); }
static void test_teleport_sched_switched_armed_hop_2p31(void)        { teleSwitchedArmedHop(false, 0x80000000u, 50); }
static void test_teleport_sched_switched_armed_hop_2p31_plus_1(void) { teleSwitchedArmedHop(false, 0x80000001u, 50); }
// one ms BACK: the clock is 1 ms behind the ARM stamp, so the normal 100 ms settle applies
// (first tick after the hop is at arm - 1 + 10, the READ at the first tick >= arm + 100)
static void test_teleport_sched_switched_armed_hop_2p32_minus_1(void) { teleSwitchedArmedHop(false, 0xFFFFFFFFu, 0, 110); }

// FIXED profile, same hops: first READ within one period, then every
// second, no ARM ever, no burst.
static void teleFixedHop(bool absolute, uint32_t param)
{
    batt_sched_t s;
    battSchedInit(&s, BATT_SCHED_PROFILE_FIXED);
    TeleportClock c;
    c.teleport_to(1000);
    std::vector<SchedEv> ev;
    schedDrive(&s, c, 5000, ev);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)ev.size());   // first tick + every 1000 ms

    if (absolute)
        c.teleport_to(param);
    else
        c.advance(param);
    const uint32_t hopNow = c.now_ms;
    ev.clear();
    schedDrive(&s, c, 20000, ev);

    TEST_ASSERT_TRUE_MESSAGE(ev.size() >= 19, "FIXED sampler stalled/burst after the hop");
    TEST_ASSERT_TRUE_MESSAGE(ev.size() <= 21, "FIXED sampler burst after the hop");
    TEST_ASSERT_TRUE((uint32_t)(ev[0].t - hopNow) <= BATT_SCHED_FIXED_PERIOD_MS + kTeleStep);
    for (size_t i = 0; i < ev.size(); i++)
    {
        TEST_ASSERT_EQUAL_INT(BATT_SCHED_READ, ev[i].a);
        if (i >= 2)   // gap #1 may be a one-off catch-up to the old grid
            TEST_ASSERT_TRUE(nearGap((uint32_t)(ev[i].t - ev[i - 1].t), BATT_SCHED_FIXED_PERIOD_MS));
    }
}

static void test_teleport_sched_fixed_hop_to_just_before_wrap(void)
{
    const uint32_t ks[] = {0, 1, 50, 99, 100, 999, 1000, 1001};
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++)
        teleFixedHop(true, TeleportClock::kWrapMinus(ks[i]));
}
static void test_teleport_sched_fixed_hop_1h(void)          { teleFixedHop(false, TeleportClock::kHour); }
static void test_teleport_sched_fixed_hop_2p31_minus_1(void) { teleFixedHop(false, 0x7FFFFFFFu); }
static void test_teleport_sched_fixed_hop_2p31(void)        { teleFixedHop(false, 0x80000000u); }
static void test_teleport_sched_fixed_hop_2p31_plus_1(void) { teleFixedHop(false, 0x80000001u); }
static void test_teleport_sched_fixed_hop_2p32_minus_1(void) { teleFixedHop(false, 0xFFFFFFFFu); }

// Continuous run through the wrap (no hop), the clock parked k ms before it:
// exact cadence on both profiles, READ exactly 100 ms after ARM.
static void test_teleport_sched_across_wrap_exact_cadence(void)
{
    const uint32_t ks[] = {0, 1, 50, 99, 100, 101, 999, 1000, 29999, 30000, 30050, 60000};
    for (unsigned i = 0; i < sizeof(ks) / sizeof(ks[0]); i++)
    {
        {   // FIXED
            batt_sched_t s;
            battSchedInit(&s, BATT_SCHED_PROFILE_FIXED);
            TeleportClock c;
            c.teleport_to(TeleportClock::kWrapMinus(ks[i]));
            std::vector<SchedEv> ev;
            schedDrive(&s, c, 100000, ev);
            TEST_ASSERT_EQUAL_UINT32(100, (uint32_t)ev.size());
            TEST_ASSERT_EQUAL_UINT32(TeleportClock::kWrapMinus(ks[i]) + kTeleStep, ev[0].t);
            for (size_t j = 1; j < ev.size(); j++)
                TEST_ASSERT_EQUAL_UINT32(1000, (uint32_t)(ev[j].t - ev[j - 1].t));
        }
        {   // SWITCHED
            batt_sched_t s;
            battSchedInit(&s, BATT_SCHED_PROFILE_SWITCHED);
            TeleportClock c;
            c.teleport_to(TeleportClock::kWrapMinus(ks[i]));
            std::vector<SchedEv> ev;
            schedDrive(&s, c, 100000, ev);
            schedAssertAlternates(ev, false);
            TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)ev.size());   // ARM+READ at 10, 30010, 60010, 90010
            for (size_t j = 0; j < ev.size(); j += 2)
            {
                TEST_ASSERT_EQUAL_INT(BATT_SCHED_ARM, ev[j].a);
                TEST_ASSERT_EQUAL_UINT32(BATT_SCHED_SETTLE_MS, (uint32_t)(ev[j + 1].t - ev[j].t));
                if (j >= 2)
                    TEST_ASSERT_EQUAL_UINT32(30000, (uint32_t)(ev[j].t - ev[j - 2].t));
            }
        }
    }
}

// (b) EMA, seed-to-settled across the wrap == the same run without a wrap,
// sample by sample (same dt every step, so bit-identical), and the settle
// rule flips on the same sample (index 90: 90 s = 3 tau, >= 8 samples).
static void test_teleport_ema_across_wrap_matches_unwrapped_run(void)
{
    const uint32_t ks[] = {0, 1, 500, 999, 1000, 1001, 45000, 89999, 90000, 119999};
    for (unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        batt_ema_t w, r;
        battEmaInit(&w, BATT_EMA_TAU_MS_DEFAULT);
        battEmaInit(&r, BATT_EMA_TAU_MS_DEFAULT);
        TeleportClock c;
        c.teleport_to(TeleportClock::kWrapMinus(ks[ki]));
        int firstSettledW = -1, firstSettledR = -1;
        for (int i = 0; i < 130; i++)
        {
            const float smp = (i < 10) ? 3000.0f : 3400.0f;
            battEmaUpdate(&w, smp, c.now_ms);
            battEmaUpdate(&r, smp, 100000u + 1000u * (uint32_t)i);
            char msg[64];
            snprintf(msg, sizeof(msg), "k=%u i=%d", (unsigned)ks[ki], i);
            TEST_ASSERT_EQUAL_FLOAT_MESSAGE(r.value, w.value, msg);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(r.elapsed_ms, w.elapsed_ms, msg);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(r.samples, w.samples, msg);
            if (firstSettledW < 0 && battEmaSettled(&w)) firstSettledW = i;
            if (firstSettledR < 0 && battEmaSettled(&r)) firstSettledR = i;
            c.advance(1000);
        }
        TEST_ASSERT_EQUAL_INT(90, firstSettledR);
        TEST_ASSERT_EQUAL_INT(90, firstSettledW);
    }
}

// Low-voltage decision across the wrap: a genuinely empty cell (3250 mV at a
// 3300 mV threshold) becomes "low" at the same sample as without a wrap and
// not one sample earlier.
static void test_teleport_ema_low_voltage_across_wrap(void)
{
    const uint32_t ks[] = {0, 1, 30000, 45000, 89999};
    for (unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        batt_ema_t w;
        battEmaInit(&w, BATT_EMA_TAU_MS_DEFAULT);
        TeleportClock c;
        c.teleport_to(TeleportClock::kWrapMinus(ks[ki]));
        int firstLow = -1;
        for (int i = 0; i < 130; i++)
        {
            battEmaUpdate(&w, 3250.0f, c.now_ms);
            if (firstLow < 0 && battEmaLowVoltage(&w, 3300.0f, 1000.0f)) firstLow = i;
            c.advance(1000);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(90, firstLow, "low-voltage must wait exactly for the settle rule");
    }
}

// Forward hop of J ms mid-run: the filter must resume within ONE sample after
// ANY jump. A hop >= 2^31 ms reads as NEGATIVE in battEmaUpdate()'s
// `(int32_t)(now_ms - s->last_ms)`, is dropped like a backwards clock, and
// last_ms is never moved: the filter then stays frozen until now - last_ms
// wraps past 2^32 again (~24.8 days later for a hop of 2^31 + 1).
static void teleEmaHop(uint32_t J, bool firstMustFold)
{
    batt_ema_t e;
    battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
    TeleportClock c;
    c.teleport_to(1000);
    for (int i = 0; i < 11; i++)
    {
        battEmaUpdate(&e, 3000.0f, c.now_ms);
        c.advance(1000);
    }
    c.advance(J - 1000u);   // J counted from the last sample
    const uint16_t before = e.samples;

    battEmaUpdate(&e, 4000.0f, c.now_ms);   // the jump sample
    if (firstMustFold)
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 4000.0f, e.value, "a forward hop is a huge dt: alpha -> 1");

    c.advance(1000);
    battEmaUpdate(&e, 4000.0f, c.now_ms);   // one sample later
    TEST_ASSERT_TRUE_MESSAGE(e.samples > before, "filter frozen: no sample folded within one sample after the hop");
    TEST_ASSERT_TRUE_MESSAGE(e.value > 3010.0f, "filter frozen: value did not move within one sample after the hop");

    for (int i = 0; i < 60; i++)            // and it converges on the new level
    {
        c.advance(1000);
        battEmaUpdate(&e, 4000.0f, c.now_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE(e.value > 3800.0f, "filter did not converge after the hop");
}

static void test_teleport_ema_hop_1h(void)          { teleEmaHop(TeleportClock::kHour, true); }
static void test_teleport_ema_hop_2p31_minus_1(void) { teleEmaHop(0x7FFFFFFFu, true); }
static void test_teleport_ema_hop_2p31(void)        { teleEmaHop(0x80000000u, false); }
static void test_teleport_ema_hop_2p31_plus_1(void) { teleEmaHop(0x80000001u, false); }
static void test_teleport_ema_hop_0xFFFFFFFF(void)  { teleEmaHop(0xFFFFFFFFu, false); }   // == one ms BACK

// Wall time a hop adds must not shortcut the settle rule: a bad first sample,
// then a 1 h hop: the very next sample folds (the hop is a gap) but the
// filter is not settled on 2 samples, so no low-voltage decision can come
// out of it; after 8 samples the healthy 3900 mV is not low, a real 3250 mV is.
static void test_teleport_ema_hop_does_not_shortcut_settle_rule(void)
{
    for (int pass = 0; pass < 2; pass++)
    {
        const float level = pass == 0 ? 3900.0f : 3250.0f;
        batt_ema_t e;
        battEmaInit(&e, BATT_EMA_TAU_MS_DEFAULT);
        TeleportClock c;
        c.teleport_to(TeleportClock::kWrapMinus(500));
        battEmaUpdate(&e, 3000.0f, c.now_ms);   // bad first sample
        c.advance(TeleportClock::kHour);
        battEmaUpdate(&e, level, c.now_ms);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, level, e.value);
        TEST_ASSERT_FALSE(battEmaSettled(&e));
        TEST_ASSERT_FALSE(battEmaLowVoltage(&e, 3300.0f, 1000.0f));
        for (int i = 0; i < 5; i++)
        {
            c.advance(1000);
            battEmaUpdate(&e, level, c.now_ms);
            TEST_ASSERT_FALSE(battEmaSettled(&e));   // 7 samples
        }
        c.advance(1000);
        battEmaUpdate(&e, level, c.now_ms);           // 8th sample
        TEST_ASSERT_TRUE(battEmaSettled(&e));
        TEST_ASSERT_EQUAL(pass == 1, battEmaLowVoltage(&e, 3300.0f, 1000.0f));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    // EMA
    RUN_TEST(test_ema_dt_independence_constant_input);
    RUN_TEST(test_ema_dt_independence_ramp_input);
    RUN_TEST(test_ema_seed_is_first_sample);
    RUN_TEST(test_ema_step_response_63_percent_at_tau);
    RUN_TEST(test_ema_dt_zero_or_negative_is_no_update);
    RUN_TEST(test_ema_huge_dt_converges_to_sample);
    RUN_TEST(test_ema_rollover_safe);
    RUN_TEST(test_ema_alpha_small_dt_precision);
    RUN_TEST(test_settle_needs_3_tau_and_8_samples);
    RUN_TEST(test_settle_needs_enough_samples);
    RUN_TEST(test_low_voltage_blocked_until_settled_bad_first_sample);
    RUN_TEST(test_low_voltage_fires_once_settled_on_real_empty_cell);
    RUN_TEST(test_low_voltage_ignores_below_floor);
    // Brown
    RUN_TEST(test_brown_bit_identical_to_old_formula);
    RUN_TEST(test_brown_constant_fixed_point_and_ramp_lead);
    // detector
    RUN_TEST(test_detect_noisy_floating_pin_geht_auf_absent);
    RUN_TEST(test_detect_stabile_zelle_bleibt_present);
    RUN_TEST(test_detect_entlade_rampe_bleibt_present);
    RUN_TEST(test_detect_einzelner_ausreisser_kippt_nicht);
    RUN_TEST(test_detect_absent_streak_grenzwert);
    RUN_TEST(test_detect_present_streak_grenzwert_bei_erholung);
    RUN_TEST(test_detect_reset_stellt_failsafe_present_wieder_her);
    RUN_TEST(test_detect_2s_pack_band_bleibt_present);
    RUN_TEST(test_detect_window_spread_helper);
    RUN_TEST(test_detect_nocell_sawtooth_single_read_bleibt_present);
    RUN_TEST(test_detect_nocell_sawtooth_mit_spread_geht_auf_absent);
    RUN_TEST(test_detect_zelle_mit_spread_bleibt_present);
    RUN_TEST(test_detect_einzelnes_spread_fenster_kippt_nicht);
    RUN_TEST(test_detect_spread_grenzwert);
    RUN_TEST(test_detect_spread_none_ist_legacy);
    RUN_TEST(test_detect_feed_spread_nutzt_singleton);
    RUN_TEST(test_detect_constants_unchanged);
    RUN_TEST(test_detect_feed_singleton_lazy_init);
    // percent
    RUN_TEST(test_percent_knots_1s_and_2s);
    RUN_TEST(test_percent_knees_match_old_curve);
    RUN_TEST(test_percent_clamps);
    RUN_TEST(test_percent_monotonic_and_bounded);
    RUN_TEST(test_percent_scale_invariant);
    RUN_TEST(test_percent_comparison_table);
    // scheduler
    RUN_TEST(test_sched_fixed_reads_every_second_never_arms);
    RUN_TEST(test_sched_fixed_period_stable_with_late_ticks);
    RUN_TEST(test_sched_switched_arm_read_sequence);
    RUN_TEST(test_sched_switched_ontime_and_period_with_late_ticks);
    RUN_TEST(test_sched_switched_late_read_never_rearms);
    RUN_TEST(test_sched_switched_reanchors_after_long_stall);
    RUN_TEST(test_sched_rollover);
    RUN_TEST(test_teleport_sched_switched_armed_hop_to_just_before_wrap);
    RUN_TEST(test_teleport_sched_switched_armed_hop_1h);
    RUN_TEST(test_teleport_sched_switched_armed_hop_2p31_minus_1);
    RUN_TEST(test_teleport_sched_switched_armed_hop_2p31);
    RUN_TEST(test_teleport_sched_switched_armed_hop_2p31_plus_1);
    RUN_TEST(test_teleport_sched_switched_armed_hop_2p32_minus_1);
    RUN_TEST(test_teleport_sched_fixed_hop_to_just_before_wrap);
    RUN_TEST(test_teleport_sched_fixed_hop_1h);
    RUN_TEST(test_teleport_sched_fixed_hop_2p31_minus_1);
    RUN_TEST(test_teleport_sched_fixed_hop_2p31);
    RUN_TEST(test_teleport_sched_fixed_hop_2p31_plus_1);
    RUN_TEST(test_teleport_sched_fixed_hop_2p32_minus_1);
    RUN_TEST(test_teleport_sched_across_wrap_exact_cadence);
    RUN_TEST(test_teleport_ema_across_wrap_matches_unwrapped_run);
    RUN_TEST(test_teleport_ema_low_voltage_across_wrap);
    RUN_TEST(test_teleport_ema_hop_1h);
    RUN_TEST(test_teleport_ema_hop_2p31_minus_1);
    RUN_TEST(test_teleport_ema_hop_2p31);
    RUN_TEST(test_teleport_ema_hop_2p31_plus_1);
    RUN_TEST(test_teleport_ema_hop_0xFFFFFFFF);
    RUN_TEST(test_teleport_ema_hop_does_not_shortcut_settle_rule);
    return UNITY_END();
}
