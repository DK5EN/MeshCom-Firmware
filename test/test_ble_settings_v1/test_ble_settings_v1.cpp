// Native test suite for the frozen nRF52 BLE settings v1 wire format
// (src/nrf52/ble_settings_v1.h/.cpp) -- the byte-for-byte conversion between
// s_meshcom_settings and the wire image the MeshCom phone app already
// understands. See ble_settings_v1.h's file banner for why this exists.
//
//   pio test -e native_ble_settings_v1
//
// SCOPE: this suite proves the CONVERSION LOGIC (every member copied in
// both directions, the length/marker contracts settings_rx_callback()
// enforces). It does NOT re-derive the frozen ARM offsets -- those are
// static_assert'd directly in ble_settings_v1.h and compiled (and verified
// in the wave report) against the real nRF52 target
// (wiscore_rak4631/t_echo), where they are the only layout that matters for
// the actual wire bytes. This suite's own host toolchain produces a
// DIFFERENT struct layout for the same source (empirically: 2008 bytes
// here vs. 2000 on the ARM EABI target, several offsets shifted -- double
// alignment differs), which is exactly why ble_settings_v1.h guards its
// offset asserts with `#ifndef NATIVE_BUILD`. The golden byte fixture below
// therefore pins THIS HOST'S OWN layout -- a valid, deterministic
// regression guard for the conversion functions (a swapped/dropped/
// truncated member shows up as a byte diff here exactly as it would on the
// real target), just not a byte-identical copy of what ships to a phone.
#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <nrf52/ble_settings_v1.h>
#include <config_json.h> // CFG_FIELD_LIST -- the canonical app-settable field list

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Fixture-path / hex helpers (same idiom as test_ack_replay.cpp: pio test's
// cwd is not guaranteed across invocations, so try a few `../` depths).
// ---------------------------------------------------------------------------

static FILE *openRel(const char *rel, const char *mode)
{
    static const char *prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, mode);
        if (f)
            return f;
    }
    return nullptr;
}

static size_t hex2bin(const char *hex, uint8_t *out, size_t maxlen)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < maxlen)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        int h = nib(hex[0]), l = nib(hex[1]);
        if (h < 0 || l < 0)
            break;
        out[n++] = (uint8_t)((h << 4) | l);
        hex += 2;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Deterministic, index-seeded byte fill -- one distinct, non-degenerate
// pattern per struct member, so a swapped, dropped, or truncated member in
// either conversion function shows up as a byte mismatch rather than
// accidentally matching. `bool` members are restricted to a clean 0/1 byte
// (any other bit pattern is not a valid `bool` representation, so reading
// it back -- exactly what bleSettingsToV1()/FromV1() do with a plain `=` --
// would be undefined behaviour for those three members).
// ---------------------------------------------------------------------------

static void fillPattern(void *p, size_t n, int idx, bool isBool)
{
    unsigned char *b = (unsigned char *)p;
    if (isBool)
    {
        b[0] = (idx % 2) ? 1 : 0;
        return;
    }
    for (size_t i = 0; i < n; ++i)
        b[i] = (unsigned char)((idx * 31 + 7 + (int)i) & 0xFF);
}

