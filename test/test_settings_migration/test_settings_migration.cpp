// FORMAL-02 wave 1: settings written by every release since the keyed store landed (2026-10-01) load
// with the current code, and the current schema round-trips losslessly.
//
// Fixtures (tools/settings_fixtures.py, read-only `git show <tag>:<schema>`): per tag a keyed record
// (<tag>.rec) and a key/type manifest (<tag>.keys) under test/test_settings_migration/fixtures/.
//
// Code path under test: settings_store::decode over settings_schema::fields() -- exactly what
// settingsStoreLoad() (nRF52) runs. The ESP32 NVS path (esp32_flash.cpp loadFieldFromPreferences) does NOT share
// that codec: Preferences hands back typed values with no text decode and no clamp. What can break an old
// NVS image is a key whose stored type changed (the typed getter then silently returns the default), so for
// ESP32 the suite pins per-key type stability against the manifest (test_nvs_key_types_stable_across_tags).
//
// Two builds of this file:
//   native_settings_migration        ESP32 + T-Deck, the widest schema (codec only, see above).
//   native_settings_migration_nrf52  NRF52_SERIES: the REAL settingsStoreLoad() (src/nrf52/settings_store_nrf52.cpp) runs
//                                    over the in-memory InternalFS of test_nrf52_settings_paths (stubs reused via -I).
//                                    Fixture keys the nRF52 schema lacks (platform esp32 | tdeck) are unknown keys and
//                                    must be ignored. Radio values are converted to nRF52 units (Hz, bw/cr index) before
//                                    the sanitize check. Not covered: init_flash()/sanitize_loaded_settings() (nrf52_flash.cpp).
//
//   pio test -e native_settings_migration
//   pio test -e native_settings_migration_nrf52
#include <unity.h>

#include <repo_file.h>

#include <meshcom_settings.h>
#include <settings_sanitize.h>
#include <settings_schema.h>
#include <settings_store.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(NRF52_SERIES)
#include <nrf52/WisBlock-API.h> // stub (force-included): s_meshcom_settings, extern meshcom_settings
#include <nrf52/settings_store_nrf52.h> // settingsStoreLoad()
#include "Adafruit_LittleFS.h" // g_fake_fs
// Defined by nrf52_flash.cpp in the firmware; that file is not built here.
s_meshcom_settings meshcom_settings;
#define MIG_NRF52 1
#else
#define MIG_NRF52 0
#endif

using settings_store::DecodeStats;
using settings_store::FieldDescriptor;
using settings_store::FieldType;

