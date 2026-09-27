// MeshCom 5 topology, wave 1 (docs/meshcom5-campaign.md), brief W1a: a host
// replay harness that feeds a real field capture (DK5EN-98, cold boot
// 2026-09-23 20:02:19 through 2026-09-24 09:28, firmware 8487ea2a -- see
// ASSUMPTION 2 below for how the fixture was assembled from two raw days)
// through the neighbour matrix and checks that it reproduces the capture's
// [NBR] lines. Wave 1 wrote it against the dense matrix; since wave 2 the
// dense code lives on as a frozen reference (reference/nbr_matrix_dense.*)
// and the harness runs the SAME fixture through it and through the edge
// pool (src/nbr_matrix.cpp) and diffs the two "actual" streams -- see
// "Wave 2: differential harness" below.
//
// test_build_src=no (env native_nbr_replay, platformio.ini): the source
// comes in by #include, same pattern as test/test_nbr_matrix/test_nbr_matrix.cpp.
//
// --- What this harness drives, and what it takes on faith ------------------
//
// nbr_matrix.cpp's public functions are called directly, in the SAME order
// OnRxDone() (src/lora_functions.cpp) calls them, for each frame:
//
//   1. Stufe 2 cover-check (nbrCoverMask(), lora_functions.cpp:826-952) --
//      BEFORE Stufe 1, using the matrix state left over from the PREVIOUS
//      frame. Needs a tiny per-msg_id "ring" model (need/alone/counted/
//      active) that this harness owns; see RingEntry below. This is not a
//      TX-ring simulation (src/txring_functions.cpp is out of the file set
//      and not needed) -- only the four bits nbrCoverMask()'s caller reads.
//   2. Lazy nbrInit()/GW-flag (lora_functions.cpp:994-1005) -- skipped for
//      the one-time own-position branch (nbrNotePos() for the own call with
//      hw=0), see ASSUMPTION 2 below.
//   3. Stufe 1 (nbrNoteFrame() + nbrNotePos() for '!' frames,
//      lora_functions.cpp:1011-1039).
//   4. Relay decision (nbrRelayNeed(), lora_functions.cpp:1856-1947) --
//      ONLY when the capture itself shows a NEED line for this frame's
//      msg_id (whether a frame gets relayed at all depends on dedup/hop/
//      loop/gateway-filter logic in lora_functions.cpp that is entirely
//      outside nbr_matrix.cpp and outside this brief's file set). This is
//      the "drive nbrRelayNeed()/nbrCoverMask() at the points the capture
//      shows the decision, trigger taken from the log" tactic the brief
//      allows explicitly.
//   5. Own HN-report send (nbrBuildReport(), sendNbrReport() in
//      loop_functions.cpp:5246-5299) -- triggered the same way as (4),
//      at every RPTTX line, with heard_count (getMheardCount(), out of
//      the file set) taken from that same line's own payload rather than
//      reproduced.
//
// ASSUMPTIONS (flags/config inferred from the capture, cannot be read off
// a --info line since none is in the window):
//   - --nbrdebug on:    required for ANY [NBR] line to exist at all.
//   - --nbrrelay on:    the capture has CANCEL lines, never CANCEL? (which
//                       would mean --nbrrelay count); hardcoded as the "if
//                       (after == 0)" branch below always emitting CANCEL,
//                       never CANCEL?, no separate flag/constant needed.
//   - --nbrsym on:      the capture has hundreds of SYM lines; sym=false
//                       would produce none.
//   - bGATEWAY == true: the capture's [GW]/GWU/keep-alive traffic implies a
//                       gateway build; row 0's flags (dec 15 in the first
//                       SNAP, i.e. GW+MESH+POS+USED) require the GW bit.
//
// ASSUMPTION 2 -- the fixture now covers a COLD BOOT (revised after the
// first wave's run showed 9531/9554 mismatches against a mid-session
// fixture, dominated by REFUSE 327-vs-0 and CANCEL 97-vs-247 -- a sparse,
// freshly-nbrInit()'d matrix computes far more "no alternative" relay
// decisions than a matrix warm with hours of history):
//   The fixture is now dk5en98-20260923-boot.txt, produced by
//   tools/nbr_replay_extract.py from TWO raw captures concatenated: the
//   tail of 2026-09-23.log starting at its LAST millis() reset (line
//   208391, t: 2210320 -> 49716, wall-clock 20:02:19 -- confirmed the
//   right one: firmware 8487ea2a, the exact commit this nbr_matrix.cpp is
//   at, was flashed that evening, and the device ran without a further
//   reboot through the end of 2026-09-24.log at 09:28, up=795; the two
//   files' `up` values line up exactly across the midnight boundary in the
//   fixture, 238 at 23:59:58 and 238 again at 00:00:09) and all of
//   2026-09-24.log from its start. Two earlier resets in 2026-09-23.log
//   (line 108152 / 09:20, line 201934 / 19:24) are older firmware and are
//   correctly NOT the boot used here.
//
//   With the true boot in the window, nbrInit() at this harness's first
//   [LOG] line now lines up with the real device's own nbrInit() (both are
//   "the first qualifying frame after reset"), so row/cell history is no
//   longer invisible the way it was in wave 1's mid-session fixture.
//
//   The own-position lazy branch ("if(!(rows[0].flags&POS) &&
//   node_lat!=0.0) nbrNotePos(own_call,...,hw=0,...)",
//   lora_functions.cpp:1001-1003) is STILL not replayed here, but now for
//   an evidence-based reason rather than a pre-window guess: this harness
//   searched the ENTIRE fixture (both files) for a "[NBR]|POS|...|
//   DK5EN-98|...|<mesh>|0" line (hw=0, the lazy branch's own literal
//   argument) and found none -- every one of the 53 own-POS lines in the
//   fixture has hw=43, the value from an ECHOED beacon (the generic
//   per-row POS path, lora_functions.cpp:1022-1038, which nbrNotePos()
//   already exercises identically for every other call sign). The first
//   own-POS line is at up=2 (20:04:13), a mere two minutes after boot,
//   from that generic echo path -- not from the lazy branch. Read
//   together with the frame trace (the very first processed frame, up=0,
//   is itself the device hearing DL2UD-1 relay its own R1; HEY beacon
//   back, "OWN:e"), the simplest explanation is that node_lat was still
//   0.0 (no GPS fix yet, or a fix pending) for the short window between
//   boot and that first echo, so the lazy branch's own guard genuinely
//   never became true -- there is no lat/lon value on record anywhere in
//   the capture (no --info dump in-window) other than the 48.40783/
//   11.73800 the echo path itself carries, which this harness already
//   reproduces on the SAME first-echo frame the real device did. Firing
//   the lazy branch unconditionally here (assuming node_lat != 0.0) would
//   reintroduce exactly the kind of fabricated line wave 1 avoided.
//
// ASSUMPTION 2b -- wave 2's first run (12389/12265 mismatched LINES, vs.
// wave 1's 9531/9442) traced the residual gap to something narrower than
// "no true boot": the CAPTURE TOOL itself has an ~80 s blind spot right at
// this exact reboot. The tail of 2026-09-23.log around line 208391 reads:
//     20:00:57.928  [LOGGER] connection lost ([Errno 104] ...)
//     20:01:03/17/37 [LOGGER] connection lost (Connection refused), retry
//                    in 5/10/20/40 s (reconnect backoff)
//     20:02:17.699  [LOGGER] connected to dk5en-98.local:2323
//     20:02:19.700  [LOGGER] flags re-applied after reconnect
// -- the flash+reboot dropped the logger's TCP session at 20:00:57.9, and
// nothing the device printed between then and 20:02:17.7 was ever
// captured, in ANY raw file -- there is no earlier line to start from.
// The device's own t= trail confirms real activity in that gap: the very
// first frame this harness sees after boot (up=1, msg E9FB734A) already
// has an EDGE between two calls that share no earlier visible frame, and
// RING_TX_READ's own "lat=24210ms" for that msg_id (line 208439) places
// its original reception at device uptime ~38 s -- inside the unobserved
// window (boot ~20:01:30, first captured line at uptime 49716 ms). This
// harness's nbrInit() is therefore a few seconds and a handful of frames
// short of the device's true one, not wave 1's ~4 h -- but still enough
// for the first few rows/edges to diverge, and a stateful matrix does not
// self-correct from a wrong start over a 9+ h replay (EVICT's victim,
// CUT's window boundary, NEED/CANCEL's alternatives all key off WHICH
// rows already exist).
//
// ASSUMPTION 2c (wave 3 revision) -- a flat, positional line-by-line diff
// (wave 2's compare_streams(), now removed) could not tell "divergence"
// apart from "the SAME divergence shifting every later line by one slot":
// EDGE/NEED/ROW's total COUNTS already matched near-exactly (3312/3312,
// 947/947, 1041/1041) while the line-by-line mismatch rate stayed ~101%,
// which is the signature of positional drift, not real disagreement. Two
// changes replace that comparator (both below):
//   - GROUPED, multiset comparison: every [NBR] line is attributed to the
//     TRIGGER that caused it (a [LOG] frame's own EDGE/ME/CUT/DROP/EVICT/
//     POS/SYM/NEED/CANCEL/REFUSE; a SNAP..ENDSNAP block; an RPTTX) --
//     TriggerGroup below. A group compares as a SORTED (order-within-
//     group-insensitive) list of its own lines, byte-exact, so one frame
//     producing its lines in a harmlessly different order no longer
//     misaligns every group after it.
//   - TWO comparison modes per group, EXACT and INDEX-FREE (see
//     normalize_exact()/normalize_indexfree() below): index-free rewrites
//     ROW's/EVICT's <idx> field (a row-table SLOT, meaningless on its own)
//     to a placeholder, and NEED's/CANCEL's/REFUSE's hex bitmasks (which
//     encode SETS of row indices) into SORTED CALLSIGN LISTS, so a group
//     that names the same neighbours through a different row allocation
//     (the direct, mechanical fallout of ASSUMPTION 2b's missed frames --
//     a row's INDEX depends on allocation history, a row's IDENTITY does
//     not) compares equal. The callsign lookup itself is necessarily
//     approximate on the EXPECTED side (there is no live matrix to read;
//     this harness reconstructs a best-effort idx->call table from the
//     capture's own ROW/EVICT lines as it replays, snapshotted once per
//     group right after that group's own row-mutating call -- a NEW row
//     from a free slot, which the capture never logs on its own, stays
//     "?<idx>" until the next SNAP names it) -- on the ACTUAL side it is
//     exact (read directly from the live NbrMatrix).
// The per-hour table below (both modes) is the sanity check this framing
// predicts: NBR_WINDOW_MIN (720 min, nbr_matrix.h) bounds how long any
// state this harness never saw can keep influencing decisions, so
// mismatches (real ones, under index-free) should concentrate in the
// first ~12 h and taper toward zero after -- not stay flat across a
// 13.5 h capture the way a positional artifact would.
//
// ASSUMPTION 2d -- the actual post-convergence run (95/384 groups still
// mismatched, index-free) shows exactly that concentration, but not a
// clean drop to zero, and the per-hour table shows why NBR_WINDOW_MIN
// does not fully clear it: EDGE/ME/CUT's trailing <cnt> is a SATURATING
// per-cell counter (nbrHitCell(), nbr_matrix.cpp) that resets to 0 only
// when the cell itself goes STALE (unfreshened for >=NBR_WINDOW_MIN) --
// it does NOT reset on a wall-clock timer. Every post-convergence failure
// sampled (the 10 printed by the test) is the SAME shape: identical
// content, a same-cell <cnt> off by a small, bounded amount (1-3; several
// ARE equal because both sides had already saturated at 255), sometimes
// with one extra/missing CANCEL from the Stufe-2 ring desyncing by that
// same margin. A handful of extra touches ASSUMPTION 2b's boot gap gave
// the real device (and this harness structurally cannot replay) stay on
// a BUSY, continuously-refreshed edge's counter for as long as that edge
// keeps being heard -- which, for the network's core relay pairs
// (DL2JA-2<->DB0ED-99, DL2UD-1's, ...), is the whole 13.5 h capture, not
// just 12 h. NBR_WINDOW_MIN bounds how long a cell's PRESENCE (did this
// pair ever hear each other) can misrepresent the boot gap; it does not
// bound a live cell's COUNT. Index-free mode still roughly halves the
// mismatched-group rate against exact mode (803 vs. 1307 of 3483 groups
// overall) by removing the row-allocation noise on top of this counter
// drift -- the remaining ~25% post-convergence rate is this counter
// effect, not a further comparator artifact. Closing it needs either the
// gap-free capture ASSUMPTION 2b already asked for, or teaching the
// index-free comparator to tolerate a small bounded <cnt> delta the way
// it already tolerates row identity -- not done here (would blur the line
// between "reported" and "silently loosened"); left for whoever reviews
// this before deciding whether wave 2's edge-pool diff needs it.
//
// The [NBR]|NEED|...|<slot>|... field stays the one field-level exception
// in BOTH modes (not silently -- normalize_exact()/normalize_indexfree()
// both replace it with a fixed placeholder, pinned by its own unit test):
// <slot> is the TX ring index addTxRingEntry() (src/txring_functions.cpp,
// out of file set) happened to hand back, not something nbr_matrix.cpp
// computes.
//
// Position decode: '!' frames carry lat/lon as an uncompressed APRS
// "DDMM.hhN/DDDMM.hhE" pair (aprs_functions.cpp's decodeAPRSPOS() /
// conv_coord_to_dec(), both out of the file set). Reimplementing that exact
// arithmetic here (not calling it) keeps this harness inside its own file
// set while still turning every POS line into a real, compared assertion
// instead of an input copied from the fixture's own answer.
//
// --- Wave 2: differential harness (dense reference vs. edge pool) -----------
//
// The frozen dense implementation (886080c4, test/test_nbr_replay/reference/)
// and the edge pool (src/nbr_matrix.cpp) are compiled into THIS binary, each
// in its own namespace: `dense` (always 21 rows, the S3 build that produced
// the capture) and `edge` (the env's NBR_MAX_ROWS/EDGES and rules). The
// production envs (NBR_REPLAY_PRODUCTION) add two more copies of the edge
// pool with rules switched off -- `edge_noshare` (NBR_SHARE_PCT 0) and
// `edge_norules` (additionally NBR_CNT_HALVE_MIN 0, NBR_SNR_AVG_N 1) -- which
// only exist to attribute each dense/production difference to a cause.
// run_replay<Adapter>() drives any of them through the same fixture; the
// adapters below hide the two APIs (uint32 masks and rows[]/cells[] vs.
// NbrMask and accessors). nbr_matrix.cpp never calls one of its own public
// functions internally, which is what makes it includable in a namespace.
//
// Tests:
//   compat env (native_nbr_replay: 21 rows, share 0, no halving, SNR last
//   value, 441 edges = never full):
//     B  dense vs. edge (no sweep), line for line over every group, after
//        only two normalisations: EDGE/ME <cnt> blanked (one counter vs.
//        per-type counters), mask hex compared on its low 32 bits.
//     S  edge swept every minute vs. edge never swept: identical, after
//        blanking SNAP <cells> (stale edges stay in the pool without sweep).
//     the wave-1 capture comparison (DECISION mode, 0 mismatches from up 180)
//        on the edge run.
//   production envs (64/128 rows, share 10 %, halving 90 min, SNR mean 8,
//   swept every minute like the firmware):
//     C  every decision that differs from dense (NEED case/need/alone,
//        ROW verdict/meshneed, E_self), with a category; OTHER must be 0;
//        the DB0ED-99/DB0FHR-12 check from concept 4.3.
//     D  NbrMask operations on the upper rows.
//   wave 3: B, B' and the capture comparison first remove the ME_DECOUPLED
//   category (see strip_me_decoupled()), count it, and assert that no
//   stage-2 line type (EVICT-H/EVICT-X/ECHO) appears on the compat replay.

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#include "configuration_default.h"
#include "nbr_matrix.h"   // global types: NbrMatrix, NbrMask, ... (env rows)

