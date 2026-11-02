// WX-01 (upstream #999): window mean, 3 s gust and pulse -> km/h conversion (src/wx_avg.h).
#include <unity.h>
#include "wx_avg.h"

static wx_mean_t m;
static wx_gust_t g;

void setUp(void) { wxMeanReset(&m); wxGustReset(&g); }
void tearDown(void) {}

static void test_mean_known_samples(void)
{
    float v = -1;
    wxMeanAdd(&m, 2.0f); wxMeanAdd(&m, 4.0f); wxMeanAdd(&m, 9.0f);
    TEST_ASSERT_TRUE(wxMeanGet(&m, &v));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, v);
    TEST_ASSERT_EQUAL_UINT32(3, wxMeanCount(&m));
}

static void test_mean_empty_is_no_value(void)
{
    float v = 123.0f;
    TEST_ASSERT_FALSE(wxMeanGet(&m, &v));
    TEST_ASSERT_EQUAL_UINT32(0, wxMeanCount(&m));
    TEST_ASSERT_EQUAL_FLOAT(123.0f, v);   // untouched, never a fake 0
}

static void test_mean_reset(void)
{
    float v;
    wxMeanAdd(&m, 7.0f);
    wxMeanReset(&m);
    TEST_ASSERT_FALSE(wxMeanGet(&m, &v));
    TEST_ASSERT_EQUAL_UINT32(0, wxMeanCount(&m));
    wxMeanAdd(&m, 1.0f);
    TEST_ASSERT_TRUE(wxMeanGet(&m, &v));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);
}

static void test_gust_fewer_than_three_no_value(void)
{
    float v;
    TEST_ASSERT_FALSE(wxGustGet(&g, &v));
    wxGustAdd(&g, 10.0f);
    TEST_ASSERT_FALSE(wxGustGet(&g, &v));
    wxGustAdd(&g, 10.0f);
    TEST_ASSERT_FALSE(wxGustGet(&g, &v));
    wxGustAdd(&g, 10.0f);
    TEST_ASSERT_TRUE(wxGustGet(&g, &v));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, v);
}

static void test_gust_is_max_rolling_3s_mean(void)
{
    float v;
    const float s[] = {1, 2, 3, 9, 6, 3, 1, 1};   // means: 2, 4.67, 6, 6, 3.33, 1.67
    for (float x : s) wxGustAdd(&g, x);
    TEST_ASSERT_TRUE(wxGustGet(&g, &v));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, v);
}

static void test_gust_single_spike_diluted(void)
{
    float v;
    wxGustAdd(&g, 0); wxGustAdd(&g, 30.0f); wxGustAdd(&g, 0); wxGustAdd(&g, 0); wxGustAdd(&g, 0);
    TEST_ASSERT_TRUE(wxGustGet(&g, &v));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, v);
}

static void test_gust_reset(void)
{
    float v;
    wxGustAdd(&g, 5); wxGustAdd(&g, 5); wxGustAdd(&g, 5);
    wxGustReset(&g);
    TEST_ASSERT_FALSE(wxGustGet(&g, &v));
    wxGustAdd(&g, 1); wxGustAdd(&g, 1);
    TEST_ASSERT_FALSE(wxGustGet(&g, &v));
    wxGustAdd(&g, 1);
    TEST_ASSERT_TRUE(wxGustGet(&g, &v));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);      // old window must not leak
}

static void test_wind_kmh(void)
{
    TEST_ASSERT_EQUAL_FLOAT(2.4f, WIND_CAL_DEFAULT);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, windKmh(0, 1.0f, WIND_CAL_DEFAULT));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.4f, windKmh(1, 1.0f, WIND_CAL_DEFAULT));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 24.0f, windKmh(10, 1.0f, WIND_CAL_DEFAULT));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 24.0f, windKmh(20, 2.0f, WIND_CAL_DEFAULT));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 30.0f, windKmh(10, 1.0f, 3.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, windKmh(10, 0.0f, WIND_CAL_DEFAULT));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, windKmh(10, -1.0f, WIND_CAL_DEFAULT));
}

