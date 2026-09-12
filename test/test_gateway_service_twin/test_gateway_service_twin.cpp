// Twin test for the gateway service block (test plan U10, C4 carve-out).
// docs/testplan/drift-matrix.csv rows DR-14 (TX drain condition) and DR-15
// (recovery policy) are decided `both-valid` -- DOCUMENTED NON-CHANGES, not
// bugs. There is no correctness claim to assert here. The risk this suite
// exists for is REINTRODUCTION: two independent readers have already read
// the nRF52 drain condition as a bug and proposed "fixing" it -- the
// original audit's OPT-D5, and the M2 drift-matrix row -- and a source
// comment (src/gateway_service.h) has failed to stop that twice. This suite
// is the instrument that does not depend on someone reading a comment
// first: it pins the call-count/order parity, so a "unifying" change fails a
// test instead of shipping.
//
// ONE binary, like the U1/U2 twins, not two runs compared through files: the
// two functions are named differently (gatewayService_esp32() /
// gatewayService_nrf52()), so both translation units link together and the
// comparison is a real differential in one process.
//
//   pio test -e native_gateway_twin -f test_gateway_service_twin
//
// WHAT IS STUBBED AND WHY, at a glance (see the stub headers themselves for
// the full reasoning):
//   stubs/udp_functions.h   real one gates ntpHarvestUDP() behind
//                            #if defined(ESP32), which this native build
//                            never sets
//   stubs/nrf_eth.h          real NrfETH drags the W5100S driver; five
//                            members gatewayService_nrf52() touches that the
//                            U1/U2 twins' own nrf_eth.h stub does not carry
//   stubs/WiFi.h             the Arduino-ESP32 WiFi library; only
//                            WiFi.status()/WL_CONNECTED is read
//   stubs/SPI.h,
//   stubs/RAK13800_W5100S.h  empty shims, nothing in the gateway service
//                            touches them directly (carried over from the
//                            nRF52 file's own #include chain)
//
// The shared settings shim test/support/nrf52/WisBlock-API.h gained four
// fields for this suite (node_last_upd_timer, node_ownip, node_owngw,
// node_ownms) -- extended, not rewritten; see the comment there.
//
// Every stub RECORDS its calls into one ordered log (g_calls) plus per-name
// counters (see record()/count_of() below), so a test can assert not just
// outcomes but call sequences.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>
#include <map>

#include <Arduino.h>
#include <IPAddress.h>
#include <configuration.h>

#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <gateway_service.h>

#include <udp_functions.h>   // stub: getMeshComUDP/sendMeshComUDP/sendMeshComHeartbeat/
                             // resetMeshComUDP/ntpHarvestUDP
#include <nrf_eth.h>         // stub: NrfETH
#include <WiFi.h>            // stub: WiFiClass/WL_CONNECTED

// ---------------------------------------------------------------------------
// File-scope state the two functions extern. Types copied verbatim from the
// top of each carved .cpp -- an ODR mismatch here would be a linker-invisible
// bug, same discipline test_udp_frame_twin.cpp/test_udp_send_twin.cpp use.
// ---------------------------------------------------------------------------

// esp32/gateway_service_esp32.cpp:13-14
bool hb_warn_logged = false;
unsigned long last_upd_timer = 0;

// nrf52/gateway_service_nrf52.cpp:14-15 (sendUDP()/startRadioReceive() are
// forward-declared by the .cpp itself at lines 16-17, so this TU does not
// redeclare them -- only defines their bodies, below).
NrfETH neth;
unsigned long iReceiveTimeOutTime = 0;

// loop_functions_extern.h globals neither .cpp defines (no loop_functions.cpp
// linked in this env's build_src_filter -- only the two carved files are).
bool bGATEWAY = false;
unsigned long hb_timer = 0;
volatile bool bSPI_ETH_Active = false;
volatile bool bPendingRadioRx = false;
bool bDEBUG = false;

s_meshcom_settings meshcom_settings;

WiFiClass WiFi;

String getTimeString() { return String("00:00:00"); }

// ---------------------------------------------------------------------------
// Call log: ordered record plus per-name counters, so a test can assert
// sequence ("startRadioReceive() after the eth access releases the SPI
// guard") as well as plain counts.
// ---------------------------------------------------------------------------

static std::vector<std::string> g_calls;
static std::map<std::string, int> g_counts;

