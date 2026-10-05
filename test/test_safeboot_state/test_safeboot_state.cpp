// Native testsuite for safeboot::OtaSession -- the host-testable OTA
// session state machine (src/safeboot/ota_state.h), driven purely by
// injected `now_ms` values, no Arduino / no real millis(). Binding contract:
// docs/safeboot-ota-contract.md ("State machine" + "/ota/state" sections).
// Covers the two bugs from docs/BACKLOG.md TM-46 (unsigned cross-task
// millis() underflow aborting a healthy upload) and TM-49 (partition switch
// after a partial image / a late disconnect retracting an earned verdict).
//
//   pio test -e native_safeboot -f test_safeboot_state

#include <unity.h>

#include <string.h>

#include <safeboot/fw_apply.h>
#include <safeboot/ota_state.h>

using safeboot::OtaSession;

void setUp(void) {}
void tearDown(void) {}

// Drains the session's action queue once into a fixed snapshot so a test
// can assert several counts against the same batch of actions without a
// second pop() call silently seeing an already-empty queue.
struct ActionList {
    OtaSession::Action items[16];
    int count = 0;
};

static ActionList drainActions(OtaSession& s) {
    ActionList list;
    OtaSession::Action a;
    while (list.count < 16 && s.pop(a)) {
        list.items[list.count++] = a;
    }
    return list;
}

static int countIn(const ActionList& list, OtaSession::ActionType type, OtaSession::Reason reason) {
    int count = 0;
    for (int i = 0; i < list.count; ++i) {
        if (list.items[i].type == type && list.items[i].reason == reason) {
            ++count;
        }
    }
    return count;
}

// Convenience for the common case: exactly one countIn() check, queue
// drained fresh for it.
static int countActions(OtaSession& s, OtaSession::ActionType type, OtaSession::Reason reason) {
    return countIn(drainActions(s), type, reason);
}

// ---------------------------------------------------------------------
// 1. Good path: start, chunks, final, verified ok.
// ---------------------------------------------------------------------
static void test_good_path_reaches_done_with_one_switch_partition(void) {
    OtaSession s;
    s.begin(0);
    uint32_t gen = s.onStart(1000, 2000);
    TEST_ASSERT_EQUAL_UINT32(1, gen);

    s.onChunk(1010, 1000);
    s.onChunk(1020, 1000);
    s.onFinalReceived(1030);
    TEST_ASSERT_EQUAL(OtaSession::State::Verifying, s.state().state);

    s.onVerified(1040, true);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Done, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::None, st.reason);
    TEST_ASSERT_TRUE(st.image_valid);
    TEST_ASSERT_NOT_EQUAL(-1, st.fallback_in_ms);

    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
}

// ---------------------------------------------------------------------
// 2. Killed at 50%: disconnect mid-transfer.
// ---------------------------------------------------------------------
static void test_disconnect_mid_upload_aborts_client_disconnected(void) {
    OtaSession s;
    s.begin(0);
    uint32_t gen = s.onStart(1000, 2000);
    s.onChunk(1010, 1000); // half the declared total

    s.onDisconnect(1020, gen);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::ClientDisconnected, st.reason);
    TEST_ASSERT_FALSE(st.image_valid);
    TEST_ASSERT_EQUAL_INT32((int32_t)OtaSession::FALLBACK_MS, st.fallback_in_ms);

    ActionList actions = drainActions(s);
    TEST_ASSERT_EQUAL(0, countIn(actions, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
    TEST_ASSERT_EQUAL(1, countIn(actions, OtaSession::ActionType::Abort, OtaSession::Reason::ClientDisconnected));
}

// ---------------------------------------------------------------------
// 3. Stall watchdog.
// ---------------------------------------------------------------------
static void test_stall_watchdog_aborts_after_30s_of_silence(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 2000);
    s.onChunk(1000, 100);

    s.tick(1000 + 29999);
    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, s.state().state);

    s.tick(1000 + 30001);
    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::Stalled, st.reason);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::Abort, OtaSession::Reason::Stalled));

    // A fresh session still works afterwards and bumps the generation.
    uint32_t gen2 = s.onStart(1000 + 30002, 500);
    TEST_ASSERT_EQUAL_UINT32(2, gen2);
    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, s.state().state);
}

