// Fake in-memory NVS for the native ESP32 flash-lifecycle suite
// (test_esp32_flash_lifecycle).
//
// Shadows the real Arduino-ESP32 `Preferences.h` (angle-bracket include,
// ordinary -I shadowing -- src/esp32/esp32_flash.cpp has no same-directory
// copy to fight, same situation as test/test_esp32_settings_nvs/stubs/Preferences.h,
// which this file is a close relative of). Models the subset of the real
// class esp32_flash.cpp actually calls:
//
//   begin()/end()/clear()/freeEntries()
//   getChar/putChar, getInt/putInt, getUInt/putUInt,
//   getFloat/putFloat, getDouble/putDouble, getBool/putBool,
//   getString/putString
//
// FIDELITY TO THE REAL LIBRARY (verified against
// ~/.platformio/packages/framework-arduinoespressif32/libraries/Preferences/src/Preferences.cpp
// while writing this stub -- same three properties test_esp32_settings_nvs's
// stub asserts, and this suite has its own dedicated case for them, see
// test_preferences_fake_models_real_begin_end_semantics below):
//
//   - Preferences::begin() returns false and does NOT reopen if this object
//     is already started (re-entrant no-op) -- modelled via `_started`. The
//     namespace name passed to a no-op begin() is ignored too, exactly like
//     the real class: `ns_` is only set on a REAL open.
//   - Preferences::end() unconditionally closes if started, and is a no-op
//     if it is not.
//   - Every get*() checks `_started` FIRST and returns the caller's default
//     untouched if not -- real device behaviour when a handle got closed out
//     from under a caller still mid-sequence.
//
// MULTIPLE NAMESPACES, MULTIPLE INSTANCES (D1-04 W3 step 4): the store is
// keyed by NAMESPACE, not flat -- `begin(name, ...)` selects which
// namespace's key/value map this Preferences object reads and writes for the
// rest of its open span, exactly like real NVS partitions namespaces from
// each other (a key in "Counters" and a same-named key in "Credentials" are
// two independent entries). This is what lets counters_store.h's ESP32
// backend (esp32_flash.cpp) open a SEPARATE static Preferences instance
// against "Counters" without it aliasing the "Credentials" namespace
// save_settings()/init_flash() use through the global `preferences` object --
// see esp32_flash.cpp's own comment on why that separation matters
// (bce95db5). Every Preferences instance in a test (the global `preferences`,
// esp32_flash.cpp's own static counters handle, or a short-lived local one)
// shares this ONE process-wide FakeNvs store, the same as the real firmware's
// single NVS partition.
//
// INSTRUMENTATION THIS SUITE ADDS ON TOP OF test_esp32_settings_nvs's STUB
// (the whole reason this is a separate file rather than a shared one): a
// monotonically increasing `generation` counter, GLOBAL across every
// Preferences object this fake ever opens (not per-instance) -- bumped once
// per REAL (non-reentrant) begin(), on ANY object. A write's or read's
// recorded generation says which open/close SPAN of *whatever handle was
// open at the time* it happened in -- generation 1 is init_flash()'s own
// load span (the global `preferences` object's begin() near init_flash()'s
// top through its own first real end()); any later generation can only exist
// because that span, or a later one on a DIFFERENT object, was genuinely
// opened after it. Being global rather than per-object is what lets this
// suite's "no write during the still-open load handle" assertions
// (test_no_write_happens_while_load_handle_is_open) stay meaningful once
// counters_store.h introduces additional Preferences objects (its own static
// handle, plus a short-lived one for the legacy-key fallback read): a write
// through ANY of them still carries the true global generation it happened
// under, so "generation 1" unambiguously still means "nested inside
// init_flash()'s own still-open load handle" no matter which object made the
// call. Each Preferences instance's own generation() accessor still reports
// "the global generation number as of MY last real open" -- monotonically
// increasing across repeated opens of that SAME object, which is all
// test_preferences_fake_models_real_begin_end_semantics relies on.
#pragma once

#ifndef NATIVE_BUILD
#error "test_esp32_flash_lifecycle/stubs/Preferences.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h> // String (test/support/Arduino.h)

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// The fake NVS itself: one process-wide store (mirrors the single real NVS
// partition every Preferences instance in the real firmware -- and every
// Preferences instance a test constructs here -- ultimately opens namespaces
// against), keyed by namespace name.
// ---------------------------------------------------------------------------
class FakeNvs
{
public:
    enum class Kind
    {
        I64,
        F64,
        Bool,
        Str
    };

    struct Entry
    {
        Kind kind;
        int64_t i = 0;
        double d = 0.0;
        bool b = false;
        std::string s;
    };

    // One recorded get*()/put*() call: which namespace, which key, and which
    // open/close generation of a Preferences handle it happened under (see
    // the file-top comment). generation 0 means "the handle was not started
    // at all" -- a get*() call that fell straight through to its default.
    struct Access
    {
        std::string ns;
        std::string key;
        int generation;
    };

