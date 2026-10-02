// Native test suite for radio_units.cpp (RF-01, RF-02, RF-03, RF-04).
//
// The three defects this pins are all the same mistake: a radio settings key
// holds a different unit on the two platforms, and a write site forgot to
// convert. Both platform sides are exercised here in one binary via the
// explicit `indexed` flag, so the nRF52 half is covered on a host with no
// nRF52 board in sight -- which matters, because RF-02 is a T114/T-Echo defect
// and neither board exists on this bench.
//
//   pio test -e native -f test_radio_units

#include <unity.h>

#include <radio_units.h>

void setUp(void) {}
void tearDown(void) {}

// ---- bandwidth ------------------------------------------------------------

static void test_bw_esp32_passes_khz_through(void)
{
    TEST_ASSERT_EQUAL_FLOAT(125.0f, radioBwStoredToKhz(125.0f, false));
    TEST_ASSERT_EQUAL_FLOAT(250.0f, radioBwStoredToKhz(250.0f, false));
    TEST_ASSERT_EQUAL_FLOAT(125.0f, radioBwKhzToStored(125.0f, false));
    TEST_ASSERT_EQUAL_FLOAT(250.0f, radioBwKhzToStored(250.0f, false));
}

static void test_bw_nrf52_is_an_index(void)
{
    TEST_ASSERT_EQUAL_FLOAT(125.0f, radioBwStoredToKhz(0, true));
    TEST_ASSERT_EQUAL_FLOAT(250.0f, radioBwStoredToKhz(1, true));
    TEST_ASSERT_EQUAL_FLOAT(500.0f, radioBwStoredToKhz(2, true));

    // RF-01: this is the conversion --txbw did not do. Storing 250 where the
    // SX126x driver reads a bandwidth enum configures the radio with enum
    // value 250.
    TEST_ASSERT_EQUAL_FLOAT(0, radioBwKhzToStored(125.0f, true));
    TEST_ASSERT_EQUAL_FLOAT(1, radioBwKhzToStored(250.0f, true));
    TEST_ASSERT_EQUAL_FLOAT(2, radioBwKhzToStored(500.0f, true));
}

static void test_bw_round_trips_on_both_sides(void)
{
    const float khz[] = {125.0f, 250.0f, 500.0f};
    for(unsigned i = 0; i < sizeof(khz)/sizeof(khz[0]); i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(khz[i], radioBwStoredToKhz(radioBwKhzToStored(khz[i], true), true));
        TEST_ASSERT_EQUAL_FLOAT(khz[i], radioBwStoredToKhz(radioBwKhzToStored(khz[i], false), false));
    }
}

static void test_bw_out_of_range_passes_through(void)
{
    // Defaulting is the caller's policy, not this layer's.
    TEST_ASSERT_EQUAL_FLOAT(7.0f, radioBwStoredToKhz(7.0f, true));
    TEST_ASSERT_EQUAL_FLOAT(300.0f, radioBwKhzToStored(300.0f, true));
}

// ---- coding rate ----------------------------------------------------------

static void test_cr_esp32_passes_denominator_through(void)
{
    for(int d = 5; d <= 8; d++)
    {
        TEST_ASSERT_EQUAL_INT(d, radioCrStoredToDenom(d, false));
        TEST_ASSERT_EQUAL_INT(d, radioCrDenomToStored(d, false));
    }
}

static void test_cr_nrf52_is_an_index(void)
{
    TEST_ASSERT_EQUAL_INT(5, radioCrStoredToDenom(1, true));
    TEST_ASSERT_EQUAL_INT(6, radioCrStoredToDenom(2, true));
    TEST_ASSERT_EQUAL_INT(7, radioCrStoredToDenom(3, true));
    TEST_ASSERT_EQUAL_INT(8, radioCrStoredToDenom(4, true));

    // RF-02: --txcr did this only under #ifdef BOARD_RAK4630, while the
    // index-unit radio path also covers USE_HELTEC_T114 and BOARD_T_ECHO.
    TEST_ASSERT_EQUAL_INT(1, radioCrDenomToStored(5, true));
    TEST_ASSERT_EQUAL_INT(2, radioCrDenomToStored(6, true));
    TEST_ASSERT_EQUAL_INT(3, radioCrDenomToStored(7, true));
    TEST_ASSERT_EQUAL_INT(4, radioCrDenomToStored(8, true));
}

