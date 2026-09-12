// Native test suite for settings_store (D1-04 target architecture,
// docs/BACKLOG.md "D1-04 target architecture: schema-driven settings").
//
// This suite invents its OWN small schema (TestState + kFields below) rather
// than config_json.cpp's real X() table -- wiring a real descriptor array
// generated from that table is later-wave work (see settings_store.h's top
// comment). What this suite proves is that the codec itself is correct
// against any well-formed schema, which is the property a later, real
// schema will then inherit for free.
//
//   pio test -e native_settings_store

#include <unity.h>

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <settings_store.h>

using settings_store::FieldDescriptor;
using settings_store::FieldType;
using settings_store::DecodeStats;

// ---------------------------------------------------------------------------
// The test schema
// ---------------------------------------------------------------------------

struct TestState {
    int8_t i8_val;
    uint8_t u8_val;
    int32_t i32_val;
    uint32_t u32_val;
    float float_val;
    double double_val;
    bool bool_val;
    char str_val[16];       // general-purpose string field, cap 16 (15 content + NUL)
    char str_cap1[1];       // degenerate: only "" ever fits (no content byte at all)
    char str_big[64];       // roomy field for escape-heavy content
    int32_t ranged_i32;     // has_range [0, 100]
    float ranged_float;     // has_range [-1.0, 1.0]
};

static const FieldDescriptor kFields[] = {
    {"i8", FieldType::I8, offsetof(TestState, i8_val), 0, false, 0, 0},
    {"u8", FieldType::U8, offsetof(TestState, u8_val), 0, false, 0, 0},
    {"i32", FieldType::I32, offsetof(TestState, i32_val), 0, false, 0, 0},
    {"u32", FieldType::U32, offsetof(TestState, u32_val), 0, false, 0, 0},
    {"flt", FieldType::FLOAT, offsetof(TestState, float_val), 0, false, 0, 0},
    {"dbl", FieldType::DOUBLE, offsetof(TestState, double_val), 0, false, 0, 0},
    {"bit", FieldType::BOOL, offsetof(TestState, bool_val), 0, false, 0, 0},
    {"str", FieldType::STRING, offsetof(TestState, str_val), sizeof(TestState::str_val), false, 0, 0},
    {"str1", FieldType::STRING, offsetof(TestState, str_cap1), sizeof(TestState::str_cap1), false, 0, 0},
    {"strbig", FieldType::STRING, offsetof(TestState, str_big), sizeof(TestState::str_big), false, 0, 0},
    {"ri32", FieldType::I32, offsetof(TestState, ranged_i32), 0, true, 0.0, 100.0},
    {"rflt", FieldType::FLOAT, offsetof(TestState, ranged_float), 0, true, -1.0, 1.0},
};
static const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

static void default_state(TestState &s) {
    memset(&s, 0, sizeof(s));
    s.i8_val = -1;
    s.u8_val = 1;
    s.i32_val = -1;
    s.u32_val = 1;
    s.float_val = 1.5f;
    s.double_val = 2.5;
    s.bool_val = true;
    strcpy(s.str_val, "default");
    s.str_cap1[0] = '\0';
    strcpy(s.str_big, "default");
    s.ranged_i32 = 50;
    s.ranged_float = 0.5f;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Round-trip: encode(state) -> decode -> identical state
// ---------------------------------------------------------------------------

static void test_roundtrip_typical_values(void) {
    TestState in;
    default_state(in);
    in.i8_val = 42;
    in.u8_val = 200;
    in.i32_val = -123456;
    in.u32_val = 3000000000u;
    in.float_val = 3.14159f;
    in.double_val = 2.718281828459045;
    in.bool_val = false;
    strcpy(in.str_val, "DK5EN-14");

    char buf[1024];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    TestState out;
    memset(&out, 0xAA, sizeof(out)); // poison, so a missed field is loud
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);

    TEST_ASSERT_EQUAL_UINT32(kFieldCount, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);

    TEST_ASSERT_EQUAL_INT8(in.i8_val, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(in.u8_val, out.u8_val);
    TEST_ASSERT_EQUAL_INT32(in.i32_val, out.i32_val);
    TEST_ASSERT_EQUAL_UINT32(in.u32_val, out.u32_val);
    TEST_ASSERT_EQUAL_FLOAT(in.float_val, out.float_val);
    // Unity's double-precision asserts are disabled in this build (no
    // -D UNITY_INCLUDE_DOUBLE) -- bit-compare instead, which is a stricter
    // check anyway (see settings_store.h "THE FLOAT/DOUBLE GUARANTEE").
    TEST_ASSERT_EQUAL_MEMORY(&in.double_val, &out.double_val, sizeof(double));
    TEST_ASSERT_EQUAL_INT(in.bool_val, out.bool_val);
    TEST_ASSERT_EQUAL_STRING(in.str_val, out.str_val);
    TEST_ASSERT_EQUAL_INT32(in.ranged_i32, out.ranged_i32);
    TEST_ASSERT_EQUAL_FLOAT(in.ranged_float, out.ranged_float);
}

static void test_roundtrip_integer_edges(void) {
    TestState in;
    default_state(in);
    in.i8_val = -128;
    in.u8_val = 0;
    in.i32_val = INT32_MIN;
    in.u32_val = 0;

    char buf[512];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TestState out;
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_INT8(-128, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(0, out.u8_val);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, out.i32_val);
    TEST_ASSERT_EQUAL_UINT32(0, out.u32_val);

    in.i8_val = 127;
    in.u8_val = 255;
    in.i32_val = INT32_MAX;
    in.u32_val = UINT32_MAX;
    n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_INT8(127, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(255, out.u8_val);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, out.i32_val);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out.u32_val);
}

