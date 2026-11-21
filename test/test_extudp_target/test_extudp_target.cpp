// Native test for the Extern-UDP target check (EXT-03, src/extudp_target.h):
// a broadcast or multicast destination for the node-to-host JSON stream is
// refused, ordinary hosts and hostnames pass.
//
//   pio test -e native -f test_extudp_target
#include <unity.h>

#include <extudp_target.h>
#include <extudp_refresh.h>
#include <mcpout_refusal.h>

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

// extudp_refresh_decide(requested, bEXTUDP, socketOpen, mayOpenHere)
//   mayOpenHere: this platform opens the socket from the refresh itself (nRF52 with Ethernet up, where the only
//   other restart is the 15-minute web timer); ESP32 restarts every pass and passes false.
static void test_peer_refresh_decision(void)
{
    // nothing requested: nothing happens, whatever the state
    for(int on = 0; on < 2; on++)
        for(int open = 0; open < 2; open++)
            for(int here = 0; here < 2; here++)
                TEST_ASSERT_EQUAL(EXTUDP_REFRESH_NONE, extudp_refresh_decide(false, on, open, here));
    // socket open: reset (re-resolve apip)
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_RESET, extudp_refresh_decide(true, true, true, false));
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_RESET, extudp_refresh_decide(true, true, true, true));
    // EXTUDP off: never reach the socket code, drop the request (also an open socket cannot exist then)
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_DROP, extudp_refresh_decide(true, false, true,  true));
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_DROP, extudp_refresh_decide(true, false, false, true));
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_DROP, extudp_refresh_decide(true, false, false, false));
}

// RAK outage: --extudpip <refused>|none closed the socket, the next valid --extudpip must open it now, not at the
// next 15-minute web tick. Only where the platform may do so (Ethernet up, N-20: never on an uninitialised W5100S).
static void test_peer_refresh_opens_a_closed_socket_where_allowed(void)
{
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_START, extudp_refresh_decide(true, true, false, true));
    // Ethernet not up (or ESP32, which restarts itself every pass): forget it, never touch the socket
    TEST_ASSERT_EQUAL(EXTUDP_REFRESH_DROP,  extudp_refresh_decide(true, true, false, false));
}

// ETH-06 (DB0AIS 2026-11-14): a W5100S hardware reset wipes every socket in the chip. The EXTUDP socket stayed
// latched "open", the next datagram went to a socket the chip no longer had, and socketSendUDP() waited forever for
// SEND_OK/TIMEOUT: main loop dead for 52 h. Before the reset the latch must drop (no send on the stale socket), and the
// socket must come back as soon as Ethernet has an address again -- not at the 15-minute web tick.
static void test_chip_reset_drops_open_socket(void)
{
    bool lost = false;
    TEST_ASSERT_TRUE(extudp_on_chip_reset(true, lost));    // open socket: the latch must be dropped
    TEST_ASSERT_TRUE(lost);
    bool lost2 = false;
    TEST_ASSERT_FALSE(extudp_on_chip_reset(false, lost2)); // nothing open (boot, EXTUDP off): nothing to drop
    TEST_ASSERT_FALSE(lost2);
    bool lost3 = true;                                     // a second reset before the reopen keeps the request
    TEST_ASSERT_FALSE(extudp_on_chip_reset(false, lost3));
    TEST_ASSERT_TRUE(lost3);
}

static void test_chip_reset_reopen_decision(void)
{
    // reopen only for a socket the reset took, with EXTUDP on, the socket closed and Ethernet up
    TEST_ASSERT_TRUE(extudp_reopen_after_chip_reset(true, true, false, true));
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(true, true, false, false));  // no IP yet: N-20, wait
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(true, true, true, true));    // already reopened
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(true, false, false, true));  // EXTUDP switched off meanwhile
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(false, true, false, true));  // closed for another reason
}

// The DB0AIS sequence: socket open, DHCP fails twice, hardware reset, DHCP succeeds, next EXTUDP pass.
static void test_chip_reset_sequence_never_sends_on_stale_socket(void)
{
    bool socketOpen = true, lost = false;
    if(extudp_on_chip_reset(socketOpen, lost))
        socketOpen = false;                   // what externUdpChipReset() does with hasExternIPaddress
    TEST_ASSERT_FALSE(socketOpen);            // every send path gates on it: no datagram to the stale socket
    bool ethUp = false;
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(lost, true, socketOpen, ethUp));
    ethUp = true;                             // dhcp_acquired_late
    TEST_ASSERT_TRUE(extudp_reopen_after_chip_reset(lost, true, socketOpen, ethUp));
    socketOpen = true; lost = false;          // startExternUDP() latched and cleared the request
    TEST_ASSERT_FALSE(extudp_reopen_after_chip_reset(lost, true, socketOpen, ethUp));
}

// WEB-PIN-01 (web GUI mcpout<P>): validate the port first, refuse an input pin
// without running --setout. io bit set == OUTPUT (command_functions.cpp --setout).
static void test_mcpout_refusal(void)
{
    uint16_t bm = 0;
    TEST_ASSERT_EQUAL(MCPOUT_PORT_OK, mcpout_check_port("A0", 0x0001, bm));
    TEST_ASSERT_EQUAL_HEX16(0x0001, bm);
    TEST_ASSERT_EQUAL(MCPOUT_PORT_OK, mcpout_check_port("B7", 0x8000, bm));
    TEST_ASSERT_EQUAL_HEX16(0x8000, bm);
    TEST_ASSERT_EQUAL(MCPOUT_PORT_OK, mcpout_check_port("B0", 0x0100, bm));
    TEST_ASSERT_EQUAL_HEX16(0x0100, bm);
    // pin configured as input: refused
    TEST_ASSERT_EQUAL(MCPOUT_PORT_IS_INPUT, mcpout_check_port("A0", 0x0000, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_IS_INPUT, mcpout_check_port("A3", 0xFFF7, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_IS_INPUT, mcpout_check_port("B2", 0x00FF, bm));
    // a neighbouring output bit does not count
    TEST_ASSERT_EQUAL(MCPOUT_PORT_IS_INPUT, mcpout_check_port("A1", 0x0001, bm));
    // malformed ports are invalid, never input/output
    TEST_ASSERT_EQUAL(MCPOUT_PORT_INVALID, mcpout_check_port("A8", 0xFFFF, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_INVALID, mcpout_check_port("C0", 0xFFFF, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_INVALID, mcpout_check_port("A", 0xFFFF, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_INVALID, mcpout_check_port("A00", 0xFFFF, bm));
    TEST_ASSERT_EQUAL(MCPOUT_PORT_INVALID, mcpout_check_port("", 0xFFFF, bm));
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
    RUN_TEST(test_peer_refresh_decision);
    RUN_TEST(test_peer_refresh_opens_a_closed_socket_where_allowed);
    RUN_TEST(test_chip_reset_drops_open_socket);
    RUN_TEST(test_chip_reset_reopen_decision);
    RUN_TEST(test_chip_reset_sequence_never_sends_on_stale_socket);
    RUN_TEST(test_mcpout_refusal);
    return UNITY_END();
}
