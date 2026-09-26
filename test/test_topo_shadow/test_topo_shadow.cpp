// MeshCom 5 topology, wave 3 (docs/meshcom5-campaign.md), brief W3b. Operator
// decision 2026-09-25 replaces the planned 24-h live shadow with a REPLAY
// shadow over the DK5EN-98 capture 21.-24.09.2026: every "[LOG]" RX frame in
// the capture is fed, ONE FRAME AT A TIME, into BOTH
//
//   OLD  updateMheard()/updateHeyPath()/getMheardCount() (src/mheard_functions.cpp,
//        read-only reference for this wave -- the file this test's job is to
//        prove parity AGAINST, never to change)
//   NEW  nbrNoteFrame()/nbrNoteDirect()/nbrNoteNcnt()/nbrNotePos() (src/nbr_matrix.cpp)
//        and the read layer nbrMhRows()/nbrMhGet()/nbrMhCount()/nbrNcnt()/
//        nbrRouteGet() (src/nbr_views.cpp) -- both W3a's in-flight contract
//        (src/nbr_matrix.h/src/nbr_views.h, "CONTRACT (Welle 3)"), not yet
//        implemented as this file was written; see the file's own header
//        note in the campaign doc for status.
//
// exactly mirroring the two call sites this wave adds in src/lora_functions.cpp
// (both nbrNoteFrame() sites, ~880-960 HN branch skipped -- see below -- and
// ~1085-1200 the regular ':'/'!'/'@' branch) and the MHeard block right after
// it (~1200-1350). Every simulated MINUTE (the on-device millis() trail,
// "t=<ms>" in each [LOG] line, /60000 -- never the wall clock) both sides are
// read back and compared; docs/meshcom5-campaign.md Welle 3 brief W3b lists
// the exact comparison set (MHeard set/count/fields, path table hop-count
// agreement, NCNT per hour) and its category vocabulary
// (CAPACITY/WINDOW_EDGE/DROP_TOKLOOP/HN/B2/OTHER).
//
// --- What this harness drives, and what it deliberately leaves out ---------
//
// Only the RX path that feeds MHeard/topology is replayed -- NOT Stufe 2's
// cover-check/relay-decision (nbrRelayNeed()/nbrCoverMask(), test/test_nbr_replay
// already proves nbr_matrix.cpp's own decisions against this same node's
// capture) and NOT the HN branch (lora_functions.cpp's payload_type=='@' &&
// destination_call=="HN" early-return): that branch feeds nbrNoteReport(),
// which is Welle 3's own separate contract and not part of the MHeard/topology
// parity question this test answers. A '@' frame addressed to "HN" is
// therefore skipped by this harness entirely (neither side), same as it is
// invisible to MHeard on the real device (OnRxDone() returns before reaching
// either).
//
// Own-position lazy branch (lora_functions.cpp: "if(!(row0.flags&POS) &&
// node_lat!=0.0) nbrNotePos(own_call,...)"): NEVER fired here, for the same
// evidence-based reason test/test_nbr_replay/test_nbr_replay.cpp's own
// ASSUMPTION 2 already established for this exact node/window (searched the
// whole fixture for a hw=0 own-POS [NBR] line and found none -- every own-POS
// entry in the capture comes from the generic per-row echo path instead).
// This also means neither side ever gets a configured static node_lat/lon,
// so MHeard's DIST field (needs meshcom_settings.node_lat/lon != 0) and
// nbr_views' distance-derived fields stay N/A on BOTH sides throughout this
// replay -- reported as such below, not fabricated from a guessed position.
//
// bGATEWAY is assumed true, same reasoning as test_nbr_replay.cpp's own
// ASSUMPTION ("[GW]/GWU/keep-alive traffic implies a gateway build").
//
// --- Wall clock -------------------------------------------------------------
//
// The OLD path's mh_date/mh_time (and therefore updateMheard()'s "year<2025"
// admission guard) come from the CAPTURE TOOL's own leading wall-clock
// timestamp on every raw-log line ("YYYY-MM-DD HH:MM:SS.mmm", meshlogger.py),
// per this wave's brief -- NOT from the device's own possibly-unsynced clock.
// nbrSetClock() on the NEW side is fed the same epoch, once per frame (cheap,
// idempotent: nbrSetClock() only ever sets a boot epoch offset).
//
// --- Reboots ------------------------------------------------------------
//
// A device reboot shows up as the on-device millis() trail ("t=<ms>")
// stepping BACKWARDS between two consecutive frames -- the same detector
// tools/nbr_replay_extract.py's --from-last-reset uses. Every such point
// reinitialises BOTH sides (initMheard(); nbrInit()) before processing the
// frame that triggered it, so neither side ever carries state across a
// boundary the real device did not.
//
// --- Fixture ------------------------------------------------------------
//
// test/test_topo_shadow/fixtures/dk5en98-20260924.txt (checked in, ~500 KB,
// built by tools/topo_shadow_extract.py from ~/Downloads/dk5en-98-nbr/2026-09-24.log)
// is ALWAYS replayed. When the four raw captures are additionally present at
// ~/Downloads/dk5en-98-nbr/2026-09-2{1,2,3,4}.log (too big -- ~54 MB raw,
// ~3.8 MB even after this tool's own filter -- to check in, see that tool's
// own header), the full 21.-24.09. window is ALSO replayed, chronologically
// concatenated; when any of the four is absent the full run is SKIPPED
// (printed, not failed).

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <Arduino.h>
#include <aprs_structures.h>
#include <mc_text.h>
#include <mheard_functions.h>
#include <mheard_record.h>
#include <nrf52/WisBlock-API.h>

#include "nbr_matrix.h"
#include "nbr_views.h"

// ============================================================================
// Link stubs (own copy, NOT test/test_decodemheard/stubs/parser_link_stubs.h:
// getUnixClock()/getTimeString() must be REPLAY-DRIVEN here, from the
// capture's own logger timestamp -- see test_mheard_aging.cpp for the same
// "own copy, not the shared header" pattern and why. Regxp.cpp/regex_functions.cpp/
// aprs_functions.cpp/charset_filter.cpp/mheard_functions.cpp/via_functions.cpp
// are the SAME five+one translation units native_parsers already links
// successfully against this exact stub shape (platformio.ini env
// native_topo_shadow mirrors native_parsers's build_src_filter, plus
// nbr_matrix.cpp/nbr_views.cpp).
// ============================================================================

s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
bool bGATEWAY = true;   // ASSUMPTION, see file header (mirrors test_nbr_replay.cpp)
bool bVIA = false;
int BOARD_HARDWARE = 0; // "no info" -- this harness never TX-encodes, only RX-replays
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

int printlndeb(const char *buff) { (void)buff; return 0; }
int printdeb(const char *buff) { (void)buff; return 0; }
int printdeb(String str) { (void)str; return 0; }
int printfdeb(const char *format, ...) { (void)format; return 0; }

void addBLEOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
void addBLEComToOutBuffer(uint8_t *buffer, uint16_t len) { (void)buffer; (void)len; }
bool is_equ(const char *buf1, const char *buf2)
{
    return buf1 != nullptr && buf2 != nullptr && strcmp(buf1, buf2) == 0;
}
String convertUNIXtoString(uint32_t timestamp) { (void)timestamp; return String(""); }

// ---- replay-driven wall clock (see file header: the logger's own leading
// timestamp, not the device's) -- getUnixClock()/getTimeString() read it.
static unsigned long g_wall_epoch = 0;
static int g_wall_year = 0, g_wall_month = 0, g_wall_day = 0;
static int g_wall_hour = 0, g_wall_minute = 0, g_wall_second = 0;

unsigned long getUnixClock() { return g_wall_epoch; }
String getTimeString()
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", g_wall_hour, g_wall_minute, g_wall_second);
    return String(buf);
}
static String getDateStringReplay()
{
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", g_wall_year, g_wall_month, g_wall_day);
    return String(buf);
}

#include "byte_fifo.h"
static uint8_t phoneComStoreStub[2048];
byte_fifo_t phoneComRing = BYTE_FIFO_INIT(phoneComStoreStub);

// ---- direct extern access to mheard_functions.cpp's own storage arrays
// (same pattern as test/test_mheard_aging/test_mheard_aging.cpp: not exported
// via mheard_functions.h on purpose, a test may still extern the real
// symbols -- see that file's own header comment).
extern MheardRecord mheardRecords[MAX_MHEARD];
extern char mheardCalls[MAX_MHEARD][10];
extern float mheardLat[MAX_MHEARD];
extern float mheardLon[MAX_MHEARD];
extern int mheardAlt[MAX_MHEARD];
extern uint32_t mheardMillis[MAX_MHEARD];
extern int mheardNCount[MAX_MHEARD];
extern unsigned char mheardPathBuffer1[MAX_MHPATH][52];
extern char mheardPathCalls[MAX_MHPATH][10];
extern uint32_t mheardPathMillis[MAX_MHPATH];
extern uint8_t mheardPathLen[MAX_MHPATH];

