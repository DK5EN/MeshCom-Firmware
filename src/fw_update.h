// fw_update.h -- pure logic of the firmware auto update (#1187, AU)
//
// Header-only, Arduino-free, no printf, no malloc. Everything here is
// host-testable (env:native_fw_update). The network, flash and NVS code
// that drives it lives elsewhere; this file only decides.
//
//   version / tag   fwParseTag(), fwCompare()           (AU-D10)
//   channel         fwChannelRepo()                     (AU-D2)
//   staging layout  fwStageLayout()                     (AU-D11)
//   stage record    fwRecordEncode(), fwRecordDecode()  (NVS "fwstage")
//   policy / timer  fwTimerInit(), fwTick(), fwCheckDone(),
//                   fwAttemptAllowed(), fwAttemptFailed()  (AU-D1..D5)
//
// All millisecond arithmetic is wrap-safe (uint32_t millis(), signed
// difference), valid for spans below 2^31 ms (24.8 days).
//
// The running version is MC_BUILD_TAG if it is non-empty, otherwise
// SOURCE_VERSION + SOURCE_VERSION_SUB; the caller builds that string and
// hands it to fwParseTag().

#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// version / tag
// ---------------------------------------------------------------------------

struct FwVersion
{
    uint8_t major;
    uint8_t minor;
    char letter;     // 'a'..'z'
    uint8_t month;   // 1..12, only if hasDate
    uint8_t day;     // 1..31, only if hasDate
    bool hasDate;
    bool valid;
};

namespace fw_update_detail
{
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads 1..3 decimal digits. Returns false if there is none or more than 3.
inline bool readNum(const char *&p, uint32_t &v)
{
    uint8_t n = 0;
    v = 0;
    while (isDigit(*p))
    {
        if (++n > 3)
            return false;
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    return n > 0;
}

// Days in each month, February counted with 29 (a leap-day tag is valid).
inline uint8_t monthDays(uint8_t m)
{
    static const uint8_t k[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return k[m - 1];
}

// Day of the year in a 365-day year: Jan 1 = 0 ... Dec 31 = 364. Feb 29
// folds onto Feb 28, so the year has no gap and the modulo arithmetic in
// fwCompare() stays closed.
inline int dayOfYear(uint8_t month, uint8_t day)
{
    static const uint16_t before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    if (month == 2 && day > 28)
        day = 28;
    return (int)before[month - 1] + (int)day - 1;
}

inline bool strEqN(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return false;
        if (a[i] == '\0')
            return true;
    }
    return true;
}

inline size_t strLenN(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i] != '\0')
        i++;
    return i;
}
} // namespace fw_update_detail

// Parses "v4.40a", "v4.40a.10.02" (the leading 'v' is optional). Strict:
// nothing may follow the tag. Returns false (out.valid = false) otherwise.
inline bool fwParseTag(const char *tag, FwVersion &out)
{
    using namespace fw_update_detail;
    out.major = 0;
    out.minor = 0;
    out.letter = 0;
    out.month = 0;
    out.day = 0;
    out.hasDate = false;
    out.valid = false;
    if (tag == nullptr)
        return false;

    const char *p = tag;
    if (*p == 'v' || *p == 'V')
        p++;

    uint32_t major, minor;
    if (!readNum(p, major) || major > 255 || *p != '.')
        return false;
    p++;
    if (!readNum(p, minor) || minor > 255)
        return false;
    if (*p < 'a' || *p > 'z')
        return false;
    char letter = *p++;

    uint8_t month = 0, day = 0;
    bool hasDate = false;
    if (*p == '.')
    {
        p++;
        uint32_t m, d;
        if (!readNum(p, m) || *p != '.')
            return false;
        p++;
        if (!readNum(p, d))
            return false;
        if (m < 1 || m > 12 || d < 1 || d > monthDays((uint8_t)m))
            return false;
        month = (uint8_t)m;
        day = (uint8_t)d;
        hasDate = true;
    }
    if (*p != '\0')
        return false;

    out.major = (uint8_t)major;
    out.minor = (uint8_t)minor;
    out.letter = letter;
    out.month = month;
    out.day = day;
    out.hasDate = hasDate;
    out.valid = true;
    return true;
}

