// Native test for W3's headline acceptance criterion (docs/BACKLOG.md,
// "Acceptance criteria for W3"):
//
//   Reordering the struct's members changes nothing observable: a test
//   reorders them and the round-trip still passes.
//
// This is the whole point of the settings-store campaign: once the on-disk
// format is a keyed "key=value" record (settings_store.h/.cpp) rather than
// a raw struct blit, the C struct's member order stops being part of the
// persisted format. Nothing asserted that until this file.
//
// s_meshcom_settings itself is a platform struct and does not compile on a
// host (see settings_store.cpp's own native test for that discussion), so
// this suite proves the property at the mechanism level: two structs with
// the SAME members in DIFFERENT declaration order, each with its own
// FieldDescriptor table built from its own offsetof/sizeof, must be fully
// interchangeable through encode()/decode().
//
//   pio test -e native_settings_roundtrip

#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <settings_store.h>

using settings_store::DecodeStats;
using settings_store::FieldDescriptor;
using settings_store::FieldType;

// ---------------------------------------------------------------------------
// Two structs, same members, different declaration order.
//
// Type spread matches the schema's actual usage (settings_store.h "TYPE
// COVERAGE"): a char[N] string, int32_t, uint32_t, float, double, bool, and
// a single `char` deliberately mapped as U8 (settings_schema.cpp maps
// CFG_CHR to U8 for exactly this reason -- a bare `char`'s signedness is
// platform-defined, so it is described to the codec as the unsigned byte
// type it is actually encoded/decoded as).
// ---------------------------------------------------------------------------

struct OrderA {
    char name[16];
    int32_t i32_field;
    uint32_t u32_field;
    float f_field;
    double d_field;
    bool b_field;
    char chr_field;    // CFG_CHR-style: a bare char, described as U8 below
    int32_t node_power; // CFG_ESC hazard case, see the dedicated test below
};

struct OrderB {
    int32_t node_power;
    bool b_field;
    double d_field;
    char chr_field;
    float f_field;
    uint32_t u32_field;
    char name[16];
    int32_t i32_field;
};

// Descriptor tables list the SAME keys in the SAME order for both structs --
// this is what "the format depends on descriptor order, not on memory
// layout" (settings_store.h) means concretely: the two tables below produce
// byte-identical output for equivalently-populated state, even though
// OrderA and OrderB place those fields at completely different offsets.
//
// node_power carries has_range = false here on purpose: the main round-trip
// tests below must not have this field silently clamped, so the dedicated
// CFG_ESC hazard test builds its OWN pair of single-field descriptors (one
// has_range = false, one true) to exercise that behaviour in isolation.

static const FieldDescriptor kFieldsA[] = {
    {"name", FieldType::STRING, offsetof(OrderA, name), sizeof(OrderA::name), false, 0, 0},
    {"i32", FieldType::I32, offsetof(OrderA, i32_field), 0, false, 0, 0},
    {"u32", FieldType::U32, offsetof(OrderA, u32_field), 0, false, 0, 0},
    {"flt", FieldType::FLOAT, offsetof(OrderA, f_field), 0, false, 0, 0},
    {"dbl", FieldType::DOUBLE, offsetof(OrderA, d_field), 0, false, 0, 0},
    {"bit", FieldType::BOOL, offsetof(OrderA, b_field), 0, false, 0, 0},
    {"chr", FieldType::U8, offsetof(OrderA, chr_field), 0, false, 0, 0},
    {"node_power", FieldType::I32, offsetof(OrderA, node_power), 0, false, 0, 0},
};
static const size_t kFieldCountA = sizeof(kFieldsA) / sizeof(kFieldsA[0]);

static const FieldDescriptor kFieldsB[] = {
    {"name", FieldType::STRING, offsetof(OrderB, name), sizeof(OrderB::name), false, 0, 0},
    {"i32", FieldType::I32, offsetof(OrderB, i32_field), 0, false, 0, 0},
    {"u32", FieldType::U32, offsetof(OrderB, u32_field), 0, false, 0, 0},
    {"flt", FieldType::FLOAT, offsetof(OrderB, f_field), 0, false, 0, 0},
    {"dbl", FieldType::DOUBLE, offsetof(OrderB, d_field), 0, false, 0, 0},
    {"bit", FieldType::BOOL, offsetof(OrderB, b_field), 0, false, 0, 0},
    {"chr", FieldType::U8, offsetof(OrderB, chr_field), 0, false, 0, 0},
    {"node_power", FieldType::I32, offsetof(OrderB, node_power), 0, false, 0, 0},
};
static const size_t kFieldCountB = sizeof(kFieldsB) / sizeof(kFieldsB[0]);

