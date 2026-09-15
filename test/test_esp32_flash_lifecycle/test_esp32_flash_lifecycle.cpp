// Regression suite for the D1-04 W3 esp32_flash.cpp handle-lifecycle defect:
//
//   Preferences preferences;  -- ONE shared global handle in esp32_flash.cpp.
//   init_flash() does preferences.begin("Credentials", false) near its top
//   and preferences.end() near its bottom; the whole field load happens
//   between them. save_settings() does its OWN begin()/end() pair.
//
//   Until fixed (same day, same wave), sanitize_loaded_settings() -- called
//   from inside init_flash()'s own open load handle -- ended with its own
//   call to save_settings(). Real Preferences semantics (verified against
//   ~/.platformio/packages/framework-arduinoespressif32/libraries/Preferences/src/Preferences.cpp):
//   begin() returns false immediately if already started (does NOT reopen);
//   end() unconditionally closes if started; every get*()/put*() checks
//   _started first. So the nested save_settings() call wrote through
//   init_flash()'s STILL-OPEN handle (its own begin() was a no-op) and then
//   CLOSED it via its own end() -- before init_flash() itself had finished
//   with it.
//
// THE FIX UNDER TEST: sanitize_loaded_settings() makes no Preferences calls
// at all any more (settings_sanitize.h is Arduino-free by design). init_flash()
// calls it, THEN preferences.end(), THEN save_settings() -- with a fresh
// handle of save_settings()'s own.
//
// WHY THIS SUITE EXISTS, NOT test_esp32_settings_nvs: that suite is built
// WITH -D MC_SAFEBOOT, which compiles sanitize_loaded_settings() OUT of
// esp32_flash.cpp entirely (`#if !defined(MC_SAFEBOOT)`) -- the exact code
// path this defect lived in. This suite (env:native_esp32_flash_lifecycle)
// is built WITHOUT it, specifically to put that path under test. Getting a
// non-safeboot native build to link means stubbing the board-configuration
// include chain sanitize_loaded_settings() pulls in transitively:
//
//   - settings_sanitize.h/.cpp: pure C++, no Arduino dependency (per its own
//     header comment) -- compiled for REAL, not stubbed.
//   - msgid_counter.h/.cpp: pure C++, no Arduino dependency (ditto) --
//     compiled for REAL.
//   - lora_setchip.h: real header pulls in <Arduino.h>/<configuration.h>
//     (a full board variant selected by a BOARD_* macro) /<debugconf.h> for
//     radio-control declarations esp32_flash.cpp's sanitize path never
//     touches -- only `max_country` is used. Stubbed (stubs/lora_setchip.h),
//     same one-line shape as test/test_nrf52_settings_paths/stubs/lora_setchip.h
//     already uses for the identical narrowing on the nRF52 sanitize path.
//   - Preferences.h: the fake in this directory, same fidelity contract as
//     test_esp32_settings_nvs's own copy (see stubs/Preferences.h), plus a
//     generation counter that makes "did this write happen while the load's
//     own handle was still open" a directly assertable fact.
//
// Everything else under test -- src/esp32/esp32_flash.cpp's
// init_flash()/save_settings()/sanitize_loaded_settings(), src/settings_store.cpp,
// src/settings_schema.cpp -- is the REAL product source, exactly like
// test_esp32_settings_nvs.
//
//   pio test -e native_esp32_flash_lifecycle
//
// MUTATION USED TO VERIFY EVERY CASE BELOW (see the wave report for the
// full pass/fail transcript of each): src/esp32/esp32_flash.cpp temporarily
// edited to remove the `save_settings();` call after init_flash()'s own
// preferences.end(), and instead call save_settings() as the last statement
// of sanitize_loaded_settings() -- bce95db5's original shape, the one this
// fix moved away from. Restored exactly afterwards; `git diff --stat` on
// this file must show only the Task 2 guard once this suite is done.
#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <esp32/esp32_flash.h> // s_meshcom_settings, meshcom_settings, init_flash()/save_settings()
#include <msgid_counter.h>     // msgIdAfterLoad() -- the real function, for computing expected values
#include <counters_store.h>    // countersLoad()/countersSave() -- the ESP32 backend under test here
#include <settings_schema.h>
#include <settings_store.h>

#include "Preferences.h" // FakeNvs

