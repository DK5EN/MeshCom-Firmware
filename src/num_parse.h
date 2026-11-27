// src/num_parse.h -- strict fixed-point number parsing for the web setup / function handlers
// (FORMAL-02 wave 7a, docs/web-server-seams-20261127.md section 6.6, behaviour change B5).
//
// Header-only, no String, no Arduino, no floating point, no libc number parser (no sscanf, atoi,
// strtol), so the file is host-testable and fit for a proof. Not wired into production yet.
//
//   bool mcParseInt (s, n, lo, hi, &out)          whole number in [lo, hi]
//   bool mcParseDec3(s, n, lo_milli, hi_milli, &m) decimal with 1..3 fraction digits, result in
//                                                 thousandths ("-0,5" -> -500, "1.25" -> 1250)
//   void mcTrimSpan(&s, &n)                       drop ASCII space and tab at both ends of (s, n)
//   size_t mcFormatDec3(milli, out, cap)          thousandths -> "-0.500" / "12.050" (sign included)
//
// Rules (both functions; on refusal *out is not written):
//   - the input is (s, n), not NUL terminated; the WHOLE input must be consumed. A NUL byte inside
//     is a non-digit and refuses.
//   - empty input (n == 0, s may be null) refuses.
//   - NO whitespace, leading or trailing: the helpers are strict. The web form does NOT trim: setvalue()
//     (web/mc.js:99) sends encodeURIComponent(value) as typed (the trim() at ~:340 is the pin-assign
//     input only). So the WIRING trims first: mcTrimSpan() drops ASCII space and tab at both ends, then
//     mcParseInt / mcParseDec3. Newline, CR and the other whitespace are not trimmed and refuse (B5).
//   - sign: an optional '-' only. '+' refuses (no GUI path sends it; one form less to prove).
//     A lone "-" refuses; "-0" and "-0,0" are 0 (the sign of zero is lost).
//   - digits: leading zeros are fine ("007" = 7) and never overflow, whatever the length.
//   - mcParseDec3 separator: ',' or '.', at most one. At least one digit before it and 1..3 digits
//     after it: ".5", "5." and a lone "." refuse; "1.50" is 1500; "1.2345" (4 digits) refuses (B5).
//     An integer like "5" is accepted by mcParseDec3 too (= 5000).
//   - range: lo <= value <= hi, inclusive; lo > hi refuses everything.
//   - overflow is impossible for any length: the accumulator is compared against constants
//     BEFORE the multiply (no division, no libc); results are exact 32-bit values.
//
// Formatting: mcFormatDec3 writes the sign itself. Do NOT format with "%d.%03d" of (m / 1000, m % 1000):
// it loses the sign for -999..-1 (-500 prints "0.500"). Use it where a thousandths value goes back
// into a console line (windcal, tempoff in/out). No floating point, division only by constants.
//
// Per site (today's parse -> target -> helper), for the wiring wave (all: trim first, then parse):
//   web_setup.cpp:563  windpin       sscanf("%d %c") ==1                 -> 0..48            mcParseInt  (--windpin range, command_functions.cpp:3038)
//   web_setup.cpp:578  windcal       cmdDecimalComma + sscanf("%f")      -> 100..100000 m    mcParseDec3 (0.1..100 km/h per Hz; mcFormatDec3 to --windcal)
//   web_setup.cpp:649  storetime     paramValue.toInt() compare          -> 1..168           mcParseInt
//   web_setup.cpp:657  storeslots    paramValue.toInt() compare          -> 1..50            mcParseInt  (MSGSTORE_SLOTS_MAX)
//   web_setup.cpp:764  gpio<N>       atoi(num.c_str()), len <= 3         -> 0..999           mcParseInt  (gpioCtlCommand refuses unknown pins)
//   web_setup.cpp:852  tempoffsetindoor   cmdDecimalComma + sscanf("%f") -> -50000..50000 m  mcParseDec3 (-50..50 C; mcFormatDec3 to --tempoff in)
//   web_setup.cpp:868  tempoffsetoutdoor  cmdDecimalComma + sscanf("%f") -> -50000..50000 m  mcParseDec3 (mcFormatDec3 to --tempoff out)
//   web_functions.cpp:5067 mboxpurge=<n>  msgstorePurge(toInt())       -> 0..MSGSTORE_SLOTS_MAX-1  mcParseInt ("all" stays an exact match first)
//   web_functions.cpp:5073 mboxdeliver=<n> msgstoreDeliverNow(toInt()) -> 0..MSGSTORE_SLOTS_MAX-1  mcParseInt
// Existing helpers: the console family cmdArgInt/cmdArgDbl/cmdDecimalComma (command_setters.h) is lax
// by design (mirrors scanf) and stays; src/mc_text.h:75 and settings_store.cpp:272 wrap strtol/strtoll
// for other contracts (not strict, not fixed point), so nothing there fits and nothing is reused.
#pragma once

