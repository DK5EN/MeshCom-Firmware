// Twin test for the inbound UDP frame handler (test plan U1 / section 4.3,
// audit rows C1/U1). "the same datagram fed to both platform copies must
// produce the same (or a DELIBERATELY pinned different) observable effect."
//
// ONE binary, like the U2 drain twin (test/test_udp_send_twin), not two: the
// handlers are named differently -- handleUdpFrame_esp32() / handleUdpFrame_
// nrf52() -- so both translation units link together and the comparison is a
// real differential, not two runs compared through files. See
// src/udp_frame.h for why the two bodies moved out unchanged and why they
// are deliberately NOT merged: which of them is right is a drift-matrix
// decision, not this test's -- this test's job is to make every difference
// explicit and failing-on-change.
//
//   pio test -e native_udp_frame_twin -f test_udp_frame_twin
//
// WHAT IS STUBBED AND WHY, at a glance (see the stub headers themselves for
// the full reasoning):
//   stubs/udp_functions.h        real one gates bUDPLOG/handleUdpFrame_esp32
//                                 behind #if defined(ESP32), which this
//                                 native build never sets
//   stubs/nrf_eth.h               real NrfETH drags the W5100S driver; four
//                                 members + handleUdpFrame_nrf52() declared
//   stubs/SPI.h,
//   stubs/RAK13800_W5100S.h        empty shims, nothing in the frame handler
//                                 touches them
//
// The shared settings shim test/support/nrf52/WisBlock-API.h carries the
// fields this env needs: node_short (both handlers write it in the CONF
// branch) and node_via (via_functions.cpp, linked here for checkVia()) were
// added there rather than shadowed, so there is exactly one native copy of
// s_meshcom_settings and not two to keep in sync. It also declares
// save_settings() -- read the drift note on it there before touching either
// handler's call site.

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <climits>
#include <dirent.h>   // corpus directory listing (POSIX; native env is host-only, no Windows target)
#if defined(__APPLE__)
#include <mach-o/dyld.h>   // _NSGetExecutablePath(), see repo_root_from_file()
#endif

#include <Arduino.h>
#include <IPAddress.h>
#include <configuration.h>
#include <aprs_functions.h>

#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <dedup_functions.h>
#include <ack_attribution.h>
#include <conf_frame.h>
#include <txring_functions.h>

#include <udp_functions.h>   // stub: bUDPLOG, resetMeshComUDP, handleUdpFrame_esp32
#include <nrf_eth.h>         // stub: NrfETH + handleUdpFrame_nrf52

// ---------------------------------------------------------------------------
// File-scope state the two handlers extern. Types/values copied verbatim
// from the top of each carved .cpp -- an ODR mismatch here would be a
// linker-invisible bug, same discipline test_udp_send_twin.cpp uses.
// ---------------------------------------------------------------------------

// esp32/udp_frame_esp32.cpp:30-38
bool udp_is_busy = false;
uint16_t lora_tx_msg_len = 0;
unsigned long last_upd_timer = 0;
bool hb_warn_logged = false;
uint8_t convBuffer[UDP_TX_BUF_SIZE + 50];
bool hasExternIPaddress = false;
String strSource_call;
bool had_initial_udp_conn = false;
IPAddress node_hostip(44, 143, 8, 143);

// nrf52/udp_frame_nrf52.cpp:28-33 (strSource_call shared with the block
// above -- both handlers really do read/write one global on their own
// firmware image; sharing it here is the twin's whole point).
NrfETH neth;
bool bUDPLOG = false;

// loop_functions_extern.h globals neither linked .cpp defines (no
// loop_functions.cpp/lora_functions.cpp/udp_functions.cpp/nrf_eth.cpp in
// this env's build_src_filter). One shared definition each, same reasoning.
bool bEXTUDP = false;
bool bDisplayInfo = false;
bool bDisplayLog = false;
bool bDisplayCont = false;
bool bLED_ORANGE = false;
bool bNoMSGtoALL = false;
bool bGATEWAY_NOPOS = false;
bool bGATEWAY = false;
bool bDEBUG = false;
bool bVIA = false;
int isPhoneReady = 1;
unsigned int msg_counter = 0;
unsigned int _GW_ID = 0x99999999;
uint8_t own_msg_id[MAX_RING][5];
unsigned long rebootAuto = 0;
uint8_t RcvBuffer[UDP_TX_BUF_SIZE * 2];
std::atomic<uint32_t> stat_newid{0};

// aprs_functions.cpp's own externs -- same values test_udp_send_twin.cpp
// uses, for the same reason (int-vs-uint8_t ODR match with the real .cpp).
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631, the bench board
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// via_functions.cpp's checkMesh() is compiled (checkVia()'s neighbour in the
// same TU) but never called by either handler; is_equ() (loop_functions.h)
// is its only otherwise-unresolved symbol. A faithful case-sensitive exact
// match is enough since nothing here exercises checkMesh() at runtime.
bool is_equ(const char *buf1, const char *buf2)
{
    if (buf1 == nullptr || buf2 == nullptr)
        return false;
    return strcmp(buf1, buf2) == 0;
}

s_meshcom_settings meshcom_settings;

String getTimeString() { return String("00:00:00"); }

// ---------------------------------------------------------------------------
// Recording sinks. Real signatures from loop_functions.h / loop_functions_
// extern.h / extudp_functions.h -- a mismatch here is a compile error, which
// is the point (a handler reaching for a fifth thing fails loudly).
// ---------------------------------------------------------------------------

struct PrintedAprs
{
    std::string source;
    std::string tail;
};
static std::vector<PrintedAprs> g_printed;
static std::vector<std::string> g_dropped;            // logRxDropUnconfigured
static std::vector<std::vector<uint8_t>> g_ble;        // addBLEOutBuffer payloads
static int g_reset_udp = 0;                            // ESP32 resetMeshComUDP()
static std::vector<unsigned int> g_ack_ids;            // SendAckMessage
static std::vector<std::string> g_ack_dest;
static int g_save_settings_calls = 0;
static int g_sendDisplayText_calls = 0;
static int g_sendDisplayPosition_calls = 0;

struct ExternCall
{
    bool bUDP;
    std::string src_type;
    std::vector<uint8_t> bytes;
    uint16_t buflen_arg;
};
static std::vector<ExternCall> g_extern;

// checkOwnTx()/insertOwnTx(): model of the "own tx" cache both handlers
// share (own_msg_id[] on hardware). Empty by default, so a fresh id always
// reads as "not ours" (icheck < 0) -- the common case in this corpus.
static std::vector<uint32_t> g_own_tx_known;
static std::vector<uint32_t> g_insert_calls;

