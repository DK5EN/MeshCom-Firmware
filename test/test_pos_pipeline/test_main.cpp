// Native tests for the GPS-11 W2 glue (src/pos_pipeline.*).
//
// Characterization part: legacy_* is a verbatim copy of what gps_functions.cpp
// did with the raw fix before W2 (cround4abs -> node_lat/lon, hemisphere from
// rawLat().negative) plus the bConvPos DDMM.MM text of PositionToAPRS()
// (loop_functions.cpp). The helper and the pipeline with the filter switched
// off must reproduce it byte for byte.
//
//   pio test -e native -f test_pos_pipeline

#include <unity.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pos_pipeline.h>

void setUp(void)    {}
void tearDown(void) {}

// ---- legacy reference (loop_functions.cpp:6203-6222, gps_functions.cpp:1140-1173) ----
static double l_cround4(double dvar)
{
    char cvar[20];
    snprintf(cvar, sizeof(cvar), "%.4lf", dvar);
    double rvar;
    sscanf(cvar, "%lf", &rvar);
    return rvar;
}
static double l_cround4abs(double dvar)
{
    double rvar = l_cround4(dvar);
    return fabs(rvar);
}

struct Node { double lat, lon; char lat_c, lon_c; };

static Node legacy_store(double lat, double lon)
{
    Node n;
    double dlat = l_cround4abs(lat);
    double dlon = l_cround4abs(lon);
    n.lat = l_cround4(dlat);
    n.lon = l_cround4(dlon);
    n.lon_c = signbit(lon) ? 'W' : 'E';   // gps.location.rawLng().negative
    n.lat_c = signbit(lat) ? 'S' : 'N';   // gps.location.rawLat().negative
    return n;
}

// PositionToAPRS(bConvPos = true): the lat/lon part of msg_start
static void legacy_aprs(const Node &n, char *out, size_t sz)
{
    double lat = fabs(n.lat), lon = fabs(n.lon);
    double slatr = 60.0, slonr = 60.0;
    double slat = (int)lat;
    slatr = (lat - slat) * slatr;
    slat = (slat * 100.) + slatr;
    double slon = (int)lon;
    slonr = (lon - slon) * slonr;
    slon = (slon * 100.) + slonr;
    snprintf(out, sz, "%07.2lf%c/%08.2lf%c", slat, n.lat_c, slon, n.lon_c);
}

struct In { double lat, lon; };

static const In TABLE[] = {
    { 48.1234, 11.5678 },           // typical
    { 48.0, 11.0 },
    { -33.8688, 151.2093 },         // S/E
    { 40.7128, -74.0060 },          // N/W
    { -34.6037, -58.3816 },         // S/W
    { -0.0, -0.0 },
    { 0.0, 0.0 },
    { -0.00004, -0.00004 },         // rounds to -0.0000 but is negative
    { 48.00005, 11.00005 },         // 1e-4 rounding edge
    { 48.000049999, 11.000049999 },
    { 48.00015, 11.00015 },
    { 47.99995, 10.99995 },
    { 48.000150001, 11.000149999 },
    { -48.00005, -11.00005 },
    { 48.0083333, 11.0083333 },     // 0.5' = 0.01' cell border at 48.0083333
    { 48.00833, 11.00833 },
    { 48.008334, 11.008334 },
    { 48.0083, 11.0083 },
    { 48.00836, 11.00836 },         // next cell
    { 48.016666, 11.016666 },       // 1.0'
    { 48.999999, 11.999999 },       // carry to the next degree
    { 89.99995, 179.99995 },
    { -89.99995, -179.99995 },
    { 0.00005, 0.00005 },
};
#define NTABLE (sizeof(TABLE) / sizeof(TABLE[0]))

static void expect_same(const Node &a, const Node &b)
{
    TEST_ASSERT_EQUAL_MEMORY(&a.lat, &b.lat, sizeof(double));
    TEST_ASSERT_EQUAL_MEMORY(&a.lon, &b.lon, sizeof(double));
    TEST_ASSERT_EQUAL_CHAR(a.lat_c, b.lat_c);
    TEST_ASSERT_EQUAL_CHAR(a.lon_c, b.lon_c);
    char ta[64], tb[64];
    legacy_aprs(a, ta, sizeof(ta));
    legacy_aprs(b, tb, sizeof(tb));
    TEST_ASSERT_EQUAL_STRING(ta, tb);
}

