// Native Testsuite fuer den Server-Link-Status der Gateway-Anzeige
// (gw_link_status.h/.cpp, "Server: ..." in Web-Info und --info).
//
//   pio test -e native -f test_gw_link_status

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <gw_link_status.h>

#include <string>

#include "../support/repo_file.h"

static char out[160];

static void fmt(bool gateway, bool hasIp, uint32_t now)
{
    gwLinkFormat(out, sizeof(out), gateway, hasIp, now);
}

void setUp(void)
{
    gwLinkResetForTest();
    out[0] = 0;
}

void tearDown(void) {}

void test_gateway_off_wins(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    gwLinkNoteRx(1000);
    fmt(false, true, 2000);
    TEST_ASSERT_EQUAL_STRING("off", out);
}

void test_no_ip_without_path(void)
{
    fmt(true, false, 5000);
    TEST_ASSERT_EQUAL_STRING("no IP address", out);
}

void test_no_ip_with_path(void)
{
    gwLinkSetDest("hamnet", "srv.example");
    fmt(true, false, 5000);
    TEST_ASSERT_EQUAL_STRING("Hamnet, no IP address", out);
    gwLinkSetDest("inet", "srv.example");
    fmt(true, false, 5000);
    TEST_ASSERT_EQUAL_STRING("Internet, no IP address", out);
}

void test_not_selected(void)
{
    fmt(true, true, 5000);
    TEST_ASSERT_EQUAL_STRING("not selected", out);
}

void test_no_response_yet(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    fmt(true, true, 5000);
    TEST_ASSERT_EQUAL_STRING("Hamnet 44.1.2.3, no response yet", out);
}

void test_hamnet_connected_age_zero(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    gwLinkNoteRx(7000);
    fmt(true, true, 7000);
    TEST_ASSERT_EQUAL_STRING("Hamnet 44.1.2.3, connected (last rx 0 s)", out);
}

void test_connected_at_exactly_65000(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    gwLinkNoteRx(10000);
    fmt(true, true, 75000);
    TEST_ASSERT_EQUAL_STRING("Hamnet 44.1.2.3, connected (last rx 65 s)", out);
}

void test_no_response_at_65001(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    gwLinkNoteRx(10000);
    fmt(true, true, 75001);
    TEST_ASSERT_EQUAL_STRING("Hamnet 44.1.2.3, no response for 65 s", out);
}

void test_internet_label(void)
{
    gwLinkSetDest("inet", "srv.meshcom.example");
    gwLinkNoteRx(0);
    fmt(true, true, 12000);
    TEST_ASSERT_EQUAL_STRING("Internet srv.meshcom.example, connected (last rx 12 s)", out);
}

void test_unknown_path_verbatim(void)
{
    gwLinkSetDest("lte", "srv");
    fmt(true, true, 100);
    TEST_ASSERT_EQUAL_STRING("lte srv, no response yet", out);
}

void test_millis_wrap(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(0xFFFFF000u);
    fmt(true, true, 0x00001000u);
    TEST_ASSERT_EQUAL_STRING("Internet srv, connected (last rx 8 s)", out);
}

void test_new_dest_forgets_rx(void)
{
    gwLinkSetDest("hamnet", "a");
    gwLinkNoteRx(1000);
    fmt(true, true, 2000);
    TEST_ASSERT_NOT_NULL(strstr(out, "connected"));
    gwLinkSetDest("inet", "b");
    fmt(true, true, 2000);
    TEST_ASSERT_EQUAL_STRING("Internet b, no response yet", out);
}

void test_truncation_stays_terminated(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    char small[10];
    memset(small, 'X', sizeof(small));
    size_t n = gwLinkFormat(small, sizeof(small), true, true, 100);
    TEST_ASSERT_EQUAL_UINT(sizeof(small) - 1, n);
    TEST_ASSERT_EQUAL_CHAR(0, small[sizeof(small) - 1]);
    TEST_ASSERT_EQUAL_UINT(n, strlen(small));
    TEST_ASSERT_EQUAL_STRING_LEN("Hamnet 44.", small, 9);
}

void test_len_one_and_zero(void)
{
    gwLinkSetDest("hamnet", "h");
    char one[1] = {'X'};
    TEST_ASSERT_EQUAL_UINT(0, gwLinkFormat(one, 1, true, true, 0));
    TEST_ASSERT_EQUAL_CHAR(0, one[0]);
    TEST_ASSERT_EQUAL_UINT(0, gwLinkFormat(NULL, 0, true, true, 0));
}

