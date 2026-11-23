// OTA-LOWHEAP-01 (a'): the slot allocator of src/fw_tls_arena.h.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "fw_tls_arena.h"

#define REC 16717u       // input record buffer (FWNET_TLS_REC_BUF)
#define OUT 4429u        // output record buffer after the 4 KB patch (FWNET_TLS_OUT_BUF)
#define ARENA_LEN 32768u // max(16720 + 4432, TINFL_LZ_DICT_SIZE): the size fw_update_net.cpp uses

static uint8_t g_buf[ARENA_LEN + 64];
static FwArena g_a;

void setUp(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    fwArenaInit(&g_a, g_buf, ARENA_LEN);
}
void tearDown(void) {}

static bool overlap(const void *p, size_t n, const void *q, size_t m)
{
    const uint8_t *a = (const uint8_t *)p, *b = (const uint8_t *)q;
    return a < b + m && b < a + n;
}

void test_two_records_fit_without_overlap(void)
{
    void *r1 = fwArenaAllocRecord(&g_a, REC);
    void *r2 = fwArenaAllocRecord(&g_a, OUT);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_FALSE(overlap(r1, REC, r2, OUT));
    TEST_ASSERT_TRUE(fwArenaOwns(&g_a, r1));
    TEST_ASSERT_TRUE(fwArenaOwns(&g_a, r2));
    // the full compiled length is writable (mbedtls_ssl_free zeroizes it)
    memset(r1, 0xAA, REC);
    memset(r2, 0xBB, OUT);
    TEST_ASSERT_EQUAL_UINT8(0xAA, ((uint8_t *)r1)[REC - 1]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, ((uint8_t *)r2)[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, ((uint8_t *)r2)[OUT - 1]);
    TEST_ASSERT_EQUAL_UINT(2, fwArenaLive(&g_a));
}

void test_third_that_does_not_fit_is_null(void)
{
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, REC));
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, OUT));
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, REC)); // 11616 B left: a second input record does not fit
    TEST_ASSERT_EQUAL_UINT(2, fwArenaLive(&g_a));
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, OUT)); // the spare slot takes a small one
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, OUT));     // slots exhausted
    TEST_ASSERT_EQUAL_UINT(3, fwArenaLive(&g_a));
}

// Slots start on FW_ARENA_ALIGN and are rounded up: without the rounding the output record would start at 16717,
// misaligned for mbedTLS (the heap hands out 4/8/16-aligned memory).
void test_slots_aligned_and_rounded(void)
{
    void *in = fwArenaAllocRecord(&g_a, REC);
    void *out = fwArenaAllocRecord(&g_a, OUT);
    TEST_ASSERT_EQUAL_PTR(g_buf, in);
    TEST_ASSERT_EQUAL_UINT(0, ((size_t)((uint8_t *)out - g_buf)) % FW_ARENA_ALIGN);
    TEST_ASSERT_EQUAL_PTR(g_buf + 16720u, out);
    TEST_ASSERT_EQUAL_UINT(16720u, fwArenaFree(&g_a, in));
    TEST_ASSERT_EQUAL_UINT(4432u, fwArenaFree(&g_a, out));
}

// A request that fits the gap below a live slot goes there (first fit from offset 0), not after it.
void test_small_request_fills_gap_below_live_slot(void)
{
    void *in = fwArenaAllocRecord(&g_a, REC);
    void *out = fwArenaAllocRecord(&g_a, OUT);
    fwArenaFree(&g_a, in); // gap [0, 16720) below the live out record
    void *p = fwArenaAllocRecord(&g_a, OUT);
    TEST_ASSERT_EQUAL_PTR(g_buf, p);
    TEST_ASSERT_FALSE(overlap(p, OUT, out, OUT));
    void *q = fwArenaAllocRecord(&g_a, OUT); // next gap start: right after p, still below out
    TEST_ASSERT_EQUAL_PTR(g_buf + 4432u, q);
    TEST_ASSERT_FALSE(overlap(q, OUT, out, OUT));
}