    static FakeNvs &instance()
    {
        static FakeNvs nvs;
        return nvs;
    }

    // Clears every namespace's store AND every log -- call between test
    // cases so one case's writes/opens can never leak into the next.
    void reset()
    {
        namespaces_.clear();
        reads_.clear();
        writes_.clear();
        openCount_ = 0;
        closeCount_ = 0;
        globalGeneration_ = 0;
    }

    bool hasKey(const char *key, const char *ns = "Credentials") const
    {
        auto nit = namespaces_.find(ns ? ns : "");
        if (nit == namespaces_.end())
            return false;
        return nit->second.count(key ? key : "") != 0;
    }

    void recordOpen() { openCount_++; }
    void recordClose() { closeCount_++; }
    // Real (non-reentrant) begin() calls seen so far, across every namespace
    // and every Preferences object -- also the "current generation" once at
    // least one has happened.
    int openCount() const { return openCount_; }
    // Real (started-was-true) end() calls seen so far, same scope as above.
    int closeCount() const { return closeCount_; }

    // Bumps and returns the GLOBAL generation counter -- called once per
    // REAL open, by whichever Preferences object just opened. See the
    // file-top comment for why this is global rather than per-instance.
    int bumpGeneration() { return ++globalGeneration_; }

    void recordRead(const char *ns, const char *key, int generation)
    {
        reads_.push_back({ns ? ns : "", key ? key : "", generation});
    }
    void recordWrite(const char *ns, const char *key, int generation)
    {
        writes_.push_back({ns ? ns : "", key ? key : "", generation});
    }
    const std::vector<Access> &reads() const { return reads_; }
    const std::vector<Access> &writes() const { return writes_; }

    // Test-only direct write, bypassing Preferences entirely -- used to seed
    // a value a real boot would have stored on a previous save. Namespace
    // defaults to "Credentials" (the settings store) so every pre-existing
    // call site in this suite -- written before namespaces existed -- keeps
    // working unchanged; the counter migration tests pass "Counters" or
    // "Credentials" explicitly where the distinction is the point.
    void seedString(const char *key, const std::string &value, const char *ns = "Credentials")
    {
        Entry e;
        e.kind = Kind::Str;
        e.s = value;
        namespaces_[ns ? ns : ""][key ? key : ""] = e;
    }
    void seedInt(const char *key, int64_t value, const char *ns = "Credentials")
    {
        Entry e;
        e.kind = Kind::I64;
        e.i = value;
        namespaces_[ns ? ns : ""][key ? key : ""] = e;
    }
    // FLOAT and DOUBLE fields both round-trip through Preferences' own
    // getFloat()/putFloat() and getDouble()/putDouble() as Kind::F64 here
    // (see getScalar/putScalar below) -- one seed helper covers both.
    void seedDouble(const char *key, double value, const char *ns = "Credentials")
    {
        Entry e;
        e.kind = Kind::F64;
        e.d = value;
        namespaces_[ns ? ns : ""][key ? key : ""] = e;
    }

    // Namespace defaults to "Credentials" for the same back-compat reason as
    // the seed helpers above. Non-const: probing a namespace that has never
    // been written creates it empty, harmless (mirrors "an empty namespace
    // has no keys" either way).
    std::map<std::string, Entry> &entries(const char *ns = "Credentials") { return namespaces_[ns ? ns : ""]; }

private:
    FakeNvs() = default;
    std::map<std::string, std::map<std::string, Entry>> namespaces_;
    std::vector<Access> reads_;
    std::vector<Access> writes_;
    int openCount_ = 0;
    int closeCount_ = 0;
    int globalGeneration_ = 0;
};

// ---------------------------------------------------------------------------
// Preferences -- the class esp32_flash.cpp actually calls.
// ---------------------------------------------------------------------------
class Preferences
{
public:
    Preferences() {}

    // Test-only: puts this object back to its just-constructed state. Does
    // NOT touch FakeNvs's global generation counter (that is process-wide
    // now, reset via FakeNvs::instance().reset() in setUp() instead) --
    // needed so this object's own `_started`/`_readOnly`/`ns_` never leak
    // between Unity cases in this one binary. Call from setUp() on the
    // global `preferences` object specifically (the one product code shares
    // across calls); a freshly constructed local Preferences needs no reset.
    void resetForTest()
    {
        _started = false;
        _readOnly = false;
        ns_.clear();
        generation_ = 0;
    }

    bool begin(const char *name, bool readOnly = false, const char *partition_label = nullptr)
    {
        (void)partition_label;
        if (_started)
            return false; // real semantics: does NOT reopen, does not touch _readOnly or the namespace
        ns_ = name ? name : "";
        _started = true;
        _readOnly = readOnly;
        generation_ = FakeNvs::instance().bumpGeneration();
        FakeNvs::instance().recordOpen();
        return true;
    }

