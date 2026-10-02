// Native suite for hey_policy.h (F6, docs/soak-20260928-impl-plan.md "Gateway
// flag design"): a gateway must not trickle-suppress its HEY once its last own
// HEY is at least TRICKLE_IMAX_S old; non-gateways keep plain RFC 6206.
//
//   pio test -e native -f test_hey_policy

#include <unity.h>

#include <stdint.h>

#include <hey_policy.h>

#define IMAX_MS (900UL * 1000UL)   // TRICKLE_IMAX_S = 900
#define K 2

void setUp(void) {}
void tearDown(void) {}

static void test_below_k_never_suppressed(void)
{
    TEST_ASSERT_FALSE(heyShouldSuppress(0, K, false, 0, IMAX_MS));
    TEST_ASSERT_FALSE(heyShouldSuppress(1, K, false, 0, IMAX_MS));
    TEST_ASSERT_FALSE(heyShouldSuppress(1, K, true, 0, IMAX_MS));
}

static void test_non_gateway_unchanged(void)
{
    // >= k consistent: suppressed regardless of how old the last own HEY is.
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, false, 0, IMAX_MS));
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, false, IMAX_MS - 1, IMAX_MS));
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, false, IMAX_MS, IMAX_MS));
    TEST_ASSERT_TRUE(heyShouldSuppress(K + 5, K, false, 0xFFFFFFFFu, IMAX_MS));
}

static void test_gateway_suppressed_while_last_hey_younger_than_imax(void)
{
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, true, 0, IMAX_MS));
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, true, IMAX_MS - 1, IMAX_MS));
}

static void test_gateway_not_suppressed_at_or_beyond_imax(void)
{
    TEST_ASSERT_FALSE(heyShouldSuppress(K, K, true, IMAX_MS, IMAX_MS));
    TEST_ASSERT_FALSE(heyShouldSuppress(K, K, true, IMAX_MS + 1, IMAX_MS));
    TEST_ASSERT_FALSE(heyShouldSuppress(K + 9, K, true, 3 * IMAX_MS, IMAX_MS));
}

static void test_never_sent_counts_as_infinitely_old(void)
{
    uint32_t since = heySinceLastOwn(123456u, 0u, false);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, since);
    TEST_ASSERT_FALSE(heyShouldSuppress(K, K, true, since, IMAX_MS));
    // A non-gateway that never sent is still suppressed (plain trickle).
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, false, since, IMAX_MS));
}

static void test_since_last_own_simple(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000u, heySinceLastOwn(5000u, 4000u, true));
    TEST_ASSERT_EQUAL_UINT32(0u, heySinceLastOwn(4000u, 4000u, true));
}

static void test_millis_wraparound(void)
{
    // last HEY 10 s before the 32-bit wrap, now 20 s after it: 30 s old.
    uint32_t last = 0xFFFFFFFFu - 10000u + 1u;   // 4294957296
    uint32_t now  = 20000u;
    uint32_t since = heySinceLastOwn(now, last, true);
    TEST_ASSERT_EQUAL_UINT32(30000u, since);
    TEST_ASSERT_TRUE(heyShouldSuppress(K, K, true, since, IMAX_MS));

    // 15 min after a HEY sent just before the wrap: not suppressed.
    now = (uint32_t)(last + IMAX_MS);
    since = heySinceLastOwn(now, last, true);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)IMAX_MS, since);
    TEST_ASSERT_FALSE(heyShouldSuppress(K, K, true, since, IMAX_MS));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_below_k_never_suppressed);
    RUN_TEST(test_non_gateway_unchanged);
    RUN_TEST(test_gateway_suppressed_while_last_hey_younger_than_imax);
    RUN_TEST(test_gateway_not_suppressed_at_or_beyond_imax);
    RUN_TEST(test_never_sent_counts_as_infinitely_old);
    RUN_TEST(test_since_last_own_simple);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