// ---------------------------------------------------------------------
// 4. Late disconnect of a superseded session.
// ---------------------------------------------------------------------
static void test_stale_generation_disconnect_is_ignored(void) {
    OtaSession s;
    s.begin(0);
    uint32_t gen1 = s.onStart(1000, 2000);
    TEST_ASSERT_EQUAL_UINT32(1, gen1);

    uint32_t gen2 = s.onStart(1500, 2000); // stale_session abort queued, gen2 armed
    TEST_ASSERT_EQUAL_UINT32(2, gen2);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::Abort, OtaSession::Reason::StaleSession));

    s.onChunk(1510, 200);
    s.onDisconnect(1520, gen1); // late event for the dead session -> ignored

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, st.state);
    TEST_ASSERT_EQUAL_UINT32(gen2, st.generation);
    TEST_ASSERT_EQUAL_UINT32(200, st.received);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::Abort, OtaSession::Reason::ClientDisconnected));
}

// ---------------------------------------------------------------------
// 5. Disconnect after the verdict must not retract it (TM-49).
// ---------------------------------------------------------------------
static void test_disconnect_after_done_does_not_retract_verdict(void) {
    OtaSession s;
    s.begin(0);
    uint32_t gen = s.onStart(1000, 100);
    s.onChunk(1010, 100);
    s.onFinalReceived(1020);
    s.onVerified(1030, true);
    // drain the good-path actions so they don't leak into this test's asserts
    OtaSession::Action a;
    while (s.pop(a)) {}

    s.onDisconnect(1040, gen);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Done, st.state);
    TEST_ASSERT_TRUE(st.image_valid);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::Abort, OtaSession::Reason::ClientDisconnected));
}

// ---------------------------------------------------------------------
// 6. MD5 mismatch.
// ---------------------------------------------------------------------
static void test_md5_mismatch_aborts_without_switch(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 100);
    s.onChunk(1010, 100);
    s.onFinalReceived(1020);

    s.onVerified(1030, false, OtaSession::Reason::Md5Mismatch);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::Md5Mismatch, st.reason);
    TEST_ASSERT_FALSE(st.image_valid);
    ActionList actions = drainActions(s);
    TEST_ASSERT_EQUAL(0, countIn(actions, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
    TEST_ASSERT_EQUAL(1, countIn(actions, OtaSession::ActionType::Abort, OtaSession::Reason::Md5Mismatch));
}

// A node whose ota_0 was invalidated by an earlier aborted attempt starts
// the upload with app_valid false. A successful upload must flip it back, so
// the "image incomplete" notice does not stay next to "Update installed".
static void test_successful_upload_makes_app_valid(void) {
    OtaSession s;
    s.begin(0);
    s.setAppValid(false);
    TEST_ASSERT_FALSE(s.state().app_valid);

    s.onStart(1000, 100);
    s.onChunk(1010, 100);
    s.onFinalReceived(1020);
    s.onVerified(1030, true);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Done, st.state);
    TEST_ASSERT_TRUE(st.image_valid);
    TEST_ASSERT_TRUE(st.app_valid);
}

// Update.end() failure reasons other than the MD5 check (Defect 1): each
// aborts without a partition switch and carries its own contract string.
static void abortWithReason(OtaSession& s, OtaSession::Reason reason) {
    s.begin(0);
    s.onStart(1000, 100);
    s.onChunk(1010, 100);
    s.onFinalReceived(1020);
    s.onVerified(1030, false, reason);
}