void test_long_host_truncated_in_store(void)
{
    char host[200];
    memset(host, 'h', sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    gwLinkSetDest("inet", host);
    fmt(true, true, 0);
    TEST_ASSERT_EQUAL_UINT(strlen("Internet ") + 63 + strlen(", no response yet"), strlen(out));
}

void test_null_host_clears(void)
{
    gwLinkSetDest("hamnet", "44.1.2.3");
    gwLinkNoteRx(100);
    gwLinkSetDest(NULL, NULL);
    fmt(true, true, 200);
    TEST_ASSERT_EQUAL_STRING("not selected", out);
    fmt(true, false, 200);
    TEST_ASSERT_EQUAL_STRING("no IP address", out);
}

void test_alive_outage_1324(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 71000)); // 70 s ago: ISP down, router up
}

void test_alive_connected_30s(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_TRUE(gwLinkServerAlive(true, true, 31000));
}

void test_alive_boundary(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_TRUE(gwLinkServerAlive(true, true, 66000));
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 66001));
}

void test_alive_never_received(void)
{
    gwLinkSetDest("inet", "srv");
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 1000));
}

void test_alive_no_destination(void)
{
    gwLinkNoteRx(1000);
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 2000));
}

void test_alive_gateway_off(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_FALSE(gwLinkServerAlive(false, true, 2000));
}

void test_alive_no_ip(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, false, 2000));
}

void test_alive_millis_wrap(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(0xFFFFF000u);
    TEST_ASSERT_TRUE(gwLinkServerAlive(true, true, 0x00001000u));
}

void test_alive_stale_across_wrap(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(0xFFFFF000u);
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 0x0000F000u)); // ~65.5 s later
}

void test_alive_forgotten_on_reconnect(void)
{
    gwLinkSetDest("inet", "srv");
    gwLinkNoteRx(1000);
    TEST_ASSERT_TRUE(gwLinkServerAlive(true, true, 2000));
    gwLinkSetDest("hamnet", "srv2"); // reconnect / server switch
    TEST_ASSERT_FALSE(gwLinkServerAlive(true, true, 3000));
}

// #1324 call sites: the RF relay's msg_server flag and the phone "server
// reached" ACK must ask gwLinkServerAlive(), not hasIPaddress alone. The
// condition sits on the line(s) right above the marker comment/statement.
static void assert_site_uses_alive(const char *rel, const char *marker, int lines_back)
{
    const std::string src = repo_slurp(rel);
    const size_t at = src.find(marker);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, marker);

    size_t from = at;
    for (int i = 0; i <= lines_back && from > 0; i++)
    {
        from = src.rfind('\n', from - 1);
        if (from == std::string::npos)
        {
            from = 0;
            break;
        }
    }
    const std::string window = src.substr(from, at - from);
    TEST_ASSERT_TRUE_MESSAGE(window.find("gwLinkServerAlive(") != std::string::npos, rel);
    TEST_ASSERT_TRUE_MESSAGE(window.find("bGATEWAY && meshcom_settings.node_hasIPaddress") == std::string::npos, rel);
}

void test_site_relay_server_flag_1324(void)
{
    assert_site_uses_alive("src/lora_functions.cpp", "signal to another gateway not to send", 1);
}

void test_site_phone_server_ack_1324(void)
{
    assert_site_uses_alive("src/loop_functions.cpp", "set Info message send and Server reached", 2);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_gateway_off_wins);
    RUN_TEST(test_no_ip_without_path);
    RUN_TEST(test_no_ip_with_path);
    RUN_TEST(test_not_selected);
    RUN_TEST(test_no_response_yet);
    RUN_TEST(test_hamnet_connected_age_zero);
    RUN_TEST(test_connected_at_exactly_65000);
    RUN_TEST(test_no_response_at_65001);
    RUN_TEST(test_internet_label);
    RUN_TEST(test_unknown_path_verbatim);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_new_dest_forgets_rx);
    RUN_TEST(test_truncation_stays_terminated);
    RUN_TEST(test_len_one_and_zero);
    RUN_TEST(test_long_host_truncated_in_store);
    RUN_TEST(test_null_host_clears);
    RUN_TEST(test_alive_outage_1324);
    RUN_TEST(test_alive_connected_30s);
    RUN_TEST(test_alive_boundary);
    RUN_TEST(test_alive_never_received);
    RUN_TEST(test_alive_no_destination);
    RUN_TEST(test_alive_gateway_off);
    RUN_TEST(test_alive_no_ip);
    RUN_TEST(test_alive_millis_wrap);
    RUN_TEST(test_alive_stale_across_wrap);
    RUN_TEST(test_alive_forgotten_on_reconnect);
    RUN_TEST(test_site_relay_server_flag_1324);
    RUN_TEST(test_site_phone_server_ack_1324);
    return UNITY_END();
}