// The one global handle esp32_flash.cpp defines and uses throughout --
// declared here so tests can assert on its lifecycle (isStarted()/generation())
// without esp32_flash.h needing to expose it (it does not, on purpose: no
// other product code reaches into it either).
extern Preferences preferences;

// Task 2 guard test hook (esp32_flash.cpp, compiled only under
// NATIVE_BUILD/UNIT_TEST): lets this suite drive save_settings()'s refusal
// path directly instead of reproducing the historical bug inside
// init_flash()'s real control flow just to exercise it.
extern "C" void mc_test_set_flash_load_in_progress(bool v);

void setUp(void)
{
    FakeNvs::instance().reset();
    preferences.resetForTest(); // see Preferences::resetForTest()'s own comment -- generation must not carry over between cases
    meshcom_settings = s_meshcom_settings(); // reset to compiled struct defaults
    Serial.clear();
    mc_test_set_flash_load_in_progress(false);
}

void tearDown(void) { mc_test_set_flash_load_in_progress(false); }

// ---------------------------------------------------------------------------
// 0. Fidelity check on the fake itself -- required by the wave brief: if
//    these three properties are not modelled, none of the cases below can
//    tell the fixed ordering apart from the buggy one, because the buggy
//    ordering's whole effect depends on exactly this behaviour.
// ---------------------------------------------------------------------------
void test_preferences_fake_models_real_begin_end_semantics(void)
{
    Preferences p;

    // begin() returns false and does NOT reopen when already started.
    TEST_ASSERT_TRUE(p.begin("Credentials", false));
    int genAfterFirstBegin = p.generation();
    TEST_ASSERT_FALSE(p.begin("Credentials", false)); // already started -- no-op
    TEST_ASSERT_EQUAL_INT(genAfterFirstBegin, p.generation()); // did NOT bump -- did not reopen

    // end() clears _started.
    TEST_ASSERT_TRUE(p.isStarted());
    p.end();
    TEST_ASSERT_FALSE(p.isStarted());

    // end() on an already-closed handle is a harmless no-op (no double-close
    // crash, no state change) -- exercised because save_settings()'s guard
    // (Task 2) relies on init_flash()'s own trailing end() being safe to
    // call even when a nested save already closed the handle.
    p.end();
    TEST_ASSERT_FALSE(p.isStarted());

    // Every getX() returns the caller's default -- NOT a stored value --
    // when not started, even if NVS actually holds something for that key.
    FakeNvs::instance().seedInt("node_alt", 42);
    TEST_ASSERT_EQUAL_INT32(-7, p.getInt("node_alt", -7));

    // Re-opening for real DOES bump the generation and DOES see the store.
    TEST_ASSERT_TRUE(p.begin("Credentials", false));
    TEST_ASSERT_TRUE(p.generation() > genAfterFirstBegin);
    TEST_ASSERT_EQUAL_INT32(42, p.getInt("node_alt", -7));
    p.end();
}

// ---------------------------------------------------------------------------
// 1a. A field loaded by the generic walk -- after the sanitize_loaded_settings()
//     call site in source order -- keeps its STORED value, not its compiled
//     default. node_track_freq or a specific reason: esp32_flash.cpp's own
//     comment on the trailing save_settings() call site names it explicitly
//     ("everything from node_track_freq onward in the old hand-written
//     order") as one of the fields the historical bug used to reset.
//
// MUTATION RESULT (documented, not hidden): this specific case does NOT fail
// under the specified mutation. Why: in the CURRENT code the generic walk
// (the for-loop over settings_schema::fields() in init_flash()) already
// loads every field, node_track_freq included, in full BEFORE
// sanitize_loaded_settings() is called at all -- that ordering is untouched
// by the mutation, which only moves save_settings()'s call site. By the time
// the mutated save_settings() runs, meshcom_settings.node_track_freq is
// already correctly in RAM, so this specific read-side assertion holds
// either way. Kept anyway as a real, live regression guard against a
// DIFFERENT and still-possible mistake (see test_1a variant below, which
// DOES fail under a mutation squarely aimed at that one) -- see the wave
// report for the full discussion.
// ---------------------------------------------------------------------------
void test_late_loaded_field_keeps_stored_value_not_default(void)
{
    FakeNvs::instance().seedDouble("node_track", 433.775); // non-default: struct default is 0

    init_flash();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 433.775f, meshcom_settings.node_track_freq);
}

