// Native test suite for the nRF52 settings cutover (W3 Wave D):
// src/nrf52/nrf52_flash.cpp's init_flash()/save_settings()/flash_reset() and
// src/nrf52/settings_store_nrf52.cpp's settingsStoreSave()/settingsStoreLoad()
// -- compiled and RUN for the first time here; before this suite the cutover
// only ever compiled and linked (see the wave brief / docs/BACKLOG.md D1-04).
//
// Everything under test is the REAL product source (src/nrf52/nrf52_flash.cpp,
// src/nrf52/settings_store_nrf52.cpp, src/settings_store.cpp,
// src/settings_schema.cpp, src/settings_sanitize.cpp) compiled natively
// against the stub set in this directory. See stubs/nrf52/WisBlock-API.h for
// why a forced pre-include is needed to shadow the real WisBlock-API.h (a
// same-directory QUOTED include from both nrf52_flash.cpp and
// settings_store_nrf52.cpp defeats plain -I shadowing), and
// stubs/Adafruit_LittleFS.h for the in-memory fake filesystem every scenario
// below drives through.
//
//   Verification (no pio env exists yet, see the wave report for the exact
//   command): compiled and run directly with cc/c++.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <counters_store.h>	// countersLoad()/countersSave()
#include <crc32_util.h>		// crc32_buf() -- must match nrf52_flash.cpp's own legacy-blob CRC
#include <msgid_counter.h>
#include <nrf52/WisBlock-API.h> // s_meshcom_settings, meshcom_settings, init_flash_done, MESHCOM_DATA_MARKER
#include <nrf52/ble_settings_v1.h> // s_ble_settings_v1, bleSettingsToV1() -- legacy blobs are v1 images now (D1-04 W3 step 4)
#include <nrf52/settings_store_nrf52.h> // SettingsLoadResult, settingsStoreLoad()/Save()
#include <settings_schema.h>
#include <settings_store.h>

#include "Adafruit_LittleFS.h" // g_fake_fs, FILE_O_READ/WRITE

// From stubs/malloc_hook.cpp -- one-shot, size-targeted malloc() failure
// injection for the two allocation-failure paths (settingsStoreSave() /
// settingsStoreLoad(), both `malloc(kSettingsBufferCap)`, 4096 bytes,
// documented in settings_store_nrf52.cpp).
extern "C" void mc_arm_malloc_failure(size_t min_size);
extern "C" bool mc_malloc_failure_fired();
extern "C" void mc_disarm_malloc_failure(void);

// ---------------------------------------------------------------------------
// Path literals -- must match the product code's own literals EXACTLY (they
// are internal constants, not exported accessors): "MeshCom-RAK" is
// nrf52_flash.cpp's `settings_name`; the other two are settings_store_nrf52.cpp's
// anonymous-namespace kSettingsPath/kSettingsTmpPath.
// ---------------------------------------------------------------------------
static const char *const kLegacyPath = "MeshCom-RAK";
static const char *const kKeyedPath = "/MeshCom-Settings-Store";
static const char *const kKeyedTmpPath = "/MeshCom-Settings-Store.tmp";
static const char *const kCountersPath = "/counters.txt";
static const char *const kLegacyCrcPath = "/legacy_blob.crc";

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

// A legacy-format blob: a v1 image (s_ble_settings_v1, src/nrf52/ble_settings_v1.h), exactly what
// nrf52_flash.cpp's init_flash() now reads through (D1-04 W3 step 4, Task 1) and what official
// firmware v4.35t still writes as its raw settings blob -- NOT a raw blit of s_meshcom_settings any
// more (that struct's layout is free to change since the D1-04 merge). Built from an actual native
// s_meshcom_settings instance converted through the REAL bleSettingsToV1() (never hand-assembled byte
// offsets), so this suite's notion of "the blob's bytes" always matches whatever this host's ABI and
// the real conversion function actually produce, the same bytes the product code reads back.
static std::vector<uint8_t> make_legacy_blob_from(const s_meshcom_settings &state)
{
    s_ble_settings_v1 img{};
    bleSettingsToV1(state, img);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&img);
    return std::vector<uint8_t>(p, p + sizeof(img));
}

static std::vector<uint8_t> make_legacy_blob(const char *node_call)
{
    s_meshcom_settings s; // struct defaults
    strncpy(s.node_call, node_call, sizeof(s.node_call) - 1);
    s.node_call[sizeof(s.node_call) - 1] = '\0';
    return make_legacy_blob_from(s);
}

// Matches nrf52_flash.cpp's own recordLegacyBlobCrc() encoding exactly ("%08lX\n" hex, uppercase) --
// Task 7 (addendum): seeds the CRC record a real boot would have written the last time it left the
// legacy blob in a known state, so a test that wants the KEYED store to win can simulate "this blob
// is unchanged since the last migration" instead of always looking freshly-downgraded.
static void seed_legacy_blob_crc(const std::vector<uint8_t> &legacy_bytes)
{
    uint32_t crc = crc32_buf(legacy_bytes.data(), legacy_bytes.size());
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%08lX\n", (unsigned long)crc);
    TEST_ASSERT_GREATER_THAN(0, n);
    g_fake_fs.seed(kLegacyCrcPath, buf, (size_t)n);
}

