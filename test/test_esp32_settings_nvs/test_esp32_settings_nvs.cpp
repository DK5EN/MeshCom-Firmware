// Native test suite for the ESP32 settings NVS cutover (D1-04 W3 Task 1/2):
// src/esp32/esp32_flash.cpp's init_flash()/save_settings()/clear_flash(),
// compiled and RUN against the fake in-memory NVS in stubs/Preferences.h --
// there was no ESP32-side settings test at all before this suite.
//
// Everything under test is the REAL product source (src/esp32/esp32_flash.cpp,
// src/settings_store.cpp, src/settings_schema.cpp) compiled natively against
// this directory's stub Preferences.h. Built with -D MC_SAFEBOOT: it takes
// the radio-sanitize/msgid-advance/auto-save path (sanitize_loaded_settings(),
// gated `#if !defined(MC_SAFEBOOT)` in esp32_flash.cpp) out of scope for this
// suite -- that path pulls in settings_sanitize.h/msgid_counter.h/
// lora_setchip.h's full board-configuration chain, none of which this
// suite's target (the generic FieldDescriptor <-> Preferences walk) needs.
// max_hop_text is still exercised: it is loaded via its own explicit
// (non-generic) line in init_flash() regardless of MC_SAFEBOOT. node_msgid is
// NOT a settings_schema row at all any more (D1-04 W3 step 4) and its
// counters_store.h load/save path (countersLoad()/countersSave(), see
// test_esp32_flash_lifecycle) is itself gated `#if !defined(MC_SAFEBOOT)` in
// esp32_flash.cpp -- under MC_SAFEBOOT, init_flash() never touches it, so it
// simply stays at whatever setUp() reset the struct to.
//
//   pio test -e native_esp32_settings_nvs

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <esp32/esp32_flash.h> // s_meshcom_settings, meshcom_settings, init_flash()/save_settings()/clear_flash()
#include <maxhop.h>            // MAXHOP_TEXT_FALLBACK
#include <settings_schema.h>
#include <settings_store.h>

#include "Preferences.h" // FakeNvs

void setUp(void)
{
    FakeNvs::instance().reset();
    meshcom_settings = s_meshcom_settings(); // reset to compiled struct defaults
}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// 1. Round-trip: save every persisted field, reset the struct, reload --
//    every field must come back exactly as it was saved. Covers one field
//    per settings_store::FieldType this schema actually uses on ESP32
//    (I32, U32, FLOAT, DOUBLE, BOOL, U8/CFG_CHR, STRING), plus a
//    SETTINGS_PERSIST_ONLY_LIST row (node_fversion) and a T-Deck-only row
//    (node_mute) to prove platform-scoped fields round-trip too.
//
// MUTATION VERIFIED: temporarily changed loadFieldFromPreferences()'s I32
// case to leave `*p` untouched (skip the preferences.getInt() call) instead
// of reading it back -- node_alt then stayed at its post-reset struct
// default (0) instead of the saved -123, and this test FAILED
// (TEST_ASSERT_EQUAL_INT32(-123, ...) reported 0). Reverted; test passes
// again.
// ---------------------------------------------------------------------------
void test_round_trip_restores_every_persisted_field(void)
{
    strncpy(meshcom_settings.node_call, "DK5EN-9", sizeof(meshcom_settings.node_call) - 1);
    meshcom_settings.node_alt = -123;                 // I32
    meshcom_settings.node_gpsbaud = 115200;            // U32
    meshcom_settings.node_maxv = 3.7f;                 // FLOAT
    meshcom_settings.node_lat = 48.2082;                // DOUBLE
    meshcom_settings.node_symid = 'X';                  // U8 (CFG_CHR)
    meshcom_settings.node_mute = true;                  // BOOL (T-Deck-only)
    meshcom_settings.node_fversion = 7;                 // SETTINGS_PERSIST_ONLY_LIST row
    strncpy(meshcom_settings.node_gwsrv, "AA", sizeof(meshcom_settings.node_gwsrv) - 1);

    save_settings();

    // Simulate a reboot: overwrite RAM with different values before reloading.
    meshcom_settings = s_meshcom_settings();
    strncpy(meshcom_settings.node_call, "ZZ", sizeof(meshcom_settings.node_call) - 1);
    meshcom_settings.node_alt = 999;
    meshcom_settings.node_mute = false;

    init_flash();

    TEST_ASSERT_EQUAL_STRING("DK5EN-9", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_INT32(-123, meshcom_settings.node_alt);
    TEST_ASSERT_EQUAL_UINT32(115200u, meshcom_settings.node_gpsbaud);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.7f, meshcom_settings.node_maxv);
    TEST_ASSERT_DOUBLE_WITHIN(0.00001, 48.2082, meshcom_settings.node_lat);
    TEST_ASSERT_EQUAL_INT8('X', meshcom_settings.node_symid);
    TEST_ASSERT_TRUE(meshcom_settings.node_mute);
    TEST_ASSERT_EQUAL_INT(7, meshcom_settings.node_fversion);
    TEST_ASSERT_EQUAL_STRING("AA", meshcom_settings.node_gwsrv);
}