static void test_not_bootable_aborts_without_switch(void) {
    OtaSession s;
    abortWithReason(s, OtaSession::Reason::NotBootable);
    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::NotBootable, st.reason);
    TEST_ASSERT_EQUAL_STRING("not_bootable", OtaSession::reasonName(st.reason));
    TEST_ASSERT_FALSE(st.image_valid);
    ActionList actions = drainActions(s);
    TEST_ASSERT_EQUAL(0, countIn(actions, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
    TEST_ASSERT_EQUAL(1, countIn(actions, OtaSession::ActionType::Abort, OtaSession::Reason::NotBootable));
}

static void test_activate_failed_aborts_without_switch(void) {
    OtaSession s;
    abortWithReason(s, OtaSession::Reason::ActivateFailed);
    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::ActivateFailed, st.reason);
    TEST_ASSERT_EQUAL_STRING("activate_failed", OtaSession::reasonName(st.reason));
    TEST_ASSERT_FALSE(st.image_valid);
    ActionList actions = drainActions(s);
    TEST_ASSERT_EQUAL(0, countIn(actions, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
    TEST_ASSERT_EQUAL(1, countIn(actions, OtaSession::ActionType::Abort, OtaSession::Reason::ActivateFailed));
}

static void test_update_error_aborts_without_switch(void) {
    OtaSession s;
    abortWithReason(s, OtaSession::Reason::UpdateError);
    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::UpdateError, st.reason);
    TEST_ASSERT_EQUAL_STRING("update_error", OtaSession::reasonName(st.reason));
    TEST_ASSERT_FALSE(st.image_valid);
    ActionList actions = drainActions(s);
    TEST_ASSERT_EQUAL(0, countIn(actions, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
    TEST_ASSERT_EQUAL(1, countIn(actions, OtaSession::ActionType::Abort, OtaSession::Reason::UpdateError));
}

static void test_update_error_reason_names(void) {
    TEST_ASSERT_EQUAL_STRING("not_bootable", OtaSession::reasonName(OtaSession::Reason::NotBootable));
    TEST_ASSERT_EQUAL_STRING("activate_failed", OtaSession::reasonName(OtaSession::Reason::ActivateFailed));
    TEST_ASSERT_EQUAL_STRING("update_error", OtaSession::reasonName(OtaSession::Reason::UpdateError));
}

static void test_from_updater_error_mapping(void) {
    TEST_ASSERT_EQUAL(OtaSession::Reason::Md5Mismatch, OtaSession::fromUpdaterError(7));
    TEST_ASSERT_EQUAL(OtaSession::Reason::NotBootable, OtaSession::fromUpdaterError(3));
    TEST_ASSERT_EQUAL(OtaSession::Reason::ActivateFailed, OtaSession::fromUpdaterError(9));
    TEST_ASSERT_EQUAL(OtaSession::Reason::UpdateError, OtaSession::fromUpdaterError(0));
    TEST_ASSERT_EQUAL(OtaSession::Reason::UpdateError, OtaSession::fromUpdaterError(1));
    TEST_ASSERT_EQUAL(OtaSession::Reason::UpdateError, OtaSession::fromUpdaterError(12));
}

// ---------------------------------------------------------------------
// 7. Incomplete upload: final frame never arrived.
// ---------------------------------------------------------------------
static void test_incomplete_upload_reason_contract_string(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 2000);
    s.onChunk(1010, 500); // well short of total, no final ever received

    // The completion handler's fail-closed gate (TM-49): image never
    // verified, so it reports the session as incomplete rather than
    // silently falling through client_disconnected.
    s.onVerified(1020, false, OtaSession::Reason::IncompleteUpload);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::IncompleteUpload, st.reason);
    TEST_ASSERT_FALSE(st.image_valid);
    TEST_ASSERT_EQUAL_STRING("incomplete_upload", OtaSession::reasonName(st.reason));
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
}

// ---------------------------------------------------------------------
// 8. Wrap-around and reversed-order clock races (TM-46).
// ---------------------------------------------------------------------
static void test_millis_wraparound_does_not_false_abort(void) {
    OtaSession s;
    uint32_t boot = 0xFFFFFF00u; // 256 ms before the 32-bit rollover
    s.begin(boot);
    s.onStart(boot, 1000);

    uint32_t last_chunk = boot + 400; // wraps past 0xFFFFFFFF during this call
    s.onChunk(last_chunk, 100);
    TEST_ASSERT_TRUE(last_chunk < boot); // sanity: we did wrap numerically

    s.tick(last_chunk + 100); // well within the stall window, across the wrap
    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, s.state().state);

    s.tick(last_chunk + 31000); // now stalled
    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Aborted, st.state);
    TEST_ASSERT_EQUAL(OtaSession::Reason::Stalled, st.reason);
}

static void test_reversed_order_tick_before_stored_chunk_does_not_abort(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(0, 1000);
    s.onChunk(1000, 100); // async task stores last_data_ms_ = 1000

    // loop() read millis() a moment before that store landed.
    s.tick(999);

    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, s.state().state);
}

// ---------------------------------------------------------------------
// 9. Cancel request.
// ---------------------------------------------------------------------
static void test_cancel_during_upload_is_refused(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 2000);
    s.onChunk(1010, 100);

    bool accepted = s.onCancelRequest(1020);

    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_EQUAL(OtaSession::State::Receiving, s.state().state);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Cancel));
}

static void test_cancel_while_idle_reboots_to_app(void) {
    OtaSession s;
    s.begin(0);

    bool accepted = s.onCancelRequest(5000);

    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Cancel));
}