namespace
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::vector<std::string> split_lines(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        size_t j = s.find('\n', i);
        if (j == std::string::npos)
            j = s.size();
        if (j > i)
            out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

struct Fixture
{
    std::string tag;
    std::vector<std::pair<std::string, std::string>> rec; // key, raw value text
    std::map<std::string, std::string> type;              // key -> manifest type name
    std::map<std::string, std::string> plat;              // key -> common | esp32 | tdeck | nrf52
};

std::vector<Fixture> load_fixtures()
{
    std::vector<Fixture> out;
    for (const std::string &tag : split_lines(repo_slurp("test/test_settings_migration/fixtures/INDEX")))
    {
        Fixture f;
        f.tag = tag;
        const std::string base = "test/test_settings_migration/fixtures/" + tag;
        for (const std::string &l : split_lines(repo_slurp((base + ".rec").c_str())))
        {
            size_t eq = l.find('=');
            TEST_ASSERT_TRUE_MESSAGE(eq != std::string::npos, "fixture line without '='");
            f.rec.emplace_back(l.substr(0, eq), l.substr(eq + 1));
        }
        for (const std::string &l : split_lines(repo_slurp((base + ".keys").c_str())))
        {
            size_t sp = l.find(' ');
            size_t sp2 = l.find(' ', sp + 1);
            size_t sp3 = l.find(' ', sp2 + 1);
            f.type[l.substr(0, sp)] = l.substr(sp + 1, sp2 - sp - 1);
            f.plat[l.substr(0, sp)] = l.substr(sp2 + 1, sp3 - sp2 - 1);
        }
        TEST_ASSERT_EQUAL_size_t_MESSAGE(f.rec.size(), f.type.size(), "rec/keys row count differs");
        out.push_back(f);
    }
    return out;
}

const FieldDescriptor *find_key(const std::string &key)
{
    const FieldDescriptor *f = settings_schema::fields();
    for (size_t i = 0; i < settings_schema::fieldCount(); i++)
        if (key == f[i].key)
            return &f[i];
    return nullptr;
}

bool manifest_type_matches(const std::string &name, FieldType t)
{
    // CFG_CHR is described to the codec as U8 (settings_schema.cpp).
    if (name == "STR") return t == FieldType::STRING;
    if (name == "CHR") return t == FieldType::U8;
    if (name == "INT") return t == FieldType::I32;
    if (name == "U32") return t == FieldType::U32;
    if (name == "BOOL") return t == FieldType::BOOL;
    if (name == "FLT") return t == FieldType::FLOAT;
    if (name == "DBL") return t == FieldType::DOUBLE;
    return false;
}

size_t width_of(const FieldDescriptor &d)
{
    switch (d.type)
    {
    case FieldType::I8: case FieldType::U8: case FieldType::BOOL: return 1;
    case FieldType::I32: case FieldType::U32: case FieldType::FLOAT: return 4;
    case FieldType::DOUBLE: return 8;
    case FieldType::STRING: return d.size;
    }
    return 0;
}

// Field equality as the type defines it: bytes for scalars (bit-exact floats), content up to NUL for strings.
bool field_equal(const FieldDescriptor &d, const void *a, const void *b)
{
    const char *pa = (const char *)a + d.offset;
    const char *pb = (const char *)b + d.offset;
    if (d.type == FieldType::STRING)
        return strncmp(pa, pb, d.size) == 0;
    return memcmp(pa, pb, width_of(d)) == 0;
}

std::string unescape(const std::string &v)
{
    std::string o;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (v[i] == '\\' && i + 1 < v.size())
        {
            i++;
            o += v[i] == 'n' ? '\n' : v[i] == 'r' ? '\r' : v[i];
        }
        else
            o += v[i];
    }
    return o;
}

double clamp_to_range(double v, const FieldDescriptor &d)
{
    if (!d.has_range) return v;
    return v < d.min_value ? d.min_value : v > d.max_value ? d.max_value : v;
}

// Independent expectation from the fixture text (libc parse, not the codec's): empty string = value as written
// is reproduced; otherwise a description of the mismatch. `clamped` is set when the schema range moved the value.
std::string check_value(const FieldDescriptor &d, const std::string &text, const void *state, bool &clamped)
{
    const char *p = (const char *)state + d.offset;
    char msg[160];
    clamped = false;
    switch (d.type)
    {
    case FieldType::I8: case FieldType::I32: case FieldType::U8: case FieldType::U32:
    {
        long long raw = strtoll(text.c_str(), nullptr, 10);
        long long want = raw;
        if (d.allowed_values && d.allowed_count)
        {
            bool in = false;
            for (size_t i = 0; i < d.allowed_count; i++) in = in || d.allowed_values[i] == raw;
            if (!in) want = d.allowed_values[0];
        }
        want = (long long)clamp_to_range((double)want, d);
        clamped = want != raw;
        long long got = 0;
        if (d.type == FieldType::I32) { int32_t v; memcpy(&v, p, 4); got = v; }
        else if (d.type == FieldType::U32) { uint32_t v; memcpy(&v, p, 4); got = v; }
        else if (d.type == FieldType::U8) { uint8_t v; memcpy(&v, p, 1); got = v; }
        else { int8_t v; memcpy(&v, p, 1); got = v; }
        if (got != want)
        {
            snprintf(msg, sizeof(msg), "int got %lld want %lld (text %s)", got, want, text.c_str());
            return msg;
        }
        return "";
    }
    case FieldType::FLOAT:
    {
        double raw = (double)strtof(text.c_str(), nullptr);   // single rounding, unlike the codec's strtod
        float want = (float)clamp_to_range(raw, d);
        clamped = (double)want != raw;
        float got; memcpy(&got, p, 4);
        if (memcmp(&got, &want, 4) != 0)
        {
            snprintf(msg, sizeof(msg), "float got %.9g want %.9g (text %s)", (double)got, (double)want, text.c_str());
            return msg;
        }
        return "";
    }
    case FieldType::DOUBLE:
    {
        double raw = strtod(text.c_str(), nullptr);
        double want = clamp_to_range(raw, d);
        clamped = want != raw;
        double got; memcpy(&got, p, 8);
        if (memcmp(&got, &want, 8) != 0)
        {
            snprintf(msg, sizeof(msg), "double got %.17g want %.17g (text %s)", got, want, text.c_str());
            return msg;
        }
        return "";
    }
    case FieldType::BOOL:
    {
        bool want = text == "1", got; memcpy(&got, p, 1);
        if (got != want) return "bool differs";
        return "";
    }
    case FieldType::STRING:
    {
        std::string want = unescape(text);
        if (want != std::string(p, strnlen(p, d.size)))
        {
            snprintf(msg, sizeof(msg), "string got '%s' want '%s'", p, want.c_str());
            return msg;
        }
        return "";
    }
    }
    return "unknown type";
}

