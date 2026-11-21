// Host test for src/eth_dns.h (RAK-ETH-DNS-01, #1289): static-IP DNS choice of the nRF52 W5100S path.
//
//   pio test -e native_eth_dns

#include <unity.h>
#include <cstring>

#include "eth_dns.h"

void setUp(void) {}
void tearDown(void) {}

static void test_own_dns_wins_on_44(void)
{
    TEST_ASSERT_EQUAL_STRING("10.0.0.53", ethStaticDns("10.0.0.53", "44.1.2.3"));
}

static void test_own_dns_wins_on_private(void)
{
    TEST_ASSERT_EQUAL_STRING("10.0.0.53", ethStaticDns("10.0.0.53", "192.168.1.5"));
}

static void test_empty_dns_hamnet_rule(void)
{
    TEST_ASSERT_EQUAL_STRING("44.143.0.10", ethStaticDns("", "44.1.2.3"));
}

static void test_empty_dns_default_rule(void)
{
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("", "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("", ""));
}

static void test_null_inputs_fall_back(void)
{
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns(nullptr, nullptr));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns(nullptr, "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("44.143.0.10", ethStaticDns(nullptr, "44.1.2.3"));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("", nullptr));
}

static void test_unusable_dns_falls_back(void)
{
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("1.1.1", "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("dns.example.org", "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("300.1.1.1", "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", ethStaticDns("8.8.8.8 ", "192.168.1.5"));
    TEST_ASSERT_EQUAL_STRING("44.143.0.10", ethStaticDns("1.1.1", "44.1.2.3"));
}

static void test_returns_own_pointer(void)
{
    const char *d = "10.0.0.53";
    TEST_ASSERT_TRUE(ethStaticDns(d, "44.1.2.3") == d);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_own_dns_wins_on_44);
    RUN_TEST(test_own_dns_wins_on_private);
    RUN_TEST(test_empty_dns_hamnet_rule);
    RUN_TEST(test_empty_dns_default_rule);
    RUN_TEST(test_null_inputs_fall_back);
    RUN_TEST(test_unusable_dns_falls_back);
    RUN_TEST(test_returns_own_pointer);
    return UNITY_END();
}