// Every member of s_meshcom_settings / s_ble_settings_v1, in struct order,
// verbatim from src/nrf52/WisBlock-API.h at commit 64ba5774 (the commit
// ble_settings_v1.h itself freezes against) -- second boolean flags the
// three `bool`-typed members. Used only to build/verify test fixtures; NOT
// a second copy of the wire contract (that is ble_settings_v1.h's
// static_asserts) -- this list drives value construction, offsets are
// never referenced here.
#define ALL_V1_MEMBERS(X)                    \
    X(valid_mark_1, false)                   \
    X(valid_mark_2, false)                   \
    X(node_device_eui, false)                \
    X(node_call, false)                      \
    X(node_short, false)                     \
    X(node_lat, false)                       \
    X(node_lat_c, false)                     \
    X(node_lon, false)                       \
    X(node_lon_c, false)                     \
    X(node_alt, false)                       \
    X(node_symid, false)                     \
    X(node_symcd, false)                     \
    X(node_date_year, false)                 \
    X(node_date_month, false)                \
    X(node_date_day, false)                  \
    X(node_date_hour, false)                 \
    X(node_date_minute, false)               \
    X(node_date_second, false)               \
    X(node_date_hundredths, false)           \
    X(node_age, false)                       \
    X(node_temp, false)                      \
    X(node_hum, false)                       \
    X(node_press, false)                     \
    X(node_ossid, false)                     \
    X(node_opwd, false)                      \
    X(send_repeat_time, false)               \
    X(auto_join, true)                       \
    X(node_hamnet_only, false)               \
    X(node_sset, false)                      \
    X(node_maxv, false)                      \
    X(node_extern, false)                    \
    X(node_msgid, false)                     \
    X(node_ackid, false)                     \
    X(node_power, false)                     \
    X(node_freq, false)                      \
    X(node_bw, false)                        \
    X(node_sf, false)                        \
    X(node_cr, false)                        \
    X(node_atxt, false)                      \
    X(node_sset2, false)                     \
    X(node_owgpio, false)                    \
    X(node_temp2, false)                     \
    X(node_utcoff, false)                    \
    X(node_gas_res, false)                   \
    X(node_co2, false)                       \
    X(node_mcp17io, false)                   \
    X(node_mcp17t, false)                    \
    X(node_mcp17out, false)                  \
    X(node_mcp17in, false)                   \
    X(node_gcb, false)                       \
    X(node_country, false)                   \
    X(node_track_freq, false)                \
    X(node_preamplebits, false)              \
    X(node_ss_rx_pin, false)                 \
    X(node_ss_tx_pin, false)                 \
    X(node_ss_baud, false)                   \
    X(node_postime, false)                   \
    X(node_passwd, false)                    \
    X(node_sset3, false)                     \
    X(bt_code, false)                        \
    X(node_button_pin, false)                \
    X(node_ownip, false)                     \
    X(node_owngw, false)                     \
    X(node_ownms, false)                     \
    X(node_name, false)                      \
    X(node_webpwd, false)                    \
    X(node_ssid, false)                      \
    X(node_pwd, false)                       \
    X(node_analog_pin, false)                \
    X(node_analog_faktor, false)             \
    X(node_parm, false)                      \
    X(node_unit, false)                      \
    X(node_format, false)                    \
    X(node_eqns, false)                      \
    X(node_values, false)                    \
    X(node_parm_time, false)                 \
    X(node_wifi_power, false)                \
    X(node_lora_call, false)                 \
    X(node_analog_alpha, false)              \
    X(node_analog_slope, false)              \
    X(node_analog_offset, false)             \
    X(node_analog_atten, false)              \
    X(node_gwsrv, false)                     \
    X(node_tempi_off, false)                 \
    X(node_tempo_off, false)                 \
    X(node_shunt, false)                     \
    X(node_imax, false)                      \
    X(node_isamp, false)                     \
    X(node_owndns, false)                    \
    X(node_contrast, false)                  \
    X(node_fversion, false)                  \
    X(node_ownntp, false)                    \
    X(node_mversion, false)                  \
    X(node_fwversion, false)                 \
    X(node_gpsbaud, false)                   \
    X(node_cleanflash, false)                \
    X(node_netmode, false)                   \
    X(node_gpsdebug, false)                  \
    X(node_relay, false)                     \
    X(node_via, false)                       \
    X(node_sset4, false)                     \
    X(node_aprsmc, false)                    \
    X(node_pingtime, false)                  \
    X(node_pingcall, false)                  \
    X(node_pingmax, false)                   \
    X(node_specstart, false)                 \
    X(node_specend, false)                   \
    X(node_specstep, false)                  \
    X(node_specsamples, false)               \
    X(node_analog_batt_faktor, false)        \
    X(node_press_alt, false)                 \
    X(node_press_asl, false)                 \
    X(node_vbus, false)                      \
    X(node_vshunt, false)                    \
    X(node_vcurrent, false)                  \
    X(node_vpower, false)                    \
    X(node_ip, false)                        \
    X(node_dns, false)                       \
    X(node_gw, false)                        \
    X(node_subnet, false)                    \
    X(node_hasIPaddress, true)                \
    X(node_last_upd_timer, false)            \
    X(max_hop_text, false)                   \
    X(max_hop_pos, false)                    \
    X(node_update, false)                    \
    X(node_parm_1, false)                    \
    X(node_parm_t, false)                    \
    X(node_parm_id, false)                   \
    X(node_ntctemp, false)                   \
    X(node_fanon, true)                      \
    X(node_pingcount, false)                 \
    X(node_pingduration, false)