static void record(const char *name)
{
    g_calls.emplace_back(name);
    g_counts[name]++;
}

static int count_of(const char *name)
{
    auto it = g_counts.find(name);
    return it == g_counts.end() ? 0 : it->second;
}

// Index of the first occurrence of `name` in g_calls, or -1.
static int index_of(const char *name)
{
    for (size_t i = 0; i < g_calls.size(); i++)
        if (g_calls[i] == name)
            return (int)i;
    return -1;
}

// Test-controlled: neth.getUDP()'s return value for the next call(s).
// 1 == "no udp-paket received" (the real comment's own words, verbatim from
// gateway_service_nrf52.cpp:32); 0 == a packet was received.
static int g_mock_getUDP_return = 1;

// Captured at the moment NrfETH::getUDP() runs, so a test can check the SPI
// guard was actually held (true) DURING the Ethernet access, not just
// released afterwards.
static bool g_spi_active_during_getUDP = false;

// ---------------------------------------------------------------------------
// Recording sinks -- ESP32 side (stubs/udp_functions.h declarations).
// ---------------------------------------------------------------------------

void getMeshComUDP() { record("esp_getMeshComUDP"); }
void sendMeshComUDP() { record("esp_sendMeshComUDP"); }
void sendMeshComHeartbeat() { record("esp_sendMeshComHeartbeat"); }
void resetMeshComUDP() { record("esp_resetMeshComUDP"); }
void ntpHarvestUDP() { record("esp_ntpHarvestUDP"); }

wl_status_t WiFiClass::status()
{
    record("esp_WiFi_status");
    return mock_connected ? WL_CONNECTED : WL_DISCONNECTED;
}

// ---------------------------------------------------------------------------
// Recording sinks -- nRF52 side (stubs/nrf_eth.h declarations, plus the two
// free functions gateway_service_nrf52.cpp forward-declares itself).
// ---------------------------------------------------------------------------

int NrfETH::getUDP()
{
    record("nrf_getUDP");
    g_spi_active_during_getUDP = bSPI_ETH_Active;
    return g_mock_getUDP_return;
}

void NrfETH::harvestNTP() { record("nrf_harvestNTP"); }
int NrfETH::resetDHCP() { record("nrf_resetDHCP"); return 0; }
void NrfETH::initethfixIP() { record("nrf_initethfixIP"); }

void sendUDP() { record("nrf_sendUDP"); }
void startRadioReceive() { record("nrf_startRadioReceive"); }

// ---------------------------------------------------------------------------
// Reset. Every test starts from an identical, empty world.
// ---------------------------------------------------------------------------

static void recorder_reset()
{
    g_calls.clear();
    g_counts.clear();
    g_mock_getUDP_return = 1;
    g_spi_active_during_getUDP = false;

    hb_warn_logged = false;
    last_upd_timer = 0;

    neth = NrfETH();
    iReceiveTimeOutTime = 0;

    bGATEWAY = false;
    hb_timer = 0;
    bSPI_ETH_Active = false;
    bPendingRadioRx = false;
    bDEBUG = false;

    memset(&meshcom_settings, 0, sizeof(meshcom_settings));

    WiFi = WiFiClass();

    Serial.clear();
    mc_test_set_millis(0);
}

void setUp(void) { recorder_reset(); }
void tearDown(void) {}

// ===========================================================================
// DR-14: TX drain condition. The row this suite exists for.
// ===========================================================================

static void test_dr14_esp32_drains_every_pass_regardless_of_rx(void)
{
    // ESP32: getMeshComUDP() and sendMeshComUDP() sit in one straight-line
    // scope with no conditional between them (gateway_service_esp32.cpp:22)
    // -- there is no "packet received" outcome ESP32 even models separately.
    // Run two passes; both calls must fire every single time, proving
    // sendMeshComUDP() is not gated on anything getMeshComUDP() observed.
    bGATEWAY = true;
    meshcom_settings.node_hasIPaddress = true;

    mc_test_set_millis(1000);
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("esp_getMeshComUDP"),
        "DR-14: esp32 must call getMeshComUDP() every bGATEWAY-on pass "
        "(docs/testplan/drift-matrix.csv DR-14)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("esp_sendMeshComUDP"),
        "DR-14: esp32 must call sendMeshComUDP() every bGATEWAY-on pass, "
        "unconditionally -- this is the ESP32 half of the DR-14 asymmetry "
        "the nRF52 case below pins the other half of");

    mc_test_set_millis(2000);
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, count_of("esp_getMeshComUDP"),
        "DR-14: esp32 skipped getMeshComUDP() on a later pass");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, count_of("esp_sendMeshComUDP"),
        "DR-14: esp32 skipped sendMeshComUDP() on a later pass -- if this "
        "reads 1, sendMeshComUDP() became conditional on something, and the "
        "DR-14 asymmetry with nRF52 (below) needs re-examining");
}