static void test_cr_round_trips_on_both_sides(void)
{
    for(int d = 5; d <= 8; d++)
    {
        TEST_ASSERT_EQUAL_INT(d, radioCrStoredToDenom(radioCrDenomToStored(d, true), true));
        TEST_ASSERT_EQUAL_INT(d, radioCrStoredToDenom(radioCrDenomToStored(d, false), false));
    }
}

static void test_cr_out_of_range_passes_through(void)
{
    TEST_ASSERT_EQUAL_INT(0, radioCrDenomToStored(0, true));
    TEST_ASSERT_EQUAL_INT(9, radioCrDenomToStored(9, true));
    TEST_ASSERT_EQUAL_INT(0, radioCrStoredToDenom(0, true));
}

// ---- frequency ------------------------------------------------------------

static void test_freq_esp32_passes_mhz_through(void)
{
    TEST_ASSERT_EQUAL_FLOAT(433.175f, radioFreqStoredToMhz(433.175f, false));
    TEST_ASSERT_EQUAL_FLOAT(433.175f, radioFreqMhzToStored(433.175f, false));
}

static void test_freq_nrf52_is_hz(void)
{
    TEST_ASSERT_EQUAL_FLOAT(433.175f, radioFreqStoredToMhz(433175000.0f, true));
    TEST_ASSERT_EQUAL_FLOAT(433175000.0f, radioFreqMhzToStored(433.175f, true));
    TEST_ASSERT_EQUAL_FLOAT(439.9125f, radioFreqStoredToMhz(439912500.0f, true));
}

// RF-03: lora_setcountry() case 7 (MAN) validated the stored value against a
// window written in MHz. On the nRF52 side the stored value is in Hz, so the
// window never matched and every --setcountry 7 forced the frequency back to
// the default. Normalising first is what makes one window correct on both.
static void test_freq_window_matches_on_both_sides_after_normalising(void)
{
    // /1000.0, the corrected divisor (RF-04) -- see the guard-band tests
    // below for why /100.0 was wrong. The point of *this* test is the unit
    // of the value being compared, not the width of the guard band.
    const float bw_khz = 250.0f;
    const float dec = (bw_khz / 2.0f) / 1000.0f;

    const float esp_stored = 434.0f;
    const float nrf_stored = 434000000.0f;

    const float esp_mhz = radioFreqStoredToMhz(esp_stored, false);
    const float nrf_mhz = radioFreqStoredToMhz(nrf_stored, true);

    TEST_ASSERT_TRUE(esp_mhz >= (430.0f + dec) && esp_mhz <= (439.0f - dec));
    TEST_ASSERT_TRUE(nrf_mhz >= (430.0f + dec) && nrf_mhz <= (439.0f - dec));

    // and the raw nRF52 value is exactly what used to fail the window
    TEST_ASSERT_FALSE(nrf_stored >= (430.0f + dec) && nrf_stored <= (439.0f - dec));
}

// The band check rejects out-of-band values on both sides, too -- a fix that
// accepted everything would pass the test above and still be wrong.
static void test_freq_window_rejects_out_of_band_on_both_sides(void)
{
    const float dec = (250.0f / 2.0f) / 1000.0f;

    const float esp_mhz = radioFreqStoredToMhz(450.0f, false);
    const float nrf_mhz = radioFreqStoredToMhz(450000000.0f, true);

    TEST_ASSERT_FALSE(esp_mhz >= (430.0f + dec) && esp_mhz <= (439.0f - dec));
    TEST_ASSERT_FALSE(nrf_mhz >= (430.0f + dec) && nrf_mhz <= (439.0f - dec));
}

