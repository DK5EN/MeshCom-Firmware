// TD-22: message tab cap, LRU eviction victim and active-index fix-up.
#include <unity.h>
#include <cstdint>
#include <t-deck/msg_tab_policy.h>

void setUp(void) {}
void tearDown(void) {}

static void test_below_cap_no_victim(void)
{
    uint32_t s[3] = {5, 1, 9};
    TEST_ASSERT_EQUAL_UINT32(MSG_TAB_NPOS, msg_tab_pick_victim(s, 3, 0, MSG_TAB_MAX_TABS));
}

static void test_lru_inactive_victim(void)
{
    uint32_t s[16];
    for(int i = 0; i < 16; i++) s[i] = 100 + i;
    s[7] = 3;
    TEST_ASSERT_EQUAL_UINT32(7, msg_tab_pick_victim(s, 16, 0, MSG_TAB_MAX_TABS));
}

static void test_active_never_victim(void)
{
    uint32_t s[16];
    for(int i = 0; i < 16; i++) s[i] = 100 + i;
    s[4] = 1;  // oldest overall, but active
    s[9] = 50; // oldest inactive
    TEST_ASSERT_EQUAL_UINT32(9, msg_tab_pick_victim(s, 16, 4, MSG_TAB_MAX_TABS));
}

static void test_tie_lowest_index(void)
{
    uint32_t s[16];
    for(int i = 0; i < 16; i++) s[i] = 10;
    TEST_ASSERT_EQUAL_UINT32(1, msg_tab_pick_victim(s, 16, 0, MSG_TAB_MAX_TABS));
    TEST_ASSERT_EQUAL_UINT32(0, msg_tab_pick_victim(s, 16, 5, MSG_TAB_MAX_TABS));
}

static void test_only_active_not_evictable(void)
{
    uint32_t s[1] = {1};
    TEST_ASSERT_EQUAL_UINT32(MSG_TAB_NPOS, msg_tab_pick_victim(s, 1, 0, 1));
}

static void test_erase_left_of_active(void)
{
    TEST_ASSERT_EQUAL_INT(2, msg_tab_index_after_erase(3, 1, 5));
}

static void test_erase_at_active(void)
{
    TEST_ASSERT_EQUAL_INT(3, msg_tab_index_after_erase(3, 3, 5));
}

static void test_erase_at_active_last(void)
{
    // active was last of 5; after erase 4 remain
    TEST_ASSERT_EQUAL_INT(3, msg_tab_index_after_erase(4, 4, 4));
}

static void test_erase_right_of_active(void)
{
    TEST_ASSERT_EQUAL_INT(1, msg_tab_index_after_erase(1, 3, 5));
}

static void test_erase_last_tab(void)
{
    TEST_ASSERT_EQUAL_INT(-1, msg_tab_index_after_erase(0, 0, 0));
}

static void test_trim_lru_inactive_with_bubbles(void)
{
    size_t c[4] = {5, 5, 5, 5};
    uint32_t s[4] = {10, 3, 7, 1}; // idx 3 oldest but active
    TEST_ASSERT_EQUAL_UINT32(1, msg_tab_pick_trim(c, s, 4, 3, MSG_ACTIVE_MIN_KEEP));
}

static void test_trim_skips_empty_tabs(void)
{
    size_t c[4] = {5, 0, 5, 5};
    uint32_t s[4] = {10, 1, 7, 8};
    TEST_ASSERT_EQUAL_UINT32(2, msg_tab_pick_trim(c, s, 4, 3, MSG_ACTIVE_MIN_KEEP));
}

static void test_trim_active_fallback_above_min(void)
{
    size_t c[3] = {0, 11, 0};
    uint32_t s[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT32(1, msg_tab_pick_trim(c, s, 3, 1, MSG_ACTIVE_MIN_KEEP));
}

static void test_trim_npos_when_nothing(void)
{
    size_t c[3] = {0, MSG_ACTIVE_MIN_KEEP, 0};
    uint32_t s[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT32(MSG_TAB_NPOS, msg_tab_pick_trim(c, s, 3, 1, MSG_ACTIVE_MIN_KEEP));
    TEST_ASSERT_EQUAL_UINT32(MSG_TAB_NPOS, msg_tab_pick_trim(c, s, 0, -1, MSG_ACTIVE_MIN_KEEP));
}

static void test_view_excess_no_hint(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, msg_view_excess(11, false, 10));
    TEST_ASSERT_EQUAL_UINT32(0, msg_view_excess(10, false, 10));
    TEST_ASSERT_EQUAL_UINT32(0, msg_view_excess(3, false, 10));
    TEST_ASSERT_EQUAL_UINT32(25, msg_view_excess(50, false, 25));
}

static void test_view_excess_hint_not_counted(void)
{
    // hint label + 10 bubbles with target 10: nothing to remove
    TEST_ASSERT_EQUAL_UINT32(0, msg_view_excess(11, true, 10));
    TEST_ASSERT_EQUAL_UINT32(1, msg_view_excess(12, true, 10));
    TEST_ASSERT_EQUAL_UINT32(0, msg_view_excess(0, true, 0));
    TEST_ASSERT_EQUAL_UINT32(0, msg_view_excess(1, true, 0));
}

static void test_utf8_align_skips_continuation(void)
{
    // "a\xC3\xA4b" = a, U+00E4 (2 bytes), b; cutting at 2 lands on the continuation byte
    const char *s = "a\xC3\xA4" "b";
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)msg_utf8_align(s, 4, 2));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)msg_utf8_align(s, 4, 1));
    TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)msg_utf8_align(s, 4, 4));
}

static void test_system_body_keep_from(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, msg_system_body_keep_from(0));
    TEST_ASSERT_EQUAL_UINT32(0, msg_system_body_keep_from(MSG_SYSTEM_BODY_MAX));
    TEST_ASSERT_EQUAL_UINT32(1, msg_system_body_keep_from(MSG_SYSTEM_BODY_MAX + 1));
    TEST_ASSERT_EQUAL_UINT32(976, msg_system_body_keep_from(2000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_below_cap_no_victim);
    RUN_TEST(test_lru_inactive_victim);
    RUN_TEST(test_active_never_victim);
    RUN_TEST(test_tie_lowest_index);
    RUN_TEST(test_only_active_not_evictable);
    RUN_TEST(test_erase_left_of_active);
    RUN_TEST(test_erase_at_active);
    RUN_TEST(test_erase_at_active_last);
    RUN_TEST(test_erase_right_of_active);
    RUN_TEST(test_erase_last_tab);
    RUN_TEST(test_trim_lru_inactive_with_bubbles);
    RUN_TEST(test_trim_skips_empty_tabs);
    RUN_TEST(test_trim_active_fallback_above_min);
    RUN_TEST(test_trim_npos_when_nothing);
    RUN_TEST(test_view_excess_no_hint);
    RUN_TEST(test_view_excess_hint_not_counted);
    RUN_TEST(test_system_body_keep_from);
    RUN_TEST(test_utf8_align_skips_continuation);
    return UNITY_END();
}
