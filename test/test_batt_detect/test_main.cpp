// BAT-01 (BACKLOG.md sec. 3.8n): Heltec V3's battery % jumped wildly with no battery
// attached -- the floating VBAT divider node reads pure ADC noise (measured on-device,
// TM-38: 844 samples over 16 min, raw readings 3716-4886 mV, up to 1.17 V sample-to-sample
// at the 500 ms read_batt() cadence). battDetectUpdate()/battDetectReset() (batt_functions.h)
// are the pure decision core that turns that signature into a hysteresis-gated
// present/absent verdict -- no Arduino calls, so they run natively against synthetic mV
// series here instead of against real hardware.
#include <unity.h>

#include "batt_functions.h"

// Single-cell Li-Ion band used throughout (matches the T-Deck/T-Deck Plus/E213/E290/
// wireless-paper boards this detector actually ships on -- fBattMax ~4.2 V): the same
// band read_batt() derives from fBattMax*BATT_DETECT_{MIN,MAX}_BAND_FACTOR.
static const float kMinMv = 4200.0f * BATT_DETECT_MIN_BAND_FACTOR;   // 2310
static const float kMaxMv = 4200.0f * BATT_DETECT_MAX_BAND_FACTOR;   // 4830

static batt_detect_state_t g_state;

void setUp(void)
{
    battDetectReset(&g_state);
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
    return UNITY_END();
}
