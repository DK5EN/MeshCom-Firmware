#include <unity.h>
#include <wifi_liveness.h>

using namespace wifi_liveness;

void setUp(void) {}
void tearDown(void) {}

static Action fail(State &s) { return step(s, true, 0); }
static Action ok(State &s)   { return step(s, true, 1); }

void test_never_armed_never_acts(void)
{
    State s;
    for (int i = 0; i < 100; i++)
        TEST_ASSERT_EQUAL(NONE, fail(s));
}

void test_reconnect_after_three_fails(void)
{
    State s;
    ok(s);
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));
}

void test_full_escalation(void)
{
    State s;
    ok(s);
    fail(s); fail(s);
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(RESET, fail(s));
    for (int i = 0; i < 9; i++)
        TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(RESTART, fail(s));
}

void test_success_resets_counters(void)
{
    State s;
    ok(s);
    fail(s); fail(s);
    ok(s);
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(NONE, fail(s));
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));
    // success after RECONNECT returns to stage 0
    ok(s);
    fail(s); fail(s);
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));
}

void test_link_down_disarms(void)
{
    State s;
    ok(s);
    fail(s); fail(s);
    TEST_ASSERT_EQUAL(NONE, step(s, false, -1));
    for (int i = 0; i < 20; i++)
        TEST_ASSERT_EQUAL(NONE, fail(s));
}

void test_no_result_is_neutral(void)
{
    State s;
    ok(s);
    fail(s);
    // -1: no round pending (first check, gateway change, own reconnect)
    TEST_ASSERT_EQUAL(NONE, step(s, true, -1));
    TEST_ASSERT_EQUAL(NONE, step(s, true, -1));
    TEST_ASSERT_EQUAL(NONE, fail(s));          // 2nd fail, -1 did not reset
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));     // 3rd, -1 did not count
}

// A check mimics checkWifiPing(): collect the last round, step(), start the next.
// ok_seq stands in for s_probeOkSeq, written by the tcpip thread per reply.
static const uint32_t GW1 = 0x0144A8C0u;   // 192.168.68.1
static const uint32_t GW2 = 0x0101A8C0u;   // 192.168.1.1
struct Sim {
    State s;
    Round r;
    uint16_t ok_seq = 0;
    uint16_t last_seq = 0;
    bool disarmed = false;
    Action check(uint32_t gw = GW1)
    {
        bool disarm = false;
        int res = round_collect(r, gw, ok_seq, disarm);
        if (disarm) { s = State(); disarmed = true; }
        Action a = step(s, true, res);
        if (a == NONE) last_seq = round_start(r, gw);
        else round_abort(r);
        return a;
    }
    void reply() { ok_seq = ok_seq_on_reply(ok_seq, last_seq, last_seq); }
};

void test_first_check_has_no_result(void)
{
    Round r;
    bool d = false;
    TEST_ASSERT_EQUAL(-1, round_collect(r, GW1, 0, d));
    TEST_ASSERT_FALSE(d);
}

void test_reply_maps_ok_and_silence_maps_fail(void)
{
    Round r;
    bool d = false;
    uint16_t seq = round_start(r, GW1);
    TEST_ASSERT_EQUAL(1, round_collect(r, GW1, seq, d));
    round_start(r, GW1);
    TEST_ASSERT_EQUAL(0, round_collect(r, GW1, seq, d));   // only the old round answered
    TEST_ASSERT_EQUAL(-1, round_collect(r, GW1, seq, d));  // collected once
}

void test_only_reply_to_current_round_counts(void)
{
    TEST_ASSERT_EQUAL_UINT16(5, ok_seq_on_reply(0, 5, 5));
    TEST_ASSERT_EQUAL_UINT16(5, ok_seq_on_reply(5, 4, 6));      // late reply of round 4
    TEST_ASSERT_EQUAL_UINT16(6, ok_seq_on_reply(5, 6, 6));
}

