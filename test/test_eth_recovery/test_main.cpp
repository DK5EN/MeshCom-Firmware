// Native tests for the ETH-04 decision logic in src/nrf52/eth_recovery.h.
//
//   pio test -e native_eth_recovery

#include <unity.h>

#include <nrf52/eth_recovery.h>

using namespace eth_recovery;

static Inputs in;
static State st;

void setUp(void)
{
    // Default: DHCP lease held, link up, no stalled reopen, no maintain event.
    in = Inputs{false, true, true, false, 0};
    st = State{0, false, 0};
}
void tearDown(void) {}

static void expect(Action a, uint32_t now = 1000)
{
    TEST_ASSERT_EQUAL_UINT8((uint8_t)a, (uint8_t)decide(in, st, now));
}

// ETH-05: a static node must stay static, whatever the other inputs say.
void test_fixed_ip_reapplies_static(void)
{
    in.fixed_ip = true;
    expect(Action::ReapplyStatic);
    st.dhcp_fail_streak = 5;  // even with F2 conditions
    expect(Action::ReapplyStatic);
}

void test_fixed_ip_ignores_other_inputs(void)
{
    in.fixed_ip = true;
    in.lease_held = true;
    expect(Action::ReapplyStatic);
    in.link_up = false;
    expect(Action::ReapplyStatic);
    in.link_up = true;
    in.reopen_stalled = true;
    expect(Action::ReapplyStatic);
    in.reopen_stalled = false;
    in.last_maintain_rc = 1;
    expect(Action::ReapplyStatic);
    in.last_maintain_rc = 3;
    expect(Action::ReapplyStatic);
}

// Core regression: today's firmware does a full DHCP here.
void test_lease_held_link_up_reopens_udp(void)
{
    expect(Action::ReopenUdp);
}

void test_no_lease_full_dhcp(void)
{
    in.lease_held = false;
    expect(Action::FullDhcp);
}

void test_link_down_full_dhcp(void)
{
    in.link_up = false;
    expect(Action::FullDhcp);
}

// ETH-04 liveness: a reopen without UDP RX must escalate, not repeat forever.
void test_reopen_stalled_full_dhcp(void)
{
    in.reopen_stalled = true;
    expect(Action::FullDhcp);
}

void test_not_stalled_reopens_udp(void)
{
    in.reopen_stalled = false;
    expect(Action::ReopenUdp);
}

void test_reopen_stalled_streak_link_up_hw_reset(void)
{
    in.reopen_stalled = true;
    st.dhcp_fail_streak = 2;
    expect(Action::HwResetThenDhcp);
    st.dhcp_fail_streak = 5;
    expect(Action::HwResetThenDhcp);
}

void test_reopen_stalled_link_down_full_dhcp(void)
{
    in.reopen_stalled = true;
    in.link_up = false;
    st.dhcp_fail_streak = 5;
    expect(Action::FullDhcp);   // N-20: no hw reset with the link down
}

void test_maintain_renew_fail_full_dhcp(void)
{
    in.last_maintain_rc = 1;
    expect(Action::FullDhcp);
}

void test_maintain_rebind_fail_full_dhcp(void)
{
    in.last_maintain_rc = 3;
    expect(Action::FullDhcp);
}

void test_maintain_ok_still_reopens_udp(void)
{
    in.last_maintain_rc = 2;
    expect(Action::ReopenUdp);
    in.last_maintain_rc = 4;
    expect(Action::ReopenUdp);
    in.last_maintain_rc = 0;
    expect(Action::ReopenUdp);
}

void test_f2_streak_thresholds(void)
{
    in.lease_held = false;
    st.dhcp_fail_streak = 0;
    expect(Action::FullDhcp);
    st.dhcp_fail_streak = 1;
    expect(Action::FullDhcp);
    st.dhcp_fail_streak = 2;
    expect(Action::HwResetThenDhcp);
    st.dhcp_fail_streak = 3;
    expect(Action::HwResetThenDhcp);
}

void test_f2_suppressed_when_link_down(void)
{
    in.lease_held = false;
    in.link_up = false;
    st.dhcp_fail_streak = 10;
    expect(Action::FullDhcp);
}

