// Unity tests for the central gateway server table (src/gw_servers.cpp).
// Doc: docs/gw-servers-unification.md section 3.5. Table cells are asserted
// as literals so a changed IP or host fails here.
#include <unity.h>
#include <string.h>
#include <stdint.h>

#include "gw_servers.h"

void setUp(void) {}
void tearDown(void) {}

static const char *ALL[] = {"OE", "DL", "IT", "HC", "HB"};
static const int NALL = 5;

static void eqIp(const uint8_t *exp, const uint8_t *got)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, got, 4);
}

static bool nonZero(const uint8_t *ip) { return (ip[0] | ip[1] | ip[2] | ip[3]) != 0; }

static GwTarget res(const char *code, GwNet net, const char *own = nullptr, bool resolver = true)
{
    GwTarget t;
    TEST_ASSERT_TRUE(gwResolve(code, net, own, resolver, &t));
    return t;
}

static void test_codes_order_and_count(void)
{
    char c[16][3];
    TEST_ASSERT_EQUAL_INT(NALL, gwCodes(c, 16));
    for (int i = 0; i < NALL; i++) TEST_ASSERT_EQUAL_STRING(ALL[i], c[i]);
    TEST_ASSERT_EQUAL_INT(2, gwCodes(c, 2));
    TEST_ASSERT_EQUAL_INT(0, gwCodes(c, 0));
}

static void test_codes_unique_two_chars_not_fr_us(void)
{
    char c[16][3];
    int n = gwCodes(c, 16);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(2, (int)strlen(c[i]));
        TEST_ASSERT_TRUE(c[i][0] >= 'A' && c[i][0] <= 'Z' && c[i][1] >= 'A' && c[i][1] <= 'Z');
        TEST_ASSERT_NOT_NULL(gwFind(c[i])->label);
        TEST_ASSERT_TRUE(strlen(gwFind(c[i])->label) > 0);
        for (int j = i + 1; j < n; j++) TEST_ASSERT_NOT_EQUAL(0, strcmp(c[i], c[j]));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(c[i], "FR"));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(c[i], "US"));
    }
}

static void test_find_unknown_empty_null_is_oe(void)
{
    TEST_ASSERT_EQUAL_STRING("OE", gwFind(nullptr)->code);
    TEST_ASSERT_EQUAL_STRING("OE", gwFind("")->code);
    TEST_ASSERT_EQUAL_STRING("OE", gwFind("D")->code);
    TEST_ASSERT_EQUAL_STRING("OE", gwFind("XX")->code);
    TEST_ASSERT_EQUAL_STRING("OE", gwFind("FR")->code);
    TEST_ASSERT_EQUAL_STRING("OE", gwFind("US")->code);
    TEST_ASSERT_EQUAL_STRING("DL", gwFind("DL")->code);
    TEST_ASSERT_EQUAL_STRING("HB", gwFind("HBX")->code); // only 2 chars compared
    GwTarget a = res(nullptr, GW_NET_INET), b = res("OE", GW_NET_INET), e = res("", GW_NET_HAMNET), o = res("OE", GW_NET_HAMNET);
    eqIp(b.srv_ip, a.srv_ip);
    eqIp(o.srv_ip, e.srv_ip);
}

static void test_all_rows_both_nets_resolve_one_step_to_direct(void)
{
    for (int i = 0; i < NALL; i++) {
        const GwServer *row = gwFind(ALL[i]);
        for (int n = 0; n < 2; n++) {
            GwTarget t;
            TEST_ASSERT_TRUE_MESSAGE(gwResolve(ALL[i], (GwNet)n, nullptr, true, &t), ALL[i]);
            TEST_ASSERT_TRUE(nonZero(t.srv_ip));
            TEST_ASSERT_TRUE(nonZero(t.ntp_ip));
            TEST_ASSERT_TRUE(t.ntp_host == nullptr || strlen(t.ntp_host) > 0);
            TEST_ASSERT_TRUE(t.path != nullptr);
            // redirection target is DIRECT after at most one step
            const GwSide *s = &row->side[n];
            if (s->mode == GWM_OTHER_NET) {
                TEST_ASSERT_EQUAL(GWM_DIRECT, row->side[1 - n].mode);
            } else if (s->mode == GWM_VIA) {
                const GwServer *v = gwFind(s->via);
                TEST_ASSERT_EQUAL_STRING(s->via, v->code);
                TEST_ASSERT_EQUAL(GWM_DIRECT, v->side[n].mode);
            }
            // path matches the network really used
            bool ham = strcmp(t.path, "hamnet") == 0;
            TEST_ASSERT_TRUE(ham || strcmp(t.path, "inet") == 0);
            if (s->mode == GWM_OTHER_NET) TEST_ASSERT_EQUAL((int)(n == GW_NET_INET), (int)ham);
            else TEST_ASSERT_EQUAL((int)(n == GW_NET_HAMNET), (int)ham);
        }
    }
}