// ============================================================================
// Own call. DK5EN-98 -- the node this capture was recorded from (docs
// throughout the campaign; MEMORY.md "48h capture harvested").
// ============================================================================
static const char *OWN_CALL = "DK5EN-98";

// ============================================================================
// Position/altitude/NCNT payload decode -- reimplemented, not called (same
// choice test/test_nbr_replay/test_nbr_replay.cpp already made and documents:
// keeps this harness inside its own file set instead of linking
// decodeAPRSPOS() (src/aprs_functions.cpp, out of set), while still turning
// every POS line into a real, independently computed value instead of an
// input copied from the fixture's own answer). Mirrors decodeAPRSPOS()'s
// lat/lon split, aprsExtractTag('A', INT) for altitude and the dedicated
// "/N[1-9]" NCNT loop (all src/aprs_functions.cpp, read-only reference).
// ============================================================================

struct DecodedPos
{
    bool ok = false;
    float lat = 0.0f;
    float lon = 0.0f;
    int alt_ft = 0;    // 0 = "/A=" tag absent, matches aprspos.alt's own default
    int ncnt = 0;      // 0 = "/N[1-9]" tag absent, matches aprspos.ncnt's own default
};

static double topo_conv_coord_to_dec(double coord)
{
    int ig = (int)(coord / 100.0);
    double dm = (coord - (double)(ig * 100)) / 60.0;
    return (double)ig + dm;
}

static DecodedPos decode_pos_payload(const std::string &payload)
{
    DecodedPos d;

    size_t i1 = payload.find_first_of("NS");
    if (i1 == std::string::npos || i1 == 0 || i1 + 1 >= payload.size())
        return d;
    std::string lat_str = payload.substr(0, i1);
    char lat_c = payload[i1];

    size_t lon_start = i1 + 2; // hemisphere char + APRS symbol-table char
    if (lon_start >= payload.size())
        return d;
    size_t i2 = payload.find_first_of("WE", lon_start);
    if (i2 == std::string::npos || i2 == lon_start)
        return d;
    std::string lon_str = payload.substr(lon_start, i2 - lon_start);
    char lon_c = payload[i2];

    char *end1 = NULL;
    char *end2 = NULL;
    double lat_num = strtod(lat_str.c_str(), &end1);
    double lon_num = strtod(lon_str.c_str(), &end2);
    if (end1 == lat_str.c_str() || end2 == lon_str.c_str())
        return d;

    float lat = (float)topo_conv_coord_to_dec(lat_num);
    if (lat_c == 'S')
        lat = lat * -1.0f;
    float lon = (float)topo_conv_coord_to_dec(lon_num);
    if (lon_c == 'W')
        lon = lon * -1.0f;

    d.ok = true;
    d.lat = lat;
    d.lon = lon;

    // "/A=<digits>" -- altitude in feet, terminated by '/'/' '/end/7 chars
    // (aprsExtractTag('A', APRS_TAG_INT), src/aprs_functions.cpp:646-680).
    size_t ap = payload.find("/A=");
    if (ap != std::string::npos)
    {
        size_t start = ap + 3;
        size_t end = start;
        while (end < payload.size() && end < start + 7 && payload[end] != '/' && payload[end] != ' ')
            end++;
        d.alt_ft = atoi(payload.substr(start, end - start).c_str());
    }

    // "/N[1-9]<digits>" -- reported neighbour count, up to 3 digits total
    // (decodeAPRSPOS()'s own dedicated loop, src/aprs_functions.cpp:844-865).
    size_t np = payload.find("/N");
    if (np != std::string::npos && np + 2 < payload.size() &&
        payload[np + 2] >= '1' && payload[np + 2] <= '9')
    {
        size_t start = np + 2;
        size_t end = start;
        while (end < payload.size() && end < start + 3 && payload[end] != '/' && payload[end] != ' ')
            end++;
        d.ncnt = atoi(payload.substr(start, end - start).c_str());
    }

    return d;
}

// ============================================================================
// [LOG] line parsing (printBuffer_aprs(), src/loop_functions.cpp, and its
// tail setlogFormatRxTail(), src/setlog_lines.cpp) -- adapted from
// test/test_nbr_replay/test_nbr_replay.cpp's parse_log_line() (read-only
// model for this wave), extended with source_call/source_last/dest_call
// (this test needs MHeard's key fields, the replay decision test did not)
// and LH:/MOD: split into source_mod + last_hw (this test needs the
// Direct-Slot's mod/hw fields, which the 0x80-bit rule in
// nbrBuildDirectInfo() -- src/lora_functions.cpp -- reads off msg_last_hw,
// not the HW: trailer alone) plus the leading logger wall-clock timestamp.
// ============================================================================

struct LogFrame
{
    char type = 0;          // ':' | '!' | '@'
    uint32_t msg_id = 0;
    std::string path;       // msg_source_path, "A,B,C"
    std::string dest;       // msg_destination_path
    std::string payload;
    std::string source_call;  // first path token
    std::string source_last;  // last path token
    std::string dest_call;    // last dest token (decodeAPRS()'s cConcat2 rule)
    bool mesh = false;
    uint8_t source_hw = 0;    // "HW:" trailer field (msg_source_hw)
    uint8_t source_mod = 0;   // "MOD:hi/lo" trailer recombined
    uint8_t last_hw = 0;      // "LH:" trailer field (msg_last_hw)
    uint8_t fw_version = 0;
    char fw_sub = 0;
    int16_t rssi = 0;
    int8_t snr = 0;
    unsigned long t_ms = 0;
    uint16_t now_min = 0;
    bool decoded = true;      // false when the trailer shows FCS:0000
    // logger's own leading wall-clock timestamp
    int wall_year = 0, wall_month = 0, wall_day = 0;
    int wall_hour = 0, wall_minute = 0, wall_second = 0;
    bool has_wall = false;
};

static std::string last_token(const std::string &s)
{
    size_t p = s.find_last_of(',');
    return (p == std::string::npos) ? s : s.substr(p + 1);
}

static std::string first_token(const std::string &s)
{
    size_t p = s.find_first_of(',');
    return (p == std::string::npos) ? s : s.substr(0, p);
}