// Keys whose range was narrowed after the first releases (production lock, config_json.h: "Spectrum fields are in
// the setter ranges"): a value an old build stored outside the new range is clamped on load, by design.
bool documented_clamp(const std::string &key)
{
    return key == "node_spstart" || key == "node_spend" || key == "node_spstep" || key == "node_spsamp";
}

std::string record_text(const Fixture &f)
{
    std::string s;
    for (auto &kv : f.rec) s += kv.first + "=" + kv.second + "\n";
    return s;
}

// Loads `text` into `st` the way the platform does. nRF52: seed the fake InternalFS and run settingsStoreLoad() (the
// target struct is the global meshcom_settings, starting from the struct defaults as after boot). ESP32: the codec only.
DecodeStats load_record(const std::string &text, s_meshcom_settings &st)
{
#if MIG_NRF52
    g_fake_fs.reset();
    g_fake_fs.seed("/MeshCom-Settings-Store", text.data(), text.size());
    meshcom_settings = s_meshcom_settings();
    SettingsLoadResult r = settingsStoreLoad();
    TEST_ASSERT_TRUE_MESSAGE(r.file_found && r.read_ok, "settingsStoreLoad did not read the seeded record");
    st = meshcom_settings;
    return r.stats;
#else
    st = s_meshcom_settings();
    return settings_store::decode(settings_schema::fields(), settings_schema::fieldCount(), &st, text.data(), text.size());
#endif
}

void expect_empty(const std::string &failures, const char *what)
{
    if (!failures.empty())
    {
        printf("%s:\n%s", what, failures.c_str());
        fflush(stdout);
        TEST_FAIL_MESSAGE(what);
    }
}

} // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Old records load
// ---------------------------------------------------------------------------

void test_fixture_inventory_minimums()
{
    std::vector<Fixture> fx = load_fixtures();
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(25, fx.size(), "release tags since 2026-10-01");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(MIG_NRF52 ? 120 : 150, settings_schema::fieldCount(),
                                                MIG_NRF52 ? "schema rows (nRF52 view)" : "schema rows (ESP32 + T-Deck view)");
    size_t total = 0;
    for (auto &f : fx)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(120, f.rec.size(), f.tag.c_str());
        total += f.rec.size();
    }
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(3000, total);
}

