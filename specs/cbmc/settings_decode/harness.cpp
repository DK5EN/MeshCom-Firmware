// FORMAL-01 harness: settings_store decode_signed / decode_unsigned / decode_real / decode_bool / unescape_string /
// decode_field_value (src/settings_store.cpp), the BLE/web settings import decoders.
// The functions live in an anonymous namespace, so the harness includes the .cpp directly.
// value is NOT NUL-terminated: it is allocated with exactly value_len bytes, so any read past value_len is a heap OOB.
// Descriptor domain (settings_store.h "Range clamping"): a range lies within the field's native width, min <= max,
// has_range implies finite bounds; allowed_values (if any) fit the native width.
// Modes (-D):
//   MODE_STRING   unescape_string for cap 0..CAPMAX, value_len 0..MAXLEN, out buffer exactly cap bytes (or nullptr dry run):
//                 no write past cap, success => NUL within cap, accepted content length < cap
//   MODE_SIGNED   decode_signed, native I8 or I32, nondet descriptor: accepted => result inside native width and range
//   MODE_UNSIGNED decode_unsigned, native U8 or U32, same
//   MODE_FIELD    decode_field_value for every FieldType; state buffer exactly the field width (STRING: f.size)
//   MODE_REAL     decode_real / decode_bool (strtod over-approximated by a model, see below)
//   MODE_ROUNDTRIP  encode_field_value -> decode_field_value is the identity on the field's domain (FORMAL-02 W1).
//                 One type per run: -DRT_I8 / RT_U8 / RT_I32 / RT_U32 / RT_BOOL / RT_STRING / RT_FLOAT / RT_DOUBLE.
//                 Domain: a value inside the descriptor's range and allowed set; STRING: content without NUL, length < size.
//                 FLOAT/DOUBLE: strtod is a nondet model here, so value identity (the %.9g / %.17g digit-count guarantee of
//                 the C library) is NOT provable, and ESBMC 8.7's snprintf model gives no usable "%g" length either
//                 (the framing assertion fails at once on the model, 2026-11-27): no .check job runs RT_FLOAT/RT_DOUBLE.
//                 The mode stays for a later tool version. Bit identity and framing are pinned in test_settings_migration
//                 (60k random bit patterns plus edge values).
//                 MUT_SRC_GEN includes gen/settings_store.cpp (a sed-mutated copy, see roundtrip_mut_*.check).
// Mutations: MUT_CAP (out buffer one byte smaller than cap), MUT_LEN (value buffer one byte smaller than value_len),
//            WITNESS (assert the accept path is never taken).
#include <stddef.h>
// esbmc libc has no strnlen; used only by the encoder (not under test), harness-local definition.
extern "C" size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
// strtod over-approximation (esbmc libc model trips an FP overflow in pow_by_squaring on exponent input: model artifact):
// nondet value, end pointer anywhere inside [s, s+strlen(s)]; reads nothing beyond the NUL.
extern "C" unsigned int nondet_uint();
extern "C" double nondet_double();
extern "C" double strtod(const char *s, char **end)
{
    size_t n = 0;
    while (s[n]) n++;
    size_t k = (size_t)nondet_uint();
    __ESBMC_assume(k <= n);
    if (end) *end = (char *)s + k;
    return nondet_double();
}
#ifdef MUT_SRC_GEN
#include "gen/settings_store.cpp"
#else
#include "../../../src/settings_store.cpp"
#endif
#include <assert.h>

#ifndef MAXLEN
#define MAXLEN 8
#endif
#ifndef CAPMAX
#define CAPMAX 8
#endif


extern "C" unsigned char nondet_uchar();
extern "C" bool nondet_bool();

extern "C" int nondet_int();
using namespace settings_store;

static char *make_value(size_t len)
{
#ifdef MUT_LEN
    char *v = new char[len > 0 ? len - 1 : 1];
    for (size_t i = 0; i + 1 < len; i++) v[i] = (char)nondet_uchar();
#else
    char *v = new char[len > 0 ? len : 1];
    for (size_t i = 0; i < len; i++) v[i] = (char)nondet_uchar();
#endif
    return v;
}

static size_t nondet_len()
{
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n <= MAXLEN);
#ifdef MUT_LEN
    __ESBMC_assume(n >= 1);
#endif
    return (size_t)n;
}

// Nondet allowed-value set, members inside [lo, hi].
static long long allowed_buf[2];
static void fill_common(FieldDescriptor &f, long long lo, long long hi)
{
    f.key = "k";
    f.offset = 0;
    f.size = 0;
    f.has_range = nondet_bool();
    int a = nondet_int(), b = nondet_int();
    __ESBMC_assume(a >= lo && a <= hi && b >= lo && b <= hi && a <= b);
    f.min_value = (double)a;
    f.max_value = (double)b;
    if (nondet_bool()) {
        int x = nondet_int(), y = nondet_int();
        __ESBMC_assume(x >= lo && x <= hi && y >= lo && y <= hi);
        allowed_buf[0] = x; allowed_buf[1] = y;
        f.allowed_values = allowed_buf;
        f.allowed_count = nondet_bool() ? 1 : 2;
        if (f.has_range) { // a set member substituted for an out-of-set value must not break the range (clamp follows anyway)
        }
    } else {
        f.allowed_values = nullptr;
        f.allowed_count = 0;
    }
}