// ---------------------------------------------------------------------------
// U1 ordered sink log (test plan 4.4, section 9.1). The vectors above record
// each sink's OWN call history for the named Unity cases to assert on; this
// log additionally records EVERY sink call, from every stub below, into one
// timeline in call order -- the interleaving the separate vectors lose. Read
// by the corpus dump further down; not used by the named cases above.
//
// Deliberately NOT recorded here (see the corpus-dump section's comment for
// why): raw Serial output (setlogPrint()/Serial.printf() lines in the real
// handlers carry millis() and this test has no interception point for
// Serial's mock without touching test/support, which is out of this file
// set) and checkOwnTx() (a pure read, not a sink).
// ---------------------------------------------------------------------------

struct SinkCall
{
    std::string sink;
    std::string detail;
};
static std::vector<SinkCall> g_sink_log;

// TSV is the wire format below (NNN\tSINK\tdetail); a raw '\t'/'\n' from an
// input string (call sign, tail, src_type -- none is expected to carry one
// today) would otherwise split a line silently. Cheap defensive escaping,
// not exercised by any corpus file today.
static std::string sink_sanitize(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

static std::string sink_hex(const uint8_t *data, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; i++)
    {
        if (i) out.push_back(' ');
        out.push_back(digits[(data[i] >> 4) & 0xF]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

static void record_sink(const char *sink, const std::string &detail)
{
    g_sink_log.push_back(SinkCall{sink, sink_sanitize(detail)});
}

void logRxDropUnconfigured(const char *call)
{
    g_dropped.push_back(call ? call : "(null)");
    record_sink("DROP", call ? call : "(null)");
}

void printBuffer_aprs(char *msg_source, struct aprsMessage &aprsMessage, const char *tail)
{
    PrintedAprs p;
    p.source = aprsMessage.msg_source_call.c_str();
    p.tail = tail ? tail : "";
    (void)msg_source;
    g_printed.push_back(p);
    record_sink("PRINT", "source=" + p.source + " tail=" + p.tail);
}

void addBLEOutBuffer(uint8_t *buffer, uint16_t len)
{
    g_ble.emplace_back(buffer, buffer + len);
    std::string detail = "len=" + std::to_string(len);
    if (len) detail += " " + sink_hex(buffer, len);
    record_sink("BLE", detail);
}

void resetMeshComUDP() { g_reset_udp++; record_sink("RESET", "-"); }

int checkOwnTx(unsigned int msg_id)
{
    for (size_t i = 0; i < g_own_tx_known.size(); i++)
        if (g_own_tx_known[i] == msg_id)
            return (int)i;
    return -1;
}

void insertOwnTx(unsigned int id)
{
    g_insert_calls.push_back(id);
    g_own_tx_known.push_back(id);
    char buf[32];
    snprintf(buf, sizeof(buf), "id=%x", id);
    record_sink("TXRING", buf);
}

String convertCallToShort(char callsign[10])
{
    String s(callsign);
    if (s.length() > 5)
        s = s.substring(0, 5);
    return s;
}

// bool, not void: see the drift note in test/support/nrf52/WisBlock-API.h --
// ESP32 declares this void, nRF52 bool, and the mangled name cannot tell them
// apart. Neither call site reads the result.
bool save_settings(void) { g_save_settings_calls++; record_sink("SAVESETTINGS", "-"); return true; }

void sendDisplayText(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr)
{
    (void)aprsmsg; (void)rssi; (void)snr;
    g_sendDisplayText_calls++;
    record_sink("DISPLAYTEXT", "-");
}

void sendDisplayPosition(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr)
{
    (void)aprsmsg; (void)rssi; (void)snr;
    g_sendDisplayPosition_calls++;
    record_sink("DISPLAYPOS", "-");
}

void SendAckMessage(String dest_call, unsigned int iAckId)
{
    g_ack_dest.push_back(dest_call.c_str());
    g_ack_ids.push_back(iAckId);
    char buf[96];
    snprintf(buf, sizeof(buf), "dest=%s id=%x", dest_call.c_str(), iAckId);
    record_sink("ACK", buf);
}

void sendExtern(bool bUDP, char *src_type, uint8_t *buffer, uint16_t buflen, int16_t rssi, int8_t snr)
{
    (void)rssi; (void)snr;
    ExternCall c;
    c.bUDP = bUDP;
    c.src_type = src_type ? src_type : "";
    c.bytes.assign(buffer, buffer + buflen);
    c.buflen_arg = buflen;
    g_extern.push_back(c);
    std::string detail = std::string("bUDP=") + (bUDP ? "1" : "0") +
                          " src_type=" + c.src_type + " len=" + std::to_string(buflen);
    if (buflen) detail += " " + sink_hex(buffer, buflen);
    record_sink("EXTERN", detail);
}

void setlogPrint(const char *body)
{
    (void)body; // real setlogFormatGwi() output is compared directly below
}

// ---------------------------------------------------------------------------
// Corpus builders
// ---------------------------------------------------------------------------

// Real UDP receive buffer is a fixed UDP_TX_BUF_SIZE/500-byte slab regardless
// of the actual datagram length -- both handlers memset/write past a short
// packetSize (ESP32 zeroes the whole UDP_TX_BUF_SIZE at the end of the GATE
// branch). Every working buffer below is sized with headroom for that, never
// just the logical frame length.
static const size_t BUF_CAP = UDP_TX_BUF_SIZE + 64;

static void copy_into(uint8_t *dst, const uint8_t *tmpl, uint16_t len)
{
    memset(dst, 0, BUF_CAP);
    memcpy(dst, tmpl, len);
}

// Builds the APRS frame only (no GATE indicator). Field set taken from
// test_udp_send_twin.cpp's build_frame(): encodeAPRS() reads msg_source_path
// / msg_destination_path, not the _call fields. Destination "9" is the bench
// group; never "*" (2026-09-11 incident). Source is always a DK5EN-* bench
// call, never a foreign or "XX0XXX" one except where the RX-01 drift test
// deliberately needs the factory default as the SOURCE of an inbound frame.
static uint16_t build_frame(uint8_t *out, const char *src, const char *dest,
                            char msgType, const char *payload, uint32_t msg_id = 0x4711)
{
    struct aprsMessage m;
    initAPRS(m, msgType);
    m.msg_id = msg_id;
    m.max_hop = 3;
    m.msg_server = true;
    m.msg_source_path = String(src);
    m.msg_destination_path = String(dest);
    m.msg_payload = String(payload);
    return encodeAPRS(out, m);
}

static uint16_t build_gate_datagram(uint8_t *out, const char *src, const char *dest,
                                    char msgType, const char *payload, uint32_t msg_id = 0x4711)
{
    memcpy(out, "GATE", 4);
    uint16_t flen = build_frame(out + 4, src, dest, msgType, payload, msg_id);
    return 4 + flen;
}

// A GATE datagram with an unrecognised msg_type byte and a raw (non-APRS)
// body -- for the EXTUDP-gate drift, which fires (or doesn't) before either
// handler ever tries to decode anything.
static uint16_t build_gate_raw(uint8_t *out, uint8_t msg_type, uint8_t fill, uint16_t bodylen)
{
    memcpy(out, "GATE", 4);
    out[4] = msg_type;
    memset(out + 5, fill, bodylen);
    return 5 + bodylen;
}

static uint16_t build_conf_datagram(uint8_t *out, const char *call, const char *shortname = nullptr)
{
    memcpy(out, "CONF", 4);
    uint16_t n = 4;
    out[n++] = 0x00;
    uint8_t clen = (uint8_t)strlen(call);
    out[n++] = clen;
    memcpy(out + n, call, clen);
    n += clen;
    if (shortname)
    {
        out[n++] = 0x01;
        uint8_t slen = (uint8_t)strlen(shortname);
        out[n++] = slen;
        memcpy(out + n, shortname, slen);
        n += slen;
    }
    return n;
}

static uint16_t build_beat_datagram(uint8_t *out)
{
    memcpy(out, "BEAT", 4);
    // Trailing bytes are irrelevant to dispatch; sample content from the
    // TODO comment both handlers carry above their BEAT branch.
    static const uint8_t tail[] = {0x00, 0x09, 'O', 'E', '1', 'K', 'F', 'R'};
    memcpy(out + 4, tail, sizeof(tail));
    return 4 + sizeof(tail);
}

// ---------------------------------------------------------------------------
// Reset. Full state: settings, both file-scope blocks, the shared dedup
// ring (dedup_functions.cpp) and the shared TX ring (txring_functions.cpp) --
// every test starts from an identical, empty world on BOTH sides.
// ---------------------------------------------------------------------------

static void recorder_reset()
{
    g_printed.clear();
    g_dropped.clear();
    g_ble.clear();
    g_reset_udp = 0;
    g_ack_ids.clear();
    g_ack_dest.clear();
    g_save_settings_calls = 0;
    g_sendDisplayText_calls = 0;
    g_sendDisplayPosition_calls = 0;
    g_extern.clear();
    g_sink_log.clear();
    g_own_tx_known.clear();
    g_insert_calls.clear();

    udp_is_busy = false;
    lora_tx_msg_len = 0;
    last_upd_timer = 0;
    hb_warn_logged = false;
    memset(convBuffer, 0, sizeof(convBuffer));
    hasExternIPaddress = false;
    strSource_call = "";
    had_initial_udp_conn = false;
    node_hostip = IPAddress(44, 143, 8, 143);

    neth = NrfETH();
    bUDPLOG = false;

    bEXTUDP = false;
    bDisplayInfo = false;
    bDisplayLog = false;
    bDisplayCont = false;
    bLED_ORANGE = false;
    bNoMSGtoALL = false;
    bGATEWAY_NOPOS = false;
    bGATEWAY = false;
    bDEBUG = false;
    bVIA = false;
    isPhoneReady = 1;
    msg_counter = 0;
    memset(own_msg_id, 0, sizeof(own_msg_id));
    rebootAuto = 0;
    memset(RcvBuffer, 0, sizeof(RcvBuffer));
    stat_newid = 0;

    memset(&meshcom_settings, 0, sizeof(meshcom_settings));
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "DK5EN-1");
    snprintf(meshcom_settings.node_short, sizeof(meshcom_settings.node_short), "DK5E1");

    // Shared dedup ring (dedup_functions.cpp) -- is_new_packet() scans every
    // slot regardless of the write pointer, so a stale id from a previous
    // test would otherwise read back as "already seen" here.
    memset(ringBufferLoraRX, 0, sizeof(ringBufferLoraRX));
    loraWrite.store(0);

    // Shared TX ring (txring_functions.cpp).
    memset(ringBuffer, 0, sizeof(ringBuffer));
    iWrite = 0;
    iRead = 0;

    Serial.clear();
    mc_test_set_millis(1000);
}

void setUp(void) { recorder_reset(); }
void tearDown(void) {}

// ===========================================================================
// AGREEMENT: what both sides must keep doing identically
// ===========================================================================

static void test_agreement_gate_text_message_decodes_and_relays_on_both(void)
{
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "hallo welt", 0x1001);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bDisplayInfo = true;
        bDisplayLog = true;

        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        int depth0 = txRingDepth();

        int rc = side ? handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4))
                      : (handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4)), 0);
        (void)rc;

        char msg[80];
        snprintf(msg, sizeof(msg), "%s: decode did not print", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(), msg);
        snprintf(msg, sizeof(msg), "%s: wrong source call decoded", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-2", g_printed[0].source.c_str(), msg);

        snprintf(msg, sizeof(msg), "%s: sendDisplayText not called", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayText_calls, msg);

        // Both suppress the SECOND (dedup-block) addBLEOutBuffer via
        // bBLELoopOut=false in this branch -- exactly one BLE frame out.
        snprintf(msg, sizeof(msg), "%s: unexpected addBLEOutBuffer count", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_ble.size(), msg);

        snprintf(msg, sizeof(msg), "%s: fresh id must relay to the TX ring", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(), msg);

        snprintf(msg, sizeof(msg), "%s: insertOwnTx not called", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_insert_calls.size(), msg);
        snprintf(msg, sizeof(msg), "%s: insertOwnTx got the wrong msg_id", name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x1001, g_insert_calls[0], msg);

        // setlogFormatGwi() is REAL and shared -- byte-identical formatted
        // text on both sides is itself an agreement worth pinning.
    }
}