void test_every_old_record_loads_with_current_schema()
{
    std::string failures;
    size_t keys_checked = 0, removed_total = 0, clamped_total = 0;
    for (const Fixture &f : load_fixtures())
    {
        s_meshcom_settings st;
        const std::string text = record_text(f);
        DecodeStats stats = load_record(text, st);
        size_t removed = 0;
        for (auto &kv : f.rec)
        {
            const FieldDescriptor *d = find_key(kv.first);
            if (!d) { removed++; continue; }
            bool clamped = false;
            std::string why = check_value(*d, kv.second, &st, clamped);
            if (!why.empty()) failures += f.tag + " " + kv.first + ": " + why + "\n";
            else if (clamped && documented_clamp(kv.first)) clamped_total++;
            else if (clamped) failures += f.tag + " " + kv.first + ": value moved by current range/allowed set\n";
            keys_checked++;
        }
        removed_total += removed;
        char m[160];
        if (stats.malformed_lines != 0 || stats.fields_set != f.rec.size() - removed || stats.unknown_keys != removed ||
            stats.lines_total != f.rec.size() || stats.values_substituted != 0)
        {
            snprintf(m, sizeof(m), "%s stats: lines %zu set %zu unknown %zu malformed %zu subst %zu (rec %zu removed %zu)\n",
                     f.tag.c_str(), stats.lines_total, stats.fields_set, stats.unknown_keys, stats.malformed_lines,
                     stats.values_substituted, f.rec.size(), removed);
            failures += m;
        }
    }
    expect_empty(failures, "old record did not load as written");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(MIG_NRF52 ? 2500 : 3000, keys_checked);
    // spectrum rows are ESP32-only; on nRF52 no fixture value may be moved by a range at all
    if (MIG_NRF52) TEST_ASSERT_EQUAL_size_t_MESSAGE(0, clamped_total, "nRF52 load moved a value by range");
    else TEST_ASSERT_GREATER_THAN_size_t_MESSAGE((size_t)0, clamped_total, "the spectrum range narrowing is no longer exercised");
    printf("[migration] %zu key values checked, %zu removed-key lines ignored, %zu documented clamps\n", keys_checked,
           removed_total, clamped_total);
}

void test_keys_added_later_keep_their_default()
{
    std::string failures;
    size_t absent_total = 0;
    s_meshcom_settings def;
    for (const Fixture &f : load_fixtures())
    {
        s_meshcom_settings st;
        const std::string text = record_text(f);
        load_record(text, st);
        std::map<std::string, bool> have;
        for (auto &kv : f.rec) have[kv.first] = true;
        const FieldDescriptor *d = settings_schema::fields();
        for (size_t i = 0; i < settings_schema::fieldCount(); i++)
        {
            if (have.count(d[i].key)) continue;
            absent_total++;
            if (!field_equal(d[i], &st, &def)) failures += f.tag + " " + d[i].key + ": default lost\n";
        }
    }
    expect_empty(failures, "key absent from an old record did not keep its default");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(100, absent_total, "no key added after the oldest tag?");
}

void test_fixture_values_are_not_the_defaults()
{
    // A fixture value equal to the struct default would make "loaded" indistinguishable from "kept default".
    std::string failures;
    s_meshcom_settings def;
    for (const Fixture &f : load_fixtures())
    {
        s_meshcom_settings st;
        const std::string text = record_text(f);
        load_record(text, st);
        for (auto &kv : f.rec)
        {
            const FieldDescriptor *d = find_key(kv.first);
            if (d && !documented_clamp(kv.first) && field_equal(*d, &st, &def)) failures += f.tag + " " + kv.first + ": fixture value == default\n";
        }
    }
    expect_empty(failures, "fixture value indistinguishable from the default (adjust tools/settings_fixtures.py)");
}

void test_removed_keys_are_ignored_and_listed()
{
    std::map<std::string, int> removed;
    for (const Fixture &f : load_fixtures())
        for (auto &kv : f.rec)
            if (!find_key(kv.first)) removed[kv.first]++;
    for (auto &r : removed) printf("[migration] key gone from the current schema: %s (%d tags)\n", r.first.c_str(), r.second);
    // An unknown key must neither set a field nor count as malformed.
    s_meshcom_settings st;
    const char rec[] = "gone_key_xyz=5\nnode_sf=9\n";
    DecodeStats s = settings_store::decode(settings_schema::fields(), settings_schema::fieldCount(), &st, rec, sizeof(rec) - 1);
    TEST_ASSERT_EQUAL_size_t(1, s.unknown_keys);
    TEST_ASSERT_EQUAL_size_t(1, s.fields_set);
    TEST_ASSERT_EQUAL_size_t(0, s.malformed_lines);
    TEST_ASSERT_EQUAL_INT(9, st.node_sf);
}