// A valid keyed-store record, built through the REAL schema/codec
// (settings_schema::fields() + settings_store::encode()) -- exactly what
// settingsStoreSave() itself would produce for this state.
static std::vector<uint8_t> make_keyed_store(const s_meshcom_settings &state)
{
    static char buf[8192];
    long n = settings_store::encode(settings_schema::fields(), settings_schema::fieldCount(), &state, buf,
                                     sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    return std::vector<uint8_t>(buf, buf + n);
}

static std::vector<uint8_t> make_keyed_store(const char *node_call)
{
    s_meshcom_settings s;
    strncpy(s.node_call, node_call, sizeof(s.node_call) - 1);
    s.node_call[sizeof(s.node_call) - 1] = '\0';
    return make_keyed_store(s);
}

void setUp(void)
{
    g_fake_fs.reset();
    meshcom_settings = s_meshcom_settings();
    init_flash_done = false;
    mc_disarm_malloc_failure();
}

void tearDown(void) { mc_disarm_malloc_failure(); }

// ---------------------------------------------------------------------------
// (a) keyed fast path: settings load from the store, legacy is never touched
// ---------------------------------------------------------------------------

static void test_keyed_fast_path_wins_and_leaves_legacy_untouched(void)
{
    auto legacy = make_legacy_blob("LEGACY-A1");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // Task 7 (addendum): the keyed fast path is refused unless the legacy blob's CRC matches a
    // recorded one -- simulate "this blob was already migrated at some prior boot and has not
    // changed since" so this test still exercises the fast path rather than the legacy one.
    seed_legacy_blob_crc(legacy);

    auto keyed = make_keyed_store("KEYED-A2");
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    init_flash();

    // The store won, not the legacy blob -- the two deliberately disagree on
    // node_call so a wrong winner is loud, not a coincidence.
    TEST_ASSERT_EQUAL_STRING("KEYED-A2", meshcom_settings.node_call);

    const auto *after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)legacy.size(), (uint32_t)after->size());
    TEST_ASSERT_EQUAL_MEMORY(legacy.data(), after->data(), legacy.size());
}

// ---------------------------------------------------------------------------
// (b) sanity-gate rejection -- one case per reason, each must fall through to
// the legacy path (proven by the legacy content winning), never straight to
// flash_reset() defaults.
// ---------------------------------------------------------------------------

static void test_sanity_gate_rejects_empty_store_file(void)
{
    auto legacy = make_legacy_blob("LEGACY-B1");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    g_fake_fs.seed(kKeyedPath, "", 0); // file exists, zero bytes

    init_flash();

    TEST_ASSERT_EQUAL_STRING("LEGACY-B1", meshcom_settings.node_call);
}

static void test_sanity_gate_rejects_all_unknown_keys(void)
{
    auto legacy = make_legacy_blob("LEGACY-B2");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());

    static const char content[] = "bogus_key=123\nanother_bogus=xyz\n";
    g_fake_fs.seed(kKeyedPath, content, strlen(content));

    init_flash();

    TEST_ASSERT_EQUAL_STRING("LEGACY-B2", meshcom_settings.node_call);
}

static void test_sanity_gate_rejects_blank_node_call(void)
{
    auto legacy = make_legacy_blob("LEGACY-B3");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());

    s_meshcom_settings s; // defaults
    s.node_call[0] = '\0';
    strncpy(s.node_short, "ABCDE", sizeof(s.node_short) - 1);
    auto keyed = make_keyed_store(s);
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    init_flash();

    TEST_ASSERT_EQUAL_STRING("LEGACY-B3", meshcom_settings.node_call);
}

// ---------------------------------------------------------------------------
// (c) legacy -> keyed migration, exactly once, legacy left alone
// ---------------------------------------------------------------------------

static void test_legacy_migrates_into_keyed_store(void)
{
    auto legacy = make_legacy_blob("MIGRATE-1");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // No keyed store file at all.

    init_flash();

    TEST_ASSERT_EQUAL_STRING("MIGRATE-1", meshcom_settings.node_call);
    TEST_ASSERT_TRUE(g_fake_fs.exists(kKeyedPath));

    const auto *bytes = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(bytes);

    s_meshcom_settings decoded{};
    strncpy(decoded.node_call, "SHOULD-BE-OVERWRITTEN", sizeof(decoded.node_call) - 1);
    settings_store::DecodeStats st = settings_store::decode(
        settings_schema::fields(), settings_schema::fieldCount(), &decoded,
        reinterpret_cast<const char *>(bytes->data()), bytes->size());
    TEST_ASSERT_GREATER_THAN(0, (int)st.fields_set);
    TEST_ASSERT_EQUAL_STRING("MIGRATE-1", decoded.node_call);
    // A field the fixture never touched should still have round-tripped its
    // struct default -- "decodes back to the same values", not just node_call.
    TEST_ASSERT_EQUAL_INT32(-20, decoded.node_power);

    const auto *legacy_after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(legacy_after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)legacy.size(), (uint32_t)legacy_after->size());
    TEST_ASSERT_EQUAL_MEMORY(legacy.data(), legacy_after->data(), legacy.size());
}

// ---------------------------------------------------------------------------
// (d) retired pre-compat marker (0xAA 0x57): reset to defaults, never
// reinterpreted as the current layout.
// ---------------------------------------------------------------------------