static void test_agreement_dedup_blocks_repeat_relay_on_both(void)
{
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "repeat me", 0x2002);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();

        uint8_t buf[BUF_CAP];
        int depth0 = txRingDepth();

        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        int depth1 = txRingDepth();

        char msg[96];
        snprintf(msg, sizeof(msg), "%s: first (fresh) frame must relay", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, depth1, msg);

        copy_into(buf, tmpl, len);   // same msg_id -- a genuine repeat
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        int depth2 = txRingDepth();

        snprintf(msg, sizeof(msg), "%s: repeat of the same msg_id must not relay again", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth1, depth2, msg);
    }
}

static void test_agreement_max_zeros_rejected_by_both(void)
{
    // Fully-zeroed, EVEN-length datagram: both loops read exactly the same
    // set of byte pairs (no odd-length bound to disagree over --
    // see test_agreement_zero_scan_bound_matches_on_odd_length() below for
    // the odd-length case), so zerocount is identical (16) on both and both
    // exceed MAX_ZEROS (6).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const int len = 16;

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);

        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        char msg[80];
        snprintf(msg, sizeof(msg), "%s: rejected frame must not decode", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(), msg);
        snprintf(msg, sizeof(msg), "%s: rejected frame must not relay", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, txRingDepth(), msg);
        snprintf(msg, sizeof(msg), "%s: rejected frame must not touch the dedup ring", name);
        TEST_ASSERT_TRUE_MESSAGE(is_new_packet((uint8_t[4]){0x02, 0x20, 0x00, 0x00}), msg);
    }
}