// ---------------------------------------------------------------------------
// 1b. The DIRECTLY relevant mutation for 1a's field: if a future change adds
//     "node_track" to isLoadSpecialCased()'s skip list (by mistake, e.g.
//     copy-pasting the node_msgid/max_hop_text pattern for a new field), the
//     walk stops loading it and it silently reverts to its compiled default
//     every boot -- the same OBSERVABLE symptom the historical bug produced,
//     reached a different way. This is the mutation that actually kills 1a's
//     assertion; recorded here as its own case so the property "a field
//     skipped by the walk reverts to default" has one test naming it
//     directly, not folded silently into 1a's non-failing one.
//
// MUTATION VERIFIED: temporarily added
//     strcmp(key, "node_track") == 0 ||
// to isLoadSpecialCased()'s condition in esp32_flash.cpp. Failure message
// (both this case and 1a fail identically, confirming neither is
// tautological):
//   "Expected 433.774994 Was 0"
// (node_track_freq stayed at its struct default because the walk skipped
// it). Reverted; passes again.
// ---------------------------------------------------------------------------
void test_field_skipped_by_load_walk_reverts_to_default(void)
{
    FakeNvs::instance().seedDouble("node_track", 433.775);

    init_flash();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 433.775f, meshcom_settings.node_track_freq);
}

// ---------------------------------------------------------------------------
// 1c. Requirement 1's OTHER half, made mutation-provable against the
//     brief's own headline mutation: a field sanitize_loaded_settings()
//     itself corrects (an out-of-range node_sf) must have BOTH its
//     corrected value in RAM (works either way, same reasoning as 1a) AND
//     that correction PERSISTED to NVS by the end of init_flash() (this is
//     where the mutation bites, once Task 2's guard is in place -- see the
//     guard's own test case for why the corrected value never reaches flash
//     under the reintroduced defect).
//
// MUTATION VERIFIED: with save_settings() moved back inside
// sanitize_loaded_settings() (Task 2's guard active, since it is now a
// permanent part of the file), this case FAILED with:
//   "Expected 0 Was 99"
// (the stored "node_sf" key in the fake NVS still held the original
// out-of-range 99 -- the correction was computed in RAM but the guard
// refused the only save_settings() call left, so it never reached flash).
// Reverted; passes again.
// ---------------------------------------------------------------------------
void test_sanitized_field_correction_reaches_flash(void)
{
    FakeNvs::instance().seedInt("node_sf", 99); // out of range: valid is 0 or 6..12

    init_flash();

    TEST_ASSERT_EQUAL_INT(0, meshcom_settings.node_sf); // sanitized in RAM
    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_sf"));
    TEST_ASSERT_EQUAL_INT(0, (int)FakeNvs::instance().entries().at("node_sf").i); // and on flash
}

// ---------------------------------------------------------------------------
// 2. init_flash() does not write to NVS while its OWN read handle
//    (the begin()/end() pair at its top/bottom) is still open. Modelled via
//    the fake's generation counter (see stubs/Preferences.h's top comment):
//    generation 1 is that load span; a write recorded under generation 1
//    means it happened nested inside the still-open load handle, exactly
//    the historical defect's shape, rather than through a handle
//    save_settings() opened fresh for itself.
//
// MUTATION VERIFIED, TWO WAYS (both with the fresh-per-test
// preferences.resetForTest() in setUp() -- without it the generation counter
// is cumulative across every Unity case in this one binary and "generation
// 1" stops meaning "this test's own load span" after the first case runs):
//
//   (a) Headline mutation ALONE, Task 2's guard temporarily bypassed
//       (`if (false && g_flash_load_in_progress)`): the nested save_settings()
//       call's own preferences.begin() is a real-semantics no-op (already
//       started), so it writes through generation 1 -- FAILED with:
//         "node_call"
//       (TEST_ASSERT_NOT_EQUAL_MESSAGE fired on the first generation-1 entry
//       it found in the write log, "node_call" being the first field
//       settings_schema::fields() lists). This is the direct, literal
//       reproduction of the historical defect's shape.
//
//   (b) Headline mutation WITH Task 2's guard active (the shipped state):
//       the guard refuses the nested call before it ever calls
//       preferences.begin(), so no generation-1 write can occur -- but
//       nothing else runs either, so the LEADING sanity assertion (there
//       must be at least one write this boot) is what actually catches it.
//       FAILED with:
//         "expected save_settings() to have written something this boot"
//       (FakeNvs::instance().writes() came back empty).
//
// Both reverted; passes again either way.
// ---------------------------------------------------------------------------
void test_no_write_happens_while_load_handle_is_open(void)
{
    init_flash();

    const auto &writes = FakeNvs::instance().writes();
    TEST_ASSERT_TRUE_MESSAGE(writes.size() > 0, "expected save_settings() to have written something this boot");
    for (const auto &w : writes)
    {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(1, w.generation, w.key.c_str());
    }
}

