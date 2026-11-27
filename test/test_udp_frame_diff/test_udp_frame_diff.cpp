// FORMAL-02 differential: the live UDP frame handler against its frozen
// copies (docs/udp-consolidation-20261127.md section 5.4).
//
//   /tmp/formal02/piolock.sh test -e native_udp_frame_diff
//
// frozen/udp_frame_{esp32,nrf52}.frozen.cpp are byte copies of the two
// per-platform handler files at fork-dev 3139a4d6 (before the consolidation)
// with only the entry points renamed (README.md has the sha256). The live side
// is the ONE handler src/udp_frame.cpp, run once as the ESP32 and once as the
// nRF52 board (the platform hooks switch on the side, see the twin test).
// Every datagram goes to the frozen and the live handler of one platform from
// the same freshly reset world; everything observable is compared:
//   - the ordered sink log of test_udp_frame_twin (display, TX ring inserts,
//     phone, ACK, extern, ...)
//   - every recorder of the twin test (kiss, extern ack, store hook, ...)
//   - Serial output, the dedup ring, the TX ring, the own-tx table, the RM
//     queue, the server-liveness stamps, the link status text, settings
// Excluded on purpose, because the consolidation moves them to the callers or
// replaces them: udp_is_busy, lora_tx_msg_len, convBuffer, RcvBuffer and the
// contents of the caller's receive buffer after the call.
//
// The twin test is reused, not copied: its recorders, fakes, corpus reader and
// frame builders are static symbols of that file, so it is #included with its
// main() renamed. Its own cases are compiled but not run here.
//
// Whitelist (every class is counted, asserted exercised, reported by the last
// test; a pair may carry several classes, which are then stripped one by one and
// everything left must be equal):
//   D1       ESP32: a GATE frame of unknown type / without type stamps server liveness now
//   D2       nRF52: a new server position (0x21) is in the dedup ring after the call even when
//            vetoed (NOPOS, unconfigured source, known own tx): ring slot + stat_newid +1, the id
//            present in the ring only on the live side
//   D3       nRF52: one more DISPLAYTEXT sink call (the `!bGATEWAY` guard is gone), text GATE
//   D4       nRF52 store-node hook order (classify before / after the {NNN cut)
//   D5       a datagram shorter than 4 bytes / a 4-byte GATE no longer depends on the slab behind
//            it: scenarios with a stale tail; the live handler must give the same result for the
//            stale and for a zeroed tail, and the reference is the frozen handler on the zeroed
//            tail (counted: pairs in which the frozen handler did read the stale bytes; the frozen
//            nRF52 handler crashes on a "GAT" + stale "E", so those pairs are counted unrun)
//   C7       nRF52: the RX-UDP debug line (PRINT sink) prints the reset destination path
//   REORDER  nRF52: the PRINT sink moved relative to the other sinks
// Anything else differing fails the case with the first differing line.

#define main udp_twin_main_not_run
#include "../test_udp_frame_twin/test_udp_frame_twin.cpp"
#undef main

// Globals only the frozen handlers use (the consolidation removed them from the firmware).
uint16_t lora_tx_msg_len = 0;                  // frozen ESP32 handler (was in udp_functions.cpp)
uint8_t convBuffer[UDP_TX_BUF_SIZE + 50];      // frozen ESP32 handler (now private to udp_drain_esp32.cpp)

#include <map>
#include <set>
#include <sstream>

int handleUdpFrame_esp32_frozen(unsigned char *inc_udp_buffer, int packetSize, IPAddress src_ip);
int handleUdpFrame_nrf52_frozen(unsigned char *inc_udp_buffer, int packetSize, IPAddress remote_ip);

// ---------------------------------------------------------------------------
// Scenario: one datagram plus the configuration of the world around it
// ---------------------------------------------------------------------------

struct Cfg
{
    bool gateway = false;       // bGATEWAY (an nRF handler only ever runs with it on)
    bool nopos = false;         // bGATEWAY_NOPOS
    bool kiss = false;          // bKISS
    bool ext = false;           // bEXTUDP + hasExternIPaddress
    bool dup = false;           // the frame id is already in the dedup ring
    bool icheck = false;        // the frame id is a known own tx (checkOwnTx() >= 0)
    bool info = false;          // bDisplayInfo + bUDPLOG + bDEBUG
    bool store = false;         // store node mode own (g_ms_mode_own)
    bool group = false;         // own group 262 set (node_gcb[0])
    bool nomsgall = false;      // bNoMSGtoALL
    bool via = false;           // bVIA
    bool rm = false;            // RM on with a password
    bool srcmatch = true;       // datagram source IP equals the configured server address
};

struct Scenario
{
    std::vector<uint8_t> bytes;
    uint32_t msg_id = 0;
    bool known_id = false;      // the id above is a real frame id (dup / icheck need it)
    std::vector<uint8_t> tail;  // D5: bytes left in the slab behind the datagram (empty = zero)
    bool has_tail = false;
    Cfg cfg;
    std::string label;
};

static Cfg cfg_from_bits(unsigned bits)
{
    Cfg c;
    c.gateway  = bits & 1u;
    c.nopos    = bits & 2u;
    c.kiss     = bits & 4u;
    c.ext      = bits & 8u;
    c.dup      = bits & 16u;
    c.icheck   = bits & 32u;
    c.info     = bits & 64u;
    c.store    = bits & 128u;
    c.group    = bits & 256u;
    c.nomsgall = bits & 512u;
    c.via      = bits & 1024u;
    c.rm       = bits & 2048u;
    c.srcmatch = !(bits & 4096u);
    return c;
}