// Dense reference, always 21 rows (it cannot be anything else: its masks are
// uint32 and nbrBit() drops every index >= 32).
#define NBR_DENSE_ROWS 21
#pragma push_macro("NBR_MAX_ROWS")
#undef NBR_MAX_ROWS
#define NBR_MAX_ROWS NBR_DENSE_ROWS
#define NBR_DENSE_REFERENCE_TU 1
namespace dense
{
#include "reference/nbr_matrix_dense.cpp"
}
#undef NBR_DENSE_REFERENCE_TU
#pragma pop_macro("NBR_MAX_ROWS")

#ifndef NBR_MATRIX_SRC
#define NBR_MATRIX_SRC "../../src/nbr_matrix.cpp"
#endif

namespace edge
{
#include NBR_MATRIX_SRC
}

#ifndef NBR_REPLAY_PRODUCTION
// The nRF52 path of the SYM lines (recorded under the clamp, printed after
// it) on the host: same rules, NBR_DEFER_LOG forced on. B runs it too.
#undef NBR_DEFER_LOG
#undef NBR_GEN_BUMP
#define NBR_DEFER_LOG 1
namespace edge_deferred
{
#include NBR_MATRIX_SRC
}
#undef NBR_DEFER_LOG
#undef NBR_GEN_BUMP
#endif

#ifdef NBR_REPLAY_PRODUCTION
#pragma push_macro("NBR_SHARE_PCT")
#pragma push_macro("NBR_CNT_HALVE_MIN")
#pragma push_macro("NBR_SNR_AVG_N")
#undef NBR_SHARE_PCT
#define NBR_SHARE_PCT 0
namespace edge_noshare
{
#include NBR_MATRIX_SRC
}
#undef NBR_CNT_HALVE_MIN
#define NBR_CNT_HALVE_MIN 0
#undef NBR_SNR_AVG_N
#define NBR_SNR_AVG_N 1
namespace edge_norules
{
#include NBR_MATRIX_SRC
}
#pragma pop_macro("NBR_SNR_AVG_N")
#pragma pop_macro("NBR_CNT_HALVE_MIN")
#pragma pop_macro("NBR_SHARE_PCT")
#endif

// --- fixture location, same pattern as test_command_toggles.cpp ------------

static bool path_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

static std::string repo_root()
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_nbr_replay/test_nbr_replay.cpp";

    if (!file.empty() && file[0] == '/')
    {
        TEST_ASSERT_TRUE_MESSAGE(
            file.size() > suffix.size() &&
                file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0,
            ("unexpected absolute __FILE__ shape: " + file).c_str());
        std::string root = file.substr(0, file.size() - suffix.size());
        while (!root.empty() && root.back() == '/')
            root.pop_back();
        TEST_ASSERT_TRUE_MESSAGE(path_exists(root + "/platformio.ini"),
                                 ("derived repo root does not look like one "
                                  "(no platformio.ini): " + root).c_str());
        return root;
    }

    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);

    for (int hops = 0; hops < 16; hops++)
    {
        if (path_exists(dir + "/" + file) && path_exists(dir + "/platformio.ini"))
            return dir;
        if (dir.empty() || dir == "/")
            break;
        size_t pos = dir.find_last_of('/');
        dir = (pos == std::string::npos || pos == 0) ? "/" : dir.substr(0, pos);
    }
    TEST_FAIL_MESSAGE("could not locate repo root from __FILE__ or cwd");
    return std::string();
}

static std::string fixture_path()
{
    return repo_root() + "/test/test_nbr_replay/fixtures/dk5en98-20260923-boot.txt";
}

// --- small string helpers ---------------------------------------------------