// The dedicated float/double honesty test: 1.0f/3.0f is the textbook value
// that a naive "%f"/"%.6g" formatter does NOT round-trip (6 significant
// digits is short of FLT_DECIMAL_DIG=9), plus DBL/FLT MIN/MAX, -0.0 (sign
// bit), and NaN/Infinity -- all via this codec's own encode/decode, at its
// documented %.9g / %.17g precision. See settings_store.h "THE FLOAT/DOUBLE
// GUARANTEE" for why that precision is provably sufficient rather than just
// "probably enough".
static void test_roundtrip_float_double_honesty(void) {
    struct Case {
        float f;
        double d;
    };
    Case cases[] = {
        {1.0f / 3.0f, 1.0 / 3.0},
        {FLT_MIN, DBL_MIN},
        {FLT_MAX, DBL_MAX},
        {-0.0f, -0.0},
        {0.1f, 0.1},
    };
    for (const Case &c : cases) {
        TestState in;
        default_state(in);
        in.float_val = c.f;
        in.double_val = c.d;
        char buf[512];
        long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
        TEST_ASSERT_GREATER_THAN(0, n);
        TestState out;
        default_state(out);
        settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
        // Bit-exact, not "close enough": memcmp the raw representation so a
        // sign-bit-only difference (-0.0 vs 0.0) is caught too.
        TEST_ASSERT_EQUAL_MEMORY(&c.f, &out.float_val, sizeof(float));
        TEST_ASSERT_EQUAL_MEMORY(&c.d, &out.double_val, sizeof(double));
    }
}

static void test_roundtrip_nan_and_infinity(void) {
    TestState in;
    default_state(in);
    in.float_val = NAN;
    in.double_val = INFINITY;

    char buf[512];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TestState out;
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_TRUE(std::isnan(out.float_val));
    TEST_ASSERT_TRUE(std::isinf(out.double_val));
    TEST_ASSERT_TRUE(out.double_val > 0);

    default_state(in);
    in.double_val = -INFINITY;
    n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_TRUE(std::isinf(out.double_val));
    TEST_ASSERT_TRUE(out.double_val < 0);
}

static void test_roundtrip_string_edges(void) {
    TestState in;
    default_state(in);
    in.str_val[0] = '\0'; // empty string
    char buf[512];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TestState out;
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_STRING("", out.str_val);

    // Max-length content: cap is 16, so 15 chars exactly fits (+ NUL).
    strcpy(in.str_val, "123456789012345");
    TEST_ASSERT_EQUAL_UINT32(15, strlen(in.str_val));
    n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_STRING("123456789012345", out.str_val);

    // The cap-1 field: only "" fits.
    in.str_cap1[0] = '\0';
    n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_STRING("", out.str_cap1);
}

