// Fake in-memory NVS for the native ESP32 settings suite (test_esp32_settings_nvs).
//
// Shadows the real Arduino-ESP32 `Preferences.h` (angle-bracket include,
// ordinary -I shadowing -- src/esp32/esp32_flash.cpp has no same-directory
// copy to fight, unlike the nRF52 suite's WisBlock-API.h situation). Models
// the subset of the real class esp32_flash.cpp actually calls:
//
//   begin()/end()/clear()/freeEntries()
//   getChar/putChar, getInt/putInt, getUInt/putUInt,
//   getFloat/putFloat, getDouble/putDouble, getBool/putBool,
//   getString/putString
//
// FIDELITY TO THE REAL LIBRARY (verified against
// ~/.platformio/packages/framework-arduinoespressif32/libraries/Preferences/src/Preferences.cpp
// while writing this stub):
//
//   - Preferences::begin() returns false and does nothing if this object is
//     already started (re-entrant no-op) -- modelled via `_started`. The
//     namespace name passed to a no-op begin() is ignored too, exactly like
//     the real class: `ns_` is only set on a REAL open.
//   - Preferences::end() unconditionally closes if started.
//   - Every get*() checks `_started` FIRST and returns the caller's default
//     untouched if not -- real device behaviour when a handle got closed out
//     from under a caller still mid-sequence. `_started` gating below is
//     what mattered.
//   - NVS is TYPE-STRICT per key: writing a key as one type and reading it
//     back as another returns the caller's default, the same as a real
//     ESP_ERR_NVS_TYPE_MISMATCH (nvs_get_i32() et al. leave the out-param at
//     the caller's supplied default on any error, including a type
//     mismatch -- confirmed in Preferences.cpp's own get*() bodies, e.g.
//     "int32_t value = defaultValue; ... if(err){ log_v(...); } return
//     value;").
//   - clear()/put*() are no-ops (return false / 0) when not started or
//     opened read-only, matching the real class's own early-return guards.
//
// MULTIPLE NAMESPACES, MULTIPLE INSTANCES (D1-04 W3 step 4): the store is
// keyed by NAMESPACE, not flat -- `begin(name, ...)` selects which
// namespace's key/value map this Preferences object reads and writes for the
// rest of its open span, matching real NVS (a key in one namespace and a
// same-named key in another are independent entries). This suite's own build
// (-D MC_SAFEBOOT) never exercises more than the one "Credentials" namespace
// -- counters_store.h's countersLoad()/countersSave() are compiled out under
// MC_SAFEBOOT in esp32_flash.cpp, see that file's own comment -- but this
// stub models namespaces anyway to stay a faithful (and reusable) twin of
// test_esp32_flash_lifecycle's own copy, which does exercise them.
//
// TEST-ONLY SURFACE (no counterpart in the real Preferences API, static so
// state survives across the several Preferences objects a test may
// construct, exactly like the ONE global `preferences` object in the real
// firmware): `FakeNvs::instance()` for direct store manipulation (seeding an
// unknown key, injecting an oversized string, reading back what a save
// wrote) and `FakeNvs::instance().reset()` to clear all state AND the
// observed-key log between test cases. Every namespace-taking method
// defaults to "Credentials" so every call site written before namespaces
// existed keeps compiling unchanged.
#pragma once

#ifndef NATIVE_BUILD
#error "test_esp32_settings_nvs/stubs/Preferences.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h> // String (test/support/Arduino.h)

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
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

    static FakeNvs &instance()
    {
        static FakeNvs nvs;
        return nvs;
    }

    // Clears every namespace's store AND the observed-key log -- call
    // between test cases so one case's writes can never leak into the next.
    void reset()
    {
        namespaces_.clear();
        seenKeys_.clear();
    }

    void recordSeen(const char *key)
    {
        if (key)
            seenKeys_.push_back(key);
    }

    const std::vector<std::string> &seenKeys() const { return seenKeys_; }

    bool hasKey(const char *key, const char *ns = "Credentials") const
    {
        auto nit = namespaces_.find(ns ? ns : "");
        if (nit == namespaces_.end())
            return false;
        return nit->second.count(key ? key : "") != 0;
    }

    // Test-only direct write, bypassing Preferences entirely -- used to seed
    // an "unknown key" (downgrade-path test) or an oversized string a
    // corrupted/foreign store might contain (truncation-safety test).
    // Namespace defaults to "Credentials" so every pre-existing call site
    // keeps working unchanged.
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

    // Namespace defaults to "Credentials" for the same back-compat reason as
    // the seed helpers above.
    std::map<std::string, Entry> &entries(const char *ns = "Credentials") { return namespaces_[ns ? ns : ""]; }

private:
    FakeNvs() = default;
    std::map<std::string, std::map<std::string, Entry>> namespaces_;
    std::vector<std::string> seenKeys_;
};

// ---------------------------------------------------------------------------
// Preferences -- the class esp32_flash.cpp actually calls.
// ---------------------------------------------------------------------------
class Preferences
{
public:
    Preferences() {}

    bool begin(const char *name, bool readOnly = false, const char *partition_label = nullptr)
    {
        (void)partition_label;
        if (_started)
            return false; // real semantics: does NOT reopen, does not touch _readOnly or the namespace
        ns_ = name ? name : "";
        _started = true;
        _readOnly = readOnly;
        return true;
    }

    void end()
    {
        if (!_started)
            return;
        _started = false;
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
        FakeNvs::instance().recordSeen(key);
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
        FakeNvs::instance().recordSeen(key);
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
        FakeNvs::instance().recordSeen(key);
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
        FakeNvs::instance().recordSeen(key);
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
};