static std::vector<std::string> split_pipe(const std::string &s)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;)
    {
        size_t p = s.find('|', start);
        if (p == std::string::npos)
        {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

static std::string join_pipe(const std::vector<std::string> &fld)
{
    std::string out;
    for (size_t i = 0; i < fld.size(); i++)
    {
        if (i)
            out += "|";
        out += fld[i];
    }
    return out;
}

static std::string token_first(const std::string &path)
{
    size_t p = path.find(',');
    return p == std::string::npos ? path : path.substr(0, p);
}

static std::string token_last(const std::string &path)
{
    size_t p = path.rfind(',');
    return p == std::string::npos ? path : path.substr(p + 1);
}

static char nbr_typ_char(char type)
{
    return (type == ':') ? 'T' : (type == '!') ? 'P' : 'H';
}

static std::string nbr_line_type(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    return fld.size() > 1 ? fld[1] : std::string("?");
}

// --- position decode (uncompressed "DDMM.hhN/DDDMM.hhE...", see file header
// comment) -- mirrors decodeAPRSPOS()'s field split (scan for N/S, then for
// W/E) and conv_coord_to_dec()'s arithmetic (both src/aprs_functions.cpp,
// out of the file set; not called from here, only replicated). ------------

struct DecodedPos
{
    bool ok;
    float lat;
    float lon;
};

static double nbr_conv_coord_to_dec(double coord)
{
    int ig = (int)(coord / 100.0);
    double dm = (coord - (double)(ig * 100)) / 60.0;
    return (double)ig + dm;
}

static DecodedPos decode_pos_payload(const std::string &payload)
{
    DecodedPos d;
    d.ok = false;
    d.lat = 0.0f;
    d.lon = 0.0f;

    size_t i1 = payload.find_first_of("NS");
    if (i1 == std::string::npos || i1 == 0 || i1 + 1 >= payload.size())
        return d;
    std::string lat_str = payload.substr(0, i1);
    char lat_c = payload[i1];

    size_t lon_start = i1 + 2; // skip hemisphere char + APRS symbol-table char
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

    float lat = (float)nbr_conv_coord_to_dec(lat_num);
    if (lat_c == 'S')
        lat = lat * -1.0f;
    float lon = (float)nbr_conv_coord_to_dec(lon_num);
    if (lon_c == 'W')
        lon = lon * -1.0f;

    d.ok = true;
    d.lat = lat;
    d.lon = lon;
    return d;
}

// --- [LOG] line parsing (printBuffer_aprs(), src/loop_functions.cpp, and
// its tail setlogFormatRxTail(), src/setlog_lines.cpp) ----------------------

struct LogFrame
{
    uint32_t msg_id;
    char type; // ':' | '!' | '@'
    std::string path;
    std::string dest;
    std::string payload;
    bool mesh;
    uint8_t hw;
    int16_t rssi;
    int8_t snr;
    unsigned long t_ms;
    uint16_t now_min;
    // false when the [LOG] trailer shows FCS:0000: decodeAPRS() sets
    // msg_fcs only on success (src/aprs_functions.cpp, "msg_fcs = FCS_SUMME"
    // at the end), so 0000 means it returned 0x00 -- OnRxDone then takes the
    // msg_type_b_lora == 0x00 branch and never reaches the matrix.
    bool decoded;
};

static bool parse_log_line(const std::string &line, LogFrame &out, std::string &err)
{
    size_t p = line.find("[LOG] ");
    if (p == std::string::npos)
    {
        err = "no [LOG] marker";
        return false;
    }
    std::string s = line.substr(p + 6);

    std::istringstream iss(s);
    std::string tok_len, tok_type, tok_msgid, tok_h, tok_s, tok_t, tok_m;
    if (!(iss >> tok_len >> tok_type >> tok_msgid >> tok_h >> tok_s >> tok_t >> tok_m))
    {
        err = "short header";
        return false;
    }
    if (tok_type.size() != 1 || tok_msgid.size() < 2 || tok_msgid[0] != 'x' || tok_m.size() < 2)
    {
        err = "malformed header token";
        return false;
    }
    out.type = tok_type[0];
    out.msg_id = (uint32_t)strtoul(tok_msgid.c_str() + 1, NULL, 16);
    out.mesh = atoi(tok_m.c_str() + 1) != 0;

    size_t rest_pos = (size_t)iss.tellg();
    while (rest_pos < s.size() && s[rest_pos] == ' ')
        rest_pos++;

    size_t hw_pos = s.rfind(" HW:");
    if (hw_pos == std::string::npos || hw_pos < rest_pos)
    {
        err = "no HW: trailer";
        return false;
    }
    std::string block = s.substr(rest_pos, hw_pos - rest_pos);
    std::string trailer = s.substr(hw_pos);

    size_t gt = block.find('>');
    if (gt == std::string::npos)
    {
        err = "no '>' in path>dest block";
        return false;
    }
    out.path = block.substr(0, gt);
    std::string rest = block.substr(gt + 1);
    size_t type_pos = rest.find(out.type);
    if (type_pos == std::string::npos)
    {
        err = "type char not found after dest";
        return false;
    }
    out.dest = rest.substr(0, type_pos);
    out.payload = rest.substr(type_pos + 1);

    size_t hwv = trailer.find("HW:");
    size_t rssiv = trailer.find("RSSI:");
    size_t snrv = trailer.find("SNR:");
    size_t tv = trailer.find("t=");
    if (hwv == std::string::npos || rssiv == std::string::npos ||
        snrv == std::string::npos || tv == std::string::npos)
    {
        err = "missing HW:/RSSI:/SNR:/t= field";
        return false;
    }
    out.hw = (uint8_t)atoi(trailer.c_str() + hwv + 3);
    out.rssi = (int16_t)atoi(trailer.c_str() + rssiv + 5);
    out.snr = (int8_t)atoi(trailer.c_str() + snrv + 4);
    out.t_ms = strtoul(trailer.c_str() + tv + 2, NULL, 10);
    out.now_min = (uint16_t)(out.t_ms / 60000UL);
    out.decoded = trailer.find("FCS:0000") == std::string::npos;
    return true;
}

// --- capture callback --------------------------------------------------

static std::vector<std::string> *g_capture = NULL;

static void capture_cb(const char *line)
{
    if (g_capture)
        g_capture->push_back(std::string(line));
}

// --- harness-side row set: up to 128 rows, whatever the implementation -----

struct HMask
{
    uint64_t w[2];
};

static HMask hm_none()
{
    HMask m;
    m.w[0] = m.w[1] = 0;
    return m;
}

static bool hm_test(const HMask &m, int i)
{
    return i >= 0 && i < 128 && ((m.w[i >> 6] >> (i & 63)) & 1u);
}

static bool hm_empty(const HMask &m)
{
    return m.w[0] == 0 && m.w[1] == 0;
}

static HMask hm_andnot(const HMask &a, const HMask &b)
{
    HMask r;
    r.w[0] = a.w[0] & ~b.w[0];
    r.w[1] = a.w[1] & ~b.w[1];
    return r;
}

static HMask hm_and(const HMask &a, const HMask &b)
{
    HMask r;
    r.w[0] = a.w[0] & b.w[0];
    r.w[1] = a.w[1] & b.w[1];
    return r;
}

static HMask hm_from32(uint32_t v)
{
    HMask m = hm_none();
    m.w[0] = v;
    return m;
}

static HMask hm_from(const NbrMask &n)
{
    HMask m = hm_none();
    for (int i = 0; i < NBR_MASK_WORDS && i < 2; i++)
        m.w[i] = n.w[i];
    return m;
}

static NbrMask hm_to(const HMask &h)
{
    NbrMask n = nbrMaskNone();
    for (int i = 0; i < NBR_MASK_WORDS && i < 2; i++)
        n.w[i] = h.w[i];
    return n;
}

// Hex of any width (8 digits from the dense code, 16/32 from nbrMaskHex())
// back into a set; most significant digit first.
static HMask hm_parse_hex(const std::string &hex)
{
    HMask m = hm_none();
    int bit = 0;
    for (int i = (int)hex.size() - 1; i >= 0 && bit < 128; i--, bit += 4)
    {
        char c = hex[i];
        unsigned v = (c >= '0' && c <= '9') ? (unsigned)(c - '0')
                   : (c >= 'A' && c <= 'F') ? (unsigned)(c - 'A' + 10)
                   : (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10) : 0u;
        m.w[bit >> 6] |= (uint64_t)v << (bit & 63);
    }
    return m;
}

typedef std::unordered_map<int, std::string> IdxToCall;

// Sorted, comma-joined callsigns for the bits set in mask, "-" if none, an
// unknown index rendered as "?<idx>" rather than silently dropped.
static std::string mask_to_names(const HMask &mask, const IdxToCall &idx_to_call)
{
    std::vector<std::string> names;
    for (int i = 0; i < 128; i++)
    {
        if (!hm_test(mask, i))
            continue;
        IdxToCall::const_iterator it = idx_to_call.find(i);
        if (it != idx_to_call.end())
            names.push_back(it->second);
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "?%d", i);
            names.push_back(buf);
        }
    }
    std::sort(names.begin(), names.end());
    std::string out;
    for (size_t i = 0; i < names.size(); i++)
    {
        if (i)
            out += ",";
        out += names[i];
    }
    return out.empty() ? "-" : out;
}

// --- adapters ------------------------------------------------------------------

struct NeedRes
{
    HMask need, alone, inferred;
    bool known;
};

struct DenseA
{
    typedef dense::NbrMatrix Mat;
    static const char *name() { return "dense"; }
    static void set_log(void (*fn)(const char *)) { dense::nbrLog = fn; }
    static bool own_is(const Mat &m, const char *c) { return strcmp(m.rows[0].call, c) == 0; }
    static void init(Mat &m, const char *c, uint16_t t) { dense::nbrInit(m, c, t); }
    static void set_gw0(Mat &m) { m.rows[0].flags |= NBR_FLAG_GW; }
    static void sweep(Mat &, uint16_t) {}
    static void note_frame(Mat &m, const LogFrame &fr, bool dest_gw)
    {
        dense::nbrNoteFrame(m, fr.path.c_str(), fr.type, fr.payload.c_str(), dest_gw, fr.rssi, fr.snr, fr.now_min);
    }
    static void note_pos(Mat &m, const char *call, float lat, float lon, bool mesh, uint8_t hw, uint16_t t)
    {
        dense::nbrNotePos(m, call, lat, lon, mesh, hw, t);
    }
    static HMask cover(const Mat &m, const char *relayer, uint16_t t, bool sym, const HMask &relevant,
                       uint32_t msgid, HMask *inferred)
    {
        uint32_t inf = 0;
        uint32_t r = dense::nbrCoverMask(m, relayer, t, sym, (uint32_t)relevant.w[0], msgid, inferred ? &inf : NULL);
        if (inferred)
            *inferred = hm_from32(inf);
        return hm_from32(r);
    }
    static NeedRes need(const Mat &m, const char *path, uint16_t t, bool sym, uint32_t msgid)
    {
        dense::NbrNeed r = dense::nbrRelayNeed(m, path, t, sym, msgid);
        NeedRes o;
        o.need = hm_from32(r.need);
        o.alone = hm_from32(r.alone);
        o.inferred = hm_from32(r.inferred);
        o.known = r.known;
        return o;
    }
    static void snapshot(const Mat &m, uint16_t t) { dense::nbrLogSnapshot(m, t); }
    static int build_report(const Mat &m, uint16_t t, int heard, char *out, size_t n)
    {
        return dense::nbrBuildReport(m, t, heard, out, n);
    }
    static std::string mask_hex(const HMask &h)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%08X", (unsigned)(uint32_t)h.w[0]);
        return buf;
    }
    static IdxToCall idx_to_call(const Mat &m)
    {
        IdxToCall t;
        t[0] = m.rows[0].call;
        for (int i = 1; i < NBR_DENSE_ROWS; i++)
            if (m.rows[i].flags & NBR_FLAG_USED)
                t[i] = m.rows[i].call;
        return t;
    }
    static HMask eself(const Mat &m, uint16_t t)
    {
        uint8_t out[NBR_DENSE_ROWS];
        int n = dense::nbrExclusiveDirect(m, t, out, NBR_DENSE_ROWS);
        HMask r = hm_none();
        for (int i = 0; i < n && i < NBR_DENSE_ROWS; i++)
            r.w[0] |= (uint64_t)1 << out[i];
        return r;
    }
    // #X of a row and the set behind it; the dense code has only the count.
    static int meshneed(const Mat &m, const char *call, uint16_t t, HMask *set)
    {
        *set = hm_none();
        int row = dense::nbrFind(m, call);
        if (row < 0)
            return -2;
        return dense::nbrRowMeshNeedCount(m, row, t);
    }
};