// ---------------------------------------------------------------------------
// Observation
// ---------------------------------------------------------------------------

struct Obs
{
    int ret = -99;
    uint32_t newid = 0;                             // stat_newid after the call
    int ring_hit = -1;                              // slots of the dedup ring holding the scenario's id (-1: id unknown)
    std::vector<std::string> sinks;                 // ordered sink log "SINK\tdetail"
    std::map<std::string, std::string> state;       // section -> text
};

static std::string hex_of(const void *p, size_t n) { return sink_hex((const uint8_t *)p, n); }

static uint64_t fnv(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static std::string u64s(uint64_t v) { char b[32]; snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b; }

static void world_for(const Scenario &sc, int side)
{
    recorder_reset();
    rmQueueReset();
    rmqReplyWanted() = false;
    gwLinkResetForTest();
    gwLinkSetDest("inet", "diff.test");
    blockListSet("DK5EN-8");
    g_blocked_drops.clear();
    g_nbr_need_calls = 0;
    g_nbr_need_path.clear();
    g_nbr_lines.clear();

    const Cfg &c = sc.cfg;
    bGATEWAY = side == 1 ? true : c.gateway;   // an nRF handler never runs with the gateway off
    bGATEWAY_NOPOS = c.nopos;
    bKISS = c.kiss;
    bEXTUDP = c.ext;
    hasExternIPaddress = c.ext;
    bDisplayInfo = c.info;
    bUDPLOG = c.info;
    bDEBUG = c.info;
    g_ms_mode_own = c.store;
    if (c.group) meshcom_settings.node_gcb[0] = 262;
    bNoMSGtoALL = c.nomsgall;
    bVIA = c.via;
    if (c.rm)
    {
        meshcom_settings.node_rm = 1;
        snprintf(meshcom_settings.node_passwd, sizeof(meshcom_settings.node_passwd), "secret");
    }
    if (sc.known_id && c.dup)
        addLoraRxBuffer(sc.msg_id, true);
    if (sc.known_id && c.icheck)
        g_own_tx_known.push_back(sc.msg_id);

    // CONF source guard: the server address the platform resolved
    node_hostip = IPAddress(44, 143, 8, 143);
    neth.udp_dest_addr = IPAddress(44, 143, 8, 143);
}

static IPAddress src_ip_for(const Scenario &sc)
{
    return sc.cfg.srcmatch ? IPAddress(44, 143, 8, 143) : IPAddress(1, 2, 3, 4);
}

static void put(std::map<std::string, std::string> &st, const char *k, const std::string &v) { st[k] += v; }

static Obs observe(int ret)
{
    Obs o;
    o.ret = ret;
    for (const auto &s : g_sink_log)
        o.sinks.push_back(s.sink + "\t" + s.detail);

    auto &st = o.state;
    std::string t;

    for (const auto &p : g_printed) t += p.source + "|" + p.tail + ";";
    put(st, "printed", t); t.clear();
    for (const auto &s : g_dropped) t += s + ";";
    put(st, "dropped_unconfigured", t); t.clear();
    for (const auto &s : g_blocked_drops) t += s + ";";
    put(st, "dropped_blocked", t); t.clear();
    for (const auto &b : g_ble) t += hex_of(b.data(), b.size()) + ";";
    put(st, "ble", t); t.clear();
    put(st, "reset_udp", std::to_string(g_reset_udp));
    for (size_t i = 0; i < g_ack_ids.size(); i++) t += std::to_string(g_ack_ids[i]) + "@" + g_ack_dest[i] + ";";
    put(st, "ack", t); t.clear();
    put(st, "save_settings", std::to_string(g_save_settings_calls));
    put(st, "display_text", std::to_string(g_sendDisplayText_calls));
    put(st, "display_pos", std::to_string(g_sendDisplayPosition_calls));
    for (const auto &s : g_display_dest_path) t += s + ";";
    put(st, "display_dest_path", t); t.clear();
    for (const auto &k : g_kiss) t += hex_of(k.bytes.data(), k.bytes.size()) + "|" + std::to_string(k.rssi) + "|" + std::to_string(k.snr) + ";";
    put(st, "kiss", t); t.clear();
    for (const auto &e : g_extern) t += std::to_string(e.bUDP) + "|" + e.src_type + "|" + hex_of(e.bytes.data(), e.bytes.size()) + "|" + std::to_string(e.buflen_arg) + ";";
    put(st, "extern", t); t.clear();
    for (const auto &e : g_extern_ack) t += std::to_string(e.msg_id) + "|" + std::to_string(e.status) + "|" + e.from + "|" + e.via + ";";
    put(st, "extern_ack", t); t.clear();
    for (auto v : g_own_tx_known) t += std::to_string(v) + ";";
    put(st, "own_tx_known", t); t.clear();
    for (auto v : g_insert_calls) t += std::to_string(v) + ";";
    put(st, "own_tx_insert", t); t.clear();
    for (auto v : g_ring_stop_calls) t += std::to_string(v) + ";";
    put(st, "ring_stop", t); t.clear();
    for (const auto &c : g_sto_holder_note_calls) t += std::to_string(c.msg_id) + "|" + c.holder + "|" + std::to_string(c.nnn) + ";";
    put(st, "sto_note", t); t.clear();
    for (auto v : g_sto_holder_clear_calls) t += std::to_string(v) + ";";
    put(st, "sto_clear", t); t.clear();
    for (const auto &c : g_ms_store) t += c.src + "|" + c.dst + "|" + std::to_string(c.nnn) + "|" + c.text + ";";
    put(st, "ms_store", t); t.clear();
    for (const auto &c : g_ms_ack) t += c.acker + "|" + c.sender + "|" + std::to_string(c.nnn) + ";";
    put(st, "ms_ack", t); t.clear();
    put(st, "nbr", std::to_string(g_nbr_need_calls) + "|" + g_nbr_need_path);
    for (const auto &l : g_nbr_lines) t += l + ";";
    put(st, "nbr_log", t); t.clear();

    put(st, "serial", Serial.captured());

    // handler-visible globals (nRF52 members and ESP32 globals both: a handler
    // writing to the wrong platform's state is a difference too)
    put(st, "esp32_state", std::to_string(last_upd_timer) + "|" + std::to_string(had_initial_udp_conn));
    put(st, "nrf52_state", std::to_string(neth.last_upd_timer) + "|" + std::to_string(neth.had_initial_udp_conn));
    put(st, "hb_warn_logged", std::to_string(hb_warn_logged));
    put(st, "led", std::to_string(bLED_ORANGE));
    put(st, "reboot", std::to_string(rebootAuto));
    put(st, "msg_counter", std::to_string(msg_counter));
    put(st, "stat_newid", std::to_string(stat_newid.load()));
    put(st, "strSource_call", strSource_call.c_str());
    put(st, "settings_call", std::string(meshcom_settings.node_call) + "|" + meshcom_settings.node_short);
    put(st, "own_msg_id", u64s(fnv(own_msg_id, sizeof(own_msg_id))));
    put(st, "dedup_ring", u64s(fnv(ringBufferLoraRX, sizeof(ringBufferLoraRX))) + "|" + std::to_string((unsigned)loraWrite.load()));
    put(st, "tx_ring", u64s(fnv(ringBuffer, sizeof(ringBuffer))) + "|" + std::to_string(txRingDepth()));

    char lk[96];
    gwLinkFormat(lk, sizeof(lk), true, true, 1000);
    put(st, "gw_link", lk);

    t = std::to_string(rmQueueCount()) + ":";
    {
        char src[RM_QUEUE_SRC_LEN], text[RM_QUEUE_TEXT_LEN];
        while (rmQueuePop(src, sizeof(src), text, sizeof(text))) t += std::string(src) + "|" + text + ";";
    }
    put(st, "rm_queue", t);
    return o;
}

typedef int (*HandlerFn)(unsigned char *, int, IPAddress);

static Obs run_handler(const Scenario &sc, int side, bool frozen, bool with_tail = false)
{
    world_for(sc, side);
    uint8_t buf[BUF_CAP];
    memset(buf, 0, sizeof(buf));
    size_t n = sc.bytes.size() <= UDP_TX_BUF_SIZE ? sc.bytes.size() : UDP_TX_BUF_SIZE;
    if (n) memcpy(buf, sc.bytes.data(), n);
    if (with_tail && !sc.tail.empty())
        memcpy(buf + n, sc.tail.data(), sc.tail.size() <= UDP_TX_BUF_SIZE - n ? sc.tail.size() : UDP_TX_BUF_SIZE - n);
    g_udp_hook_side = side;
    IPAddress ip = src_ip_for(sc);
    int ret;
    if (frozen)
        ret = side ? handleUdpFrame_nrf52_frozen(buf, (int)n, ip) : handleUdpFrame_esp32_frozen(buf, (int)n, ip);
    else
        ret = side ? handleUdpFrame_nrf52(buf, (int)n, ip) : handleUdpFrame_esp32(buf, (int)n, ip);
    Obs o = observe(ret);
    o.newid = stat_newid.load();
    o.ring_hit = sc.known_id ? dedup_ring_entries(sc.msg_id) : -1;
    return o;
}

// ---------------------------------------------------------------------------
// Comparison with the whitelist
// ---------------------------------------------------------------------------

struct Tally
{
    int pairs = 0;
    int identical = 0;
    int d1 = 0, d2 = 0, d3 = 0, d4 = 0, d5 = 0, c7 = 0, reorder = 0;   // pairs carrying the class
    int d5_pairs = 0;                                                   // stale-slab pairs run
    long sinks_compared = 0;
    long sections_compared = 0;
    int failures = 0;
    std::map<std::string, int> sink_kinds;     // frames (live side) per sink name
    int rejected_zeros = 0;
    int per_side[2] = {0, 0};
    int relayed = 0;
    int with_rm = 0;
    int with_settings = 0;
    int with_server_stamp = 0;
    std::string first_failure;
};
static Tally T;

static std::string sink_name(const std::string &line) { return line.substr(0, line.find('\t')); }

struct Tags { bool d1 = false, d2 = false, d3 = false, d4 = false, c7 = false, reorder = false; };

static bool starts_gate(const Scenario &sc)
{
    return sc.bytes.size() >= 4 && memcmp(sc.bytes.data(), "GATE", 4) == 0;
}
static bool gate_type_is(const Scenario &sc, uint8_t t) { return starts_gate(sc) && sc.bytes.size() >= 5 && sc.bytes[4] == t; }
static bool gate_type_unknown_or_none(const Scenario &sc)
{
    if (!starts_gate(sc)) return false;
    if (sc.bytes.size() < 5) return true;
    const uint8_t t = sc.bytes[4];
    return t != 0x3A && t != 0x21 && t != 0x40;
}

static int count_sink(const std::vector<std::string> &v, const char *name)
{
    int n = 0;
    for (const auto &l : v) if (sink_name(l) == name) n++;
    return n;
}
static std::vector<std::string> sinks_without(const std::vector<std::string> &v, const char *name)
{
    std::vector<std::string> out;
    for (const auto &l : v) if (sink_name(l) != name) out.push_back(l);
    return out;
}
static std::vector<size_t> sink_positions(const std::vector<std::string> &v, const char *name)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < v.size(); i++) if (sink_name(v[i]) == name) out.push_back(i);
    return out;
}
// text between two markers of a PRINT sink line ("... dst=X dst_path=Y tail=Z")
static std::string between(const std::string &l, const char *from, const char *to)
{
    size_t a = l.find(from);
    if (a == std::string::npos) return "";
    a += strlen(from);
    size_t b = l.find(to, a);
    return l.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
static std::string print_masked(const std::string &l)   // the PRINT line without its dst_path field
{
    size_t a = l.find(" dst_path="), b = l.find(" tail=");
    if (a == std::string::npos || b == std::string::npos || b < a) return l;
    return l.substr(0, a) + l.substr(b);
}

static bool obs_equal(const Obs &a, const Obs &b)
{
    return a.ret == b.ret && a.sinks == b.sinks && a.state == b.state && a.newid == b.newid && a.ring_hit == b.ring_hit;
}

// `a` = reference (the frozen handler), `b` = live. Returns "" when equal up to the whitelisted
// classes, which are then reported in `tg`; otherwise a description of the first difference.
static std::string compare_obs(const Obs &fa, const Obs &fb, int side, const Scenario &sc, Tags &tg)
{
    if (obs_equal(fa, fb))
        return "";

    Obs a = fa, b = fb;
    if (a.ret != b.ret)
        goto fail;   // the return value is never whitelisted

    // D1 (ESP32): unknown-type / typeless GATE frame, live stamps liveness, frozen did not
    if (side == 0 && gate_type_unknown_or_none(sc) && a.state["esp32_state"] == "0|0" && b.state["esp32_state"] != "0|0")
    {
        tg.d1 = true;
        for (const char *k : {"esp32_state", "hb_warn_logged", "gw_link"}) { a.state.erase(k); b.state.erase(k); }
    }

    // D2 (nRF52): a new server position is in the ring on the live side only; one slot, one count
    if (side == 1 && gate_type_is(sc, 0x21) && b.newid == a.newid + 1 &&
        (!sc.known_id || (b.ring_hit == 1 && a.ring_hit == 0)))
    {
        tg.d2 = true;
        for (const char *k : {"dedup_ring", "stat_newid"}) { a.state.erase(k); b.state.erase(k); }
        a.newid = b.newid = 0;
        a.ring_hit = b.ring_hit = -1;
    }

    // D3 (nRF52): exactly one more display call for a text GATE frame
    if (side == 1 && gate_type_is(sc, 0x3A) &&
        count_sink(b.sinks, "DISPLAYTEXT") == count_sink(a.sinks, "DISPLAYTEXT") + 1)
    {
        tg.d3 = true;
        a.sinks = sinks_without(a.sinks, "DISPLAYTEXT");
        b.sinks = sinks_without(b.sinks, "DISPLAYTEXT");
        for (const char *k : {"display_text", "display_dest_path"}) { a.state.erase(k); b.state.erase(k); }
    }

    // D4 (nRF52): only the store-node hook sees a different payload
    if (side == 1 && (a.state["ms_store"] != b.state["ms_store"] || a.state["ms_ack"] != b.state["ms_ack"]))
    {
        tg.d4 = true;
        for (const char *k : {"ms_store", "ms_ack"}) { a.state.erase(k); b.state.erase(k); }
    }

    // C7 / REORDER (nRF52): only PRINT sink lines differ, in place and/or in the destination path
    if (side == 1 && a.sinks != b.sinks && sinks_without(a.sinks, "PRINT") == sinks_without(b.sinks, "PRINT"))
    {
        std::vector<std::string> pa, pb;
        for (const auto &l : a.sinks) if (sink_name(l) == "PRINT") pa.push_back(l);
        for (const auto &l : b.sinks) if (sink_name(l) == "PRINT") pb.push_back(l);
        bool masked_equal = pa.size() == pb.size();
        for (size_t i = 0; masked_equal && i < pa.size(); i++)
            masked_equal = print_masked(pa[i]) == print_masked(pb[i]);
        if (masked_equal)
        {
            if (pa != pb)
            {
                // the live line prints the destination path reset to the destination call
                for (const auto &l : pb)
                    if (between(l, " dst_path=", " tail=") != between(l, " dst=", " dst_path="))
                        goto fail;
                tg.c7 = true;
            }
            if (sink_positions(a.sinks, "PRINT") != sink_positions(b.sinks, "PRINT"))
                tg.reorder = true;
            a.sinks = sinks_without(a.sinks, "PRINT");
            b.sinks = sinks_without(b.sinks, "PRINT");
        }
    }

    if (a.sinks == b.sinks && a.state == b.state && a.newid == b.newid && a.ring_hit == b.ring_hit)
        return "";

fail:
    std::string why;
    if (fa.ret != fb.ret) why += "ret " + std::to_string(fa.ret) + "!=" + std::to_string(fb.ret) + "; ";
    if (a.sinks != b.sinks)
    {
        size_t i = 0;
        while (i < a.sinks.size() && i < b.sinks.size() && a.sinks[i] == b.sinks[i]) i++;
        why += "sink#" + std::to_string(i) + " frozen=<" + (i < a.sinks.size() ? a.sinks[i] : "end") +
               "> live=<" + (i < b.sinks.size() ? b.sinks[i] : "end") + ">; ";
    }
    for (const auto &kv : a.state)
    {
        auto it = b.state.find(kv.first);
        if (it != b.state.end() && it->second == kv.second) continue;
        std::string av = kv.second, bv = it == b.state.end() ? "-" : it->second;
        if (av.size() > 160) av = av.substr(0, 160) + "...";
        if (bv.size() > 160) bv = bv.substr(0, 160) + "...";
        why += "state[" + kv.first + "] frozen=<" + av + "> live=<" + bv + ">; ";
    }
    if (a.newid != b.newid || a.ring_hit != b.ring_hit)
        why += "newid " + std::to_string(a.newid) + "/" + std::to_string(b.newid) + " ring_hit " +
               std::to_string(a.ring_hit) + "/" + std::to_string(b.ring_hit) + "; ";
    char head[160];
    snprintf(head, sizeof(head), "side=%d len=%zu cfg_gw=%d ", side, sc.bytes.size(), (int)sc.cfg.gateway);
    return std::string(head) + sc.label + ": " + why;
}

static void tally_pair(const Obs &lv, int side)
{
    T.pairs++;
    T.per_side[side]++;
    T.sinks_compared += (long)lv.sinks.size();
    T.sections_compared += (long)lv.state.size();
    if (lv.ret == 1) T.rejected_zeros++;
    {
        std::set<std::string> seen;
        for (const auto &l : lv.sinks) seen.insert(sink_name(l));
        for (const auto &k : seen) T.sink_kinds[k]++;
    }
    std::map<std::string, std::string> st = lv.state;
    if (st["tx_ring"] != "" && st["tx_ring"].find("|0") == std::string::npos) T.relayed++;
    if (st["rm_queue"].compare(0, 2, "1:") == 0) T.with_rm++;
    if (st["settings_call"] != "DK5EN-1|DK5E1") T.with_settings++;
    if (st["esp32_state"] != "0|0" || st["nrf52_state"] != "0|0") T.with_server_stamp++;
}

static void tally_result(const std::string &r, const Tags &tg)
{
    if (!r.empty())
    {
        T.failures++;
        if (T.first_failure.empty()) T.first_failure = r;
        if (T.failures <= 5) printf("DIFF %s\n", r.c_str());
        return;
    }
    if (!(tg.d1 || tg.d2 || tg.d3 || tg.d4 || tg.c7 || tg.reorder)) T.identical++;
    T.d1 += tg.d1; T.d2 += tg.d2; T.d3 += tg.d3; T.d4 += tg.d4; T.c7 += tg.c7; T.reorder += tg.reorder;
}

static void run_pair(const Scenario &sc, int side)
{
    Obs fz = run_handler(sc, side, true);
    Obs lv = run_handler(sc, side, false);
    tally_pair(lv, side);
    Tags tg;
    tally_result(compare_obs(fz, lv, side, sc, tg), tg);
}

// D5: the scenario has bytes left in the slab behind the datagram. The live handler must not care
// (stale == zeroed tail); the reference is the frozen handler on the zeroed tail, which is what
// the callers guarantee; the pair counts for D5 when the frozen handler did read the stale bytes.
static void run_pair_stale(const Scenario &sc, int side)
{
    // The frozen nRF52 handler, fed a GATE frame the slab completes behind a datagram under 4 bytes,
    // computes a negative length (packetSize - 4) and memcpy()s it: it crashes (SIGBUS). That is the
    // D5 hazard itself, so such a pair is counted without running the frozen handler on the stale slab.
    std::vector<uint8_t> whole = sc.bytes;
    whole.insert(whole.end(), sc.tail.begin(), sc.tail.end());
    const bool frozen_crashes = side == 1 && sc.bytes.size() < 4 && whole.size() >= 4 && memcmp(whole.data(), "GATE", 4) == 0;
    Obs fz_zero  = run_handler(sc, side, true, false);
    Obs fz_stale = frozen_crashes ? Obs() : run_handler(sc, side, true, true);
    Obs lv_stale = run_handler(sc, side, false, true);
    Obs lv_zero  = run_handler(sc, side, false, false);
    tally_pair(lv_stale, side);
    T.d5_pairs++;
    if (!obs_equal(lv_stale, lv_zero))
    {
        T.failures++;
        std::string r = "D5: live handler depends on the slab behind the datagram: " + sc.label;
        if (T.first_failure.empty()) T.first_failure = r;
        if (T.failures <= 5) printf("DIFF %s\n", r.c_str());
        return;
    }
    Tags tg;
    tally_result(compare_obs(fz_zero, lv_stale, side, sc, tg), tg);
    if (frozen_crashes || !obs_equal(fz_stale, fz_zero)) T.d5++;
}

static void run_both(const Scenario &sc)
{
    if (sc.has_tail)
    {
        run_pair_stale(sc, 0);
        run_pair_stale(sc, 1);
        return;
    }
    run_pair(sc, 0);
    run_pair(sc, 1);
}

// ---------------------------------------------------------------------------
// Corpora
// ---------------------------------------------------------------------------

static uint32_t xs_state = 0x2545F491u;
static uint32_t xs(void)
{
    xs_state ^= xs_state << 13;
    xs_state ^= xs_state >> 17;
    xs_state ^= xs_state << 5;
    return xs_state;
}

static Scenario make_sc(const uint8_t *data, size_t len, uint32_t id, bool known, unsigned cfgbits, const std::string &label)
{
    Scenario s;
    s.bytes.assign(data, data + len);
    s.msg_id = id;
    s.known_id = known;
    s.cfg = cfg_from_bits(cfgbits);
    s.label = label;
    return s;
}

// A deterministic spread of configurations per frame: the two extremes plus
// `n` strided picks over the 13 axes, so every axis is on and off many times
// per frame family without taking the whole cross product.
static void run_frame_over_configs(const uint8_t *data, size_t len, uint32_t id, bool known, unsigned salt,
                                   const std::string &label, int n)
{
    const unsigned all = 1u << 13;
    run_both(make_sc(data, len, id, known, 0, label + " cfg0"));
    run_both(make_sc(data, len, id, known, all - 1, label + " cfgAll"));
    for (int k = 0; k < n; k++)
    {
        unsigned bits = (salt * 2654435761u + (unsigned)k * 40503u + (unsigned)(xs() & 0xFF)) % all;
        run_both(make_sc(data, len, id, known, bits, label + " cfg" + std::to_string(bits)));
    }
}

static void test_diff_corpus_files(void)
{
    const std::string dir = repo_root() + "/test/golden/corpus/udp1990";
    DIR *d = opendir(dir.c_str());
    TEST_ASSERT_NOT_NULL(d);
    std::vector<std::string> names;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr)
    {
        std::string nm = e->d_name;
        if (nm != "." && nm != "..") names.push_back(nm);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    TEST_ASSERT_TRUE(names.size() >= 37);

    int before = T.pairs;
    unsigned salt = 1;
    for (const auto &nm : names)
    {
        std::vector<uint8_t> dg;
        TEST_ASSERT_TRUE(read_hex_corpus_file(dir + "/" + nm, dg));
        uint8_t buf[BUF_CAP];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, dg.data(), dg.size());
        // the id inside a corpus frame is unknown here; dup/icheck stay off for them
        run_frame_over_configs(buf, dg.size(), 0, false, salt++, "corpus " + nm, 6);
    }
    TEST_ASSERT_TRUE(T.pairs - before >= 37 * 8 * 2);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

struct Shape { const char *src; const char *dest; char type; const char *payload; };

static void test_diff_generated_frames(void)
{
    static const char *SRC[] = {"DK5EN-2", "XX0XXX-1", "DK5EN-1", "DK5EN-8", "DK5EN-2,DK5EN-1", "DK5EN-3,DK5EN-8", "DK5EN-2,DK5EN-3"};
    // the last two carry a destination path in front of the call (C7: the path is reset to the call)
    static const char *DEST[] = {"DK5EN-1", "*", "9", "262", "DK5EN-7", "DK5EN-4", "DK5EN-3,DK5EN-7", "DK5EN-3,DK5EN-1"};
    static const char *PAY[] = {
        "hello{12", "x:ack3", "x:rej4", "{SET}abc", "{CET}2026-11-27 12:00:00", "{ping}", "{pong}", "{MCP}secret",
        "RM1 1791300000 status 0123456789abcdef", "RM1 1791300000 ok v=4.40a up=1 0123456789abcdef",
        "DK5EN-9  :sto001 held", "grp:ack{5", "t:ack012{077", "x{1:ack5", "ab{12:ack034", "plain text", ""};

    int before = T.pairs;
    unsigned salt = 100;
    uint32_t id = 0x5000;
    for (const char *src : SRC)
        for (const char *dest : DEST)
            for (const char *pay : PAY)
            {
                uint8_t buf[BUF_CAP];
                memset(buf, 0, sizeof(buf));
                id++;
                uint16_t len = build_gate_datagram(buf, src, dest, ':', pay, id);
                run_frame_over_configs(buf, len, id, true, salt++,
                                       std::string("text ") + src + ">" + dest + " " + pay, 24);
            }
    for (const char *src : SRC)
        for (char type : {'!', '@'})
        {
            uint8_t buf[BUF_CAP];
            memset(buf, 0, sizeof(buf));
            id++;
            uint16_t len = build_gate_datagram(buf, src, "*", type, type == '!' ? "pos" : "hey", id);
            run_frame_over_configs(buf, len, id, true, salt++, std::string("type ") + type + " " + src, 24);
        }
    // unknown / raw types and the 4-byte GATE
    for (uint8_t type : {0x41, 0x00, 0x7F, 0xFF, 0x3A, 0x21, 0x40})
        for (uint16_t body : {0, 1, 5, 40})
        {
            uint8_t buf[BUF_CAP];
            memset(buf, 0, sizeof(buf));
            uint16_t len = build_gate_raw(buf, type, 0x55, body);
            run_frame_over_configs(buf, len, 0, false, salt++, "raw type " + std::to_string(type) + " body " + std::to_string(body), 8);
        }
    {
        uint8_t buf[BUF_CAP];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "GATE", 4);
        run_frame_over_configs(buf, 4, 0, false, salt++, "GATE 4 bytes", 12);
    }
    TEST_ASSERT_TRUE(T.pairs - before >= 20000);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

static void test_diff_conf_beat_other(void)
{
    int before = T.pairs;
    unsigned salt = 900;
    const char *calls[] = {"DK5EN-12", "dk5en-12", "XX0XXX-1", "A", "ok", "DK5EN-12 ", "9X9"};
    for (const char *call : calls)
        for (int variant = 0; variant < 3; variant++)
        {
            uint8_t buf[BUF_CAP];
            memset(buf, 0, sizeof(buf));
            uint16_t len = build_conf_datagram(buf, call, variant == 1 ? "SHRT" : nullptr);
            if (variant == 2) len = (uint16_t)(len > 8 ? len - 2 : len);   // truncated: malformed
            run_frame_over_configs(buf, len, 0, false, salt++, std::string("conf ") + call, 24);
        }
    {   // oversized CONF, a CONF of exactly the size bounds
        uint8_t buf[BUF_CAP];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "CONF", 4);
        memset(buf + 4, 'A', 251);
        run_frame_over_configs(buf, 255, 0, false, salt++, "conf 255", 24);
        run_frame_over_configs(buf, 4, 0, false, salt++, "conf 4", 24);
        run_frame_over_configs(buf, 3, 0, false, salt++, "conf 3", 8);
    }
    {
        uint8_t buf[BUF_CAP];
        memset(buf, 0, sizeof(buf));
        uint16_t len = build_beat_datagram(buf);
        run_frame_over_configs(buf, len, 0, false, salt++, "beat", 24);
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "BEAT", 4);
        run_frame_over_configs(buf, 4, 0, false, salt++, "beat 4", 8);
        memcpy(buf, "WHAT is this", 12);
        run_frame_over_configs(buf, 12, 0, false, salt++, "other", 8);
        memset(buf, 0, sizeof(buf));
        run_frame_over_configs(buf, 0, 0, false, salt++, "empty", 4);
    }
    TEST_ASSERT_TRUE(T.pairs - before >= 800);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

