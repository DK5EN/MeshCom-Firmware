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
//     is already started (re-entrant no-op) -- modelled via `_started`.
//   - Preferences::end() unconditionally closes if started, and is a no-op
//     if it is not.
//   - Every get*() checks `_started` FIRST and returns the caller's default
//     untouched if not -- real device behaviour when a handle got closed out
//     from under a caller still mid-sequence.
//
// INSTRUMENTATION THIS SUITE ADDS ON TOP OF test_esp32_settings_nvs's STUB
// (the whole reason this is a separate file rather than a shared one): a
// monotonically increasing `generation` counter on the Preferences object,
// bumped on every REAL (non-reentrant) begin(). A write's or read's recorded
// generation says which open/close SPAN of the handle it happened in --
// generation 1 is init_flash()'s own load span (preferences.begin() near its
// top through to the first real preferences.end()); a later generation can
// only exist if that span was genuinely closed and reopened. This is what
// lets the test suite assert "no write happened while the load's own handle
// was still open" as a structural fact instead of inferring it from field
// values, which the fix under test (D1-04 W3) can make correct anyway once
// the load walk itself completes before anything else runs -- see the test
// file's own top-of-file comment for why that matters here specifically.
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
// The fake NVS itself: one process-wide store (mirrors the single global
// `Preferences preferences;` object every Preferences instance in the real
// firmware -- and every Preferences instance a test constructs here --
// ultimately opens the SAME "Credentials" namespace against).
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

    // One recorded get*()/put*() call: which key, and which open/close
    // generation of the Preferences handle it happened under (see the
    // file-top comment). generation 0 means "the handle was not started at
    // all" -- a get*() call that fell straight through to its default.
    struct Access
    {
        std::string key;
        int generation;
    };

    static FakeNvs &instance()
    {
        static FakeNvs nvs;
        return nvs;
    }

    // Clears the store AND every log -- call between test cases so one
    // case's writes/opens can never leak into the next.
    void reset()
    {
        entries_.clear();
        reads_.clear();
        writes_.clear();
        openCount_ = 0;
        closeCount_ = 0;
    }

    bool hasKey(const char *key) const { return entries_.count(key) != 0; }

    void recordOpen() { openCount_++; }
    void recordClose() { closeCount_++; }
    // Real (non-reentrant) begin() calls seen so far -- also the "current
    // generation" once at least one has happened.
    int openCount() const { return openCount_; }
    // Real (started-was-true) end() calls seen so far.
    int closeCount() const { return closeCount_; }

    void recordRead(const char *key, int generation) { reads_.push_back({key ? key : "", generation}); }
    void recordWrite(const char *key, int generation) { writes_.push_back({key ? key : "", generation}); }
    const std::vector<Access> &reads() const { return reads_; }
    const std::vector<Access> &writes() const { return writes_; }

    // Test-only direct write, bypassing Preferences entirely -- used to seed
    // a value a real boot would have stored on a previous save.
    void seedString(const char *key, const std::string &value)
    {
        Entry e;
        e.kind = Kind::Str;
        e.s = value;
        entries_[key] = e;
    }
    void seedInt(const char *key, int64_t value)
    {
        Entry e;
        e.kind = Kind::I64;
        e.i = value;
        entries_[key] = e;
    }
    // FLOAT and DOUBLE fields both round-trip through Preferences' own
    // getFloat()/putFloat() and getDouble()/putDouble() as Kind::F64 here
    // (see getScalar/putScalar below) -- one seed helper covers both.
    void seedDouble(const char *key, double value)
    {
        Entry e;
        e.kind = Kind::F64;
        e.d = value;
        entries_[key] = e;
    }

    std::map<std::string, Entry> &entries() { return entries_; }

private:
    FakeNvs() = default;
    std::map<std::string, Entry> entries_;
    std::vector<Access> reads_;
    std::vector<Access> writes_;
    int openCount_ = 0;
    int closeCount_ = 0;
};

// ---------------------------------------------------------------------------
// Preferences -- the class esp32_flash.cpp actually calls.
// ---------------------------------------------------------------------------
class Preferences
{
public:
    Preferences() {}

    // Test-only: puts this object back to its just-constructed state,
    // INCLUDING the generation counter. Needed because the product code
    // keeps exactly one global `Preferences preferences;` for the whole
    // process (real firmware behaviour, reproduced here on purpose) -- so
    // without this, `generation()` keeps counting up across every test case
    // Unity runs in this one binary, and "generation 1" would only ever mean
    // "the very first real open of the whole test run" instead of "this
    // test's own load span". Call from setUp().
    void resetForTest()
    {
        _started = false;
        _readOnly = false;
        generation_ = 0;
    }

    bool begin(const char *name, bool readOnly = false, const char *partition_label = nullptr)
    {
        (void)name;
        (void)partition_label;
        if (_started)
            return false; // real semantics: does NOT reopen, does not touch _readOnly
        _started = true;
        _readOnly = readOnly;
        generation_++;
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
        FakeNvs::instance().entries().clear();
        return true;
    }

    size_t freeEntries()
    {
        // Arbitrary large NVS-like capacity; no test in this suite asserts
        // on the exact number, only that the call does not crash.
        size_t used = FakeNvs::instance().entries().size();
        return used < 500 ? 500 - used : 0;
    }

    bool isKey(const char *key) { return _started && FakeNvs::instance().hasKey(key); }

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
        FakeNvs::instance().recordRead(key, gen);
        if (!_started)
            return defaultValue;
        auto &entries = FakeNvs::instance().entries();
        auto it = entries.find(key ? key : "");
        if (it == entries.end() || it->second.kind != FakeNvs::Kind::Str)
            return defaultValue;
        return String(it->second.s.c_str());
    }

    size_t putString(const char *key, String value)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordWrite(key, gen);
        if (!_started || _readOnly)
            return 0;
        FakeNvs::Entry e;
        e.kind = FakeNvs::Kind::Str;
        e.s = value.c_str();
        FakeNvs::instance().entries()[key ? key : ""] = e;
        return e.s.size();
    }

private:
    template <typename T, typename Field>
    T getScalar(const char *key, T defaultValue, FakeNvs::Kind kind, Field FakeNvs::Entry::*field)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordRead(key, gen);
        if (!_started)
            return defaultValue;
        auto &entries = FakeNvs::instance().entries();
        auto it = entries.find(key ? key : "");
        if (it == entries.end() || it->second.kind != kind)
            return defaultValue; // absent key, or type mismatch -- both return the caller's default, like real NVS
        return (T)(it->second.*field);
    }

    template <typename StoreT, typename Field>
    size_t putScalar(const char *key, StoreT value, FakeNvs::Kind kind, Field FakeNvs::Entry::*field, size_t reportedSize)
    {
        int gen = _started ? generation_ : 0;
        FakeNvs::instance().recordWrite(key, gen);
        if (!_started || _readOnly)
            return 0;
        FakeNvs::Entry e;
        e.kind = kind;
        e.*field = value;
        FakeNvs::instance().entries()[key ? key : ""] = e;
        return reportedSize;
    }

    bool _started = false;
    bool _readOnly = false;
    int generation_ = 0;
};