static void default_a(OrderA &s) {
    memset(&s, 0, sizeof(s));
    strcpy(s.name, "default");
    s.i32_field = -1;
    s.u32_field = 1;
    s.f_field = 1.5f;
    s.d_field = 2.5;
    s.b_field = true;
    s.chr_field = 'X';
    s.node_power = 14;
}

static void default_b(OrderB &s) {
    memset(&s, 0, sizeof(s));
    strcpy(s.name, "default");
    s.i32_field = -1;
    s.u32_field = 1;
    s.f_field = 1.5f;
    s.d_field = 2.5;
    s.b_field = true;
    s.chr_field = 'X';
    s.node_power = 14;
}

// Interesting, non-default values shared by every test below that needs
// "a populated state", so OrderA and OrderB instances are always populated
// equivalently field-for-field.
static void populate_a(OrderA &s) {
    default_a(s);
    strcpy(s.name, "DK5EN-14");
    s.i32_field = -123456;
    s.u32_field = 3000000000u;
    s.f_field = 3.14159f;
    s.d_field = 2.718281828459045;
    s.b_field = false;
    uint8_t chr_byte = 250; // exercises the top of U8's range, not just ASCII
    memcpy(&s.chr_field, &chr_byte, sizeof(chr_byte));
    s.node_power = 17;
}

static void populate_b(OrderB &s) {
    default_b(s);
    strcpy(s.name, "DK5EN-14");
    s.i32_field = -123456;
    s.u32_field = 3000000000u;
    s.f_field = 3.14159f;
    s.d_field = 2.718281828459045;
    s.b_field = false;
    uint8_t chr_byte = 250;
    memcpy(&s.chr_field, &chr_byte, sizeof(chr_byte));
    s.node_power = 17;
}

static uint8_t chr_as_byte(char c) {
    uint8_t v;
    memcpy(&v, &c, 1);
    return v;
}

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// THE criterion: OrderA -> encode -> decode into OrderB, field for field
// equal. A layout change (OrderA's declaration order vs OrderB's) is
// invisible across the format.
// ---------------------------------------------------------------------------