static void test_agreement_indicator_dispatch_prints_matching_gw_rx_type_lines(void)
{
    // BEAT, CONF and the unrecognised-indicator ("OTHER") lines are all
    // "raw & unconditional" (TM-39) on both sides -- printed regardless of
    // --debug or bDisplayInfo, so Serial's capture buffer is a clean,
    // stub-free way to pin the classification text itself matches.
    uint8_t tmpl[BUF_CAP];

    memset(tmpl, 0, sizeof(tmpl));
    uint16_t beat_len = build_beat_datagram(tmpl);
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, beat_len);
        if (side) handleUdpFrame_nrf52(buf, beat_len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, beat_len, IPAddress(1, 2, 3, 4));
        TEST_ASSERT_NOT_NULL(strstr(Serial.captured().c_str(), "type;BEAT"));
    }

    memset(tmpl, 0, sizeof(tmpl));
    uint16_t conf_len = build_conf_datagram(tmpl, "DK5EN-9");
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, conf_len);
        if (side) handleUdpFrame_nrf52(buf, conf_len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, conf_len, IPAddress(1, 2, 3, 4));
        TEST_ASSERT_NOT_NULL(strstr(Serial.captured().c_str(), "type;CONF"));
    }

    memset(tmpl, 0, sizeof(tmpl));
    memcpy(tmpl, "ZZZZ", 4);          // not GATE/CONF/BEAT
    memset(tmpl + 4, 0xAB, 8);
    uint16_t other_len = 12;
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, other_len);
        if (side) handleUdpFrame_nrf52(buf, other_len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, other_len, IPAddress(1, 2, 3, 4));
        TEST_ASSERT_NOT_NULL(strstr(Serial.captured().c_str(), "type;OTHER"));
    }
}

static void test_agreement_conf_updates_node_call_and_short_identically(void)
{
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_conf_datagram(tmpl, "DK5EN-7", "DK7");

    // ESP32 side.
    recorder_reset();
    node_hostip = IPAddress(10, 0, 0, 5);
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(10, 0, 0, 5));   // matching source
    TEST_ASSERT_EQUAL_STRING("DK5EN-7", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_STRING("DK7", meshcom_settings.node_short);
    TEST_ASSERT_TRUE(had_initial_udp_conn);
    TEST_ASSERT_EQUAL_INT(1, g_save_settings_calls);
    TEST_ASSERT_TRUE_MESSAGE(rebootAuto > 0, "esp32: CONF must schedule an auto-reboot");

    // nRF52 side, same frame.
    recorder_reset();
    neth.udp_dest_addr = IPAddress(10, 0, 0, 5);
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(10, 0, 0, 5));   // matching source
    TEST_ASSERT_EQUAL_STRING("DK5EN-7", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_STRING("DK7", meshcom_settings.node_short);
    TEST_ASSERT_TRUE(neth.had_initial_udp_conn);
    TEST_ASSERT_EQUAL_INT(1, g_save_settings_calls);
    TEST_ASSERT_TRUE_MESSAGE(rebootAuto > 0, "nrf52: CONF must schedule an auto-reboot");

    // Both reject a CONF datagram whose source IP does not match the
    // resolved gateway -- neither applies the callsign.
    recorder_reset();
    node_hostip = IPAddress(10, 0, 0, 5);
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(9, 9, 9, 9));    // spoofed source
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-1", meshcom_settings.node_call,
                                     "esp32 applied a CONF from an unmatched source");
    TEST_ASSERT_EQUAL_INT(0, g_save_settings_calls);

    recorder_reset();
    neth.udp_dest_addr = IPAddress(10, 0, 0, 5);
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(9, 9, 9, 9));    // spoofed source
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-1", meshcom_settings.node_call,
                                     "nrf52 applied a CONF from an unmatched source");
    TEST_ASSERT_EQUAL_INT(0, g_save_settings_calls);
}

static void test_agreement_zero_scan_bound_matches_on_odd_length(void)
{
    // BUG-13 fix: nRF52's zero-scan loop used `for (i = 0; i < packetSize;
    // i += 2)`, one byte too permissive for an odd packetSize -- its last
    // iteration read a FOURTH pair (6,7) for a 7-byte datagram, where byte 7
    // is one past the logical datagram. ESP32's `for (i = 0; i + 1 <
    // packetSize; i += 2)` never had this: it stops after (4,5), the third
    // and last IN-BOUNDS pair. Fixed on ESP32 by commit cd88ae12
    // (SEC-05/SEC-06/BUG-12); the same fix now applied to nRF52
    // (src/nrf52/udp_frame_nrf52.cpp:53) -- this test used to live in the
    // DRIFT block, pinning the disagreement the bug caused. See
    // test_regression_zero_scan_oob_byte_flips_verdict_before_fix() below
    // for a case where the extra byte does not just change zerocount's
    // final value but flips accept/reject outright.
    //
    // A fully-zeroed buffer with headroom makes the bound itself observable
    // without touching unmapped memory: the fixed-size UDP receive buffer
    // on hardware is exactly this shape -- fixed capacity, a shorter
    // logical packetSize. Both loops now read exactly the same 3 in-bounds
    // pairs -- (0,1)(2,3)(4,5) -- never touching index 6 or 7: zerocount 6
    // on both, at the MAX_ZEROS (6) boundary, both accept.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));   // whole buffer zero, including the tail
    const int len = 7;

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);

        char msg[96];
        if (side)
        {
            int rc = handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
            snprintf(msg, sizeof(msg), "%s: 3 in-bounds pairs (zerocount 6) must accept", name);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, msg);
        }
        else
        {
            handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
            snprintf(msg, sizeof(msg), "%s: 3 pairs (zerocount 6) must accept", name);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_reset_udp, msg);
        }
    }
}

static void test_regression_zero_scan_oob_byte_flips_verdict_before_fix(void)
{
    // Regression for the bound fixed just above: constructs a case where
    // the one-byte over-read does not merely nudge zerocount's final value
    // but flips the accept/reject VERDICT outright, so this test is a
    // meaningful fail-before/pass-after check on its own (not just a
    // restatement of the agreement test).
    //
    // packetSize 15 (odd): the correct bound (`i + 1 < packetSize`) reads 7
    // in-bounds pairs, indices (0,1)..(12,13) -- 14 bytes, all zero here --
    // so zerocount accumulates to 14, over MAX_ZEROS (6): the correct
    // verdict is REJECT (rc == 1). Index 14, the datagram's real but
    // unpaired trailing byte (odd length always leaves one), is never read
    // by either correct scan.
    //
    // The buggy bound (`i < packetSize`) adds one more iteration at i=14,
    // pairing that trailing byte with inc_udp_buffer[15] -- one byte past
    // the 15-byte datagram, poisoned non-zero below. A non-zero pair resets
    // zerocount to 0 on the spot, and since it is the LOOP'S LAST
    // iteration, that reset is never overwritten: the accumulated
    // over-threshold count is silently erased and the all-zero frame is
    // wrongly ACCEPTED (rc == 0) -- a real garbage/malformed frame let
    // through solely because of what happened to sit one byte past the
    // buffer.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const int len = 15;

    recorder_reset();
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    buf[len] = 0xFF;   // poison one byte past the datagram; copy_into() only
                       // clears/copies the first `len` logical bytes, so this
                       // stands in for whatever real hardware's fixed-size
                       // UDP receive buffer happens to hold there

    int rc = handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, rc,
                                  "nrf52 must reject an all-zero 15-byte datagram (zerocount 14 "
                                  "from the 7 in-bounds pairs) regardless of the byte one past "
                                  "packetSize -- if this reads 0, the zero-scan bound is reading "
                                  "inc_udp_buffer[packetSize] again");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, txRingDepth(), "a rejected frame must not relay");
}