static void test_path_cases(void)
{
    TEST_ASSERT_EQUAL_STRING("inet", res("IT", GW_NET_HAMNET).path);
    TEST_ASSERT_EQUAL_STRING("inet", res("IT", GW_NET_INET).path);
    TEST_ASSERT_EQUAL_STRING("hamnet", res("DL", GW_NET_HAMNET).path);
    TEST_ASSERT_EQUAL_STRING("inet", res("HC", GW_NET_INET).path);
    TEST_ASSERT_EQUAL_STRING("hamnet", res("HC", GW_NET_HAMNET).path);
}

static void test_other_net_resolves_inet_endpoints(void)
{
    GwTarget h = res("IT", GW_NET_HAMNET), i = res("IT", GW_NET_INET);
    eqIp(i.srv_ip, h.srv_ip);
    eqIp(i.ntp_ip, h.ntp_ip);
    TEST_ASSERT_EQUAL_STRING("meshcom.dig-italia.it", h.srv_host);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", h.ntp_host);
}

static void test_via_resolves_target_row(void)
{
    GwTarget d = res("DL", GW_NET_HAMNET), hc = res("HC", GW_NET_HAMNET);
    eqIp(hc.srv_ip, d.srv_ip);
    eqIp(hc.ntp_ip, d.ntp_ip);
    GwTarget h = res("HC", GW_NET_INET), dl = res("DL", GW_NET_INET);
    eqIp(dl.srv_ip, h.srv_ip);
    TEST_ASSERT_EQUAL_STRING("meshcom.hamnet.network", h.srv_host);
}

static void expectTarget(const char *code, GwNet net, const char *shost, const uint8_t *sip,
                         const char *nhost, const uint8_t *nip)
{
    GwTarget t = res(code, net);
    if (shost) TEST_ASSERT_EQUAL_STRING(shost, t.srv_host); else TEST_ASSERT_NULL(t.srv_host);
    if (nhost) TEST_ASSERT_EQUAL_STRING(nhost, t.ntp_host); else TEST_ASSERT_NULL(t.ntp_host);
    eqIp(sip, t.srv_ip);
    eqIp(nip, t.ntp_ip);
}

static void test_table_values_every_cell(void)
{
    static const uint8_t pool[4] = {162, 159, 200, 1};
    static const uint8_t oeI[4] = {89, 185, 97, 38}, oeH[4] = {44, 143, 8, 143}, oeN[4] = {44, 143, 0, 9};
    static const uint8_t dlI[4] = {192, 68, 17, 26};
    static const uint8_t itI[4] = {145, 239, 75, 155};
    static const uint8_t hcH[4] = {44, 148, 230, 197}, hcN[4] = {44, 148, 224, 123};
    static const uint8_t hbI[4] = {157, 143, 4, 44}, hbH[4] = {44, 142, 42, 59}, hbN[4] = {44, 142, 163, 2};

    expectTarget("OE", GW_NET_INET, "meshcom.oevsv.at", oeI, "pool.ntp.org", pool);
    expectTarget("OE", GW_NET_HAMNET, nullptr, oeH, nullptr, oeN);
    expectTarget("DL", GW_NET_INET, "meshcom.hamnet.network", dlI, "pool.ntp.org", pool);
    expectTarget("DL", GW_NET_HAMNET, nullptr, hcH, nullptr, hcN);
    expectTarget("IT", GW_NET_INET, "meshcom.dig-italia.it", itI, "pool.ntp.org", pool);
    expectTarget("IT", GW_NET_HAMNET, "meshcom.dig-italia.it", itI, "pool.ntp.org", pool);
    expectTarget("HC", GW_NET_INET, "meshcom.hamnet.network", dlI, "pool.ntp.org", pool);
    expectTarget("HC", GW_NET_HAMNET, nullptr, hcH, nullptr, hcN);
    expectTarget("HB", GW_NET_INET, "meshcom.emcomm.ch", hbI, "pool.ntp.org", pool);
    expectTarget("HB", GW_NET_HAMNET, nullptr, hbH, nullptr, hbN);

    // table structure
    TEST_ASSERT_EQUAL(GWM_VIA, gwFind("DL")->side[GW_NET_HAMNET].mode);
    TEST_ASSERT_EQUAL_STRING("HC", gwFind("DL")->side[GW_NET_HAMNET].via);
    TEST_ASSERT_EQUAL(GWM_VIA, gwFind("HC")->side[GW_NET_INET].mode);
    TEST_ASSERT_EQUAL_STRING("DL", gwFind("HC")->side[GW_NET_INET].via);
    TEST_ASSERT_EQUAL(GWM_OTHER_NET, gwFind("IT")->side[GW_NET_HAMNET].mode);
    TEST_ASSERT_EQUAL(GWM_DIRECT, gwFind("OE")->side[GW_NET_INET].mode);
    TEST_ASSERT_EQUAL(GWM_DIRECT, gwFind("OE")->side[GW_NET_HAMNET].mode);
    TEST_ASSERT_EQUAL(GWM_DIRECT, gwFind("HB")->side[GW_NET_HAMNET].mode);
}