static void test_dr14_nrf52_sends_only_when_no_packet_received(void)
{
    // nRF52: sendUDP() lives INSIDE the `getUDP() == 1` ("no packet") branch
    // (gateway_service_nrf52.cpp:32-35) -- the opposite of ESP32's
    // unconditional call above. This is deliberate, not a bug: on RAK4631 the
    // W5100S and SX1262 share the SPI bus, gatewayService_nrf52() holds
    // bSPI_ETH_Active across the Ethernet access and defers the radio
    // re-arm (bPendingRadioRx -> startRadioReceive()); draining TX on every
    // RX pass would lengthen the window in which the radio cannot be
    // re-armed, and a late radio misses LoRa frames. If this test starts
    // failing because sendUDP() now fires on both passes, that is exactly
    // the "unify with ESP32" mutation docs/testplan/drift-matrix.csv DR-14
    // documents as a non-change -- see src/gateway_service.h and the DR-14
    // row before "fixing" this.
    bGATEWAY = true;
    neth.hasIPaddress = true;

    // Pass 1: no packet received.
    g_mock_getUDP_return = 1;
    gatewayService_nrf52();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_getUDP"),
        "DR-14: nrf52 must call getUDP() every bGATEWAY-on pass with an IP");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_sendUDP"),
        "DR-14: nrf52 must call sendUDP() when getUDP() reported no packet "
        "(docs/testplan/drift-matrix.csv DR-14)");

    // Pass 2: a packet WAS received.
    g_mock_getUDP_return = 0;
    gatewayService_nrf52();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, count_of("nrf_getUDP"),
        "DR-14: nrf52 skipped getUDP() on the packet-received pass");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_sendUDP"),
        "DR-14 REINTRODUCTION: nrf52 called sendUDP() on a pass where a "
        "packet WAS received -- this is the exact 'drain unconditionally, "
        "like ESP32' change two independent readers have already proposed "
        "(the original audit's OPT-D5, and the M2 drift-matrix row) and "
        "src/gateway_service.h documents as deliberately NOT done: it would "
        "lengthen the window bSPI_ETH_Active holds the SPI bus away from the "
        "radio. See docs/testplan/drift-matrix.csv row DR-14 before changing "
        "this call site");
}

static void test_dr14_nrf52_spi_guard_and_deferred_radio_rearm(void)
{
    // The SPI guard itself: bSPI_ETH_Active must be true DURING the
    // Ethernet access (captured inside the NrfETH::getUDP() sink above) and
    // false again once gatewayService_nrf52() returns. A pending radio
    // re-arm must fire, and only AFTER the guard is released.
    bGATEWAY = true;
    neth.hasIPaddress = true;
    bPendingRadioRx = true;
    g_mock_getUDP_return = 1;

    gatewayService_nrf52();

    TEST_ASSERT_TRUE_MESSAGE(g_spi_active_during_getUDP,
        "DR-14: bSPI_ETH_Active must be held true across the Ethernet "
        "access (nrf52 shares the SPI bus between the W5100S and the "
        "SX1262)");
    TEST_ASSERT_FALSE_MESSAGE(bSPI_ETH_Active,
        "DR-14: bSPI_ETH_Active must be released before gatewayService_nrf52() "
        "returns -- a guard left held would starve every other SPI user, "
        "not just the deferred radio re-arm checked below");
    TEST_ASSERT_FALSE_MESSAGE(bPendingRadioRx,
        "DR-14: a pending radio re-arm must be cleared once serviced");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_startRadioReceive"),
        "DR-14: a pending radio re-arm must call startRadioReceive() once "
        "the SPI guard releases");

    int i_release = -1; // no direct log entry for the guard release itself,
                         // so order is checked via the two calls that bracket
                         // it: getUDP() (guard held) must precede
                         // startRadioReceive() (guard released).
    (void)i_release;
    int i_get = index_of("nrf_getUDP");
    int i_rearm = index_of("nrf_startRadioReceive");
    TEST_ASSERT_TRUE_MESSAGE(i_get >= 0 && i_rearm >= 0 && i_get < i_rearm,
        "DR-14: startRadioReceive() must be called AFTER getUDP()/the SPI "
        "guard release, not before or interleaved");
}