// ---------------------------------------------------------------------------
// 3. The handle opens and closes a known, exact number of times across one
//    init_flash() call on a completely empty store -- never leaked open,
//    never a stray extra open/close.
//
// COUNT CHANGED under D1-04 W3 step 4 (was 2/2 before this step): on an
// empty NVS, sanitize_loaded_settings() finds nothing to correct (every
// radio-param/max_hop_text default is already a valid sentinel -- see
// test_no_write_happens_while_load_handle_is_open's identical empty-store
// premise), so the conditional save_settings() call is skipped entirely and
// contributes 0. countersLoad() (also called unconditionally, now that
// node_msgid is no longer part of the "one write per boot" save_settings()
// call) contributes the other 3 real opens/closes on its OWN Preferences
// objects, never the global `preferences` this test inspects:
//   1. the load span itself (global `preferences`, "Credentials")
//   2. countersLoad() probing "Counters" -- empty, falls through
//   3. countersLoad()'s legacy-fallback read of "Credentials"/"node_msgid"
//   4. countersLoad() reopening "Counters" to write the advanced value back
// 1 + 3 = 4. See test_counter_namespace_migrates_from_credentials for the
// same sequence exercised for its DATA content rather than its open count.
//
// MUTATION VERIFIED (pre-existing, still holds): with save_settings() moved
// back inside sanitize_loaded_settings() (Task 2 guard active), the guard
// refuses the nested call before it makes any Preferences call at all, so
// only the load's own preferences.end() would execute on the global handle
// -- this case's own assertion on `preferences.isStarted()` and the total
// count both catch that regression the same way they did before this step.
// ---------------------------------------------------------------------------
void test_handle_opens_and_closes_exactly_twice(void)
{
    init_flash();

    TEST_ASSERT_FALSE(preferences.isStarted()); // not leaked open
    TEST_ASSERT_EQUAL_INT(4, FakeNvs::instance().openCount());
    TEST_ASSERT_EQUAL_INT(4, FakeNvs::instance().closeCount());
}

// ---------------------------------------------------------------------------
// 4. The msgid write-back guarantee (commit bce95db5) still holds: after
//    init_flash(), the value STORED in NVS for "node_msgid" is the advanced
//    (msgIdAfterLoad) value, not the raw value that was on flash before this
//    boot. This is the guarantee bce95db5's own commit message describes as
//    "the load-bearing half" of the whole scheme -- if it silently stops
//    reaching flash, a crash in the following boot's first
//    kMsgIdPersistStep frames replays ids the mesh has already seen.
//
// TARGET NAMESPACE CHANGED under D1-04 W3 step 4: the counter's storage
// moved from "Credentials" (the settings namespace, save_settings()'s own)
// to its own "Counters" namespace (counters_store.h) -- see
// esp32_flash.cpp's countersLoad(). Seeded here in the LEGACY location, the
// same as a node upgrading from a pre-cutover firmware image: "Counters"
// starts empty, "Credentials" still holds the value from before this change.
// The assertions below moved from the default ("Credentials") namespace to
// "Counters" to match; test_counter_namespace_migrates_from_credentials
// covers the migration's STRUCTURAL properties (legacy key preserved,
// Counters wins once populated) separately -- this case stays focused on the
// numeric write-back guarantee itself, unchanged in substance since
// bce95db5.
//
// MUTATION VERIFIED (pre-existing, still holds): with save_settings() moved
// back inside sanitize_loaded_settings() (Task 2 guard active) AND
// countersLoad()'s own write-back temporarily removed, this case fails with
// "Expected 124 Was 0" (RAM assertion: countersLoad() never ran at all, so
// meshcom_settings.node_msgid never advanced past its struct default).
// Reverted; passes again.
// ---------------------------------------------------------------------------
void test_msgid_writeback_survives_the_boot(void)
{
    FakeNvs::instance().seedInt("node_msgid", 24); // legacy location ("Credentials", the default ns)
    int expected = msgIdAfterLoad(24);
    TEST_ASSERT_EQUAL_INT(124, expected); // sanity on the fixture itself (kMsgIdPersistStep == 100)

    init_flash();

    TEST_ASSERT_EQUAL_INT(expected, meshcom_settings.node_msgid); // RAM
    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_msgid", "Counters"));
    TEST_ASSERT_EQUAL_INT(expected, (int)FakeNvs::instance().entries("Counters").at("node_msgid").i); // new location on flash
}

