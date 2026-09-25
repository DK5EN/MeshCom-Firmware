// MeshCom 5 topology, wave 1 (docs/meshcom5-campaign.md), brief W1a: a host
// replay harness that feeds a real field capture (DK5EN-98, cold boot
// 2026-09-23 20:02:19 through 2026-09-24 09:28, firmware 8487ea2a -- see
// ASSUMPTION 2 below for how the fixture was assembled from two raw days)
// through the CURRENT dense neighbour matrix
// (src/nbr_matrix.{h,cpp}) and checks that it reproduces the capture's
// [NBR] lines. This is the regression instrument for the upcoming rewrite
// of nbr_matrix into an edge pool: run the SAME fixture through the old and
// the new code and diff the two "actual" streams against each other.
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
// --- Seam for a wave-2 differential test (nbr_matrix.cpp vs. an edge-pool
// rewrite) -- NBR_MATRIX_SRC below, per orchestrator instruction: make the
// seam, do not build the frozen copy or the differential test here. -------
//
// A future env points NBR_MATRIX_SRC at a second implementation (build_flags
// `-D NBR_MATRIX_SRC='"path/to/edge_pool.cpp"'`) and builds this SAME file a
// second time in its own pio env -- two binaries, not one process linking
// both (nbrNoteFrame() et al. are free functions; two implementations can't
// share a translation unit under the same names). run_replay() needs no
// change either way: it only calls the named functions/globals the macro's
// target provides (NbrMatrix, nbrInit, nbrNoteFrame, nbrNotePos,
// nbrRelayNeed, nbrCoverMask, nbrLogSnapshot, nbrBuildReport, nbrLog,
// NBR_FLAG_*, NbrNeed, NBR_MAX_ROWS), so a same-shaped edge-pool
// implementation is a drop-in. Each build's ReplayResult.groups (flattened
// to its own expected/actual streams) is the artifact a wave-2 test dumps
// and diffs against the other build's -- not implemented here.

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

#ifndef NBR_MATRIX_SRC
#define NBR_MATRIX_SRC "../../src/nbr_matrix.cpp"
#endif
#include NBR_MATRIX_SRC

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

// Returns false (leaving *err set) on a line this parser cannot make sense
// of -- the caller reports that as a hard failure rather than skipping it
// silently, since tools/nbr_replay_extract.py already filtered to lines
// that are supposed to have this exact shape.
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

// --- Stufe-2 ring model (see file header comment) ---------------------------

struct RingEntry
{
    uint32_t need;
    uint32_t alone;
    bool active;
    bool counted;
};

static const char *OWN_CALL = "DK5EN-98";
static const bool BNBRSYM = true;
static const bool BGATEWAY = true;

// --- trigger groups (see ASSUMPTION 2c) -------------------------------------
//
// One group per [LOG] frame (its own Stufe-2 CANCEL/REFUSE/SYM, Stufe-1
// CUT/DROP/EDGE*/EVICT*/ME/POS, and -- still the SAME group, see run_replay()
// -- its NEED/SYM if the capture shows one for this frame's msg_id), one per
// SNAP..ENDSNAP block, one per RPTTX. idx_to_call is a snapshot for
// normalize_indexfree() (see below), taken once per group right after that
// group's own row-mutating call -- see the file header comment for the
// (documented, best-effort) timing this implies for Stufe-2 lines.
struct TriggerGroup
{
    std::string kind; // "LOG" | "SNAP" | "RPTTX"
    uint16_t up;
    std::vector<std::string> expected;
    std::vector<std::string> actual;
    std::unordered_map<int, std::string> exp_idx_to_call;
    std::unordered_map<int, std::string> act_idx_to_call;
};

static std::unordered_map<int, std::string> snapshot_actual_idx_to_call(const NbrMatrix &m)
{
    std::unordered_map<int, std::string> t;
    t[0] = m.rows[0].call;
    for (int i = 1; i < NBR_MAX_ROWS; i++)
        if (m.rows[i].flags & NBR_FLAG_USED)
            t[i] = m.rows[i].call;
    return t;
}

// --- replay: fixture -> ordered TriggerGroups -------------------------------
//
// Kept deliberately separate from the comparison below: a later wave can
// call run_replay() again against a different nbr_matrix.cpp (see the
// NBR_MATRIX_SRC seam in the file header) and diff its `groups` against
// THIS run's, without touching the comparator at all.