static Node helper_store(double lat, double lon)
{
    Node n;
    posPipelineStore(lat, lon, &n.lat, &n.lon, &n.lat_c, &n.lon_c);
    return n;
}

void test_helper_matches_legacy(void)
{
    for(size_t i = 0; i < NTABLE; i++)
        expect_same(legacy_store(TABLE[i].lat, TABLE[i].lon), helper_store(TABLE[i].lat, TABLE[i].lon));
}

// Literal on-air text, so the pin does not rest on the legacy copy alone.
void test_aprs_text_literals(void)
{
    static const struct { double lat, lon; const char *txt; } LIT[] = {
        { 48.1234, 11.5678,     "4807.40N/01134.07E" },
        { -33.8688, 151.2093,   "3352.13S/15112.56E" },
        { 40.7128, -74.0060,    "4042.77N/07400.36W" },
        { -0.00004, -0.00004,   "0000.00S/00000.00W" },
    };
    for(size_t i = 0; i < sizeof(LIT) / sizeof(LIT[0]); i++)
    {
        char t[64];
        legacy_aprs(legacy_store(LIT[i].lat, LIT[i].lon), t, sizeof(t));
        TEST_ASSERT_EQUAL_STRING(LIT[i].txt, t);
        legacy_aprs(helper_store(LIT[i].lat, LIT[i].lon), t, sizeof(t));
        TEST_ASSERT_EQUAL_STRING(LIT[i].txt, t);
    }
}

void test_disabled_pipeline_is_bit_identical(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    posPipelineSetEnabled(&p, false);
    uint32_t t = 1000;
    for(size_t i = 0; i < NTABLE; i++, t += 3000)
    {
        double ol, oo;
        posPipelineFix(&p, t, TABLE[i].lat, TABLE[i].lon, 1.0f, true, 0.0f, 0.0f, POS_KF_MODE_FIXED, &ol, &oo);
        TEST_ASSERT_EQUAL_MEMORY(&TABLE[i].lat, &ol, sizeof(double));
        TEST_ASSERT_EQUAL_MEMORY(&TABLE[i].lon, &oo, sizeof(double));
        expect_same(legacy_store(TABLE[i].lat, TABLE[i].lon), helper_store(ol, oo));
    }
}

void test_zeroed_struct_is_enabled(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    TEST_ASSERT_TRUE(posPipelineEnabled(&p));
}

// ---- pipeline behaviour ----
static void feed(struct PosPipeline *p, uint32_t t, double lat, double lon, float hdop,
                 enum PosFilterResult *r, double *ol, double *oo, float spd = 0.0f,
                 enum PosFilterMode m = POS_KF_MODE_FIXED)
{
    *r = posPipelineFix(p, t, lat, lon, hdop, true, spd, 0.0f, m, ol, oo);
}

void test_first_call_dt_zero_and_reseed(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    enum PosFilterResult r; double ol, oo;
    feed(&p, 5000, 48.1, 11.5, 1.0f, &r, &ol, &oo);
    TEST_ASSERT_EQUAL_INT(POS_KF_RESEED, r);
    TEST_ASSERT_TRUE(p.have_last);
    TEST_ASSERT_EQUAL_UINT32(5000, p.last_ms);
    TEST_ASSERT_TRUE(fabs(ol - 48.1) < 1e-9 && fabs(oo - 11.5) < 1e-9);
}

void test_skip_keeps_timestamp_and_last_output(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    enum PosFilterResult r; double ol, oo, sl, so;
    feed(&p, 1000, 48.1, 11.5, 1.0f, &r, &ol, &oo);
    feed(&p, 4000, 48.1, 11.5, 1.0f, &r, &ol, &oo);
    TEST_ASSERT_EQUAL_UINT32(4000, p.last_ms);
    feed(&p, 7000, 49.0, 12.0, 0.0f, &r, &sl, &so);   // hdop 0 -> SKIP
    TEST_ASSERT_EQUAL_INT(POS_KF_SKIP, r);
    TEST_ASSERT_EQUAL_UINT32(4000, p.last_ms);
    TEST_ASSERT_EQUAL_MEMORY(&ol, &sl, sizeof(double));
    TEST_ASSERT_EQUAL_MEMORY(&oo, &so, sizeof(double));
}

