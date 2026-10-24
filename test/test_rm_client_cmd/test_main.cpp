// Client RM1 commands: the pending table (rm_client_cmd.h) and the RX gate that queues their replies.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "remote_cmd.h"
#include "rm_queue.h"
#include "rm_client_cmd.h"
#include "rm_rx_gate.h"

void setUp(void)
{
    rmQueueReset();
    rmClientCmdReset();
    rmqReplyWanted() = false;
}
void tearDown(void) {}

static const char *DST = "DK5EN-1";
static const char *OWN = "DK5EN-98";
static const char *REPLY = "RM1 1792859569 ok p=20 0123456789abcdef";

void test_arm_take_match(void)
{
    rmClientCmdArm(DST, 1792859569u, 0x1234u, 1000);
    TEST_ASSERT_EQUAL_UINT32(0x1234u, rmClientCmdTake(DST, 1792859569u, 5000));
}

void test_no_match_wrong_ctr_or_call(void)
{
    rmClientCmdArm(DST, 77, 0x1234u, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 78, 2000));
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake("DK5EN-2", 77, 2000));
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake("DK5EN", 77, 2000));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, rmClientCmdTake(DST, 77, 2000)); // still there
}

// the reply's source call may arrive in another case than the upper-cased destination
void test_call_match_ignores_case(void)
{
    rmClientCmdArm("DK5EN-1", 77, 0x1234u, 1000);
    TEST_ASSERT_EQUAL_UINT32(0x1234u, rmClientCmdTake("dk5en-1", 77, 2000));
}

void test_take_removes(void)
{
    rmClientCmdArm(DST, 5, 9, 0);
    TEST_ASSERT_EQUAL_UINT32(9, rmClientCmdTake(DST, 5, 10));
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 5, 11));
    TEST_ASSERT_FALSE(rmClientReplyWanted(12));
}

void test_expiry_boundary(void)
{
    rmClientCmdArm(DST, 5, 9, 1000);
    TEST_ASSERT_TRUE(rmClientReplyWanted(1000 + RM_NOANSWER_MS - 1));
    TEST_ASSERT_FALSE(rmClientReplyWanted(1000 + RM_NOANSWER_MS));
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 5, 1000 + RM_NOANSWER_MS));
    rmClientCmdArm(DST, 5, 9, 1000);
    TEST_ASSERT_EQUAL_UINT32(9, rmClientCmdTake(DST, 5, 1000 + RM_NOANSWER_MS - 1));
}

void test_expiry_millis_wrap(void)
{
    const uint32_t t0 = 0xFFFFFF00u;
    rmClientCmdArm(DST, 5, 9, t0);
    TEST_ASSERT_TRUE(rmClientReplyWanted(t0 + 1000u)); // wrapped past 0
    TEST_ASSERT_EQUAL_UINT32(9, rmClientCmdTake(DST, 5, t0 + 1000u));
    rmClientCmdArm(DST, 5, 9, t0);
    TEST_ASSERT_FALSE(rmClientReplyWanted(t0 + RM_NOANSWER_MS));
}

void test_evict_oldest_when_full(void)
{
    for (uint32_t i = 0; i < RM_CLIENT_SLOTS; i++)
        rmClientCmdArm(DST, 100 + i, 10 + i, 1000 + i * 10);
    rmClientCmdArm(DST, 200, 99, 1100); // all live: the oldest (ctr 100) goes
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 100, 1200));
    TEST_ASSERT_EQUAL_UINT32(99, rmClientCmdTake(DST, 200, 1200));
    TEST_ASSERT_EQUAL_UINT32(11, rmClientCmdTake(DST, 101, 1200));
    TEST_ASSERT_EQUAL_UINT32(13, rmClientCmdTake(DST, 103, 1200));
}

void test_rearm_same_dst_ctr_replaces(void)
{
    rmClientCmdArm(DST, 5, 9, 1000);
    rmClientCmdArm(DST, 5, 10, 2000);
    TEST_ASSERT_EQUAL_UINT32(10, rmClientCmdTake(DST, 5, 3000));
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 5, 3000));
}