#if defined(MODE_ROUNDTRIP)
// Value domain of an integer descriptor: inside the range (if any) and a member of the allowed set (if any).
static void assume_int_domain(const FieldDescriptor &f, long long v)
{
    if (f.has_range) __ESBMC_assume((double)v >= f.min_value && (double)v <= f.max_value);
    if (f.allowed_values != nullptr) {
        bool in = (v == f.allowed_values[0]) || (f.allowed_count == 2 && v == f.allowed_values[1]);
        __ESBMC_assume(in);
    }
}

int main()
{
    FieldDescriptor f;
    char enc[64];
    Sink w{enc, sizeof(enc), 0};
    bool subst = false; (void)subst;
#if defined(RT_I8) || defined(RT_I32)
#ifdef RT_I8
    long long lo = -128, hi = 127; f.type = FieldType::I8;
#else
    long long lo = -2147483648LL, hi = 2147483647LL; f.type = FieldType::I32;
#endif
    fill_common(f, lo, hi);
    long long v = (long long)nondet_int();
    __ESBMC_assume(v >= lo && v <= hi);
    assume_int_domain(f, v);
    char a[4] = {0}, b[4] = {0};
#ifdef RT_I8
    int8_t s = (int8_t)v; memcpy(a, &s, 1);
    encode_field_value(w, f, a);
    assert(w.pos <= sizeof(enc));
    bool ok = decode_field_value(f, enc, w.pos, b, &subst);
    assert(ok);
    assert(!subst && memcmp(a, b, 1) == 0);
#else
    int32_t s = (int32_t)v; memcpy(a, &s, 4);
    encode_field_value(w, f, a);
    assert(w.pos <= sizeof(enc));
    bool ok = decode_field_value(f, enc, w.pos, b, &subst);
    assert(ok);
    assert(!subst && memcmp(a, b, 4) == 0);
#endif
#elif defined(RT_U8) || defined(RT_U32)
#ifdef RT_U8
    long long hi = 255; f.type = FieldType::U8;
#else
    long long hi = 2147483647LL; f.type = FieldType::U32;   // range/allowed inputs are drawn from nondet_int, see MODE_UNSIGNED
#endif
    fill_common(f, 0, hi);
    unsigned int uv = nondet_uint();
#ifdef RT_U8
    __ESBMC_assume(uv <= 255u);
#endif
    if (f.has_range || f.allowed_values != nullptr) assume_int_domain(f, (long long)uv);
    char a[4] = {0}, b[4] = {0};
#ifdef RT_U8
    uint8_t s = (uint8_t)uv; memcpy(a, &s, 1);
    encode_field_value(w, f, a);
    assert(w.pos <= sizeof(enc));
    bool ok = decode_field_value(f, enc, w.pos, b, &subst);
    assert(ok);
    assert(!subst && memcmp(a, b, 1) == 0);
#else
    uint32_t s = (uint32_t)uv; memcpy(a, &s, 4);
    encode_field_value(w, f, a);
    assert(w.pos <= sizeof(enc));
    bool ok = decode_field_value(f, enc, w.pos, b, &subst);
    assert(ok);
    assert(!subst && memcmp(a, b, 4) == 0);
#endif
#elif defined(RT_BOOL)
    f.key = "k"; f.type = FieldType::BOOL; f.offset = 0; f.size = 0; f.has_range = false;
    f.min_value = f.max_value = 0; f.allowed_values = nullptr; f.allowed_count = 0;
    bool a = nondet_bool(), b = !a;
    encode_field_value(w, f, &a);
    assert(w.pos == 1);
    bool ok = decode_field_value(f, enc, w.pos, &b, &subst);
    assert(ok);
    assert(a == b);
#elif defined(RT_STRING)
    f.key = "k"; f.type = FieldType::STRING; f.offset = 0; f.has_range = false;
    f.min_value = f.max_value = 0; f.allowed_values = nullptr; f.allowed_count = 0;
    int sz = nondet_int();
    __ESBMC_assume(sz >= 1 && sz <= CAPMAX);
    f.size = (size_t)sz;
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n < sz);
    char a[CAPMAX + 1], b[CAPMAX + 1];
    for (int i = 0; i < CAPMAX + 1; i++) { a[i] = 0; b[i] = 0x55; }
    for (int i = 0; i < n; i++) {
        a[i] = (char)nondet_uchar();         // every byte value incl. '\\', '\n', '\r', '=', high bytes
        __ESBMC_assume(a[i] != 0);
    }
    Sink w2{enc, sizeof(enc), 0};
    encode_field_value(w2, f, a);
    assert(w2.pos <= sizeof(enc));
    bool ok = decode_field_value(f, enc, w2.pos, b, &subst);
    assert(ok);
    assert(!subst);
    for (int i = 0; i < n; i++) assert(a[i] == b[i]);
    assert(b[n] == 0);
    for (size_t i = 0; i < w2.pos; i++) assert(enc[i] != '\n');   // line framing: no raw newline in the escaped token
