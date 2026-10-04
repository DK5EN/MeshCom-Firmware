// Native test suite for the slot bookkeeping of the T-Deck decoded-tile cache
// (TD-09, src/t-deck/tile_cache.h).
//
// The cache holds no pixels itself: it maps {set, zoom, tx, ty} to a slot and
// picks the least recently used slot for eviction. Covered: miss then hit,
// LRU eviction order, a touched slot surviving, invalidate_all, the set index
// as part of the key, and the hit/miss counters.
//
//   pio test -e native -f test_tile_cache

#include <unity.h>

// Pure header under src/t-deck/, no Arduino/LVGL deps; [env:native] has -I src.
#include <t-deck/tile_cache.h>

void setUp(void)    {}
void tearDown(void) {}

static const int N = 4;

static TileKey K(int set, int zoom, int tx, int ty) { return TileKey{ set, zoom, tx, ty }; }

static void test_miss_then_hit(void)
{
    TileCache<N> c;
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 8, 136, 89)));
    int s = c.claim(K(0, 8, 136, 89));
    TEST_ASSERT_TRUE(s >= 0 && s < N);
    TEST_ASSERT_EQUAL_INT(s, c.find(K(0, 8, 136, 89)));
}

static void test_slot_payload_survives(void)
{
    TileCache<N> c;
    int dummy;
    int s = c.claim(K(0, 8, 1, 1));
    c.slot[s].buf = &dummy;
    c.slot[s].w = 256;
    c.slot[s].h = 256;
    int f = c.find(K(0, 8, 1, 1));
    TEST_ASSERT_EQUAL_INT(s, f);
    TEST_ASSERT_TRUE(c.slot[f].buf == &dummy);
    TEST_ASSERT_EQUAL_UINT(256, c.slot[f].w);
}

static void test_free_slots_used_before_eviction(void)
{
    TileCache<N> c;
    bool used[N] = { false };
    for (int i = 0; i < N; i++)
    {
        int s = c.claim(K(0, 5, i, 0));
        TEST_ASSERT_FALSE(used[s]);
        used[s] = true;
    }
    for (int i = 0; i < N; i++)
        TEST_ASSERT_TRUE(c.find(K(0, 5, i, 0)) >= 0);
}

static void test_lru_evicts_first_inserted(void)
{
    TileCache<N> c;
    int first = c.claim(K(0, 5, 0, 0));
    for (int i = 1; i < N; i++) c.claim(K(0, 5, i, 0));
    // N+1th distinct key evicts the first inserted one and takes its slot.
    int s = c.claim(K(0, 5, N, 0));
    TEST_ASSERT_EQUAL_INT(first, s);
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 5, 0, 0)));
    for (int i = 1; i <= N; i++)
        TEST_ASSERT_TRUE(c.find(K(0, 5, i, 0)) >= 0);
}

static void test_touched_slot_survives(void)
{
    TileCache<N> c;
    for (int i = 0; i < N; i++) c.claim(K(0, 5, i, 0));
    // touch the oldest; the second-oldest becomes the victim
    TEST_ASSERT_TRUE(c.find(K(0, 5, 0, 0)) >= 0);
    c.claim(K(0, 5, N, 0));
    TEST_ASSERT_TRUE(c.find(K(0, 5, 0, 0)) >= 0);
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 5, 1, 0)));
}

static void test_eviction_order_over_several_rounds(void)
{
    TileCache<N> c;
    for (int i = 0; i < N + 3; i++) c.claim(K(0, 5, i, 0));
    // keys 0..2 are gone in insertion order, 3..N+2 remain
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 5, i, 0)));
    for (int i = 3; i < N + 3; i++)
        TEST_ASSERT_TRUE(c.find(K(0, 5, i, 0)) >= 0);
}

static void test_invalidate_all(void)
{
    TileCache<N> c;
    for (int i = 0; i < N; i++) c.claim(K(0, 5, i, 0));
    c.invalidate_all();
    for (int i = 0; i < N; i++)
        TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 5, i, 0)));
    // slots are reusable afterwards
    int s = c.claim(K(0, 5, 0, 0));
    TEST_ASSERT_EQUAL_INT(s, c.find(K(0, 5, 0, 0)));
}

static void test_set_index_is_part_of_key(void)
{
    TileCache<N> c;
    int a = c.claim(K(0, 8, 10, 20));
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(1, 8, 10, 20)));
    int b = c.claim(K(1, 8, 10, 20));
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_EQUAL_INT(a, c.find(K(0, 8, 10, 20)));
    TEST_ASSERT_EQUAL_INT(b, c.find(K(1, 8, 10, 20)));
}

static void test_every_key_field_matters(void)
{
    TileCache<N> c;
    c.claim(K(0, 8, 10, 20));
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 9, 10, 20)));
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 8, 11, 20)));
    TEST_ASSERT_EQUAL_INT(-1, c.find(K(0, 8, 10, 21)));
}

static void test_counters(void)
{
    TileCache<N> c;
    TEST_ASSERT_EQUAL_UINT32(0, c.hits());
    TEST_ASSERT_EQUAL_UINT32(0, c.misses());
    c.find(K(0, 1, 0, 0));            // miss
    c.claim(K(0, 1, 0, 0));
    c.find(K(0, 1, 0, 0));            // hit
    c.find(K(0, 1, 0, 0));            // hit
    c.find(K(0, 1, 1, 0));            // miss
    TEST_ASSERT_EQUAL_UINT32(2, c.hits());
    TEST_ASSERT_EQUAL_UINT32(2, c.misses());
    c.invalidate_all();               // counters are cumulative
    TEST_ASSERT_EQUAL_UINT32(2, c.hits());
    TEST_ASSERT_EQUAL_UINT32(2, c.misses());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_miss_then_hit);
    RUN_TEST(test_slot_payload_survives);
    RUN_TEST(test_free_slots_used_before_eviction);
    RUN_TEST(test_lru_evicts_first_inserted);
    RUN_TEST(test_touched_slot_survives);
    RUN_TEST(test_eviction_order_over_several_rounds);
    RUN_TEST(test_invalidate_all);
    RUN_TEST(test_set_index_is_part_of_key);
    RUN_TEST(test_every_key_field_matters);
    RUN_TEST(test_counters);
    return UNITY_END();
}