// ---------------------------------------------------------------------------
// 2. The six dropped sensor fields produce NO NVS key (operator decision
//    2026-09-13) -- they keep their struct members but must never reach the
//    fake store.
//
// MUTATION VERIFIED: temporarily re-added
//    X("node_temp", CFG_FLT, node_temp, CFG_NORANGE, CFG_NOESC)
// to settings_schema.h's SETTINGS_PERSIST_ONLY_LIST -- this test then FAILED
// (TEST_ASSERT_FALSE on hasKey("node_temp") saw true). Reverted; test passes
// again.
// ---------------------------------------------------------------------------
void test_dropped_sensor_fields_produce_no_nvs_key(void)
{
    meshcom_settings.node_temp = 21.5f;
    meshcom_settings.node_hum = 55.0f;
    meshcom_settings.node_press = 1013.0f;
    meshcom_settings.node_temp2 = 30.0f;
    meshcom_settings.node_gas_res = 12345.0f;
    meshcom_settings.node_co2 = 800.0f;

    save_settings();

    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_temp"));
    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_hum"));
    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_press"));
    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_temp2"));
    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_gas"));  // NVS key spelling, per the old hand-written code
    TEST_ASSERT_FALSE(FakeNvs::instance().hasKey("node_co2"));
}

// ---------------------------------------------------------------------------
// 3. Upgrade path: a field present in the schema but absent from the store
//    takes its default -- exercised against an EMPTY fake NVS (a genuinely
//    first-ever boot). Covers both classes of default: an ordinary field
//    whose struct initialiser already IS the load default (node_wifi_power,
//    node_contrast), and the fields whose load default had to be seeded
//    separately because it differs from the struct's own compiled default
//    (node_lat_c, node_maxv, node_power, node_owgpio, node_gwsrv,
//    node_fversion, node_kbl_sync), plus the one explicitly-loaded key
//    (max_hop_text). node_msgid is asserted too, but only as "MC_SAFEBOOT
//    never touches it" -- see that assertion's own comment.
//
// MUTATION VERIFIED: temporarily commented out the
//    meshcom_settings.node_power = -20;
// seed line in init_flash() -- this test then FAILED
// (TEST_ASSERT_EQUAL_INT32(-20, ...) saw 0, the struct's own compiled
// default). Reverted; test passes again. This is exactly the historical
// struct-default-vs-load-default drift the seed block exists to close.
// ---------------------------------------------------------------------------
void test_missing_key_takes_default_on_first_boot(void)
{
    init_flash();

    TEST_ASSERT_EQUAL_INT8('N', meshcom_settings.node_lat_c);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.200f, meshcom_settings.node_maxv);
    TEST_ASSERT_EQUAL_INT32(-20, meshcom_settings.node_power);
    TEST_ASSERT_EQUAL_INT32(0, meshcom_settings.node_owgpio);
    TEST_ASSERT_EQUAL_STRING("OE", meshcom_settings.node_gwsrv);
    TEST_ASSERT_EQUAL_INT(0, meshcom_settings.node_fversion);
    TEST_ASSERT_FALSE(meshcom_settings.node_kbl_sync);
    TEST_ASSERT_EQUAL_INT(60, meshcom_settings.node_wifi_power);   // unseeded: struct default already correct
    TEST_ASSERT_EQUAL_INT(255, meshcom_settings.node_contrast);    // unseeded: struct default already correct
    TEST_ASSERT_EQUAL_INT(0, meshcom_settings.node_msgid);         // MC_SAFEBOOT: countersLoad() never runs, struct default stands
    TEST_ASSERT_EQUAL_INT(MAXHOP_TEXT_FALLBACK, meshcom_settings.max_hop_text); // explicit load, MC_SAFEBOOT branch
}