// ===========================================================================
// DRIFT: differences that exist today and must not change silently
// ===========================================================================

static void test_drift_extudp_requires_hasExternIPaddress_on_esp32_only(void)
{
    // ESP32 gates the EXTUDP forward on `hasExternIPaddress && bEXTUDP`;
    // nRF52 has no hasExternIPaddress concept at all and gates on bEXTUDP
    // alone (src/udp_frame.h).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "extudp", 0x3003);

    recorder_reset();
    bEXTUDP = true;
    hasExternIPaddress = false;
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_extern.size(),
                                  "esp32 forwarded to EXTUDP without hasExternIPaddress");

    recorder_reset();
    bEXTUDP = true;
    hasExternIPaddress = false;   // ESP32-only flag; nRF52 does not consult it
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_extern.size(),
                                  "nrf52 stopped forwarding on bEXTUDP alone -- drift row "
                                  "changed, update the matrix");
}

static void test_drift_extudp_gate_before_vs_after_msgtype_check(void)
{
    // ESP32 forwards to EXTUDP INSIDE the recognised-msg_type_b branch (a
    // GATE frame whose type is not ':'/'!'/'@' never reaches sendExtern()
    // at all). nRF52 forwards BEFORE the msg_type_b check, unconditionally
    // for every GATE frame -- so an unrecognised type is forwarded there
    // and dropped on ESP32.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const uint16_t bodylen = 8;
    uint16_t len = build_gate_raw(tmpl, 0xFF /* unrecognised */, 0xAB, bodylen);

    recorder_reset();
    bEXTUDP = true;
    hasExternIPaddress = true;   // ESP32's other precondition satisfied, so
                                 // this isolates the before/after ordering
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_extern.size(),
                                  "esp32 forwarded an unrecognised msg_type to EXTUDP");

    recorder_reset();
    bEXTUDP = true;
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_extern.size(),
                                  "nrf52 stopped forwarding an unrecognised msg_type -- "
                                  "drift row changed, update the matrix");
    // RcvBuffer[0] is msg_type_b itself (the byte right after the 4-byte
    // "GATE" indicator) -- lora_tx_msg_len, and so buflen_arg, covers it
    // plus the bodylen fill bytes: bodylen+1, not bodylen.
    TEST_ASSERT_EQUAL_INT(bodylen + 1, g_extern[0].buflen_arg);
    TEST_ASSERT_EQUAL_UINT8(0xFF, g_extern[0].bytes[0]);
    for (uint16_t i = 0; i < bodylen; i++)
        TEST_ASSERT_EQUAL_UINT8(0xAB, g_extern[0].bytes[1 + i]);
}

static void test_drift_extudp_length_arg_type_differs_but_is_unobservable_at_the_clamp(void)
{
    // nRF52 casts the EXTUDP length argument to (uint8_t) before the call
    // (udp_frame_nrf52.cpp:99); ESP32 passes lora_tx_msg_len as uint16_t
    // unchanged (udp_frame_esp32.cpp:111).
    //
    // MEASURED LIMIT, recorded so nobody re-derives it: this cannot be made
    // to diverge from this boundary today. BOTH handlers clamp
    // lora_tx_msg_len to UDP_TX_BUF_SIZE (255) BEFORE the sendExtern() call
    // -- and 255 is exactly UINT8_MAX, so `(uint8_t)255 == 255`: the cast is
    // a no-op at the one value where it could matter, by construction of
    // the clamp itself. Forcing a value above 255 would mean feeding
    // decodeAPRS() a body longer than the real 255-byte wire format ever
    // produces, which is a bounds-safety question of its own and out of
    // scope for a length-argument-type test; not attempted here to avoid
    // asserting on a call this test has no business making. The real risk
    // is latent, not live: if UDP_TX_BUF_SIZE is ever raised past 255,
    // nRF52's cast starts silently truncating while ESP32's does not, with
    // no test between here and that day to catch it. Pinned instead: the
    // clamp itself, and that both sides forward the SAME length for a
    // representative frame.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "short", 0x3103);

    int lens[2];
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        bEXTUDP = true;
        hasExternIPaddress = true;
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        TEST_ASSERT_EQUAL_INT(1, (int)g_extern.size());
        lens[side] = g_extern[0].buflen_arg;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(lens[0], lens[1],
                                  "the forwarded length differs between platforms for the "
                                  "same frame -- the clamp argument above may have changed, "
                                  "re-examine whether the cast drift is still unobservable");
    TEST_ASSERT_TRUE(lens[0] <= 255);
}

static void test_drift_dedup_gate_position_esp32_honors_gateway_nopos_nrf52_does_not(void)
{
    // src/udp_frame.h: "ESP32 reads the dedup gate BEFORE the position
    // branch inserts the msg_id (TM-31); the nRF52 path never had the early
    // insert to begin with." That fix means a fresh vs. repeated position
    // frame behaves THE SAME on both sides today (pinned below as the
    // agreement half) -- the historical self-dedup bug the fix addresses
    // (a position frame deduplicating against the entry it had JUST
    // inserted, radiating 0 of 30 injected frames) does not reproduce on
    // either side any more, so it is not independently observable here.
    //
    // What the surviving structural difference DOES still make observable
    // is bGATEWAY_NOPOS: ESP32's position branch (the same code region the
    // early dedup read guards) checks it and can veto the relay for a
    // position frame specifically; nRF52 never references bGATEWAY_NOPOS at
    // all. That is the live, testable shape of "the two position branches
    // are different code today", even though the specific TM-31 symptom
    // is gone from both.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x4004);

    // Agreement half: fresh relays, repeat does not -- on both.
    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        int depth0 = txRingDepth();
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: fresh position frame must relay", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(), msg);

        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        snprintf(msg, sizeof(msg), "%s: repeated position frame must not relay again", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(), msg);
    }

    // Drift half: bGATEWAY_NOPOS, fresh id each time (never seen before).
    recorder_reset();
    bGATEWAY_NOPOS = true;
    uint16_t len2 = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x4005);
    uint8_t buf[BUF_CAP];
    int depth0 = txRingDepth();
    copy_into(buf, tmpl, len2);
    handleUdpFrame_esp32(buf, len2, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(),
                                  "esp32 relayed a position frame despite bGATEWAY_NOPOS");

    recorder_reset();
    bGATEWAY_NOPOS = true;   // nRF52 never reads this flag in the frame handler
    uint16_t len3 = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x4006);
    depth0 = txRingDepth();
    copy_into(buf, tmpl, len3);
    handleUdpFrame_nrf52(buf, len3, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(),
                                  "nrf52 started honouring bGATEWAY_NOPOS -- drift row "
                                  "changed, update the matrix");
}