#include <stddef.h>
#include <stdint.h>

inline bool mcParseInt(const char *s, size_t n, int32_t lo, int32_t hi, int32_t *out)
{
    if (s == 0 || out == 0 || n == 0 || lo > hi)
        return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-')
    {
        neg = true;
        i = 1;
    }
    if (i >= n)
        return false; // lone "-"
    // |INT32_MIN| = 2147483648 = 214748364 * 10 + 8; the largest positive is ...7.
    const uint32_t lastDigitMax = neg ? 8u : 7u;
    uint32_t acc = 0;
    for (; i < n; i++)
    {
        const char c = s[i];
        if (c < '0' || c > '9')
            return false;
        const uint32_t d = (uint32_t)(c - '0');
        if (acc > 214748364u || (acc == 214748364u && d > lastDigitMax))
            return false; // would not fit into int32
        acc = acc * 10u + d;
    }
    // acc <= 2147483648 here; build the signed value without overflow
    const int64_t v = neg ? -(int64_t)acc : (int64_t)acc;
    if (v < (int64_t)lo || v > (int64_t)hi)
        return false;
    *out = (int32_t)v;
    return true;
}

inline bool mcParseDec3(const char *s, size_t n, int32_t lo_milli, int32_t hi_milli, int32_t *milli)
{
    if (s == 0 || milli == 0 || n == 0 || lo_milli > hi_milli)
        return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-')
    {
        neg = true;
        i = 1;
    }
    // integer part: at least one digit; capped at 2147483 so that ip * 1000 + 999 fits int32 range checks
    int64_t ip = 0;
    size_t nInt = 0;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, nInt++)
    {
        if (ip > 214748) // ip * 10 + 9 would pass 2147489: already beyond the 2147483 cap
            return false;
        ip = ip * 10 + (s[i] - '0');
    }
    if (nInt == 0 || ip > 2147483)
        return false;
    int64_t frac = 0;
    if (i < n)
    {
        if (s[i] != ',' && s[i] != '.')
            return false;
        i++;
        int64_t scale = 100; // weight of the next fraction digit, in thousandths
        size_t nFrac = 0;
        for (; i < n; i++, nFrac++)
        {
            const char c = s[i];
            if (c < '0' || c > '9' || nFrac >= 3)
                return false;
            frac += (int64_t)(c - '0') * scale;
            scale = (nFrac == 0) ? 10 : 1; // 100 -> 10 -> 1; the 3rd digit's next scale is unused
        }
        if (nFrac == 0)
            return false; // "5." or a lone separator
    }
    const int64_t mag = ip * 1000 + frac; // <= 2147483999, fits int64
    const int64_t v = neg ? -mag : mag;
    if (v < (int64_t)lo_milli || v > (int64_t)hi_milli)
        return false;
    *milli = (int32_t)v;
    return true;
}

// Trims ASCII space (0x20) and tab (0x09) at both ends; (s, n) is narrowed in place. Bounded loops.
inline void mcTrimSpan(const char **s, size_t *n)
{
    if (s == 0 || n == 0 || *s == 0)
        return;
    const char *p = *s;
    size_t len = *n;
    while (len > 0 && (p[0] == ' ' || p[0] == '\t'))
    {
        p++;
        len--;
    }
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
        len--;
    *s = p;
    *n = len;
}

// Thousandths -> text "[-]I.FFF" (three fraction digits always, "." as separator), NUL terminated.
// Returns the length without the NUL, or 0 when it does not fit (cap < length + 1; out[0] = 0 if cap > 0).
// The longest text is "-2147483.648" (12 characters, cap 13). No floating point; INT32_MIN is exact.
inline size_t mcFormatDec3(int32_t milli, char *out, size_t cap)
{
    if (out == 0 || cap == 0)
        return 0;
    out[0] = 0;
    const bool neg = milli < 0;
    const uint32_t mag = neg ? (0u - (uint32_t)milli) : (uint32_t)milli; // exact for INT32_MIN
    uint32_t ip = mag / 1000u;
    const uint32_t frac = mag % 1000u;
    char tmp[16]; // integer digits, reversed: at most 7 (2147483)
    size_t ni = 0;
    do
    {
        tmp[ni++] = (char)('0' + (ip % 10u));
        ip /= 10u;
    } while (ip != 0 && ni < sizeof(tmp));
    const size_t len = (neg ? 1u : 0u) + ni + 1u + 3u;
    if (len + 1 > cap)
        return 0;
    size_t o = 0;
    if (neg)
        out[o++] = '-';
    while (ni > 0)
        out[o++] = tmp[--ni];
    out[o++] = '.';
    out[o++] = (char)('0' + frac / 100u);
    out[o++] = (char)('0' + (frac / 10u) % 10u);
    out[o++] = (char)('0' + frac % 10u);
    out[o] = 0;
    return o;
}