// A value containing '=' needs no escaping (split on the FIRST '='); a
// value containing '\n', '\r' or a literal backslash round-trips through
// the three-sequence escaping settings_store.h documents.
static void test_roundtrip_string_needs_escaping(void) {
    TestState in;
    default_state(in);
    strcpy(in.str_val, "a=b=c"); // embedded '=', unescaped on purpose
    char buf[512];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    // Confirm it really did travel unescaped: the raw encoded bytes contain
    // "str=a=b=c" verbatim, not e.g. "str=a\\=b\\=c".
    TEST_ASSERT_NOT_NULL(memmem(buf, (size_t)n, "str=a=b=c\n", 10));
    TestState out;
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_STRING("a=b=c", out.str_val);

    strcpy(in.str_big, "line1\nline2\\tail\rcr");
    n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    default_state(out);
    settings_store::decode(kFields, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_STRING("line1\nline2\\tail\rcr", out.str_big);
}

// A shuffled descriptor order (as if the schema table were reordered, or
// decode is handed the fields in a different order than encode used) must
// not change the outcome at all -- this is the whole point of a keyed
// format over a positional struct blit (D1-04 acceptance criterion:
// "Reordering the struct's members changes nothing observable").
static void test_field_order_is_not_observable(void) {
    TestState in;
    default_state(in);
    in.i8_val = 7;
    in.str_val[0] = 0;
    strcpy(in.str_val, "reordered");
    in.ranged_i32 = 99;

    char buf[512];
    long n = settings_store::encode(kFields, kFieldCount, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    FieldDescriptor shuffled[kFieldCount];
    // Reverse order.
    for (size_t i = 0; i < kFieldCount; i++) {
        shuffled[i] = kFields[kFieldCount - 1 - i];
    }

    TestState out;
    default_state(out);
    DecodeStats st = settings_store::decode(shuffled, kFieldCount, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_UINT32(kFieldCount, st.fields_set);
    TEST_ASSERT_EQUAL_INT8(7, out.i8_val);
    TEST_ASSERT_EQUAL_STRING("reordered", out.str_val);
    TEST_ASSERT_EQUAL_INT32(99, out.ranged_i32);
}

// ---------------------------------------------------------------------------
// Behaviour 1: value escaping -- already exercised by
// test_roundtrip_string_needs_escaping above; nothing further to add here.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Behaviour 2: unknown key on read is ignored
// ---------------------------------------------------------------------------

static void test_unknown_key_ignored(void) {
    const char *in = "i8=5\nbogus_field=123\nu8=9\n";
    TestState out;
    default_state(out);
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT8(5, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(9, out.u8_val);
}

// ---------------------------------------------------------------------------
// Behaviour 3: missing key on read takes its default
// ---------------------------------------------------------------------------

static void test_missing_key_keeps_default(void) {
    // Input never mentions "u8" at all.
    const char *in = "i8=5\n";
    TestState out;
    default_state(out);
    out.u8_val = 111; // the "default" this field must keep
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.fields_set);
    TEST_ASSERT_EQUAL_INT8(5, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(111, out.u8_val); // untouched
}

static void test_empty_input_keeps_every_default(void) {
    TestState out;
    default_state(out);
    TestState before = out;
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, "", 0);
    TEST_ASSERT_EQUAL_UINT32(0, st.lines_total);
    TEST_ASSERT_EQUAL_UINT32(0, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(TestState));
}

// ---------------------------------------------------------------------------
// Behaviour 4: malformed line is skipped, decoding continues, state for
// that field (if any) keeps its default
// ---------------------------------------------------------------------------

static void test_malformed_line_no_equals_sign(void) {
    const char *in = "i8=5\nthis has no equals sign\nu8=9\n";
    TestState out;
    default_state(out);
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_INT8(5, out.i8_val);
    TEST_ASSERT_EQUAL_UINT8(9, out.u8_val);
}

static void test_malformed_blank_line(void) {
    const char *in = "i8=5\n\nu8=9\n"; // blank line in the middle
    TestState out;
    default_state(out);
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(3, st.lines_total);
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
}

static void test_malformed_value_overflows_native_width(void) {
    // "i8" has no descriptor range, so 999 overflows its native int8 width
    // and must be rejected outright -- not wrapped, not clamped.
    const char *in = "i8=999\n";
    TestState out;
    default_state(out);
    out.i8_val = -7;
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(0, st.fields_set);
    TEST_ASSERT_EQUAL_INT8(-7, out.i8_val); // untouched
}

static void test_malformed_value_trailing_garbage(void) {
    const char *in = "i32=123abc\n";
    TestState out;
    default_state(out);
    out.i32_val = -99;
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT32(-99, out.i32_val);
}

static void test_malformed_unsigned_rejects_negative(void) {
    const char *in = "u32=-1\n";
    TestState out;
    default_state(out);
    out.u32_val = 42;
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(42, out.u32_val);
}

static void test_malformed_bool_not_zero_or_one(void) {
    const char *in = "bit=true\n";
    TestState out;
    default_state(out);
    out.bool_val = false;
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT(false, out.bool_val);
}

static void test_malformed_string_bad_escape(void) {
    const char *in = "str=abc\\qdef\n"; // "\q" is not a recognised escape
    TestState out;
    default_state(out);
    strcpy(out.str_val, "kept");
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_STRING("kept", out.str_val);
}

static void test_malformed_string_trailing_backslash(void) {
    const char *in = "str=abc\\\n"; // value is "abc\", lone trailing backslash
    TestState out;
    default_state(out);
    strcpy(out.str_val, "kept");
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_STRING("kept", out.str_val);
}

// Behaviour 4's other half, explicitly named in the brief: a value too long
// for its field is REJECTED, never truncated.
static void test_string_too_long_is_rejected_not_truncated(void) {
    // "str" has cap 16 (15 content bytes max); this value is 16 bytes.
    const char *in = "str=1234567890123456\n";
    TestState out;
    default_state(out);
    strcpy(out.str_val, "kept");
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(0, st.fields_set);
    TEST_ASSERT_EQUAL_STRING("kept", out.str_val); // not truncated to "123456789012345"
}

static void test_string_too_long_by_one_is_rejected(void) {
    // Exactly cap bytes (16) still does not fit (15 is the max content).
    char in[64];
    snprintf(in, sizeof(in), "str=%s\n", "1234567890123456");
    TestState out;
    default_state(out);
    strcpy(out.str_val, "kept");
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.malformed_lines);
    TEST_ASSERT_EQUAL_STRING("kept", out.str_val);
}

// ---------------------------------------------------------------------------
// Range clamping (has_range descriptors)
// ---------------------------------------------------------------------------

static void test_range_clamps_instead_of_rejecting(void) {
    const char *in = "ri32=500\nrflt=5.0\n";
    TestState out;
    default_state(out);
    DecodeStats st = settings_store::decode(kFields, kFieldCount, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT32(100, out.ranged_i32);   // clamped to max
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.ranged_float); // clamped to max

    const char *in2 = "ri32=-500\nrflt=-5.0\n";
    default_state(out);
    st = settings_store::decode(kFields, kFieldCount, &out, in2, strlen(in2));
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_INT32(0, out.ranged_i32);     // clamped to min
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, out.ranged_float); // clamped to min

    const char *in3 = "ri32=50\nrflt=0.25\n"; // inside range: unchanged
    default_state(out);
    st = settings_store::decode(kFields, kFieldCount, &out, in3, strlen(in3));
    TEST_ASSERT_EQUAL_INT32(50, out.ranged_i32);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, out.ranged_float);
}