#define EDGE_ADAPTER(NAME, NS)                                                                            \
    struct NAME                                                                                           \
    {                                                                                                     \
        typedef ::NbrMatrix Mat;                                                                          \
        static const char *name() { return #NS; }                                                         \
        static void set_log(void (*fn)(const char *)) { NS::nbrLog = fn; }                                \
        static bool own_is(const Mat &m, const char *c) { return NS::nbrOwnCallIs(m, c); }                \
        static void init(Mat &m, const char *c, uint16_t t) { NS::nbrInit(m, c, t); }                     \
        static void set_gw0(Mat &m) { NS::nbrRowSetFlag(m, 0, NBR_FLAG_GW); }                             \
        static void sweep(Mat &m, uint16_t t) { NS::nbrSweep(m, t); }                                     \
        static void note_frame(Mat &m, const LogFrame &fr, bool dest_gw)                                  \
        {                                                                                                 \
            NS::nbrNoteFrame(m, fr.path.c_str(), fr.type, fr.payload.c_str(), dest_gw, fr.rssi, fr.snr,  \
                             fr.now_min);                                                                 \
        }                                                                                                 \
        static void note_pos(Mat &m, const char *call, float lat, float lon, bool mesh, uint8_t hw,       \
                             uint16_t t)                                                                  \
        {                                                                                                 \
            NS::nbrNotePos(m, call, lat, lon, mesh, hw, t);                                               \
        }                                                                                                 \
        static HMask cover(const Mat &m, const char *relayer, uint16_t t, bool sym, const HMask &relevant, \
                           uint32_t msgid, HMask *inferred)                                               \
        {                                                                                                 \
            NbrMask inf = nbrMaskNone();                                                                  \
            NbrMask r = NS::nbrCoverMask(m, relayer, t, sym, hm_to(relevant), msgid, inferred ? &inf : NULL); \
            if (inferred)                                                                                 \
                *inferred = hm_from(inf);                                                                 \
            return hm_from(r);                                                                            \
        }                                                                                                 \
        static NeedRes need(const Mat &m, const char *path, uint16_t t, bool sym, uint32_t msgid)         \
        {                                                                                                 \
            NbrNeed r = NS::nbrRelayNeed(m, path, t, sym, msgid);                                         \
            NeedRes o;                                                                                    \
            o.need = hm_from(r.need);                                                                     \
            o.alone = hm_from(r.alone);                                                                   \
            o.inferred = hm_from(r.inferred);                                                             \
            o.known = r.known;                                                                            \
            return o;                                                                                     \
        }                                                                                                 \
        static void snapshot(const Mat &m, uint16_t t) { NS::nbrLogSnapshot(m, t); }                      \
        static int build_report(const Mat &m, uint16_t t, int heard, char *out, size_t n)                 \
        {                                                                                                 \
            return NS::nbrBuildReport(m, t, heard, out, n);                                               \
        }                                                                                                 \
        static std::string mask_hex(const HMask &h)                                                       \
        {                                                                                                 \
            char buf[NBR_MASK_HEX_LEN + 1];                                                               \
            NS::nbrMaskHex(hm_to(h), buf, sizeof(buf));                                                   \
            return buf;                                                                                   \
        }                                                                                                 \
        static IdxToCall idx_to_call(const Mat &m)                                                        \
        {                                                                                                 \
            IdxToCall t;                                                                                  \
            for (int i = 0; i < NBR_MAX_ROWS; i++)                                                        \
            {                                                                                             \
                NbrRowView v;                                                                             \
                if (NS::nbrRowGet(m, i, &v))                                                              \
                    t[i] = v.call;                                                                        \
            }                                                                                             \
            return t;                                                                                     \
        }                                                                                                 \
        static HMask eself(const Mat &m, uint16_t t)                                                      \
        {                                                                                                 \
            uint8_t out[NBR_MAX_ROWS];                                                                    \
            int n = NS::nbrExclusiveDirect(m, t, out, NBR_MAX_ROWS);                                      \
            HMask r = hm_none();                                                                          \
            for (int i = 0; i < n && i < NBR_MAX_ROWS; i++)                                               \
                r.w[out[i] >> 6] |= (uint64_t)1 << (out[i] & 63);                                         \
            return r;                                                                                     \
        }                                                                                                 \
        static int meshneed(const Mat &m, const char *call, uint16_t t, HMask *set)                       \
        {                                                                                                 \
            *set = hm_none();                                                                             \
            int row = NS::nbrFind(m, call);                                                               \
            if (row < 0)                                                                                  \
                return -2;                                                                                \
            NbrMask s;                                                                                    \
            int n = NS::nbrRowMeshNeedSet(m, row, t, &s);                                                 \
            *set = hm_from(s);                                                                            \
            return n;                                                                                     \
        }                                                                                                 \
    };

EDGE_ADAPTER(EdgeA, edge)
#ifndef NBR_REPLAY_PRODUCTION
EDGE_ADAPTER(EdgeDeferredA, edge_deferred)
#endif
#ifdef NBR_REPLAY_PRODUCTION
EDGE_ADAPTER(EdgeNoShareA, edge_noshare)
EDGE_ADAPTER(EdgeNoRulesA, edge_norules)
#endif

// --- Stufe-2 ring model (see file header comment) ---------------------------

struct RingEntry
{
    HMask need;
    HMask alone;
    bool active;
    bool counted;
};

static const char *OWN_CALL = "DK5EN-98";
static const bool BNBRSYM = true;
static const bool BGATEWAY = true;

// The row the concept 4.3 check is about, and the station it should carry.
static const char *PROBE_CALL = "DB0ED-99";
__attribute__((unused)) static const char *PROBE_EXCL = "DB0FHR-12";

// --- trigger groups (see ASSUMPTION 2c) -------------------------------------

struct TriggerGroup
{
    std::string kind; // "LOG" | "SNAP" | "RPTTX"
    uint16_t up;
    std::vector<std::string> expected;
    std::vector<std::string> actual;
    IdxToCall exp_idx_to_call;
    IdxToCall act_idx_to_call;
    // Wave 2 (production envs): decisions keyed for the dense/production
    // comparison -- "NEED|<msgid>" -> "case|need|alone" (callsign sets),
    // "ESELF" -> callsign set; ROW verdicts are read back from `actual`.
    std::vector<std::pair<std::string, std::string> > decisions;
    // #X of every row at a SNAP (not a categorised decision: shown only to
    // make the share rule's own effect visible, see test C).
    std::vector<std::pair<std::string, std::string> > xcounts;
    int probe_x = -3;            // #X of PROBE_CALL at this SNAP, -2 = no row, -3 = not probed
    std::string probe_set;       // callsigns behind it ("" for the dense code)
};

struct ReplayResult
{
    std::vector<TriggerGroup> groups;
    std::vector<std::string> parse_errors; // "<lineno>: <reason>: <line>"
    long need_no_path = 0;                 // NEED lines whose msg_id had no preceding [LOG] line
};

struct ReplayOpts
{
    bool sweep_each_minute = false; // call nbrSweep() once per minute like the loop task
    bool probe = false;             // record decisions/probes per group (production envs)
};

// --- replay: fixture -> ordered TriggerGroups -------------------------------

template <class A>
static void run_replay(const std::string &fixture_path, ReplayResult &out, const ReplayOpts &opt)
{
    std::ifstream f(fixture_path.c_str());
    if (!f.good())
    {
        out.parse_errors.push_back("cannot open fixture: " + fixture_path);
        return;
    }

    typename A::Mat *mp = new typename A::Mat();
    typename A::Mat &m = *mp;
    memset(mp, 0, sizeof(*mp)); // the lazy-init sentinel, same as a real cold matrix (BSS)
    bool inited = false;
    uint16_t last_swept = 0;

    // The loop task sweeps once a minute, whether or not frames arrive.
    // Called before every event with that event's minute, it runs every
    // minute in between.
    auto advance = [&](uint16_t up) {
        if (!opt.sweep_each_minute || !inited)
            return;
        uint16_t steps = (uint16_t)(up - last_swept);
        if (steps > 2000)
            steps = 1; // clock jump: one sweep at the new minute
        for (uint16_t k = 1; k <= steps; k++)
            A::sweep(m, (uint16_t)(up - steps + k));
        last_swept = up;
    };

    std::unordered_map<uint32_t, RingEntry> ring;
    std::unordered_map<uint32_t, std::string> last_path;
    IdxToCall exp_idx_to_call; // rolling, best-effort (see TriggerGroup comment)

    A::set_log(capture_cb);

    TriggerGroup *cur = NULL;
    TriggerGroup *cur_log = NULL;

    std::string line;
    long lineno = 0;
    while (std::getline(f, line))
    {
        lineno++;
        if (line.find("[LOG] ") != std::string::npos)
        {
            LogFrame fr;
            std::string err;
            if (!parse_log_line(line, fr, err))
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%ld: ", lineno);
                out.parse_errors.push_back(std::string(buf) + err + ": " + line);
                continue;
            }

            out.groups.push_back(TriggerGroup());
            cur = &out.groups.back();
            cur->kind = "LOG";
            cur->up = fr.now_min;
            cur_log = cur;
            g_capture = &cur->actual;

            advance(fr.now_min);
            if (!fr.decoded)
                continue;

            std::string last_hop = token_last(fr.path);
            bool has_comma = fr.path.find(',') != std::string::npos;

            // 1) Stufe 2 cover-check -- BEFORE lazy init/Stufe 1.
            if (has_comma && last_hop != OWN_CALL)
            {
                HMask cover_gate = A::cover(m, last_hop.c_str(), fr.now_min, BNBRSYM, hm_none(), 0, NULL);
                if (!hm_empty(cover_gate))
                {
                    std::unordered_map<uint32_t, RingEntry>::iterator it = ring.find(fr.msg_id);
                    if (it != ring.end() && it->second.active)
                    {
                        RingEntry &e = it->second;
                        char typ = nbr_typ_char(fr.type);
                        if (!hm_empty(e.alone))
                        {
                            if (!e.counted)
                            {
                                e.counted = true;
                                char buf[160];
                                snprintf(buf, sizeof(buf), "[NBR]|REFUSE|%u|%08X|%c|%s|%s",
                                         (unsigned)fr.now_min, (unsigned)fr.msg_id, typ,
                                         last_hop.c_str(), A::mask_hex(e.alone).c_str());
                                cur->actual.push_back(buf);
                            }
                        }
                        else
                        {
                            HMask slot_inferred = hm_none();
                            HMask cover = A::cover(m, last_hop.c_str(), fr.now_min, BNBRSYM, e.need, fr.msg_id,
                                                   &slot_inferred);
                            HMask before = e.need;
                            e.need = hm_andnot(e.need, cover);
                            HMask after = e.need;
                            HMask removed_inferred = hm_and(hm_andnot(before, after), slot_inferred);
                            if (hm_empty(after))
                            {
                                char buf[224];
                                snprintf(buf, sizeof(buf), "[NBR]|CANCEL|%u|%08X|%c|%s|%s|%s|%s",
                                         (unsigned)fr.now_min, (unsigned)fr.msg_id, typ, last_hop.c_str(),
                                         A::mask_hex(before).c_str(), A::mask_hex(after).c_str(),
                                         A::mask_hex(removed_inferred).c_str());
                                cur->actual.push_back(buf);
                                e.active = false;
                            }
                        }
                    }
                }
            }

            // 2) Lazy nbrInit()/GW-flag.
            if (!A::own_is(m, OWN_CALL))
            {
                A::init(m, OWN_CALL, fr.now_min);
                inited = true;
                last_swept = fr.now_min;
            }
            if (BGATEWAY)
                A::set_gw0(m);

            // 3) Stufe 1.
            bool dest_gw = (fr.dest == "HG");
            A::note_frame(m, fr, dest_gw);
            last_path[fr.msg_id] = fr.path;

            if (fr.type == '!')
            {
                DecodedPos d = decode_pos_payload(fr.payload);
                if (d.ok)
                    A::note_pos(m, token_first(fr.path).c_str(), d.lat, d.lon, fr.mesh, fr.hw, fr.now_min);
            }

            cur->act_idx_to_call = A::idx_to_call(m);
            cur->exp_idx_to_call = exp_idx_to_call;
        }
        else
        {
            size_t p = line.find("[NBR]|");
            if (p == std::string::npos)
                continue;
            std::string nbrtext = line.substr(p);

            std::vector<std::string> fld = split_pipe(nbrtext);
            std::string type = fld.size() > 1 ? fld[1] : std::string();
            uint16_t up = fld.size() > 2 ? (uint16_t)strtoul(fld[2].c_str(), NULL, 10) : (cur ? cur->up : 0);

            if (type == "SNAP")
            {
                out.groups.push_back(TriggerGroup());
                cur = &out.groups.back();
                cur->kind = "SNAP";
                cur->up = up;
                cur->expected.push_back(nbrtext);
                g_capture = &cur->actual;
                advance(up);
                A::snapshot(m, up);
                cur->act_idx_to_call = A::idx_to_call(m);
                cur->exp_idx_to_call = exp_idx_to_call;
                if (opt.probe)
                {
                    cur->decisions.push_back(std::make_pair(std::string("ESELF"),
                                                            mask_to_names(A::eself(m, up), cur->act_idx_to_call)));
                    HMask set;
                    cur->probe_x = A::meshneed(m, PROBE_CALL, up, &set);
                    cur->probe_set = mask_to_names(set, cur->act_idx_to_call);
                    for (IdxToCall::const_iterator it = cur->act_idx_to_call.begin();
                         it != cur->act_idx_to_call.end(); ++it)
                    {
                        if (it->first == 0)
                            continue;
                        HMask xs;
                        int n = A::meshneed(m, it->second.c_str(), up, &xs);
                        cur->xcounts.push_back(std::make_pair("#X|" + it->second, std::to_string(n)));
                    }
                }
            }
            else if (type == "RPTTX")
            {
                out.groups.push_back(TriggerGroup());
                cur = &out.groups.back();
                cur->kind = "RPTTX";
                cur->up = up;
                cur->expected.push_back(nbrtext);
                g_capture = &cur->actual;
                advance(up);

                int heard_count = 0;
                if (fld.size() > 4 && !fld[4].empty() && fld[4][0] == 'R')
                    heard_count = atoi(fld[4].c_str() + 1);

                char report[160];
                int n = A::build_report(m, up, heard_count, report, sizeof(report));
                char buf[224];
                if (n >= 0)
                    snprintf(buf, sizeof(buf), "[NBR]|RPTTX|%u|%d|%s", (unsigned)up, n, report);
                else
                    snprintf(buf, sizeof(buf), "[NBR]|RPTTX|%u|-1|", (unsigned)up);
                cur->actual.push_back(buf);
                cur->act_idx_to_call = A::idx_to_call(m);
                cur->exp_idx_to_call = exp_idx_to_call;
            }
            else if (type == "NEED")
            {
                if (cur_log == NULL)
                {
                    out.need_no_path++;
                    continue;
                }
                cur_log->expected.push_back(nbrtext);
                g_capture = &cur_log->actual;
                advance(up);

                uint32_t msgid = fld.size() > 3 ? (uint32_t)strtoul(fld[3].c_str(), NULL, 16) : 0;
                char typ = (fld.size() > 4 && !fld[4].empty()) ? fld[4][0] : '?';

                std::unordered_map<uint32_t, std::string>::iterator pit = last_path.find(msgid);
                if (pit == last_path.end())
                {
                    out.need_no_path++;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[NBR]|NEED-NO-PATH|%08X", (unsigned)msgid);
                    cur_log->actual.push_back(buf);
                }
                else
                {
                    NeedRes r = A::need(m, pit->second.c_str(), up, BNBRSYM, msgid);
                    char case_ch = !r.known ? 'U' : (!hm_empty(r.alone) ? 'A' : 'B');
                    char buf[224];
                    snprintf(buf, sizeof(buf), "[NBR]|NEED|%u|%08X|%c|%c|%s|%s|%d|%s",
                             (unsigned)up, (unsigned)msgid, typ, case_ch, A::mask_hex(r.need).c_str(),
                             A::mask_hex(r.alone).c_str(), -1, A::mask_hex(r.inferred).c_str());
                    cur_log->actual.push_back(buf);

                    if (opt.probe)
                    {
                        IdxToCall t = A::idx_to_call(m);
                        char key[32];
                        snprintf(key, sizeof(key), "NEED|%08X", (unsigned)msgid);
                        cur_log->decisions.push_back(std::make_pair(
                            std::string(key),
                            std::string(1, case_ch) + "|" + mask_to_names(r.need, t) + "|" + mask_to_names(r.alone, t)));
                    }

                    if (r.known)
                    {
                        RingEntry e;
                        e.need = r.need;
                        e.alone = r.alone;
                        e.active = true;
                        e.counted = false;
                        ring[msgid] = e;
                    }
                }
            }
            else
            {
                if (cur != NULL)
                    cur->expected.push_back(nbrtext);
                else
                    out.parse_errors.push_back(
                        std::to_string(lineno) + ": [NBR] line before any trigger: " + nbrtext);

                if (type == "ROW" && fld.size() > 4)
                    exp_idx_to_call[atoi(fld[3].c_str())] = fld[4];
                else if (type == "EVICT" && fld.size() > 5)
                    exp_idx_to_call[atoi(fld[3].c_str())] = fld[5];
                if (cur != NULL && (type == "ROW" || type == "ENDSNAP"))
                    cur->exp_idx_to_call = exp_idx_to_call;
            }
        }
    }

    g_capture = NULL;
    A::set_log(NULL);
    delete mp;
}