void test_forget(void)
{
    rmClientCmdArm(DST, 5, 9, 1000);
    rmClientCmdForget(9);
    TEST_ASSERT_EQUAL_UINT32(0, rmClientCmdTake(DST, 5, 1001));
    TEST_ASSERT_FALSE(rmClientReplyWanted(1001));
}

void test_sync_ctr_zero(void)
{
    rmClientCmdArm(DST, 0, 42, 1000);
    TEST_ASSERT_EQUAL_UINT32(42, rmClientCmdTake(DST, 0, 2000));
}

void test_parse_command_ctr(void)
{
    uint8_t key[32];
    rmDeriveKey("pw", key);
    char out[170];
    TEST_ASSERT_TRUE(rmBuildCommand(DST, OWN, 1792859569u, "status", "", key, out, sizeof(out)) > 0);
    uint32_t ctr = 0;
    TEST_ASSERT_TRUE(rmClientParseCtr(out, ctr));
    TEST_ASSERT_EQUAL_UINT32(1792859569u, ctr);
    TEST_ASSERT_TRUE(rmBuildCommand(DST, OWN, 0, "sync", "", key, out, sizeof(out)) > 0);
    TEST_ASSERT_TRUE(rmClientParseCtr(out, ctr));
    TEST_ASSERT_EQUAL_UINT32(0, ctr);
    TEST_ASSERT_FALSE(rmClientParseCtr("hello", ctr));
    TEST_ASSERT_FALSE(rmClientParseCtr(REPLY, ctr)); // a reply is no command
}

void test_gate_queues_reply_for_client_cmd(void)
{
    rmClientCmdArm(DST, 1792859569u, 0x1234u, 1000);
    TEST_ASSERT_FALSE(rmRxTryQueue(DST, REPLY, false, 1, 2000)); // never consumed
    TEST_ASSERT_EQUAL_UINT8(1, rmReplyQueueCount());
    char s[RM_QUEUE_SRC_LEN], t[RM_QUEUE_TEXT_LEN];
    TEST_ASSERT_TRUE(rmReplyPop(s, sizeof(s), t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING(DST, s);
    TEST_ASSERT_EQUAL_STRING(REPLY, t);
}

void test_gate_ignores_reply_when_nothing_pending(void)
{
    TEST_ASSERT_FALSE(rmRxTryQueue(DST, REPLY, false, 1, 2000));
    TEST_ASSERT_EQUAL_UINT8(0, rmReplyQueueCount());
}

void test_gate_ignores_reply_after_expiry(void)
{
    rmClientCmdArm(DST, 1792859569u, 0x1234u, 1000);
    TEST_ASSERT_FALSE(rmRxTryQueue(DST, REPLY, false, 1, 1000 + RM_NOANSWER_MS));
    TEST_ASSERT_EQUAL_UINT8(0, rmReplyQueueCount());
}

void test_gate_own_key_still_queues(void)
{
    rmqReplyWanted() = true;
    TEST_ASSERT_FALSE(rmRxTryQueue(DST, REPLY, false, 1, 2000));
    TEST_ASSERT_EQUAL_UINT8(1, rmReplyQueueCount());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_arm_take_match);
    RUN_TEST(test_no_match_wrong_ctr_or_call);
    RUN_TEST(test_call_match_ignores_case);
    RUN_TEST(test_take_removes);
    RUN_TEST(test_expiry_boundary);
    RUN_TEST(test_expiry_millis_wrap);
    RUN_TEST(test_evict_oldest_when_full);
    RUN_TEST(test_rearm_same_dst_ctr_replaces);
    RUN_TEST(test_forget);
    RUN_TEST(test_sync_ctr_zero);
    RUN_TEST(test_parse_command_ctr);
    RUN_TEST(test_gate_queues_reply_for_client_cmd);
    RUN_TEST(test_gate_ignores_reply_when_nothing_pending);
    RUN_TEST(test_gate_ignores_reply_after_expiry);
    RUN_TEST(test_gate_own_key_still_queues);
    return UNITY_END();
}