void test_oversize_is_null(void)
{
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, ARENA_LEN + 1));
    TEST_ASSERT_EQUAL_UINT(0, fwArenaLive(&g_a));
}

void test_free_then_realloc_reuses(void)
{
    void *r1 = fwArenaAllocRecord(&g_a, REC);
    void *r2 = fwArenaAllocRecord(&g_a, OUT);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(REC, fwArenaFree(&g_a, r1));
    void *r3 = fwArenaAllocRecord(&g_a, REC);
    TEST_ASSERT_EQUAL_PTR(r1, r3);
    TEST_ASSERT_FALSE(overlap(r3, REC, r2, OUT));
    // the second slot freed: reused as well
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(OUT, fwArenaFree(&g_a, r2));
    TEST_ASSERT_EQUAL_PTR(r2, fwArenaAllocRecord(&g_a, OUT));
}

void test_mixed_sizes_first_fit(void)
{
    // in 16717 + out 4429 (after the output buffer patch) fit with room to spare
    void *in = fwArenaAllocRecord(&g_a, REC);
    void *out = fwArenaAllocRecord(&g_a, 4429u);
    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_FALSE(overlap(in, REC, out, 4429u));
    TEST_ASSERT_TRUE(fwArenaOwns(&g_a, out));
    // freeing the first leaves a hole that a same-sized record takes again
    fwArenaFree(&g_a, in);
    TEST_ASSERT_EQUAL_PTR(in, fwArenaAllocRecord(&g_a, REC));
}

void test_owns_inside_outside_edge(void)
{
    TEST_ASSERT_TRUE(fwArenaOwns(&g_a, g_buf));
    TEST_ASSERT_TRUE(fwArenaOwns(&g_a, g_buf + ARENA_LEN - 1));
    TEST_ASSERT_FALSE(fwArenaOwns(&g_a, g_buf + ARENA_LEN)); // one past the end
    TEST_ASSERT_FALSE(fwArenaOwns(&g_a, g_buf - 1));
    TEST_ASSERT_FALSE(fwArenaOwns(&g_a, nullptr));
    int local = 0;
    TEST_ASSERT_FALSE(fwArenaOwns(&g_a, &local));
    FwArena none;
    fwArenaInit(&none, nullptr, 0);
    TEST_ASSERT_FALSE(fwArenaOwns(&none, g_buf));
    TEST_ASSERT_FALSE(fwArenaOwns(&none, nullptr));
}

void test_free_of_foreign_pointer_is_ignored(void)
{
    void *r1 = fwArenaAllocRecord(&g_a, REC);
    int local = 0;
    TEST_ASSERT_EQUAL_UINT(0, fwArenaFree(&g_a, &local));
    TEST_ASSERT_EQUAL_UINT(0, fwArenaFree(&g_a, nullptr));
    TEST_ASSERT_EQUAL_UINT(0, fwArenaFree(&g_a, (uint8_t *)r1 + 16)); // inside, not a slot start
    TEST_ASSERT_EQUAL_UINT(1, fwArenaLive(&g_a));
    TEST_ASSERT_GREATER_THAN_UINT(0, fwArenaFree(&g_a, r1));
    TEST_ASSERT_EQUAL_UINT(0, fwArenaFree(&g_a, r1)); // double free
}

void test_take_whole_refused_while_record_live(void)
{
    void *r1 = fwArenaAllocRecord(&g_a, REC);
    TEST_ASSERT_NULL(fwArenaTakeWhole(&g_a, 32768u));
    fwArenaFree(&g_a, r1);
    void *d = fwArenaTakeWhole(&g_a, 32768u);
    TEST_ASSERT_EQUAL_PTR(g_buf, d);
}