// ===========================================================================
// DR-15: recovery policy parity. Both platforms recover on link-down; both
// take NO action when the link is up but the server is silent. This is the
// row whose original premise was wrong (the drift-matrix's earlier reading
// of it) -- this test is what stops it being re-filed as a real drift.
// ===========================================================================

static void test_dr15_esp32_recovery_on_link_down_not_on_silent_server(void)
{
    bGATEWAY = true;
    meshcom_settings.node_hasIPaddress = true;
    hb_timer = 0;
    last_upd_timer = 1000;       // last time we heard from the server
    mc_test_set_millis(41000);   // hb_age = 40000ms: > HB_WARN_TIME (35s),
                                  // < MAX_HB_RX_TIME (65s) -- stage 1 only,
                                  // and >= HEARTBEAT_INTERVAL (30s) so the
                                  // whole heartbeat block runs this pass

    // Link down: WiFi.status() != WL_CONNECTED.
    WiFi.mock_connected = false;
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("esp_resetMeshComUDP"),
        "DR-15: esp32 must reset on a silent server when WiFi itself is "
        "down (docs/testplan/drift-matrix.csv DR-15)");

    // Link up, server just silent: WiFi.status() == WL_CONNECTED.
    recorder_reset();
    bGATEWAY = true;
    meshcom_settings.node_hasIPaddress = true;
    hb_timer = 0;
    last_upd_timer = 1000;
    mc_test_set_millis(41000);
    WiFi.mock_connected = true;
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("esp_resetMeshComUDP"),
        "DR-15 REINTRODUCTION: esp32 reset the UDP session while WiFi is "
        "still connected -- the documented policy is 'wait, the server is "
        "unresponsive, not the link' (docs/testplan/drift-matrix.csv DR-15); "
        "resetting here is the premise the row's own history got wrong "
        "once already");
}

static void test_dr15_nrf52_recovery_on_link_down_not_on_silent_server(void)
{
    // Link down: neth.hasIPaddress == false. Recovery fields
    // (node_ownip/node_ownms/node_owngw) are left at their zeroed reset
    // default (each <= 6 chars), so the recovery branch taken is
    // resetDHCP() -- WHICH of the two nRF52 recovery calls fires is DR-16's
    // question, not this test's; only that recovery fires at all, here.
    bGATEWAY = true;
    neth.hasIPaddress = false;
    neth.last_upd_timer = 0;
    mc_test_set_millis(66000);   // >= MAX_HB_RX_TIME (65s)

    gatewayService_nrf52();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_resetDHCP"),
        "DR-15: nrf52 must attempt recovery when the link itself is down "
        "(docs/testplan/drift-matrix.csv DR-15)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("nrf_initethfixIP"),
        "sanity: reset defaults (empty node_ownip/ownms/owngw) should take "
        "the resetDHCP() branch, not initethfixIP() -- if this fails, the "
        "settings shim's new fields did not reset the way this test assumes");

    // Link up, server just silent: neth.hasIPaddress == true. The recovery
    // block's own `if(!neth.hasIPaddress)` gate (gateway_service_nrf52.cpp:66)
    // is the ONLY thing standing between "server silent" and "recovery
    // fires" here -- this is exactly the gate a well-meaning "always retry
    // when the timer expires" edit would delete.
    recorder_reset();
    bGATEWAY = true;
    neth.hasIPaddress = true;
    neth.last_upd_timer = 0;
    mc_test_set_millis(66000);
    g_mock_getUDP_return = 1;   // irrelevant to recovery, only to DR-14's
                                // sendUDP() gate on this same pass

    gatewayService_nrf52();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("nrf_resetDHCP"),
        "DR-15 REINTRODUCTION: nrf52 attempted DHCP recovery while the link "
        "is still up -- the documented policy is 'wait, the server is "
        "unresponsive, not the link' (docs/testplan/drift-matrix.csv DR-15)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("nrf_initethfixIP"),
        "DR-15 REINTRODUCTION: nrf52 attempted fixed-IP recovery while the "
        "link is still up -- see DR-15 above");
}