// > 0 if cand is newer than running, 0 same or not comparable, < 0 older.
// (major, minor, letter) decide first. On a tie the date decides: cand is
// newer iff its MM.DD lies 1..182 days after running's MM.DD modulo the
// year (365-day table, Feb 29 = Feb 28), older if 183..364 days after
// (= 1..182 before). A tag without a date is the OLDEST of its version.
inline int fwCompare(const FwVersion &running, const FwVersion &cand)
{
    if (!running.valid || !cand.valid)
        return 0;
    if (cand.major != running.major)
        return cand.major > running.major ? 1 : -1;
    if (cand.minor != running.minor)
        return cand.minor > running.minor ? 1 : -1;
    if (cand.letter != running.letter)
        return cand.letter > running.letter ? 1 : -1;

    if (!running.hasDate && !cand.hasDate)
        return 0;
    if (cand.hasDate && !running.hasDate)
        return 1;
    if (!cand.hasDate && running.hasDate)
        return -1;

    int r = fw_update_detail::dayOfYear(running.month, running.day);
    int c = fw_update_detail::dayOfYear(cand.month, cand.day);
    int d = (c - r + 365) % 365;
    if (d == 0)
        return 0;
    return d <= 182 ? 1 : -1;
}

// ---------------------------------------------------------------------------
// channel
// ---------------------------------------------------------------------------

enum FwChannel : uint8_t
{
    FW_CH_PROD = 0,
    FW_CH_DEV = 1
};

// Unknown values fall back to prod.
inline const char *fwChannelRepo(FwChannel c)
{
    return c == FW_CH_DEV ? "DK5EN/MeshCom-Firmware" : "icssw-org/MeshCom-Firmware";
}

// ---------------------------------------------------------------------------
// staging layout (AU-D11)
// ---------------------------------------------------------------------------

enum FwRefuse : uint8_t
{
    FW_OK = 0,
    FW_REF_SIZE,    // zLen == 0 or zLen > slotSize
    FW_REF_OVERLAP, // the running image reaches into the stage area
    FW_REF_SMALL,   // stage offset below 64 KB (slot hardly bigger than the asset)
    FW_REF_INFLATE  // inflated image length is 0 or would reach into the stage area
};

#define FW_STAGE_ALIGN 0x10000u

// The compressed asset is staged at the END of the ota_0 slot:
//   stageOff = (slotSize - zLen) rounded DOWN to 64 KB.
// iLen is the expected INFLATED image length (the raw .bin asset size of the
// same release). It must fit below stageOff, so Safeboot's inflate into the
// head of ota_0 can never reach the stage area: iLen == 0 or iLen > stageOff
// is refused with FW_REF_INFLATE.
// stageOff is set on FW_OK, FW_REF_OVERLAP and FW_REF_INFLATE (informational);
// it is 0 for the other refusals.
inline FwRefuse fwStageLayout(uint32_t slotSize, uint32_t runningLen, uint32_t zLen,
                              uint32_t iLen, uint32_t &stageOff)
{
    stageOff = 0;
    if (zLen == 0 || zLen > slotSize)
        return FW_REF_SIZE;
    uint32_t off = (slotSize - zLen) & ~(FW_STAGE_ALIGN - 1u);
    if (off < FW_STAGE_ALIGN)
        return FW_REF_SMALL;
    stageOff = off;
    if (runningLen > off)
        return FW_REF_OVERLAP;
    if (iLen == 0 || iLen > off)
        return FW_REF_INFLATE;
    return FW_OK;
}

// ---------------------------------------------------------------------------
// stage record (NVS "fwstage", written by the app, consumed by Safeboot)
// ---------------------------------------------------------------------------

struct FwStageRecord
{
    uint32_t magic;
    char tag[24];
    char env[32];
    uint32_t off;
    uint32_t zlen;  // compressed asset length (.bin.zz)
    uint32_t ilen;  // expected inflated length (raw .bin asset size)
    uint32_t crc32;
    uint8_t sha256[32];
    uint32_t ts;
};

#define FW_STAGE_MAGIC 0x32535746u // 'FWS2' little-endian
#define FW_STAGE_ENC_LEN 112u      // 4 + 24 + 32 + 4 + 4 + 4 + 4 + 32 + 4

namespace fw_update_detail
{
inline void putU32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
inline uint32_t getU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
// Copies a string field: at most n-1 characters, the rest zero, always
// terminated (so the encoding is deterministic and decodable).
inline void putStr(uint8_t *p, const char *s, size_t n)
{
    size_t len = strLenN(s, n - 1);
    for (size_t i = 0; i < n; i++)
        p[i] = i < len ? (uint8_t)s[i] : 0;
}
inline bool getStr(const uint8_t *p, char *s, size_t n)
{
    bool term = false;
    for (size_t i = 0; i < n; i++)
    {
        s[i] = (char)p[i];
        if (p[i] == 0)
            term = true;
    }
    return term;
}
} // namespace fw_update_detail

