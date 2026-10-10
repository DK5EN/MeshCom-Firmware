// rm_exec_read.cpp -- see rm_exec_ext.h.
//
// Read executors of the extended RM commands. This file only GATHERS node state into the input structs
// of rm_format.h; every reply text is built by the pure formatters there. Runs on the loop task (small
// stack: no big arrays, no heap, no Arduino String).
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configuration.h"
#include "loop_functions.h"
#include "loop_functions_extern.h"
#include "nbr_matrix.h"
#include "nbr_views.h"
#include "node_position.h"
#include "radio_units.h"
#include "rm_commands.h"
#include "rm_exec_ext.h"
#include "rm_format.h"
#include "rm_radio_in.h"
#include "txring_functions.h"
#include "uptime_min.h"
#if defined(ENABLE_MSGSTORE)
#include "msgstore_api.h"
#endif

// MHEARD window: RM_HEARD_WINDOW_MIN (rm_format.h, shared with the web page, decision D11).
#define RM_READ_MH_PAGE 8   // MHEARD rows per reply
#define RM_READ_VIA_MAX 4   // via calls of a route reply

static int fail(char *res, size_t n, const char *tok)
{
    snprintf(res, n, "err %s", tok);
    return -1;
}

// Wraps a formatter result: 0 chars = the formatter refused -> err failed.
static int done(char *res, size_t n, const char *body, size_t len)
{
    if (len == 0)
        return fail(res, n, "failed");
    snprintf(res, n, "ok %s", body);
    return 1;
}

static double signedLat()
{
    return nodeSignedLat(meshcom_settings.node_lat, meshcom_settings.node_lat_c);
}

static double signedLon()
{
    return nodeSignedLon(meshcom_settings.node_lon, meshcom_settings.node_lon_c);
}

// The node beacons its position iff it has a position: the sender drops every beacon with
// lat == 0 && lon == 0 (sendPosition(), src/loop_functions.cpp:4932). There is no other switch that
// turns the position beacon off (the timer in src/esp32/esp32_main.cpp:4033 fires unconditionally).
// 0/0 = own position unset (same rule as src/mh_phone.h); also the "distance is computable" test.
static bool ownPosKnown()
{
    return !(meshcom_settings.node_lat == 0.0 && meshcom_settings.node_lon == 0.0);
}

static int execRadio(char *res, size_t n)
{
    // node_freq / node_bw / node_cr are Hz and indices on the SX126x nRF52 path (RF-01): convert like the console.
    RmRadioIn in = rmRadioInFromStored((float)meshcom_settings.node_freq, (int)meshcom_settings.node_sf,
                                             (int)meshcom_settings.node_cr, (float)meshcom_settings.node_bw,
                                             (int)meshcom_settings.node_power, (int)TX_POWER_MAX, radioUnitsIndexed());
    in.pMin = (int)TX_POWER_MIN; // lets the managing node floor its TX power stepper (D9)
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtRadio(body, sizeof(body), in));
}

static int execPos(char *res, size_t n)
{
    if (!ownPosKnown())
        return fail(res, n, "hidden"); // D8: never reveal a position the node does not beacon
    RmPosIn in;
    in.lat = signedLat();
    in.lon = signedLon();
    in.alt = (int)meshcom_settings.node_alt;
    // GPS on: posinfo_fix (src/loop_functions_extern.h:444) says whether the receiver has a fix.
    in.src = bGPSON ? (posinfo_fix ? RM_POS_GPS : RM_POS_NOFIX) : RM_POS_SET;
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtPos(body, sizeof(body), in));
}