// --- compare against the capture: grouped, multiset, three modes ------------

static std::string normalize_exact(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    if (fld.size() > 1 && fld[1] == "NEED" && fld.size() > 8)
        fld[8] = "-";
    return join_pipe(fld);
}

static std::string normalize_indexfree(const std::string &line, const IdxToCall &idx_to_call)
{
    std::vector<std::string> fld = split_pipe(line);
    std::string type = fld.size() > 1 ? fld[1] : std::string();

    if (type == "ROW" && fld.size() > 3)
        fld[3] = "-";
    else if (type == "EVICT" && fld.size() > 3)
        fld[3] = "-";
    else if (type == "NEED" && fld.size() > 9)
    {
        fld[6] = mask_to_names(hm_parse_hex(fld[6]), idx_to_call);
        fld[7] = mask_to_names(hm_parse_hex(fld[7]), idx_to_call);
        fld[8] = "-";
        fld[9] = mask_to_names(hm_parse_hex(fld[9]), idx_to_call);
    }
    else if ((type == "CANCEL" || type == "CANCEL?") && fld.size() > 8)
    {
        fld[6] = mask_to_names(hm_parse_hex(fld[6]), idx_to_call);
        fld[7] = mask_to_names(hm_parse_hex(fld[7]), idx_to_call);
        fld[8] = mask_to_names(hm_parse_hex(fld[8]), idx_to_call);
    }
    else if (type == "REFUSE" && fld.size() > 6)
    {
        fld[6] = mask_to_names(hm_parse_hex(fld[6]), idx_to_call);
    }
    return join_pipe(fld);
}

enum class CompareMode
{
    EXACT,
    INDEX_FREE,
    DECISION
};

static bool decision_keeps(const std::string &line)
{
    std::string type = nbr_line_type(line);
    return !(type == "CANCEL" || type == "CANCEL?" || type == "REFUSE");
}

static std::string normalize_decision(const std::string &line, const IdxToCall &idx_to_call)
{
    std::vector<std::string> fld = split_pipe(normalize_indexfree(line, idx_to_call));
    std::string type = fld.size() > 1 ? fld[1] : std::string();
    if (type == "EDGE" && fld.size() > 7)
        fld[7] = "-";
    else if (type == "ME" && fld.size() > 6)
        fld[6] = "-";
    return join_pipe(fld);
}

static bool group_matches(const TriggerGroup &g, CompareMode mode)
{
    std::vector<std::string> exp, act;
    exp.reserve(g.expected.size());
    act.reserve(g.actual.size());
    for (size_t i = 0; i < g.expected.size(); i++)
    {
        if (mode == CompareMode::DECISION && !decision_keeps(g.expected[i]))
            continue;
        exp.push_back(mode == CompareMode::EXACT      ? normalize_exact(g.expected[i])
                      : mode == CompareMode::DECISION ? normalize_decision(g.expected[i], g.exp_idx_to_call)
                                                      : normalize_indexfree(g.expected[i], g.exp_idx_to_call));
    }
    for (size_t i = 0; i < g.actual.size(); i++)
    {
        if (mode == CompareMode::DECISION && !decision_keeps(g.actual[i]))
            continue;
        act.push_back(mode == CompareMode::EXACT      ? normalize_exact(g.actual[i])
                      : mode == CompareMode::DECISION ? normalize_decision(g.actual[i], g.act_idx_to_call)
                                                      : normalize_indexfree(g.actual[i], g.act_idx_to_call));
    }
    std::sort(exp.begin(), exp.end());
    std::sort(act.begin(), act.end());
    return exp == act;
}

#ifndef NBR_REPLAY_PRODUCTION // capture comparison: compat env only

static std::vector<std::string> group_types(const TriggerGroup &g)
{
    std::vector<std::string> types;
    for (size_t i = 0; i < g.expected.size(); i++)
        types.push_back(nbr_line_type(g.expected[i]));
    for (size_t i = 0; i < g.actual.size(); i++)
        types.push_back(nbr_line_type(g.actual[i]));
    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());
    return types;
}

static std::string join_comma(const std::vector<std::string> &v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i)
            out += ",";
        out += v[i];
    }
    return out;
}

struct HourBucket
{
    long groups = 0;
    long mismatched = 0;
    std::unordered_map<std::string, long> mismatch_by_type;
};

struct GroupReport
{
    std::map<int, HourBucket> hours;
    long total_groups = 0;
    long total_mismatched = 0;
};

static GroupReport build_group_report(const std::vector<TriggerGroup> &groups, CompareMode mode)
{
    GroupReport rep;
    for (size_t i = 0; i < groups.size(); i++)
    {
        const TriggerGroup &g = groups[i];
        int hour = (int)(g.up / 60);
        HourBucket &hb = rep.hours[hour];
        hb.groups++;
        rep.total_groups++;
        if (!group_matches(g, mode))
        {
            hb.mismatched++;
            rep.total_mismatched++;
            std::vector<std::string> types = group_types(g);
            for (size_t t = 0; t < types.size(); t++)
                hb.mismatch_by_type[types[t]]++;
        }
    }
    return rep;
}

static void print_hourly_table(const char *label, const GroupReport &rep)
{
    printf("\n[nbr_replay] per-hour mismatch table (%s mode): hour groups mismatched by-type\n", label);
    for (std::map<int, HourBucket>::const_iterator it = rep.hours.begin(); it != rep.hours.end(); ++it)
    {
        const HourBucket &hb = it->second;
        std::string bytype;
        for (std::unordered_map<std::string, long>::const_iterator jt = hb.mismatch_by_type.begin();
             jt != hb.mismatch_by_type.end(); ++jt)
        {
            if (!bytype.empty())
                bytype += ",";
            char buf[32];
            snprintf(buf, sizeof(buf), "%s:%ld", jt->first.c_str(), jt->second);
            bytype += buf;
        }
        printf("  %4d %8ld %10ld  %s\n", it->first, hb.groups, hb.mismatched,
               bytype.empty() ? "-" : bytype.c_str());
    }
    printf("[nbr_replay] %s total: %ld/%ld groups mismatched\n", label, rep.total_mismatched, rep.total_groups);
}

#endif // !NBR_REPLAY_PRODUCTION

// --- implementation vs. implementation, line for line ------------------------

// Wave 2 differential (B): the only two normalisations allowed between the
// dense reference and the edge pool. EDGE/ME <cnt> is one counter over all
// frame types now instead of the counter of the frame's own type; a mask is
// printed with NBR_MASK_HEX_LEN digits instead of 8 -- compared on the low
// 32 bits, which hold all 21 rows of the compat build.
static std::string low32_hex(const std::string &hex)
{
    return hex.size() > 8 ? hex.substr(hex.size() - 8) : hex;
}

static std::string normalize_b(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    std::string type = fld.size() > 1 ? fld[1] : std::string();
    if (type == "EDGE" && fld.size() > 7)
        fld[7] = "-";
    else if (type == "ME" && fld.size() > 6)
        fld[6] = "-";
    else if (type == "NEED" && fld.size() > 9)
    {
        fld[6] = low32_hex(fld[6]);
        fld[7] = low32_hex(fld[7]);
        fld[9] = low32_hex(fld[9]);
    }
    else if ((type == "CANCEL" || type == "CANCEL?") && fld.size() > 8)
    {
        fld[6] = low32_hex(fld[6]);
        fld[7] = low32_hex(fld[7]);
        fld[8] = low32_hex(fld[8]);
    }
    else if (type == "REFUSE" && fld.size() > 6)
        fld[6] = low32_hex(fld[6]);
    return join_pipe(fld);
}

// Sweep independence (S): SNAP <cells> counts live pool entries, and without
// the sweep stale edges stay in the pool. Nothing else may differ.
static std::string normalize_s(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    if (fld.size() > 6 && fld[1] == "SNAP")
        fld[6] = "-";
    return join_pipe(fld);
}