static void test_retired_marker_resets_to_defaults(void)
{
    s_meshcom_settings would_be_current_layout;
    strncpy(would_be_current_layout.node_call, "WOULDBE-1", sizeof(would_be_current_layout.node_call) - 1);
    would_be_current_layout.valid_mark_2 = 0x57; // the retired marker byte

    const uint8_t *p = reinterpret_cast<const uint8_t *>(&would_be_current_layout);
    g_fake_fs.seed(kLegacyPath, p, sizeof(would_be_current_layout));
    // No keyed store -> falls into the legacy path, which hits the 0x57 check.

    init_flash();

    // Not the value those bytes would have produced under the current layout.
    TEST_ASSERT_TRUE(strcmp(meshcom_settings.node_call, "WOULDBE-1") != 0);

    // Exactly the compiled-in defaults instead.
    s_meshcom_settings defaults;
    TEST_ASSERT_EQUAL_STRING(defaults.node_call, meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_UINT8(MESHCOM_DATA_MARKER, meshcom_settings.valid_mark_2);
}

// ---------------------------------------------------------------------------
// Save side: skip-if-unchanged
// ---------------------------------------------------------------------------

static void test_save_skip_if_unchanged_performs_no_second_write(void)
{
    strncpy(meshcom_settings.node_call, "SKIP-0001", sizeof(meshcom_settings.node_call) - 1);

    TEST_ASSERT_TRUE(settingsStoreSave());
    int writes_after_first = g_fake_fs.open_write_calls;
    TEST_ASSERT_GREATER_THAN(0, writes_after_first);
    int renames_after_first = g_fake_fs.rename_calls;

    TEST_ASSERT_TRUE(settingsStoreSave()); // identical settings, second call
    TEST_ASSERT_EQUAL_INT(writes_after_first, g_fake_fs.open_write_calls);
    TEST_ASSERT_EQUAL_INT(renames_after_first, g_fake_fs.rename_calls);
}

// ---------------------------------------------------------------------------
// Save side: atomicity -- the live path holds the OLD content right up to
// the rename that replaces it.
// ---------------------------------------------------------------------------

static void test_save_atomicity_live_path_held_old_content_until_rename(void)
{
    auto old_bytes = make_keyed_store("OLD-CALL1");
    g_fake_fs.seed(kKeyedPath, old_bytes.data(), old_bytes.size());

    strncpy(meshcom_settings.node_call, "NEW-CALL2", sizeof(meshcom_settings.node_call) - 1);
    TEST_ASSERT_TRUE(settingsStoreSave());

    TEST_ASSERT_TRUE(g_fake_fs.last_rename_had_dest);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)old_bytes.size(), (uint32_t)g_fake_fs.last_rename_dest_previous_content.size());
    TEST_ASSERT_EQUAL_MEMORY(old_bytes.data(), g_fake_fs.last_rename_dest_previous_content.data(), old_bytes.size());

    // The temp path is consumed by the rename -- gone afterwards.
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedTmpPath));
}

// ---------------------------------------------------------------------------
// A rename that fails is the one save failure mode observed on hardware and
// not yet explained: DK5EN-90 printed [SETST];save;rename_failed twice on the
// boot right after a reflash, on 2026-09-12, and has not repeated it since.
// Two things have to hold when it happens, and neither was asserted before:
// the previously persisted file must survive untouched (the whole point of
// writing to a temp path first), and the failure must be VISIBLE -- 234
// save_settings() call sites ignore the bool return, so the console line is
// the only signal a bench session gets. The filesystem inventory is part of
// that line's job: it is what distinguishes "out of space" from a transient
// flash error, which is exactly the open question.
// ---------------------------------------------------------------------------

static void test_rename_failure_keeps_old_file_and_reports_the_filesystem(void)
{
    auto old_bytes = make_keyed_store("OLD-CALL1");
    g_fake_fs.seed(kKeyedPath, old_bytes.data(), old_bytes.size());
    const char *const kBondFile = "/adafruit/bond_prph/1";
    g_fake_fs.seed(kBondFile, "bond", 4);

    strncpy(meshcom_settings.node_call, "NEW-CALL2", sizeof(meshcom_settings.node_call) - 1);
    Serial.clear();
    g_fake_fs.force_rename_fail = 2; // the first attempt AND the retry fail

    TEST_ASSERT_FALSE(settingsStoreSave());

    // The live file still holds what it held before the attempt.
    const std::vector<uint8_t> *live = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(live);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)old_bytes.size(), (uint32_t)live->size());
    TEST_ASSERT_EQUAL_MEMORY(old_bytes.data(), live->data(), old_bytes.size());

    // No half-written temp file is left behind.
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedTmpPath));

    const std::string &log = Serial.captured();
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];save;rename_failed"));
    // The inventory reached the root file AND the one three levels down
    // under /adafruit/bond_prph/, which is the depth the real filesystem
    // uses for BLE bonds -- a two-level walk would have missed it.
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), ";file;/MeshCom-Settings-Store;"));
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), ";file;/adafruit/bond_prph/1;4"));
    // Three files, because the inventory is taken BEFORE the temp file is
    // cleaned up: on the space hypothesis the temp file is precisely what
    // pushed the filesystem over, so a report that had already removed it
    // would describe a state that never existed.
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), ";file;/MeshCom-Settings-Store.tmp;"));
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];fs;rename_failed;total;files;3;dirs;2;"));
    // 224 is the real geometry: 7 x 4096 B in 128 B blocks (InternalFileSystem.cpp).
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), ";of;224;"));
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];save;rename_failed_twice"));
}