static void test_diff_every_length_random_bytes(void)
{
    int before = T.pairs;
    for (unsigned len = 0; len <= 255; len++)
        for (int rep = 0; rep < 36; rep++)
        {
            uint8_t buf[BUF_CAP];
            memset(buf, 0, sizeof(buf));
            for (unsigned i = 0; i < len; i++) buf[i] = (uint8_t)xs();
            const int style = rep % 6;   // 0 random, 1-3 GATE/CONF/BEAT prefix, 4 GATE+type, 5 zero run
            if (len >= 4 && style >= 1)
            {
                static const char *IND[] = {"GATE", "CONF", "BEAT", "GATE", "GATE"};
                memcpy(buf, IND[style - 1], 4);
            }
            if (style == 4 && len >= 8)   // a plausible GATE type byte
                buf[4] = (uint8_t)(xs() & 1 ? 0x3A : 0x21);
            if (style == 5 && len >= 24)  // long zero run, aligned or not
            {
                unsigned at = xs() % (len - 12), run = 4 + xs() % 12;
                memset(buf + at, 0, run);
            }
            run_both(make_sc(buf, len, 0, false, xs() & 0x1FFF, "random len " + std::to_string(len) + " rep " + std::to_string(rep)));
        }
    TEST_ASSERT_TRUE(T.pairs - before >= 256 * 36 * 2);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

// Real frames cut or damaged at a random byte: decodeAPRS() sees partial input
static void test_diff_mutated_real_frames(void)
{
    int before = T.pairs;
    uint32_t id = 0x9000;
    for (int i = 0; i < 20000; i++)
    {
        uint8_t buf[BUF_CAP];
        memset(buf, 0, sizeof(buf));
        id++;
        static const char *DEST[] = {"DK5EN-1", "*", "9", "DK5EN-7"};
        static const char *PAY[] = {"hello{12", "x:ack3", "t:ack012{077", "{SET}abc", "RM1 1791300000 status 0123456789abcdef", "text"};
        const char type = (i % 5 == 0) ? '!' : ':';
        uint16_t len = build_gate_datagram(buf, "DK5EN-2", DEST[xs() % 4], type, PAY[xs() % 6], id);
        const unsigned m = xs() % 4;
        if (m == 0 && len > 6) len = (uint16_t)(5 + xs() % (len - 5));                    // cut
        else if (m == 1) buf[xs() % len] = (uint8_t)xs();                                  // one damaged byte
        else if (m == 2 && len > 12) { unsigned at = 4 + xs() % (len - 10); memset(buf + at, 0, 2 + (xs() % 9)); }   // zero run
        else if (m == 3) { buf[4 + xs() % (len - 4)] ^= (uint8_t)(1u << (xs() % 8)); }       // bit flip
        run_both(make_sc(buf, len, id, true, xs() & 0x1FFF, "mutant " + std::to_string(i)));
    }
    TEST_ASSERT_TRUE(T.pairs - before >= 40000);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

// D5: real frames cut to a datagram of 0..5 bytes with the rest of the frame still in the slab.
static void test_diff_d5_stale_slab_behind_short_datagrams(void)
{
    int before = T.pairs;
    unsigned salt = 3000;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> fulls;
    auto add = [&](const char *name, const uint8_t *b, size_t n) { fulls.push_back({name, std::vector<uint8_t>(b, b + n)}); };
    uint8_t buf[BUF_CAP];
    uint16_t n;
    memset(buf, 0, sizeof(buf)); n = build_gate_datagram(buf, "DK5EN-2", "DK5EN-1", ':', "hello{12", 0x6101); add("text", buf, n);
    memset(buf, 0, sizeof(buf)); n = build_gate_datagram(buf, "DK5EN-2", "9", '!', "pos", 0x6102);           add("pos", buf, n);
    memset(buf, 0, sizeof(buf)); n = build_gate_datagram(buf, "DK5EN-2", "*", '@', "hey", 0x6103);           add("hey", buf, n);
    memset(buf, 0, sizeof(buf)); n = build_conf_datagram(buf, "DK5EN-12", "SHRT");                          add("conf", buf, n);
    memset(buf, 0, sizeof(buf)); n = build_beat_datagram(buf);                                              add("beat", buf, n);
    for (const auto &f : fulls)
        for (size_t cut = 0; cut <= 5 && cut <= f.second.size(); cut++)
        {
            Scenario base = make_sc(f.second.data(), cut, 0x6101, false, 0, f.first + " cut " + std::to_string(cut));
            base.tail.assign(f.second.begin() + cut, f.second.end());
            base.has_tail = true;
            for (int k = 0; k < 12; k++)
            {
                Scenario sc = base;
                sc.cfg = cfg_from_bits(k == 0 ? 0u : (salt * 2654435761u + (unsigned)k * 40503u) % (1u << 13));
                run_both(sc);
            }
            salt++;
        }
    TEST_ASSERT_TRUE(T.pairs - before >= 5 * 6 * 12 * 2 - 40);
    TEST_ASSERT_EQUAL_INT(0, T.failures);
}

static void test_diff_totals_and_coverage(void)
{
    printf("DIFF-SUMMARY pairs=%d identical=%d failures=%d sinks_compared=%ld sections_compared=%ld\n",
           T.pairs, T.identical, T.failures, T.sinks_compared, T.sections_compared);
    printf("DIFF-SUMMARY whitelist d1=%d d2=%d d3=%d d4=%d d5=%d (stale pairs %d) c7=%d reorder=%d\n",
           T.d1, T.d2, T.d3, T.d4, T.d5, T.d5_pairs, T.c7, T.reorder);
    printf("DIFF-SUMMARY per_side esp32=%d nrf52=%d zero_rejects=%d relayed=%d rm_queued=%d settings_changed=%d server_stamp=%d\n",
           T.per_side[0], T.per_side[1], T.rejected_zeros, T.relayed, T.with_rm, T.with_settings, T.with_server_stamp);
    for (const auto &kv : T.sink_kinds) printf("DIFF-SUMMARY sink %s in %d pairs\n", kv.first.c_str(), kv.second);
    fflush(stdout);

    TEST_ASSERT_EQUAL_INT(0, T.failures);
    TEST_ASSERT_TRUE_MESSAGE(T.pairs >= 80000, "differential ran too few pairs");
    TEST_ASSERT_TRUE_MESSAGE(T.sinks_compared >= 60000, "too few sink calls compared");
    // non-vacuity: every important sink class must actually have been exercised
    for (const char *k : {"DISPLAYTEXT", "DISPLAYPOS", "BLE", "ACK", "TXRING", "EXTERN", "EXTERNACK", "SAVESETTINGS", "DROP", "PRINT"})
        TEST_ASSERT_TRUE_MESSAGE(T.sink_kinds[k] >= 20, k);
    TEST_ASSERT_TRUE(T.rejected_zeros >= 200);
    TEST_ASSERT_TRUE(T.relayed >= 2000);
    TEST_ASSERT_TRUE(T.with_rm >= 20);
    TEST_ASSERT_TRUE(T.with_settings >= 20);
    TEST_ASSERT_TRUE(T.with_server_stamp >= 5000);
    // the whitelist must stay exercised, or it could hide nothing and prove nothing
    TEST_ASSERT_TRUE_MESSAGE(T.d1 >= 100, "D1 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.d2 >= 100, "D2 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.d3 >= 100, "D3 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.d4 >= 50, "D4 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.d5 >= 20, "D5 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.c7 >= 100, "C7 whitelist not exercised");
    TEST_ASSERT_TRUE_MESSAGE(T.reorder >= 500, "REORDER whitelist not exercised");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_diff_corpus_files);
    RUN_TEST(test_diff_generated_frames);
    RUN_TEST(test_diff_conf_beat_other);
    RUN_TEST(test_diff_every_length_random_bytes);
    RUN_TEST(test_diff_mutated_real_frames);
    RUN_TEST(test_diff_d5_stale_slab_behind_short_datagrams);
    RUN_TEST(test_diff_totals_and_coverage);
    return UNITY_END();
}
