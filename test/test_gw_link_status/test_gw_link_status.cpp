// Native Testsuite fuer den Server-Link-Status der Gateway-Anzeige
// (gw_link_status.h/.cpp, "Server: ..." in Web-Info und --info).
//
//   pio test -e native -f test_gw_link_status

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <gw_link_status.h>

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
    return UNITY_END();
}