#elif defined(RT_FLOAT) || defined(RT_DOUBLE)
    f.key = "k"; f.offset = 0; f.size = 0; f.has_range = false; f.min_value = f.max_value = 0;
    f.allowed_values = nullptr; f.allowed_count = 0;
    char a[8] = {0};
#ifdef RT_FLOAT
    f.type = FieldType::FLOAT;
    float x; unsigned int bits = nondet_uint(); memcpy(&x, &bits, 4); memcpy(a, &x, 4);
#else
    f.type = FieldType::DOUBLE;
    double x = nondet_double(); memcpy(a, &x, 8);
#endif
    encode_field_value(w, f, a);
    assert(w.pos >= 1 && w.pos <= kNumTokenCap);
    for (size_t i = 0; i < w.pos; i++) assert(enc[i] != '\n' && enc[i] != '=' && enc[i] != 0);
#endif
    return 0;
}
#else
int main()
{
    size_t len = nondet_len();
    char *value = make_value(len);
    bool subst = false;
#if defined(MODE_STRING)
    int c = nondet_int();
    __ESBMC_assume(c >= 0 && c <= CAPMAX);
    size_t cap = (size_t)c;
    bool dry = nondet_bool();
#ifdef MUT_CAP
    __ESBMC_assume(c >= 1);
    char *out = new char[cap - 1];
#else
    char *out = new char[cap > 0 ? cap : 1];
#endif
    bool ok = unescape_string(value, len, cap, dry ? nullptr : out);
    if (ok && !dry && cap >= 1) {
        bool nul = false;
        for (size_t i = 0; i < cap; i++) if (out[i] == 0 && !nul) nul = true;
        assert(nul);
    }
#ifdef WITNESS
    assert(!(ok && !dry && len >= 1));
#endif
    delete[] out;
#elif defined(MODE_SIGNED)
    FieldDescriptor f;
    bool wide = nondet_bool();
    long long lo = wide ? -2147483648LL : -128, hi = wide ? 2147483647LL : 127;
    fill_common(f, lo, hi);
    long long out = 0;
    bool ok = decode_signed(value, len, f, lo, hi, &out, &subst);
    if (ok) {
        assert(out >= lo && out <= hi);
        if (f.has_range) assert((double)out >= f.min_value && (double)out <= f.max_value);
    }
#ifdef WITNESS
    assert(!ok);
#endif
#elif defined(MODE_UNSIGNED)
    FieldDescriptor f;
    bool wide = nondet_bool();
    long long hi = wide ? 2147483647LL : 255;   // allowed/range inputs drawn from nondet_int, so the U32 upper half is not drawn
    fill_common(f, 0, hi);
    unsigned long long nmax = wide ? 4294967295ULL : 255ULL, out = 0;
    bool ok = decode_unsigned(value, len, f, nmax, &out, &subst);
    if (ok) {
        assert(out <= nmax);
        if (f.has_range) assert((double)out >= f.min_value && (double)out <= f.max_value);
    }
#ifdef WITNESS
    assert(!ok);
#endif
#elif defined(MODE_FIELD)
    FieldDescriptor f;
    int t = nondet_int();
    __ESBMC_assume(t >= 0 && t <= 7);
    f.type = (FieldType)t;
    long long lo = -128, hi = 127; size_t sz = 1;
    switch (f.type) {
        case FieldType::I8: lo = -128; hi = 127; sz = 1; break;
        case FieldType::U8: lo = 0; hi = 255; sz = 1; break;
        case FieldType::I32: lo = -2147483648LL; hi = 2147483647LL; sz = 4; break;
        case FieldType::U32: lo = 0; hi = 2147483647LL; sz = 4; break;
        case FieldType::FLOAT: lo = -1000; hi = 1000; sz = 4; break;
        case FieldType::DOUBLE: lo = -1000; hi = 1000; sz = 8; break;
        case FieldType::BOOL: sz = 1; break;
        case FieldType::STRING: { int s = nondet_int(); __ESBMC_assume(s >= 0 && s <= CAPMAX); sz = (size_t)s; break; }
    }
    fill_common(f, lo, hi);
    f.type = (FieldType)t;
    if (f.type == FieldType::STRING) f.size = sz;
    char *state = new char[sz > 0 ? sz : 1];
    bool ok = decode_field_value(f, value, len, state, &subst);
#ifdef WITNESS
    assert(!ok);
#endif
    (void)ok;
    delete[] state;
#elif defined(MODE_REAL)
    double d = 0; bool b = false;
    bool ok = decode_real(value, len, &d);
    bool ok2 = decode_bool(value, len, &b);
    if (ok2) assert(len == 1);
    (void)ok;
#endif
    delete[] value;
    return 0;
}
#endif