void test_skip_before_any_output_returns_raw(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    enum PosFilterResult r; double ol, oo;
    feed(&p, 1000, 49.0, 12.0, 0.0f, &r, &ol, &oo);
    TEST_ASSERT_EQUAL_INT(POS_KF_SKIP, r);
    TEST_ASSERT_TRUE((49.0) == (ol));
    TEST_ASSERT_TRUE((12.0) == (oo));
    TEST_ASSERT_FALSE(p.have_last);
}

void test_reject_advances_timestamp(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    enum PosFilterResult r; double ol, oo;
    feed(&p, 1000, 48.1, 11.5, 1.0f, &r, &ol, &oo);
    feed(&p, 4000, 48.1, 11.5, 1.0f, &r, &ol, &oo);
    feed(&p, 7000, 48.2, 11.5, 1.0f, &r, &ol, &oo);   // ~11 km away: gated
    TEST_ASSERT_EQUAL_INT(POS_KF_REJECT, r);
    TEST_ASSERT_EQUAL_UINT32(7000, p.last_ms);
    TEST_ASSERT_TRUE(fabs(ol - 48.1) < 1e-3);          // output stays near the old position
}

void test_millis_rollover_dt(void)
{
    // 2 s before the wrap, 1 s after: dt = 3 s, not ~4.29e9 (that would re-seed in TRACK)
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    enum PosFilterResult r; double ol, oo;
    feed(&p, 0xFFFFFFFFu - 1999u, 48.1, 11.5, 1.0f, &r, &ol, &oo, 5.0f, POS_KF_MODE_TRACK);
    feed(&p, 0xFFFFFFFFu - 999u, 48.1, 11.5, 1.0f, &r, &ol, &oo, 5.0f, POS_KF_MODE_TRACK);
    feed(&p, 1000u, 48.1, 11.5, 1.0f, &r, &ol, &oo, 5.0f, POS_KF_MODE_TRACK);
    TEST_ASSERT_EQUAL_INT(POS_KF_OK, r);
    TEST_ASSERT_EQUAL_UINT32(1000, p.last_ms);
}

// stationary FIXED node with a tiny deterministic wobble
static void run_still(struct PosPipeline *p, uint32_t *t, int n, double *ol, double *oo)
{
    enum PosFilterResult r;
    for(int i = 0; i < n; i++, *t += 3000)
    {
        double w = 1e-6 * ((i % 5) - 2);
        feed(p, *t, 48.1 + w, 11.5 - w, 1.0f, &r, ol, oo);
    }
}

void test_hold_active_while_still(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    uint32_t t = 1000;
    double ol, oo;
    run_still(&p, &t, 40, &ol, &oo);
    TEST_ASSERT_TRUE(posFilterIsStill(&p.filter));
    TEST_ASSERT_TRUE(p.hold.init);
    double hl = p.hold.lat;
    run_still(&p, &t, 20, &ol, &oo);
    TEST_ASSERT_TRUE((hl) == (ol));     // held cell value
}