// ---------------------------------------------------------------------------
// 4. Downgrade path: an unknown NVS key present in the store is ignored and
//    does not corrupt anything else. The ESP32 walk never enumerates NVS
//    (it only requests specific known keys by name), so an unrecognised key
//    cannot be "read" in the first place -- what could still break is the
//    KNOWN keys around it, if e.g. a future change made the walk skip a
//    field it should not.
//
// MUTATION VERIFIED: temporarily added "node_call" to
// isLoadSpecialCased()'s skip list (as if it were mistakenly treated like
// node_msgid/max_hop_text) -- node_call then stayed at its post-reset
// default ("") instead of the previously-saved "DK5EN-1", and this test
// FAILED. Reverted; test passes again.
// ---------------------------------------------------------------------------
void test_unknown_key_ignored_real_fields_unaffected(void)
{
    strncpy(meshcom_settings.node_call, "DK5EN-1", sizeof(meshcom_settings.node_call) - 1);
    meshcom_settings.node_alt = 42;
    save_settings();

    // A foreign/legacy key nothing in the schema names.
    FakeNvs::instance().seedString("node_totally_bogus", "surprise");

    meshcom_settings = s_meshcom_settings();
    init_flash();

    TEST_ASSERT_EQUAL_STRING("DK5EN-1", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_INT32(42, meshcom_settings.node_alt);
    TEST_ASSERT_TRUE(FakeNvs::instance().hasKey("node_totally_bogus")); // still there, untouched
}

// ---------------------------------------------------------------------------
// 5. Every NVS key this schema ever emits is <= 15 characters (ESP32 NVS's
//    own hard limit). settings_schema.cpp static_asserts this at compile
//    time already (SETTINGS_SCHEMA_ASSERT_KEY_LENGTH, guarded `#ifdef
//    ESP32`) -- this is the runtime companion, proving the two stay in
//    agreement rather than one silently drifting.
//
// MUTATION VERIFIED: with the compile-time gate temporarily neutered
// (SETTINGS_SCHEMA_ASSERT_KEY_LENGTH redefined to a no-op) AND one real key
// lengthened past 15 characters ("node_map" -> "node_map_toolongkey" in
// settings_schema.h), this test FAILED on the lengthened key while the
// build still succeeded (proving the runtime check catches what the
// compile-time gate, disabled for the mutation, no longer would). Both
// changes reverted; test and the real compile-time gate pass again.
// ---------------------------------------------------------------------------
void test_every_emitted_nvs_key_is_at_most_15_chars(void)
{
    save_settings();

    for (size_t i = 0; i < settings_schema::fieldCount(); i++)
    {
        const char *key = settings_schema::fields()[i].key;
        TEST_ASSERT_TRUE_MESSAGE(strlen(key) <= 15, key);
    }
    // The keys actually written to the fake store must also satisfy the
    // bound -- a second, independent view of the same property (via what
    // Preferences was actually called with, not the descriptor table).
    for (const auto &kv : FakeNvs::instance().entries())
    {
        TEST_ASSERT_TRUE_MESSAGE(kv.first.size() <= 15, kv.first.c_str());
    }
}

// ---------------------------------------------------------------------------
// 6. The char[] audio fields (D1-04 W3 Task 2) round-trip normally, and
//    truncate SAFELY (bounded, NUL-terminated, no overflow) instead of
//    overflowing when the store holds something longer than the buffer --
//    e.g. a foreign/corrupted value, since nothing in THIS firmware ever
//    writes more than the UI's own 100-character cap.
//
// MUTATION VERIFIED: temporarily changed loadFieldFromPreferences()'s
// STRING case from `snprintf(buf, d.size, "%s", v.c_str())` to
// `strcpy(buf, v.c_str())` (unbounded). Against this env's
// -fsanitize=address build, loading the 300-byte oversized value below then
// aborted the test run with a heap-buffer-overflow report instead of
// finishing (a crash counts as a failing test, and a stronger one than a
// silently wrong length would have been). Reverted to the bounded snprintf;
// test passes again.
// ---------------------------------------------------------------------------
void test_audio_fields_roundtrip_and_truncate_safely(void)
{
    strncpy(meshcom_settings.node_audio_start, "/sd/tones/start.wav", sizeof(meshcom_settings.node_audio_start) - 1);
    strncpy(meshcom_settings.node_audio_msg, "/sd/tones/msg.wav", sizeof(meshcom_settings.node_audio_msg) - 1);
    save_settings();

    meshcom_settings = s_meshcom_settings();
    init_flash();

    TEST_ASSERT_EQUAL_STRING("/sd/tones/start.wav", meshcom_settings.node_audio_start);
    TEST_ASSERT_EQUAL_STRING("/sd/tones/msg.wav", meshcom_settings.node_audio_msg);

    // Foreign/corrupted store: a value far longer than the 128-byte buffer.
    std::string oversized(300, 'A');
    FakeNvs::instance().seedString("node_audstart", oversized);

    meshcom_settings = s_meshcom_settings();
    init_flash();

    TEST_ASSERT_EQUAL_size_t(128, sizeof(meshcom_settings.node_audio_start));
    TEST_ASSERT_LESS_OR_EQUAL_UINT(127, strlen(meshcom_settings.node_audio_start));
    // Every content byte snprintf did write is the source character -- a
    // silent corruption (garbage instead of a clean prefix) would fail this.
    for (size_t i = 0; i < strlen(meshcom_settings.node_audio_start); i++)
    {
        TEST_ASSERT_EQUAL_CHAR('A', meshcom_settings.node_audio_start[i]);
    }
    // The adjacent field must be untouched -- the concrete "no overflow"
    // signal a plain length check on node_audio_start alone would miss.
    TEST_ASSERT_EQUAL_STRING("/sd/tones/msg.wav", meshcom_settings.node_audio_msg);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_restores_every_persisted_field);
    RUN_TEST(test_dropped_sensor_fields_produce_no_nvs_key);
    RUN_TEST(test_missing_key_takes_default_on_first_boot);
    RUN_TEST(test_unknown_key_ignored_real_fields_unaffected);
    RUN_TEST(test_every_emitted_nvs_key_is_at_most_15_chars);
    RUN_TEST(test_audio_fields_roundtrip_and_truncate_safely);
    return UNITY_END();
}