// The other half of the same branch: a rename that fails once and works on
// the retry must SAVE, not report a failure. That is the outcome the retry
// exists to produce -- and the console line is what tells a bench session
// afterwards that the failure was transient rather than persistent, which is
// the open question on this row.

static void test_rename_failure_recovers_on_the_retry(void)
{
    auto old_bytes = make_keyed_store("OLD-CALL1");
    g_fake_fs.seed(kKeyedPath, old_bytes.data(), old_bytes.size());

    strncpy(meshcom_settings.node_call, "NEW-CALL2", sizeof(meshcom_settings.node_call) - 1);
    Serial.clear();
    g_fake_fs.force_rename_fail = 1; // only the first attempt fails

    TEST_ASSERT_TRUE(settingsStoreSave());

    const std::vector<uint8_t> *live = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(live);
    // The live path now holds the NEW content -- the retry really wrote, it
    // did not merely stop complaining.
    const std::string live_text(live->begin(), live->end());
    TEST_ASSERT_NOT_NULL(strstr(live_text.c_str(), "node_call=NEW-CALL2"));
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedTmpPath));

    const std::string &log = Serial.captured();
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];save;rename_retry_ok"));
    TEST_ASSERT_NULL(strstr(log.c_str(), "rename_failed_twice"));
}

// ---------------------------------------------------------------------------
// node_power's -20 sentinel (CFG_ESC(CFG_POWER_NOT_SET), outside
// TX_POWER_MIN..MAX on RAK4631) must survive a real save/load round trip
// through the real schema, not a synthetic one.
// ---------------------------------------------------------------------------

static void test_node_power_sentinel_survives_round_trip(void)
{
    meshcom_settings.node_power = -20;
    strncpy(meshcom_settings.node_call, "PWR-TEST1", sizeof(meshcom_settings.node_call) - 1);

    TEST_ASSERT_TRUE(settingsStoreSave());

    meshcom_settings = s_meshcom_settings();
    meshcom_settings.node_power = 7; // poison: a real, in-range value a wrongful clamp could not be confused with

    SettingsLoadResult r = settingsStoreLoad();
    TEST_ASSERT_TRUE(r.file_found);
    TEST_ASSERT_TRUE(r.read_ok);
    TEST_ASSERT_EQUAL_INT32(-20, meshcom_settings.node_power);
}

// ---------------------------------------------------------------------------
// Allocation failure (malloc(kSettingsBufferCap) == malloc(4096) returning
// null) -- the one save/load failure mode with no seam in the fake
// filesystem, so it is injected at the allocator itself (stubs/malloc_hook.cpp).
// ---------------------------------------------------------------------------

static void test_save_allocation_failure_leaves_everything_untouched(void)
{
    strncpy(meshcom_settings.node_call, "ALLOC-001", sizeof(meshcom_settings.node_call) - 1);

    // Lower bound, deliberately NOT kSettingsBufferCap: that constant is
    // private to settings_store_nrf52.cpp and has already been raised once
    // (4096 -> 8192). Arming on the exact value made this test silently stop
    // injecting when the cap moved, and it passed anyway. 1024 is far below
    // any plausible cap and far above every other allocation on this path.
    mc_arm_malloc_failure(1024);
    bool ok = settingsStoreSave();

    // Assert the fault ACTUALLY fired before asserting how the code reacted --
    // without this, "the failure never happened" is indistinguishable from
    // "the failure was handled", which is exactly how this test went green
    // while testing nothing.
    TEST_ASSERT_TRUE_MESSAGE(mc_malloc_failure_fired(),
                             "injected malloc failure never fired -- the test is not testing anything");
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedPath));
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedTmpPath));
    TEST_ASSERT_EQUAL_INT(0, g_fake_fs.rename_calls);
}

static void test_load_allocation_failure_reports_read_not_ok(void)
{
    auto keyed = make_keyed_store("SHOULDNT1");
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    mc_arm_malloc_failure(1024);   // see the note in the save case above
    SettingsLoadResult r = settingsStoreLoad();

    TEST_ASSERT_TRUE_MESSAGE(mc_malloc_failure_fired(),
                             "injected malloc failure never fired -- the test is not testing anything");
    TEST_ASSERT_TRUE(r.file_found);
    TEST_ASSERT_FALSE(r.read_ok);
}

// ---------------------------------------------------------------------------
// flash_reset() (Fable verdict, docs/w3-settings-verdict.md, Finding 2):
// targeted removal of its own two files, NOT InternalFS.format(). format()
// would erase every file on the filesystem, including BLE bond pairings the
// Adafruit nRF52 core keeps on the same filesystem under BOND_DIR_PRPH
// (bonding.cpp, "/adafruit/bond_prph/") -- flash_reset() has no business
// touching those. Also: real FILE_O_WRITE seeks to END rather than
// truncating (see stubs/Adafruit_LittleFS.h), so the remove() ahead of the
// defaults rewrite is load-bearing -- drop it and the legacy blob comes back
// double length instead of an obvious failure.
// ---------------------------------------------------------------------------