void test_f2_backoff_and_release(void)
{
    in.lease_held = false;
    st.dhcp_fail_streak = 2;
    onHwReset(st, 10000);
    expect(Action::FullDhcp, 10000);
    expect(Action::FullDhcp, 10000 + kHwResetMinGapMs - 1);
    expect(Action::HwResetThenDhcp, 10000 + kHwResetMinGapMs);
    expect(Action::HwResetThenDhcp, 10000 + kHwResetMinGapMs + 1);
}

void test_f2_gap_survives_millis_wraparound(void)
{
    in.lease_held = false;
    st.dhcp_fail_streak = 2;
    onHwReset(st, 0xFFFFFFF0UL);
    // now wrapped to a small value: only 0x10 + 100 ms elapsed
    expect(Action::FullDhcp, 100);
    expect(Action::FullDhcp, (uint32_t)(0xFFFFFFF0UL + kHwResetMinGapMs - 1));
    expect(Action::HwResetThenDhcp, (uint32_t)(0xFFFFFFF0UL + kHwResetMinGapMs));
}

void test_f2_first_reset_at_time_zero_state(void)
{
    // hw_reset_done=false must allow a reset even if now_ms is "close" to 0.
    in.lease_held = false;
    st.dhcp_fail_streak = 2;
    expect(Action::HwResetThenDhcp, 0);
}

void test_f1_wins_over_f2(void)
{
    // Lease held + link steady: no reset even with a stale streak.
    st.dhcp_fail_streak = 9;
    expect(Action::ReopenUdp);
}

void test_on_dhcp_result_counts_and_resets(void)
{
    onDhcpResult(st, false, true);
    TEST_ASSERT_EQUAL_UINT8(1, st.dhcp_fail_streak);
    onDhcpResult(st, false, true);
    TEST_ASSERT_EQUAL_UINT8(2, st.dhcp_fail_streak);
    onDhcpResult(st, true, true);
    TEST_ASSERT_EQUAL_UINT8(0, st.dhcp_fail_streak);
}

void test_on_dhcp_result_success_without_link_resets(void)
{
    st.dhcp_fail_streak = 4;
    onDhcpResult(st, true, false);
    TEST_ASSERT_EQUAL_UINT8(0, st.dhcp_fail_streak);
}

void test_on_dhcp_result_link_down_no_change(void)
{
    st.dhcp_fail_streak = 3;
    onDhcpResult(st, false, false);
    TEST_ASSERT_EQUAL_UINT8(3, st.dhcp_fail_streak);
}

void test_on_dhcp_result_saturates(void)
{
    st.dhcp_fail_streak = 254;
    onDhcpResult(st, false, true);
    TEST_ASSERT_EQUAL_UINT8(255, st.dhcp_fail_streak);
    onDhcpResult(st, false, true);
    TEST_ASSERT_EQUAL_UINT8(255, st.dhcp_fail_streak);
}

void test_on_hw_reset_keeps_streak(void)
{
    st.dhcp_fail_streak = 2;
    onHwReset(st, 4242);
    TEST_ASSERT_TRUE(st.hw_reset_done);
    TEST_ASSERT_EQUAL_UINT32(4242, st.last_hw_reset_ms);
    TEST_ASSERT_EQUAL_UINT8(2, st.dhcp_fail_streak);
}

// ETH-05 follow-up: static recovery without a hardware reset when possible.
static StaticInputs si(bool link_up, bool ip_matches, bool reopen_stalled, bool hw_reset_allowed = true)
{
    StaticInputs in;
    in.hw_reset_allowed = hw_reset_allowed;
    in.link_up = link_up;
    in.ip_matches = ip_matches;
    in.reopen_stalled = reopen_stalled;
    return in;
}

void test_static_link_up_ip_matches_reopens(void)
{
    TEST_ASSERT_EQUAL(StaticAction::Reopen, decideStatic(si(true, true, false)));
}

void test_static_link_up_ip_differs_reapplies(void)
{
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(true, false, false)));
}

void test_static_link_down_hw_reset(void)
{
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(false, true, false)));
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(false, false, false)));
}

