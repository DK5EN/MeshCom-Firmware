// Native test suite for radio_units.cpp (RF-01, RF-02, RF-03).
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
    // /100.0 as the shipping code computes it, not the physically correct
    // /1000.0 -- see RF-04. The point of this test is the unit of the value
    // being compared, not the width of the guard band.
    const float bw_khz = 250.0f;
    const float dec = (bw_khz / 2.0f) / 100.0f;

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
    const float dec = (250.0f / 2.0f) / 100.0f;

    const float esp_mhz = radioFreqStoredToMhz(450.0f, false);
    const float nrf_mhz = radioFreqStoredToMhz(450000000.0f, true);

    TEST_ASSERT_FALSE(esp_mhz >= (430.0f + dec) && esp_mhz <= (439.0f - dec));
    TEST_ASSERT_FALSE(nrf_mhz >= (430.0f + dec) && nrf_mhz <= (439.0f - dec));
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

    return UNITY_END();
}
