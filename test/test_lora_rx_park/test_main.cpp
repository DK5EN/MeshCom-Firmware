// test_lora_rx_park -- WEB-RX-01 (#1229): host tests of the parked-packet ring.
#include <unity.h>
#include <string.h>

#include "lora_rx_park.h"

void setUp(void) { rxParkReset(); }
void tearDown(void) {}

static bool push1(uint8_t tag, int16_t rssi = -90, int8_t snr = 5, int state = 0)
{
    uint8_t b[3] = {tag, (uint8_t)(tag + 1), (uint8_t)(tag + 2)};
    return rxParkPush(b, sizeof(b), rssi, snr, state);
}

static uint8_t pop_tag(void)
{
    uint8_t b[RX_PARK_PAYLOAD_MAX];
    size_t len = 0;
    int16_t r;
    int8_t s;
    int st;
    TEST_ASSERT_TRUE(rxParkPop(b, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_UINT32(3, len);
    TEST_ASSERT_EQUAL_UINT8(b[0] + 1, b[1]);
    return b[0];
}

void test_order_across_wrap(void)
{
    uint8_t next_push = 0, next_pop = 0;
    for (int round = 0; round < 3 * RX_PARK_SLOTS; round++) {
        TEST_ASSERT_TRUE(push1(next_push++));
        TEST_ASSERT_TRUE(push1(next_push++));
        TEST_ASSERT_EQUAL_UINT8(next_pop++, pop_tag());
        TEST_ASSERT_EQUAL_UINT8(next_pop++, pop_tag());
    }
    TEST_ASSERT_EQUAL_UINT32(0, rxParkCount());
    TEST_ASSERT_EQUAL_UINT32(6 * RX_PARK_SLOTS, rxParkParked());
}

void test_order_wrap_partial_fill(void)
{
    uint8_t np = 0, nq = 0;
    for (int i = 0; i < RX_PARK_SLOTS - 1; i++) TEST_ASSERT_TRUE(push1(np++));
    for (int i = 0; i < 2 * RX_PARK_SLOTS; i++) {
        TEST_ASSERT_TRUE(push1(np++));
        TEST_ASSERT_EQUAL_UINT8(nq++, pop_tag());
    }
    while (rxParkCount()) TEST_ASSERT_EQUAL_UINT8(nq++, pop_tag());
    TEST_ASSERT_EQUAL_UINT8(np, nq);
}

void test_overflow_drops_newest(void)
{
    for (int i = 0; i < RX_PARK_SLOTS; i++) TEST_ASSERT_TRUE(push1((uint8_t)i));
    TEST_ASSERT_FALSE(push1(100));
    TEST_ASSERT_FALSE(push1(101));
    TEST_ASSERT_EQUAL_UINT32(2, rxParkOverflow());
    TEST_ASSERT_EQUAL_UINT32(RX_PARK_SLOTS, rxParkParked());
    TEST_ASSERT_EQUAL_UINT32(RX_PARK_SLOTS, rxParkCount());
    for (int i = 0; i < RX_PARK_SLOTS; i++) TEST_ASSERT_EQUAL_UINT8((uint8_t)i, pop_tag());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkCount());
}

void test_clamp_counts_and_stores_max(void)
{
    uint8_t big[RX_PARK_PAYLOAD_MAX + 40];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7 + 1);
    TEST_ASSERT_TRUE(rxParkPush(big, sizeof(big), -100, -3, 0));
    TEST_ASSERT_EQUAL_UINT32(1, rxParkClamped());
    TEST_ASSERT_EQUAL_UINT32(1, rxParkParked());

    uint8_t out[RX_PARK_PAYLOAD_MAX];
    size_t len = 0;
    int16_t r;
    int8_t s;
    int st;
    TEST_ASSERT_TRUE(rxParkPop(out, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_UINT32(RX_PARK_PAYLOAD_MAX, len);
    TEST_ASSERT_EQUAL_MEMORY(big, out, RX_PARK_PAYLOAD_MAX);

    // exactly MAX is not a clamp
    TEST_ASSERT_TRUE(rxParkPush(big, RX_PARK_PAYLOAD_MAX, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(1, rxParkClamped());
}

void test_empty_pop_leaves_outputs(void)
{
    uint8_t buf[RX_PARK_PAYLOAD_MAX];
    memset(buf, 0xAB, sizeof(buf));
    size_t len = 77;
    int16_t rssi = -11;
    int8_t snr = 9;
    int st = 42;
    TEST_ASSERT_FALSE(rxParkPop(buf, &len, &rssi, &snr, &st));
    TEST_ASSERT_EQUAL_UINT32(77, len);
    TEST_ASSERT_EQUAL_INT16(-11, rssi);
    TEST_ASSERT_EQUAL_INT8(9, snr);
    TEST_ASSERT_EQUAL_INT(42, st);
    TEST_ASSERT_EQUAL_UINT8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, buf[RX_PARK_PAYLOAD_MAX - 1]);
}

void test_metadata_round_trip(void)
{
    TEST_ASSERT_TRUE(push1(1, -123, -17, -7));
    TEST_ASSERT_TRUE(push1(2, 20, 12, 3));
    uint8_t b[RX_PARK_PAYLOAD_MAX];
    size_t len;
    int16_t r;
    int8_t s;
    int st;
    TEST_ASSERT_TRUE(rxParkPop(b, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_INT16(-123, r);
    TEST_ASSERT_EQUAL_INT8(-17, s);
    TEST_ASSERT_EQUAL_INT(-7, st);
    TEST_ASSERT_TRUE(rxParkPop(b, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_INT16(20, r);
    TEST_ASSERT_EQUAL_INT8(12, s);
    TEST_ASSERT_EQUAL_INT(3, st);
}

void test_count(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, rxParkCount());
    push1(1);
    TEST_ASSERT_EQUAL_UINT32(1, rxParkCount());
    push1(2);
    TEST_ASSERT_EQUAL_UINT32(2, rxParkCount());
    pop_tag();
    TEST_ASSERT_EQUAL_UINT32(1, rxParkCount());
}

void test_reset(void)
{
    for (int i = 0; i < RX_PARK_SLOTS + 1; i++) push1((uint8_t)i); // one overflow
    uint8_t big[RX_PARK_PAYLOAD_MAX + 1] = {0};
    rxParkReset();
    rxParkPush(big, sizeof(big), 0, 0, 0);
    rxParkReset();
    TEST_ASSERT_EQUAL_UINT32(0, rxParkCount());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkOverflow());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkClamped());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkParked());
    uint8_t b[RX_PARK_PAYLOAD_MAX];
    size_t len;
    int16_t r;
    int8_t s;
    int st;
    TEST_ASSERT_FALSE(rxParkPop(b, &len, &r, &s, &st));
}

void test_zero_length_packet(void)
{
    TEST_ASSERT_TRUE(rxParkPush(NULL, 0, -80, 2, 0));
    uint8_t zero = 0;
    TEST_ASSERT_TRUE(rxParkPush(&zero, 0, -81, 3, 0));
    TEST_ASSERT_EQUAL_UINT32(2, rxParkCount());
    uint8_t b[RX_PARK_PAYLOAD_MAX];
    size_t len = 99;
    int16_t r;
    int8_t s;
    int st;
    TEST_ASSERT_TRUE(rxParkPop(b, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_UINT32(0, len);
    TEST_ASSERT_EQUAL_INT16(-80, r);
    TEST_ASSERT_TRUE(rxParkPop(b, &len, &r, &s, &st));
    TEST_ASSERT_EQUAL_UINT32(0, len);
}

void test_null_with_len_refused(void)
{
    TEST_ASSERT_FALSE(rxParkPush(NULL, 5, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rxParkCount());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkParked());
    TEST_ASSERT_EQUAL_UINT32(0, rxParkOverflow());
}

void test_should_service_only_flagged_idle_with_room(void)
{
    // the one case that services: packet flagged, no checkRX running, no TX, ring has room
    TEST_ASSERT_TRUE(rxParkShouldService(true, false, false, false));
    // each blocker alone stops it
    TEST_ASSERT_FALSE(rxParkShouldService(false, false, false, false)); // nothing flagged
    TEST_ASSERT_FALSE(rxParkShouldService(true, true, false, false));   // checkRX/drain in progress
    TEST_ASSERT_FALSE(rxParkShouldService(true, false, true, false));   // TX running
    TEST_ASSERT_FALSE(rxParkShouldService(true, false, false, true));   // ring full
}

void test_full_ring_is_reported_full_to_the_service_gate(void)
{
    // the gate input the hook passes: rxParkCount() >= RX_PARK_SLOTS; a full ring must say no
    // so the packet stays on the chip instead of being read and dropped
    uint8_t p[4] = {1, 2, 3, 4};
    for (int i = 0; i < RX_PARK_SLOTS; i++)
        TEST_ASSERT_TRUE(rxParkPush(p, sizeof(p), 0, 0, 0));
    TEST_ASSERT_FALSE(rxParkShouldService(true, false, false, rxParkCount() >= RX_PARK_SLOTS));
    uint8_t out[RX_PARK_PAYLOAD_MAX];
    size_t len = 0;
    TEST_ASSERT_TRUE(rxParkPop(out, &len, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(rxParkShouldService(true, false, false, rxParkCount() >= RX_PARK_SLOTS));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_order_across_wrap);
    RUN_TEST(test_order_wrap_partial_fill);
    RUN_TEST(test_overflow_drops_newest);
    RUN_TEST(test_clamp_counts_and_stores_max);
    RUN_TEST(test_empty_pop_leaves_outputs);
    RUN_TEST(test_metadata_round_trip);
    RUN_TEST(test_count);
    RUN_TEST(test_reset);
    RUN_TEST(test_zero_length_packet);
    RUN_TEST(test_null_with_len_refused);
    RUN_TEST(test_should_service_only_flagged_idle_with_room);
    RUN_TEST(test_full_ring_is_reported_full_to_the_service_gate);
    return UNITY_END();
}