// 132 members total -- matches the count parsed from src/nrf52/WisBlock-API.h
// at commit 64ba5774 while writing this suite.

// `T x{}` value-initialization is only REQUIRED by the standard to zero
// padding -- in practice, on the exact toolchain this suite's own env
// build uses, it does not reliably do so (verified while writing this
// suite: a full-struct memcmp saw stray non-zero bytes in the gap between
// node_short and node_lat, i.e. real leftover stack content, not the
// zero-initialised padding the standard promises). memset() first, always,
// before filling members -- that is unconditionally well-defined.
static void zeroAndFillAllMembers(s_meshcom_settings &s)
{
    memset(&s, 0, sizeof(s));
    int idx = 0;
#define X(member, isBool) fillPattern(&s.member, sizeof(s.member), idx++, isBool);
    ALL_V1_MEMBERS(X)
#undef X
}

static s_ble_settings_v1 validImage()
{
    s_meshcom_settings src;
    zeroAndFillAllMembers(src);
    // The generic fill pattern above does not produce the real marker
    // bytes for valid_mark_1/valid_mark_2 -- this helper is specifically
    // for tests that need a VALID image, so pin them to the real markers
    // after the generic fill.
    src.valid_mark_1 = BLE_SETTINGS_V1_MARK_1;
    src.valid_mark_2 = BLE_SETTINGS_V1_MARK_2;

    s_ble_settings_v1 wire;
    memset(&wire, 0, sizeof(wire));
    bleSettingsToV1(src, wire);
    return wire;
}

// =============================================================================
// 1. Golden byte image -- catches ANY change to the byte sequence
//    bleSettingsToV1() produces (reorder, drop, wrong sizeof in a memcpy,
//    swapped members, ...).
// =============================================================================

static void test_golden_byte_image(void)
{
    s_meshcom_settings src;
    zeroAndFillAllMembers(src);

    s_ble_settings_v1 wire;
    memset(&wire, 0, sizeof(wire));
    bleSettingsToV1(src, wire);

    FILE *f = openRel("test/test_ble_settings_v1/golden_v1_image.hex", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "golden_v1_image.hex not found (cwd?)");
    static char hexbuf[sizeof(s_ble_settings_v1) * 2 + 16];
    size_t got = fread(hexbuf, 1, sizeof(hexbuf) - 1, f);
    fclose(f);
    hexbuf[got] = 0;

    static uint8_t expected[sizeof(s_ble_settings_v1)];
    size_t n = hex2bin(hexbuf, expected, sizeof(expected));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof(s_ble_settings_v1), n,
                                      "golden fixture length mismatch -- regenerate golden_v1_image.hex");

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected, &wire, sizeof(s_ble_settings_v1),
                                      "bleSettingsToV1() output no longer matches the committed golden "
                                      "image -- wire-format regression in the conversion function");
}

// =============================================================================
// 2. Round trip -- toV1() then fromV1() restores every field. `src`, `wire`
//    and `dst` are all explicitly memset() to zero before anything is
//    filled in (see zeroAndFillAllMembers()'s own comment on why `{}` is
//    not good enough for this), so padding is identically zero on both
//    sides and, once every member is proven copied (see the coverage check
//    in the wave report), a single whole-struct memcmp is a valid, exact
//    check.
// =============================================================================

static void test_round_trip_restores_every_field(void)
{
    s_meshcom_settings src;
    zeroAndFillAllMembers(src);

    s_ble_settings_v1 wire;
    memset(&wire, 0, sizeof(wire));
    bleSettingsToV1(src, wire);

    s_meshcom_settings dst;
    memset(&dst, 0, sizeof(dst));
    bleSettingsFromV1(wire, dst);

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&src, &dst, sizeof(src),
                                      "round trip (bleSettingsToV1 + bleSettingsFromV1) did not "
                                      "restore the original settings struct byte-for-byte");
}