// ===========================================================================
// Cheap agreement pins picked up along the way (not padding: each one is a
// change a later "cleanup" could plausibly make).
// ===========================================================================

static void test_agreement_bgateway_off_only_harvests_ntp_on_both(void)
{
    // TM-45, both platforms: bGATEWAY off + an IP present means the socket
    // is still read, but only via the harvest-only path -- never the full
    // gateway receive (getMeshComUDP()/getUDP()), so there is no double read
    // of the same socket in one loop pass.
    bGATEWAY = false;
    meshcom_settings.node_hasIPaddress = true;
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("esp_ntpHarvestUDP"),
        "TM-45: esp32 must harvest NTP replies while bGATEWAY is off");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("esp_getMeshComUDP"),
        "TM-45: esp32 must not run the full gateway receive while bGATEWAY is off");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("esp_sendMeshComUDP"),
        "TM-45: esp32 must not drain TX while bGATEWAY is off");

    recorder_reset();
    bGATEWAY = false;
    neth.hasIPaddress = true;
    gatewayService_nrf52();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("nrf_harvestNTP"),
        "TM-45: nrf52 must harvest NTP replies while bGATEWAY is off");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("nrf_getUDP"),
        "TM-45: nrf52 must not run the full gateway receive while bGATEWAY is off");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("nrf_sendUDP"),
        "TM-45: nrf52 must not drain TX while bGATEWAY is off");

    // bGATEWAY off AND no IP: neither branch's else-if condition holds, so
    // nothing at all runs this pass, on either side.
    recorder_reset();
    bGATEWAY = false;
    meshcom_settings.node_hasIPaddress = false;
    gatewayService_esp32();
    TEST_ASSERT_TRUE_MESSAGE(g_calls.empty(),
        "esp32: bGATEWAY off with no IP must call nothing this pass");

    recorder_reset();
    bGATEWAY = false;
    neth.hasIPaddress = false;
    gatewayService_nrf52();
    TEST_ASSERT_TRUE_MESSAGE(g_calls.empty(),
        "nrf52: bGATEWAY off with no IP must call nothing this pass");
}

static void test_drift_esp32_heartbeat_interval_gate_nrf52_has_none(void)
{
    // ESP32-only: sendMeshComHeartbeat() is gated by
    // `(millis() - hb_timer) >= HEARTBEAT_INTERVAL*1000` -- it does not fire
    // every pass, only once the interval has elapsed. nRF52's
    // gatewayService_nrf52() has no periodic heartbeat SEND call at all (it
    // only ever checks the RX side, last_upd_timer vs MAX_HB_RX_TIME) --
    // there is no equivalent call to compare against, so this is pinned as
    // an ESP32-only behaviour rather than a two-sided agreement/drift case.
    bGATEWAY = true;
    meshcom_settings.node_hasIPaddress = true;
    hb_timer = 0;

    mc_test_set_millis(1000);   // 1s elapsed: well under HEARTBEAT_INTERVAL (30s)
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_of("esp_sendMeshComHeartbeat"),
        "esp32 sent a heartbeat before HEARTBEAT_INTERVAL elapsed");

    mc_test_set_millis(31000);  // 30s since hb_timer was last set (0)
    gatewayService_esp32();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_of("esp_sendMeshComHeartbeat"),
        "esp32 did not send a heartbeat once HEARTBEAT_INTERVAL elapsed");
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_dr14_esp32_drains_every_pass_regardless_of_rx);
    RUN_TEST(test_dr14_nrf52_sends_only_when_no_packet_received);
    RUN_TEST(test_dr14_nrf52_spi_guard_and_deferred_radio_rearm);

    RUN_TEST(test_dr15_esp32_recovery_on_link_down_not_on_silent_server);
    RUN_TEST(test_dr15_nrf52_recovery_on_link_down_not_on_silent_server);

    RUN_TEST(test_agreement_bgateway_off_only_harvests_ntp_on_both);
    RUN_TEST(test_drift_esp32_heartbeat_interval_gate_nrf52_has_none);

    return UNITY_END();
}