#ifndef NBR_REPLAY_PRODUCTION
// Line-for-line, in emission order, over every group of two runs of the same
// fixture. Returns the number of differing lines and prints the first few.
static long diff_runs(const char *label, const ReplayResult &a, const ReplayResult &b,
                      std::string (*norm)(const std::string &))
{
    long bad = 0, lines = 0, shown = 0;
    if (a.groups.size() != b.groups.size())
    {
        printf("[nbr_replay] %s: group count differs %zu vs %zu\n", label, a.groups.size(), b.groups.size());
        return 1;
    }
    for (size_t g = 0; g < a.groups.size(); g++)
    {
        const std::vector<std::string> &la = a.groups[g].actual;
        const std::vector<std::string> &lb = b.groups[g].actual;
        size_t n = std::max(la.size(), lb.size());
        for (size_t i = 0; i < n; i++)
        {
            lines++;
            std::string x = i < la.size() ? norm(la[i]) : std::string("<none>");
            std::string y = i < lb.size() ? norm(lb[i]) : std::string("<none>");
            if (x != y)
            {
                bad++;
                if (shown++ < 12)
                    printf("  %s group %zu up=%u line %zu:\n    A: %s\n    B: %s\n", label, g,
                           (unsigned)a.groups[g].up, i, x.c_str(), y.c_str());
            }
        }
    }
    printf("[nbr_replay] %s: %ld/%ld lines differ over %zu groups\n", label, bad, lines, a.groups.size());
    return bad;
}

#endif

static long count_lines(const ReplayResult &r, const char *type)
{
    long n = 0;
    for (size_t g = 0; g < r.groups.size(); g++)
        for (size_t i = 0; i < r.groups[g].actual.size(); i++)
            if (nbr_line_type(r.groups[g].actual[i]) == type)
                n++;
    return n;
}

// --- Wave 3: categories the dense reference cannot produce -----------------
//
// ME decoupling (concept 4.6, wave 3): a frame dropped for DROP TOK/LOOP now
// still gets its ME step when its last token is valid and not my own call.
// The dense code (and the capture, made by it) has only the DROP line for
// such a frame; the edge pool adds the ME line plus, if the last hop had no
// row yet, the EVICT/EVICT-E that making its row caused. Those lines -- and
// only those, and only in a group whose reference side holds a DROP TOK or
// DROP LOOP -- are the ME_DECOUPLED category, removed before comparing.
// EVICT-H/EVICT-X/ECHO are stage-2 line types the dense code never emits;
// the harness does not drive nbrNoteDirect()/nbrNoteOwnTx(), and the
// horizon (NBR_HZ_ENTRIES) never fills on this fixture, so they are counted
// and asserted to be 0, not normalised away.

static bool is_drop_tok_or_loop(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    return fld.size() > 3 && fld[1] == "DROP" && (fld[3] == "TOK" || fld[3] == "LOOP");
}

// Removes from `lines` the ME/EVICT/EVICT-E lines that `ref` does not hold,
// when `ref` has a DROP TOK/LOOP line. Returns how many were removed.
static long strip_me_decoupled(const std::vector<std::string> &ref, std::vector<std::string> &lines)
{
    bool dropped = false;
    for (size_t i = 0; i < ref.size() && !dropped; i++)
        dropped = is_drop_tok_or_loop(ref[i]);
    if (!dropped)
        return 0;
    std::multiset<std::string> have(ref.begin(), ref.end());
    std::vector<std::string> kept;
    long removed = 0;
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string t = nbr_line_type(lines[i]);
        std::multiset<std::string>::iterator it = have.find(lines[i]);
        if (it != have.end())
        {
            have.erase(it);
            kept.push_back(lines[i]);
        }
        else if (t == "ME" || t == "EVICT" || t == "EVICT-E")
            removed++;
        else
            kept.push_back(lines[i]);
    }
    lines.swap(kept);
    return removed;
}

// Copy of `run` with the ME_DECOUPLED lines removed relative to `ref` (same
// fixture, one group per trigger in both). `against_expected`: compare with
// the capture's own lines (g.expected) instead of another run's actual lines.
__attribute__((unused)) static ReplayResult strip_run(const ReplayResult &ref, const ReplayResult &run, bool against_expected,
                              long *removed)
{
    ReplayResult out = run;
    *removed = 0;
    for (size_t g = 0; g < out.groups.size() && g < ref.groups.size(); g++)
    {
        const std::vector<std::string> &r = against_expected ? run.groups[g].expected : ref.groups[g].actual;
        *removed += strip_me_decoupled(r, out.groups[g].actual);
    }
    return out;
}

__attribute__((unused)) static long count_stage2_types(const ReplayResult &r)
{
    return count_lines(r, "EVICT-H") + count_lines(r, "EVICT-X") + count_lines(r, "ECHO");
}

// --- tests -------------------------------------------------------------

void setUp(void) {}
void tearDown(void)
{
    dense::nbrLog = NULL;
    edge::nbrLog = NULL;
#ifndef NBR_REPLAY_PRODUCTION
    edge_deferred::nbrLog = NULL;
#endif
    g_capture = NULL;
}

void test_decode_pos_payload_matches_known_capture_example(void)
{
    DecodedPos d = decode_pos_payload("4825.35N\\01147.19E-Marzling#Werner/R=9;");
    TEST_ASSERT_TRUE(d.ok);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.5f|%.5f", (double)d.lat, (double)d.lon);
    TEST_ASSERT_EQUAL_STRING("48.42250|11.78650", buf);
}

void test_decode_pos_payload_applies_south_west_sign(void)
{
    DecodedPos d = decode_pos_payload("4825.35S\\01147.19W-x");
    TEST_ASSERT_TRUE(d.ok);
    TEST_ASSERT_TRUE(d.lat < 0.0f);
    TEST_ASSERT_TRUE(d.lon < 0.0f);
}

void test_token_first_last_and_split_pipe(void)
{
    TEST_ASSERT_EQUAL_STRING("A", token_first("A,B,C").c_str());
    TEST_ASSERT_EQUAL_STRING("C", token_last("A,B,C").c_str());
    TEST_ASSERT_EQUAL_STRING("A", token_first("A").c_str());
    TEST_ASSERT_EQUAL_STRING("A", token_last("A").c_str());

    std::vector<std::string> f = split_pipe("[NBR]|NEED|1|2|3");
    TEST_ASSERT_EQUAL_INT(5, (int)f.size());
    TEST_ASSERT_EQUAL_STRING("NEED", f[1].c_str());
    TEST_ASSERT_EQUAL_STRING("[NBR]|NEED|1|2|3", join_pipe(f).c_str());
}

