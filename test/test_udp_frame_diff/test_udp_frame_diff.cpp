// FORMAL-02 differential: the live UDP frame handlers against their frozen
// copies (docs/udp-consolidation-20261127.md section 5.4, steps 0-3).
//
//   /tmp/formal02/piolock.sh test -e native_udp_frame_diff
//
// frozen/udp_frame_{esp32,nrf52}.frozen.cpp are byte copies of the two handler
// files at fork-dev 3139a4d6 with only the entry points renamed (README.md has
// the sha256). The live files are src/esp32/udp_frame_esp32.cpp and
// src/nrf52/udp_frame_nrf52.cpp, which the consolidation edits step by step.
// Every datagram goes to the frozen and the live handler of one platform from
// the same freshly reset world; everything observable is compared:
//   - the ordered sink log of test_udp_frame_twin (display, TX ring inserts,
//     phone, ACK, extern, ...)
//   - every recorder of the twin test (kiss, extern ack, store hook, ...)
//   - Serial output, the dedup ring, the TX ring, the own-tx table, the RM
//     queue, the server-liveness stamps, the link status text, settings
// Excluded on purpose, because the consolidation moves them to the callers or
// replaces them (steps 2/3): udp_is_busy, lora_tx_msg_len, convBuffer,
// RcvBuffer and the contents of the caller's receive buffer after the call.
//
// The twin test is reused, not copied: its recorders, fakes, corpus reader and
// frame builders are static symbols of that file, so it is #included with its
// main() renamed. Its own cases are compiled but not run here.
//
// Whitelist (counted, asserted, reported by the last test):
//   D4        nRF52 store-node hook order (classify before / after the {NNN cut)
//   REORDER   nRF52 only: the PRINT sink (printBuffer_aprs, C7) moved relative to the other sinks;
//             all other sinks, their order and all state are equal
// Anything else differing fails the case with the first differing line.

#define main udp_twin_main_not_run
#include "../test_udp_frame_twin/test_udp_frame_twin.cpp"
#undef main

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

static Obs run_handler(const Scenario &sc, int side, bool frozen)
{
    world_for(sc, side);
    uint8_t buf[BUF_CAP];
    memset(buf, 0, sizeof(buf));
    size_t n = sc.bytes.size() <= UDP_TX_BUF_SIZE ? sc.bytes.size() : UDP_TX_BUF_SIZE;
    if (n) memcpy(buf, sc.bytes.data(), n);
    g_udp_hook_side = side;
    IPAddress ip = src_ip_for(sc);
    int ret;
    if (frozen)
        ret = side ? handleUdpFrame_nrf52_frozen(buf, (int)n, ip) : handleUdpFrame_esp32_frozen(buf, (int)n, ip);
    else
        ret = side ? handleUdpFrame_nrf52(buf, (int)n, ip) : handleUdpFrame_esp32(buf, (int)n, ip);
    return observe(ret);
}

// ---------------------------------------------------------------------------
// Comparison with the whitelist
// ---------------------------------------------------------------------------

struct Tally
{
    int pairs = 0;
    int identical = 0;
    int d4 = 0;
    int reorder = 0;
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

// Returns "" when identical, "D4" / "REORDER" when the difference is whitelisted, else a description
static std::string compare_obs(const Obs &a, const Obs &b, int side, const Scenario &sc)
{
    std::set<std::string> state_diff;
    for (const auto &kv : a.state)
    {
        auto it = b.state.find(kv.first);
        if (it == b.state.end() || it->second != kv.second) state_diff.insert(kv.first);
    }
    const bool sinks_equal = a.sinks == b.sinks;
    if (a.ret == b.ret && sinks_equal && state_diff.empty())
        return "";

    std::string why;
    if (a.ret != b.ret) why += "ret " + std::to_string(a.ret) + "!=" + std::to_string(b.ret) + "; ";

    // D4: only the store-node hook sees a different payload; nothing else may differ
    if (a.ret == b.ret && sinks_equal && side == 1)
    {
        bool only_ms = true;
        for (const auto &k : state_diff) if (k != "ms_store" && k != "ms_ack") only_ms = false;
        if (only_ms) return "D4";
    }

    // REORDER (C7): only the PRINT sink (printBuffer_aprs) moved relative to the other sinks, on the
    // nRF52 side, everything else equal in content and order
    if (a.ret == b.ret && !sinks_equal && state_diff.empty() && side == 1)
    {
        std::vector<std::string> fa, fb, pa, pb;
        for (const auto &l : a.sinks) (sink_name(l) == "PRINT" ? pa : fa).push_back(l);
        for (const auto &l : b.sinks) (sink_name(l) == "PRINT" ? pb : fb).push_back(l);
        if (fa == fb && pa == pb) return "REORDER";
    }

    if (!sinks_equal)
    {
        size_t i = 0;
        while (i < a.sinks.size() && i < b.sinks.size() && a.sinks[i] == b.sinks[i]) i++;
        why += "sink#" + std::to_string(i) + " frozen=<" + (i < a.sinks.size() ? a.sinks[i] : "end") +
               "> live=<" + (i < b.sinks.size() ? b.sinks[i] : "end") + ">; ";
    }
    for (const auto &k : state_diff)
    {
        std::string av = a.state.count(k) ? a.state.at(k) : "-", bv = b.state.count(k) ? b.state.at(k) : "-";
        if (av.size() > 160) av = av.substr(0, 160) + "...";
        if (bv.size() > 160) bv = bv.substr(0, 160) + "...";
        why += "state[" + k + "] frozen=<" + av + "> live=<" + bv + ">; ";
    }
    char head[160];
    snprintf(head, sizeof(head), "side=%d len=%zu cfg_gw=%d ", side, sc.bytes.size(), (int)sc.cfg.gateway);
    return std::string(head) + sc.label + ": " + why;
}

static void run_pair(const Scenario &sc, int side)
{
    Obs fz = run_handler(sc, side, true);
    Obs lv = run_handler(sc, side, false);

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
    if (lv.state["tx_ring"] != "" && lv.state["tx_ring"].find("|0") == std::string::npos) T.relayed++;
    if (lv.state["rm_queue"].compare(0, 2, "1:") == 0) T.with_rm++;
    if (lv.state["settings_call"] != "DK5EN-1|DK5E1") T.with_settings++;
    if (lv.state["esp32_state"] != "0|0" || lv.state["nrf52_state"] != "0|0") T.with_server_stamp++;

    std::string r = compare_obs(fz, lv, side, sc);
    if (r.empty()) T.identical++;
    else if (r == "D4") T.d4++;
    else if (r == "REORDER") T.reorder++;
    else
    {
        T.failures++;
        if (T.first_failure.empty()) T.first_failure = r;
        if (T.failures <= 5) printf("DIFF %s\n", r.c_str());
    }
}

static void run_both(const Scenario &sc)
{
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
    static const char *DEST[] = {"DK5EN-1", "*", "9", "262", "DK5EN-7", "DK5EN-4"};
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

static void test_diff_totals_and_coverage(void)
{
    printf("DIFF-SUMMARY pairs=%d identical=%d d4=%d reorder=%d failures=%d sinks_compared=%ld sections_compared=%ld\n",
           T.pairs, T.identical, T.d4, T.reorder, T.failures, T.sinks_compared, T.sections_compared);
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
    TEST_ASSERT_TRUE_MESSAGE(T.d4 >= 50, "D4 whitelist not exercised");
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
    RUN_TEST(test_diff_totals_and_coverage);
    return UNITY_END();
}