static void test_ownntp_ipv4_literal(void)
{
    static const uint8_t exp[4] = {10, 1, 2, 3};
    for (int r = 0; r < 2; r++) {
        GwTarget t = res("OE", GW_NET_INET, "10.1.2.3", r == 1);
        TEST_ASSERT_NULL(t.ntp_host);
        eqIp(exp, t.ntp_ip);
        TEST_ASSERT_FALSE(t.ownntp_ignored);
        GwTarget h = res("HB", GW_NET_HAMNET, "10.1.2.3", r == 1);
        TEST_ASSERT_NULL(h.ntp_host);
        eqIp(exp, h.ntp_ip);
    }
}

static void test_ownntp_hostname(void)
{
    static const uint8_t pool[4] = {162, 159, 200, 1};
    const char *own = "time.example.org";
    GwTarget a = res("OE", GW_NET_INET, own, true);
    TEST_ASSERT_TRUE(a.ntp_host == own);
    eqIp(pool, a.ntp_ip);
    TEST_ASSERT_FALSE(a.ownntp_ignored);
    GwTarget b = res("OE", GW_NET_INET, own, false);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", b.ntp_host);
    eqIp(pool, b.ntp_ip);
    TEST_ASSERT_TRUE(b.ownntp_ignored);
    // not a valid IPv4 even though it looks numeric
    GwTarget c = res("OE", GW_NET_INET, "300.1.2.3", false);
    TEST_ASSERT_TRUE(c.ownntp_ignored);
    GwTarget d = res("OE", GW_NET_INET, "1.2.3.4.5", false);
    TEST_ASSERT_TRUE(d.ownntp_ignored);
}

static void test_ownntp_too_short_ignored(void)
{
    static const uint8_t oeN[4] = {44, 143, 0, 9};
    GwTarget t = res("OE", GW_NET_HAMNET, "1.2.3", true); // 5 chars
    TEST_ASSERT_NULL(t.ntp_host);
    eqIp(oeN, t.ntp_ip);
    TEST_ASSERT_FALSE(t.ownntp_ignored);
    GwTarget u = res("OE", GW_NET_HAMNET, "", true);
    eqIp(oeN, u.ntp_ip);
    GwTarget v = res("OE", GW_NET_HAMNET, "abcdef", true); // 6 chars
    TEST_ASSERT_NULL(v.ntp_host);
    eqIp(oeN, v.ntp_ip);
    GwTarget w = res("OE", GW_NET_HAMNET, "1.1.1.1", false); // exactly 7
    static const uint8_t one[4] = {1, 1, 1, 1};
    eqIp(one, w.ntp_ip);
}

static void test_resolve_null_out(void)
{
    TEST_ASSERT_FALSE(gwResolve("OE", GW_NET_INET, nullptr, true, nullptr));
}