void test_hold_released_on_moving_and_reseed(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    uint32_t t = 1000;
    double ol, oo;
    enum PosFilterResult r;
    run_still(&p, &t, 40, &ol, &oo);
    TEST_ASSERT_TRUE(p.hold.init);
    // walking: reported speed 1.5 m/s for a while -> still -> moving
    for(int i = 0; i < 20 && posFilterIsStill(&p.filter); i++, t += 3000)
        feed(&p, t, 48.1, 11.5, 1.0f, &r, &ol, &oo, 1.5f);
    TEST_ASSERT_FALSE(posFilterIsStill(&p.filter));
    TEST_ASSERT_EQUAL_UINT16(1, p.filter.n_moving);
    TEST_ASSERT_FALSE(p.hold.init);
    // output is the filtered position while moving
    double fl, fo;
    posFilterGet(&p.filter, &fl, &fo);
    TEST_ASSERT_TRUE((fl) == (ol));
    TEST_ASSERT_TRUE((fo) == (oo));

    // reseed resets the hold: settle still again, then relocate far for > RESEED_MS
    struct PosPipeline q;
    memset(&q, 0, sizeof(q));
    posPipelineReset(&q);
    t = 1000;
    run_still(&q, &t, 40, &ol, &oo);
    TEST_ASSERT_TRUE(q.hold.init);
    for(int i = 0; i < 40; i++, t += 3000)
    {
        feed(&q, t, 49.0, 12.0, 1.0f, &r, &ol, &oo);
        if(r == POS_KF_RESEED) break;
    }
    TEST_ASSERT_EQUAL_INT(POS_KF_RESEED, r);
    // hold was reset and re-seeded by the same call with the new value
    TEST_ASSERT_TRUE(fabs(q.hold.lat - 49.0) < 1e-3);
}

// A Setup save that rewrites the unchanged position keeps the filter; a changed one resets it.
void test_manual_set_resets_only_on_change(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    double ol, oo;
    posPipelineFix(&p, 1000, 48.1234, 11.5678, 1.0f, true, 0.0f, 0.0f, POS_KF_MODE_FIXED, &ol, &oo);
    TEST_ASSERT_TRUE(p.filter.init);

    posPipelineManualSet(&p, 48.1234, 'N', 11.5678, 'E', 48.1234, 'N', 11.5678, 'E');
    TEST_ASSERT_TRUE(p.filter.init);
    TEST_ASSERT_TRUE(p.have_last);

    posPipelineManualSet(&p, 48.1234, 'N', 11.5678, 'E', 48.1234, 'S', 11.5678, 'E');
    TEST_ASSERT_FALSE(p.filter.init);

    posPipelineFix(&p, 2000, 48.1234, 11.5678, 1.0f, true, 0.0f, 0.0f, POS_KF_MODE_FIXED, &ol, &oo);
    posPipelineManualSet(&p, 48.1234, 'N', 11.5678, 'E', 48.1234, 'N', 11.5679, 'E');
    TEST_ASSERT_FALSE(p.filter.init);
}

void test_disable_and_reset_semantics(void)
{
    struct PosPipeline p;
    memset(&p, 0, sizeof(p));
    posPipelineReset(&p);
    uint32_t t = 1000;
    double ol, oo;
    run_still(&p, &t, 10, &ol, &oo);
    TEST_ASSERT_TRUE(p.filter.init);
    posPipelineSetEnabled(&p, true);          // no change: no reset
    TEST_ASSERT_TRUE(p.filter.init);
    posPipelineSetEnabled(&p, false);
    TEST_ASSERT_FALSE(p.filter.init);
    TEST_ASSERT_FALSE(p.hold.init);
    TEST_ASSERT_FALSE(p.have_last);
    posPipelineSetEnabled(&p, true);
    run_still(&p, &t, 3, &ol, &oo);
    posPipelineReset(&p);
    TEST_ASSERT_TRUE(posPipelineEnabled(&p)); // reset keeps the switch
    TEST_ASSERT_FALSE(p.filter.init);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_helper_matches_legacy);
    RUN_TEST(test_aprs_text_literals);
    RUN_TEST(test_disabled_pipeline_is_bit_identical);
    RUN_TEST(test_zeroed_struct_is_enabled);
    RUN_TEST(test_first_call_dt_zero_and_reseed);
    RUN_TEST(test_skip_keeps_timestamp_and_last_output);
    RUN_TEST(test_skip_before_any_output_returns_raw);
    RUN_TEST(test_reject_advances_timestamp);
    RUN_TEST(test_millis_rollover_dt);
    RUN_TEST(test_hold_active_while_still);
    RUN_TEST(test_hold_released_on_moving_and_reseed);
    RUN_TEST(test_disable_and_reset_semantics);
    RUN_TEST(test_manual_set_resets_only_on_change);
    return UNITY_END();
}