// ---------------------------------------------------------------------------
// 4a. D1-04 W3 step 4, upgrade path: a node that has never booted this
//     firmware has "node_msgid" only in the LEGACY "Credentials" location
//     and nothing yet in "Counters". After init_flash(), "Counters" must
//     hold the ADVANCED (msgIdAfterLoad) value, and the legacy key must be
//     left exactly as it was -- migrated, never deleted, so a downgrade back
//     to a pre-cutover firmware still finds its counter.
//
// MUTATION VERIFIED: temporarily made countersLoad()'s fallback branch skip
// the legacy read entirely (`if (false)` guarding the `else` body instead of
// the real `!counters_preferences.isKey(...)` check, i.e. countersLoad()
// always takes the "Counters already has it" branch). This case then FAILED
// with:
//   "Expected 100 Was 0"
// (meshcom_settings.node_msgid stayed at its struct default 0 -- with
// "Counters" empty and the fallback disabled, getInt()'s own caller-supplied
// default of the CURRENT struct value, still 0, was all countersLoad() ever
// saw before advancing it -- msgIdAfterLoad(0) is 100, and 100 is what a
// correctly-migrated node would have stored instead). Reverted; passes
// again.
// ---------------------------------------------------------------------------
void test_counter_namespace_migrates_from_credentials(void)
{
    FakeNvs::instance().seedInt("node_msgid", 55, "Credentials"); // legacy location, explicit
    int expected = msgIdAfterLoad(55);

    init_flash();

    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_msgid", "Counters"));
    TEST_ASSERT_EQUAL_INT(expected, (int)FakeNvs::instance().entries("Counters").at("node_msgid").i);
    // Legacy key untouched -- migrated, not deleted (a downgrade must still find it).
    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_msgid", "Credentials"));
    TEST_ASSERT_EQUAL_INT(55, (int)FakeNvs::instance().entries("Credentials").at("node_msgid").i);
}

// ---------------------------------------------------------------------------
// 4b. D1-04 W3 step 4: once "Counters" itself holds a value, it wins outright
//     over whatever the legacy "Credentials" key still says -- the legacy
//     key is only ever consulted on the FIRST boot after the cutover, never
//     again once "Counters" has been populated (the common case for every
//     boot from the second one on).
// ---------------------------------------------------------------------------
void test_counter_namespace_wins_over_legacy_key(void)
{
    FakeNvs::instance().seedInt("node_msgid", 900, "Credentials"); // stale legacy value
    FakeNvs::instance().seedInt("node_msgid", 300, "Counters");    // authoritative, current value
    int expected = msgIdAfterLoad(300);

    init_flash();

    TEST_ASSERT_EQUAL_INT(expected, meshcom_settings.node_msgid);
    TEST_ASSERT_EQUAL_INT(expected, (int)FakeNvs::instance().entries("Counters").at("node_msgid").i);
    // Stale legacy value never touched, and never used to compute the result.
    TEST_ASSERT_EQUAL_INT(900, (int)FakeNvs::instance().entries("Credentials").at("node_msgid").i);
}

// ---------------------------------------------------------------------------
// 4c. countersSave() (the loop_functions.cpp high-water-mark write path, not
//     countersLoad()'s own boot-time write-back) touches ONLY the "Counters"
//     namespace -- it must never reach into "Credentials", the settings
//     store's own namespace, the way the pre-cutover save_settings() call it
//     replaced implicitly did (that call rewrote the ENTIRE settings record
//     on every high-water mark, "Credentials" included).
// ---------------------------------------------------------------------------
void test_counters_save_touches_only_the_counters_namespace(void)
{
    init_flash(); // establishes a baseline Counters/node_msgid entry, like a real boot would
    FakeNvs::instance().seedString("node_call", "DK5EN-1", "Credentials");
    size_t credentialsSizeBefore = FakeNvs::instance().entries("Credentials").size();
    size_t writesBefore = FakeNvs::instance().writes().size(); // isolate THIS call's writes from init_flash()'s own

    meshcom_settings.node_msgid = 42;
    bool ok = countersSave();

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(42, (int)FakeNvs::instance().entries("Counters").at("node_msgid").i);
    TEST_ASSERT_EQUAL_size_t(credentialsSizeBefore, FakeNvs::instance().entries("Credentials").size());
    TEST_ASSERT_EQUAL_STRING("DK5EN-1", FakeNvs::instance().entries("Credentials").at("node_call").s.c_str());

    const auto &writes = FakeNvs::instance().writes();
    TEST_ASSERT_TRUE_MESSAGE(writes.size() > writesBefore, "expected countersSave() to have written something");
    for (size_t i = writesBefore; i < writes.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("Counters", writes[i].ns.c_str(), "countersSave() wrote outside its own namespace");
    }
}