// ---------------------------------------------------------------------
// 10. Fallback-to-app timeout, including re-arm after an abort.
// ---------------------------------------------------------------------
static void test_fallback_timeout_reboots_to_app_after_180s(void) {
    OtaSession s;
    s.begin(0);

    s.tick(179999);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));

    s.tick(180001);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));
}

static void test_fallback_window_rearms_on_abort(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 100);
    s.onChunk(1000, 10);
    s.tick(1000 + OtaSession::STALL_MS + 1); // aborts (stalled) at t = 31001, re-arms fallback there
    OtaSession::Action a;
    while (s.pop(a)) {} // drain the stall abort, not under test here

    uint32_t abort_at = 1000 + OtaSession::STALL_MS + 1; // 31001

    s.tick(abort_at + 100000); // 131101: window re-armed at abort_at, not yet elapsed
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));

    s.tick(abort_at + OtaSession::FALLBACK_MS + 1); // now elapsed since abort_at
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));
}

// ---------------------------------------------------------------------
// 11. fallback_in_ms is suspended (-1) while an upload is active.
// ---------------------------------------------------------------------
static void test_fallback_in_ms_suspended_while_receiving_and_verifying(void) {
    OtaSession s;
    s.begin(0);
    s.onStart(1000, 100);
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);

    s.onChunk(1010, 50);
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);

    s.onFinalReceived(1020);
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);
}

// ---------------------------------------------------------------------
// 12. Single app slot: app_valid gates the fallback and cancel (bench
// finding, docs/safeboot-ota-contract.md "Single app slot").
// ---------------------------------------------------------------------
static void test_app_invalid_suspends_fallback_and_refuses_cancel(void) {
    OtaSession s;
    s.begin(0);
    s.setAppValid(false);

    s.tick(400000);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);
    TEST_ASSERT_FALSE(s.state().app_valid);

    bool accepted = s.onCancelRequest(400100);
    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Cancel));
}

static void test_app_invalid_then_good_upload_still_switches_partition(void) {
    OtaSession s;
    s.begin(0);
    s.setAppValid(false);

    uint32_t gen = s.onStart(1000, 100);
    TEST_ASSERT_EQUAL_UINT32(1, gen);
    s.onChunk(1010, 100);
    s.onFinalReceived(1020);
    s.onVerified(1030, true);

    const OtaSession::Status& st = s.state();
    TEST_ASSERT_EQUAL(OtaSession::State::Done, st.state);
    TEST_ASSERT_TRUE(st.image_valid);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::SwitchPartition, OtaSession::Reason::None));
}

static void test_setappvalid_true_rearms_fallback_from_that_moment(void) {
    OtaSession s;
    s.begin(0);
    s.setAppValid(false);
    s.tick(50000); // app invalid for a while, now_ tracks this tick

    s.setAppValid(true); // re-arms the fallback window starting at now_ (50000)
    TEST_ASSERT_TRUE(s.state().app_valid);

    s.tick(50000 + 179000);
    TEST_ASSERT_EQUAL(0, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));

    s.tick(50000 + 181000);
    TEST_ASSERT_EQUAL(1, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));
}

// ---------------------------------------------------------------------
// AU-07: Applying state (offline apply of a staged update).
// ---------------------------------------------------------------------
static void test_applying_state_is_truthful_and_quiet(void) {
    OtaSession s;
    s.begin(0);
    s.onApplyBegin(100, 1000);
    TEST_ASSERT_EQUAL_STRING("applying", OtaSession::stateName(s.state().state));
    TEST_ASSERT_EQUAL_UINT32(1000, s.state().total);
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);
    TEST_ASSERT_FALSE(s.state().image_valid);

    s.onApplyProgress(200, 400);
    TEST_ASSERT_EQUAL_UINT32(400, s.state().received);

    // no stall abort, no fallback reboot, cancel refused while applying
    s.tick(100 + OtaSession::STALL_MS + OtaSession::FALLBACK_MS + 5000);
    TEST_ASSERT_EQUAL_INT(0, drainActions(s).count);
    TEST_ASSERT_FALSE(s.onCancelRequest(300));
    TEST_ASSERT_EQUAL_INT(0, drainActions(s).count);
}

static void test_apply_ok_ends_done_with_image_valid(void) {
    OtaSession s;
    s.begin(0);
    s.onApplyBegin(10, 500);
    s.onApplyEnd(20, true);
    TEST_ASSERT_EQUAL_STRING("done", OtaSession::stateName(s.state().state));
    TEST_ASSERT_TRUE(s.state().image_valid);
    TEST_ASSERT_EQUAL_STRING("", OtaSession::reasonName(s.state().reason));
}