void test_take_whole_granted_when_all_free(void)
{
    void *r1 = fwArenaAllocRecord(&g_a, REC);
    void *r2 = fwArenaAllocRecord(&g_a, OUT);
    fwArenaFree(&g_a, r1);
    TEST_ASSERT_NULL(fwArenaTakeWhole(&g_a, 32768u)); // one still live
    fwArenaFree(&g_a, r2);
    void *d = fwArenaTakeWhole(&g_a, 32768u);
    TEST_ASSERT_NOT_NULL(d);
    memset(d, 0x5A, 32768u); // the whole dictionary is writable
    TEST_ASSERT_FALSE(fwArenaTakeWhole(&g_a, 32768u) != nullptr); // already lent
}

void test_take_whole_need_larger_than_arena_refused(void)
{
    TEST_ASSERT_NULL(fwArenaTakeWhole(&g_a, ARENA_LEN + 1));
    // a refused take does not lend
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, REC));
}

void test_records_refused_while_lent_then_after_return(void)
{
    void *d = fwArenaTakeWhole(&g_a, 32768u);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, REC));
    TEST_ASSERT_FALSE(fwArenaReturnWhole(&g_a, g_buf + 16)); // wrong pointer
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, REC));
    TEST_ASSERT_TRUE(fwArenaReturnWhole(&g_a, d));
    TEST_ASSERT_FALSE(fwArenaReturnWhole(&g_a, d)); // not lent any more
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, REC));
}

void test_small_requests_never_served(void)
{
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, 0));
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, 1));
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, 512));
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, FW_ARENA_MIN_RECORD - 1));
    TEST_ASSERT_NOT_NULL(fwArenaAllocRecord(&g_a, FW_ARENA_MIN_RECORD));
    TEST_ASSERT_EQUAL_UINT(1, fwArenaLive(&g_a));
}

void test_slot_limit(void)
{
    void *p[FW_ARENA_SLOTS + 1];
    for (unsigned i = 0; i < FW_ARENA_SLOTS; i++)
    {
        p[i] = fwArenaAllocRecord(&g_a, FW_ARENA_MIN_RECORD);
        TEST_ASSERT_NOT_NULL(p[i]);
    }
    TEST_ASSERT_NULL(fwArenaAllocRecord(&g_a, FW_ARENA_MIN_RECORD)); // no slot left although room is
    for (unsigned i = 0; i < FW_ARENA_SLOTS; i++)
        for (unsigned j = i + 1; j < FW_ARENA_SLOTS; j++)
            TEST_ASSERT_FALSE(overlap(p[i], FW_ARENA_MIN_RECORD, p[j], FW_ARENA_MIN_RECORD));
}

void test_uninitialised_arena_serves_nothing(void)
{
    FwArena e;
    fwArenaInit(&e, nullptr, 0);
    TEST_ASSERT_NULL(fwArenaAllocRecord(&e, REC));
    TEST_ASSERT_NULL(fwArenaTakeWhole(&e, 1));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_two_records_fit_without_overlap);
    RUN_TEST(test_third_that_does_not_fit_is_null);
    RUN_TEST(test_slots_aligned_and_rounded);
    RUN_TEST(test_small_request_fills_gap_below_live_slot);
    RUN_TEST(test_oversize_is_null);
    RUN_TEST(test_free_then_realloc_reuses);
    RUN_TEST(test_mixed_sizes_first_fit);
    RUN_TEST(test_owns_inside_outside_edge);
    RUN_TEST(test_free_of_foreign_pointer_is_ignored);
    RUN_TEST(test_take_whole_refused_while_record_live);
    RUN_TEST(test_take_whole_granted_when_all_free);
    RUN_TEST(test_take_whole_need_larger_than_arena_refused);
    RUN_TEST(test_records_refused_while_lent_then_after_return);
    RUN_TEST(test_small_requests_never_served);
    RUN_TEST(test_slot_limit);
    RUN_TEST(test_uninitialised_arena_serves_nothing);
    return UNITY_END();
}