void test_normalize_exact_ignores_only_the_need_slot_field(void)
{
    TEST_ASSERT_EQUAL_STRING(
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|7|00000000").c_str(),
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|-1|00000000").c_str());
    TEST_ASSERT_TRUE(
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000001|00000000|7|00000000") !=
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|-1|00000000"));
    TEST_ASSERT_TRUE(
        normalize_exact("[NBR]|EDGE|1|A|B|P|0|1|NA") != normalize_exact("[NBR]|EDGE|1|A|B|P|0|2|NA"));
}

void test_mask_to_names_sorts_and_marks_unknown_index(void)
{
    IdxToCall table;
    table[1] = "AAA-1";
    table[3] = "CCC-3";
    table[100] = "ZZZ-9";
    HMask mask = hm_parse_hex("0000001000000000000000000000002A"); // bits 1, 3, 5, 100
    TEST_ASSERT_EQUAL_STRING("?5,AAA-1,CCC-3,ZZZ-9", mask_to_names(mask, table).c_str());
    TEST_ASSERT_EQUAL_STRING("-", mask_to_names(hm_none(), table).c_str());
    // 8, 16 and 32 digits of the same low set parse to the same set.
    TEST_ASSERT_EQUAL_STRING(mask_to_names(hm_parse_hex("0000002A"), table).c_str(),
                             mask_to_names(hm_parse_hex("000000000000002A"), table).c_str());
}

void test_normalize_indexfree_strips_row_and_evict_idx_and_rewrites_need_masks(void)
{
    IdxToCall table;
    table[2] = "OE1AAA-1";
    table[5] = "OE1BBB-2";

    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|ROW|10|-|OE1AAA-1|1|0|3|UNK|NA",
        normalize_indexfree("[NBR]|ROW|10|2|OE1AAA-1|1|0|3|UNK|NA", table).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|EVICT|10|-|OLD-1|OE1AAA-1",
        normalize_indexfree("[NBR]|EVICT|10|2|OLD-1|OE1AAA-1", table).c_str());

    std::string got = normalize_indexfree(
        "[NBR]|NEED|10|AABBCCDD|P|B|00000024|00000004|7|00000020", table);
    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|NEED|10|AABBCCDD|P|B|OE1AAA-1,OE1BBB-2|OE1AAA-1|-|OE1BBB-2", got.c_str());
}

// The differential normalisation is exactly the two the brief allows: <cnt>
// of EDGE/ME, and mask width. Everything else must still differ.
void test_normalize_b_allows_only_cnt_and_mask_width(void)
{
    TEST_ASSERT_EQUAL_STRING(normalize_b("[NBR]|EDGE|1|A|B|P|0|1|NA").c_str(),
                             normalize_b("[NBR]|EDGE|1|A|B|P|0|7|NA").c_str());
    TEST_ASSERT_EQUAL_STRING(normalize_b("[NBR]|ME|1|B|P|-90|1|5").c_str(),
                             normalize_b("[NBR]|ME|1|B|P|-90|3|5").c_str());
    TEST_ASSERT_TRUE(normalize_b("[NBR]|ME|1|B|P|-90|1|5") != normalize_b("[NBR]|ME|1|B|P|-90|1|6"));
    TEST_ASSERT_EQUAL_STRING(
        normalize_b("[NBR]|NEED|1|AABBCCDD|P|B|00000024|00000004|-1|00000000").c_str(),
        normalize_b("[NBR]|NEED|1|AABBCCDD|P|B|0000000000000024|0000000000000004|-1|0000000000000000").c_str());
    TEST_ASSERT_TRUE(normalize_b("[NBR]|NEED|1|AABBCCDD|P|B|00000024|00000004|-1|00000000") !=
                     normalize_b("[NBR]|NEED|1|AABBCCDD|P|A|00000024|00000004|-1|00000000"));
    TEST_ASSERT_TRUE(normalize_b("[NBR]|SNAP|1|X|3|21|16") != normalize_b("[NBR]|SNAP|1|X|3|21|15"));
    TEST_ASSERT_EQUAL_STRING(normalize_s("[NBR]|SNAP|1|X|3|21|16").c_str(),
                             normalize_s("[NBR]|SNAP|1|X|3|21|15").c_str());
}

void test_group_matches_is_order_insensitive_within_a_group(void)
{
    TriggerGroup g;
    g.kind = "LOG";
    g.up = 5;
    g.expected.push_back("[NBR]|EDGE|5|A|B|P|0|1|NA");
    g.expected.push_back("[NBR]|ME|5|B|P|-90|1|NA");
    g.actual.push_back("[NBR]|ME|5|B|P|-90|1|NA");
    g.actual.push_back("[NBR]|EDGE|5|A|B|P|0|1|NA");
    TEST_ASSERT_TRUE(group_matches(g, CompareMode::EXACT));

    g.actual.pop_back();
    TEST_ASSERT_FALSE(group_matches(g, CompareMode::EXACT));
}

// Wave 3: the ME_DECOUPLED category removes exactly the ME/EVICT lines a
// dropped frame gained, and nothing in a group without DROP TOK/LOOP.
void test_strip_me_decoupled_only_touches_dropped_groups(void)
{
    std::vector<std::string> ref, got;
    ref.push_back("[NBR]|DROP|7|LOOP|A-1,B-2,A-1");
    got.push_back("[NBR]|DROP|7|LOOP|A-1,B-2,A-1");
    got.push_back("[NBR]|EVICT|7|3|OLD-1|A-1");
    got.push_back("[NBR]|ME|7|A-1|P|-90|1|5");
    got.push_back("[NBR]|SYM|7|00000001|HASF|X|Y|3");
    TEST_ASSERT_EQUAL_INT(2, (int)strip_me_decoupled(ref, got));
    TEST_ASSERT_EQUAL_INT(2, (int)got.size());
    TEST_ASSERT_EQUAL_STRING("[NBR]|SYM|7|00000001|HASF|X|Y|3", got[1].c_str()); // kept, so the diff still sees it

    std::vector<std::string> ref2, got2;
    ref2.push_back("[NBR]|EDGE|8|A-1|B-2|P|0|1|NA");
    got2.push_back("[NBR]|EDGE|8|A-1|B-2|P|0|1|NA");
    got2.push_back("[NBR]|ME|8|B-2|P|-90|1|5");
    TEST_ASSERT_EQUAL_INT(0, (int)strip_me_decoupled(ref2, got2));
    TEST_ASSERT_EQUAL_INT(2, (int)got2.size());
}

// Wave 2, D: the mask operations on the upper rows (64..127 on the 128-row
// build, the top of the single word on the 64-row build), and the hex form
// the log lines use.
void test_mask_ops_on_upper_rows(void)
{
    const int lo = NBR_MAX_ROWS > 64 ? 64 : NBR_MAX_ROWS / 2;
    const int hi = NBR_MAX_ROWS - 1;
    NbrMask m = nbrMaskNone();
    TEST_ASSERT_TRUE(nbrMaskEmpty(m));
    nbrMaskSet(m, lo);
    nbrMaskSet(m, hi);
    nbrMaskSet(m, (lo + hi) / 2);
    TEST_ASSERT_TRUE(nbrMaskTest(m, lo));
    TEST_ASSERT_TRUE(nbrMaskTest(m, hi));
    TEST_ASSERT_TRUE(nbrMaskTest(m, (lo + hi) / 2));
    TEST_ASSERT_FALSE(nbrMaskTest(m, lo + 1));
    TEST_ASSERT_FALSE(nbrMaskTest(m, NBR_MAX_ROWS));   // ausserhalb: nie gesetzt
    nbrMaskSet(m, NBR_MAX_ROWS);                       // ausserhalb: kein Effekt
    TEST_ASSERT_EQUAL_INT(3, nbrMaskCount(m));

    // Iteration in aufsteigender Reihenfolge ueber die Wortgrenze.
    nbrMaskSet(m, 1);
    nbrMaskSet(m, 63);
    std::vector<int> seen;
    for (int i = nbrMaskNext(m, -1); i >= 0; i = nbrMaskNext(m, i))
        seen.push_back(i);
    std::set<int> ws;
    ws.insert(1);
    if (63 < NBR_MAX_ROWS)
        ws.insert(63);
    ws.insert(lo);
    ws.insert((lo + hi) / 2);
    ws.insert(hi);
    std::vector<int> wv(ws.begin(), ws.end());
    TEST_ASSERT_EQUAL_INT((int)wv.size(), (int)seen.size());
    for (size_t i = 0; i < wv.size(); i++)
        TEST_ASSERT_EQUAL_INT(wv[i], seen[i]);
    TEST_ASSERT_EQUAL_INT(-1, nbrMaskNext(m, hi));

    nbrMaskClear(m, hi);
    TEST_ASSERT_FALSE(nbrMaskTest(m, hi));
    NbrMask b = nbrMaskBit(lo);
    TEST_ASSERT_TRUE(nbrMaskEqual(nbrMaskAnd(m, b), b));
    TEST_ASSERT_FALSE(nbrMaskTest(nbrMaskAndNot(m, b), lo));
    TEST_ASSERT_TRUE(nbrMaskTest(nbrMaskOr(nbrMaskNone(), b), lo));

    // Hex: NBR_MASK_HEX_LEN Stellen, hoechstwertige zuerst, %08lX-Haelften.
    char buf[NBR_MASK_HEX_LEN + 1];
    NbrMask one = nbrMaskBit(hi);
    TEST_ASSERT_EQUAL_INT(NBR_MASK_HEX_LEN, edge::nbrMaskHex(one, buf, sizeof(buf)));
    std::string want_hex(NBR_MASK_HEX_LEN, '0');
    int digit = hi / 4, nib = 1 << (hi % 4);
    want_hex[NBR_MASK_HEX_LEN - 1 - digit] = "0123456789ABCDEF"[nib];
    TEST_ASSERT_EQUAL_STRING(want_hex.c_str(), buf);
    HMask back = hm_parse_hex(buf);
    TEST_ASSERT_TRUE(hm_test(back, hi));
    TEST_ASSERT_EQUAL_INT(NBR_MASK_WORDS * 16, (int)strlen(buf));
#if NBR_MAX_ROWS > 64
    TEST_ASSERT_EQUAL_INT(32, NBR_MASK_HEX_LEN);
    NbrMask two = nbrMaskBit(64);
    nbrMaskSet(two, 0);
    edge::nbrMaskHex(two, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0000000000000001" "0000000000000001", buf);
#else
    TEST_ASSERT_EQUAL_INT(16, NBR_MASK_HEX_LEN);
#endif
}

#ifndef NBR_REPLAY_PRODUCTION

// Convergence minute, see wave 1: the boot-gap stops showing after up 180.
static const uint16_t CONVERGENCE_MINUTE = 180;

static ReplayResult &dense_run()
{
    static ReplayResult r;
    static bool done = false;
    if (!done)
    {
        run_replay<DenseA>(fixture_path(), r, ReplayOpts());
        done = true;
    }
    return r;
}

static ReplayResult &edge_run(bool sweep)
{
    static ReplayResult r[2];
    static bool done[2] = {false, false};
    if (!done[sweep])
    {
        ReplayOpts o;
        o.sweep_each_minute = sweep;
        run_replay<EdgeA>(fixture_path(), r[sweep], o);
        done[sweep] = true;
    }
    return r[sweep];
}

// The wave-1 instrument, now on the edge pool (no sweep, the compat build):
// every [NBR] line it produces against the capture, grouped by trigger.
void test_replay_reproduces_dk5en98_20260923_boot(void)
{
    TEST_ASSERT_TRUE_MESSAGE(path_exists(fixture_path()), ("fixture missing: " + fixture_path()).c_str());
    long me_decoupled = 0;
    ReplayResult rr = strip_run(edge_run(false), edge_run(false), true, &me_decoupled);
    printf("[nbr_replay] capture: ME_DECOUPLED lines (TOK/LOOP frames with ME) = %ld, stage-2 types = %ld\n",
           me_decoupled, count_stage2_types(rr));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)count_stage2_types(rr), "EVICT-H/EVICT-X/ECHO on the compat replay");

    GroupReport decision_rep = build_group_report(rr.groups, CompareMode::DECISION);
    print_hourly_table("DECISION (edge pool)", decision_rep);
    printf("[nbr_replay] parse_errors=%zu need_no_path=%ld total_groups=%zu\n",
           rr.parse_errors.size(), rr.need_no_path, rr.groups.size());
    for (size_t i = 0; i < rr.parse_errors.size() && i < 5; i++)
        printf("[nbr_replay] parse error: %s\n", rr.parse_errors[i].c_str());

    long post_total = 0, post_mismatched = 0;
    std::vector<const TriggerGroup *> post_fail;
    for (size_t i = 0; i < rr.groups.size(); i++)
    {
        const TriggerGroup &g = rr.groups[i];
        if (g.up < CONVERGENCE_MINUTE)
            continue;
        post_total++;
        if (!group_matches(g, CompareMode::DECISION))
        {
            post_mismatched++;
            if (post_fail.size() < 10)
                post_fail.push_back(&g);
        }
    }
    printf("[nbr_replay] post-convergence (up>=%u), decision mode: %ld/%ld groups mismatched\n",
           (unsigned)CONVERGENCE_MINUTE, post_mismatched, post_total);
    for (size_t i = 0; i < post_fail.size(); i++)
    {
        const TriggerGroup &g = *post_fail[i];
        printf("  #%zu kind=%s up=%u category=%s\n", i, g.kind.c_str(), (unsigned)g.up,
               join_comma(group_types(g)).c_str());
        for (size_t j = 0; j < g.expected.size(); j++)
            printf("      expected: %s\n", g.expected[j].c_str());
        for (size_t j = 0; j < g.actual.size(); j++)
            printf("      actual:   %s\n", g.actual[j].c_str());
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)rr.parse_errors.size(), "unparseable [LOG] line(s) in the fixture");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)rr.need_no_path,
                                  "NEED line(s) whose msg_id had no preceding [LOG] line in the fixture");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)post_mismatched,
                                  "post-convergence decision-mode group mismatches against the capture");
}

// B: dense reference vs. edge pool, line for line, every group, every hour.
void test_differential_dense_vs_edge_pool_compat(void)
{
    ReplayResult &d = dense_run();
    long me_decoupled = 0;
    ReplayResult e = strip_run(d, edge_run(false), false, &me_decoupled);
    TEST_ASSERT_TRUE(d.groups.size() > 1000);
    long bad = diff_runs("B dense vs edge", d, e, normalize_b);
    printf("[nbr_replay] B: ME_DECOUPLED lines removed = %ld, stage-2 types = %ld\n", me_decoupled,
           count_stage2_types(e));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)count_stage2_types(e), "EVICT-H/EVICT-X/ECHO on the compat replay");
    printf("[nbr_replay] B: EVICT dense=%ld edge=%ld, EVICT-E edge=%ld, SYM dense=%ld edge=%ld\n",
           count_lines(d, "EVICT"), count_lines(e, "EVICT"), count_lines(e, "EVICT-E"),
           count_lines(d, "SYM"), count_lines(e, "SYM"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)count_lines(e, "EVICT-E"), "compat pool (441) must never fill");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)bad, "dense and edge pool differ beyond <cnt> and mask width");
}

// B': the same differential against the nRF52 logging path (SYM lines
// recorded under the clamp and printed after it).
void test_differential_dense_vs_edge_pool_deferred_log(void)
{
    static ReplayResult raw;
    run_replay<EdgeDeferredA>(fixture_path(), raw, ReplayOpts());
    long me_decoupled = 0;
    ReplayResult r = strip_run(dense_run(), raw, false, &me_decoupled);
    long bad = diff_runs("B' dense vs edge (deferred SYM)", dense_run(), r, normalize_b);
    printf("[nbr_replay] B': ME_DECOUPLED lines removed = %ld\n", me_decoupled);
    long drops = 0;
    for (size_t g = 0; g < r.groups.size(); g++)
        for (size_t i = 0; i < r.groups[g].actual.size(); i++)
            if (r.groups[g].actual[i].find("|SYMBUF|") != std::string::npos)
                drops++;
    printf("[nbr_replay] B': SYM dense=%ld deferred=%ld, SYMBUF drops=%ld\n", count_lines(dense_run(), "SYM"),
           count_lines(r, "SYM"), drops);
    TEST_ASSERT_EQUAL_INT(0, (int)drops);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)bad, "deferred SYM path differs from dense");
}

// S: the sweep is cleanup only -- swept every minute or never, same lines.
void test_sweep_independence_compat(void)
{
    ReplayResult &a = edge_run(true);
    ReplayResult &b = edge_run(false);
    long bad = diff_runs("S sweep vs no sweep", a, b, normalize_s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)bad, "decisions depend on whether nbrSweep() ran");
}

