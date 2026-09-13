// Native suite for the message-id high-water-mark scheme (msgid_counter.h).
//
// The property under test is not "the arithmetic is right", it is **an id is
// never handed out twice across an unclean shutdown**. That is why the last
// two cases simulate reboots at the worst moment rather than checking
// individual return values: a scheme that looks correct function by function
// can still reuse ids, and that is exactly the mistake this file exists to
// keep out (the first draft of the scheme persisted only at multiples of the
// step and skipped the write-back at load -- it reused a whole step's worth
// of ids after a crash in the first frames of a boot).
//
//   pio test -e native -f test_msgid_counter

#include <unity.h>

#include <set>
#include <vector>

#include <msgid_counter.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// The pieces
// ---------------------------------------------------------------------------

static void test_advance_increments_and_wraps_at_999(void)
{
    TEST_ASSERT_EQUAL_INT(1, msgIdAdvance(0));
    TEST_ASSERT_EQUAL_INT(500, msgIdAdvance(499));
    TEST_ASSERT_EQUAL_INT(999, msgIdAdvance(998));
    TEST_ASSERT_EQUAL_INT(0, msgIdAdvance(999));
}

static void test_advance_folds_a_corrupt_value_into_range(void)
{
    // node_msgid is a plain int in both platform structs and is decoded from
    // flash; a value outside 0..999 must not reach a frame.
    TEST_ASSERT_EQUAL_INT(0, msgIdAdvance(-1));
    TEST_ASSERT_EQUAL_INT(1, msgIdAdvance(1000));
    TEST_ASSERT_EQUAL_INT(2, msgIdAdvance(123001));
    for (int corrupt : {-987654, -1000, -1, 1000, 55555})
    {
        const int next = msgIdAdvance(corrupt);
        TEST_ASSERT_TRUE(next >= 0 && next <= kMsgIdMax);
    }
}

static void test_persist_points_are_the_multiples_of_the_step(void)
{
    int persists = 0;
    for (int i = 0; i <= kMsgIdMax; i++)
    {
        if (msgIdNeedsPersist(i))
        {
            TEST_ASSERT_EQUAL_INT(0, i % kMsgIdPersistStep);
            persists++;
        }
    }
    // Exactly one write per step over a full cycle -- this is the wear claim.
    TEST_ASSERT_EQUAL_INT((kMsgIdMax + 1) / kMsgIdPersistStep, persists);
}

static void test_after_load_moves_one_whole_step_ahead(void)
{
    TEST_ASSERT_EQUAL_INT(kMsgIdPersistStep, msgIdAfterLoad(0));
    TEST_ASSERT_EQUAL_INT(300, msgIdAfterLoad(200));
    // Wraps with the counter rather than running past it.
    TEST_ASSERT_EQUAL_INT(0, msgIdAfterLoad(kMsgIdMax + 1 - kMsgIdPersistStep));
    // A corrupt stored value still yields an in-range start.
    TEST_ASSERT_EQUAL_INT(kMsgIdPersistStep - 1, msgIdAfterLoad(-1));
    TEST_ASSERT_EQUAL_INT(kMsgIdPersistStep, msgIdAfterLoad(kMsgIdMax + 1));
}

// ---------------------------------------------------------------------------
// The property: no id is ever handed out twice, however the node dies
// ---------------------------------------------------------------------------

// Models a node exactly as the firmware drives it: the current value goes on
// the wire, then the counter advances, then it is persisted if the new value
// is a persist point. `crash_after` frames are sent, then power is lost --
// whatever was last persisted is all that survives.
struct Node
{
    int live = 0;
    int stored = 0;

    void boot()
    {
        live = msgIdAfterLoad(stored);
        stored = live; // the mandatory write-back at load
    }

    int send()
    {
        const int used = live;
        live = msgIdAdvance(live);
        if (msgIdNeedsPersist(live))
        {
            stored = live;
        }
        return used;
    }
};

static void test_no_id_is_reused_across_a_crash_at_any_point(void)
{
    // For every possible crash point inside the first two blocks after a
    // boot, the ids used before the crash and the ids used after the next
    // boot must not overlap.
    for (int crash_after = 0; crash_after <= 2 * kMsgIdPersistStep; crash_after++)
    {
        Node n;
        n.stored = 0;

        n.boot();
        std::set<int> before;
        for (int i = 0; i < crash_after; i++)
        {
            before.insert(n.send());
        }

        // Power loss: `live` is gone, `stored` survives.
        Node after;
        after.stored = n.stored;
        after.boot();
        for (int i = 0; i < kMsgIdPersistStep; i++)
        {
            const int id = after.send();
            if (before.count(id) != 0)
            {
                char msg[96];
                snprintf(msg, sizeof(msg), "id %d reused after a crash at frame %d", id, crash_after);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_writes_drop_by_the_step_over_a_long_run(void)
{
    Node n;
    n.boot();

    int writes = 0;
    int last_stored = n.stored;
    const int frames = 10000;
    for (int i = 0; i < frames; i++)
    {
        n.send();
        if (n.stored != last_stored)
        {
            writes++;
            last_stored = n.stored;
        }
    }

    // One per step, not one per frame. The old behaviour was `frames`.
    TEST_ASSERT_EQUAL_INT(frames / kMsgIdPersistStep, writes);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_advance_increments_and_wraps_at_999);
    RUN_TEST(test_advance_folds_a_corrupt_value_into_range);
    RUN_TEST(test_persist_points_are_the_multiples_of_the_step);
    RUN_TEST(test_after_load_moves_one_whole_step_ahead);
    RUN_TEST(test_no_id_is_reused_across_a_crash_at_any_point);
    RUN_TEST(test_writes_drop_by_the_step_over_a_long_run);
    return UNITY_END();
}