// Reed debounce: two-state machine, accept a level change only >= WIND_DEBOUNCE_US after the
// previous accepted change, count on an accepted HIGH->LOW (contact closes).
static wind_deb_t d;
static uint32_t feed(uint32_t t, uint8_t level, uint32_t *n) { if (windDebEdge(&d, level, t)) (*n)++; return *n; }

static void test_deb_constant(void)
{
    TEST_ASSERT_EQUAL_UINT32(3000, WIND_DEBOUNCE_US);
}

static void test_deb_close_bounce_counts_once(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(10000, 0, &n);                       // contact closes
    feed(10200, 1, &n); feed(10400, 0, &n);   // bounce burst
    feed(10900, 1, &n); feed(11500, 0, &n);
    feed(12000, 1, &n); feed(12700, 0, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
}

static void test_deb_release_bounce_adds_no_pulse(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(10000, 0, &n);                       // close: 1
    feed(60000, 1, &n);                       // release (accepted, no count)
    feed(60300, 0, &n);                       // release bounce = falling edge, rejected
    feed(60700, 1, &n);
    feed(61500, 0, &n); feed(62000, 1, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
}

static void test_deb_two_closes_12ms_apart_count_twice(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(0, 0, &n);
    feed(6000, 1, &n);
    feed(12000, 0, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

static void test_deb_boundary_exactly_3ms(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(1000, 0, &n);
    feed(1000 + WIND_DEBOUNCE_US - 1, 1, &n); // 2999 us: rejected
    feed(1000 + WIND_DEBOUNCE_US, 1, &n);     // 3000 us: accepted
    feed(1000 + 2 * WIND_DEBOUNCE_US, 0, &n); // accepted close
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

static void test_deb_same_level_is_ignored(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(100, 1, &n); feed(200, 1, &n);
    TEST_ASSERT_EQUAL_UINT32(0, n);
}

static void test_deb_micros_wrap(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(0xFFFFFF00u, 0, &n);                 // close 256 us before the wrap: 1
    feed(0x00000100u, 1, &n);                 // 512 us later, across the wrap: rejected
    feed(0x00000200u, 0, &n);                 // same level as state: ignored
    TEST_ASSERT_EQUAL_UINT32(1, n);
    feed(0x00000100u + 3000u, 1, &n);         // >= 3 ms after the close: release accepted
    feed(0x00000100u + 7000u, 0, &n);         // next close
    TEST_ASSERT_EQUAL_UINT32(2, n);
}

static void test_deb_reset_starts_unprimed(void)
{
    uint32_t n = 0;
    windDebReset(&d, 1);
    feed(5, 0, &n);                           // first edge counts regardless of time since boot
    TEST_ASSERT_EQUAL_UINT32(1, n);
}

static void test_pulse_delta_wraps(void)
{
    uint32_t last = 10;
    TEST_ASSERT_EQUAL_UINT32(5, windPulseDelta(15, &last));
    TEST_ASSERT_EQUAL_UINT32(15, last);
    TEST_ASSERT_EQUAL_UINT32(0, windPulseDelta(15, &last));
    last = 0xFFFFFFFEu;
    TEST_ASSERT_EQUAL_UINT32(3, windPulseDelta(1, &last));   // counter wrapped
    TEST_ASSERT_EQUAL_UINT32(1, last);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_mean_known_samples);
    RUN_TEST(test_mean_empty_is_no_value);
    RUN_TEST(test_mean_reset);
    RUN_TEST(test_gust_fewer_than_three_no_value);
    RUN_TEST(test_gust_is_max_rolling_3s_mean);
    RUN_TEST(test_gust_single_spike_diluted);
    RUN_TEST(test_gust_reset);
    RUN_TEST(test_wind_kmh);
    RUN_TEST(test_deb_constant);
    RUN_TEST(test_deb_close_bounce_counts_once);
    RUN_TEST(test_deb_release_bounce_adds_no_pulse);
    RUN_TEST(test_deb_two_closes_12ms_apart_count_twice);
    RUN_TEST(test_deb_boundary_exactly_3ms);
    RUN_TEST(test_deb_same_level_is_ignored);
    RUN_TEST(test_deb_micros_wrap);
    RUN_TEST(test_deb_reset_starts_unprimed);
    RUN_TEST(test_pulse_delta_wraps);
    return UNITY_END();
}