// Fixed little-endian layout, FW_STAGE_ENC_LEN bytes. Returns the length,
// 0 if the buffer is too small. Strings are truncated to fit with a NUL.
inline size_t fwRecordEncode(const FwStageRecord &r, uint8_t *buf, size_t n)
{
    using namespace fw_update_detail;
    if (buf == nullptr || n < FW_STAGE_ENC_LEN)
        return 0;
    uint8_t *p = buf;
    putU32(p, r.magic);      p += 4;
    putStr(p, r.tag, 24);    p += 24;
    putStr(p, r.env, 32);    p += 32;
    putU32(p, r.off);        p += 4;
    putU32(p, r.zlen);       p += 4;
    putU32(p, r.ilen);       p += 4;
    putU32(p, r.crc32);      p += 4;
    for (int i = 0; i < 32; i++)
        p[i] = r.sha256[i];
    p += 32;
    putU32(p, r.ts);         p += 4;
    return FW_STAGE_ENC_LEN;
}

// False on a wrong size, a wrong magic or an unterminated string; out is
// untouched in that case.
inline bool fwRecordDecode(const uint8_t *buf, size_t n, FwStageRecord &out)
{
    using namespace fw_update_detail;
    if (buf == nullptr || n != FW_STAGE_ENC_LEN)
        return false;
    if (getU32(buf) != FW_STAGE_MAGIC)
        return false;
    FwStageRecord r;
    const uint8_t *p = buf;
    r.magic = getU32(p);                      p += 4;
    if (!getStr(p, r.tag, 24))                return false;
    p += 24;
    if (!getStr(p, r.env, 32))                return false;
    p += 32;
    r.off = getU32(p);                        p += 4;
    r.zlen = getU32(p);                       p += 4;
    r.ilen = getU32(p);                       p += 4;
    r.crc32 = getU32(p);                      p += 4;
    for (int i = 0; i < 32; i++)
        r.sha256[i] = p[i];
    p += 32;
    r.ts = getU32(p);
    out = r;
    return true;
}

// ---------------------------------------------------------------------------
// install decision (reinstall-loop guard)
// ---------------------------------------------------------------------------