struct ReplayResult
{
    std::vector<TriggerGroup> groups;
    std::vector<std::string> parse_errors; // "<lineno>: <reason>: <line>"
    long need_no_path = 0;                 // NEED lines whose msg_id had no preceding [LOG] line
};

static void run_replay(const std::string &fixture_path, ReplayResult &out)
{
    std::ifstream f(fixture_path.c_str());
    if (!f.good())
    {
        out.parse_errors.push_back("cannot open fixture: " + fixture_path);
        return;
    }

    NbrMatrix m;
    memset(&m, 0, sizeof(m)); // rows[0].call == "" -- the lazy-init sentinel, same as a real cold matrix

    std::unordered_map<uint32_t, RingEntry> ring;
    std::unordered_map<uint32_t, std::string> last_path;
    std::unordered_map<int, std::string> exp_idx_to_call; // rolling, best-effort (see TriggerGroup comment)

    nbrLog = capture_cb;

    TriggerGroup *cur = NULL;     // group currently receiving g_capture output / stray expected lines
    TriggerGroup *cur_log = NULL; // most recent LOG-kind group -- NEED attaches here, not to `cur`

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

            // Decode failure (FCS:0000): logged by the firmware, never fed to
            // the matrix -- keep the (empty) group so the trigger is counted.
            if (!fr.decoded)
                continue;

            std::string last_hop = token_last(fr.path);
            bool has_comma = fr.path.find(',') != std::string::npos;

            // 1) Stufe 2 cover-check -- BEFORE lazy init/Stufe 1, same order
            //    as lora_functions.cpp (see file header comment).
            if (has_comma && last_hop != OWN_CALL)
            {
                uint32_t cover_gate = nbrCoverMask(m, last_hop.c_str(), fr.now_min, BNBRSYM, 0, 0, NULL);
                if (cover_gate != 0)
                {
                    std::unordered_map<uint32_t, RingEntry>::iterator it = ring.find(fr.msg_id);
                    if (it != ring.end() && it->second.active)
                    {
                        RingEntry &e = it->second;
                        char typ = nbr_typ_char(fr.type);
                        if (e.alone != 0)
                        {
                            if (!e.counted)
                            {
                                e.counted = true;
                                char buf[112];
                                snprintf(buf, sizeof(buf), "[NBR]|REFUSE|%u|%08X|%c|%s|%08X",
                                         (unsigned)fr.now_min, (unsigned)fr.msg_id, typ,
                                         last_hop.c_str(), (unsigned)e.alone);
                                cur->actual.push_back(buf);
                            }
                        }
                        else
                        {
                            uint32_t slot_inferred = 0;
                            uint32_t cover = nbrCoverMask(m, last_hop.c_str(), fr.now_min, BNBRSYM,
                                                           e.need, fr.msg_id, &slot_inferred);
                            uint32_t before = e.need;
                            e.need &= ~cover;
                            uint32_t after = e.need;
                            uint32_t removed_inferred = before & ~after & slot_inferred;
                            if (after == 0)
                            {
                                char buf[112];
                                snprintf(buf, sizeof(buf), "[NBR]|CANCEL|%u|%08X|%c|%s|%08X|%08X|%08X",
                                         (unsigned)fr.now_min, (unsigned)fr.msg_id, typ, last_hop.c_str(),
                                         (unsigned)before, (unsigned)after, (unsigned)removed_inferred);
                                cur->actual.push_back(buf);
                                e.active = false;
                            }
                        }
                    }
                }
            }

            // 2) Lazy nbrInit()/GW-flag (own-position branch intentionally
            //    skipped, see ASSUMPTION 2 in the file header comment).
            if (strcmp(m.rows[0].call, OWN_CALL) != 0)
                nbrInit(m, OWN_CALL, fr.now_min);
            if (BGATEWAY)
                m.rows[0].flags |= NBR_FLAG_GW;

            // 3) Stufe 1.
            bool dest_gw = (fr.dest == "HG");
            nbrNoteFrame(m, fr.path.c_str(), fr.type, fr.payload.c_str(), dest_gw, fr.rssi, fr.snr, fr.now_min);
            last_path[fr.msg_id] = fr.path;

            if (fr.type == '!')
            {
                DecodedPos d = decode_pos_payload(fr.payload);
                if (d.ok)
                    nbrNotePos(m, token_first(fr.path).c_str(), d.lat, d.lon, fr.mesh, fr.hw, fr.now_min);
            }

            // Snapshot both idx->call tables now: this group's own
            // row-mutating calls (Stufe 1 above) are done, and nothing
            // mutates `m` again until the NEXT [LOG] frame's own Stufe 2
            // (see file header comment for the resulting, accepted,
            // Stufe-2-uses-slightly-future-state approximation).
            cur->act_idx_to_call = snapshot_actual_idx_to_call(m);
            cur->exp_idx_to_call = exp_idx_to_call;
        }
        else
        {
            size_t p = line.find("[NBR]|");
            if (p == std::string::npos)
                continue; // neither a [LOG] nor an [NBR] line -- extractor should never emit this
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
                nbrLogSnapshot(m, up);
                cur->act_idx_to_call = snapshot_actual_idx_to_call(m);
                cur->exp_idx_to_call = exp_idx_to_call; // updated below as this block's own ROW lines arrive
            }
            else if (type == "RPTTX")
            {
                out.groups.push_back(TriggerGroup());
                cur = &out.groups.back();
                cur->kind = "RPTTX";
                cur->up = up;
                cur->expected.push_back(nbrtext);
                g_capture = &cur->actual;

                // sendNbrReport() (src/loop_functions.cpp:5246-5299): own
                // periodic HN-report send. heard_count is getMheardCount()
                // (out of the file set) -- an INPUT to nbrBuildReport(), not
                // something it computes, exactly like NEED's msg_id below;
                // taken from the capture's own payload ("R<heard>;...", the
                // same field the real call formats FROM this value, so
                // reading it back is not reading the answer). Everything
                // nbrBuildReport() actually decides is freshly computed.
                int heard_count = 0;
                if (fld.size() > 4 && !fld[4].empty() && fld[4][0] == 'R')
                    heard_count = atoi(fld[4].c_str() + 1);

                char report[160];
                int n = nbrBuildReport(m, up, heard_count, report, sizeof(report));
                char buf[224];
                if (n >= 0)
                    snprintf(buf, sizeof(buf), "[NBR]|RPTTX|%u|%d|%s", (unsigned)up, n, report);
                else
                    snprintf(buf, sizeof(buf), "[NBR]|RPTTX|%u|-1|", (unsigned)up);
                cur->actual.push_back(buf);
                cur->act_idx_to_call = snapshot_actual_idx_to_call(m);
                cur->exp_idx_to_call = exp_idx_to_call;
            }
            else if (type == "NEED")
            {
                // NEED belongs to the [LOG] frame that caused it -- stays in
                // cur_log's group, does NOT open a new one (ASSUMPTION 2c /
                // the orchestrator's grouping rule).
                if (cur_log == NULL)
                {
                    out.need_no_path++;
                    continue;
                }
                cur_log->expected.push_back(nbrtext);
                g_capture = &cur_log->actual;

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
                    NbrNeed r = nbrRelayNeed(m, pit->second.c_str(), up, BNBRSYM, msgid);
                    char case_ch = !r.known ? 'U' : (r.alone != 0 ? 'A' : 'B');
                    char buf[112];
                    // <slot> (8th field) is a TX-ring index this harness cannot
                    // reproduce (see file header comment) -- "-1" placeholder,
                    // normalized away by both comparator modes, never silently.
                    snprintf(buf, sizeof(buf), "[NBR]|NEED|%u|%08X|%c|%c|%08X|%08X|%d|%08X",
                             (unsigned)up, (unsigned)msgid, typ, case_ch,
                             (unsigned)r.need, (unsigned)r.alone, -1, (unsigned)r.inferred);
                    cur_log->actual.push_back(buf);

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
                // EDGE/ME/CUT/DROP/EVICT/POS/SYM/CANCEL/CANCEL?/REFUSE/ROW/
                // ENDSNAP: byproducts of the trigger already open above.
                if (cur != NULL)
                    cur->expected.push_back(nbrtext);
                else
                    out.parse_errors.push_back(
                        std::to_string(lineno) + ": [NBR] line before any trigger: " + nbrtext);

                // Roll the EXPECTED-side idx->call table forward as ROW/
                // EVICT lines reveal it (see TriggerGroup comment).
                if (type == "ROW" && fld.size() > 4)
                    exp_idx_to_call[atoi(fld[3].c_str())] = fld[4];
                else if (type == "EVICT" && fld.size() > 5)
                    exp_idx_to_call[atoi(fld[3].c_str())] = fld[5];
                if (cur != NULL && (type == "ROW" || type == "ENDSNAP"))
                    cur->exp_idx_to_call = exp_idx_to_call; // keep the SNAP group's own snapshot current
            }
        }
    }

    g_capture = NULL;
    nbrLog = NULL;
}

