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

#include <msgid_counter.h>
#include <nrf52/WisBlock-API.h> // s_meshcom_settings, meshcom_settings, init_flash_done, MESHCOM_DATA_MARKER
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

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

// A legacy-format blob: a raw struct blit, exactly what nrf52_flash.cpp's
// pre-cutover load/save path read and wrote directly. Built from an actual
// native s_meshcom_settings instance (never hand-assembled byte offsets) so
// this suite's notion of "the struct's bytes" always matches whatever this
// host's ABI actually produces, the same object the product code reads back.
static std::vector<uint8_t> make_legacy_blob(const char *node_call)
{
    s_meshcom_settings s; // struct defaults
    strncpy(s.node_call, node_call, sizeof(s.node_call) - 1);
    s.node_call[sizeof(s.node_call) - 1] = '\0';
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&s);
    return std::vector<uint8_t>(p, p + sizeof(s));
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

    // Exactly sizeof(s_meshcom_settings), not double: a missing remove()
    // before the defaults rewrite would append onto the OLD legacy content
    // instead of replacing it.
    const auto *after = g_fake_fs.peek(kLegacyPath);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(s_meshcom_settings), (uint32_t)after->size());
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
// node_msgid / node_ackid persistence (Fable verdict, Finding 3): restored to
// SETTINGS_PERSIST_ONLY_LIST (settings_schema.h) after being dropped from the
// schema. Losing either row silently restarts that counter at 0 every
// reboot, replaying msg_ids into every neighbour's dedup ring
// (src/loop_functions.cpp:3331).
// ---------------------------------------------------------------------------

static void test_node_msgid_and_ackid_persist_round_trip(void)
{
    meshcom_settings.node_msgid = 321;
    meshcom_settings.node_ackid = 654;
    strncpy(meshcom_settings.node_call, "MSGID-001", sizeof(meshcom_settings.node_call) - 1);

    TEST_ASSERT_TRUE(settingsStoreSave());

    meshcom_settings = s_meshcom_settings();
    meshcom_settings.node_msgid = 0; // zeroed explicitly, even though this is also the struct default
    meshcom_settings.node_ackid = 0;

    SettingsLoadResult r = settingsStoreLoad();
    TEST_ASSERT_TRUE(r.file_found);
    TEST_ASSERT_TRUE(r.read_ok);
    TEST_ASSERT_EQUAL_INT32(321, meshcom_settings.node_msgid);
    TEST_ASSERT_EQUAL_INT32(654, meshcom_settings.node_ackid);
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

// ---------------------------------------------------------------------------
// The message-id high-water mark on the REAL load path (msgid_counter.h).
// The unit test for the policy lives in test_msgid_counter; what is asserted
// here is the half that policy cannot do for itself -- that init_flash()
// actually advances the counter past the block the previous run may have
// used, and that the advanced value REACHES FLASH before the first frame can
// go out. Without the write-back the scheme silently reuses ids after a crash
// in the first frames of a boot.
// ---------------------------------------------------------------------------

static void test_load_advances_the_msgid_block_and_writes_it_back(void)
{
    s_meshcom_settings stored;
    strncpy(stored.node_call, "MSGID-HWM", sizeof(stored.node_call) - 1);
    stored.node_msgid = 200;
    // Everything else deliberately VALID, so sanitize_loaded_settings() finds
    // nothing to correct: its pre-existing "something was fixed" write would
    // otherwise carry the counter to flash by accident and this test would
    // pass with the write-back removed (it did, until these lines existed).
    stored.node_power = 22;
    stored.node_freq = 433175000.0f;
    stored.node_bw = 1;
    stored.node_sf = 11;
    stored.node_cr = 2;
    stored.node_country = 1;
    stored.max_hop_text = 4;
    auto keyed = make_keyed_store(stored);
    g_fake_fs.seed(kKeyedPath, keyed.data(), keyed.size());

    init_flash();

    TEST_ASSERT_EQUAL_INT32(200 + kMsgIdPersistStep, meshcom_settings.node_msgid);

    // ... and the file says so too, so a crash before the next persist point
    // cannot hand out that block a second time.
    const std::vector<uint8_t> *live = g_fake_fs.peek(kKeyedPath);
    TEST_ASSERT_NOT_NULL(live);
    s_meshcom_settings reloaded;
    settings_store::decode(settings_schema::fields(), settings_schema::fieldCount(), &reloaded,
                           (const char *)live->data(), live->size());
    TEST_ASSERT_EQUAL_INT32(200 + kMsgIdPersistStep, reloaded.node_msgid);
}

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
    RUN_TEST(test_max_hop_text_sentinel_survives_round_trip);
    RUN_TEST(test_node_msgid_and_ackid_persist_round_trip);
    RUN_TEST(test_load_advances_the_msgid_block_and_writes_it_back);

    return UNITY_END();
}