#else // NBR_REPLAY_PRODUCTION

// --- C: production rules vs. dense ---------------------------------------------

typedef std::map<std::string, std::string> DecisionMap;

// Per group: NEED and ESELF from the run, ROW verdict|meshneed from the
// group's own ROW lines (a row missing on one side reads "(absent)").
static std::vector<DecisionMap> extract_decisions(const ReplayResult &r)
{
    std::vector<DecisionMap> out(r.groups.size());
    for (size_t g = 0; g < r.groups.size(); g++)
    {
        const TriggerGroup &tg = r.groups[g];
        for (size_t i = 0; i < tg.decisions.size(); i++)
            out[g][tg.decisions[i].first] = tg.decisions[i].second;
        for (size_t i = 0; i < tg.actual.size(); i++)
        {
            std::vector<std::string> fld = split_pipe(tg.actual[i]);
            if (fld.size() > 9 && fld[1] == "ROW")
                out[g]["ROW|" + fld[4]] = fld[8] + "|" + fld[9];
        }
    }
    return out;
}

static std::string dget(const DecisionMap &m, const std::string &k)
{
    DecisionMap::const_iterator it = m.find(k);
    return it == m.end() ? std::string("(absent)") : it->second;
}

// Cumulative count of a line type up to and including each group.
static std::vector<long> cumulative(const ReplayResult &r, const char *type)
{
    std::vector<long> c(r.groups.size());
    long n = 0;
    for (size_t g = 0; g < r.groups.size(); g++)
    {
        for (size_t i = 0; i < r.groups[g].actual.size(); i++)
            if (nbr_line_type(r.groups[g].actual[i]) == type)
                n++;
        c[g] = n;
    }
    return c;
}

void test_production_rules_against_dense(void)
{
    ReplayOpts dense_opt;
    dense_opt.probe = true;
    ReplayOpts edge_opt;
    edge_opt.probe = true;
    edge_opt.sweep_each_minute = true; // the loop task does, and only the sweep halves

    static ReplayResult rd, rp, rs, rn;
    run_replay<DenseA>(fixture_path(), rd, dense_opt);
    run_replay<EdgeA>(fixture_path(), rp, edge_opt);
    run_replay<EdgeNoShareA>(fixture_path(), rs, edge_opt);
    run_replay<EdgeNoRulesA>(fixture_path(), rn, edge_opt);
    TEST_ASSERT_EQUAL_INT(0, (int)rd.parse_errors.size());
    TEST_ASSERT_EQUAL_INT((int)rd.groups.size(), (int)rp.groups.size());
    TEST_ASSERT_EQUAL_INT((int)rd.groups.size(), (int)rs.groups.size());
    TEST_ASSERT_EQUAL_INT((int)rd.groups.size(), (int)rn.groups.size());

    std::vector<DecisionMap> dd = extract_decisions(rd), dp = extract_decisions(rp),
                             ds = extract_decisions(rs), dn = extract_decisions(rn);
    std::vector<long> dense_evict = cumulative(rd, "EVICT");
    std::vector<long> norules_evicte = cumulative(rn, "EVICT-E");

    std::map<std::string, long> cat_count;
    cat_count["SHARE"] = 0;
    cat_count["HALVE/SNRAVG"] = 0;
    cat_count["CAPACITY"] = 0;
    cat_count["OTHER"] = 0;
    long compared = 0;
    printf("\n[nbr_replay] C: rows=%d edges=%d share=%d%% halve=%dmin snr_avg=%d vs dense %d rows\n",
           (int)NBR_MAX_ROWS, (int)NBR_MAX_EDGES, (int)NBR_SHARE_PCT, (int)NBR_CNT_HALVE_MIN,
           (int)NBR_SNR_AVG_N, (int)NBR_DENSE_ROWS);
    printf("[nbr_replay] C: every differing decision (group up kind key: dense => production [category])\n");
    for (size_t g = 0; g < rd.groups.size(); g++)
    {
        std::set<std::string> keys;
        for (DecisionMap::const_iterator it = dd[g].begin(); it != dd[g].end(); ++it)
            keys.insert(it->first);
        for (DecisionMap::const_iterator it = dp[g].begin(); it != dp[g].end(); ++it)
            keys.insert(it->first);
        for (std::set<std::string>::const_iterator k = keys.begin(); k != keys.end(); ++k)
        {
            compared++;
            std::string vd = dget(dd[g], *k), vp = dget(dp[g], *k);
            if (vd == vp)
                continue;
            std::string vs = dget(ds[g], *k), vn = dget(dn[g], *k);
            const char *cat;
            if (vn != vd)
                cat = (dense_evict[g] > 0 && norules_evicte[g] == 0) ? "CAPACITY" : "OTHER";
            else if (vs != vn)
                cat = "HALVE/SNRAVG";
            else if (vp != vs)
                cat = "SHARE";
            else
                cat = "OTHER";
            cat_count[cat]++;
            printf("  g%zu up=%u %s %s: %s => %s [%s]\n", g, (unsigned)rd.groups[g].up, rd.groups[g].kind.c_str(),
                   k->c_str(), vd.c_str(), vp.c_str(), cat);
        }
    }
    printf("[nbr_replay] C: %ld decisions compared; differing: SHARE=%ld HALVE/SNRAVG=%ld CAPACITY=%ld OTHER=%ld\n",
           compared, cat_count["SHARE"], cat_count["HALVE/SNRAVG"], cat_count["CAPACITY"], cat_count["OTHER"]);
    printf("[nbr_replay] C: EVICT dense=%ld production=%ld, EVICT-E production=%ld noshare=%ld norules=%ld\n",
           count_lines(rd, "EVICT"), count_lines(rp, "EVICT"), count_lines(rp, "EVICT-E"),
           count_lines(rs, "EVICT-E"), count_lines(rn, "EVICT-E"));

    // Concept 4.3: at the final snapshot DB0FHR-12 belongs to DB0ED-99 alone
    // under the share rule; the dense one-hit rule shows DB0ED-99 with #X 0.
    int last = -1;
    for (size_t g = 0; g < rd.groups.size(); g++)
        if (rd.groups[g].kind == "SNAP")
            last = (int)g;
    TEST_ASSERT_TRUE(last >= 0);
    const TriggerGroup &gd = rd.groups[last], &gp = rp.groups[last];
    printf("[nbr_replay] C: final SNAP up=%u: %s #X dense=%d production=%d {%s}\n", (unsigned)gp.up, PROBE_CALL,
           gd.probe_x, gp.probe_x, gp.probe_set.c_str());
    // Isolated rule effects (report only): the same pool with and without
    // the share rule, and with and without halving + SNR mean, over the
    // categorised decisions plus #X per row. Capacity plays no part here.
    long share_eff = 0, halve_eff = 0;
    printf("[nbr_replay] C: isolated rule effects (key: without rule => with rule)\n");
    for (size_t g = 0; g < rp.groups.size(); g++)
    {
        DecisionMap xp = dp[g], xs = ds[g], xn = dn[g];
        for (size_t i = 0; i < rp.groups[g].xcounts.size(); i++)
            xp[rp.groups[g].xcounts[i].first] = rp.groups[g].xcounts[i].second;
        for (size_t i = 0; i < rs.groups[g].xcounts.size(); i++)
            xs[rs.groups[g].xcounts[i].first] = rs.groups[g].xcounts[i].second;
        for (size_t i = 0; i < rn.groups[g].xcounts.size(); i++)
            xn[rn.groups[g].xcounts[i].first] = rn.groups[g].xcounts[i].second;
        std::set<std::string> keys;
        for (DecisionMap::const_iterator it = xp.begin(); it != xp.end(); ++it)
            keys.insert(it->first);
        for (DecisionMap::const_iterator it = xs.begin(); it != xs.end(); ++it)
            keys.insert(it->first);
        for (DecisionMap::const_iterator it = xn.begin(); it != xn.end(); ++it)
            keys.insert(it->first);
        for (std::set<std::string>::const_iterator k = keys.begin(); k != keys.end(); ++k)
        {
            std::string vp = dget(xp, *k), vs = dget(xs, *k), vn = dget(xn, *k);
            if (vs != vn)
            {
                halve_eff++;
                printf("  g%zu up=%u %s: %s => %s [HALVE/SNRAVG]\n", g, (unsigned)rp.groups[g].up, k->c_str(),
                       vn.c_str(), vs.c_str());
            }
            if (vp != vs)
            {
                share_eff++;
                printf("  g%zu up=%u %s: %s => %s [SHARE]\n", g, (unsigned)rp.groups[g].up, k->c_str(),
                       vs.c_str(), vp.c_str());
            }
        }
    }
    printf("[nbr_replay] C: isolated effects: SHARE=%ld HALVE/SNRAVG=%ld\n", share_eff, halve_eff);

    // Trend of the check over all snapshots (report only).
    long snaps = 0, dense_zero = 0, prod_zero = 0, prod_has = 0;
    for (size_t g = 0; g < rd.groups.size(); g++)
    {
        if (rd.groups[g].kind != "SNAP" || rd.groups[g].probe_x < 0 || rp.groups[g].probe_x < 0)
            continue;
        snaps++;
        if (rd.groups[g].probe_x == 0)
            dense_zero++;
        if (rp.groups[g].probe_x == 0)
            prod_zero++;
        if (("," + rp.groups[g].probe_set + ",").find(std::string(",") + PROBE_EXCL + ",") != std::string::npos)
            prod_has++;
    }
    printf("[nbr_replay] C: %s over %ld snapshots: #X==0 dense %ld, production %ld; %s in its set %ld\n",
           PROBE_CALL, snaps, dense_zero, prod_zero, PROBE_EXCL, prod_has);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)cat_count["OTHER"], "unexplained dense/production differences");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, gd.probe_x, "dense: DB0ED-99 #X at the final snapshot");
    TEST_ASSERT_TRUE_MESSAGE(gp.probe_x >= 1, "production: DB0ED-99 #X >= 1 at the final snapshot");
    TEST_ASSERT_TRUE_MESSAGE(
        ("," + gp.probe_set + ",").find(std::string(",") + PROBE_EXCL + ",") != std::string::npos,
        "production: DB0FHR-12 in DB0ED-99's exclusive set at the final snapshot");
}

#endif // NBR_REPLAY_PRODUCTION

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_pos_payload_matches_known_capture_example);
    RUN_TEST(test_decode_pos_payload_applies_south_west_sign);
    RUN_TEST(test_token_first_last_and_split_pipe);
    RUN_TEST(test_normalize_exact_ignores_only_the_need_slot_field);
    RUN_TEST(test_mask_to_names_sorts_and_marks_unknown_index);
    RUN_TEST(test_normalize_indexfree_strips_row_and_evict_idx_and_rewrites_need_masks);
    RUN_TEST(test_normalize_b_allows_only_cnt_and_mask_width);
    RUN_TEST(test_group_matches_is_order_insensitive_within_a_group);
    RUN_TEST(test_strip_me_decoupled_only_touches_dropped_groups);
    RUN_TEST(test_mask_ops_on_upper_rows);
#ifndef NBR_REPLAY_PRODUCTION
    RUN_TEST(test_replay_reproduces_dk5en98_20260923_boot);
    RUN_TEST(test_differential_dense_vs_edge_pool_compat);
    RUN_TEST(test_differential_dense_vs_edge_pool_deferred_log);
    RUN_TEST(test_sweep_independence_compat);
#else
    RUN_TEST(test_production_rules_against_dense);
#endif
    return UNITY_END();
}