void test_long_silence_does_not_go_stale(void)
{
    // last reply in round 100, then 40000 silent rounds (about 14 days at 30 s)
    TEST_ASSERT_EQUAL_UINT16(40100, ok_seq_on_reply(100, 40100, 40100));
    Round r;
    bool d = false;
    r.seq = 40099;
    uint16_t seq = round_start(r, GW1);
    TEST_ASSERT_EQUAL(1, round_collect(r, GW1, ok_seq_on_reply(100, seq, seq), d));
}

void test_seq_never_zero_across_wrap(void)
{
    Round r;
    r.seq = 0xFFFF;
    TEST_ASSERT_EQUAL_UINT16(1, round_start(r, GW1));
    TEST_ASSERT_EQUAL_UINT16(2, round_start(r, GW1));
}

void test_dead_stack_escalates_to_reconnect(void)
{
    Sim m;
    TEST_ASSERT_EQUAL(NONE, m.check());   // first round starts
    m.reply();
    TEST_ASSERT_EQUAL(NONE, m.check());   // ok: armed
    TEST_ASSERT_TRUE(m.s.armed);
    // tcpip thread wedged: nothing is sent or received any more
    TEST_ASSERT_EQUAL(NONE, m.check());
    TEST_ASSERT_EQUAL(NONE, m.check());
    TEST_ASSERT_EQUAL(RECONNECT, m.check());
}

void test_reconnect_aborts_round(void)
{
    Sim m;
    m.check(); m.reply(); m.check();
    m.check(); m.check();
    TEST_ASSERT_EQUAL(RECONNECT, m.check());
    TEST_ASSERT_FALSE(m.r.pending);       // no round spans our own action
    TEST_ASSERT_EQUAL(NONE, m.check());   // -1, new round starts
    m.reply();
    TEST_ASSERT_EQUAL(NONE, m.check());   // ok again
    TEST_ASSERT_EQUAL(0, m.s.stage);
}

void test_never_answering_gateway_never_acts(void)
{
    Sim m;
    for (int i = 0; i < 40; i++) TEST_ASSERT_EQUAL(NONE, m.check());
    TEST_ASSERT_FALSE(m.s.armed);
}

void test_gw_change_while_armed_disarms(void)
{
    Sim m;
    m.check(); m.reply(); m.check();
    m.check(); m.check();                 // two fails on GW1
    TEST_ASSERT_EQUAL(NONE, m.check(GW2));
    TEST_ASSERT_TRUE(m.disarmed);
    TEST_ASSERT_FALSE(m.s.armed);
    for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(NONE, m.check(GW2));   // GW2 silent: no action
    m.reply();
    TEST_ASSERT_EQUAL(NONE, m.check(GW2));
    TEST_ASSERT_TRUE(m.s.armed);
}

void test_no_gateway_is_neutral(void)
{
    Round r;
    bool d = false;
    round_start(r, GW1);
    TEST_ASSERT_EQUAL(-1, round_collect(r, 0, 0, d));
    TEST_ASSERT_TRUE(r.pending);           // kept for the next check with a gateway
    TEST_ASSERT_EQUAL(0, round_collect(r, GW1, 0, d));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_never_armed_never_acts);
    RUN_TEST(test_reconnect_after_three_fails);
    RUN_TEST(test_full_escalation);
    RUN_TEST(test_success_resets_counters);
    RUN_TEST(test_link_down_disarms);
    RUN_TEST(test_no_result_is_neutral);
    RUN_TEST(test_first_check_has_no_result);
    RUN_TEST(test_reply_maps_ok_and_silence_maps_fail);
    RUN_TEST(test_only_reply_to_current_round_counts);
    RUN_TEST(test_long_silence_does_not_go_stale);
    RUN_TEST(test_seq_never_zero_across_wrap);
    RUN_TEST(test_dead_stack_escalates_to_reconnect);
    RUN_TEST(test_reconnect_aborts_round);
    RUN_TEST(test_never_answering_gateway_never_acts);
    RUN_TEST(test_gw_change_while_armed_disarms);
    RUN_TEST(test_no_gateway_is_neutral);
    return UNITY_END();
}