static void test_select_net(void)
{
    const uint8_t ham[4] = {44, 143, 1, 1}, inet[4] = {192, 168, 1, 5}, none[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL(GW_NET_INET, gwSelectNet(0, ham));
    TEST_ASSERT_EQUAL(GW_NET_INET, gwSelectNet(0, inet));
    TEST_ASSERT_EQUAL(GW_NET_HAMNET, gwSelectNet(1, ham));
    TEST_ASSERT_EQUAL(GW_NET_HAMNET, gwSelectNet(1, inet));
    TEST_ASSERT_EQUAL(GW_NET_HAMNET, gwSelectNet(2, ham));
    TEST_ASSERT_EQUAL(GW_NET_INET, gwSelectNet(2, inet));
    TEST_ASSERT_EQUAL(GW_NET_INET, gwSelectNet(2, none));
    TEST_ASSERT_EQUAL(GW_NET_HAMNET, gwSelectNet(7, ham));
    TEST_ASSERT_EQUAL(GW_NET_INET, gwSelectNet(7, inet));
    TEST_ASSERT_EQUAL(GW_NET_HAMNET, gwSelectNet(255, ham));
}

static void test_resolve_node_mode_and_address(void)
{
    const uint8_t ham[4] = {44, 143, 1, 1}, inet[4] = {192, 168, 1, 5};
    GwTarget t;
    TEST_ASSERT_TRUE(gwResolveNode("OE", 0, ham, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("inet", t.path);
    TEST_ASSERT_EQUAL_STRING("meshcom.oevsv.at", t.srv_host);
    TEST_ASSERT_TRUE(gwResolveNode("OE", 1, inet, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("hamnet", t.path);
    TEST_ASSERT_NULL(t.srv_host);
    TEST_ASSERT_TRUE(gwResolveNode("OE", 2, ham, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("hamnet", t.path);
    TEST_ASSERT_TRUE(gwResolveNode("OE", 2, inet, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("inet", t.path);
    TEST_ASSERT_TRUE(gwResolveNode("OE", 7, ham, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("hamnet", t.path);
    TEST_ASSERT_TRUE(gwResolveNode("OE", 7, inet, nullptr, true, &t));
    TEST_ASSERT_EQUAL_STRING("inet", t.path);
}

static void test_resolve_node_end_to_end(void)
{
    static const uint8_t pool[4] = {162, 159, 200, 1};
    static const uint8_t oeI[4] = {89, 185, 97, 38}, oeH[4] = {44, 143, 8, 143}, oeN[4] = {44, 143, 0, 9};
    static const uint8_t dlI[4] = {192, 68, 17, 26};
    static const uint8_t itI[4] = {145, 239, 75, 155};
    static const uint8_t hcH[4] = {44, 148, 230, 197}, hcN[4] = {44, 148, 224, 123};
    static const uint8_t hbI[4] = {157, 143, 4, 44}, hbH[4] = {44, 142, 42, 59}, hbN[4] = {44, 142, 163, 2};
    const uint8_t ip[4] = {10, 0, 0, 1};
    struct C { const char *code; uint8_t mode; const char *path, *sh; const uint8_t *sip; const char *nh; const uint8_t *nip; };
    const C cs[] = {
        {"OE", 0, "inet", "meshcom.oevsv.at", oeI, "pool.ntp.org", pool},
        {"OE", 1, "hamnet", nullptr, oeH, nullptr, oeN},
        {"DL", 0, "inet", "meshcom.hamnet.network", dlI, "pool.ntp.org", pool},
        {"DL", 1, "hamnet", nullptr, hcH, nullptr, hcN},
        {"IT", 0, "inet", "meshcom.dig-italia.it", itI, "pool.ntp.org", pool},
        {"IT", 1, "inet", "meshcom.dig-italia.it", itI, "pool.ntp.org", pool},
        {"HC", 0, "inet", "meshcom.hamnet.network", dlI, "pool.ntp.org", pool},
        {"HC", 1, "hamnet", nullptr, hcH, nullptr, hcN},
        {"HB", 0, "inet", "meshcom.emcomm.ch", hbI, "pool.ntp.org", pool},
        {"HB", 1, "hamnet", nullptr, hbH, nullptr, hbN},
    };
    for (const C &c : cs) {
        GwTarget t;
        TEST_ASSERT_TRUE(gwResolveNode(c.code, c.mode, ip, nullptr, true, &t));
        TEST_ASSERT_EQUAL_STRING(c.path, t.path);
        if (c.sh) TEST_ASSERT_EQUAL_STRING(c.sh, t.srv_host); else TEST_ASSERT_NULL(t.srv_host);
        if (c.nh) TEST_ASSERT_EQUAL_STRING(c.nh, t.ntp_host); else TEST_ASSERT_NULL(t.ntp_host);
        eqIp(c.sip, t.srv_ip);
        eqIp(c.nip, t.ntp_ip);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_codes_order_and_count);
    RUN_TEST(test_codes_unique_two_chars_not_fr_us);
    RUN_TEST(test_find_unknown_empty_null_is_oe);
    RUN_TEST(test_all_rows_both_nets_resolve_one_step_to_direct);
    RUN_TEST(test_path_cases);
    RUN_TEST(test_other_net_resolves_inet_endpoints);
    RUN_TEST(test_via_resolves_target_row);
    RUN_TEST(test_table_values_every_cell);
    RUN_TEST(test_ownntp_ipv4_literal);
    RUN_TEST(test_ownntp_hostname);
    RUN_TEST(test_ownntp_too_short_ignored);
    RUN_TEST(test_resolve_null_out);
    RUN_TEST(test_select_net);
    RUN_TEST(test_resolve_node_mode_and_address);
    RUN_TEST(test_resolve_node_end_to_end);
    return UNITY_END();
}