void test_nvs_key_types_stable_across_tags()
{
    // ESP32 NVS has no shared codec; a key whose type changed between releases reads back as its default.
    std::string failures;
    size_t compared = 0;
    for (const Fixture &f : load_fixtures())
        for (auto &kv : f.rec)
        {
            const FieldDescriptor *d = find_key(kv.first);
            if (!d) continue;
            compared++;
            if (!manifest_type_matches(f.type.at(kv.first), d->type))
                failures += f.tag + " " + kv.first + ": type " + f.type.at(kv.first) + " at the tag, different now\n";
            if (!MIG_NRF52 && kv.first.size() > 15) failures += f.tag + " " + kv.first + ": NVS key longer than 15\n";
        }
    expect_empty(failures, "NVS key type drift");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(MIG_NRF52 ? 2500 : 3000, compared);
}

void test_platform_key_subset_of_newest_fixture()
{
    // Per the manifest platform column: this build's schema holds every key of its platforms and none of the others
    // (an esp32/tdeck key in an nRF52 record is then an unknown key, counted and ignored by the test above).
    std::vector<Fixture> fx = load_fixtures();
    const Fixture &f = fx.back();
    std::string failures;
    size_t present = 0, foreign = 0;
    for (auto &kv : f.plat)
    {
        const std::string &pl = kv.second;
#if MIG_NRF52
        bool mine = pl == "common" || pl == "nrf52";
#else
        bool mine = pl == "common" || pl == "esp32" || pl == "tdeck";
#endif
        bool have = find_key(kv.first) != nullptr;
        if (mine && !have) failures += kv.first + " (" + pl + "): missing from this build's schema\n";
        if (!mine && have) failures += kv.first + " (" + pl + "): present in this build's schema\n";
        (mine ? present : foreign)++;
    }
    expect_empty(failures, "platform key subset differs from the manifest");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(MIG_NRF52 ? 100 : 140, present);
    printf("[migration] newest %s: %zu keys of this platform, %zu of other platforms\n", f.tag.c_str(), present, foreign);
}

void test_loaded_old_records_pass_sanitize()
{
    // The load path of both platforms runs settings_sanitize on the decoded struct; a record that sanitize
    // rewrites would change on the first boot after the update. ESP32 limits (MHz, kHz, 5..8); nRF52 (Hz, index).
    std::string failures;
    for (const Fixture &f : load_fixtures())
    {
        s_meshcom_settings st;
        const std::string text = record_text(f);
        load_record(text, st);
#if MIG_NRF52
        // nRF52 stores Hz, a bandwidth index (0..2) and a coding-rate index (1..4); fixtures carry the ESP32 units.
        RadioLimits lim = { TX_POWER_MIN, TX_POWER_MAX, 400.0e6f, 960.0e6f, 1, 1, 17 };
        float freq = st.node_freq == 0.0f ? 0.0f : (float)((double)st.node_freq * 1.0e6);
        float bw = st.node_bw == 125.0f ? 0.0f : st.node_bw == 250.0f ? 1.0f : st.node_bw == 500.0f ? 2.0f : st.node_bw;
        int cr = st.node_cr >= 5 && st.node_cr <= 8 ? st.node_cr - 4 : st.node_cr;
        RadioParams p = { st.node_power, freq, bw, st.node_sf, cr, st.node_country };
        {
            // negative control: the unconverted ESP32 units must be rewritten by the nRF52 limits
            RadioParams raw = { st.node_power, st.node_freq, st.node_bw, st.node_sf, st.node_cr, st.node_country };
            if (st.node_freq != 0.0f && sanitize_radio_params(raw, lim, nullptr) == 0)
                failures += f.tag + ": nRF52 limits accepted ESP32 radio units\n";
        }
#else
        RadioLimits lim = { TX_POWER_MIN, TX_POWER_MAX, 400.0f, 960.0f, 0, 0, 17 };
        RadioParams p = { st.node_power, st.node_freq, st.node_bw, st.node_sf, st.node_cr, st.node_country };
#endif
        if (sanitize_radio_params(p, lim, nullptr) != 0) failures += f.tag + ": radio params rewritten\n";
        int hop = st.max_hop_text;
        if (sanitize_max_hop_text(hop, nullptr)) failures += f.tag + ": max_hop_text rewritten\n";
        int dht = st.node_dht_type;
        if (sanitize_dht_type(dht, nullptr)) failures += f.tag + ": node_dht_type rewritten\n";
        const FieldDescriptor *d = settings_schema::fields();
        for (size_t i = 0; i < settings_schema::fieldCount(); i++)
            if (d[i].type == FieldType::STRING && sanitize_cstring((char *)&st + d[i].offset, d[i].size))
                failures += f.tag + " " + d[i].key + ": terminator missing\n";
    }
    expect_empty(failures, "sanitize rewrote a loaded old record");
}

