// BLE session state and link statistics (BLC-01, issue #1191).
//
// Header-only, no Arduino includes, so the host test (test_ble_session) can
// drive it. Shared by the ESP32 (NimBLE) and nRF52 (Bluefruit) BLE callbacks.
//
// Session reset: everything that belongs to ONE phone connection (app-layer
// hello done, config burst state, ackinfo) must be cleared inside the
// connect/disconnect callbacks. A reset that only runs on an edge observed by
// the main loop is racy: disconnect + reconnect between two loop passes shows
// the loop no edge at all and the new central inherits isPhoneReady == 1.
//
// Statistics: each platform defines `BleStats g_bleStats;` exactly once.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <atomic>

// Same value as src/configuration_global.h (an identical redefinition is legal).
#ifndef MAX_MSG_LEN_PHONE
#define MAX_MSG_LEN_PHONE 300
#endif

// Pointers to the platform's session flags (the flags live in different files).
// A null member is skipped.
struct BleSessionFlags
{
    int  *isPhoneReady;   // app-layer hello done
    bool *uartConnected;  // g_ble_uart_is_connected
    bool *ackInfo;        // bAckInfo
    bool *confPrepare;    // config_to_phone_prepare
    bool *conffinSent;    // conffin_sent
    bool *discRequested;  // ble_disconnect_requested (wrong-PIN hello of the previous central)
};

// keepPhone: the phone session is owned by AppLink (LAN); only the BLE link
// state (uartConnected, discRequested) is reset, the phone-session flags stay.
inline void bleSessionReset(const BleSessionFlags &f, bool keepPhone = false)
{
    if(keepPhone)
    {
        if(f.uartConnected) *f.uartConnected = false;
        if(f.discRequested) *f.discRequested = false;
        return;
    }
    if(f.isPhoneReady)  *f.isPhoneReady  = 0;
    if(f.uartConnected) *f.uartConnected = false;
    if(f.ackInfo)       *f.ackInfo       = false;
    if(f.confPrepare)   *f.confPrepare   = false;
    if(f.conffinSent)   *f.conffinSent   = false;
    if(f.discRequested) *f.discRequested = false;
}

// Counters since boot. dis counts every disconnect; to/rem/loc/fail/other
// classify it (to+rem+loc+fail+other == dis). adv counts advertising restarts
// done by the self-check (not the library's own restart).
struct BleStats
{
    uint32_t con, dis, to, rem, loc, fail, other, adv;
};

extern BleStats g_bleStats;

inline void bleStatsOnConnect(BleStats &s)    { s.con++; }
inline void bleStatsOnAdvRestart(BleStats &s) { s.adv++; }

// reason: NimBLE gives 0x200 + HCI code, Bluefruit the raw HCI code.
inline void bleStatsOnDisconnect(BleStats &s, int reason)
{
    s.dis++;
    switch(reason & 0xFF)
    {
        case 0x08: s.to++;    break;   // Connection Timeout (supervision)
        case 0x13: s.rem++;   break;   // Remote User Terminated
        case 0x16: s.loc++;   break;   // Local Host Terminated
        case 0x3E: s.fail++;  break;   // Failed to be Established
        default:   s.other++; break;
    }
}

inline const char *bleReasonText(int reason)
{
    switch(reason & 0xFF)
    {
        case 0x08: return "Supervision Timeout";
        case 0x13: return "Remote User Terminated";
        case 0x16: return "Local Host Terminated";
        case 0x22: return "LL Response Timeout";
        case 0x3B: return "Unacceptable Conn Params";
        case 0x3E: return "Failed to Establish";
        default:   return "other";
    }
}

// "BLE: con=<n> dis=<n> to=<n> rem=<n> loc=<n> fail=<n> adv=<n>"; needs 110
// bytes at the 32-bit maximum, use a buffer of 128. Returns snprintf's value.
inline int bleStatsFormat(const BleStats &s, char *buf, size_t n)
{
    return snprintf(buf, n, "BLE: con=%lu dis=%lu to=%lu rem=%lu loc=%lu fail=%lu adv=%lu",
                    (unsigned long)s.con, (unsigned long)s.dis, (unsigned long)s.to,
                    (unsigned long)s.rem, (unsigned long)s.loc, (unsigned long)s.fail,
                    (unsigned long)s.adv);
}

// ---------------------------------------------------------------------------
// Phone frame queue (bleQueue): item layout, session generation, drop counters.
// Shared by both platforms (BLE-QDROP-01, BLE-STALE-01; specs/tla/bletask.tla).
//
// The producer (NimBLE host task / Bluefruit rx callback) stamps each item with the session
// generation; the connect/disconnect callbacks bump it. The loop applies an item only if its
// stamp is still current: an item queued (or already dequeued) during connection k is dropped in
// k+1. A queue reset in the callbacks alone does not close this (item in flight).
struct BleQueueItem
{
    uint8_t  data[MAX_MSG_LEN_PHONE];
    size_t   length;
    uint32_t gen;   // session generation at enqueue time
};

// Written by the BLE callbacks, read by the loop task: atomics, no Serial in the callbacks.
// Each platform defines `BleQueueCounters g_bleQ;` exactly once.
struct BleQueueCounters
{
    std::atomic<uint32_t> gen;       // session generation
    std::atomic<uint32_t> dropped;   // frames lost to a full queue (BLE-QDROP-01)
    std::atomic<uint32_t> stale;     // frames of an earlier connection, dropped by the loop (BLE-STALE-01)
    uint32_t droppedLogged;          // loop task only: last value already logged
    uint32_t staleLogged;
};

extern BleQueueCounters g_bleQ;

// Connect and disconnect callbacks (and every other session reset).
inline void bleQueueNewSession(BleQueueCounters &c) { c.gen.fetch_add(1, std::memory_order_relaxed); }

// Producer: stamp before xQueueSend.
inline void bleItemStamp(BleQueueItem &it, const BleQueueCounters &c)
{
    it.gen = c.gen.load(std::memory_order_relaxed);
}

inline bool bleItemIsCurrent(const BleQueueItem &it, uint32_t gen) { return it.gen == gen; }

// Producer: result of xQueueSend (true = queued). A failed send is a lost frame, count it.
inline void bleQueueOnSend(BleQueueCounters &c, bool queued)
{
    if(!queued) c.dropped.fetch_add(1, std::memory_order_relaxed);
}

// Loop, right before readPhoneCommand: true = apply; a stale item is counted and must be skipped.
inline bool bleQueueAccept(BleQueueCounters &c, const BleQueueItem &it)
{
    if(bleItemIsCurrent(it, c.gen.load(std::memory_order_relaxed))) return true;
    c.stale.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// Loop, once per pass: counts since the last call (0 = nothing to log).
inline uint32_t bleQueueTakeDropped(BleQueueCounters &c)
{
    uint32_t n = c.dropped.load(std::memory_order_relaxed);
    uint32_t d = n - c.droppedLogged;
    c.droppedLogged = n;
    return d;
}

inline uint32_t bleQueueTakeStale(BleQueueCounters &c)
{
    uint32_t n = c.stale.load(std::memory_order_relaxed);
    uint32_t d = n - c.staleLogged;
    c.staleLogged = n;
    return d;
}

// "BLE: qdrop=<n> qstale=<n>" for the --bledebug stat line.
inline int bleQueueFormat(const BleQueueCounters &c, char *buf, size_t n)
{
    return snprintf(buf, n, "BLE: qdrop=%lu qstale=%lu",
                    (unsigned long)c.dropped.load(std::memory_order_relaxed),
                    (unsigned long)c.stale.load(std::memory_order_relaxed));
}
