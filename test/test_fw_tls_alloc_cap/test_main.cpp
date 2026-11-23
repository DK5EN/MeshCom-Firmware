// FORMAL-01 mbedTLS #2: the cap decision of src/fw_tls_alloc_cap.h.
#include <unity.h>
#include <stdint.h>
#include "fw_tls_alloc_cap.h"

void setUp(void) {}
void tearDown(void) {}

static bool allow(uint32_t live, size_t n, size_t sz)
{
    size_t t;
    return fwTlsCapAllow(live, n, sz, FW_TLS_CAP_PER_ALLOC, FW_TLS_CAP_SESSION, &t);
}

void test_record_buffer_passes(void) { TEST_ASSERT_TRUE(allow(0, 1, 16717)); }
void test_per_alloc_cap_edge(void)
{
    TEST_ASSERT_TRUE(allow(0, 1, FW_TLS_CAP_PER_ALLOC));
    TEST_ASSERT_FALSE(allow(0, 1, FW_TLS_CAP_PER_ALLOC + 1));
}
void test_hostile_cert_size_refused(void) { TEST_ASSERT_FALSE(allow(0, 1, 1u << 20)); }
void test_n_times_size_counts(void)
{
    TEST_ASSERT_TRUE(allow(0, 64, 256));
    TEST_ASSERT_FALSE(allow(0, 128, 256));
}
void test_overflow_refused(void)
{
    size_t t = 1;
    TEST_ASSERT_FALSE(fwTlsCapAllow(0, (size_t)-1, 2, (size_t)-1, 0xFFFFFFFFu, &t));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)t);
}
void test_session_total_edge(void)
{
    TEST_ASSERT_TRUE(allow(FW_TLS_CAP_SESSION - 1000, 1, 1000));
    TEST_ASSERT_FALSE(allow(FW_TLS_CAP_SESSION - 1000, 1, 1001));
    TEST_ASSERT_FALSE(allow(FW_TLS_CAP_SESSION, 1, 1));
}
void test_live_above_total_refused(void) { TEST_ASSERT_FALSE(allow(0xFFFFFFFFu, 1, 1)); }
void test_zero_size_ok(void) { TEST_ASSERT_TRUE(allow(0, 0, 8)); }

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_record_buffer_passes);
    RUN_TEST(test_per_alloc_cap_edge);
    RUN_TEST(test_hostile_cert_size_refused);
    RUN_TEST(test_n_times_size_counts);
    RUN_TEST(test_overflow_refused);
    RUN_TEST(test_session_total_edge);
    RUN_TEST(test_live_above_total_refused);
    RUN_TEST(test_zero_size_ok);
    return UNITY_END();
}