static void test_flash_reset_preserves_bonds_and_writes_exact_length(void)
{
    const char *const kBondFile = "/adafruit/bond_prph/1";
    static const uint8_t bond_bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    g_fake_fs.seed(kBondFile, bond_bytes, sizeof(bond_bytes));

    auto legacy = make_legacy_blob("RESET-001");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());

    auto keyed = make_keyed_store("RESET-001");
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    flash_reset();

    // Unrelated file, entirely outside flash_reset()'s own two -- untouched.
    TEST_ASSERT_TRUE(g_fake_fs.exists(kBondFile));
    TEST_ASSERT_EQUAL_INT(0, g_fake_fs.format_calls);

    // The keyed store IS one of flash_reset()'s own two files -- gone.
    TEST_ASSERT_FALSE(g_fake_fs.exists(kKeyedPath));

    // Exactly sizeof(s_ble_settings_v1) (the v1 image flash_reset() now writes, Task 2), not double:
    // a missing remove() before the defaults rewrite would append onto the OLD legacy content instead
    // of replacing it. NOT sizeof(s_meshcom_settings) -- on this host's ABI the two coincidentally
    // differ (2000 vs. 2008 bytes; see test_wrong_sized_legacy_blob_resets_to_defaults below for why
    // that coincidence is exactly the regression this cutover has to guard against), so asserting
    // against the live struct's size here would not even catch a real bug.
    const auto *after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(s_ble_settings_v1), (uint32_t)after->size());
}

// ---------------------------------------------------------------------------
// max_hop_text's 0 sentinel ("nothing stored yet", maxhop.h) must survive a
// real save/load round trip, mirroring test_node_power_sentinel_survives_
// round_trip above. CFG_ESC(0) on its config_json.h row (settings_schema.cpp)
// is what makes this pass: without it, decode()'s has_range clamp would
// raise a stored 0 up to MAXHOP_TEXT_MIN (1) before sanitize_max_hop_text()
// ever saw it, and 1 is a legal value there, so it would stick -- pinning
// the node to one text hop instead of falling back to MAXHOP_TEXT_FALLBACK
// (4). The poison value below (3) is a real in-range value distinct from
// both the sentinel (0) and the wrongful clamp target (1), so a pass here
// cannot be a coincidence of "value happened to already be right".
// ---------------------------------------------------------------------------

static void test_max_hop_text_sentinel_survives_round_trip(void)
{
    meshcom_settings.max_hop_text = 0; // "nothing stored yet"
    strncpy(meshcom_settings.node_call, "HOP-TEST1", sizeof(meshcom_settings.node_call) - 1);

    TEST_ASSERT_TRUE(settingsStoreSave());

    meshcom_settings = s_meshcom_settings();
    meshcom_settings.max_hop_text = 3; // poison, see comment above

    SettingsLoadResult r = settingsStoreLoad();
    TEST_ASSERT_TRUE(r.file_found);
    TEST_ASSERT_TRUE(r.read_ok);
    TEST_ASSERT_EQUAL_INT32(0, meshcom_settings.max_hop_text);
}

// ---------------------------------------------------------------------------
// node_msgid persistence (D1-04 W3 step 4, operator decision 2026-09-13):
// node_msgid was dropped from settings_schema.h entirely and given its own
// file instead (src/counters_store.h), so a settings restore/rewrite/BLE
// write can never rewind it. Replaces the two settings-store-based msgid/
// ackid tests this suite had before that decision (node_ackid itself was
// dropped from s_meshcom_settings in the same wave -- loaded, saved, never
// read -- so there is nothing left to test a round trip of).
// ---------------------------------------------------------------------------

static void test_node_msgid_persists_via_counters_file(void)
{
    meshcom_settings.node_msgid = 321;

    TEST_ASSERT_TRUE(countersSave());

    const std::vector<uint8_t> *bytes = g_fake_fs.peek(kCountersPath);
    TEST_ASSERT_NOT_NULL(bytes);

    // Decoded through a local one-row descriptor rather than countersLoad(): that function's own
    // contract is to ALWAYS advance the value it reads (msgIdAfterLoad()), so it cannot itself prove
    // "what was written is what comes back" -- only what the file's bytes actually say, which is what
    // this checks. Same shape as nrf52_flash.cpp's own (unexported) descriptor for this file.
    const settings_store::FieldDescriptor counters_fields[] = {
        {"node_msgid", settings_store::FieldType::I32, offsetof(s_meshcom_settings, node_msgid), 0, false, 0.0,
         0.0},
    };
    s_meshcom_settings decoded{};
    decoded.node_msgid = -1; // poison: distinct from both the struct default (0) and the value under test (321)
    settings_store::DecodeStats st =
        settings_store::decode(counters_fields, 1, &decoded, (const char *)bytes->data(), bytes->size());
    TEST_ASSERT_EQUAL_UINT32(1, (unsigned)st.fields_set);
    TEST_ASSERT_EQUAL_INT32(321, decoded.node_msgid);
}

// ---------------------------------------------------------------------------
// The message-id high-water mark on the REAL load path (msgid_counter.h),
// now through countersLoad()'s own file rather than the settings store. The
// unit test for the policy itself lives in test_msgid_counter; what is
// asserted here is the half that policy cannot do for itself -- that
// init_flash() actually advances the counter past the block the previous run
// may have used, that the advanced value REACHES FLASH before the first
// frame can go out, and that it reaches the COUNTERS file specifically, not
// the settings store (which no longer has a row for node_msgid at all).
// ---------------------------------------------------------------------------