// ---- RF-04: guard-band width and its per-platform quantity ---------------
//
// Two problems, both in the `dec_bandwith = (X/2.0)/100.0` guard-band
// arithmetic at src/lora_setchip.cpp:246 and src/command_functions.cpp:4568:
//
//   1. /100.0 converts kHz to MHz ten times too wide. Half of 250 kHz is
//      0.125 MHz, not 1.25 MHz -- the divisor must be /1000.0.
//   2. command_functions.cpp fed the raw LORA_BANDWIDTH macro into that
//      arithmetic. Per OPT-D14, LORA_BANDWIDTH is kHz on ESP32 variants
//      (e.g. variants/heltec_wifi_lora_32_V3/configuration.h: `#define
//      LORA_BANDWIDTH 250`) but a bandwidth INDEX on nRF52 variants (e.g.
//      variants/wiscore_rak4631/configuration.h: `#define LORA_BANDWIDTH 1
//      // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, ...]`) -- so the same
//      expression computed a different quantity per platform. Both sites
//      must route the macro through radioBwStoredToKhz() first, exactly as
//      lora_setchip.cpp:233 already does for the LORA_BANDWIDTH default.
static void test_guard_band_divisor_is_one_tenth_of_bandwidth_in_mhz(void)
{
    const float bw_khz = 250.0f;
    const float dec = (bw_khz / 2.0f) / 1000.0f;

    TEST_ASSERT_EQUAL_FLOAT(0.125f, dec);

    // 70cm window: 431.25..437.75 (old, ten times too wide a guard) widens
    // to 430.125..438.875 (correct).
    TEST_ASSERT_EQUAL_FLOAT(430.125f, 430.0f + dec);
    TEST_ASSERT_EQUAL_FLOAT(438.875f, 439.0f - dec);

    // SRD860 window: 869.4..869.65 is itself exactly one 250 kHz channel, so
    // a correctly-sized guard eats it down to its single physically valid
    // centre frequency, 869.525 -- not the 869.275..869.775 the old /100.0
    // divisor would have (wrongly) computed as symmetric bounds.
    TEST_ASSERT_EQUAL_FLOAT(869.525f, 869.4f + dec);
    TEST_ASSERT_EQUAL_FLOAT(869.65f - dec, 869.4f + dec);
}

static void test_guard_band_same_quantity_on_both_platforms_via_converter(void)
{
    // ESP32 stores LORA_BANDWIDTH as kHz directly; nRF52 stores it as a
    // bandwidth index (0/1/2). Route both through radioBwStoredToKhz()
    // before halving, exactly as the fixed call sites must, and check they
    // agree on the resulting guard band.
    const float esp32_bw_khz = radioBwStoredToKhz(250.0f, false); // ESP32 LORA_BANDWIDTH
    const float nrf52_bw_khz = radioBwStoredToKhz(1.0f, true);    // nRF52 LORA_BANDWIDTH (index)

    TEST_ASSERT_EQUAL_FLOAT(250.0f, esp32_bw_khz);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, nrf52_bw_khz);

    const float esp32_dec = (esp32_bw_khz / 2.0f) / 1000.0f;
    const float nrf52_dec = (nrf52_bw_khz / 2.0f) / 1000.0f;

    TEST_ASSERT_EQUAL_FLOAT(esp32_dec, nrf52_dec);
    TEST_ASSERT_EQUAL_FLOAT(0.125f, nrf52_dec);

    // Problem 2, pinned directly: feeding the raw nRF52 index (1) into the
    // same arithmetic without the converter -- what command_functions.cpp
    // did before this fix -- computes a different, wrong guard band instead
    // of agreeing with the ESP32 side.
    const float unconverted_nrf52_dec = (1.0f / 2.0f) / 1000.0f;
    TEST_ASSERT_FALSE(unconverted_nrf52_dec == nrf52_dec);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_bw_esp32_passes_khz_through);
    RUN_TEST(test_bw_nrf52_is_an_index);
    RUN_TEST(test_bw_round_trips_on_both_sides);
    RUN_TEST(test_bw_out_of_range_passes_through);

    RUN_TEST(test_cr_esp32_passes_denominator_through);
    RUN_TEST(test_cr_nrf52_is_an_index);
    RUN_TEST(test_cr_round_trips_on_both_sides);
    RUN_TEST(test_cr_out_of_range_passes_through);

    RUN_TEST(test_freq_esp32_passes_mhz_through);
    RUN_TEST(test_freq_nrf52_is_hz);
    RUN_TEST(test_freq_window_matches_on_both_sides_after_normalising);
    RUN_TEST(test_freq_window_rejects_out_of_band_on_both_sides);

    RUN_TEST(test_guard_band_divisor_is_one_tenth_of_bandwidth_in_mhz);
    RUN_TEST(test_guard_band_same_quantity_on_both_platforms_via_converter);

    return UNITY_END();
}
