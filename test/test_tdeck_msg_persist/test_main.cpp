// Native test for the T-Deck message staging policy (TD-20).
//
// persisted_msgs kept a RAM-only copy of every received message (~220 B internal
// heap each) although nothing reads it without T_DECK_SPIFFS; it starved WiFi.
//
//   pio test -e native -f test_tdeck_msg_persist

#include <unity.h>

#include <utility>
#include <vector>

#include <t-deck/msg_persist_policy.h>

void setUp(void)    {}
void tearDown(void) {}

void test_default_build_disabled(void)
{
    TEST_ASSERT_EQUAL_INT(0, TDECK_MSG_PERSIST);
}

void test_default_policy_stages_nothing(void)
{
    std::vector<std::pair<int, int>> v;
    for(int i = 0; i < 500; i++)
        msg_persist_stage(v, std::make_pair(i, i), 1000, TDECK_MSG_PERSIST != 0);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)v.size());
}

void test_enabled_keeps_newest_limit(void)
{
    std::vector<std::pair<int, int>> v;
    for(int i = 0; i < 500; i++)
        msg_persist_stage(v, std::make_pair(i, i), 50, true);
    TEST_ASSERT_EQUAL_UINT32(50, (uint32_t)v.size());
    TEST_ASSERT_EQUAL_INT(450, v.front().first);
    TEST_ASSERT_EQUAL_INT(499, v.back().first);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_build_disabled);
    RUN_TEST(test_default_policy_stages_nothing);
    RUN_TEST(test_enabled_keeps_newest_limit);
    return UNITY_END();
}