static void test_load_advances_the_msgid_block_via_counters_file(void)
{
    s_meshcom_settings stored;
    strncpy(stored.node_call, "MSGID-HWM", sizeof(stored.node_call) - 1);
    stored.node_msgid = 200;
    // Everything else deliberately VALID, so sanitize_loaded_settings() finds nothing to correct --
    // keeps this test's signal isolated to the counters file, not a settings-store side effect.
    stored.node_power = 22;
    stored.node_freq = 433175000.0f;
    stored.node_bw = 1;
    stored.node_sf = 11;
    stored.node_cr = 2;
    stored.node_country = 1;
    stored.max_hop_text = 4;
    auto legacy = make_legacy_blob_from(stored);
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // No keyed store, no counters file -- the legacy path is the only one reachable regardless of the
    // Task 7 CRC gate, so this test does not need to seed a CRC record.

    init_flash();

    TEST_ASSERT_EQUAL_INT32(200 + kMsgIdPersistStep, meshcom_settings.node_msgid);

    // The counters file says so too, so a crash before the next persist point cannot hand out that
    // block a second time.
    const std::vector<uint8_t> *counters_bytes = g_fake_fs.peek(kCountersPath);
    TEST_ASSERT_NOT_NULL(counters_bytes);
    std::string counters_text(counters_bytes->begin(), counters_bytes->end());
    char expect_line[32];
    snprintf(expect_line, sizeof(expect_line), "node_msgid=%d", 200 + kMsgIdPersistStep);
    TEST_ASSERT_NOT_NULL(strstr(counters_text.c_str(), expect_line));

    // ... and the SETTINGS file was not rewritten for it: node_msgid has no schema row any more
    // (settings_schema.h), so a settings-store record that somehow still carried it would mean the
    // counters write-back leaked into the wrong file.
    const std::vector<uint8_t> *keyed_bytes = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(keyed_bytes);
    std::string keyed_text(keyed_bytes->begin(), keyed_bytes->end());
    TEST_ASSERT_NULL(strstr(keyed_text.c_str(), "node_msgid="));
}

// ---------------------------------------------------------------------------
// countersLoad() contract (src/counters_store.h): if the counters file is
// absent (a node upgrading onto this firmware for the first time), the
// counter must fall back to whatever init_flash()'s settings load already
// left in meshcom_settings.node_msgid -- for the legacy v1 blob path, that is
// a REAL value (the frozen layout still carries node_msgid at its own
// offset), not the struct default.
// ---------------------------------------------------------------------------

static void test_counters_file_absent_keeps_legacy_msgid(void)
{
    s_meshcom_settings stored;
    strncpy(stored.node_call, "MSGID-ABS", sizeof(stored.node_call) - 1);
    stored.node_msgid = 417;
    stored.node_power = 22;
    stored.node_freq = 433175000.0f;
    stored.node_bw = 1;
    stored.node_sf = 11;
    stored.node_cr = 2;
    stored.node_country = 1;
    stored.max_hop_text = 4;
    auto legacy = make_legacy_blob_from(stored);
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // No keyed store, no counters file.

    init_flash();

    TEST_ASSERT_EQUAL_INT32(msgIdAfterLoad(417), meshcom_settings.node_msgid);

    const std::vector<uint8_t> *counters_bytes = g_fake_fs.peek(kCountersPath);
    TEST_ASSERT_NOT_NULL(counters_bytes);
    std::string counters_text(counters_bytes->begin(), counters_bytes->end());
    char expect_line[32];
    snprintf(expect_line, sizeof(expect_line), "node_msgid=%d", msgIdAfterLoad(417));
    TEST_ASSERT_NOT_NULL(strstr(counters_text.c_str(), expect_line));
}

// ---------------------------------------------------------------------------
// Task 1: the legacy blob is read through the FROZEN v1 layout
// (s_ble_settings_v1), not the live s_meshcom_settings struct -- member
// order differs between the two (meshcom_settings.h's own file banner: "the
// member order below is free to change" vs. ble_settings_v1.h's frozen
// order). Picks members whose relative order/offset actually differs
// between the two layouts, so a regression that went back to reading the
// blob directly into meshcom_settings would scramble them, not merely widen
// or narrow a field.
// ---------------------------------------------------------------------------

static void test_legacy_blob_reads_through_frozen_v1_layout(void)
{
    s_meshcom_settings stored;
    strncpy(stored.node_call, "V1LAYOUT1", sizeof(stored.node_call) - 1);
    stored.node_owgpio = 77;
    stored.node_mcp17out = 111;
    stored.node_mcp17in = 222;
    stored.node_msgid = 555;
    stored.node_wifi_power = 88;
    stored.node_specstart = 440.5f;
    auto legacy = make_legacy_blob_from(stored);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(s_ble_settings_v1), (uint32_t)legacy.size());
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // No keyed store -- the legacy path is the only one reachable.

    Serial.clear();
    init_flash();

    TEST_ASSERT_EQUAL_STRING("V1LAYOUT1", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_INT32(77, meshcom_settings.node_owgpio);
    TEST_ASSERT_EQUAL_INT32(111, meshcom_settings.node_mcp17out);
    TEST_ASSERT_EQUAL_INT32(222, meshcom_settings.node_mcp17in);
    TEST_ASSERT_EQUAL_INT32(555, meshcom_settings.node_msgid - kMsgIdPersistStep); // countersLoad() advances it
    TEST_ASSERT_EQUAL_INT32(88, meshcom_settings.node_wifi_power);
    TEST_ASSERT_EQUAL_FLOAT(440.5f, meshcom_settings.node_specstart);

    const std::string &log = Serial.captured();
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];path;legacy_migrated"));

    // Mutation check (recorded in the wave report, not executed here): reading the very same bytes
    // directly into meshcom_settings instead of through s_ble_settings_v1 -- i.e. reverting Task 1 --
    // makes every assertion above fail or read a scrambled value, because node_owgpio/node_mcp17out/
    // node_mcp17in/node_wifi_power/node_specstart sit at different offsets (and in a different
    // relative order) in the two layouts. Confirmed by hand against the pre-Task-1 code path and
    // restored; see the wave report for the exact failure output.
}