static void test_apply_failed_ends_aborted_and_rearms_fallback(void) {
    OtaSession s;
    s.begin(0);
    s.onApplyBegin(10, 500);
    s.onApplyEnd(20, false);
    TEST_ASSERT_EQUAL_STRING("aborted", OtaSession::stateName(s.state().state));
    TEST_ASSERT_EQUAL_STRING("apply_failed", OtaSession::reasonName(s.state().reason));
    TEST_ASSERT_FALSE(s.state().image_valid);
    // app still valid (a CRC failure never touched ota_0): the fallback runs from the verdict
    s.tick(20 + OtaSession::FALLBACK_MS + 1);
    TEST_ASSERT_EQUAL_INT(1, countActions(s, OtaSession::ActionType::RebootToApp, OtaSession::Reason::Timeout));
    // progress / end outside Applying are no-ops
    s.onApplyProgress(30, 99);
    s.onApplyEnd(30, true);
    TEST_ASSERT_EQUAL_STRING("aborted", OtaSession::stateName(s.state().state));
}

static void test_apply_failed_with_invalid_app_stays_quiet(void) {
    OtaSession s;
    s.begin(0);
    s.onApplyBegin(10, 500);
    s.onApplyEnd(20, false);
    s.setAppValid(false); // erase happened, inflate failed
    s.tick(20 + 10 * OtaSession::FALLBACK_MS);
    TEST_ASSERT_EQUAL_INT(0, drainActions(s).count);
    TEST_ASSERT_EQUAL_INT32(-1, s.state().fallback_in_ms);
    // an upload still works afterwards
    s.onStart(50000, 0);
    TEST_ASSERT_EQUAL_STRING("receiving", OtaSession::stateName(s.state().state));
}

// ---------------------------------------------------------------------
// AU-07: fwApplyPlan.
// ---------------------------------------------------------------------
#define SLOT 0x1A0000u // 1664 KB ota_0 of the 4 MB safeboot table

static FwStageRecord goodRec(void) {
    FwStageRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = FW_STAGE_MAGIC;
    r.off = 0x120000;
    r.zlen = 0x7C000;  // 0x120000 + 0x7C000 = 0x19C000 <= SLOT
    r.ilen = 0x118123; // below off, not 4 KB aligned
    r.crc32 = 0xDEADBEEF;
    return r;
}

static void test_plan_valid(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    TEST_ASSERT_TRUE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("ok", why);
    TEST_ASSERT_EQUAL_UINT32(r.off, p.srcOff);
    TEST_ASSERT_EQUAL_UINT32(r.zlen, p.zlen);
    TEST_ASSERT_EQUAL_UINT32(r.ilen, p.ilen);
    TEST_ASSERT_EQUAL_UINT32(0x119000, p.eraseLen); // 0x118123 rounded up to 4 KB
    TEST_ASSERT_TRUE(p.eraseLen <= r.off);          // the staged tail is never erased
    TEST_ASSERT_TRUE(fwApplyPlan(r, SLOT, p, nullptr)); // reason is optional
}

static void test_plan_erase_len_exact_multiple_and_boundaries(void) {
    FwApplyPlan p;
    FwStageRecord r = goodRec();
    r.ilen = 0x118000; // already aligned: no extra sector
    TEST_ASSERT_TRUE(fwApplyPlan(r, SLOT, p, nullptr));
    TEST_ASSERT_EQUAL_UINT32(0x118000, p.eraseLen);

    r.ilen = r.off; // exactly up to the stage area is allowed
    TEST_ASSERT_TRUE(fwApplyPlan(r, SLOT, p, nullptr));
    TEST_ASSERT_EQUAL_UINT32(r.off, p.eraseLen);

    r = goodRec();
    r.zlen = SLOT - r.off; // tail ends exactly at the slot end
    TEST_ASSERT_TRUE(fwApplyPlan(r, SLOT, p, nullptr));
}

static void test_plan_refuses_off_plus_zlen_beyond_slot(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    r.zlen = SLOT - r.off + 1;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("range", why);
    TEST_ASSERT_EQUAL_UINT32(0, p.eraseLen);

    r = goodRec();
    r.off = SLOT + 0x10000; // off beyond the slot
    r.zlen = 1;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("range", why);

    r = goodRec();
    r.zlen = 0xFFFFFFF0u; // 32-bit wrap must not slip through
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("range", why);
}