static bool parse_log_line(const std::string &line, LogFrame &out)
{
    // Leading logger timestamp: "YYYY-MM-DD HH:MM:SS.mmm  ..." -- best
    // effort, absent on a line this tool never sees (there is none in the
    // fixture this test reads, but a raw ~/Downloads file always has it).
    {
        int y, mo, d, h, mi, s;
        if (sscanf(line.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6)
        {
            out.wall_year = y;
            out.wall_month = mo;
            out.wall_day = d;
            out.wall_hour = h;
            out.wall_minute = mi;
            out.wall_second = s;
            out.has_wall = true;
        }
    }

    size_t p = line.find("[LOG] ");
    if (p == std::string::npos)
        return false;
    std::string s = line.substr(p + 6);

    std::istringstream iss(s);
    std::string tok_len, tok_type, tok_msgid, tok_h, tok_s, tok_t, tok_m;
    if (!(iss >> tok_len >> tok_type >> tok_msgid >> tok_h >> tok_s >> tok_t >> tok_m))
        return false;
    if (tok_type.size() != 1 || tok_msgid.size() < 2 || tok_msgid[0] != 'x' || tok_m.size() < 2)
        return false;
    out.type = tok_type[0];
    out.msg_id = (uint32_t)strtoul(tok_msgid.c_str() + 1, NULL, 16);
    out.mesh = atoi(tok_m.c_str() + 1) != 0;

    size_t rest_pos = (size_t)iss.tellg();
    while (rest_pos < s.size() && s[rest_pos] == ' ')
        rest_pos++;

    size_t hw_pos = s.rfind(" HW:");
    if (hw_pos == std::string::npos || hw_pos < rest_pos)
        return false;
    std::string block = s.substr(rest_pos, hw_pos - rest_pos);
    std::string trailer = s.substr(hw_pos);

    size_t gt = block.find('>');
    if (gt == std::string::npos)
        return false;
    out.path = block.substr(0, gt);
    std::string rest = block.substr(gt + 1);
    size_t type_pos = rest.find(out.type);
    if (type_pos == std::string::npos)
        return false;
    out.dest = rest.substr(0, type_pos);
    out.payload = rest.substr(type_pos + 1);

    out.source_call = first_token(out.path);
    out.source_last = last_token(out.path);
    out.dest_call = last_token(out.dest);

    size_t hwv = trailer.find("HW:");
    size_t modv = trailer.find("MOD:");
    size_t rssiv = trailer.find("RSSI:");
    size_t snrv = trailer.find("SNR:");
    size_t fwv = trailer.find("FW:");
    size_t lhv = trailer.find("LH:");
    size_t tv = trailer.find("t=");
    if (hwv == std::string::npos || modv == std::string::npos || rssiv == std::string::npos ||
        snrv == std::string::npos || fwv == std::string::npos || lhv == std::string::npos ||
        tv == std::string::npos)
        return false;

    out.source_hw = (uint8_t)atoi(trailer.c_str() + hwv + 3);
    out.rssi = (int16_t)atoi(trailer.c_str() + rssiv + 5);
    out.snr = (int8_t)atoi(trailer.c_str() + snrv + 4);
    out.last_hw = (uint8_t)strtoul(trailer.c_str() + lhv + 3, NULL, 16);
    out.t_ms = strtoul(trailer.c_str() + tv + 2, NULL, 10);
    out.now_min = (uint16_t)(out.t_ms / 60000UL);
    out.decoded = trailer.find("FCS:0000") == std::string::npos;

    // "MOD:%01X/%01i" -- hi nibble (source_mod>>4) then lo nibble
    // (source_mod&0xF), src/loop_functions.cpp printBuffer_aprs().
    {
        unsigned hi = 0;
        int lo = 0;
        if (sscanf(trailer.c_str() + modv + 4, "%1X/%1d", &hi, &lo) == 2)
            out.source_mod = (uint8_t)(((hi & 0xF) << 4) | (lo & 0xF));
    }
    // "FW:%02i:%c"
    {
        int fwv_i = 0;
        char fwc = 0;
        if (sscanf(trailer.c_str() + fwv + 3, "%d:%c", &fwv_i, &fwc) == 2)
        {
            out.fw_version = (uint8_t)fwv_i;
            out.fw_sub = fwc;
        }
    }

    return true;
}

// ============================================================================
// Replay state and reinit
// ============================================================================

struct PathState
{
    unsigned long last_seen_ms = 0;
};

static NbrMatrix g_topo;
static bool g_have_frame = false;
static unsigned long g_last_t_ms = 0;
static uint16_t g_last_now_min = 0xFFFF;

// Orchestrator follow-up 3: own position learned from OWN_CALL's own '!'
// echoes (source_call == OWN_CALL, any last hop -- the frame this node sent
// itself, heard back via a relay), same tactic test_nbr_replay.cpp's own
// header comment describes for the identical node/window. Once known, fed
// into row 0 exactly like lora_functions.cpp's lazy own-position branch
// (nbrNotePos(), guarded on NBR_FLAG_POS not already set) and used for DIST
// on BOTH sides below -- unlike every other field, DIST is computed by THIS
// harness itself (nbrDistKm(), Haversine) rather than taken from either
// production formula (gps.distanceBetween()/decodeAPRSPOS(), both out of
// file set): comparable between OLD and NEW because both sides use the same
// harness-computed value, not byte-identical to a real device's own output.
static double g_own_lat = 0.0, g_own_lon = 0.0;
static bool g_own_pos_known = false;

// Mirrors mheardRoundDist() (src/mheard_record.h, read-only reference):
// format/reparse for the same decimal (not binary) rounding, so a NEW-side
// value compares fairly against the OLD side's mr_dist (which that function
// already rounds this way).
static float round_dist_km(double km)
{
    if (km < 0.0)
        return -1.0f;
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.1lf", km);
    return (float)atof(tmp);
}

// B2 (brief category: "relay HEY overwrote the direct entry's date/type"):
// updateHeyPath()'s own "R<n>;" REP-action (src/mheard_functions.cpp
// ~547-565, read-only reference) finds the ORIGINATOR's MHeard slot (by
// mh_sourcecallsign, not mh_callsign/last-hop) and overwrites its WHOLE
// record from the CURRENT frame via mheardRecordFromLine(), restoring only
// hw/mod/rssi/snr/dist from the previous record afterward -- date/time/type/
// path_len/mesh/ncount are left as the CURRENT frame's values. A RELAYED
// copy of a HEY report (source_call != source_last) therefore leaks THAT
// hop's own mesh/type/date/time/path_len/ncount into the originator's direct
// entry, confirmed empirically while building this harness: DL2JA-1's own
// direct HEY frames are all M00 in the fixture, yet its MHeard mesh field
// flips to 1 the first minute a relayed copy of one of its HEY reports
// (M01, via DL2UD-1/DL3NCU-1/DB0ED-99) arrives -- exactly this mechanism,
// not a harness bug. Sticky per call (a corrupted record stays corrupted on
// the OLD side until the call is heard DIRECTLY again -- updateMheard()
// then fully rewrites its slot, see replay_frame()) rather than cleared
// every minute, so every later minute that still shows the stale value is
// also attributed to B2, not OTHER.
static std::set<std::string> g_b2_touched;

static void reinit_both()
{
    initMheard();
    memset(&g_topo, 0, sizeof(g_topo));
    nbrInit(g_topo, OWN_CALL, 0);
    if (bGATEWAY)
        nbrRowSetFlag(g_topo, 0, NBR_FLAG_GW);
    g_last_now_min = 0xFFFF;
}

// Direct-Slot builder -- exact mirror of nbrBuildDirectInfo()/its call sites
// in src/lora_functions.cpp (this wave's own change there).
static NbrDirectInfo build_direct_info(const LogFrame &lf)
{
    NbrDirectInfo info;
    memset(&info, 0, sizeof(info));
    info.plt = lf.type;
    info.hw = lf.last_hw & 0x7F;
    info.mod = ((lf.last_hw & 0x80) == 0x80) ? lf.source_mod : (uint8_t)(lf.source_mod | 0xF0);
    info.rssi = lf.rssi;
    // sec: the wall clock is always valid in this replay (fed from the
    // logger timestamp before the millis()-fallback branch could ever be
    // reached) -- see nbrBuildDirectInfo()'s own "< 2025" guard, mirrored
    // here as "has_wall".
    info.sec = lf.has_wall ? (uint8_t)lf.wall_second : (uint8_t)((lf.t_ms / 1000UL) % 60);
    info.pl = 0; // msg_last_path_cnt is not in the [LOG] trailer; PL comparison is reported N/A (see report)
    info.mesh = lf.mesh;
    info.own_frame = is_equ(lf.source_call.c_str(), lf.source_last.c_str());
    info.fw = info.own_frame ? lf.fw_sub : 0;
    info.has_pos = false;
    info.lat = NAN;
    info.lon = NAN;
    info.alt_m = NBR_ALT_UNKNOWN;
    return info;
}

// "R<n>" HEY report parse, mirroring nbrParseHeyReportedCount() (this wave's
// own addition, src/lora_functions.cpp) -- reimplemented here rather than
// linked (that function is static in lora_functions.cpp, out of this test's
// reach; lora_functions.cpp is far too heavy to link into a native test).
static bool parse_hey_reported_count(const std::string &payload, long *out_n)
{
    std::string buf = payload;
    if (buf.empty() || buf.back() != ';')
        buf += ';';
    size_t ipos = buf.find(';');
    if (ipos == std::string::npos || ipos == 0 || buf[0] != 'R')
        return false;
    int icomma = 0;
    for (size_t i = 1; i < ipos; i++)
        if (buf[i] == ',')
            icomma++;
    if (icomma != 0 && icomma != 2)
        return false;
    *out_n = strtol(buf.substr(1, ipos - 1).c_str(), NULL, 10);
    return true;
}

// ============================================================================
// One frame, both sides -- mirrors src/lora_functions.cpp OnRxDone()'s
// ':'/'!'/'@' branch (nbrNoteFrame() call site ~1085-1200) and the MHeard
// block right after it (~1200-1350), MINUS Stufe 2 and the HN branch (see
// file header).
// ============================================================================

static void replay_frame(const LogFrame &lf)
{
    if (lf.has_wall)
    {
        g_wall_year = lf.wall_year;
        g_wall_month = lf.wall_month;
        g_wall_day = lf.wall_day;
        g_wall_hour = lf.wall_hour;
        g_wall_minute = lf.wall_minute;
        g_wall_second = lf.wall_second;
        struct tm tmv;
        memset(&tmv, 0, sizeof(tmv));
        tmv.tm_year = lf.wall_year - 1900;
        tmv.tm_mon = lf.wall_month - 1;
        tmv.tm_mday = lf.wall_day;
        tmv.tm_hour = lf.wall_hour;
        tmv.tm_min = lf.wall_minute;
        tmv.tm_sec = lf.wall_second;
        g_wall_epoch = (unsigned long)timegm(&tmv);
        nbrSetClock(g_topo, (uint32_t)g_wall_epoch, lf.now_min);
    }

    // HN branch is out of scope for this test (see file header) -- skip
    // BOTH sides, exactly like OnRxDone()'s early return.
    if (lf.type == '@' && is_equ(lf.dest_call.c_str(), "HN"))
        return;

    if (!lf.decoded)
        return; // FCS:0000 -- decodeAPRS() failed, OnRxDone() never reaches either side

    if (lf.type != ':' && lf.type != '!' && lf.type != '@')
        return;

    bool own_echo = is_equ(lf.source_last.c_str(), OWN_CALL);

    // ---- NEW: topology (src/nbr_matrix.cpp, this wave's lora_functions.cpp
    // call sites) --------------------------------------------------------
    nbrNoteFrame(g_topo, lf.path.c_str(), lf.type, lf.payload.c_str(),
                 is_equ(lf.dest.c_str(), "HG"), lf.rssi, lf.snr, lf.now_min);

    NbrDirectInfo direct_info = build_direct_info(lf);

    if (lf.type == '!')
    {
        DecodedPos pos = decode_pos_payload(lf.payload);
        if (pos.ok)
        {
            nbrNotePos(g_topo, lf.source_call.c_str(), pos.lat, pos.lon, lf.mesh,
                       lf.source_hw, lf.now_min);

            if (direct_info.own_frame)
            {
                direct_info.has_pos = true;
                direct_info.lat = pos.lat;
                direct_info.lon = pos.lon;
                int alt_m = pos.alt_ft;
                if (lf.fw_version > 13)
                    alt_m = (int)((float)alt_m * 0.3048f);
                direct_info.alt_m = alt_m;
            }

            if (pos.ncnt > 0)
                nbrNoteNcnt(g_topo, lf.source_call.c_str(), pos.ncnt, lf.now_min);

            // Own position (see g_own_lat's own comment): learned from any
            // frame THIS node originated, regardless of last hop.
            if (is_equ(lf.source_call.c_str(), OWN_CALL))
            {
                g_own_lat = pos.lat;
                g_own_lon = pos.lon;
                g_own_pos_known = true;
                if (!nbrRowHasFlag(g_topo, 0, NBR_FLAG_POS))
                    nbrNotePos(g_topo, OWN_CALL, (float)g_own_lat, (float)g_own_lon, bMESH, 0, lf.now_min);
            }
        }
    }

    if (!own_echo)
        nbrNoteDirect(g_topo, lf.source_last.c_str(), direct_info, lf.now_min);

    if (lf.type == '@')
    {
        long n = 0;
        if (parse_hey_reported_count(lf.payload, &n))
            nbrNoteNcnt(g_topo, lf.source_call.c_str(), (int)n, lf.now_min);
    }

    // ---- OLD: MHeard (src/mheard_functions.cpp, read-only reference) ---
    if (own_echo)
        return;

    struct mheardLine ml;
    initMheardLine(ml);

    mcSet(ml.mh_callsign, sizeof(ml.mh_callsign), lf.source_last.c_str());
    mcSet(ml.mh_sourcepath, sizeof(ml.mh_sourcepath), lf.path.c_str());
    mcSet(ml.mh_sourcecallsign, sizeof(ml.mh_sourcecallsign), lf.source_call.c_str());
    mcSet(ml.mh_destinationpath, sizeof(ml.mh_destinationpath), lf.dest.c_str());
    ml.mh_hw = lf.last_hw & 0x7F;
    ml.mh_mod = ((lf.last_hw & 0x80) == 0x80) ? lf.source_mod : (uint8_t)(lf.source_mod | 0xF0);
    ml.mh_rssi = lf.rssi;
    ml.mh_snr = lf.snr;
    mcSet(ml.mh_date, sizeof(ml.mh_date), getDateStringReplay().c_str());
    mcSet(ml.mh_time, sizeof(ml.mh_time), getTimeString().c_str());
    ml.mh_payload_type = lf.type;
    ml.mh_dist = -1; // recomputed below for an own_frame POS once g_own_pos_known (see g_own_lat's comment)
    ml.mh_path_len = 0; // PL not in the [LOG] trailer -- N/A, see report
    ml.mh_mesh = lf.mesh;
    ml.mh_ncount = 0;
    ml.mh_path_payload[0] = 0;

    if (lf.type == '!')
    {
        DecodedPos pos = decode_pos_payload(lf.payload);
        if (pos.ok && pos.ncnt > 0)
            ml.mh_ncount = 0; // mirrors lora_functions.cpp: applied via mheardNCount[] below, not mh_ncount here

        // DIST: mirrors lora_functions.cpp's own gate (source_call ==
        // source_last, i.e. own_frame) -- a relayed POS leaves mh_dist at -1
        // here too, same as production (updateMheard()'s own REP-action may
        // still inherit an OLDER value from the existing record, exactly as
        // it does on a real device).
        if (pos.ok && is_equ(lf.source_call.c_str(), lf.source_last.c_str()) && g_own_pos_known)
            ml.mh_dist = nbrDistKm((float)g_own_lat, (float)g_own_lon, pos.lat, pos.lon);
    }

    updateMheard(ml, 0);
    // This frame's mh_callsign (== source_last) just got its WHOLE record
    // rewritten from genuine, direct fields -- any earlier B2 taint on this
    // call is gone (see g_b2_touched's own comment).
    g_b2_touched.erase(lf.source_last);

    if (lf.type == '!')
    {
        DecodedPos pos = decode_pos_payload(lf.payload);
        if (pos.ok)
        {
            // find the slot updateMheard() just wrote/refreshed
            int ipos = -1;
            for (int i = 0; i < MAX_MHEARD; i++)
                if (mheardCalls[i][0] != 0 && is_equ(mheardCalls[i], lf.source_last.c_str()))
                {
                    ipos = i;
                    break;
                }
            if (ipos >= 0)
            {
                mheardLat[ipos] = pos.lat;
                mheardLon[ipos] = pos.lon;
                int alt_m = pos.alt_ft;
                if (lf.fw_version > 13)
                    alt_m = (int)((float)alt_m * 0.3048f);
                mheardAlt[ipos] = alt_m;
            }
            if (pos.ncnt > 0)
            {
                // mirrors lora_functions.cpp lines ~1163-1206: the entry keyed
                // by SOURCE call (own_frame or relayed), not necessarily ipos
                // above (that one is keyed by the LAST HOP / mh_callsign).
                for (int i = 0; i < MAX_MHEARD; i++)
                    if (mheardCalls[i][0] != 0 && is_equ(mheardCalls[i], lf.source_call.c_str()))
                    {
                        mheardNCount[i] = pos.ncnt;
                        break;
                    }
            }
        }
    }

    if (lf.type == '@')
    {
        mcSet(ml.mh_path_payload, sizeof(ml.mh_path_payload), lf.payload.c_str());
        updateHeyPath(ml);

        // See g_b2_touched's own comment: a RELAYED copy (source_call !=
        // source_last) whose payload matches updateHeyPath()'s own "R<n>;"
        // grammar overwrites the ORIGINATOR's (source_call's) MHeard record
        // with this frame's date/type/mesh/path_len/ncount.
        if (!own_echo && !is_equ(lf.source_call.c_str(), lf.source_last.c_str()))
        {
            long dummy_n = 0;
            if (parse_hey_reported_count(lf.payload, &dummy_n))
                g_b2_touched.insert(lf.source_call);
        }
    }
}

// ============================================================================
// Per-minute comparison / reporting
// ============================================================================

enum Category
{
    CAT_CAPACITY,
    CAT_WINDOW_EDGE,
    CAT_DROP_TOKLOOP,
    CAT_HN,
    CAT_B2,
    CAT_OTHER,
    CAT_COUNT
};
static const char *CAT_NAME[CAT_COUNT] = {
    "CAPACITY", "WINDOW_EDGE", "DROP_TOKLOOP", "HN", "B2", "OTHER"};

// Path-table categorisation (orchestrator follow-up 1): why a sender of the
// OLD path table is missing on the NEW side (row XOR horizon is the
// acceptance criterion; a MISSING sender falls into exactly one of these).
enum PathCat
{
    PCAT_DIRECT,        // sender has a fresh direct row -- routes deliberately exclude direct rows (Konzept 4.7); MHeard shows it, this is not a gap
    PCAT_SHORT,         // stored path < 3 tokens: too short for a horizon entry by rule, and should already have a 2-hop row -- worth a look if it shows up
    PCAT_OWN_SENDER,    // own call IS the sender/originator (first token) -- excluded by rule 4.7 ("me as entry token"); a relay of our own report we hear back
    PCAT_OWN_ELSEWHERE, // own call appears somewhere ELSE in the stored path (an intermediate relay hop, not the originator) -- same rule 4.7 exclusion, different mechanism
    PCAT_AGE,           // OLD's entry sits within 2 min of ITS OWN 12h aging boundary -- a minute-quantised race, not a real gap
    PCAT_EVICT_H,       // horizon or row table at capacity when this entry should have landed
    PCAT_TEXT,          // OLD entry was fed from a frame type the horizon ignores (never reached via this harness's own replay_frame(), see its comment -- kept for completeness)
    PCAT_OTHER,         // unexplained -- must be 0
    PCAT_COUNT
};
static const char *PCAT_NAME[PCAT_COUNT] = {
    "DIRECT", "SHORT", "OWN_SENDER", "OWN_ELSEWHERE", "AGE", "EVICT_H", "TEXT", "OTHER"};

// Orchestrator follow-up: SHORT is a label, not a reason -- for a stored
// 2-token path "A,B", nbrNoteFrame()'s window covers both tokens (A gets a
// row, edge (A,B) is a window-pair hit, and the ME step marks B Direct if B
// is the last hop), so nbrRouteGet() SHOULD show A as a 2-hop row via B
// (Konzept 4.7: "Eintritt ueber die direkten Nachbarn B aus heardBy(X) &
// Direkt"). Sub-categorises every SHORT case by the actual new-side cause.
enum ShortSubCat
{
    SHORT_A_DIRECT,       // A turns out to be a fresh direct row after all (re-verified independently of the is_direct check above -- should not occur)
    SHORT_ONE_TOKEN,      // full_path has only 1 token -- updateHeyPath() itself refuses this (mheard_functions.cpp: "ips <= 0 -> return", no path-table entry at all), so this should never fire; kept defensive
    SHORT_A_NO_ROW,       // A has no row at all right now (never created, or evicted)
    SHORT_B_NO_ROW,       // the stored last hop B has no row at all right now
    SHORT_EDGE_NOT_FRESH, // A has a row, B has a row, but edge (A heard-by B) is not live/fresh
    SHORT_B_NOT_DIRECT,   // edge (A,B) is fresh, but B itself is not currently a fresh direct neighbour (cell[B][0] not fresh)
    SHORT_OTHER,          // a broad scan found SOME currently-direct neighbour M with a fresh edge (A,M) -- nbrRouteGet() should have produced a row via M and did not: a real gap
    SHORT_SUBCAT_COUNT
};
static const char *SHORT_SUBCAT_NAME[SHORT_SUBCAT_COUNT] = {
    "A_DIRECT", "ONE_TOKEN", "A_NO_ROW", "B_NO_ROW", "EDGE_NOT_FRESH", "B_NOT_DIRECT", "OTHER"};

struct HourBucket
{
    long minutes = 0;
    long minutes_set_mismatch = 0;
    long cat[CAT_COUNT] = {0};
    long count_mismatch = 0;
    long field_mismatch = 0;
};

struct Report
{
    std::map<int, HourBucket> hours; // key: hour index since replay start
    long total_minutes = 0;
    long total_set_mismatch_minutes = 0;
    std::vector<std::string> examples[CAT_COUNT];
    long path_hop_agree = 0;
    long path_hop_disagree = 0;      // kept for the "both row and horizon" structural violation only now
    long path_cat[PCAT_COUNT] = {0};
    std::vector<std::string> path_examples[PCAT_COUNT];
    long short_sub[SHORT_SUBCAT_COUNT] = {0};
    std::vector<std::string> short_sub_examples[SHORT_SUBCAT_COUNT];
    long ncnt_rows_by_hour_topo = 0;
    long ncnt_rows_by_hour_mheard = 0;
    long dist_compared = 0;
    long dist_agree = 0;
    std::vector<std::string> dist_examples;
};

static void report_example(Report &r, Category c, const std::string &line)
{
    if (r.examples[c].size() < 10)
        r.examples[c].push_back(line);
}

static bool mheard_fresh(int i, uint32_t now_ms, uint32_t window_ms)
{
    return mheardCalls[i][0] != 0 && (uint32_t)(now_ms - mheardMillis[i]) < window_ms;
}

// Compares the two MHeard sets at the given window (minutes) and records
// mismatches into r. window_min in {60, 720} per the brief.
// Age (ms) since the window boundary this entry sits at, within WINDOW_EDGE_MS
// of expiring/entering -- minute-quantised now_min vs. millis()-quantised
// mheardMillis[]/edge last_min make an entry cross the window right at a
// minute boundary look present on one side and absent on the other for
// exactly one minute; 2 minutes of slack covers both quantisations.
static const uint32_t WINDOW_EDGE_MS = 2UL * 60UL * 1000UL;

static void compare_set(Report &r, HourBucket &hb, uint32_t now_ms, uint16_t now_min,
                         uint16_t window_min, uint32_t window_ms)
{
    std::map<std::string, uint32_t> old_age_ms, new_age_min;
    for (int i = 0; i < MAX_MHEARD; i++)
        if (mheard_fresh(i, now_ms, window_ms))
            old_age_ms[std::string(mheardCalls[i])] = now_ms - mheardMillis[i];

    uint8_t idxbuf[NBR_MAX_ROWS];
    int n = nbrMhRows(g_topo, now_min, window_min, idxbuf, NBR_MAX_ROWS);
    for (int i = 0; i < n && i < NBR_MAX_ROWS; i++)
    {
        NbrMhView v;
        if (nbrMhGet(g_topo, idxbuf[i], now_min, &v))
            new_age_min[std::string(v.call)] = v.age_min;
    }

    bool old_full = true;
    for (int i = 0; i < MAX_MHEARD; i++)
        if (mheardCalls[i][0] == 0)
        {
            old_full = false;
            break;
        }
    bool new_full = nbrRowsUsed(g_topo) >= NBR_MAX_ROWS;

    bool mismatch = false;
    for (const auto &kv : old_age_ms)
    {
        if (new_age_min.count(kv.first))
            continue;
        mismatch = true;
        Category c = CAT_OTHER;
        if (new_full && !old_full)
            c = CAT_CAPACITY;
        else if (window_ms > kv.second && (window_ms - kv.second) <= WINDOW_EDGE_MS)
            c = CAT_WINDOW_EDGE;
        hb.cat[c]++;
        char buf[160];
        snprintf(buf, sizeof(buf), "min=%u win=%u OLD-only call=%s age_ms=%u", (unsigned)now_min,
                 (unsigned)window_min, kv.first.c_str(), (unsigned)kv.second);
        report_example(r, c, buf);
    }
    for (const auto &kv : new_age_min)
    {
        if (old_age_ms.count(kv.first))
            continue;
        mismatch = true;
        // WINDOW_EDGE does NOT apply here (orchestrator follow-up): a
        // NEW-only entry cannot be explained by minute-quantisation the way
        // an OLD-only one can (compare_count()'s minute-granular now_ms vs.
        // millis()-based aging) -- NEW's own window check already runs at
        // that same minute granularity, so a call appearing there that OLD
        // does not yet/no longer have is either CAPACITY or a real,
        // reportable gap, never a quantisation wash.
        Category c = (new_full && !old_full) ? CAT_CAPACITY : CAT_OTHER;
        hb.cat[c]++;
        char buf[160];
        snprintf(buf, sizeof(buf), "min=%u win=%u NEW-only call=%s age_min=%u", (unsigned)now_min,
                 (unsigned)window_min, kv.first.c_str(), (unsigned)kv.second);
        report_example(r, c, buf);
    }

    if (mismatch)
    {
        hb.minutes_set_mismatch++;
        r.total_set_mismatch_minutes++;
    }
}

static void compare_count(Report &r, HourBucket &hb, uint16_t now_min)
{
    (void)r;
    int old_count = getMheardCount();
    int new_count = nbrMhCount(g_topo, now_min, 60);
    if (old_count != new_count)
        hb.count_mismatch++;
}

// Field comparison for entries present on both the 60-min old and new sets.
static void compare_fields(Report &r, HourBucket &hb, uint32_t now_ms, uint16_t now_min)
{
    uint8_t idxbuf[NBR_MAX_ROWS];
    int n = nbrMhRows(g_topo, now_min, 60, idxbuf, NBR_MAX_ROWS);
    for (int i = 0; i < n && i < NBR_MAX_ROWS; i++)
    {
        NbrMhView v;
        if (!nbrMhGet(g_topo, idxbuf[i], now_min, &v))
            continue;
        int op = -1;
        for (int j = 0; j < MAX_MHEARD; j++)
            if (mheard_fresh(j, now_ms, 60UL * 60UL * 1000UL) && is_equ(mheardCalls[j], v.call))
            {
                op = j;
                break;
            }
        if (op < 0)
            continue; // set mismatch already reported by compare_set()

        const MheardRecord &rec = mheardRecords[op];

        // B2-explainable fields (orchestrator follow-up: narrow CAT_B2 to
        // what updateHeyPath()'s REP-action actually overwrites -- plt/mesh/
        // date-time/ncnt/pl, mheard_functions.cpp read-only reference; it
        // explicitly RESTORES hw/mod/rssi/snr/dist from the previous record
        // afterward, so a mismatch there is never B2, see below).
        bool b2_bad = false;
        if ((char)rec.mr_type != v.plt)
            b2_bad = true;
        if (rec.mr_mesh != v.mesh)
            b2_bad = true;
        // ncnt is deliberately NOT compared per-entry (tried both mr_ncount
        // and mheardNCount[op] while building this: both threw false
        // positives unrelated to B2). Root cause found empirically (full-
        // window run, DL2JA-2 from min=2): NEW's row-level ncnt can be
        // learned from a nbrNoteNcnt() call BEFORE the call ever becomes a
        // "direct" MHeard-equivalent entry -- nbrNoteFrame() creates a row
        // for a 2-hop-window PARTICIPANT (e.g. an intermediate relay hop in
        // someone else's chain) well before that call is ever directly
        // heard, and nbrNoteNcnt()'s own contract ("ohne Zeile folgenlos")
        // only needs a row, not a direct entry. OLD's mheardNCount[], by
        // contrast, is only ever written once the call has its own MHeard
        // SLOT, which updateMheard() only creates on a DIRECT hearing
        // (mh_callsign == source_last). A call can therefore carry a real,
        // reported NCNT on the NEW side for many minutes before OLD's slot
        // exists at all to receive it -- a genuine population-order
        // difference between the two designs, not a B2 effect and not a
        // bug either side. The aggregate NCNT table below (sums over the
        // whole replay) is the sound comparison for this field; a per-entry
        // one is not.
        // pl (mr_path_len/v.pl): stays N/A on both sides (see file header --
        // msg_last_path_cnt is not in the [LOG] trailer this harness parses,
        // and this replay writes 0 for it on both sides identically), so it
        // is deliberately not compared here; a real 0-vs-nonzero mismatch
        // would be a harness artifact, not evidence either way.

        // date/time: NEW has no absolute date/time field, only age_min
        // against nbrBootEpoch() (nbr_matrix.h) -- reconstruct it and
        // compare to OLD's mr_year/month/day/hour/minute/second (2 min
        // tolerance for the two independent quantisations: OLD ages by
        // mheardMillis()/millis(), NEW by now_min/age_min).
        bool datetime_bad = false;
        uint32_t boot_epoch = nbrBootEpoch(g_topo);
        if (boot_epoch != 0)
        {
            time_t new_epoch = (time_t)boot_epoch + (time_t)((int)now_min - (int)v.age_min) * 60;
            struct tm ntm;
            gmtime_r(&new_epoch, &ntm);
            struct tm otm;
            memset(&otm, 0, sizeof(otm));
            otm.tm_year = (2000 + rec.mr_year) - 1900;
            otm.tm_mon = rec.mr_month - 1;
            otm.tm_mday = rec.mr_day;
            otm.tm_hour = rec.mr_hour;
            otm.tm_min = rec.mr_minute;
            otm.tm_sec = rec.mr_second;
            time_t old_epoch = timegm(&otm);
            long diff = (long)new_epoch - (long)old_epoch;
            if (diff < 0)
                diff = -diff;
            if (diff > 120)
                datetime_bad = true;
        }

        // hw/mod/rssi: B2 NEVER touches these (explicitly restored from the
        // previous record) -- a mismatch here is unexplained by B2 and goes
        // straight to OTHER, never lumped in with a real B2 minute.
        bool other_bad = false;
        if (rec.mr_hw != v.hw)
            other_bad = true;
        if (rec.mr_mod != v.mod)
            other_bad = true;
        if (rec.mr_rssi != v.rssi)
            other_bad = true;

        // DIST (orchestrator follow-up 3, "optional if cheap"): own, own reasoning
        // (both sides computed by THIS harness's own nbrDistKm(), see
        // g_own_lat's comment) -- tracked separately, never folded into
        // field_mismatch/bad above, since it is a harness approximation, not
        // a byte-exact reproduction of either production formula.
        if (g_own_pos_known && rec.mr_dist >= 0.0f && !isnan(v.lat) && !isnan(v.lon))
        {
            float new_dist = round_dist_km(nbrDistKm((float)g_own_lat, (float)g_own_lon, v.lat, v.lon));
            r.dist_compared++;
            if (fabsf(new_dist - rec.mr_dist) < 0.05f)
                r.dist_agree++;
            else if (r.dist_examples.size() < 10)
            {
                char dbuf[160];
                snprintf(dbuf, sizeof(dbuf), "min=%u call=%s OLD=%.1f NEW=%.1f",
                         (unsigned)now_min, v.call, rec.mr_dist, new_dist);
                r.dist_examples.push_back(dbuf);
            }
        }

        // SNR differs BY DESIGN (8-frame mean vs. last value) -- reported
        // separately below, never counted as a mismatch.
        if (b2_bad || datetime_bad || other_bad)
        {
            hb.field_mismatch++;
            // other_bad is never B2 (see its own comment); b2_bad/datetime_bad
            // are B2 only on a call the REP-action actually touched this
            // replay, otherwise unexplained (OTHER).
            Category c = (!other_bad && g_b2_touched.count(v.call)) ? CAT_B2 : CAT_OTHER;
            hb.cat[c]++;
            char buf[240];
            snprintf(buf, sizeof(buf),
                     "min=%u call=%s b2_bad=%d datetime_bad=%d other_bad=%d "
                     "OLD(plt=%c hw=%u mod=%u rssi=%d mesh=%u ncnt=%d %04u-%02u-%02u %02u:%02u:%02u) "
                     "NEW(plt=%c hw=%u mod=%u rssi=%d mesh=%u ncnt=%u age_min=%u)",
                     (unsigned)now_min, v.call, (int)b2_bad, (int)datetime_bad, (int)other_bad,
                     rec.mr_type, rec.mr_hw, rec.mr_mod, rec.mr_rssi, (unsigned)rec.mr_mesh, mheardNCount[op],
                     2000 + rec.mr_year, rec.mr_month, rec.mr_day, rec.mr_hour, rec.mr_minute, rec.mr_second,
                     v.plt, v.hw, v.mod, v.rssi, (unsigned)v.mesh, v.ncnt, (unsigned)v.age_min);
            report_example(r, c, buf);
        }
    }
}

// Path table hop-count agreement (brief task 4, "path table" bullet): every
// sender in the old path table must appear on the new side either as a
// 2-hop row or a horizon entry, never both.
// Brief task 4's numeric hop-count comparison ("report hop-count agreement")
// needs mheardLine.mh_path_len -- msg_last_path_cnt, which the [LOG] trailer
// this harness parses does not carry (see the file header's DIST/PL/SNR N/A
// note). What IS checkable without it, and is checked here: the brief's
// structural rule -- every sender in the old path table appears on the new
// side either as a 2-hop row or a horizon entry, NEVER both -- plus whether
// it is visible on the new side at all (path_hop_agree) or not
// (path_hop_disagree, covered again by the MHeard-set comparison for the
// direct-entry case, so this isolates the 3+-hop / horizon-only senders).
// Splits s on ',' and returns the token count (1 for an empty/no-comma
// string, matching how a bare callsign with no relay suffix counts as a
// single-token path).
static int count_tokens(const std::string &s)
{
    if (s.empty())
        return 0;
    int n = 1;
    for (char c : s)
        if (c == ',')
            n++;
    return n;
}

static bool path_contains_call(const std::string &full_path, const char *call)
{
    size_t pos = 0;
    while (true)
    {
        size_t comma = full_path.find(',', pos);
        std::string tok = full_path.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (tok == call)
            return true;
        if (comma == std::string::npos)
            return false;
        pos = comma + 1;
    }
}

static const uint32_t PATH_AGE_EDGE_MS = 2UL * 60UL * 1000UL;

static void compare_path_table(Report &r, uint32_t now_ms, uint16_t now_min)
{
    // Horizon-side fullness: count LIVE horizon-only route entries (there is
    // no direct "is the horizon full" accessor in nbr_views.h, so this counts
    // the same way the acceptance check below does, once, up front).
    int horizon_live = 0;
    {
        int rn = nbrRouteCount(g_topo, now_min);
        for (int j = 0; j < rn; j++)
        {
            NbrRouteView rv;
            if (nbrRouteGet(g_topo, j, now_min, &rv) && !rv.is_row)
                horizon_live++;
        }
    }
    bool row_table_full = nbrRowsUsed(g_topo) >= NBR_MAX_ROWS;
    bool horizon_full = horizon_live >= NBR_HZ_ENTRIES;

    for (int i = 0; i < MAX_MHPATH; i++)
    {
        if (mheardPathCalls[i][0] == 0)
            continue;
        if (!mheardPathFreshMs(i, 12UL * 60UL * 60UL * 1000UL))
            continue;

        bool found_row = false, found_horizon = false;
        int rn = nbrRouteCount(g_topo, now_min);
        for (int j = 0; j < rn; j++)
        {
            NbrRouteView rv;
            if (!nbrRouteGet(g_topo, j, now_min, &rv))
                continue;
            if (!is_equ(rv.call, mheardPathCalls[i]))
                continue;
            if (rv.is_row)
                found_row = true;
            else
                found_horizon = true;
        }
        if (found_row && found_horizon)
        {
            r.path_hop_disagree++; // "never both" violated -- its own bucket, not part of the MISSING categorisation below
            continue;
        }
        if (found_row || found_horizon)
        {
            r.path_hop_agree++;
            continue;
        }

        // MISSING: categorise why.
        std::string sender(mheardPathCalls[i]);
        std::string suffix((const char *)mheardPathBuffer1[i]);
        // Reconstruction of updateHeyPath()'s own storage split (mheard_functions.cpp,
        // read-only reference): mheardPathCalls[i] is the originator (first token),
        // mheardPathBuffer1[i] is everything AFTER the first comma of the original
        // msg_source_path -- concatenating them with ',' recovers that original path.
        std::string full_path = suffix.empty() ? sender : (sender + "," + suffix);

        // DIRECT: does this sender have a fresh direct row (cell[sender][0])
        // right now? Routes deliberately exclude direct rows (Konzept 4.7);
        // MHeard itself shows the sender there too, so this is not a gap.
        bool is_direct = false;
        {
            uint8_t idxbuf[NBR_MAX_ROWS];
            int n = nbrMhRows(g_topo, now_min, 720, idxbuf, NBR_MAX_ROWS);
            for (int k = 0; k < n && k < NBR_MAX_ROWS; k++)
            {
                NbrMhView v;
                if (nbrMhGet(g_topo, idxbuf[k], now_min, &v) && is_equ(v.call, sender.c_str()))
                {
                    is_direct = true;
                    break;
                }
            }
        }

        uint32_t age_ms = now_ms - mheardPathMillis[i];
        uint32_t window_12h_ms = 12UL * 60UL * 60UL * 1000UL;

        PathCat cat;
        if (is_direct)
            cat = PCAT_DIRECT;
        else if (is_equ(sender.c_str(), OWN_CALL))
            cat = PCAT_OWN_SENDER;
        else if (path_contains_call(full_path, OWN_CALL))
            cat = PCAT_OWN_ELSEWHERE;
        else if (count_tokens(full_path) < 3)
        {
            cat = PCAT_SHORT;

            // Sub-categorise (see ShortSubCat's own comment).
            ShortSubCat sub;
            if (count_tokens(full_path) < 2)
                sub = SHORT_ONE_TOKEN;
            else
            {
                int a_idx = nbrFind(g_topo, sender.c_str());
                if (a_idx < 0)
                    sub = SHORT_A_NO_ROW;
                else
                {
                    NbrEdgeView a0ev;
                    bool a_direct_recheck = nbrEdgeGet(g_topo, a_idx, 0, &a0ev) && nbrFresh(a0ev.last_min, now_min);
                    if (a_direct_recheck)
                        sub = SHORT_A_DIRECT;
                    else
                    {
                        // Broad scan (not just the stored B): does ANY row M
                        // have a fresh edge (A,M) AND is M itself a fresh
                        // direct neighbour right now? If so, Konzept 4.7's
                        // own rule says A should be a 2-hop row via M --
                        // nbrRouteGet() not showing it is a real gap.
                        bool any_qualifying = false;
                        int qualifying_m = -1;
                        for (int m = 0; m < NBR_MAX_ROWS; m++)
                        {
                            if (m == a_idx)
                                continue;
                            NbrEdgeView em;
                            if (!nbrEdgeGet(g_topo, a_idx, m, &em) || !nbrFresh(em.last_min, now_min))
                                continue;
                            NbrEdgeView em0;
                            if (nbrEdgeGet(g_topo, m, 0, &em0) && nbrFresh(em0.last_min, now_min))
                            {
                                any_qualifying = true;
                                qualifying_m = m;
                                break;
                            }
                        }
                        if (any_qualifying)
                        {
                            sub = SHORT_OTHER;
                            NbrRowView mv;
                            char mcall[NBR_CALL_LEN] = "?";
                            if (nbrRowGet(g_topo, qualifying_m, &mv))
                                mcSet(mcall, sizeof(mcall), mv.call);
                            char buf[240];
                            snprintf(buf, sizeof(buf),
                                     "min=%u A=%s stored_B=%s qualifying_M=%s (edge(A,M) and M direct both fresh, "
                                     "but nbrRouteGet() shows neither row nor horizon for A)",
                                     (unsigned)now_min, sender.c_str(), suffix.c_str(), mcall);
                            if (r.short_sub_examples[SHORT_OTHER].size() < 3)
                                r.short_sub_examples[SHORT_OTHER].push_back(buf);
                        }
                        else
                        {
                            int b_idx = nbrFind(g_topo, suffix.c_str());
                            if (b_idx < 0)
                                sub = SHORT_B_NO_ROW;
                            else
                            {
                                NbrEdgeView ev;
                                bool edge_ok = nbrEdgeGet(g_topo, a_idx, b_idx, &ev) && nbrFresh(ev.last_min, now_min);
                                sub = edge_ok ? SHORT_B_NOT_DIRECT : SHORT_EDGE_NOT_FRESH;
                            }
                        }
                    }
                }
            }
            r.short_sub[sub]++;
            if (sub != SHORT_OTHER && r.short_sub_examples[sub].size() < 2)
            {
                char buf[220];
                snprintf(buf, sizeof(buf), "min=%u A=%s stored_B=%s age_ms=%u",
                         (unsigned)now_min, sender.c_str(), suffix.c_str(), (unsigned)age_ms);
                r.short_sub_examples[sub].push_back(buf);
            }
        }
        else if (window_12h_ms > age_ms && (window_12h_ms - age_ms) <= PATH_AGE_EDGE_MS)
            cat = PCAT_AGE;
        else if (horizon_full || row_table_full)
            cat = PCAT_EVICT_H;
        else
            cat = PCAT_OTHER;

        r.path_cat[cat]++;
        if (r.path_examples[cat].size() < 3)
        {
            char buf[220];
            snprintf(buf, sizeof(buf), "min=%u sender=%s full_path=%s tokens=%d age_ms=%u",
                     (unsigned)now_min, sender.c_str(), full_path.c_str(), count_tokens(full_path),
                     (unsigned)age_ms);
            r.path_examples[cat].push_back(buf);
        }
    }
}

static Report g_report;

static void per_minute_check(uint16_t now_min)
{
    if (now_min == g_last_now_min)
        return;
    g_last_now_min = now_min;

    nbrSweep(g_topo, now_min);

    uint32_t now_ms = (uint32_t)now_min * 60000UL;
    int hour_idx = now_min / 60;
    HourBucket &hb = g_report.hours[hour_idx];

    compare_set(g_report, hb, now_ms, now_min, 60, 60UL * 60UL * 1000UL);
    compare_set(g_report, hb, now_ms, now_min, 720, 12UL * 60UL * 60UL * 1000UL);
    compare_count(g_report, hb, now_min);
    compare_fields(g_report, hb, now_ms, now_min);
    compare_path_table(g_report, now_ms, now_min);

    g_report.ncnt_rows_by_hour_topo += nbrNcnt(g_topo, now_min);
    g_report.ncnt_rows_by_hour_mheard += getMheardCount();

    hb.minutes++;
    g_report.total_minutes++;
}

// ============================================================================
// Driver: replay one concatenated stream (one or more files, in order)
// ============================================================================

static void replay_stream(const std::vector<std::string> &paths, Report &out)
{
    (void)out;
    reinit_both();
    g_have_frame = false;
    g_last_t_ms = 0;

    for (const auto &path : paths)
    {
        std::ifstream f(path);
        if (!f.is_open())
            continue;
        std::string line;
        while (std::getline(f, line))
        {
            LogFrame lf;
            if (!parse_log_line(line, lf))
                continue;

            if (g_have_frame && lf.t_ms < g_last_t_ms)
                reinit_both();
            g_have_frame = true;
            g_last_t_ms = lf.t_ms;

            mc_test_set_millis(lf.t_ms);
            per_minute_check(lf.now_min);
            replay_frame(lf);
        }
    }
    // final minute
    if (g_have_frame)
        per_minute_check((uint16_t)(g_last_t_ms / 60000UL));
}

static void print_report(const Report &r, const char *label)
{
    printf("\n=== test_topo_shadow: %s ===\n", label);
    printf("minutes replayed: %ld, minutes with a SET mismatch: %ld (%.2f%%)\n",
           r.total_minutes, r.total_set_mismatch_minutes,
           r.total_minutes ? 100.0 * (double)r.total_set_mismatch_minutes / (double)r.total_minutes : 0.0);

    printf("hour | minutes | set_mm | count_mm | field_mm | CAPACITY | WINDOW_EDGE | DROP_TOKLOOP | HN | B2 | OTHER\n");
    for (const auto &kv : r.hours)
    {
        const HourBucket &hb = kv.second;
        printf("%4d | %7ld | %6ld | %8ld | %8ld | %8ld | %11ld | %12ld | %2ld | %2ld | %5ld\n",
               kv.first, hb.minutes, hb.minutes_set_mismatch, hb.count_mismatch, hb.field_mismatch,
               hb.cat[CAT_CAPACITY], hb.cat[CAT_WINDOW_EDGE], hb.cat[CAT_DROP_TOKLOOP],
               hb.cat[CAT_HN], hb.cat[CAT_B2], hb.cat[CAT_OTHER]);
    }

    for (int c = 0; c < CAT_COUNT; c++)
    {
        if (r.examples[c].empty())
            continue;
        printf("-- %s examples (first %zu) --\n", CAT_NAME[c], r.examples[c].size());
        for (const auto &ex : r.examples[c])
            printf("   %s\n", ex.c_str());
    }

    printf("path table: %ld visible (row XOR horizon), %ld both-row-and-horizon "
           "(numeric hop-count agreement is N/A -- PL not in the [LOG] trailer, see file header)\n",
           r.path_hop_agree, r.path_hop_disagree);
    printf("path table MISSING (sender neither row nor horizon), categorised:\n");
    printf("  %-10s %8s\n", "category", "count");
    for (int c = 0; c < PCAT_COUNT; c++)
        printf("  %-10s %8ld\n", PCAT_NAME[c], r.path_cat[c]);
    for (int c = 0; c < PCAT_COUNT; c++)
    {
        if (r.path_examples[c].empty())
            continue;
        printf("-- path %s examples (first %zu) --\n", PCAT_NAME[c], r.path_examples[c].size());
        for (const auto &ex : r.path_examples[c])
            printf("   %s\n", ex.c_str());
    }
    if (r.path_cat[PCAT_SHORT] > 0)
    {
        printf("SHORT sub-categories (why does the new side show neither row nor horizon for a 2-token path):\n");
        printf("  %-16s %8s\n", "sub-category", "count");
        for (int s = 0; s < SHORT_SUBCAT_COUNT; s++)
            printf("  %-16s %8ld\n", SHORT_SUBCAT_NAME[s], r.short_sub[s]);
        for (int s = 0; s < SHORT_SUBCAT_COUNT; s++)
        {
            if (r.short_sub_examples[s].empty())
                continue;
            printf("-- SHORT/%s examples (first %zu) --\n", SHORT_SUBCAT_NAME[s], r.short_sub_examples[s].size());
            for (const auto &ex : r.short_sub_examples[s])
                printf("   %s\n", ex.c_str());
        }
    }
    printf("NCNT sum over replay: topo=%ld mheard(getMheardCount)=%ld "
           "(definitions differ by design, see file header -- table only, not a mismatch)\n",
           r.ncnt_rows_by_hour_topo, r.ncnt_rows_by_hour_mheard);
    if (g_own_pos_known)
    {
        printf("DIST (own position learned at lat=%.4f lon=%.4f, both sides via this harness's own "
               "nbrDistKm(), rounded 0.1 km): %ld compared, %ld agree (%.2f%%)\n",
               g_own_lat, g_own_lon, r.dist_compared, r.dist_agree,
               r.dist_compared ? 100.0 * (double)r.dist_agree / (double)r.dist_compared : 0.0);
        for (const auto &ex : r.dist_examples)
            printf("   DIST disagree: %s\n", ex.c_str());
    }
    else
    {
        printf("DIST: N/A this run -- own position never seen (no OWN_CALL '!' frame in this fixture).\n");
    }
    printf("PL/SNR: N/A by design -- PL (msg_last_path_cnt) is not carried in the [LOG] trailer this "
           "harness parses; SNR differs by design (8-frame mean vs. last value).\n");
}

// ============================================================================
// Unity tests
// ============================================================================

void setUp(void) {}
void tearDown(void) {}

static void test_checked_in_fixture(void)
{
    g_report = Report();
    std::vector<std::string> paths = {"test/test_topo_shadow/fixtures/dk5en98-20260924.txt"};
    replay_stream(paths, g_report);
    print_report(g_report, "checked-in fixture (2026-09-24 only)");

    TEST_ASSERT_GREATER_THAN(0, g_report.total_minutes);

    // Brief W3b: "Assert OTHER == 0". Verified against a real build once
    // W3a's contract landed (src/nbr_matrix.cpp's Welle-3 additions,
    // src/nbr_views.cpp): every field mismatch in this fixture is B2 (see
    // g_b2_touched's own comment) and every MHeard-set mismatch is 0 for
    // this particular (single-boot, no capacity pressure) fixture.
    long other_total = 0;
    for (const auto &kv : g_report.hours)
        other_total += kv.second.cat[CAT_OTHER];
    TEST_ASSERT_EQUAL(0, other_total);

    // Orchestrator follow-up 1: "every sender of the old path table appears
    // as row or horizon entry" -- path_cat[PCAT_OTHER] is the unexplained
    // remainder after DIRECT/SHORT/OWN_SENDER/OWN_ELSEWHERE/AGE/EVICT_H/TEXT.
    TEST_ASSERT_EQUAL(0, g_report.path_cat[PCAT_OTHER]);
    // path_hop_disagree ("both row and horizon", Konzept's own "never both"
    // rule) is exactly the class of bug W3a was fixing concurrently with
    // this test's own follow-ups -- expect 0 once that lands, may be > 0
    // otherwise; asserted (not just reported) because that is the criterion.
    TEST_ASSERT_EQUAL(0, g_report.path_hop_disagree);
    // short_sub[SHORT_OTHER]: the broad-scan real-gap detector inside SHORT
    // (see ShortSubCat's own comment) -- must never fire.
    TEST_ASSERT_EQUAL(0, g_report.short_sub[SHORT_OTHER]);
}

static void test_full_window_if_present(void)
{
    const char *raw[4] = {
        "2026-09-21.log", "2026-09-22.log", "2026-09-23.log", "2026-09-24.log"};
    std::vector<std::string> paths;
    const char *home = getenv("HOME");
    std::string dir = (home ? std::string(home) : std::string("")) + "/Downloads/dk5en-98-nbr/";
    bool all_present = true;
    for (int i = 0; i < 4; i++)
    {
        std::string p = dir + raw[i];
        std::ifstream f(p);
        if (!f.is_open())
        {
            all_present = false;
            break;
        }
        paths.push_back(p);
    }

    if (!all_present)
    {
        printf("\ntest_topo_shadow: full 21.-24.09. window not present at %s -- SKIPPED\n", dir.c_str());
        TEST_IGNORE_MESSAGE("full raw captures not present locally -- skipped, not failed");
        return;
    }

    g_report = Report();
    replay_stream(paths, g_report);
    print_report(g_report, "full window (2026-09-21..24, from ~/Downloads)");
    TEST_ASSERT_GREATER_THAN(0, g_report.total_minutes);

    // Orchestrator follow-up 4a: the full-window run gets the same OTHER==0
    // gate the checked-in fixture already has.
    long other_total = 0;
    for (const auto &kv : g_report.hours)
        other_total += kv.second.cat[CAT_OTHER];
    TEST_ASSERT_EQUAL(0, other_total);

    TEST_ASSERT_EQUAL(0, g_report.path_cat[PCAT_OTHER]);
    TEST_ASSERT_EQUAL(0, g_report.path_hop_disagree);
    TEST_ASSERT_EQUAL(0, g_report.short_sub[SHORT_OTHER]);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_checked_in_fixture);
    RUN_TEST(test_full_window_if_present);
    return UNITY_END();
}