// ---------------------------------------------------------------------------
// 4d. init_flash() no longer writes "node_msgid" into "Credentials" at all
//     (it moved to "Counters" entirely, D1-04 W3 step 4) -- on an empty
//     store, after init_flash(), "Credentials" must have no "node_msgid" key
//     whether or not sanitize_loaded_settings() found something else to
//     correct (this case uses the empty-store, nothing-to-correct path, same
//     premise as test_no_write_happens_while_load_handle_is_open).
// ---------------------------------------------------------------------------
void test_init_flash_writes_no_msgid_into_credentials(void)
{
    init_flash();

    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_msgid", "Credentials"));
    for (const auto &w : FakeNvs::instance().writes())
    {
        if (w.key == "node_msgid")
            TEST_ASSERT_EQUAL_STRING_MESSAGE("Counters", w.ns.c_str(), "node_msgid written into the wrong namespace");
    }
}

// ---------------------------------------------------------------------------
// 5. Task 2's guard itself: save_settings() called while
//    g_flash_load_in_progress is set refuses outright (no Preferences call
//    at all -- NVS is left byte-for-byte as it was) and says so loudly on
//    Serial; called again with the flag clear, it behaves completely
//    normally. Driven through the test-only hook so this case does not need
//    to reproduce the historical bug inside init_flash()'s real control flow
//    just to reach the guard.
//
// MUTATION VERIFIED: temporarily made the guard's condition
// `if (false && g_flash_load_in_progress)` (i.e. disabled it) -- this case
// then FAILED with:
//   "guard let a write through while load was in progress"
// (the refused call went ahead and wrote anyway). Reverted; passes again.
// ---------------------------------------------------------------------------
void test_guard_refuses_save_during_load_and_logs(void)
{
    strncpy(meshcom_settings.node_call, "DK5EN-9", sizeof(meshcom_settings.node_call) - 1);

    mc_test_set_flash_load_in_progress(true);
    save_settings();

    TEST_ASSERT_FALSE_MESSAGE(FakeNvs::instance().hasKey("node_call"), "guard let a write through while load was in progress");
    TEST_ASSERT_EQUAL_INT(0, FakeNvs::instance().openCount()); // never even called preferences.begin()
    TEST_ASSERT_TRUE_MESSAGE(Serial.captured().find("REFUSED") != std::string::npos,
                              "guard did not log a refusal to Serial");

    // Flag clear -- save_settings() must work completely normally.
    mc_test_set_flash_load_in_progress(false);
    Serial.clear();
    save_settings();

    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_call"));
    TEST_ASSERT_EQUAL_STRING("DK5EN-9", FakeNvs::instance().entries().at("node_call").s.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_preferences_fake_models_real_begin_end_semantics);
    RUN_TEST(test_late_loaded_field_keeps_stored_value_not_default);
    RUN_TEST(test_field_skipped_by_load_walk_reverts_to_default);
    RUN_TEST(test_sanitized_field_correction_reaches_flash);
    RUN_TEST(test_no_write_happens_while_load_handle_is_open);
    RUN_TEST(test_handle_opens_and_closes_exactly_twice);
    RUN_TEST(test_msgid_writeback_survives_the_boot);
    RUN_TEST(test_counter_namespace_migrates_from_credentials);
    RUN_TEST(test_counter_namespace_wins_over_legacy_key);
    RUN_TEST(test_counters_save_touches_only_the_counters_namespace);
    RUN_TEST(test_init_flash_writes_no_msgid_into_credentials);
    RUN_TEST(test_guard_refuses_save_during_load_and_logs);
    return UNITY_END();
}