static void test_plan_refuses_inflate_reaching_the_stage_area(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    r.ilen = r.off + 1;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("overlap", why);
    r.ilen = 0xFFFFFFFFu; // would wrap the 4 KB round-up
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("overlap", why);
}

static void test_plan_refuses_unaligned_off(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    r.off += 0x1000; // 4 KB aligned but not 64 KB aligned
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("align", why);
    r = goodRec();
    r.off += 1;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("align", why);
}

static void test_plan_refuses_zero_sizes(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    r.zlen = 0;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("size", why);
    r = goodRec();
    r.ilen = 0;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("size", why);
}

static void test_plan_refuses_bad_magic_and_decode_roundtrip(void) {
    FwApplyPlan p;
    const char* why = nullptr;
    FwStageRecord r = goodRec();
    r.magic = 0x12345678;
    TEST_ASSERT_FALSE(fwApplyPlan(r, SLOT, p, &why));
    TEST_ASSERT_EQUAL_STRING("magic", why);

    // the record as the app stores it survives encode -> decode -> plan
    r = goodRec();
    uint8_t buf[FW_STAGE_ENC_LEN];
    TEST_ASSERT_EQUAL_UINT(FW_STAGE_ENC_LEN, fwRecordEncode(r, buf, sizeof(buf)));
    FwStageRecord d;
    TEST_ASSERT_TRUE(fwRecordDecode(buf, sizeof(buf), d));
    TEST_ASSERT_TRUE(fwApplyPlan(d, SLOT, p, nullptr));
    TEST_ASSERT_EQUAL_UINT32(r.crc32, d.crc32);

    // a record with a damaged magic never decodes (Safeboot drops it as "record")
    buf[0] ^= 0xFF;
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), d));
    buf[0] ^= 0xFF;
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf) - 1, d));
}

// ---------------------------------------------------------------------
// AU-07: fwInflateStream with a fake decompressor (identity: the "stream"
// is the output). Drives the chunking, the circular dictionary wrap, the
// MORE_INPUT flag and the ilen bound without the ROM tinfl.
// ---------------------------------------------------------------------
enum FakeMode { FAKE_IDENTITY, FAKE_NEVER_DONE, FAKE_ERROR_AT_HALF, FAKE_STALL };

struct FakeDec {
    FakeMode mode = FAKE_IDENTITY;
    uint32_t consumed = 0;
    uint32_t calls = 0;
    uint32_t moreFalseCalls = 0;
    uint32_t zlen = 0;
    size_t dictSize = 0; // set by the test: tinfl's BAD_PARAM contract is checked on every call
    int step(const uint8_t* in, size_t* inSize, uint8_t* dictStart, uint8_t* outNext, size_t* outSize, bool more) {
        calls++;
        if (!more) moreFalseCalls++;
        // tinfl_decompress() returns BAD_PARAM unless the dictionary is a power of two and
        // the output window runs exactly to its end.
        TEST_ASSERT_TRUE(dictSize != 0 && (dictSize & (dictSize - 1)) == 0);
        TEST_ASSERT_TRUE(outNext >= dictStart);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)dictSize, (uint32_t)((size_t)(outNext - dictStart) + *outSize));
        const size_t inAvail = *inSize;
        const size_t outCap = *outSize;
        if (mode == FAKE_STALL) {
            *inSize = 0;
            *outSize = 0;
            return FWI_ST_HAS_MORE_OUTPUT;
        }
        const size_t n = inAvail < outCap ? inAvail : outCap;
        if (mode == FAKE_ERROR_AT_HALF && consumed + n > zlen / 2) {
            *inSize = 0;
            *outSize = 0;
            return -1; // TINFL_STATUS_FAILED
        }
        for (size_t i = 0; i < n; i++) outNext[i] = in[i];
        *inSize = n;
        *outSize = n;
        consumed += (uint32_t)n;
        if (mode == FAKE_NEVER_DONE) return FWI_ST_NEEDS_MORE_INPUT;
        if (n < inAvail) return FWI_ST_HAS_MORE_OUTPUT; // dictionary full, input left
        return more ? FWI_ST_NEEDS_MORE_INPUT : FWI_ST_DONE;
    }
};