static int execSens(char *res, size_t n)
{
    // Presence comes from the driver flags, never from value != 0. Mapping follows the writers in
    // src/esp32/esp32_main.cpp:4362-4407 (BMX280/AHT20/SHT21), src/bme680.cpp:156 (BME680).
    // bBMP3ON: pressure only (the BMX280 path skips pressure when a BMP3 is found); its temperature
    // writer is not part of this mapping, so T is not claimed for it. bONEWIRE is claimed as T2 only
    // (a DHT on that path writes node_temp, a DS18B20 writes node_temp2: not distinguishable by flag).
    RmSensIn in;
    memset(&in, 0, sizeof(in));
    in.hasT = bBMPON || bBMEON || bBME680ON || bAHT20ON;
    in.hasH = bBMEON || bBME680ON || bAHT20ON || bSHT21ON;
    in.hasP = bBMPON || bBMEON || bBME680ON || bBMP3ON;
    in.hasT2 = bSHT21ON || bONEWIRE;
    if (!in.hasT && !in.hasH && !in.hasP && !in.hasT2)
        return fail(res, n, "unsupported");
    in.t = meshcom_settings.node_temp;
    in.h = meshcom_settings.node_hum;
    in.p = meshcom_settings.node_press;
    in.t2 = meshcom_settings.node_temp2;
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtSens(body, sizeof(body), in));
}

static int execTxq(char *res, size_t n)
{
    uint8_t prio[6];
    txRingPrioCounts(prio);
    RmTxqIn in;
    memset(&in, 0, sizeof(in));
    uint16_t q = 0;
    for (int i = 0; i < 6; i++)
        q = (uint16_t)(q + prio[i]);
    in.queued = q;
    in.cap = (uint16_t)MAX_RING;
    in.bp = (uint8_t)bpCurrentState();
    in.tx = (uint32_t)stat_txn;  // own transmissions (interval counter, drained by the STAT log)
    in.rt = 0;                   // no retransmission counter exists (iRetransmit is a setting)
    uint32_t dr = 0;
    for (int i = 1; i <= 5; i++)
        dr += stat_drop_count[i]; // ring drops per priority, current interval
    in.dr = dr;
    in.util = 0; // no stored utilisation percentage exists (computed and drained in esp32_main.cpp:2796)
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtTxq(body, sizeof(body), in));
}

static int execMbox(char *res, size_t n)
{
#if defined(ENABLE_MSGSTORE)
    RmMboxIn in;
    memset(&in, 0, sizeof(in));
    in.mode = (uint8_t)msgstoreMode();
    in.used = (uint16_t)msgstoreUsed();
    in.slots = (uint16_t)msgstoreSlots();
    in.bytes = (uint32_t)msgstoreBytes();
    in.aUsed = (uint16_t)msgstoreActionsLastHour();
    in.aCap = (uint16_t)MSGSTORE_ACTIONS_PER_HOUR;
    const struct MsgStoreCounters *k = msgstoreCounters();
    if (k != nullptr)
    {
        in.stored = k->stored;
        in.delivered = k->delivered;
        in.purgedAck = k->purged_ack;
        in.droppedStoretime = k->dropped_storetime;
        in.droppedCap = k->dropped_cap;
        in.droppedSlots = k->dropped_slots;
        in.blockedBp = k->blocked_bp;
        in.notified = k->notified;
    }
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtMbox(body, sizeof(body), in));
#else
    return fail(res, n, "unsupported");
#endif
}

// ---- mh -------------------------------------------------------------------------------------------------

// MHEARD rows inside the window, newest first, clamped to the index array. Returns the row count.
static int mhRowsInWindow(uint8_t *idx, uint16_t now_min)
{
    int total = nbrMhRows(nbrMatrix, now_min, RM_HEARD_WINDOW_MIN, idx, NBR_MAX_ROWS);
    return total > NBR_MAX_ROWS ? NBR_MAX_ROWS : total;
}

static int execMhPage(int row, char *res, size_t n)
{
    uint8_t idx[NBR_MAX_ROWS]; // <= 128 bytes on every board
    const uint16_t now_min = uptimeMin16();
    const int total = mhRowsInWindow(idx, now_min);
    if (row >= total && !(total == 0 && row == 0))
        return fail(res, n, "end");
    RmMhRow rows[RM_READ_MH_PAGE];
    uint8_t cnt = 0;
    for (int k = row; k < total && cnt < RM_READ_MH_PAGE; k++)
    {
        // A row that fell out of the window between nbrMhRows() and here keeps its list position
        // as "?": the pager's next index is first + emitted, so a skipped row would be offered
        // again (or, with a whole page gone, the same index forever).
        NbrMhView v;
        if (nbrMhGet(nbrMatrix, idx[k], now_min, &v))
        {
            strlcpy(rows[cnt].call, v.call, sizeof(rows[cnt].call));
            rows[cnt].ageMin = v.age_min;
        }
        else
        {
            strlcpy(rows[cnt].call, "?", sizeof(rows[cnt].call));
            rows[cnt].ageMin = 0;
        }
        cnt++;
    }
    RmMhPageIn in;
    in.total = (uint16_t)total;
    in.first = (uint16_t)row;
    in.rows = rows;
    in.count = cnt;
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtMhPage(body, sizeof(body), in, nullptr));
}

