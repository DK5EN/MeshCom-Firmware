// OR-STAT: histogram behind onrx_* fields of the STAT line.
#include <unity.h>
#include <stdint.h>
#include <string.h>

#include <onrx_stats.h>

void setUp(void) { struct onrxStatsOut o; onrxStatsSnapshotReset(&o, 50); }
void tearDown(void) {}

static void test_leer(void)
{
    struct onrxStatsOut o;
    memset(&o, 0x55, sizeof(o));
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(0, o.n);
    TEST_ASSERT_EQUAL_UINT32(0, o.avg_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.p95_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.p99_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.max_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.warn);
}

static void test_einzelprobe(void)
{
    struct onrxStatsOut o;
    onrxStatsAdd(7);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(1, o.n);
    TEST_ASSERT_EQUAL_UINT32(7, o.avg_ms);
    TEST_ASSERT_EQUAL_UINT32(7, o.p95_ms);
    TEST_ASSERT_EQUAL_UINT32(7, o.p99_ms);
    TEST_ASSERT_EQUAL_UINT32(7, o.max_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.warn);
}

static void test_hundert_proben_1_bis_100(void)
{
    struct onrxStatsOut o;
    for(uint32_t i = 1; i <= 100; i++)
        onrxStatsAdd(i);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(100, o.n);
    TEST_ASSERT_EQUAL_UINT32(50, o.avg_ms);   // 5050 / 100 = 50 (integer)
    // ceil(0.95*100)=95 -> 95th smallest = 95 would be bucket 95, but values
    // >= 64 share the overflow bucket -> reports window max 100.
    TEST_ASSERT_EQUAL_UINT32(100, o.p95_ms);
    TEST_ASSERT_EQUAL_UINT32(100, o.p99_ms);
    TEST_ASSERT_EQUAL_UINT32(100, o.max_ms);
    TEST_ASSERT_EQUAL_UINT32(50, o.warn);     // 51..100
}

// ceil(p * n) is pinned here: at n = 1 and n = 100 floor and ceil agree.
static void test_zehn_proben_ceil_regel(void)
{
    struct onrxStatsOut o;
    for(uint32_t i = 1; i <= 10; i++)
        onrxStatsAdd(i);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(10, o.n);
    TEST_ASSERT_EQUAL_UINT32(5, o.avg_ms);    // 55 / 10
    TEST_ASSERT_EQUAL_UINT32(10, o.p95_ms);   // ceil(9.5) = 10 (floor would give 9)
    TEST_ASSERT_EQUAL_UINT32(10, o.p99_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.warn);
}

static void test_dreissig_proben_ceil_regel(void)
{
    struct onrxStatsOut o;
    for(uint32_t i = 1; i <= 30; i++)
        onrxStatsAdd(i);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(29, o.p95_ms);   // ceil(28.5) = 29 (floor would give 28)
    TEST_ASSERT_EQUAL_UINT32(30, o.p99_ms);   // ceil(29.7) = 30
}

// A cross-task snapshot can see bucket counts whose max has not landed yet.
static void test_ueberlauf_ohne_max_meldet_mindestens_64(void)
{
    uint32_t c[ONRX_STATS_BUCKETS] = {0};
    c[ONRX_STATS_BUCKETS - 1] = 5;
    TEST_ASSERT_EQUAL_UINT32(64, onrxStatsPercentile(c, 5, 0, 95));
}

static void test_hundert_proben_1_bis_60(void)
{
    struct onrxStatsOut o;
    for(int r = 0; r < 100; r++)              // 100 samples: 1..60 spread, 0.. sorted
        onrxStatsAdd((uint32_t)(r % 60) + 1);
    onrxStatsSnapshotReset(&o, 50);
    // sorted: values 1..40 twice, 41..60 once... r%60: 0..59 then 0..39
    // counts: v 1..40 -> 2, v 41..60 -> 1. cum(v)=2v for v<=40 (=80), then 80+(v-40).
    // p95: need cum >= 95 -> v=55. p99: cum >= 99 -> v=59.
    TEST_ASSERT_EQUAL_UINT32(55, o.p95_ms);
    TEST_ASSERT_EQUAL_UINT32(59, o.p99_ms);
    TEST_ASSERT_EQUAL_UINT32(60, o.max_ms);
}

static void test_ueberlauf_meldet_fenstermax(void)
{
    struct onrxStatsOut o;
    for(int i = 0; i < 98; i++)
        onrxStatsAdd(2);
    onrxStatsAdd(64);
    onrxStatsAdd(900);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(100, o.n);
    TEST_ASSERT_EQUAL_UINT32(2, o.p95_ms);    // cum(2)=98 >= 95
    TEST_ASSERT_EQUAL_UINT32(900, o.p99_ms);  // ceil(99)=99 > 98 -> overflow -> max
    TEST_ASSERT_EQUAL_UINT32(900, o.max_ms);
    TEST_ASSERT_EQUAL_UINT32(2, o.warn);
}

static void test_63_ist_noch_kein_ueberlauf(void)
{
    struct onrxStatsOut o;
    onrxStatsAdd(63);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(63, o.p99_ms);
    onrxStatsAdd(64);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(64, o.p99_ms);   // overflow -> max (64)
}

static void test_snapshot_setzt_fenster_zurueck(void)
{
    struct onrxStatsOut o;
    onrxStatsAdd(10);
    onrxStatsAdd(80);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(2, o.n);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(0, o.n);
    TEST_ASSERT_EQUAL_UINT32(0, o.max_ms);
    TEST_ASSERT_EQUAL_UINT32(0, o.warn);
    onrxStatsAdd(3);
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(1, o.n);
    TEST_ASSERT_EQUAL_UINT32(3, o.max_ms);    // old max 80 is gone
}

static void test_warn_strikt_groesser(void)
{
    struct onrxStatsOut o;
    onrxStatsAdd(49);
    onrxStatsAdd(50);   // == threshold: not counted
    onrxStatsAdd(51);   // counted
    onrxStatsSnapshotReset(&o, 50);
    TEST_ASSERT_EQUAL_UINT32(1, o.warn);
}

static void test_warn_schwelle_im_histogrammbereich(void)
{
    struct onrxStatsOut o;
    onrxStatsAdd(10);
    onrxStatsAdd(11);
    onrxStatsAdd(12);
    onrxStatsSnapshotReset(&o, 10);
    TEST_ASSERT_EQUAL_UINT32(2, o.warn);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_leer);
    RUN_TEST(test_einzelprobe);
    RUN_TEST(test_hundert_proben_1_bis_100);
    RUN_TEST(test_zehn_proben_ceil_regel);
    RUN_TEST(test_dreissig_proben_ceil_regel);
    RUN_TEST(test_ueberlauf_ohne_max_meldet_mindestens_64);
    RUN_TEST(test_hundert_proben_1_bis_60);
    RUN_TEST(test_ueberlauf_meldet_fenstermax);
    RUN_TEST(test_63_ist_noch_kein_ueberlauf);
    RUN_TEST(test_snapshot_setzt_fenster_zurueck);
    RUN_TEST(test_warn_strikt_groesser);
    RUN_TEST(test_warn_schwelle_im_histogrammbereich);
    return UNITY_END();
}