// ---------------------------------------------------------------------------
// encode() contract: all-or-nothing on a too-small buffer
// ---------------------------------------------------------------------------

static void test_encode_rejects_too_small_buffer(void) {
    TestState in;
    default_state(in);
    char big[1024];
    long full = settings_store::encode(kFields, kFieldCount, &in, big, sizeof(big));
    TEST_ASSERT_GREATER_THAN(0, full);

    char small[4];
    memset(small, 0x55, sizeof(small));
    long n = settings_store::encode(kFields, kFieldCount, &in, small, sizeof(small));
    TEST_ASSERT_EQUAL_INT(-1, n);
    // Buffer must be left completely untouched, not partially written.
    for (size_t i = 0; i < sizeof(small); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x55, (uint8_t)small[i]);
    }
}

static void test_encode_exact_fit_succeeds(void) {
    TestState in;
    default_state(in);
    char big[1024];
    long full = settings_store::encode(kFields, kFieldCount, &in, big, sizeof(big));
    TEST_ASSERT_GREATER_THAN(0, full);

    char exact[1024];
    long n = settings_store::encode(kFields, kFieldCount, &in, exact, (size_t)full);
    TEST_ASSERT_EQUAL_INT(full, n);
    TEST_ASSERT_EQUAL_MEMORY(big, exact, (size_t)full);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_typical_values);
    RUN_TEST(test_roundtrip_integer_edges);
    RUN_TEST(test_roundtrip_float_double_honesty);
    RUN_TEST(test_roundtrip_nan_and_infinity);
    RUN_TEST(test_roundtrip_string_edges);
    RUN_TEST(test_roundtrip_string_needs_escaping);
    RUN_TEST(test_field_order_is_not_observable);
    RUN_TEST(test_unknown_key_ignored);
    RUN_TEST(test_missing_key_keeps_default);
    RUN_TEST(test_empty_input_keeps_every_default);
    RUN_TEST(test_malformed_line_no_equals_sign);
    RUN_TEST(test_malformed_blank_line);
    RUN_TEST(test_malformed_value_overflows_native_width);
    RUN_TEST(test_malformed_value_trailing_garbage);
    RUN_TEST(test_malformed_unsigned_rejects_negative);
    RUN_TEST(test_malformed_bool_not_zero_or_one);
    RUN_TEST(test_malformed_string_bad_escape);
    RUN_TEST(test_malformed_string_trailing_backslash);
    RUN_TEST(test_string_too_long_is_rejected_not_truncated);
    RUN_TEST(test_string_too_long_by_one_is_rejected);
    RUN_TEST(test_range_clamps_instead_of_rejecting);
    RUN_TEST(test_encode_rejects_too_small_buffer);
    RUN_TEST(test_encode_exact_fit_succeeds);
    return UNITY_END();
}