static bool mhDirect(const char *call, char *res, size_t n, int *rc)
{
    uint8_t idx[NBR_MAX_ROWS];
    const uint16_t now_min = uptimeMin16();
    const int total = mhRowsInWindow(idx, now_min);
    for (int k = 0; k < total; k++)
    {
        NbrMhView v;
        if (!nbrMhGet(nbrMatrix, idx[k], now_min, &v) || strcasecmp(v.call, call) != 0)
            continue;
        RmMhDirectIn in;
        memset(&in, 0, sizeof(in));
        in.gw = v.gw != 0;
        in.mesh = v.mesh != 0;
        in.hasRssi = v.rssi != NBR_MH_RSSI_UNKNOWN;
        in.rssi = v.rssi;
        in.hasSnr = v.snr != NBR_SNR_UNKNOWN;
        in.snr = v.snr;
        in.hasPos = nbrPosKnown(v.lat, v.lon);
        in.lat = v.lat;
        in.lon = v.lon;
        // Distance from the SIGNED own position.
        in.hasDist = in.hasPos && ownPosKnown();
        if (in.hasDist)
            in.distKm = nbrDistKm((float)signedLat(), (float)signedLon(), v.lat, v.lon);
        in.hasAlt = v.alt != NBR_MH_ALT_UNKNOWN;
        in.alt = v.alt;
        in.ncnt = v.ncnt;
        in.ex = v.ex;
        in.nb = v.nb;
        in.ageMin = v.age_min;
        char body[RM_FMT_BODY_MAX + 1];
        *rc = done(res, n, body, rmFmtMhDirect(body, sizeof(body), in));
        return true;
    }
    return false;
}

static bool mhRoute(const char *call, char *res, size_t n, int *rc)
{
    const uint16_t now_min = uptimeMin16();
    const int count = nbrRouteCount(nbrMatrix, now_min);
    int best = -1, routes = 0;
    NbrRouteView bv;
    for (int i = 0; i < count; i++)
    {
        NbrRouteView r;
        if (!nbrRouteGet(nbrMatrix, i, now_min, &r) || strcasecmp(r.call, call) != 0)
            continue;
        routes++;
        // shortest route wins, then the youngest
        if (best < 0 || r.hops < bv.hops || (r.hops == bv.hops && r.age_min < bv.age_min))
        {
            bv = r;
            best = i;
        }
    }
    if (best < 0)
        return false;
    RmMhRouteIn in;
    memset(&in, 0, sizeof(in));
    in.hops = bv.hops;
    in.routes = (uint8_t)(routes > 255 ? 255 : routes);
    NbrRowView rv;
    const bool hasRow = bv.row != 0xFF && nbrRowGet(nbrMatrix, bv.row, &rv);
    in.gw = hasRow ? ((rv.flags & NBR_FLAG_GW) != 0) : (bv.gw != 0);
    // mesh bit only known for a row with position info (same rule as the web path page)
    in.hasMesh = hasRow && (rv.flags & NBR_FLAG_POS);
    in.mesh = hasRow && (rv.flags & NBR_FLAG_MESH);
    in.hasRc = false; // no relay-SNR view for a route entry: reported absent
    in.ageMin = bv.age_min;
    // via chain = the entry rows (2-hop row: the direct neighbours B; horizon: its entry rows A)
    char viaBuf[RM_READ_VIA_MAX][NBR_CALL_LEN];
    const char *via[RM_READ_VIA_MAX];
    uint8_t vc = 0;
    for (int a = nbrMaskNext(bv.entry, -1); a >= 0 && vc < RM_READ_VIA_MAX; a = nbrMaskNext(bv.entry, a))
    {
        NbrRowView av;
        if (!nbrRowGet(nbrMatrix, a, &av))
            continue;
        strlcpy(viaBuf[vc], av.call, NBR_CALL_LEN);
        via[vc] = viaBuf[vc];
        vc++;
    }
    in.via = via;
    in.viaCount = vc;
    char body[RM_FMT_BODY_MAX + 1];
    *rc = done(res, n, body, rmFmtMhRoute(body, sizeof(body), in));
    return true;
}

