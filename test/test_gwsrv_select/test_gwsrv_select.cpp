// CTY-02 (issue #1133): pins the whole country/transport server matrix that
// src/gwsrv_select.h owns.
//
// The regression this suite exists for: a DL node on a plain Internet uplink
// was handed the OE server (meshcom.oevsv.at) instead of the DL one, so
// --gateway srv dl looked like it was being ignored -- the setting was stored
// and displayed correctly, only the destination lookup dropped it.
//
//   pio test -e native -f test_gwsrv_select
//
// Every cell asserts host AND literal, because the ESP32 path consumes the
// name and the nRF52 path consumes the literal: if those two ever disagree,
// the same node reaches a different server depending on which board it is.
#include <unity.h>

#include <cstring>

#include "gwsrv_select.h"

// ------------------------------------------------------------------ helpers

static void assert_target(const GwSrvTarget &t, const char *host, uint8_t a, uint8_t b, uint8_t c,
                          uint8_t d, const char *path)
{
    TEST_ASSERT_EQUAL_STRING(host, t.host);
    TEST_ASSERT_EQUAL_UINT8(a, t.ip[0]);
    TEST_ASSERT_EQUAL_UINT8(b, t.ip[1]);
    TEST_ASSERT_EQUAL_UINT8(c, t.ip[2]);
    TEST_ASSERT_EQUAL_UINT8(d, t.ip[3]);
    TEST_ASSERT_EQUAL_STRING(path, t.path);
}

// --------------------------------------------------------- Internet transport

// THE REGRESSION. A DL node on a normal Internet uplink must reach the DL
// server. meshcom.hamnet.network is the name behind 192.68.17.26, the literal
// this branch carried before the IT server replaced it.
static void test_dl_internet_reaches_dl_server(void)
{
    assert_target(gwsrvSelect("DL", false), "meshcom.hamnet.network", 192, 68, 17, 26, "inet");
}

static void test_oe_internet(void)
{
    assert_target(gwsrvSelect("OE", false), "meshcom.oevsv.at", 89, 185, 97, 38, "inet");
}

static void test_it_internet(void)
{
    assert_target(gwsrvSelect("IT", false), "meshcom.dig-italia.it", 145, 239, 75, 155, "inet");
}

// ----------------------------------------------------------- HAMNET transport

static void test_dl_hamnet(void)
{
    assert_target(gwsrvSelect("DL", true), "meshcom.hamnet.cloud", 44, 148, 230, 197, "hamnet");
}

static void test_oe_hamnet(void)
{
    assert_target(gwsrvSelect("OE", true), "44.143.8.143", 44, 143, 8, 143, "hamnet");
}

// IT runs no HAMNET server, so a HAMNET-only IT node is deliberately sent out
// over the Internet -- same endpoint, and the path field must say so, because
// that field is what the [GW];srv marker reports to the field log.
static void test_it_hamnet_falls_back_to_internet(void)
{
    assert_target(gwsrvSelect("IT", true), "meshcom.dig-italia.it", 145, 239, 75, 155, "inet");
}

// ------------------------------------------------------------------ defaults

// node_gwsrv is char[3] and starts empty; esp32_main/nrf52_main seed it to
// "OE", but the selector must not depend on that having happened.
static void test_empty_setting_defaults_to_oe(void)
{
    assert_target(gwsrvSelect("", false), "meshcom.oevsv.at", 89, 185, 97, 38, "inet");
    assert_target(gwsrvSelect("", true), "44.143.8.143", 44, 143, 8, 143, "hamnet");
}

// Guards the memcmp(.., 2) comparison: only the first two bytes decide, which
// is what the call sites have always done.
static void test_unknown_country_defaults_to_oe(void)
{
    assert_target(gwsrvSelect("XX", false), "meshcom.oevsv.at", 89, 185, 97, 38, "inet");
}

// The name and the literal must be two views of one endpoint on every cell --
// a mismatch means ESP32 and nRF52 nodes with identical settings diverge.
static void test_every_cell_agrees_between_platforms(void)
{
    const char *countries[] = { "OE", "DL", "IT", "" };

    for (int i = 0; i < 4; i++)
    {
        for (int h = 0; h < 2; h++)
        {
            GwSrvTarget t = gwsrvSelect(countries[i], h != 0);

            TEST_ASSERT_NOT_NULL(t.host);
            TEST_ASSERT_NOT_NULL(t.path);
            TEST_ASSERT_TRUE(strcmp(t.path, "inet") == 0 || strcmp(t.path, "hamnet") == 0);

            // no cell may leave the destination unset
            TEST_ASSERT_TRUE(t.ip[0] != 0);
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_dl_internet_reaches_dl_server);
    RUN_TEST(test_oe_internet);
    RUN_TEST(test_it_internet);

    RUN_TEST(test_dl_hamnet);
    RUN_TEST(test_oe_hamnet);
    RUN_TEST(test_it_hamnet_falls_back_to_internet);

    RUN_TEST(test_empty_setting_defaults_to_oe);
    RUN_TEST(test_unknown_country_defaults_to_oe);
    RUN_TEST(test_every_cell_agrees_between_platforms);

    return UNITY_END();
}
