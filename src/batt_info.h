// batt_info.h -- what --info prints after "BATTERY PIN n".
// Header-only and pure, natively testable (test/test_batt_info).
//
// fBattFaktor is only used by the USE_NEW_BATT path (batt_functions.cpp). The legacy path
// (batt_function_old.cpp) measures with the compile-time ADC_MULTIPLIER and never reads it,
// so printing it there showed a misleading "factor 0.0000".
#pragma once

struct BattInfo {
    bool show;          // false: print no number at all
    const char *label;  // "factor" (settable via --batt factor) or "multiplier" (compile time)
    float value;
};

inline BattInfo battInfoDecide(bool newPath, bool hasMultiplier, float faktor, float multiplier)
{
    if (newPath)
        return BattInfo{true, "factor", faktor};
    if (hasMultiplier)
        return BattInfo{true, "multiplier", multiplier};
    return BattInfo{false, "", 0.0f};
}