struct MemRd {
    const uint8_t* data;
    uint32_t len;
    bool fail = false;
    uint32_t reads = 0;
    bool read(uint32_t pos, uint8_t* dst, size_t n) {
        reads++;
        if (fail || pos + n > len) return false;
        memcpy(dst, data + pos, n);
        return true;
    }
};

struct MemWr {
    uint8_t out[4096];
    uint32_t next = 0; // enforces sequential writes
    int failAfter = -1;
    int writes = 0;
    bool sequential = true;
    bool write(uint32_t pos, const uint8_t* src, size_t n) {
        if (pos != next || pos + n > sizeof(out)) {
            sequential = false;
            return false;
        }
        if (failAfter >= 0 && writes >= failAfter) return false;
        memcpy(out + pos, src, n);
        next += (uint32_t)n;
        writes++;
        return true;
    }
};

static uint8_t g_src[1000];
static uint8_t g_in[16];
static uint8_t g_dict[64];

static void fillSrc(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) g_src[i] = (uint8_t)(i * 7 + 3);
}

static void test_inflate_streams_through_small_buffers_and_wraps_the_dictionary(void) {
    fillSrc(1000);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.zlen = 1000;
    MemRd rd = {g_src, 1000};
    MemWr wr;
    uint32_t total = 0;
    FwInflateResult r = fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 1000, 1000, &total);
    TEST_ASSERT_EQUAL_INT(FWI_OK, r);
    TEST_ASSERT_EQUAL_UINT32(1000, total);
    TEST_ASSERT_TRUE(wr.sequential);
    TEST_ASSERT_EQUAL_MEMORY(g_src, wr.out, 1000);
    TEST_ASSERT_EQUAL_UINT32(63, rd.reads); // ceil(1000 / 16) chunks
    TEST_ASSERT_EQUAL_UINT32(1, dec.moreFalseCalls); // only the final chunk is announced as the end
}

static void test_inflate_more_input_flag_only_clear_on_the_last_chunk(void) {
    fillSrc(32);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.zlen = 32;
    MemRd rd = {g_src, 32};
    MemWr wr;
    uint32_t total = 0;
    TEST_ASSERT_EQUAL_INT(FWI_OK, fwInflateStream(dec, rd, wr, g_in, 16, g_dict, 64, 32, 32, &total));
    TEST_ASSERT_EQUAL_UINT32(1, dec.moreFalseCalls); // 2 chunks, only the second is "last"
    TEST_ASSERT_EQUAL_UINT32(2, dec.calls);
}

static void test_inflate_refuses_output_beyond_ilen_without_writing_it(void) {
    fillSrc(100);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.zlen = 100;
    MemRd rd = {g_src, 100};
    MemWr wr;
    uint32_t total = 0;
    FwInflateResult r = fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 50, &total);
    TEST_ASSERT_EQUAL_INT(FWI_OVERFLOW, r);
    TEST_ASSERT_TRUE(total <= 50);
    TEST_ASSERT_TRUE(wr.next <= 50);
}

static void test_inflate_short_output_is_a_size_error(void) {
    fillSrc(100);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.zlen = 100;
    MemRd rd = {g_src, 100};
    MemWr wr;
    uint32_t total = 0;
    FwInflateResult r = fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 120, &total);
    TEST_ASSERT_EQUAL_INT(FWI_SIZE, r);
    TEST_ASSERT_EQUAL_UINT32(100, total);
}

static void test_inflate_truncated_input(void) {
    fillSrc(100);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.mode = FAKE_NEVER_DONE;
    dec.zlen = 100;
    MemRd rd = {g_src, 100};
    MemWr wr;
    uint32_t total = 0;
    TEST_ASSERT_EQUAL_INT(FWI_TRUNC, fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 100, &total));
}

static void test_inflate_decoder_error_and_stall(void) {
    fillSrc(100);
    FakeDec dec;
    dec.dictSize = sizeof(g_dict);
    dec.mode = FAKE_ERROR_AT_HALF;
    dec.zlen = 100;
    MemRd rd = {g_src, 100};
    MemWr wr;
    uint32_t total = 0;
    TEST_ASSERT_EQUAL_INT(FWI_DATA, fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 100, &total));

    FakeDec stall;

    stall.dictSize = sizeof(g_dict);
    stall.mode = FAKE_STALL;
    MemRd rd2 = {g_src, 100};
    MemWr wr2;
    TEST_ASSERT_EQUAL_INT(FWI_DATA, fwInflateStream(stall, rd2, wr2, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 100, &total));
    TEST_ASSERT_TRUE(stall.calls < 10); // gave up instead of spinning
}