// ---------------------------------------------------------------------------
// Task 2: flash_reset() writes a v1-sized defaults blob, not a
// sizeof(s_meshcom_settings)-sized one -- the two happen to differ on this
// host's ABI (2000 vs. 2008 bytes), which is exactly the silent-drift
// failure mode Task 1's read-through-v1 cutover exists to close off from the
// WRITE side too: a defaults blob official firmware could not read back.
// ---------------------------------------------------------------------------

static void test_flash_reset_writes_v1_sized_defaults(void)
{
    auto legacy = make_legacy_blob("PRE-RESET");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());

    flash_reset();

    const auto *after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(s_ble_settings_v1), (uint32_t)after->size());

    // Markers must validate through the same frozen-layout accessor init_flash() itself uses --
    // proves the bytes are a real v1 image, not merely the right length by coincidence.
    s_ble_settings_v1 img{};
    memcpy(&img, after->data(), sizeof(img));
    TEST_ASSERT_TRUE(bleSettingsV1MarkersOk(img));
}

// ---------------------------------------------------------------------------
// A blob whose size equals sizeof(s_meshcom_settings) but not
// sizeof(s_ble_settings_v1) must still reset to defaults -- on this host's
// ABI sizeof(s_meshcom_settings) is coincidentally 2000, the SAME number the
// pre-D1-04 code compared against as a LITERAL; that literal would have
// silently accepted a live-struct-sized blob under the OLD check, exactly
// the regression this cutover (Task 1: compare against sizeof(s_ble_settings_v1),
// never a literal) has to guard against.
// ---------------------------------------------------------------------------

static void test_wrong_sized_legacy_blob_resets_to_defaults(void)
{
    TEST_ASSERT_NOT_EQUAL(sizeof(s_meshcom_settings), sizeof(s_ble_settings_v1));

    std::vector<uint8_t> blob(sizeof(s_meshcom_settings), 0xAB);
    // Valid v1 markers at the front, so only the SIZE check can catch this -- not a marker mismatch.
    blob[0] = 0xAA;
    blob[1] = MESHCOM_DATA_MARKER; // == BLE_SETTINGS_V1_MARK_2, same offset in both layouts
    g_fake_fs.seed(kLegacyPath, blob.data(), blob.size());

    init_flash();

    s_meshcom_settings defaults;
    TEST_ASSERT_EQUAL_STRING(defaults.node_call, meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_UINT8(MESHCOM_DATA_MARKER, meshcom_settings.valid_mark_2);
}

// ---------------------------------------------------------------------------
// Task 7 (addendum): the keyed store's fast path must not win over a legacy
// blob that changed since the last time this firmware recorded its CRC --
// the scenario is a node downgraded to official firmware (which only ever
// writes the raw v1 blob), reconfigured there, then upgraded back: without
// this gate the keyed store's now-stale configuration would silently win.
// ---------------------------------------------------------------------------

static void test_rewritten_legacy_blob_beats_stale_keyed_store(void)
{
    auto keyed = make_keyed_store("OLD");
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    auto legacy = make_legacy_blob("NEW");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    // No CRC file -- "never recorded" must be treated exactly like "recorded but different".

    Serial.clear();
    init_flash();

    TEST_ASSERT_EQUAL_STRING("NEW", meshcom_settings.node_call);

    const std::string &log = Serial.captured();
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];path;legacy_rewritten"));
    TEST_ASSERT_NOT_NULL(strstr(log.c_str(), "[SETST];path;legacy_migrated"));

    // The keyed store was rewritten with the blob's content, and a fresh CRC record now exists.
    const auto *keyed_after = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(keyed_after);
    std::string keyed_text(keyed_after->begin(), keyed_after->end());
    TEST_ASSERT_NOT_NULL(strstr(keyed_text.c_str(), "node_call=NEW"));
    TEST_ASSERT_TRUE(g_fake_fs.exists(kLegacyCrcPath));
}

// The other half: an UNCHANGED legacy blob (its CRC matches what was
// recorded) must let the keyed store win and must leave the blob untouched
// -- the ordinary boot, proven the same way
// test_keyed_fast_path_wins_and_leaves_legacy_untouched already does, but
// named and grouped with its Task 7 counterpart above for the wave report.