// ---------------------------------------------------------------------------
// Current schema: lossless round trip with edge values
// ---------------------------------------------------------------------------

namespace
{

void fill_edges(s_meshcom_settings &st, bool high, size_t &n_by_type_out_of_range)
{
    (void)n_by_type_out_of_range;
    const FieldDescriptor *d = settings_schema::fields();
    static const char pat[] = {'a', '\\', '\n', '\r', '=', (char)0xE4, ' ', 'Z'};
    for (size_t i = 0; i < settings_schema::fieldCount(); i++)
    {
        char *p = (char *)&st + d[i].offset;
        switch (d[i].type)
        {
        case FieldType::STRING:
            memset(p, 0, d[i].size);
            for (size_t k = 0; k + 1 < d[i].size; k++) p[k] = pat[(k + (high ? 0 : 3)) % sizeof(pat)];
            break;
        case FieldType::I8: case FieldType::I32: case FieldType::U8: case FieldType::U32:
        {
            long long lo, hi;
            if (d[i].allowed_values && d[i].allowed_count)
            {
                lo = d[i].allowed_values[0];
                hi = d[i].allowed_values[d[i].allowed_count - 1];
            }
            else if (d[i].has_range) { lo = (long long)d[i].min_value; hi = (long long)d[i].max_value; }
            else if (d[i].type == FieldType::I32) { lo = INT32_MIN; hi = INT32_MAX; }
            else if (d[i].type == FieldType::U32) { lo = 0; hi = UINT32_MAX; }
            else if (d[i].type == FieldType::U8) { lo = 0; hi = 255; }
            else { lo = -128; hi = 127; }
            long long v = high ? hi : lo;
            if (d[i].type == FieldType::I32) { int32_t x = (int32_t)v; memcpy(p, &x, 4); }
            else if (d[i].type == FieldType::U32) { uint32_t x = (uint32_t)v; memcpy(p, &x, 4); }
            else if (d[i].type == FieldType::U8) { uint8_t x = (uint8_t)v; memcpy(p, &x, 1); }
            else { int8_t x = (int8_t)v; memcpy(p, &x, 1); }
            break;
        }
        case FieldType::FLOAT:
        {
            float x = d[i].has_range ? (float)(high ? d[i].max_value : d[i].min_value) : (high ? FLT_MAX : -FLT_MAX);
            if (!d[i].has_range && !high) x = -0.0f;
            memcpy(p, &x, 4);
            break;
        }
        case FieldType::DOUBLE:
        {
            double x = d[i].has_range ? (high ? d[i].max_value : d[i].min_value) : (high ? DBL_MAX : DBL_MIN);
            memcpy(p, &x, 8);
            break;
        }
        case FieldType::BOOL:
        {
            bool b = high;
            memcpy(p, &b, 1);
            break;
        }
        }
    }
}

} // namespace