// --- compare: grouped, multiset, two modes (see ASSUMPTION 2c) -------------

// Sorted, comma-joined callsigns for the bits set in mask, "-" if none, an
// unknown index rendered as "?<idx>" rather than silently dropped (an
// idx->call table that is missing an entry is reported as a mismatch
// against a real callsign, not quietly treated as a match).
static std::string mask_to_names(uint32_t mask, const std::unordered_map<int, std::string> &idx_to_call)
{
    std::vector<std::string> names;
    for (int i = 0; i < 32; i++)
    {
        if (!(mask & (1u << (unsigned)i)))
            continue;
        std::unordered_map<int, std::string>::const_iterator it = idx_to_call.find(i);
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

// EXACT mode: byte-identical except NEED's <slot> (TX-ring index, out of
// file set -- see file header comment), always normalized to "-".
static std::string normalize_exact(const std::string &line)
{
    std::vector<std::string> fld = split_pipe(line);
    if (fld.size() > 1 && fld[1] == "NEED" && fld.size() > 8)
        fld[8] = "-";
    return join_pipe(fld);
}

// INDEX-FREE mode: EXACT's rewrite, plus ROW's/EVICT's <idx> field blanked
// (row-table SLOT, not identity) and NEED's/CANCEL's/CANCEL?'s/REFUSE's hex
// bitmasks rewritten to sorted callsign lists via idx_to_call (see
// TriggerGroup / mask_to_names above).
static std::string normalize_indexfree(const std::string &line,
                                        const std::unordered_map<int, std::string> &idx_to_call)
{
    std::vector<std::string> fld = split_pipe(line);
    std::string type = fld.size() > 1 ? fld[1] : std::string();

    if (type == "ROW" && fld.size() > 3)
        fld[3] = "-";
    else if (type == "EVICT" && fld.size() > 3)
        fld[3] = "-";
    else if (type == "NEED" && fld.size() > 9)
    {
        fld[6] = mask_to_names((uint32_t)strtoul(fld[6].c_str(), NULL, 16), idx_to_call);
        fld[7] = mask_to_names((uint32_t)strtoul(fld[7].c_str(), NULL, 16), idx_to_call);
        fld[8] = "-";
        fld[9] = mask_to_names((uint32_t)strtoul(fld[9].c_str(), NULL, 16), idx_to_call);
    }
    else if ((type == "CANCEL" || type == "CANCEL?") && fld.size() > 8)
    {
        fld[6] = mask_to_names((uint32_t)strtoul(fld[6].c_str(), NULL, 16), idx_to_call);
        fld[7] = mask_to_names((uint32_t)strtoul(fld[7].c_str(), NULL, 16), idx_to_call);
        fld[8] = mask_to_names((uint32_t)strtoul(fld[8].c_str(), NULL, 16), idx_to_call);
    }
    else if (type == "REFUSE" && fld.size() > 6)
    {
        fld[6] = mask_to_names((uint32_t)strtoul(fld[6].c_str(), NULL, 16), idx_to_call);
    }
    return join_pipe(fld);
}

enum class CompareMode
{
    EXACT,
    INDEX_FREE,
    DECISION
};

// DECISION mode (orchestrator, wave 1 gate): INDEX_FREE plus two exclusions
// that are properties of the capture, not of the matrix code --
//  - EDGE/ME <cnt> blanked: the per-type counter of a cell that stays fresh
//    for the whole capture never resets, so the ~40 s boot gap
//    (ASSUMPTION 2b) leaves a permanent offset of 1-3 on the core relay
//    pairs (ASSUMPTION 2d);
//  - CANCEL/CANCEL?/REFUSE dropped: whether a slot is still in the TX ring
//    when a foreign copy arrives depends on the device's TX timing, which the
//    harness ring model cannot know (seen: replay CANCELs for slots the
//    device had already sent).
// Everything that IS a matrix decision stays compared exactly: NEED case and
// masks, SYM, EDGE/ME presence and SNR, CUT, EVICT, POS, ROW verdicts, RPTTX.
static bool decision_keeps(const std::string &line)
{
    std::string type = nbr_line_type(line);
    return !(type == "CANCEL" || type == "CANCEL?" || type == "REFUSE");
}

static std::string normalize_decision(const std::string &line,
                                      const std::unordered_map<int, std::string> &idx_to_call)
{
    std::vector<std::string> fld = split_pipe(normalize_indexfree(line, idx_to_call));
    std::string type = fld.size() > 1 ? fld[1] : std::string();
    if (type == "EDGE" && fld.size() > 7)
        fld[7] = "-";
    else if (type == "ME" && fld.size() > 6)
        fld[6] = "-";
    return join_pipe(fld);
}

// Sorted (order-within-group-insensitive), byte-exact-after-normalization
// multiset compare -- see ASSUMPTION 2c.
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

// The set of [NBR] types present in a group (union of expected+actual) --
// the "category" a mismatched group is reported under.
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

// --- per-hour-of-uptime report (see ASSUMPTION 2c) --------------------------

struct HourBucket
{
    long groups = 0;
    long mismatched = 0;
    std::unordered_map<std::string, long> mismatch_by_type;
};

struct GroupReport
{
    std::map<int, HourBucket> hours; // ordered by hour
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

// --- tests -------------------------------------------------------------

void setUp(void) {}
void tearDown(void)
{
    nbrLog = NULL;
    g_capture = NULL;
}

// Known example from the fixture itself (DL2JA-1's beacon, several
// occurrences in test/test_nbr_replay/fixtures/dk5en98-20260923-boot.txt,
// e.g. line 359): "4825.35N\01147.19E" -> the capture's own POS line for
// the same frame says "48.42250|11.78650" -- decode_pos_payload() must
// land on exactly that, via the reimplemented arithmetic (see file header
// comment), not by reading the answer back out of the fixture.
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

// The NEED slot placeholder is the ONLY field BOTH comparator modes are
// told to ignore -- pin that down directly so a future edit cannot widen
// it by accident without a test noticing.
void test_normalize_exact_ignores_only_the_need_slot_field(void)
{
    TEST_ASSERT_EQUAL_STRING(
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|7|00000000").c_str(),
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|-1|00000000").c_str());
    // A non-slot field differing must still differ after normalization.
    TEST_ASSERT_TRUE(
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000001|00000000|7|00000000") !=
        normalize_exact("[NBR]|NEED|1|AABBCCDD|P|B|00000000|00000000|-1|00000000"));
    TEST_ASSERT_EQUAL_STRING(
        normalize_exact("[NBR]|EDGE|1|A|B|P|0|1|NA").c_str(),
        normalize_exact("[NBR]|EDGE|1|A|B|P|0|1|NA").c_str());
    TEST_ASSERT_TRUE(
        normalize_exact("[NBR]|EDGE|1|A|B|P|0|1|NA") != normalize_exact("[NBR]|EDGE|1|A|B|P|0|2|NA"));
}

void test_mask_to_names_sorts_and_marks_unknown_index(void)
{
    std::unordered_map<int, std::string> table;
    table[1] = "AAA-1";
    table[3] = "CCC-3";
    // bit 5 has no table entry -- rendered "?5", not dropped.
    uint32_t mask = (1u << 1) | (1u << 3) | (1u << 5);
    TEST_ASSERT_EQUAL_STRING("?5,AAA-1,CCC-3", mask_to_names(mask, table).c_str());
    TEST_ASSERT_EQUAL_STRING("-", mask_to_names(0, table).c_str());
}

void test_normalize_indexfree_strips_row_and_evict_idx_and_rewrites_need_masks(void)
{
    std::unordered_map<int, std::string> table;
    table[2] = "OE1AAA-1";
    table[5] = "OE1BBB-2";

    // ROW/EVICT <idx> (field 3) is blanked, not compared.
    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|ROW|10|-|OE1AAA-1|1|0|3|UNK|NA",
        normalize_indexfree("[NBR]|ROW|10|2|OE1AAA-1|1|0|3|UNK|NA", table).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|EVICT|10|-|OLD-1|OE1AAA-1",
        normalize_indexfree("[NBR]|EVICT|10|2|OLD-1|OE1AAA-1", table).c_str());

    // NEED's need/alone/inferred masks (bits 2 and 5) become sorted names;
    // <slot> is still blanked, same as normalize_exact().
    std::string got = normalize_indexfree(
        "[NBR]|NEED|10|AABBCCDD|P|B|00000024|00000004|7|00000020", table);
    TEST_ASSERT_EQUAL_STRING(
        "[NBR]|NEED|10|AABBCCDD|P|B|OE1AAA-1,OE1BBB-2|OE1AAA-1|-|OE1BBB-2", got.c_str());
}

// A group compares equal to itself with its lines reshuffled -- the whole
// point of the multiset comparator (see ASSUMPTION 2c): a harmlessly
// different emission order inside one frame's own group must not read as
// a mismatch.
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

    g.actual.pop_back(); // drop the EDGE -- now a real mismatch
    TEST_ASSERT_FALSE(group_matches(g, CompareMode::EXACT));
}

// Convergence minute: NBR_WINDOW_MIN (720 min, nbr_matrix.h) plus a small
// margin -- ASSUMPTION 2b's boot-gap history cannot influence any decision
// once every cell it touched has aged out. Configurable (not a magic
// number baked into the assertion below) so a later wave can tighten it.
// Measured at the wave 1 gate (DECISION mode): the boot gap stops showing
// after up 180 -- hours 3..13 of the fixture are clean -- so the assertion
// covers everything from there, not only the last 1.5 h after 720 + 2.
static const uint16_t CONVERGENCE_MINUTE = 180;

// The main instrument: replay the DK5EN-98 fixture and diff every [NBR]
// line it produces against the capture, grouped by trigger (ASSUMPTION 2c).
// Everything before CONVERGENCE_MINUTE is reported, not asserted -- see the
// file header for why (ASSUMPTION 2b's boot-gap is expected to still be
// visible there). At and after it, index-free mode must be a clean match.
void test_replay_reproduces_dk5en98_20260923_boot(void)
{
    std::string fixture = repo_root() + "/test/test_nbr_replay/fixtures/dk5en98-20260923-boot.txt";
    TEST_ASSERT_TRUE_MESSAGE(path_exists(fixture), ("fixture missing: " + fixture).c_str());

    ReplayResult rr;
    run_replay(fixture, rr);

    GroupReport exact_rep = build_group_report(rr.groups, CompareMode::EXACT);
    GroupReport idxfree_rep = build_group_report(rr.groups, CompareMode::INDEX_FREE);
    print_hourly_table("EXACT", exact_rep);
    print_hourly_table("INDEX-FREE", idxfree_rep);
    GroupReport decision_rep = build_group_report(rr.groups, CompareMode::DECISION);
    print_hourly_table("DECISION", decision_rep);
    printf("[nbr_replay] convergence minute: up >= %u (NBR_WINDOW_MIN=%d + margin)\n",
           (unsigned)CONVERGENCE_MINUTE, (int)NBR_WINDOW_MIN);
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

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)rr.parse_errors.size(),
        "unparseable [LOG] line(s) in the fixture -- see printed parse errors above");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)rr.need_no_path,
        "NEED line(s) whose msg_id had no preceding [LOG] line in the fixture");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)post_mismatched,
        "post-convergence (up>=CONVERGENCE_MINUTE) decision-mode group mismatches -- "
        "see the groups printed above; <cnt> drift and TX-timing CANCEL/REFUSE are "
        "already excluded (CompareMode::DECISION), so anything left is a real "
        "decision difference");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_pos_payload_matches_known_capture_example);
    RUN_TEST(test_decode_pos_payload_applies_south_west_sign);
    RUN_TEST(test_token_first_last_and_split_pipe);
    RUN_TEST(test_normalize_exact_ignores_only_the_need_slot_field);
    RUN_TEST(test_mask_to_names_sorts_and_marks_unknown_index);
    RUN_TEST(test_normalize_indexfree_strips_row_and_evict_idx_and_rewrites_need_masks);
    RUN_TEST(test_group_matches_is_order_insensitive_within_a_group);
    RUN_TEST(test_replay_reproduces_dk5en98_20260923_boot);
    return UNITY_END();
}