static void test_inflate_io_errors_and_params(void) {
    fillSrc(100);
    uint32_t total = 0;
    {
        FakeDec dec;
        dec.dictSize = sizeof(g_dict);
        dec.zlen = 100;
        MemRd rd = {g_src, 100};
        rd.fail = true;
        MemWr wr;
        TEST_ASSERT_EQUAL_INT(FWI_READ, fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 100, &total));
    }
    {
        FakeDec dec;
        dec.dictSize = sizeof(g_dict);
        dec.zlen = 100;
        MemRd rd = {g_src, 100};
        MemWr wr;
        wr.failAfter = 2;
        TEST_ASSERT_EQUAL_INT(FWI_WRITE, fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, sizeof(g_dict), 100, 100, &total));
    }
    {
        FakeDec dec;
        dec.dictSize = sizeof(g_dict);
        MemRd rd = {g_src, 100};
        MemWr wr;
        TEST_ASSERT_EQUAL_INT(FWI_PARAM, fwInflateStream(dec, rd, wr, g_in, sizeof(g_in), g_dict, 48, 100, 100, &total));
        TEST_ASSERT_EQUAL_INT(FWI_PARAM, fwInflateStream(dec, rd, wr, g_in, 0, g_dict, 64, 100, 100, &total));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_good_path_reaches_done_with_one_switch_partition);
    RUN_TEST(test_disconnect_mid_upload_aborts_client_disconnected);
    RUN_TEST(test_stall_watchdog_aborts_after_30s_of_silence);
    RUN_TEST(test_stale_generation_disconnect_is_ignored);
    RUN_TEST(test_disconnect_after_done_does_not_retract_verdict);
    RUN_TEST(test_md5_mismatch_aborts_without_switch);
    RUN_TEST(test_successful_upload_makes_app_valid);
    RUN_TEST(test_not_bootable_aborts_without_switch);
    RUN_TEST(test_activate_failed_aborts_without_switch);
    RUN_TEST(test_update_error_aborts_without_switch);
    RUN_TEST(test_update_error_reason_names);
    RUN_TEST(test_from_updater_error_mapping);
    RUN_TEST(test_incomplete_upload_reason_contract_string);
    RUN_TEST(test_millis_wraparound_does_not_false_abort);
    RUN_TEST(test_reversed_order_tick_before_stored_chunk_does_not_abort);
    RUN_TEST(test_cancel_during_upload_is_refused);
    RUN_TEST(test_cancel_while_idle_reboots_to_app);
    RUN_TEST(test_fallback_timeout_reboots_to_app_after_180s);
    RUN_TEST(test_fallback_window_rearms_on_abort);
    RUN_TEST(test_fallback_in_ms_suspended_while_receiving_and_verifying);
    RUN_TEST(test_app_invalid_suspends_fallback_and_refuses_cancel);
    RUN_TEST(test_app_invalid_then_good_upload_still_switches_partition);
    RUN_TEST(test_setappvalid_true_rearms_fallback_from_that_moment);
    RUN_TEST(test_applying_state_is_truthful_and_quiet);
    RUN_TEST(test_apply_ok_ends_done_with_image_valid);
    RUN_TEST(test_apply_failed_ends_aborted_and_rearms_fallback);
    RUN_TEST(test_apply_failed_with_invalid_app_stays_quiet);
    RUN_TEST(test_plan_valid);
    RUN_TEST(test_plan_erase_len_exact_multiple_and_boundaries);
    RUN_TEST(test_plan_refuses_off_plus_zlen_beyond_slot);
    RUN_TEST(test_plan_refuses_inflate_reaching_the_stage_area);
    RUN_TEST(test_plan_refuses_unaligned_off);
    RUN_TEST(test_plan_refuses_zero_sizes);
    RUN_TEST(test_plan_refuses_bad_magic_and_decode_roundtrip);
    RUN_TEST(test_inflate_streams_through_small_buffers_and_wraps_the_dictionary);
    RUN_TEST(test_inflate_more_input_flag_only_clear_on_the_last_chunk);
    RUN_TEST(test_inflate_refuses_output_beyond_ilen_without_writing_it);
    RUN_TEST(test_inflate_short_output_is_a_size_error);
    RUN_TEST(test_inflate_truncated_input);
    RUN_TEST(test_inflate_decoder_error_and_stall);
    RUN_TEST(test_inflate_io_errors_and_params);
    return UNITY_END();
}