    void end()
    {
        if (!_started)
            return;
        _started = false;
        FakeNvs::instance().recordClose();
    }

    bool clear()
    {
        if (!_started || _readOnly)
            return false;
        FakeNvs::instance().entries(ns_.c_str()).clear();
        return true;
    }

    size_t freeEntries()
    {
        // Arbitrary large NVS-like capacity; no test in this suite asserts
        // on the exact number, only that the call does not crash.
        size_t used = _started ? FakeNvs::instance().entries(ns_.c_str()).size() : 0;
        return used < 500 ? 500 - used : 0;
    }

    bool isKey(const char *key) { return _started && FakeNvs::instance().hasKey(key, ns_.c_str()); }

    // Test-only accessors (no counterpart in the real Preferences API).
    bool isStarted() const { return _started; }
    int generation() const { return generation_; }

    int8_t getChar(const char *key, int8_t defaultValue = 0) { return getScalar(key, defaultValue, FakeNvs::Kind::I64, &FakeNvs::Entry::i); }
    size_t putChar(const char *key, int8_t value) { return putScalar(key, (int64_t)value, FakeNvs::Kind::I64, &FakeNvs::Entry::i, sizeof(value)); }

    int32_t getInt(const char *key, int32_t defaultValue = 0) { return getScalar(key, defaultValue, FakeNvs::Kind::I64, &FakeNvs::Entry::i); }
    size_t putInt(const char *key, int32_t value) { return putScalar(key, (int64_t)value, FakeNvs::Kind::I64, &FakeNvs::Entry::i, sizeof(value)); }

    uint32_t getUInt(const char *key, uint32_t defaultValue = 0) { return getScalar(key, defaultValue, FakeNvs::Kind::I64, &FakeNvs::Entry::i); }
    size_t putUInt(const char *key, uint32_t value) { return putScalar(key, (int64_t)value, FakeNvs::Kind::I64, &FakeNvs::Entry::i, sizeof(value)); }

    float getFloat(const char *key, float defaultValue = 0.0f) { return getScalar(key, defaultValue, FakeNvs::Kind::F64, &FakeNvs::Entry::d); }
    size_t putFloat(const char *key, float value) { return putScalar(key, (double)value, FakeNvs::Kind::F64, &FakeNvs::Entry::d, sizeof(value)); }

    double getDouble(const char *key, double defaultValue = 0.0) { return getScalar(key, defaultValue, FakeNvs::Kind::F64, &FakeNvs::Entry::d); }
    size_t putDouble(const char *key, double value) { return putScalar(key, value, FakeNvs::Kind::F64, &FakeNvs::Entry::d, sizeof(value)); }

    bool getBool(const char *key, bool defaultValue = false) { return getScalar(key, defaultValue, FakeNvs::Kind::Bool, &FakeNvs::Entry::b); }
    size_t putBool(const char *key, bool value) { return putScalar(key, value, FakeNvs::Kind::Bool, &FakeNvs::Entry::b, sizeof(value)); }

    String getString(const char *key, String defaultValue = String())
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordRead(ns_.c_str(), key, gen);
        if (!_started)
            return defaultValue;
        auto &entries = FakeNvs::instance().entries(ns_.c_str());
        auto it = entries.find(key ? key : "");
        if (it == entries.end() || it->second.kind != FakeNvs::Kind::Str)
            return defaultValue;
        return String(it->second.s.c_str());
    }

    size_t putString(const char *key, String value)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordWrite(ns_.c_str(), key, gen);
        if (!_started || _readOnly)
            return 0;
        FakeNvs::Entry e;
        e.kind = FakeNvs::Kind::Str;
        e.s = value.c_str();
        FakeNvs::instance().entries(ns_.c_str())[key ? key : ""] = e;
        return e.s.size();
    }

private:
    template <typename T, typename Field>
    T getScalar(const char *key, T defaultValue, FakeNvs::Kind kind, Field FakeNvs::Entry::*field)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordRead(ns_.c_str(), key, gen);
        if (!_started)
            return defaultValue;
        auto &entries = FakeNvs::instance().entries(ns_.c_str());
        auto it = entries.find(key ? key : "");
        if (it == entries.end() || it->second.kind != kind)
            return defaultValue; // absent key, or type mismatch -- both return the caller's default, like real NVS
        return (T)(it->second.*field);
    }

    template <typename StoreT, typename Field>
    size_t putScalar(const char *key, StoreT value, FakeNvs::Kind kind, Field FakeNvs::Entry::*field, size_t reportedSize)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordWrite(ns_.c_str(), key, gen);
        if (!_started || _readOnly)
            return 0;
        FakeNvs::Entry e;
        e.kind = kind;
        e.*field = value;
        FakeNvs::instance().entries(ns_.c_str())[key ? key : ""] = e;
        return reportedSize;
    }

    bool _started = false;
    bool _readOnly = false;
    std::string ns_;
    int generation_ = 0;
};