// =============================================================================
// 3. Wrong length rejected (the settings_rx_callback():339 contract, now
//    bleSettingsV1LengthOk()).
// =============================================================================

static void test_wrong_length_rejected(void)
{
    TEST_ASSERT_TRUE_MESSAGE(bleSettingsV1LengthOk(sizeof(s_ble_settings_v1)),
                              "the exact frozen length must be accepted");
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1LengthOk(sizeof(s_ble_settings_v1) - 1),
                               "one byte short of the frozen length must be rejected");
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1LengthOk(sizeof(s_ble_settings_v1) + 1),
                               "one byte over the frozen length must be rejected");
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1LengthOk(0), "a zero-length write must be rejected");
}

// =============================================================================
// 4. Bad markers rejected BEFORE staging (the settings_rx_callback():344-348
//    contract, now bleSettingsV1MarkersOk()).
// =============================================================================

static void test_bad_markers_rejected(void)
{
    s_ble_settings_v1 good = validImage();
    TEST_ASSERT_TRUE_MESSAGE(bleSettingsV1MarkersOk(good), "correct markers must be accepted");

    s_ble_settings_v1 badFirst = good;
    badFirst.valid_mark_1 = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1MarkersOk(badFirst), "a wrong first marker must be rejected");

    s_ble_settings_v1 badSecond = good;
    badSecond.valid_mark_2 = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1MarkersOk(badSecond), "a wrong second marker must be rejected");

    s_ble_settings_v1 badBoth = good;
    badBoth.valid_mark_1 = 0x00;
    badBoth.valid_mark_2 = 0x00;
    TEST_ASSERT_FALSE_MESSAGE(bleSettingsV1MarkersOk(badBoth), "two wrong markers must be rejected");
}

// =============================================================================
// 5. Every field the app can set (config_json.h's CFG_FIELD_LIST -- the
//    SAME field list config export/import and the settings schema already
//    use, not a hand-picked subset invented for this suite) survives
//    bleSettingsToV1()+bleSettingsFromV1(). A field added to
//    CFG_FIELD_LIST later without a matching line in both conversion
//    functions is caught here: it stays at its s_meshcom_settings default
//    instead of picking up the fill pattern, and the per-field message
//    below names exactly which one.
// =============================================================================

static void test_every_cfg_field_survives_conversion(void)
{
    s_meshcom_settings src;
    zeroAndFillAllMembers(src); // covers every field, including every CFG_FIELD_LIST one

    s_ble_settings_v1 wire;
    memset(&wire, 0, sizeof(wire));
    bleSettingsToV1(src, wire);
    s_meshcom_settings dst;
    memset(&dst, 0, sizeof(dst));
    bleSettingsFromV1(wire, dst);

    // X is variadic for the same reason config_json.cpp's own consumer is
    // (see its CFG_ROW comment): CFG_NORANGE / CFG_NOESC / CFG_ESC() must be
    // expanded BEFORE the row macro counts arguments, or a fixed 7-parameter
    // X sees "CFG_NORANGE" as a single token and fails to match.
#define CFG_FIELD_CHECK(key, type, member, lo, hi, esc, has_esc)                                       \
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&src.member, &dst.member, sizeof(src.member),                     \
                                      "app-settable field '" #member "' (config_json.h key \"" key      \
                                      "\") did not survive bleSettingsToV1()+bleSettingsFromV1()");
#define X(...) CFG_FIELD_CHECK(__VA_ARGS__)
    CFG_FIELD_LIST(X)
#undef X
#undef CFG_FIELD_CHECK
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_golden_byte_image);
    RUN_TEST(test_round_trip_restores_every_field);
    RUN_TEST(test_wrong_length_rejected);
    RUN_TEST(test_bad_markers_rejected);
    RUN_TEST(test_every_cfg_field_survives_conversion);

    return UNITY_END();
}