// A blob rewritten by official firmware carries the counter that firmware advanced; a counters file
// left behind by an earlier run of THIS firmware is older than that and must not win (W3c advisor
// finding 2). blob 700, counters file 300, no CRC record -> the counter continues from 700.
static void test_rewritten_legacy_blob_counter_beats_stale_counters_file(void)
{
    s_meshcom_settings stored;
    strncpy(stored.node_call, "CNT-BLOB", sizeof(stored.node_call) - 1);
    stored.node_msgid = 700;
    stored.node_power = 22;
    stored.node_freq = 433175000.0f;
    stored.node_bw = 1;
    stored.node_sf = 11;
    stored.node_cr = 2;
    stored.node_country = 1;
    stored.max_hop_text = 4;
    auto legacy = make_legacy_blob_from(stored);
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    const char stale_counters[] = "node_msgid=300\n";
    g_fake_fs.seed(kCountersPath, (const uint8_t *)stale_counters, sizeof(stale_counters) - 1);

    init_flash();

    TEST_ASSERT_EQUAL_INT32(msgIdAfterLoad(700), meshcom_settings.node_msgid);
    const std::vector<uint8_t> *counters_bytes = g_fake_fs.peek(kCountersPath);
    TEST_ASSERT_NOT_NULL(counters_bytes);
    std::string counters_text(counters_bytes->begin(), counters_bytes->end());
    char expect_line[32];
    snprintf(expect_line, sizeof(expect_line), "node_msgid=%d", msgIdAfterLoad(700));
    TEST_ASSERT_NOT_NULL(strstr(counters_text.c_str(), expect_line));
}

static void test_unchanged_legacy_blob_does_not_override_keyed_store(void)
{
    auto legacy = make_legacy_blob("LEGACY-C1");
    g_fake_fs.seed(kLegacyPath, legacy.data(), legacy.size());
    seed_legacy_blob_crc(legacy);

    auto keyed = make_keyed_store("KEYED-C2");
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    Serial.clear();
    init_flash();

    TEST_ASSERT_EQUAL_STRING("KEYED-C2", meshcom_settings.node_call);

    const std::string &log = Serial.captured();
    TEST_ASSERT_NULL(strstr(log.c_str(), "[SETST];path;legacy_rewritten"));

    // The blob itself is byte-for-byte untouched.
    const auto *legacy_after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(legacy_after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)legacy.size(), (uint32_t)legacy_after->size());
    TEST_ASSERT_EQUAL_MEMORY(legacy.data(), legacy_after->data(), legacy.size());
}

// ---------------------------------------------------------------------------
// encode() overflow -- NOT COVERED, reported rather than faked.
//
// settingsStoreSave()'s overflow branch (encode() returning -1) can only be
// reached by content whose real encoded size exceeds kSettingsBufferCap
// (4096 B, settings_store_nrf52.cpp). That constant is a private, non-exported
// value inside an anonymous namespace -- there is no seam to shrink it from a
// test, so the only way to reach the branch is through genuinely oversized
// field content. Measured empirically while writing this suite: filling every
// CFG_STR field the real schema persists to its declared capacity with
// backslashes (the one byte encode() always expands to two, i.e. the actual
// worst case, not merely a long string) encodes to 3396 B -- comfortably
// under the 4096 B cap, and there is no field left to add content to (every
// other struct member is numeric, fixed-width, or not part of the persisted
// schema at all: settings_schema.h's own "nicht im Flash" fields). The
// buffer's ~700 B of headroom over that measured worst case (on top of the
// ~1.4x margin already recorded in
// docs/opt07-nrf52-settings-store-sizing-20260912.md) is exactly why: this
// branch is deliberately unreachable through any value the current schema
// can legally hold. Exercising it would need either a mock encode() (which
// would stop testing the real save path) or shrinking the real
// kSettingsBufferCap (product-code change, out of scope for a test suite) --
// so it is reported here rather than weakened into a synthetic pass.
// ---------------------------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_keyed_fast_path_wins_and_leaves_legacy_untouched);

    RUN_TEST(test_sanity_gate_rejects_empty_store_file);
    RUN_TEST(test_sanity_gate_rejects_all_unknown_keys);
    RUN_TEST(test_sanity_gate_rejects_blank_node_call);

    RUN_TEST(test_legacy_migrates_into_keyed_store);

    RUN_TEST(test_retired_marker_resets_to_defaults);

    RUN_TEST(test_save_skip_if_unchanged_performs_no_second_write);
    RUN_TEST(test_save_atomicity_live_path_held_old_content_until_rename);
    RUN_TEST(test_rename_failure_keeps_old_file_and_reports_the_filesystem);
    RUN_TEST(test_rename_failure_recovers_on_the_retry);

    RUN_TEST(test_node_power_sentinel_survives_round_trip);

    RUN_TEST(test_save_allocation_failure_leaves_everything_untouched);
    RUN_TEST(test_load_allocation_failure_reports_read_not_ok);

    RUN_TEST(test_flash_reset_preserves_bonds_and_writes_exact_length);
    RUN_TEST(test_flash_reset_writes_v1_sized_defaults);
    RUN_TEST(test_max_hop_text_sentinel_survives_round_trip);
    RUN_TEST(test_node_msgid_persists_via_counters_file);
    RUN_TEST(test_load_advances_the_msgid_block_via_counters_file);
    RUN_TEST(test_counters_file_absent_keeps_legacy_msgid);

    RUN_TEST(test_legacy_blob_reads_through_frozen_v1_layout);
    RUN_TEST(test_wrong_sized_legacy_blob_resets_to_defaults);

    RUN_TEST(test_rewritten_legacy_blob_beats_stale_keyed_store);
    RUN_TEST(test_rewritten_legacy_blob_counter_beats_stale_counters_file);
    RUN_TEST(test_unchanged_legacy_blob_does_not_override_keyed_store);

    return UNITY_END();
}