static void test_a_to_b_roundtrip_is_field_for_field_equal(void) {
    OrderA in;
    populate_a(in);

    char buf[512];
    long n = settings_store::encode(kFieldsA, kFieldCountA, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    OrderB out;
    memset(&out, 0xAA, sizeof(out)); // poison: a missed field must be loud
    DecodeStats st = settings_store::decode(kFieldsB, kFieldCountB, &out, buf, (size_t)n);

    TEST_ASSERT_EQUAL_UINT32(kFieldCountA, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);

    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_INT32(in.i32_field, out.i32_field);
    TEST_ASSERT_EQUAL_UINT32(in.u32_field, out.u32_field);
    TEST_ASSERT_EQUAL_FLOAT(in.f_field, out.f_field);
    // Unity's double asserts are not built with -D UNITY_INCLUDE_DOUBLE in
    // this suite; bit-compare instead (stricter anyway).
    TEST_ASSERT_EQUAL_MEMORY(&in.d_field, &out.d_field, sizeof(double));
    TEST_ASSERT_EQUAL_INT(in.b_field, out.b_field);
    TEST_ASSERT_EQUAL_UINT8(chr_as_byte(in.chr_field), chr_as_byte(out.chr_field));
    TEST_ASSERT_EQUAL_INT32(in.node_power, out.node_power);
}

// ---------------------------------------------------------------------------
// The other direction: OrderB -> encode -> decode into OrderA. Lossless.
// ---------------------------------------------------------------------------

static void test_b_to_a_roundtrip_is_lossless(void) {
    OrderB in;
    populate_b(in);

    char buf[512];
    long n = settings_store::encode(kFieldsB, kFieldCountB, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    OrderA out;
    memset(&out, 0xAA, sizeof(out));
    DecodeStats st = settings_store::decode(kFieldsA, kFieldCountA, &out, buf, (size_t)n);

    TEST_ASSERT_EQUAL_UINT32(kFieldCountB, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);

    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_INT32(in.i32_field, out.i32_field);
    TEST_ASSERT_EQUAL_UINT32(in.u32_field, out.u32_field);
    TEST_ASSERT_EQUAL_FLOAT(in.f_field, out.f_field);
    TEST_ASSERT_EQUAL_MEMORY(&in.d_field, &out.d_field, sizeof(double));
    TEST_ASSERT_EQUAL_INT(in.b_field, out.b_field);
    TEST_ASSERT_EQUAL_UINT8(chr_as_byte(in.chr_field), chr_as_byte(out.chr_field));
    TEST_ASSERT_EQUAL_INT32(in.node_power, out.node_power);
}

// ---------------------------------------------------------------------------
// A full round trip back through the SAME struct it started from (A -> B ->
// A), chained through both directions above, must reproduce the original
// exactly -- the two hops compose losslessly.
// ---------------------------------------------------------------------------

static void test_a_to_b_to_a_full_roundtrip(void) {
    OrderA original;
    populate_a(original);

    char buf1[512];
    long n1 = settings_store::encode(kFieldsA, kFieldCountA, &original, buf1, sizeof(buf1));
    TEST_ASSERT_GREATER_THAN(0, n1);

    OrderB middle;
    memset(&middle, 0xAA, sizeof(middle));
    settings_store::decode(kFieldsB, kFieldCountB, &middle, buf1, (size_t)n1);

    char buf2[512];
    long n2 = settings_store::encode(kFieldsB, kFieldCountB, &middle, buf2, sizeof(buf2));
    TEST_ASSERT_GREATER_THAN(0, n2);

    OrderA final_state;
    memset(&final_state, 0xAA, sizeof(final_state));
    settings_store::decode(kFieldsA, kFieldCountA, &final_state, buf2, (size_t)n2);

    TEST_ASSERT_EQUAL_STRING(original.name, final_state.name);
    TEST_ASSERT_EQUAL_INT32(original.i32_field, final_state.i32_field);
    TEST_ASSERT_EQUAL_UINT32(original.u32_field, final_state.u32_field);
    TEST_ASSERT_EQUAL_FLOAT(original.f_field, final_state.f_field);
    TEST_ASSERT_EQUAL_MEMORY(&original.d_field, &final_state.d_field, sizeof(double));
    TEST_ASSERT_EQUAL_INT(original.b_field, final_state.b_field);
    TEST_ASSERT_EQUAL_UINT8(chr_as_byte(original.chr_field), chr_as_byte(final_state.chr_field));
    TEST_ASSERT_EQUAL_INT32(original.node_power, final_state.node_power);
}

// ---------------------------------------------------------------------------
// The format depends on descriptor order, not on memory layout: encoding
// equivalently-populated OrderA and OrderB (same values, different struct
// layouts) through their respective (same-key-order) descriptor tables
// produces BYTE-IDENTICAL output.
// ---------------------------------------------------------------------------

static void test_encoding_is_byte_identical_across_layouts(void) {
    OrderA a;
    populate_a(a);
    OrderB b;
    populate_b(b);

    char buf_a[512];
    long n_a = settings_store::encode(kFieldsA, kFieldCountA, &a, buf_a, sizeof(buf_a));
    TEST_ASSERT_GREATER_THAN(0, n_a);

    char buf_b[512];
    long n_b = settings_store::encode(kFieldsB, kFieldCountB, &b, buf_b, sizeof(buf_b));
    TEST_ASSERT_GREATER_THAN(0, n_b);

    TEST_ASSERT_EQUAL_INT(n_a, n_b);
    TEST_ASSERT_EQUAL_MEMORY(buf_a, buf_b, (size_t)n_a);
}

// ---------------------------------------------------------------------------
// What the format must tolerate (settings_store.h's four read behaviours),
// re-proven here in the two-order setting rather than re-derived from
// scratch: a key present in the store but absent from the schema is
// ignored; a key absent from the store leaves that field at its
// pre-existing (default) value; a malformed line does not prevent the
// remaining lines from applying.
// ---------------------------------------------------------------------------

static void test_unknown_key_in_store_is_ignored(void) {
    // "name" is absent from kFieldsB's keys on purpose -- simulate encoding
    // with an extra field the OrderB schema does not know about.
    const char *in = "name=DK5EN-14\ni32=7\nghost_field=999\nu32=42\n";
    OrderB out;
    default_b(out);
    DecodeStats st = settings_store::decode(kFieldsB, kFieldCountB, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(1, st.unknown_keys);
    TEST_ASSERT_EQUAL_UINT32(3, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_STRING("DK5EN-14", out.name);
    TEST_ASSERT_EQUAL_INT32(7, out.i32_field);
    TEST_ASSERT_EQUAL_UINT32(42, out.u32_field);
}

static void test_key_absent_from_store_keeps_default(void) {
    // The input never mentions "u32" or "node_power" at all.
    const char *in = "name=partial\ni32=3\n";
    OrderA out;
    default_a(out);
    out.u32_field = 555;   // the pre-existing/default value that must survive
    out.node_power = 8;    // likewise
    DecodeStats st = settings_store::decode(kFieldsA, kFieldCountA, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_STRING("partial", out.name);
    TEST_ASSERT_EQUAL_INT32(3, out.i32_field);
    TEST_ASSERT_EQUAL_UINT32(555, out.u32_field);   // untouched
    TEST_ASSERT_EQUAL_INT32(8, out.node_power);     // untouched
}

static void test_malformed_line_does_not_block_remaining_lines(void) {
    const char *in =
        "name=ok1\n"
        "this line has no equals sign\n"
        "i32=999999999999\n" // overflows I32 native width, no range set
        "u32=42\n";
    OrderB out;
    default_b(out);
    out.i32_field = -9; // must survive the malformed i32 line untouched
    DecodeStats st = settings_store::decode(kFieldsB, kFieldCountB, &out, in, strlen(in));
    TEST_ASSERT_EQUAL_UINT32(2, st.malformed_lines);
    TEST_ASSERT_EQUAL_UINT32(2, st.fields_set);
    TEST_ASSERT_EQUAL_STRING("ok1", out.name);
    TEST_ASSERT_EQUAL_INT32(-9, out.i32_field); // untouched
    TEST_ASSERT_EQUAL_UINT32(42, out.u32_field);
}

// ---------------------------------------------------------------------------
// The CFG_ESC hazard a sibling wave just fixed: node_power's -20 sentinel
// against a board range of 2..22. A descriptor with has_range = false must
// round-trip -20 unchanged; the SAME field, described with has_range = true
// and that range, must clamp it to the nearer bound (2) instead.
// ---------------------------------------------------------------------------

static void test_cfg_esc_hazard_no_range_preserves_sentinel(void) {
    static const FieldDescriptor rangeless[] = {
        {"node_power", FieldType::I32, offsetof(OrderA, node_power), 0, false, 0, 0},
    };
    OrderA in;
    default_a(in);
    in.node_power = -20; // the sentinel value

    char buf[128];
    long n = settings_store::encode(rangeless, 1, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);

    OrderA out;
    default_a(out);
    out.node_power = 999; // must be overwritten by the decode below
    DecodeStats st = settings_store::decode(rangeless, 1, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_UINT32(1, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT32(-20, out.node_power); // unchanged, not clamped
}

static void test_cfg_esc_hazard_with_range_clamps_sentinel(void) {
    // Same field, same encoded bytes as above, but this table describes it
    // with the board's real range (2..22) -- the sentinel now gets clamped.
    static const FieldDescriptor ranged[] = {
        {"node_power", FieldType::I32, offsetof(OrderA, node_power), 0, true, 2.0, 22.0},
    };
    OrderA in;
    default_a(in);
    in.node_power = -20;

    char buf[128];
    long n = settings_store::encode(ranged, 1, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    // Confirm it is really the same -20 sentinel on the wire, unclamped by
    // encode() (encode() never clamps, per settings_store.h).
    TEST_ASSERT_NOT_NULL(memmem(buf, (size_t)n, "node_power=-20\n", 15));

    OrderA out;
    default_a(out);
    out.node_power = 999;
    DecodeStats st = settings_store::decode(ranged, 1, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_UINT32(1, st.fields_set);
    TEST_ASSERT_EQUAL_UINT32(0, st.malformed_lines);
    TEST_ASSERT_EQUAL_INT32(2, out.node_power); // clamped to the nearer bound
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_a_to_b_roundtrip_is_field_for_field_equal);
    RUN_TEST(test_b_to_a_roundtrip_is_lossless);
    RUN_TEST(test_a_to_b_to_a_full_roundtrip);
    RUN_TEST(test_encoding_is_byte_identical_across_layouts);
    RUN_TEST(test_unknown_key_in_store_is_ignored);
    RUN_TEST(test_key_absent_from_store_keeps_default);
    RUN_TEST(test_malformed_line_does_not_block_remaining_lines);
    RUN_TEST(test_cfg_esc_hazard_no_range_preserves_sentinel);
    RUN_TEST(test_cfg_esc_hazard_with_range_clamps_sentinel);
    return UNITY_END();
}