static void test_drift_senddisplayposition_esp32_only(void)
{
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x5005);

    recorder_reset();
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayPosition_calls,
                                  "esp32 stopped calling sendDisplayPosition() for a position frame");

    recorder_reset();
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_sendDisplayPosition_calls,
                                  "nrf52 started calling sendDisplayPosition() -- drift row "
                                  "changed, update the matrix");
}

static void test_drift_rx01_unconfigured_guard_esp32_only(void)
{
    // RX-01 (BACKLOG 3.8k), second door: a GATE frame whose APRS source is
    // still the factory callsign is counted and refused relay -- on ESP32.
    // nRF52 has no such check. The factory callsign is used here ONLY as
    // the SOURCE of an inbound frame under test, never as a transmitting
    // identity (never used as an outbound sender elsewhere in this suite).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "XX0XXX-00", "9", ':', "unconfigured", 0x6006);

    recorder_reset();
    uint8_t buf[BUF_CAP];
    int depth0 = txRingDepth();
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_dropped.size(), "esp32 lost the RX-01 count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(),
                                  "esp32 relayed a frame from an unconfigured source");

    recorder_reset();
    depth0 = txRingDepth();
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_dropped.size(),
                                  "nrf52 grew an RX-01 guard -- drift row changed, update the matrix");
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(),
                                  "nrf52 stopped relaying a frame from an unconfigured source -- "
                                  "drift row changed, update the matrix");
}

static void test_drift_decodeaprs_reject_suppresses_processing_nrf52_only(void)
{
    // DR-06: decodeAPRS() returns 0x00 (aprs_functions.cpp) for a GATE frame
    // whose logical body is shorter than 16 bytes. nRF52 captures the return
    // value into msg_type_b_lora and gates ALL further processing on it
    // (udp_frame_nrf52.cpp:119-121) -- DECIDED 2026-09-12 nrf52-correct.
    // ESP32 discards decodeAPRS()'s return value (udp_frame_esp32.cpp:117)
    // and keeps dispatching on the RAW msg_type_b byte read straight off the
    // wire, so a rejected 0x21 ('!' position) frame still runs
    // sendDisplayPosition() and unconditionally inserts msg_id 0 into the
    // dedup ring (REVIEW 2026-09-12, fable Finding 7) -- neither is gated by
    // the RX-01 bSrcUnconfigured check either. The relay itself stays
    // suppressed on ESP32 today only by accident: isUnconfiguredCall("")
    // (the source_call a failed decode leaves behind) reads true, so
    // bUDPtoLoraSend is false regardless of this bug -- pinned below too, so
    // a future change that fixes DR-06 by tightening isUnconfiguredCall()
    // instead of porting the return-value gate would be caught.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    // Body: type 0x21 + 4 non-zero fill bytes -> rsize 5, under decodeAPRS's
    // 16-byte floor (aprs_functions.cpp).
    uint16_t len = build_gate_raw(tmpl, 0x21, 0xAB, 4);

    // nRF52 (decided-correct): decode rejects, so the whole block --
    // including the print and the position-branch side effects -- never
    // runs.
    recorder_reset();
    bDisplayInfo = true;
    uint8_t buf[BUF_CAP];
    int depth0 = txRingDepth();
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "nrf52 printed a frame decodeAPRS() rejected");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_sendDisplayPosition_calls,
                                  "nrf52 called sendDisplayPosition() on a frame decodeAPRS() rejected");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, loraWrite.load(),
                                  "nrf52 inserted a decode-rejected frame's msg_id into the dedup ring");
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(),
                                  "nrf52 relayed a frame decodeAPRS() rejected");

    // ESP32 (today's bug): decode's return value is discarded, so the raw
    // msg_type_b byte alone still drives the position branch on a zeroed
    // aprsmsg. If any of these flips, DR-06 has been fixed -- update the
    // matrix and tighten this half of the test.
    recorder_reset();
    bDisplayInfo = true;
    depth0 = txRingDepth();
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(),
                                  "esp32 stopped printing a decode-rejected frame -- drift row "
                                  "changed, update the matrix (DR-06 may now be fixed)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayPosition_calls,
                                  "esp32 stopped calling sendDisplayPosition() on a decode-rejected "
                                  "frame -- drift row changed, update the matrix");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, loraWrite.load(),
                                  "esp32 stopped inserting a decode-rejected frame's msg_id into "
                                  "the dedup ring -- drift row changed, update the matrix");
    TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(),
                                  "esp32 relayed a decode-rejected frame to LoRa TX -- the accidental "
                                  "RX-01 protection from isUnconfiguredCall(\"\") is load-bearing here, "
                                  "per the DR-06 matrix note; re-examine before removing it");
}

static void test_drift_conf_zero_address_guard_esp32_only(void)
{
    // DR-08: ESP32 rejects a CONF frame when the source doesn't match the
    // resolved gateway OR the resolved gateway address is still unresolved
    // (0.0.0.0) -- an explicit, independent check
    // (udp_frame_esp32.cpp:381). nRF52 checks only equality
    // (udp_frame_nrf52.cpp:372) -- a CONF datagram whose OWN source happens
    // to read 0.0.0.0 slips through while the destination address is also
    // still unresolved (0.0.0.0 before DHCP/pre-resolve). DECIDED
    // 2026-09-12 esp32-correct.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_conf_datagram(tmpl, "DK5EN-8");

    // ESP32: gateway address unresolved (node_hostip == 0) -- the frame's
    // OWN source also reads 0.0.0.0, so the plain equality check alone
    // would pass; the explicit zero-guard is what rejects it.
    recorder_reset();
    node_hostip = IPAddress(0, 0, 0, 0);
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(0, 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-1", meshcom_settings.node_call,
                                     "esp32 applied a CONF while the gateway address was unresolved (0.0.0.0)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_save_settings_calls,
                                  "esp32 saved settings from a CONF received before the gateway address resolved");

    // nRF52: same scenario -- udp_dest_addr defaults to 0.0.0.0 (unresolved,
    // see recorder_reset()'s `neth = NrfETH();`) and the source also reads
    // 0.0.0.0, so the plain equality check (no independent zero-guard)
    // passes and the CONF is applied. This is today's bug the row exists to
    // fix.
    recorder_reset();
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)neth.udp_dest_addr == 0,
                             "fixture assumption broken: neth.udp_dest_addr is not 0.0.0.0 by default");
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(0, 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-8", meshcom_settings.node_call,
                                     "nrf52 stopped applying a CONF from a 0.0.0.0 source while "
                                     "unresolved -- drift row changed, update the matrix (DR-08 may "
                                     "now be fixed)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_save_settings_calls,
                                  "nrf52 stopped saving settings in this scenario -- drift row "
                                  "changed, update the matrix");
}

