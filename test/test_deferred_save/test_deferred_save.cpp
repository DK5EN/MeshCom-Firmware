// Native tests for deferredSaveDue() from src/deferred_save.h (QA-RX-02).
//
//   pio test -e native -f test_deferred_save

#include <unity.h>

#include <deferred_save.h>

void setUp(void)    {}
void tearDown(void) {}

static void test_not_dirty_is_never_due(void)
{
    TEST_ASSERT_FALSE(deferredSaveDue(false, 0, 0));
    TEST_ASSERT_FALSE(deferredSaveDue(false, 10000000UL, 0));
}

static void test_dirty_inside_window_is_not_due(void)
{
    TEST_ASSERT_FALSE(deferredSaveDue(true, 1000, 0));
    TEST_ASSERT_FALSE(deferredSaveDue(true, 599999UL, 0));
}

static void test_dirty_after_window_is_due(void)
{
    TEST_ASSERT_TRUE(deferredSaveDue(true, 600001UL, 0));
    TEST_ASSERT_TRUE(deferredSaveDue(true, 5000000UL, 1000));
}

static void test_exact_boundary_is_due(void)
{
    TEST_ASSERT_TRUE(deferredSaveDue(true, DEFERRED_SAVE_WINDOW_MS, 0));
    TEST_ASSERT_FALSE(deferredSaveDue(true, DEFERRED_SAVE_WINDOW_MS - 1, 0));
}

static void test_millis_rollover(void)
{
    // now wrapped below lastSave: 100 ms before wrap + 100 ms after = 200 ms
    TEST_ASSERT_FALSE(deferredSaveDue(true, 100UL, 0xFFFFFF9CUL));
    // 10 min after a save taken just before the wrap
    TEST_ASSERT_TRUE(deferredSaveDue(true, 600000UL - 100UL, 0xFFFFFF9CUL));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_not_dirty_is_never_due);
    RUN_TEST(test_dirty_inside_window_is_not_due);
    RUN_TEST(test_dirty_after_window_is_due);
    RUN_TEST(test_exact_boundary_is_due);
    RUN_TEST(test_millis_rollover);
    return UNITY_END();
}