static int execMh(const char *args, char *res, size_t n)
{
    if (args[0] == '\0')
        return 0;
    bool digits = true;
    for (const char *p = args; *p; p++)
        if (!isdigit((unsigned char)*p))
            digits = false;
    if (digits)
        return execMhPage(atoi(args), res, n);
    char call[NBR_CALL_LEN];
    size_t i = 0;
    for (; args[i] != '\0' && i < sizeof(call) - 1; i++)
        call[i] = (char)toupper((unsigned char)args[i]);
    call[i] = '\0';
    if (args[i] != '\0')
        return fail(res, n, "unknown"); // longer than any call the matrix holds
    int rc = 0;
    if (mhDirect(call, res, n, &rc) || mhRoute(call, res, n, &rc))
        return rc;
    return fail(res, n, "unknown");
}

// ---- dispatch -------------------------------------------------------------------------------------------
// One row per READ / RW command of rm_commands.h; the static_assert below fails the build when the two
// lists drift (a command without a handler would fall through to "err blocked" at runtime).
// A handler returns 0 = not mine (a write with args: rmExecWrite answers), 1 = done, -1 = failed.

static int rdRadio(const RmCmd &, char *res, size_t n)
{
    return execRadio(res, n);
}

static int rdName(const RmCmd &c, char *res, size_t n)
{
    if (c.args[0] != '\0')
        return 0; // write executor
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtName(body, sizeof(body), meshcom_settings.node_name));
}

static int rdAtxt(const RmCmd &c, char *res, size_t n)
{
    if (c.args[0] != '\0')
        return 0;
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtAtxt(body, sizeof(body), meshcom_settings.node_atxt));
}

static int rdPos(const RmCmd &c, char *res, size_t n)
{
    return c.args[0] == '\0' ? execPos(res, n) : 0;
}

static int rdSens(const RmCmd &, char *res, size_t n)
{
    return execSens(res, n);
}

static int rdMh(const RmCmd &c, char *res, size_t n)
{
    return execMh(c.args, res, n);
}

static int rdTxq(const RmCmd &, char *res, size_t n)
{
    return execTxq(res, n);
}

static int rdMbox(const RmCmd &, char *res, size_t n)
{
    return execMbox(res, n);
}

static int rdMaxhop(const RmCmd &, char *res, size_t n)
{
    char body[RM_FMT_BODY_MAX + 1];
    return done(res, n, body, rmFmtMaxhop(body, sizeof(body), meshcom_settings.max_hop_text, meshcom_settings.max_hop_pos));
}

struct ReadRow
{
    const char *name;
    int (*run)(const RmCmd &c, char *res, size_t n);
};

static constexpr ReadRow kReadRows[] = {
    {"radio", rdRadio}, {"name", rdName}, {"atxt", rdAtxt}, {"pos", rdPos},     {"sens", rdSens},
    {"mh", rdMh},       {"txq", rdTxq},   {"mbox", rdMbox}, {"maxhop", rdMaxhop},
};
static_assert(rmRowsMatchList(kReadRows, rmKindIsRead), "kReadRows = READ and RW rows of rm_commands.h");

int rmExecRead(const RmCmd &c, char *res, size_t n)
{
    for (size_t i = 0; i < sizeof(kReadRows) / sizeof(kReadRows[0]); i++)
        if (strcmp(c.cmd, kReadRows[i].name) == 0)
            return kReadRows[i].run(c, res, n);
    return 0;
}
