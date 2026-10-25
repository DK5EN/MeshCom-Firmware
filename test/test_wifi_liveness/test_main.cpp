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
    // -1 comes from a round that is merely running (1st check after start)
    int pc = 0;
    int r1 = probe_eval(true, -1, pc).result;
    TEST_ASSERT_EQUAL(-1, r1);
    TEST_ASSERT_EQUAL(NONE, step(s, true, r1));
    TEST_ASSERT_EQUAL(NONE, step(s, true, r1));
    TEST_ASSERT_EQUAL(NONE, fail(s));          // 2nd fail, -1 did not reset
    TEST_ASSERT_EQUAL(RECONNECT, fail(s));     // 3rd, -1 did not count
}

// Item 1: round bookkeeping via probe_eval. A check mimics wifiPingRound():
// evaluate the pending round, feed the result to step().
struct Sim {
    State s;
    bool pending = false;
    int  replies = -1;
    int  pchecks = 0;
    // returns Action of this check; starts a new round afterwards
    Action check(int ended_replies = -1)   // ended_replies: on_ping_end fired with n before this check
    {
        if (ended_replies >= 0) replies = ended_replies;
        Probe p = probe_eval(pending, replies, pchecks);
        if (p.done) { pending = false; replies = -1; }
        Action a = step(s, true, p.result);
        if (!pending) { replies = -1; pending = true; pchecks = 0; }   // start next round
        return a;
    }
};

void test_running_round_first_check_is_neutral(void)
{
    int pc = 0;
    Probe p = probe_eval(true, -1, pc);        // merely running, 1st check after start
    TEST_ASSERT_EQUAL(-1, p.result);
    TEST_ASSERT_FALSE(p.done);
    TEST_ASSERT_FALSE(p.stuck);
    TEST_ASSERT_EQUAL(1, pc);
    p = probe_eval(false, -1, pc);             // no round at all
    TEST_ASSERT_EQUAL(-1, p.result);
    TEST_ASSERT_FALSE(p.done);
    TEST_ASSERT_EQUAL(0, pc);
}

void test_ended_round_maps_replies(void)
{
    int pc = 1;
    Probe p = probe_eval(true, 3, pc);
    TEST_ASSERT_TRUE(p.done); TEST_ASSERT_EQUAL(1, p.result); TEST_ASSERT_EQUAL(0, pc);
    p = probe_eval(true, 0, pc);
    TEST_ASSERT_TRUE(p.done); TEST_ASSERT_EQUAL(0, p.result); TEST_ASSERT_FALSE(p.stuck);
}

void test_pending_at_second_check_is_failure(void)
{
    int pc = 0;
    Probe p = probe_eval(true, -1, pc);        // 1st check: neutral
    TEST_ASSERT_EQUAL(-1, p.result);
    p = probe_eval(true, -1, pc);              // 2nd check: dead
    TEST_ASSERT_TRUE(p.done);
    TEST_ASSERT_TRUE(p.stuck);
    TEST_ASSERT_EQUAL(0, p.result);
    TEST_ASSERT_EQUAL(0, pc);
}

void test_stuck_rounds_escalate_to_reconnect(void)
{
    Sim m;
    m.s.armed = true;                          // armed by an earlier ok round
    // the wedged tcpip thread never fires on_ping_end: every round stays pending.
    // check1 starts round A; check2 pending(1) neutral; check3 pending(2) -> fail 1
    TEST_ASSERT_EQUAL(NONE, m.check());        // start A
    TEST_ASSERT_EQUAL(NONE, m.check());        // A pending 1st
    TEST_ASSERT_EQUAL(NONE, m.check());        // A dead -> fail 1, start B
    TEST_ASSERT_EQUAL(NONE, m.check());        // B pending 1st
    TEST_ASSERT_EQUAL(NONE, m.check());        // B dead -> fail 2, start C
    TEST_ASSERT_EQUAL(NONE, m.check());        // C pending 1st
    TEST_ASSERT_EQUAL(RECONNECT, m.check());   // C dead -> fail 3
}

void test_slow_but_finished_round_does_not_fail(void)
{
    Sim m;
    m.s.armed = true;
    for (int i = 0; i < 20; i++) {
        m.check();                              // start
        TEST_ASSERT_EQUAL(NONE, m.check(3));    // ended with replies before next check
    }
    TEST_ASSERT_EQUAL(0, m.s.fails);
}

