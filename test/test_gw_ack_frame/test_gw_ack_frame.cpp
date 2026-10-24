// Native test suite for gwAckFrame() and gwAckBuildPair() (src/gw_ack_frame.h):
// the gateway's double ACK for a heard group or "*" text (GWACK-01).
//
// Core requirement: both copies carry the SAME frame id, whatever the clock
// does between them. A same-id copy is a repeat (a listener that missed copy 1
// takes copy 2, relays forward it once); a distinct id is a second frame every
// relay forwards again (docs/campaign-gwack01-20261024.md).
//
//   pio test -e native -f test_gw_ack_frame

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <gw_ack_frame.h>

// Clock that advances 1 ms per read: the DisplayInfo-on case, where the
// print between the copies lets the millisecond tick.
static uint32_t s_now;
static uint32_t tickingClock(void) { return s_now++; }

void setUp(void) { s_now = 0x0002286C; }
void tearDown(void) {}

void test_frame_layout_byte_for_byte(void)
{
    uint8_t out[GW_ACK_FRAME_LEN];
    gwAckFrame(out, 0x0002286C, 0x4106E12E, 0x04);
    // bytes as captured on air in BL-01 W2: 416C280200842EE106410100
    const uint8_t want[GW_ACK_FRAME_LEN] = {0x41, 0x6C, 0x28, 0x02, 0x00, 0x84,
                                            0x2E, 0xE1, 0x06, 0x41, 0x01, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, GW_ACK_FRAME_LEN);
}

void test_pair_shares_one_id_on_a_ticking_clock(void)
{
    GwAckPair p;
    gwAckBuildPair(&p, tickingClock, 0x4106E12E, 0x04);
    TEST_ASSERT_EQUAL_HEX32(p.firstId, p.secondId);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p.first, p.second, GW_ACK_FRAME_LEN);
}

void test_pair_reads_the_clock_once(void)
{
    GwAckPair p;
    gwAckBuildPair(&p, tickingClock, 0x4106E12E, 0x04);
    TEST_ASSERT_EQUAL_HEX32(0x0002286C, p.firstId);
    TEST_ASSERT_EQUAL_HEX32(0x0002286D, s_now);
}

void test_pair_carries_acked_id_and_hop(void)
{
    GwAckPair p;
    gwAckBuildPair(&p, tickingClock, 0xB5978333, 0x02);
    TEST_ASSERT_EQUAL_HEX8(0x41, p.second[0]);
    TEST_ASSERT_EQUAL_HEX8(0x82, p.second[5]);
    TEST_ASSERT_EQUAL_HEX8(0x33, p.second[6]);
    TEST_ASSERT_EQUAL_HEX8(0xB5, p.second[9]);
    TEST_ASSERT_EQUAL_HEX8(0x01, p.second[10]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_layout_byte_for_byte);
    RUN_TEST(test_pair_shares_one_id_on_a_ticking_clock);
    RUN_TEST(test_pair_reads_the_clock_once);
    RUN_TEST(test_pair_carries_acked_id_and_hop);
    return UNITY_END();
}
