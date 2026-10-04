// Native test for the Extern-UDP target check (EXT-03, src/extudp_target.h):
// a broadcast or multicast destination for the node-to-host JSON stream is
// refused, ordinary hosts and hostnames pass.
//
//   pio test -e native -f test_extudp_target
#include <unity.h>

#include <extudp_target.h>

void setUp(void)    {}
void tearDown(void) {}

// DK5EN-98's LAN: 192.168.68.63 in 192.168.68.0/22, broadcast 192.168.71.255
static const char *OWN  = "192.168.68.63";
static const char *MASK = "255.255.252.0";

static void test_limited_broadcast_refused(void)
{
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_LIMITED_BROADCAST, extudp_target_check("255.255.255.255", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_LIMITED_BROADCAST, extudp_target_check("255.255.255.255", "", ""));
}

static void test_directed_broadcast_of_own_subnet_refused(void)
{
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_DIRECTED_BROADCAST, extudp_target_check("192.168.71.255", OWN, MASK));
    // with a /24 the .255 of the own net is the broadcast
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_DIRECTED_BROADCAST, extudp_target_check("192.168.68.255", OWN, "255.255.255.0"));
}

static void test_dot255_inside_a_wider_subnet_is_a_host(void)
{
    // 192.168.68.255 is an ordinary host in /22
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("192.168.68.255", OWN, MASK));
}

static void test_multicast_unspecified_and_own_ip_refused(void)
{
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_MULTICAST, extudp_target_check("239.1.1.1", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_MULTICAST, extudp_target_check("224.0.0.1", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_UNSPECIFIED, extudp_target_check("0.0.0.0", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OWN_IP, extudp_target_check(OWN, OWN, MASK));
}

static void test_ordinary_hosts_and_hostnames_pass(void)
{
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("192.168.68.74", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("10.0.0.1", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("mcapp.local", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("", OWN, MASK));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check(nullptr, OWN, MASK));
}

static void test_unusable_mask_skips_the_directed_rule_only(void)
{
    // no mask, a non-contiguous mask or a /32: the limited broadcast is still
    // refused, the directed-broadcast rule cannot be applied
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("192.168.71.255", OWN, ""));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("192.168.71.255", OWN, "255.0.255.0"));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_OK, extudp_target_check("192.168.71.255", OWN, "255.255.255.255"));
    TEST_ASSERT_EQUAL(EXTUDP_TARGET_LIMITED_BROADCAST, extudp_target_check("255.255.255.255", OWN, "255.0.255.0"));
}

static void test_parser_is_strict(void)
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(extudp_parse_ipv4("192.168.68.63", v));
    TEST_ASSERT_EQUAL_HEX32(0xC0A8443Fu, v);
    TEST_ASSERT_FALSE(extudp_parse_ipv4("192.168.68", v));
    TEST_ASSERT_FALSE(extudp_parse_ipv4("192.168.68.256", v));
    TEST_ASSERT_FALSE(extudp_parse_ipv4("192.168.68.63.1", v));
    TEST_ASSERT_FALSE(extudp_parse_ipv4("192.168.68.63 ", v));
    TEST_ASSERT_FALSE(extudp_parse_ipv4("1.2.3.", v));
    TEST_ASSERT_FALSE(extudp_parse_ipv4("mcapp.local", v));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_limited_broadcast_refused);
    RUN_TEST(test_directed_broadcast_of_own_subnet_refused);
    RUN_TEST(test_dot255_inside_a_wider_subnet_is_a_host);
    RUN_TEST(test_multicast_unspecified_and_own_ip_refused);
    RUN_TEST(test_ordinary_hosts_and_hostnames_pass);
    RUN_TEST(test_unusable_mask_skips_the_directed_rule_only);
    RUN_TEST(test_parser_is_strict);
    return UNITY_END();
}