// TD-21b: session bookkeeping
struct SSim {
    Session s;
    State st;
    int stops = 0, dels = 0, abandons = 0, creates = 0;
    int check(uint32_t gw, Action *act = nullptr)
    {
        Plan pl = session_collect(s, gw);
        if (pl.stop) stops++;
        if (pl.del) dels++;
        if (pl.abandon) abandons++;
        if (pl.disarm) st = State();
        if (pl.create) { creates++; session_created(s, gw); }
        Action a = step(st, true, pl.result);
        if (a == RECONNECT) { session_abort_round(s); }
        else if (a == NONE) session_start_round(s);
        if (act) *act = a;
        return pl.result;
    }
    void on_end(int n) { session_on_end(s, n); }
};

void test_gw_change_while_armed_disarms(void)
{
    SSim m;
    m.check(1);                      // create
    m.on_end(0);                     // (start happened in check)
    m.st.armed = true;
    m.s.busy = false; m.s.pending = false;
    Action a;
    m.check(2, &a);                  // gw change, idle session: delete + create
    TEST_ASSERT_EQUAL(1, m.dels);
    TEST_ASSERT_FALSE(m.st.armed);
    for (int i = 0; i < 6; i++) { m.check(2, &a); TEST_ASSERT_EQUAL(NONE, a); m.on_end(0); }
    TEST_ASSERT_EQUAL(0, m.st.fails);                // disarmed: fails not counted
    m.check(2); m.on_end(2); m.check(2);             // answers once -> armed
    TEST_ASSERT_TRUE(m.st.armed);
}

void test_late_end_after_abort_not_counted(void)
{
    Session s;
    session_created(s, 1);
    TEST_ASSERT_TRUE(session_start_round(s));
    TEST_ASSERT_TRUE(session_abort_round(s));
    session_on_end(s, 3);                            // late
    TEST_ASSERT_EQUAL(-1, s.replies);
    TEST_ASSERT_FALSE(s.busy);
    Plan pl = session_collect(s, 1);
    TEST_ASSERT_EQUAL(-1, pl.result);
}

void test_drop_while_busy_deferred_then_done(void)
{
    Session s;
    session_created(s, 1);
    session_start_round(s);
    Plan pl = session_collect(s, 2);                 // gw change, round running
    TEST_ASSERT_TRUE(pl.stop);
    TEST_ASSERT_FALSE(pl.del);
    TEST_ASSERT_FALSE(pl.abandon);
    TEST_ASSERT_FALSE(pl.create);
    TEST_ASSERT_TRUE(s.have);
    session_on_end(s, 0);                            // task leaves the round
    pl = session_collect(s, 2);
    TEST_ASSERT_TRUE(pl.del);
    TEST_ASSERT_FALSE(pl.abandon);
    TEST_ASSERT_TRUE(pl.create);
    TEST_ASSERT_EQUAL(-1, pl.result);
}

void test_gw_change_with_wedged_old_round_recovers(void)
{
    SSim m;
    Action a;
    m.check(1);                                      // create + start
    m.on_end(2); m.check(1);                         // ok round -> armed
    TEST_ASSERT_TRUE(m.st.armed);
    // the check above also started a new round, which never ends
    m.check(2, &a);                                  // gw change, old pending (1)
    TEST_ASSERT_EQUAL(0, m.abandons);
    m.check(2, &a);                                  // pending (2) -> stuck -> abandon + create
    TEST_ASSERT_EQUAL(1, m.abandons);
    TEST_ASSERT_EQUAL(0, m.dels);
    TEST_ASSERT_EQUAL(2, m.creates);
    TEST_ASSERT_TRUE(m.s.have);
    TEST_ASSERT_EQUAL(2, (int)m.s.gw);
    TEST_ASSERT_FALSE(m.st.armed);
    // new gateway answers once -> armed
    m.check(2); m.on_end(3); m.check(2);
    TEST_ASSERT_TRUE(m.st.armed);
    // 3 failed rounds -> RECONNECT
    Action last = NONE;
    for (int i = 0; i < 3; i++) { m.check(2, &last); m.on_end(0); }
    m.check(2, &last);
    TEST_ASSERT_EQUAL(RECONNECT, last);
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
    RUN_TEST(test_running_round_first_check_is_neutral);
    RUN_TEST(test_ended_round_maps_replies);
    RUN_TEST(test_pending_at_second_check_is_failure);
    RUN_TEST(test_stuck_rounds_escalate_to_reconnect);
    RUN_TEST(test_slow_but_finished_round_does_not_fail);
    RUN_TEST(test_gw_change_while_armed_disarms);
    RUN_TEST(test_late_end_after_abort_not_counted);
    RUN_TEST(test_drop_while_busy_deferred_then_done);
    RUN_TEST(test_gw_change_with_wedged_old_round_recovers);
    return UNITY_END();
}