static void test_drift_ack_phone_frame_attribution_esp32_only(void)
{
    // DR-09: the BLE ack/rej phone frame built when a UDP-received text
    // message carries a ":ack"/":rej" payload. ESP32 builds it via the
    // shared buildAckPhoneFrame() (src/ack_attribution.h), which appends
    // the acknowledging callsign as a variable-length suffix and forwards
    // the real length (udp_frame_esp32.cpp:253,258). nRF52 hand-rolls the
    // same 7-byte base frame inline and always calls
    // addBLEOutBuffer(print_buff, 7) -- no attribution suffix is ever
    // attached (udp_frame_nrf52.cpp:217-223,242). DECIDED 2026-09-12
    // esp32-correct, via the shared helper (the status byte itself was
    // already brought in line by DRY-21; only length/attribution remains
    // open).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    // Destination == own node_call, NOT "*" (which would force iAckPos back
    // to 0 before the ack branch is even reached); payload carries ":ack"
    // at a position > 0.
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-9", "DK5EN-1", ':', "x:ack3", 0x7007);

    const char *attribution = "DK5EN-9";
    uint16_t expected_len_esp32 = (uint16_t)(7 + strlen(attribution));

    recorder_reset();
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_TRUE_MESSAGE(g_ble.size() >= 1, "esp32 did not build an ack phone frame");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x41, g_ble[0][0], "esp32 ack frame indicator byte");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected_len_esp32, (int)g_ble[0].size(),
                                  "esp32 stopped attributing the ack -- the frame length no "
                                  "longer matches base(7)+attribution");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)strlen(attribution), g_ble[0][6],
                                  "esp32 ack frame attribution length byte");
    for (size_t i = 0; i < strlen(attribution); i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)attribution[i], g_ble[0][7 + i],
                                        "esp32 ack frame attribution byte mismatch");

    recorder_reset();
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_TRUE_MESSAGE(g_ble.size() >= 1, "nrf52 did not build an ack phone frame");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x41, g_ble[0][0], "nrf52 ack frame indicator byte");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, (int)g_ble[0].size(),
                                  "nrf52 started attributing the ack -- drift row changed, update "
                                  "the matrix (DR-09 may now be fixed; the length should match "
                                  "base(7)+attribution once it is)");
}

static void test_drift_max_zeros_return_value_and_reset_call_differ(void)
{
    // ESP32 returns void and calls resetMeshComUDP() itself; nRF52 returns 1
    // and calls nothing -- its caller resets DHCP instead (src/udp_frame.h,
    // src/nrf52/nrf_eth.h's own doc comment on handleUdpFrame_nrf52()).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const int len = 16;   // even length: both loops read identical pairs
                          // (test_agreement_zero_scan_bound_matches_on_odd_length
                          // below covers the odd-length case)

    recorder_reset();
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_reset_udp,
                                  "esp32 stopped calling resetMeshComUDP() on the reject path");

    recorder_reset();
    copy_into(buf, tmpl, len);
    int rc = handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, rc,
                                  "nrf52 stopped returning 1 on the too-many-zeros reject path");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_reset_udp,
                                  "nrf52 called the ESP32-only resetMeshComUDP() sink -- "
                                  "drift row changed, update the matrix");
}

// ===========================================================================
// U1 CORPUS DUMP: the ordered sink-call sequence per corpus datagram (test
// plan section 4.4, "Fixture written": native/u1-esp32.txt, native/u1-nrf52.
// txt), consumed by section 9.1's after-run diff. Additive alongside the
// named AGREEMENT/DRIFT cases above -- bulk coverage, not a replacement:
// those pin specific drift with failure messages naming DR rows and reasons,
// which a corpus dump cannot do on its own.
//
// Deliberately excluded from the dump (see the g_sink_log comment above for
// why): raw Serial output and checkOwnTx() reads.
// ===========================================================================

// This source lives at <repo_root>/test/test_udp_frame_twin/test_udp_frame_
// twin.cpp. A PlatformIO native test binary's CWD is not guaranteed (it can
// be the project root, the build dir, or wherever the runner happened to
// start), so a path relative to CWD can silently resolve to nothing.
//
// __FILE__, measured on this build (`strings` on the .o -- see the wave
// report), comes back RELATIVE ("test/test_udp_frame_twin/test_udp_frame_
// twin.cpp"): SCons runs with the project root as its own CWD, so the
// relative form is correct at COMPILE time -- but useless standing alone at
// RUN time, since this binary's run-time CWD is exactly the thing not
// guaranteed to still be the project root (the trap this function exists
// to dodge). An absolute __FILE__ (a different toolchain/build system)
// is handled directly, by stripping the known suffix. For the relative
// case, the fallback is this executable's own resolved path: PlatformIO
// always builds a native test to <repo_root>/.pio/build/<env>/program, a
// fixed 4-component depth, and resolving argv[0]/the running image through
// the OS (not through textual path arithmetic on the current CWD) is safe
// regardless of where the binary is invoked from.
static std::string g_argv0;   // captured in main() before anything else runs

static std::string resolve_absolute_path(const std::string &path)
{
    char buf[PATH_MAX];
    if (path.empty())
        return std::string();
    if (realpath(path.c_str(), buf) != nullptr)
        return std::string(buf);
    return std::string();
}

static std::string running_executable_path()
{
#if defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) == 0)
    {
        std::string resolved = resolve_absolute_path(raw);
        if (!resolved.empty())
            return resolved;
    }
#endif
    // Linux: /proc/self/exe is a symlink the OS resolves to the running
    // image regardless of how the process was invoked.
    std::string resolved = resolve_absolute_path("/proc/self/exe");
    if (!resolved.empty())
        return resolved;
    // Last resort: argv[0] realpath()'d against the CURRENT cwd. Valid even
    // then, because if argv[0] was relative, the OS resolved it against
    // this same cwd at exec() time and the process has not chdir'd since.
    return resolve_absolute_path(g_argv0);
}

static std::string strip_trailing_path_components(std::string p, int n)
{
    for (int i = 0; i < n; i++)
    {
        size_t pos = p.find_last_of("/\\");
        if (pos == std::string::npos)
            return std::string();
        p.erase(pos);
    }
    return p;
}