void test_current_schema_roundtrip_edge_values_is_lossless()
{
    static std::vector<char> buf(1 << 20);
    size_t unused = 0;
    size_t types[8] = {0};
    for (size_t i = 0; i < settings_schema::fieldCount(); i++) types[(int)settings_schema::fields()[i].type]++;
    // every FieldType the schema uses is exercised (I8 is not used by any row today)
    for (FieldType t : {FieldType::U8, FieldType::I32, FieldType::U32, FieldType::FLOAT, FieldType::DOUBLE,
                        FieldType::STRING})
        TEST_ASSERT_GREATER_THAN_size_t((size_t)0, types[(int)t]);
    // BOOL rows are T-Deck only (settings manifest platform "tdeck"); the nRF52 schema has none
    if (!MIG_NRF52) TEST_ASSERT_GREATER_THAN_size_t((size_t)0, types[(int)FieldType::BOOL]);

    for (int pass = 0; pass < 2; pass++)
    {
        s_meshcom_settings a;
        fill_edges(a, pass == 1, unused);
        long n = settings_store::encode(settings_schema::fields(), settings_schema::fieldCount(), &a, buf.data(), buf.size());
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, n, "encode failed");
        s_meshcom_settings b;
        DecodeStats s = settings_store::decode(settings_schema::fields(), settings_schema::fieldCount(), &b, buf.data(), (size_t)n);
        TEST_ASSERT_EQUAL_size_t(settings_schema::fieldCount(), s.fields_set);
        TEST_ASSERT_EQUAL_size_t(0, s.malformed_lines);
        TEST_ASSERT_EQUAL_size_t(0, s.values_substituted);
        std::string failures;
        const FieldDescriptor *d = settings_schema::fields();
        for (size_t i = 0; i < settings_schema::fieldCount(); i++)
            if (!field_equal(d[i], &a, &b)) failures += std::string(d[i].key) + (pass ? " (high)" : " (low)") + ": changed\n";
        expect_empty(failures, "round trip changed a field");
        // encode(decode(x)) is byte-identical to x
        static std::vector<char> buf2(1 << 20);
        long n2 = settings_store::encode(settings_schema::fields(), settings_schema::fieldCount(), &b, buf2.data(), buf2.size());
        TEST_ASSERT_EQUAL_INT64(n, n2);
        TEST_ASSERT_EQUAL_MEMORY(buf.data(), buf2.data(), (size_t)n);
    }
}