// True only if the candidate is newer than the running image AND its tag
// differs from lastInstalledTag, the tag the caller persisted when it last
// staged and handed over an image (null or empty = nothing installed yet).
//
// Why the second condition: a running image built without MC_BUILD_TAG
// reports SOURCE_VERSION+SUB, e.g. "4.40a", which is UNDATED, so any dated
// release of the same letter ("v4.40a.10.02") compares as newer. If that
// release itself was built without a tag, it would again report "4.40a"
// after the update, see the same release as newer and reinstall it forever.
// The lastInstalled guard stops that loop after one install.
inline bool fwShouldInstall(const FwVersion &running, const FwVersion &cand, const char *candTag,
                            const char *lastInstalledTag)
{
    using namespace fw_update_detail;
    if (candTag == nullptr || candTag[0] == '\0')
        return false;
    if (fwCompare(running, cand) <= 0)
        return false;
    // compared over the 23 characters the stage record keeps
    if (lastInstalledTag != nullptr && strEqN(candTag, lastInstalledTag, 23))
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// policy / timer
// ---------------------------------------------------------------------------

#define FW_FIRST_CHECK_MS (10u * 60u * 1000u)
#define FW_CHECK_BASE_S (22u * 3600u)     // 24 h - 2 h
#define FW_CHECK_SPREAD_S (4u * 3600u)    // jitter span: 22 h .. 26 h
#define FW_WINDOW_START_MIN 180u          // 03:00
#define FW_WINDOW_END_MIN 300u            // 05:00 (exclusive)
#define FW_BATT_FLOOR_MV 3600u
#define FW_MAX_ATTEMPTS 3u
#define FW_ATTEMPT_SPACING_MS (6u * 3600u * 1000u)

struct FwPolicyIn
{
    uint8_t mode; // 0 off, 1 notify, 2 auto
    bool wifiUp;
    bool timeValid;
    bool phoneConnected;
    bool onBattery;
    uint16_t battMv;
    uint16_t localMinuteOfDay; // 0..1439, from TZ-01 or UTC
};

enum FwAction : uint8_t
{
    FW_NONE = 0,
    FW_CHECK,
    FW_HANDOVER
};

struct FwTimer
{
    uint32_t nextCheckMs;
    bool firstDone;
    uint32_t jitterSeed;
    uint8_t attempts;
    char attemptTag[24];
    uint32_t nextAttemptMs;
};

namespace fw_update_detail
{
// a is reached or passed by b (wrap-safe).
inline bool reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

inline uint32_t mixSeed(uint32_t x)
{
    // murmur3 finalizer: adjacent seeds (node ids) give unrelated jitter.
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x ? x : 0x9e3779b9u;
}

inline uint32_t nextSeed(uint32_t x)
{
    x ^= x << 13; // xorshift32
    x ^= x >> 17;
    x ^= x << 5;
    return x ? x : 0x9e3779b9u;
}
} // namespace fw_update_detail

// Call when WiFi first comes up. The first check is due 10 min later.
inline void fwTimerInit(FwTimer &t, uint32_t nowMs, uint32_t seed)
{
    t.nextCheckMs = nowMs + FW_FIRST_CHECK_MS;
    t.firstDone = false;
    t.jitterSeed = fw_update_detail::mixSeed(seed);
    t.attempts = 0;
    for (size_t i = 0; i < sizeof(t.attemptTag); i++)
        t.attemptTag[i] = 0;
    t.nextAttemptMs = 0;
}

// CHECK  : mode > 0, wifiUp, timeValid, due; never while a phone is connected.
// HANDOVER: mode == 2, stagedPending, valid time, local time in [03:00,05:00),
//           no phone, and (mains or battery >= 3.6 V). Needs no WiFi: the
//           apply runs offline in Safeboot. HANDOVER wins over CHECK.
//
// Wrap latch: a stored deadline that has passed would flip back to "not due"
// after 2^31 ms (24.8 days) if nobody looked at it. While a deadline is
// reached but cannot be acted on (mode off, no WiFi, no time, phone
// connected), fwTick moves it to nowMs on every call, so it keeps following
// the clock and stays reached. The same holds for the retry deadline.
// Call fwTick at least once per day (it is meant to run every loop pass).
inline FwAction fwTick(FwTimer &t, const FwPolicyIn &in, bool stagedPending, uint32_t nowMs)
{
    using fw_update_detail::reached;
    if (t.attempts > 0 && t.attempts < FW_MAX_ATTEMPTS && reached(nowMs, t.nextAttemptMs))
        t.nextAttemptMs = nowMs;
    if (reached(nowMs, t.nextCheckMs) &&
        !(in.mode > 0 && in.wifiUp && in.timeValid && !in.phoneConnected))
        t.nextCheckMs = nowMs;

    if (in.mode == 0 || in.phoneConnected)
        return FW_NONE;

    if (in.mode == 2 && stagedPending && in.timeValid &&
        in.localMinuteOfDay >= FW_WINDOW_START_MIN && in.localMinuteOfDay < FW_WINDOW_END_MIN &&
        (!in.onBattery || in.battMv >= FW_BATT_FLOOR_MV))
        return FW_HANDOVER;

    if (in.wifiUp && in.timeValid && fw_update_detail::reached(nowMs, t.nextCheckMs))
        return FW_CHECK;

    return FW_NONE;
}

// Schedules the next check: 24 h +- 2 h (22..26 h, inclusive), the jitter
// advances with every call.
inline void fwCheckDone(FwTimer &t, uint32_t nowMs)
{
    t.firstDone = true;
    t.jitterSeed = fw_update_detail::nextSeed(t.jitterSeed);
    uint32_t jitterS = t.jitterSeed % (FW_CHECK_SPREAD_S + 1u);
    t.nextCheckMs = nowMs + (FW_CHECK_BASE_S + jitterS) * 1000u;
}

// Download retry (AU-D5): at most 3 attempts per tag, 6 h apart, a new tag
// resets the count. fwAttemptAllowed() only refreshes the wrap latch of an
// already reached deadline; it never consumes an attempt.
inline bool fwAttemptAllowed(FwTimer &t, const char *tag, uint32_t nowMs)
{
    using namespace fw_update_detail;
    if (tag == nullptr)
        return false;
    if (!strEqN(tag, t.attemptTag, sizeof(t.attemptTag) - 1))
        return true; // new tag
    if (t.attempts == 0)
        return true;
    if (t.attempts >= FW_MAX_ATTEMPTS)
        return false;
    if (!reached(nowMs, t.nextAttemptMs))
        return false;
    t.nextAttemptMs = nowMs; // wrap latch, see fwTick()
    return true;
}

inline void fwAttemptFailed(FwTimer &t, const char *tag, uint32_t nowMs)
{
    using namespace fw_update_detail;
    if (tag == nullptr)
        return;
    if (!strEqN(tag, t.attemptTag, sizeof(t.attemptTag) - 1))
    {
        putStr((uint8_t *)t.attemptTag, tag, sizeof(t.attemptTag));
        t.attempts = 0;
    }
    if (t.attempts < 255)
        t.attempts++;
    t.nextAttemptMs = nowMs + FW_ATTEMPT_SPACING_MS;
}

#endif // FW_UPDATE_H