static std::string repo_root_from_file()
{
    std::string f = __FILE__;
    for (char &c : f)
        if (c == '\\') c = '/';

    static const char *anchor = "test/test_udp_frame_twin/test_udp_frame_twin.cpp";
    size_t pos = f.rfind(anchor);
    if (pos != std::string::npos && pos > 0 &&
        pos + strlen(anchor) == f.size() && f[pos - 1] == '/')
    {
        // Absolute __FILE__: strip the anchor and its leading separator.
        return f.substr(0, pos - 1);
    }

    // Relative __FILE__ (the measured case): fall back to the executable's
    // own OS-resolved location, four components below the repo root
    // (.pio/build/<env>/program).
    std::string exe = running_executable_path();
    if (exe.empty())
        return std::string();
    return strip_trailing_path_components(exe, 4);
}

// Corpus format (test/golden/corpus_lint.py, test/golden/mc_frame.py): lines
// starting with '#' are comments, the remaining hex digits (whitespace
// tolerated) are one datagram.
static bool read_hex_corpus_file(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;

    std::string hex;
    char line[2048];
    while (fgets(line, sizeof(line), f))
    {
        std::string l(line);
        size_t hashpos = l.find('#');
        if (hashpos != std::string::npos)
            l.erase(hashpos);
        for (char c : l)
            if (isxdigit((unsigned char)c))
                hex.push_back(c);
    }
    fclose(f);

    if (hex.size() % 2 != 0)
        return false;

    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        int hi = hexval(hex[i]);
        int lo = hexval(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return true;
}

// Drives every corpus datagram (sorted filename order) through one platform
// handler, recorder_reset() between EACH datagram -- the dump is one
// sequence PER datagram, not a chained simulation across the corpus, so a
// file's meaning never depends on what ran before it (same discipline the
// named cases above use). Appends "=== <filename>" plus the ordered sink
// lines (or "(no sink calls)") to out_path. Returns the number of corpus
// files processed; fails the test loudly (TEST_ASSERT, not a return code) if
// the corpus directory is missing, unreadable, empty, or any file in it
// fails to parse -- a silently-empty dump is exactly the false-comfort
// artefact this exists to avoid.
static int dump_corpus_for_platform(const std::string &corpus_dir,
                                     const std::string &out_path,
                                     bool nrf52_side)
{
    DIR *d = opendir(corpus_dir.c_str());
    {
        std::string msg = "U1: corpus directory missing or unreadable: " + corpus_dir;
        TEST_ASSERT_NOT_NULL_MESSAGE(d, msg.c_str());
    }

    std::vector<std::string> names;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());

    {
        std::string msg = "U1: corpus directory yielded zero datagrams: " + corpus_dir;
        TEST_ASSERT_TRUE_MESSAGE(!names.empty(), msg.c_str());
    }

    FILE *out = fopen(out_path.c_str(), "wb");
    {
        std::string msg = "U1: cannot open output file for writing: " + out_path;
        TEST_ASSERT_NOT_NULL_MESSAGE(out, msg.c_str());
    }

    int processed = 0;
    for (const auto &name : names)
    {
        std::vector<uint8_t> datagram;
        bool ok = read_hex_corpus_file(corpus_dir + "/" + name, datagram);
        {
            std::string msg = "U1: failed to parse corpus file " + name;
            TEST_ASSERT_TRUE_MESSAGE(ok, msg.c_str());
        }
        {
            std::string msg = "U1: corpus file " + name + " exceeds the working buffer capacity";
            TEST_ASSERT_TRUE_MESSAGE(datagram.size() <= BUF_CAP, msg.c_str());
        }

        recorder_reset();
        uint8_t buf[BUF_CAP];
        memset(buf, 0, BUF_CAP);
        if (!datagram.empty())
            memcpy(buf, datagram.data(), datagram.size());

        if (nrf52_side)
            handleUdpFrame_nrf52(buf, (int)datagram.size(), IPAddress(1, 2, 3, 4));
        else
            handleUdpFrame_esp32(buf, (int)datagram.size(), IPAddress(1, 2, 3, 4));

        fprintf(out, "=== %s\n", name.c_str());
        if (g_sink_log.empty())
        {
            fprintf(out, "(no sink calls)\n");
        }
        else
        {
            for (size_t i = 0; i < g_sink_log.size(); i++)
                fprintf(out, "%03zu\t%s\t%s\n", i + 1,
                        g_sink_log[i].sink.c_str(), g_sink_log[i].detail.c_str());
        }
        processed++;
    }
    fclose(out);
    return processed;
}

static void test_u1_corpus_ordered_sink_dump_both_platforms(void)
{
    std::string root = repo_root_from_file();
    TEST_ASSERT_TRUE_MESSAGE(!root.empty(),
        "U1: could not derive the repo root from __FILE__ -- this source moved, "
        "update repo_root_from_file()'s component count");

    std::string corpus_dir = root + "/test/golden/corpus/udp1990";
    std::string out_esp32 = root + "/test/golden/native/u1-esp32.txt";
    std::string out_nrf52 = root + "/test/golden/native/u1-nrf52.txt";

    int n_esp32 = dump_corpus_for_platform(corpus_dir, out_esp32, false);
    int n_nrf52 = dump_corpus_for_platform(corpus_dir, out_nrf52, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(n_esp32, n_nrf52,
        "U1: esp32 and nrf52 dumps processed a different number of corpus files");
}

int main(int, char **argv)
{
    g_argv0 = argv[0] ? argv[0] : "";

    UNITY_BEGIN();

    RUN_TEST(test_agreement_gate_text_message_decodes_and_relays_on_both);
    RUN_TEST(test_agreement_dedup_blocks_repeat_relay_on_both);
    RUN_TEST(test_agreement_max_zeros_rejected_by_both);
    RUN_TEST(test_agreement_indicator_dispatch_prints_matching_gw_rx_type_lines);
    RUN_TEST(test_agreement_conf_updates_node_call_and_short_identically);
    RUN_TEST(test_agreement_zero_scan_bound_matches_on_odd_length);
    RUN_TEST(test_regression_zero_scan_oob_byte_flips_verdict_before_fix);

    RUN_TEST(test_drift_extudp_requires_hasExternIPaddress_on_esp32_only);
    RUN_TEST(test_drift_extudp_gate_before_vs_after_msgtype_check);
    RUN_TEST(test_drift_extudp_length_arg_type_differs_but_is_unobservable_at_the_clamp);
    RUN_TEST(test_drift_dedup_gate_position_esp32_honors_gateway_nopos_nrf52_does_not);
    RUN_TEST(test_drift_senddisplayposition_esp32_only);
    RUN_TEST(test_drift_rx01_unconfigured_guard_esp32_only);
    RUN_TEST(test_drift_decodeaprs_reject_suppresses_processing_nrf52_only);
    RUN_TEST(test_drift_conf_zero_address_guard_esp32_only);
    RUN_TEST(test_drift_ack_phone_frame_attribution_esp32_only);
    RUN_TEST(test_drift_max_zeros_return_value_and_reset_call_differ);

    RUN_TEST(test_u1_corpus_ordered_sink_dump_both_platforms);

    return UNITY_END();
}
