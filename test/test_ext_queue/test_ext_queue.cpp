// F2-14 regression: writer of the deferred sendExtern ring must check `used`
// before touching the slot (specs/tla/extqueue.tla Fix="check").
// Define EXTQ_OLD_WRITER to run against the pre-fix writer (never reads used):
// test_full_ring_not_overwritten then fails.
#include <unity.h>
#include <atomic>
#include <string.h>
#include <stdint.h>
#include "ext_queue_core.h"

struct Slot { uint8_t buf[8]; std::atomic<bool> used{false}; };
static Slot q[2];
static int wr;
static uint32_t drops;

#ifdef EXTQ_OLD_WRITER
template <typename F> static bool put(F f) {
    Slot *e = &q[wr]; f(e); e->used.store(true, std::memory_order_release);
    wr = (wr + 1) % 2; return true;
}
#else
template <typename F> static bool put(F f) { return extQueueWrite(q, 2, wr, drops, f); }
#endif

void setUp() { for (auto &s : q) { memset(s.buf, 0, 8); s.used = false; } wr = 0; drops = 0; }
void tearDown() {}

static bool putId(uint8_t id) { return put([&](Slot *s) { memset(s->buf, id, 8); }); }

void test_fill_two_then_ok() {
    TEST_ASSERT_TRUE(putId(1)); TEST_ASSERT_TRUE(putId(2));
    TEST_ASSERT_EQUAL(0, wr); TEST_ASSERT_EQUAL_UINT32(0, drops);
}
void test_full_ring_not_overwritten() {
    putId(1); putId(2);
    TEST_ASSERT_FALSE(putId(3));                 // burst frame 3: ring full
    TEST_ASSERT_EQUAL_UINT8(1, q[0].buf[0]);     // slot 0 still frame 1, untouched
    TEST_ASSERT_EQUAL_UINT8(1, q[0].buf[7]);
    TEST_ASSERT_EQUAL_UINT32(1, drops);          // counted, not silent
    TEST_ASSERT_EQUAL(0, wr);                    // wr not advanced on drop
}
void test_burst_of_five() {
    for (uint8_t i = 1; i <= 5; i++) putId(i);
    TEST_ASSERT_EQUAL_UINT32(3, drops);
}
void test_reader_frees_slot() {
    putId(1); putId(2); putId(3);
    q[0].used.store(false);                      // flush consumed slot 0
    TEST_ASSERT_TRUE(putId(4));
    TEST_ASSERT_EQUAL_UINT8(4, q[0].buf[0]);
    TEST_ASSERT_EQUAL(1, wr);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fill_two_then_ok); RUN_TEST(test_full_ring_not_overwritten);
    RUN_TEST(test_burst_of_five); RUN_TEST(test_reader_frees_slot);
    return UNITY_END();
}
