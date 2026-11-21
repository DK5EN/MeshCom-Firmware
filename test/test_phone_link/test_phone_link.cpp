#include <unity.h>
#include "phone_link.h"

static PhoneLinkState s;

void setUp(void) { phoneLinkInit(&s); }
void tearDown(void) {}

void test_claim_from_none(void)
{
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
    TEST_ASSERT_EQUAL_UINT8(PL_NONE, phoneLinkClaim(&s, PL_BLE));
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_BLE));
    TEST_ASSERT_TRUE(phoneLinkUp(&s));
    TEST_ASSERT_EQUAL_UINT32(0, s.takeovers);
}

void test_takeover_ble_to_net_and_back(void)
{
    phoneLinkClaim(&s, PL_BLE);
    TEST_ASSERT_EQUAL_UINT8(PL_BLE, phoneLinkClaim(&s, PL_NET));
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_NET));
    TEST_ASSERT_EQUAL_UINT32(1, s.takeovers);
    TEST_ASSERT_EQUAL_UINT8(PL_NET, phoneLinkClaim(&s, PL_BLE));
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_BLE));
    TEST_ASSERT_EQUAL_UINT32(2, s.takeovers);
}

void test_same_owner_reclaim_is_no_takeover(void)
{
    phoneLinkClaim(&s, PL_BLE);
    TEST_ASSERT_EQUAL_UINT8(PL_NONE, phoneLinkClaim(&s, PL_BLE));
    TEST_ASSERT_EQUAL_UINT32(0, s.takeovers);
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_BLE));
}

void test_release_by_non_owner_keeps_owner(void)
{
    phoneLinkClaim(&s, PL_NET);
    TEST_ASSERT_FALSE(phoneLinkRelease(&s, PL_BLE));
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_NET));
    TEST_ASSERT_TRUE(phoneLinkUp(&s));
}

void test_release_by_owner_ends_session(void)
{
    phoneLinkClaim(&s, PL_NET);
    TEST_ASSERT_TRUE(phoneLinkRelease(&s, PL_NET));
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
    TEST_ASSERT_FALSE(phoneLinkRelease(&s, PL_NET));
}

void test_none_is_inert(void)
{
    TEST_ASSERT_EQUAL_UINT8(PL_NONE, phoneLinkClaim(&s, PL_NONE));
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
    TEST_ASSERT_FALSE(phoneLinkRelease(&s, PL_NONE));
}

// What nrf52BleTick() / the ESP32 edge do with a pending BLE-down edge.
static void bleDownEdge(PhoneLinkState *st, bool bleLinkUpNow)
{
    if (phoneLinkBleDownEndsSession(st, bleLinkUpNow))
        phoneLinkRelease(st, PL_BLE);
}

void test_ble_down_edge_ends_the_ble_session(void)
{
    phoneLinkClaim(&s, PL_BLE);
    bleDownEdge(&s, false);
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
}

// nRF52 loop pass: central A owns, A drops (callback: link false, edge pending), central B connects
// (callback: link true) and its hello is processed (claim) BEFORE the edge is handled in the same pass.
void test_new_central_hello_before_old_edge_keeps_the_session(void)
{
    phoneLinkClaim(&s, PL_BLE);              // A authenticated
    bool linkUp = false;                      // A dropped
    linkUp = true;                            // B connected
    phoneLinkClaim(&s, PL_BLE);              // B hello drained from bleQueue
    bleDownEdge(&s, linkUp);                  // nrf52BleTick handles A's edge
    TEST_ASSERT_TRUE_MESSAGE(phoneLinkOwnedBy(&s, PL_BLE), "B's session was killed by A's disconnect edge");
}

void test_edge_with_new_central_that_drops_again_ends_the_session(void)
{
    phoneLinkClaim(&s, PL_BLE);
    bleDownEdge(&s, false);                   // A dropped, B connected and dropped again: link down at the edge
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
}

void test_ble_down_edge_keeps_a_net_session(void)
{
    phoneLinkClaim(&s, PL_NET);
    bleDownEdge(&s, false);
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_NET));
    bleDownEdge(&s, true);
    TEST_ASSERT_TRUE(phoneLinkOwnedBy(&s, PL_NET));
}

void test_ble_down_edge_without_owner_is_inert(void)
{
    bleDownEdge(&s, false);
    TEST_ASSERT_FALSE(phoneLinkUp(&s));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_claim_from_none);
    RUN_TEST(test_takeover_ble_to_net_and_back);
    RUN_TEST(test_same_owner_reclaim_is_no_takeover);
    RUN_TEST(test_release_by_non_owner_keeps_owner);
    RUN_TEST(test_release_by_owner_ends_session);
    RUN_TEST(test_none_is_inert);
    RUN_TEST(test_ble_down_edge_ends_the_ble_session);
    RUN_TEST(test_new_central_hello_before_old_edge_keeps_the_session);
    RUN_TEST(test_edge_with_new_central_that_drops_again_ends_the_session);
    RUN_TEST(test_ble_down_edge_keeps_a_net_session);
    RUN_TEST(test_ble_down_edge_without_owner_is_inert);
    return UNITY_END();
}