// ---------------------------------------------------------------------------
// REAL contract (specs/cbmc/settings_decode MODE_ROUNDTRIP proves only the token framing for FLOAT/DOUBLE):
// every finite float/double and -0.0 come back bit-identical when the value lies inside the range (or the row has
// none); a value outside the range is clamped; nan, inf and -inf are rejected as malformed and the field keeps what
// it had (settings_store.h behaviour 4).
// ---------------------------------------------------------------------------
namespace
{
struct RealState { float f; double d; };

const FieldDescriptor kRealFields[] = {
    {"f", FieldType::FLOAT, offsetof(RealState, f), 0, false, 0, 0},
    {"d", FieldType::DOUBLE, offsetof(RealState, d), 0, false, 0, 0},
};
const FieldDescriptor kRealRanged[] = {
    {"f", FieldType::FLOAT, offsetof(RealState, f), 0, true, 0.0, 10.0},
    {"d", FieldType::DOUBLE, offsetof(RealState, d), 0, true, 0.0, 10.0},
};

RealState real_roundtrip(const FieldDescriptor *fd, const RealState &in)
{
    char buf[256];
    long n = settings_store::encode(fd, 2, &in, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    RealState out = {1.0f, 1.0};
    DecodeStats s = settings_store::decode(fd, 2, &out, buf, (size_t)n);
    TEST_ASSERT_EQUAL_size_t(2, s.fields_set);
    TEST_ASSERT_EQUAL_size_t(0, s.malformed_lines);
    return out;
}
} // namespace

void test_real_values_roundtrip_bit_identical()
{
    uint64_t x = 0x9E3779B97F4A7C15ull;
    size_t checked = 0;
    for (int i = 0; i < 60000; i++)
    {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;           // xorshift64: deterministic bit patterns
        RealState in;
        uint32_t fb = (uint32_t)x;
        uint64_t db = x * 0x2545F4914F6CDD1Dull;
        memcpy(&in.f, &fb, 4);
        memcpy(&in.d, &db, 8);
        if (!std::isfinite(in.f) || !std::isfinite(in.d)) continue;   // non-finite: see test_real_nonfinite_and_range_contract
        RealState out = real_roundtrip(kRealFields, in);
        if (memcmp(&out.f, &in.f, 4) != 0) { printf("float %.9g (0x%08x) came back %.9g\n", (double)in.f, fb, (double)out.f); fflush(stdout); TEST_FAIL_MESSAGE("float not bit-identical"); }
        if (memcmp(&out.d, &in.d, 8) != 0) { printf("double %.17g (0x%016llx) came back %.17g\n", in.d, (unsigned long long)db, out.d); fflush(stdout); TEST_FAIL_MESSAGE("double not bit-identical"); }
        checked++;
    }
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(50000, checked);

    const float fe[] = {FLT_MIN, FLT_MAX, -FLT_MAX, 1.4e-45f, -1.4e-45f, 1.0f / 3.0f, 0.1f, -0.0f};
    const double de[] = {DBL_MIN, DBL_MAX, -DBL_MAX, 4.9406564584124654e-324, 1.0 / 3.0, 0.1, -0.0};
    for (float f : fe)
        for (double d : de)
        {
            RealState in = {f, d};
            RealState out = real_roundtrip(kRealFields, in);
            TEST_ASSERT_EQUAL_MEMORY(&in.f, &out.f, 4);
            TEST_ASSERT_EQUAL_MEMORY(&in.d, &out.d, 8);
        }
}

void test_real_nonfinite_and_range_contract()
{
    // Non-finite values are rejected: the field keeps its prior value, both lines count as malformed.
    const RealState bad[] = {{NAN, NAN}, {INFINITY, -INFINITY}, {-INFINITY, INFINITY}};
    for (const RealState &in : bad)
        for (const FieldDescriptor *fd : {kRealFields, kRealRanged})
        {
            char buf[256];
            long n = settings_store::encode(fd, 2, &in, buf, sizeof(buf));
            TEST_ASSERT_GREATER_THAN(0, n);
            RealState out = {1.0f, 1.0};
            DecodeStats s = settings_store::decode(fd, 2, &out, buf, (size_t)n);
            TEST_ASSERT_EQUAL_size_t(0, s.fields_set);
            TEST_ASSERT_EQUAL_size_t(2, s.malformed_lines);
            TEST_ASSERT_TRUE(out.f == 1.0f && out.d == 1.0);
        }
    // Out of range: clamped.
    RealState hi = {99.5f, -3.25};
    RealState out = real_roundtrip(kRealRanged, hi);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, out.f);
    TEST_ASSERT_TRUE(out.d == 0.0);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fixture_inventory_minimums);
    RUN_TEST(test_every_old_record_loads_with_current_schema);
    RUN_TEST(test_keys_added_later_keep_their_default);
    RUN_TEST(test_fixture_values_are_not_the_defaults);
    RUN_TEST(test_removed_keys_are_ignored_and_listed);
    RUN_TEST(test_nvs_key_types_stable_across_tags);
    RUN_TEST(test_platform_key_subset_of_newest_fixture);
    RUN_TEST(test_loaded_old_records_pass_sanitize);
    RUN_TEST(test_current_schema_roundtrip_edge_values_is_lossless);
    RUN_TEST(test_real_values_roundtrip_bit_identical);
    RUN_TEST(test_real_nonfinite_and_range_contract);
    return UNITY_END();
}