// Wedged sockets with the correct SIPR: a stalled earlier Reopen must escalate to
// the hardware reset, not reopen forever.
void test_static_stalled_ip_matches_hw_reset(void)
{
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(true, true, true)));
}

void test_static_stalled_ip_differs_reapplies(void)
{
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(true, false, true)));
}

void test_static_stalled_link_down_hw_reset(void)
{
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(false, true, true)));
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(false, false, true)));
}

// Rate limit of the static hardware reset (same kHwResetMinGapMs as F2): while it is not
// allowed, every case that would reset falls back to Reapply; the other outcomes are unchanged.
void test_static_hw_reset_not_allowed_falls_back_to_reapply(void)
{
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(false, true, false, false)));   // link down
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(false, false, true, false)));
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(true, true, true, false)));     // wedged sockets
}

void test_static_hw_reset_not_allowed_leaves_other_outcomes(void)
{
    TEST_ASSERT_EQUAL(StaticAction::Reopen, decideStatic(si(true, true, false, false)));
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(true, false, false, false)));
    TEST_ASSERT_EQUAL(StaticAction::Reapply, decideStatic(si(true, false, true, false)));
}

void test_static_hw_reset_allowed_still_resets(void)
{
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(false, true, false, true)));
    TEST_ASSERT_EQUAL(StaticAction::HwReset, decideStatic(si(true, true, true, true)));
}

// The input is derived from the F2 state with the same gap.
void test_hw_reset_allowed_follows_gap(void)
{
    State st{};
    TEST_ASSERT_TRUE(hwResetAllowed(st, 1000));                       // never reset yet
    onHwReset(st, 1000);
    TEST_ASSERT_FALSE(hwResetAllowed(st, 1000 + kHwResetMinGapMs - 1));
    TEST_ASSERT_TRUE(hwResetAllowed(st, 1000 + kHwResetMinGapMs));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fixed_ip_reapplies_static);
    RUN_TEST(test_fixed_ip_ignores_other_inputs);
    RUN_TEST(test_lease_held_link_up_reopens_udp);
    RUN_TEST(test_no_lease_full_dhcp);
    RUN_TEST(test_link_down_full_dhcp);
    RUN_TEST(test_reopen_stalled_full_dhcp);
    RUN_TEST(test_not_stalled_reopens_udp);
    RUN_TEST(test_reopen_stalled_streak_link_up_hw_reset);
    RUN_TEST(test_reopen_stalled_link_down_full_dhcp);
    RUN_TEST(test_maintain_renew_fail_full_dhcp);
    RUN_TEST(test_maintain_rebind_fail_full_dhcp);
    RUN_TEST(test_maintain_ok_still_reopens_udp);
    RUN_TEST(test_f2_streak_thresholds);
    RUN_TEST(test_f2_suppressed_when_link_down);
    RUN_TEST(test_f2_backoff_and_release);
    RUN_TEST(test_f2_gap_survives_millis_wraparound);
    RUN_TEST(test_f2_first_reset_at_time_zero_state);
    RUN_TEST(test_f1_wins_over_f2);
    RUN_TEST(test_on_dhcp_result_counts_and_resets);
    RUN_TEST(test_on_dhcp_result_success_without_link_resets);
    RUN_TEST(test_on_dhcp_result_link_down_no_change);
    RUN_TEST(test_on_dhcp_result_saturates);
    RUN_TEST(test_on_hw_reset_keeps_streak);
    RUN_TEST(test_static_link_up_ip_matches_reopens);
    RUN_TEST(test_static_link_up_ip_differs_reapplies);
    RUN_TEST(test_static_link_down_hw_reset);
    RUN_TEST(test_static_stalled_ip_matches_hw_reset);
    RUN_TEST(test_static_stalled_ip_differs_reapplies);
    RUN_TEST(test_static_stalled_link_down_hw_reset);
    RUN_TEST(test_static_hw_reset_not_allowed_falls_back_to_reapply);
    RUN_TEST(test_static_hw_reset_not_allowed_leaves_other_outcomes);
    RUN_TEST(test_static_hw_reset_allowed_still_resets);
    RUN_TEST(test_hw_reset_allowed_follows_gap);
    return UNITY_END();
}
