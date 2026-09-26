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

#include "../../src/mc_text.h"
#include "../../src/udp_frame.h"   // F4: buildExternAckJson() direkt pruefen
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
#include <dm_dedup.h>        // 2.1 hookup under test: dmDedupReset()
#include <reack_limiter.h>   // 0.2 hookup under test: reackLimiterReset()

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
bool bKISS = false;          // KISS/TCP on (loop_functions.cpp on the board)
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
// msg_destination_path as the display saw it (sendDisplayText()/
// sendDisplayPosition(), in call order). Kept out of the ordered sink log on
// purpose, so the U1 corpus dump does not change with it.
static std::vector<std::string> g_display_dest_path;

// queueKiss() -- declared by stubs/kiss_functions.h, which shadows the real
// header (see there). Kept out of the ordered sink log on purpose: bKISS is
// off in every other test, and the corpus dump must not change with it.
struct KissCall
{
    std::vector<uint8_t> bytes;
    int16_t rssi;
    int8_t snr;
};
static std::vector<KissCall> g_kiss;
void queueKiss(uint8_t *buffer, uint16_t buflen, int16_t rssi, int8_t snr)
{
    g_kiss.push_back(KissCall{std::vector<uint8_t>(buffer, buffer + buflen), rssi, snr});
}

struct ExternCall
{
    bool bUDP;
    std::string src_type;
    std::vector<uint8_t> bytes;
    uint16_t buflen_arg;
};
static std::vector<ExternCall> g_extern;

// DR-18 part 2: queueExternAck() calls -- the EXTUDP ack status datagram,
// separate from g_extern above (that one is sendExtern()'s raw-APRS-bytes
// path; this is the JSON-ack path, see extudp_functions.cpp).
struct ExternAckCall
{
    uint32_t msg_id;
    uint8_t status;
    std::string from;
    std::string via;
};
static std::vector<ExternAckCall> g_extern_ack;

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
    p.source = aprsMessage.msg_source_call;
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
    (void)rssi; (void)snr;
    g_display_dest_path.push_back(aprsmsg.msg_destination_path);
    g_sendDisplayText_calls++;
    record_sink("DISPLAYTEXT", "-");
}

void sendDisplayPosition(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr)
{
    (void)rssi; (void)snr;
    g_display_dest_path.push_back(aprsmsg.msg_destination_path);
    g_sendDisplayPosition_calls++;
    record_sink("DISPLAYPOS", "-");
}

// Signature since the 2026-09-25 upstream merge (KISS #1151): returns the new
// msg_id and takes an optional foreign source call. Neither frame handler
// passes one; the stub returns 0 (no id), which neither handler reads.
unsigned int SendAckMessage(String dest_call, unsigned int iAckId, const char *src_override)
{
    (void)src_override;
    g_ack_dest.push_back(dest_call.c_str());
    g_ack_ids.push_back(iAckId);
    char buf[96];
    snprintf(buf, sizeof(buf), "dest=%s id=%x", dest_call.c_str(), iAckId);
    record_sink("ACK", buf);
    return 0;
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

// DR-18 part 2: real definition lives in extudp_functions.cpp, which this
// native env does not build (build_src_filter has neither the TU nor an
// ArduinoJson lib_dep) -- stubbed here exactly like sendExtern() above.
void queueExternAck(uint32_t msg_id, uint8_t status, const char *from, const char *via)
{
    ExternAckCall c;
    c.msg_id = msg_id;
    c.status = status;
    c.from = from ? from : "";
    c.via = via ? via : "";
    g_extern_ack.push_back(c);
    char buf[96];
    snprintf(buf, sizeof(buf), "msg_id=%08X status=%u from=%s via=%s",
             msg_id, (unsigned)status, c.from.c_str(), c.via.c_str());
    record_sink("EXTERNACK", buf);
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
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), src);
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), dest);
    mcSet(m.msg_payload, sizeof(m.msg_payload), payload);
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
    g_display_dest_path.clear();
    g_kiss.clear();
    g_extern.clear();
    g_extern_ack.clear();
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
    bKISS = false;
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

    // Stage 2.1 dedup table + stage 0.2 re-ACK rate limiter -- both process-
    // global, shared by every test in this binary the moment the RX-decode
    // hookup calls them.
    dmDedupReset();
    reackLimiterReset();

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

static void test_agreement_extudp_forward_ahead_of_dedup_gate_on_both(void)
{
    // DR-18 (drift-matrix.csv row, ordering clause): the EXTUDP forward
    // must stay AHEAD of is_new_packet() on both platforms, so a DUPLICATE
    // datagram still reaches EXTUDP even though its second relay to LoRa TX
    // is suppressed by the dedup ring -- nothing pinned this before. Same
    // repeat pattern as test_agreement_dedup_blocks_repeat_relay_on_both
    // just above, plus the EXTUDP assertion that test lacks: the TX ring
    // gains only ONE entry (the dedup gate did its job), but EXTUDP sees
    // the frame BOTH times (the forward never consults the dedup ring).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "extudp-dup", 0x2102);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bEXTUDP = true;
        hasExternIPaddress = true;
        int depth0 = txRingDepth();

        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        copy_into(buf, tmpl, len);   // identical frame, same msg_id -- a genuine repeat
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        char msg[128];
        snprintf(msg, sizeof(msg), "%s: duplicate must not relay to LoRa TX a second time", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0 + 1, txRingDepth(), msg);

        snprintf(msg, sizeof(msg), "%s: EXTUDP forward must still see the duplicate -- it must "
                                    "stay ahead of is_new_packet()", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)g_extern.size(), msg);
    }
}

// Stage 2.1 (dm_dedup.h) + stage 0.2 (reack_limiter.h), server ingress hookup
// (docs/snf-port-campaign.md wave 2 / wave2-anchors.md Group B). A DM
// addressed to this node, same (source call, NNN, stripped payload) but a
// FRESH msg_id each time -- the shape a stage-1 retry-ladder resend takes
// (not built yet): the stage-0 msg_id dedup ring alone (is_new_packet(),
// already exercised by test_agreement_dedup_blocks_repeat_relay_on_both
// above) cannot catch this, since the id differs on every attempt. The
// first delivery must display, forward to the phone and ack; a repeat of
// the same (call, NNN, payload) must NOT display or forward again, but --
// once the 0.2 re-ACK limiter's 30 s window has passed -- must still be
// acked (fork-main semantics: a lost :ackNNN is repaired without a second
// copy of the message reaching the phone).
static void test_regression_dm_dedup_reacks_without_redisplay_on_both(void)
{
    uint8_t tmpl1[BUF_CAP], tmpl2[BUF_CAP];
    memset(tmpl1, 0, sizeof(tmpl1));
    memset(tmpl2, 0, sizeof(tmpl2));
    // "duptest{007": no leading '{' (iEnqPos scans from offset 1), "{007"
    // is the transport-sequence tag stripped before the dedup key is built.
    uint16_t len1 = build_gate_datagram(tmpl1, "DK5EN-2", "DK5EN-1", ':', "duptest{007", 0x8001);
    uint16_t len2 = build_gate_datagram(tmpl2, "DK5EN-2", "DK5EN-1", ':', "duptest{007", 0x8002);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bDisplayInfo = true;
        mc_test_set_millis(1000);

        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl1, len1);
        if (side) handleUdpFrame_nrf52(buf, len1, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len1, IPAddress(1, 2, 3, 4));

        char msg[160];
        snprintf(msg, sizeof(msg), "%s: first delivery must display", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayText_calls, msg);
        snprintf(msg, sizeof(msg), "%s: first delivery must forward to the phone", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_ble.size(), msg);
        snprintf(msg, sizeof(msg), "%s: first delivery must ack", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_ack_ids.size(), msg);
        snprintf(msg, sizeof(msg), "%s: first ack has the wrong NNN", name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, g_ack_ids[0], msg);

        // Past the 0.2 re-ACK limiter's 30 s window, so the second ack below
        // exercises the dedup-duplicate re-ack path, not the rate limiter.
        mc_test_set_millis(1000 + 31000);

        copy_into(buf, tmpl2, len2);   // same (call, NNN, payload), fresh msg_id
        if (side) handleUdpFrame_nrf52(buf, len2, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len2, IPAddress(1, 2, 3, 4));

        snprintf(msg, sizeof(msg), "%s: repeat (call,NNN,payload) must not display again", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayText_calls, msg);
        snprintf(msg, sizeof(msg), "%s: repeat must not forward to the phone again", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_ble.size(), msg);
        snprintf(msg, sizeof(msg), "%s: repeat must still be acked (fork-main semantics)", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)g_ack_ids.size(), msg);
        snprintf(msg, sizeof(msg), "%s: second ack has the wrong NNN", name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, g_ack_ids[1], msg);
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

// DR-03 / F3: die vier Latch-Loeschungen sind sonst durch KEINEN Test
// gedeckt -- man kann jede einzelne entfernen und 961 Faelle bleiben gruen.
//
// Der Latch hb_warn_logged macht aus der Heartbeat-Warnung einen Einmalruf pro
// Episode. Wer Serververkehr sieht, muss ihn zusammen mit der Alterung
// zuruecksetzen, sonst warnt der Knoten nach der ersten Stille nie wieder --
// ein Defekt, den man nur daran merkt, dass eine Logzeile FEHLT. Genau die
// Sorte, die jahrelang unbemerkt bleibt.
//
// Beide Plattformen, alle drei erreichbaren Zweige (GATE braucht einen
// vollstaendigen LoRa-Frame und ist durch die Faelle oben abgedeckt; BEAT,
// CONF und OTHER sind hier die drei, die ein Datagramm allein ausloest).
static void test_agreement_live_traffic_clears_the_heartbeat_warn_latch(void)
{
    uint8_t tmpl[BUF_CAP];

    struct { const char *name; uint16_t len; } kinds[3];

    memset(tmpl, 0, sizeof(tmpl));
    uint16_t beat_len = build_beat_datagram(tmpl);
    uint8_t beat_tmpl[BUF_CAP];
    memcpy(beat_tmpl, tmpl, sizeof(beat_tmpl));

    memset(tmpl, 0, sizeof(tmpl));
    uint16_t conf_len = build_conf_datagram(tmpl, "DK5EN-9");
    uint8_t conf_tmpl[BUF_CAP];
    memcpy(conf_tmpl, tmpl, sizeof(conf_tmpl));

    uint8_t other_tmpl[BUF_CAP];
    memset(other_tmpl, 0, sizeof(other_tmpl));
    memcpy(other_tmpl, "ZZZZ", 4);
    memset(other_tmpl + 4, 0xAB, 8);
    uint16_t other_len = 12;

    (void)kinds;

    const uint8_t *tmpls[3] = { beat_tmpl, conf_tmpl, other_tmpl };
    const uint16_t lens[3]  = { beat_len,  conf_len,  other_len };
    const char *names[3]    = { "BEAT",    "CONF",    "OTHER"   };

    for (int k = 0; k < 3; k++)
    {
        for (int side = 0; side < 2; side++)
        {
            recorder_reset();
            hb_warn_logged = true;          // eine Warnung steht bereits

            uint8_t buf[BUF_CAP];
            copy_into(buf, tmpls[k], lens[k]);
            if (side) handleUdpFrame_nrf52(buf, lens[k], IPAddress(1, 2, 3, 4));
            else      handleUdpFrame_esp32(buf, lens[k], IPAddress(1, 2, 3, 4));

            char msg[160];
            snprintf(msg, sizeof(msg),
                     "DR-03: %s auf %s hat hb_warn_logged nicht geloescht -- "
                     "nach der ersten Warnung warnt dieser Knoten nie wieder, "
                     "bis die handelnde Stufe den Latch faellt",
                     names[k], side ? "nrf52" : "esp32");
            TEST_ASSERT_FALSE_MESSAGE(hb_warn_logged, msg);
        }
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
// AGREEMENT (formerly DRIFT, unified 2026-09-12 wave, DR-20 added 2026-09-17
// wave W6): DR-02/DR-04/DR-05/DR-06/DR-07/DR-08/DR-09/DR-18/DR-19/DR-20 all
// had a decided verdict in the drift matrix and are implemented here --
// what were separate ESP32-only/nRF52-only cases are now single cases run
// on both sides. DR-01 needed no implementation (already fixed) and lives
// in the AGREEMENT block above. No U1 row is left in DRIFT as of this wave.
// ===========================================================================

static void test_agreement_extudp_requires_hasExternIPaddress_on_both(void)
{
    // DR-07 (2026-09-12 decided esp32-correct, STYLE not a fix -- sendExtern()
    // already guards itself on hasExternIPaddress internally, see
    // extudp_functions.cpp:455-458, so there was never an observable hole;
    // this pins the call SITE now reading alike): both handlers gate the
    // EXTUDP forward on `hasExternIPaddress && bEXTUDP`. Landed together with
    // DR-18 and DR-19 (one lifted call site, udp_frame_nrf52.cpp:128-135).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "extudp", 0x3003);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bEXTUDP = true;
        hasExternIPaddress = false;
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        char msg[96];
        snprintf(msg, sizeof(msg), "%s forwarded to EXTUDP without hasExternIPaddress", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_extern.size(), msg);
    }
}

static void test_agreement_extudp_forwards_one_decodable_frame_per_type_and_rejects_unrecognised(void)
{
    // DR-18 (2026-09-12 decided, RE-DECIDED withdrawing the wider 4-type
    // set; TEST clause added 2026-09-17 wave W6): ESP32's shape wins --
    // EXTUDP is forwarded only for the recognised msg_type_b set
    // (0x3A/0x21/0x40), now as its own explicit type test on BOTH platforms
    // (udp_frame_esp32.cpp, udp_frame_nrf52.cpp), lifted out of (nRF52) /
    // alongside (ESP32) the relay branch.
    //
    // Replaces a single 0xFF-garbage/undecodable case that used to be this
    // test's only coverage. An advisor PROVED by mutation that narrowing
    // BOTH handlers' type test to `msg_type_b == 0x3A` alone left the whole
    // suite green: 0xFF was already excluded before AND after that mutation
    // (it was never in the recognised set), so it never exercised the
    // difference, and nothing else in this file positively checked that
    // 0x21 (position) or 0x40 (hey) reach EXTUDP at all -- only
    // test_agreement_extudp_length_arg_matches_after_cast_removed did, and
    // only for 0x3A. This test drives one DECODABLE frame per recognised
    // type through both handlers and asserts each one DOES reach EXTUDP
    // (g_extern), so narrowing the check to 0x3A fails here on 0x21 and
    // 0x40 for both platforms. The negative case (an unrecognised,
    // undecodable type must NOT reach EXTUDP) is kept alongside the
    // positive ones rather than dropped, so this test still covers both
    // directions of the gate.
    struct TypeCase { char msgType; const char *payload; uint32_t msg_id; };
    static const TypeCase cases[] = {
        { ':', "extudp-text",                      0x8101 },   // 0x3A text
        { '!', "4700.00N/01300.00E-extudp",         0x8102 },   // 0x21 position
        { '@', "extudp-hey",                        0x8103 },   // 0x40 hey
    };

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
    {
        uint8_t tmpl[BUF_CAP];
        memset(tmpl, 0, sizeof(tmpl));
        uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", cases[c].msgType,
                                           cases[c].payload, cases[c].msg_id);

        for (int side = 0; side < 2; side++)
        {
            const char *name = side ? "nrf52" : "esp32";
            recorder_reset();
            bEXTUDP = true;
            hasExternIPaddress = true;
            uint8_t buf[BUF_CAP];
            copy_into(buf, tmpl, len);
            if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
            else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
            char msg[128];
            snprintf(msg, sizeof(msg), "%s did not forward a decodable 0x%02X frame to EXTUDP "
                                        "(0x3A-narrowing mutation would leave this undetected "
                                        "for 0x21/0x40)", name, (unsigned)cases[c].msgType);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_extern.size(), msg);
        }
    }

    // Negative half, unchanged in spirit from the case this replaces: an
    // unrecognised, undecodable msg_type must still not reach EXTUDP on
    // either platform.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const uint16_t bodylen = 8;
    uint16_t len = build_gate_raw(tmpl, 0xFF /* unrecognised */, 0xAB, bodylen);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bEXTUDP = true;
        hasExternIPaddress = true;
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        char msg[96];
        snprintf(msg, sizeof(msg), "%s forwarded an unrecognised msg_type to EXTUDP", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_extern.size(), msg);
    }
}

static void test_agreement_extudp_length_arg_matches_after_cast_removed(void)
{
    // DR-19 (2026-09-12 decided esp32-correct, class corrected cosmetic ->
    // bug-one-side (latent): inert and harmless are not the same thing):
    // nRF52's redundant (uint8_t) cast on the EXTUDP length argument
    // (previously udp_frame_nrf52.cpp:99) is REMOVED, not just left as a
    // documented no-op -- MAX_APRS_FRAME_SIZE (340) vs. UDP_TX_BUF_SIZE
    // (255) is an open lead (src/aprs_functions.cpp:10,
    // configuration_global.h:169); any work that resolves it by raising
    // UDP_TX_BUF_SIZE past 255 would have made this cast start silently
    // truncating on nRF52 only. Landed together with DR-18/DR-07 (one
    // lifted call site, udp_frame_nrf52.cpp:128-135).
    //
    // MEASURED LIMIT, recorded so nobody re-derives it: BOTH handlers clamp
    // lora_tx_msg_len to UDP_TX_BUF_SIZE (255) BEFORE the sendExtern() call
    // -- and 255 is exactly UINT8_MAX, so the removed cast was a no-op at
    // the one value where it could have mattered, by construction of the
    // clamp itself. Forcing a value above 255 would mean feeding
    // decodeAPRS() a body longer than the real 255-byte wire format ever
    // produces, which is a bounds-safety question of its own and out of
    // scope for a length-argument-type test; not attempted here to avoid
    // asserting on a call this test has no business making. Pinned instead:
    // the clamp itself, and that both sides forward the SAME length for a
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
                                  "re-examine the DR-19 cast removal");
    TEST_ASSERT_TRUE(lens[0] <= 255);
}

static void test_agreement_dedup_gate_position_and_gateway_nopos_on_both(void)
{
    // src/udp_frame.h: "ESP32 reads the dedup gate BEFORE the position
    // branch inserts the msg_id (TM-31); the nRF52 path never had the early
    // insert to begin with." That fix means a fresh vs. repeated position
    // frame behaves THE SAME on both sides today (pinned below as the
    // first agreement half) -- the historical self-dedup bug the fix
    // addresses (a position frame deduplicating against the entry it had
    // JUST inserted, radiating 0 of 30 injected frames) does not reproduce
    // on either side any more, so it is not independently observable here.
    //
    // DR-04 (2026-09-12 decided esp32-correct): bGATEWAY_NOPOS is now also
    // honoured on nRF52 (udp_frame_nrf52.cpp:225-231, landing after DR-06's
    // decode gate and DR-05's sendDisplayPosition() port in the same
    // block) -- previously nRF52 never referenced the flag at all, so a
    // position frame relayed regardless of it.
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

    // Second agreement half: bGATEWAY_NOPOS vetoes the relay on both, fresh
    // id each time (never seen before).
    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bGATEWAY_NOPOS = true;
        uint16_t len2 = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test",
                                            side ? 0x4006 : 0x4005);
        uint8_t buf[BUF_CAP];
        int depth0 = txRingDepth();
        copy_into(buf, tmpl, len2);
        if (side) handleUdpFrame_nrf52(buf, len2, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len2, IPAddress(1, 2, 3, 4));
        char msg[96];
        snprintf(msg, sizeof(msg), "%s relayed a position frame despite bGATEWAY_NOPOS", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(), msg);
    }
}

static void test_agreement_senddisplayposition_on_both(void)
{
    // DR-05 (2026-09-12 decided esp32-correct): sendDisplayPosition() is now
    // called for a position frame on both platforms
    // (udp_frame_nrf52.cpp:227, landing after DR-06's decode gate) --
    // previously nRF52 never called it at all (only sendDisplayText()).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x5005);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        char msg[96];
        snprintf(msg, sizeof(msg), "%s did not call sendDisplayPosition() for a position frame", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_sendDisplayPosition_calls, msg);
    }
}

static void test_agreement_rx01_unconfigured_guard_on_both(void)
{
    // RX-01 (BACKLOG 3.8k), second door: a GATE frame whose APRS source is
    // still the factory callsign is counted and refused relay. DR-02
    // (2026-09-12 decided esp32-correct): now on both platforms
    // (udp_frame_nrf52.cpp:164-168) -- previously nRF52 had no such check
    // and relayed the frame like any other. The factory callsign is used
    // here ONLY as the SOURCE of an inbound frame under test, never as a
    // transmitting identity (never used as an outbound sender elsewhere in
    // this suite).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "XX0XXX-00", "9", ':', "unconfigured", 0x6006);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        int depth0 = txRingDepth();
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
        char msg[80];
        snprintf(msg, sizeof(msg), "%s lost the RX-01 count", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_dropped.size(), msg);
        snprintf(msg, sizeof(msg), "%s relayed a frame from an unconfigured source", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(), msg);
    }
}

static void test_agreement_decodeaprs_reject_suppresses_processing_on_both(void)
{
    // DR-06 (2026-09-12 decided nrf52-correct, fable Finding 7 on the
    // display-call/ring-insert gap): decodeAPRS() returns 0x00
    // (aprs_functions.cpp) for a GATE frame whose logical body is shorter
    // than 16 bytes. Both platforms now capture the return value and gate
    // ALL further processing on it (udp_frame_esp32.cpp:145-147,
    // udp_frame_nrf52.cpp:144-146) -- previously ESP32 discarded it and
    // kept dispatching on the RAW msg_type_b byte read straight off the
    // wire, so a rejected 0x21 ('!' position) frame still ran
    // sendDisplayPosition() and unconditionally inserted msg_id 0 into the
    // dedup ring, neither gated by the RX-01 bSrcUnconfigured check either.
    // The relay itself was suppressed on ESP32 even before the fix, but
    // only by accident: isUnconfiguredCall("") (the source_call a failed
    // decode leaves behind) reads true, so bUDPtoLoraSend was false
    // regardless -- pinned below too, so a future regression that removes
    // the decode gate would still be caught even if it happened to leave
    // the relay path closed.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    // Body: type 0x21 + 4 non-zero fill bytes -> rsize 5, under decodeAPRS's
    // 16-byte floor (aprs_functions.cpp).
    uint16_t len = build_gate_raw(tmpl, 0x21, 0xAB, 4);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bDisplayInfo = true;
        uint8_t buf[BUF_CAP];
        int depth0 = txRingDepth();
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        char msg[96];
        snprintf(msg, sizeof(msg), "%s printed a frame decodeAPRS() rejected", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(), msg);
        snprintf(msg, sizeof(msg), "%s called sendDisplayPosition() on a frame decodeAPRS() rejected", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_sendDisplayPosition_calls, msg);
        snprintf(msg, sizeof(msg), "%s inserted a decode-rejected frame's msg_id into the dedup ring", name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, loraWrite.load(), msg);
        snprintf(msg, sizeof(msg), "%s relayed a frame decodeAPRS() rejected", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(depth0, txRingDepth(), msg);
    }
}

static void test_agreement_conf_zero_address_guard_on_both(void)
{
    // DR-08 (2026-09-12 decided esp32-correct): both platforms now reject a
    // CONF frame when the source doesn't match the resolved gateway OR the
    // resolved gateway address is still unresolved (0.0.0.0) -- an
    // explicit, independent check (udp_frame_esp32.cpp:387,
    // udp_frame_nrf52.cpp:438). Previously nRF52 checked only equality, so
    // a CONF datagram whose OWN source happened to read 0.0.0.0 slipped
    // through while udp_dest_addr was itself still unresolved
    // (0.0.0.0 before DHCP/pre-resolve).
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
    // 0.0.0.0; the explicit zero-guard now rejects this here too.
    recorder_reset();
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)neth.udp_dest_addr == 0,
                             "fixture assumption broken: neth.udp_dest_addr is not 0.0.0.0 by default");
    copy_into(buf, tmpl, len);
    handleUdpFrame_nrf52(buf, len, IPAddress(0, 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DK5EN-1", meshcom_settings.node_call,
                                     "nrf52 applied a CONF while the gateway address was unresolved (0.0.0.0)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_save_settings_calls,
                                  "nrf52 saved settings from a CONF received before the gateway address resolved");
}

static void test_agreement_ack_phone_frame_attribution_on_both(void)
{
    // DR-09 (2026-09-12 decided esp32-correct, via the shared helper): the
    // BLE ack/rej phone frame built when a UDP-received text message
    // carries a ":ack"/":rej" payload. Both platforms now build it via the
    // shared buildAckPhoneFrame() (src/ack_attribution.h), which appends
    // the acknowledging callsign as a variable-length suffix and forwards
    // the real length (udp_frame_esp32.cpp:283,288,
    // udp_frame_nrf52.cpp:293,301). Previously nRF52 hand-rolled the same
    // 7-byte base frame inline and always called addBLEOutBuffer(print_buff,
    // 7) -- no attribution suffix was ever attached (the status byte itself
    // was already brought in line earlier by DRY-21).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    // Destination == own node_call, NOT "*" (which would force iAckPos back
    // to 0 before the ack branch is even reached); payload carries ":ack"
    // at a position > 0.
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-9", "DK5EN-1", ':', "x:ack3", 0x7007);

    const char *attribution = "DK5EN-9";
    uint16_t expected_len = (uint16_t)(7 + strlen(attribution));

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        char msg[96];
        snprintf(msg, sizeof(msg), "%s did not build an ack phone frame", name);
        TEST_ASSERT_TRUE_MESSAGE(g_ble.size() >= 1, msg);
        snprintf(msg, sizeof(msg), "%s ack frame indicator byte", name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x41, g_ble[0][0], msg);
        snprintf(msg, sizeof(msg), "%s did not attribute the ack -- the frame length does not "
                                    "match base(7)+attribution", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected_len, (int)g_ble[0].size(), msg);
        snprintf(msg, sizeof(msg), "%s ack frame attribution length byte", name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)strlen(attribution), g_ble[0][6], msg);
        for (size_t i = 0; i < strlen(attribution); i++)
        {
            snprintf(msg, sizeof(msg), "%s ack frame attribution byte mismatch", name);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)attribution[i], g_ble[0][7 + i], msg);
        }
    }
}

static void test_agreement_extudp_ack_json_mirrors_ble_ack_on_both(void)
{
    // DR-18 part 2 (docs/ack-wer-hat-quittiert.md §6.3, implemented
    // 2026-09-17 wave W6): the same ":ack" frame as the DR-09 test just
    // above also queues an EXTUDP status datagram (queueExternAck(),
    // extudp_functions.cpp) at the same call site as the BLE ack frame --
    // same msg_id, same status byte (ack_attribution.h's 0x00/0x01/0x02
    // values ARE the doc's status values, mirrored verbatim, not
    // reinterpreted), same acknowledging callsign, via "udp" (this ack
    // arrived as a UDP GATE-relayed text frame). Deliberately NOT routed
    // through sendExtern() -- see that function's and queueExternAck()'s
    // own comments for why (the widened-type-set alternative was
    // considered and withdrawn).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-9", "DK5EN-1", ':', "x:ack3", 0x7107);

    const char *attribution = "DK5EN-9";
    // msg_counter = ((_GW_ID & 0x3FFFFF) << 10) | (iAckId & 0x3FF); iAckId=3
    // (the digit after ":ack" in the payload above), _GW_ID is the file-scope
    // default set at the top of this file.
    uint32_t expected_msg_id = ((_GW_ID & 0x3FFFFF) << 10) | (3 & 0x3FF);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        char msg[112];

        // W6b-Nachtrag (Advisor F1): die Ack-Ausleitung haengt jetzt an
        // bEXTUDP, genau wie queueExtern() in lora_functions.cpp:983. Erst der
        // AUS-Fall, damit die Wache selbst gepinnt ist und nicht nur der
        // Normalfall.
        recorder_reset();
        bEXTUDP = false;
        uint8_t off_buf[BUF_CAP];
        copy_into(off_buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(off_buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(off_buf, len, IPAddress(1, 2, 3, 4));
        snprintf(msg, sizeof(msg), "%s emitted an EXTUDP ack with --extudp off", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_extern_ack.size(), msg);

        recorder_reset();
        bEXTUDP = true;
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        snprintf(msg, sizeof(msg), "%s did not queue an EXTUDP ack datagram for a BLE ack frame", name);
        TEST_ASSERT_TRUE_MESSAGE(g_extern_ack.size() >= 1, msg);

        snprintf(msg, sizeof(msg), "%s EXTUDP ack msg_id does not match the BLE ack frame's", name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_msg_id, g_extern_ack[0].msg_id, msg);

        snprintf(msg, sizeof(msg), "%s EXTUDP ack status does not match the BLE ack frame's status byte (g_ble[0][5])", name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(g_ble[0][5], g_extern_ack[0].status, msg);

        snprintf(msg, sizeof(msg), "%s EXTUDP ack 'from' does not match the attributed callsign", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(attribution, g_extern_ack[0].from.c_str(), msg);

        snprintf(msg, sizeof(msg), "%s EXTUDP ack 'via' must be \"udp\"", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("udp", g_extern_ack[0].via.c_str(), msg);
    }
}

// ===========================================================================
// DRIFT (deliberate): KISS/TCP exists on ESP32 only
// ===========================================================================

// Upstream f070ad50 taps server-relayed TEXT/POSITION frames into queueKiss()
// inside its monolithic getMeshComUDPpacket(). That body lives in
// handleUdpFrame_esp32() here since the C1/U1 carve, so the 2026-09-25
// upstream merge resolved the udp_functions.cpp conflict to our side and
// silently dropped the tap; it was re-anchored by hand next to the dedup
// gate. This test fails without it (zero calls). nRF52 has no KISS at all,
// so its handler must never call queueKiss().
static void test_drift_kiss_server_relay_tap_is_esp32_only(void)
{
    uint8_t tmpl[BUF_CAP];
    uint8_t frame[BUF_CAP];
    uint8_t buf[BUF_CAP];
    char msg[96];

    // 1. A fresh GATE text frame with KISS on: queued once, as the exact
    //    decodeAPRS()-compatible frame (datagram minus the GATE indicator),
    //    with the "came from the server" sentinel rssi=99/snr=0.
    memset(tmpl, 0, sizeof(tmpl));
    uint16_t len = build_gate_datagram(tmpl, "DK5EN-2", "9", ':', "kiss tap", 0x7101);
    memset(frame, 0, sizeof(frame));
    uint16_t flen = build_frame(frame, "DK5EN-2", "9", ':', "kiss tap", 0x7101);

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bKISS = true;
        copy_into(buf, tmpl, len);
        if (side) handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else      handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        snprintf(msg, sizeof(msg), "%s: queueKiss() call count for a fresh text frame", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(side ? 0 : 1, (int)g_kiss.size(), msg);
    }
    recorder_reset();
    bKISS = true;
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_kiss.size(), "esp32: fresh text frame not queued");
    TEST_ASSERT_EQUAL_INT_MESSAGE(flen, (int)g_kiss[0].bytes.size(), "esp32: queued length");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(frame, g_kiss[0].bytes.data(), flen, "esp32: queued bytes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(99, g_kiss[0].rssi, "esp32: rssi sentinel");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_kiss[0].snr, "esp32: snr sentinel");

    // 2. The same msg_id again: the dedup gate stops a second delivery.
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_kiss.size(), "esp32: repeat was queued twice");

    // 3. KISS off: nothing.
    recorder_reset();
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_kiss.size(), "esp32: queued with KISS off");

    // 4. A position frame is queued too.
    recorder_reset();
    bKISS = true;
    memset(tmpl, 0, sizeof(tmpl));
    len = build_gate_datagram(tmpl, "DK5EN-2", "9", '!', "4700.00N/01300.00E-test", 0x7102);
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_kiss.size(), "esp32: position frame not queued");

    // 5. A factory-default source (RX-01) is not handed to the KISS client.
    //    The factory call is used ONLY as the source of an inbound frame
    //    under test, as in the RX-01 agreement test above.
    recorder_reset();
    bKISS = true;
    memset(tmpl, 0, sizeof(tmpl));
    len = build_gate_datagram(tmpl, "XX0XXX-00", "9", ':', "unconfigured", 0x7103);
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_kiss.size(), "esp32: unconfigured source queued");
}

static void test_agreement_max_zeros_returns_1_without_resetting_on_both(void)
{
    // DR-20 (2026-09-12 decided nrf52-correct on WHO decides, IMPLEMENTED
    // 2026-09-17 wave W6): both handlers now return a status -- 0 handled, 1
    // too many zeros -- instead of ESP32 returning void. ESP32's handler
    // used to call resetMeshComUDP() itself on this path; that call moved to
    // its caller, getMeshComUDP() (src/udp_functions.cpp, outside this
    // handler-only harness and therefore not observable here -- see that
    // function for the reset itself). The handler-level agreement pinned
    // here is exactly that NEITHER handler resets anything on its own any
    // more; WHICH reset the respective caller performs stays
    // platform-specific (ESP32 resets the UDP socket, nRF52 resets DHCP --
    // neither call site links into this native binary, only the handlers
    // do). g_reset_udp (the resetMeshComUDP() sink) is asserted at 0 on both
    // sides as a regression pin: if a handler ever calls it again directly,
    // this fails.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const int len = 16;   // even length: both loops read identical pairs
                          // (test_agreement_zero_scan_bound_matches_on_odd_length
                          // below covers the odd-length case)

    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        uint8_t buf[BUF_CAP];
        copy_into(buf, tmpl, len);
        int rc;
        if (side)
            rc = handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
        else
            rc = handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

        char msg[96];
        snprintf(msg, sizeof(msg), "%s must return 1 (too many zeros)", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, rc, msg);

        snprintf(msg, sizeof(msg), "%s must not reset anything itself any more -- DR-20 moved "
                                    "the reset to the caller", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_reset_udp, msg);
    }
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

// ADVISOR-BEFUND W6b (F4): buildExternAckJson() hatte NULL Abdeckung. Der
// einzige Aufrufer ist extudp_functions.cpp, das keine native Umgebung
// uebersetzt, und der Zwilling stubbt queueExternAck() -- jede Mutation am
// Serialisierer liess die Suite gruen. Der Bauer ist eine reine Funktion in
// einem Header, den diese Umgebung ohnehin einbindet, also wird er hier direkt
// geprueft: an seinen Raendern, nicht ueber seine Aufrufargumente.
static void test_extern_ack_json_is_valid_at_its_edges(void)
{
    char o[160];

    size_t n = buildExternAckJson(o, sizeof(o), 0x11223344u, 1, "DK5EN-90", "udp");
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "Normalfall wurde verworfen");
    TEST_ASSERT_NOT_NULL(strstr(o, "\"type\":\"ack\""));
    TEST_ASSERT_NOT_NULL(strstr(o, "\"msg_id\":\"11223344\""));
    TEST_ASSERT_NOT_NULL(strstr(o, "\"from\":\"DK5EN-90\""));
    TEST_ASSERT_NOT_NULL(strstr(o, "\"via\":\"udp\""));

    // Ein Anfuehrungszeichen aus dem Mesh darf das Dokument NICHT sprengen --
    // weder ueber from noch ueber via. Beide Felder fallen dann weg.
    n = buildExternAckJson(o, sizeof(o), 1, 1, "DK\"5EN", "udp");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL_MESSAGE(strstr(o, "DK\"5EN"), "Rufzeichen mit Quote eingebettet");
    n = buildExternAckJson(o, sizeof(o), 1, 1, "DK5EN-90", "u\"dp");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL_MESSAGE(strstr(o, "u\"dp"), "via mit Quote eingebettet");

    // Jede Ausgabe muss ausgewogene Anfuehrungszeichen haben: ein kaputtes
    // Dokument ist schlimmer als ein fehlendes Feld.
    static const char *bad_from[] = {"", "dk5en-90", "DK5EN-90-TOO-LONG", "\\"};
    for(size_t i = 0; i < sizeof(bad_from)/sizeof(bad_from[0]); i++)
    {
        n = buildExternAckJson(o, sizeof(o), 7, 2, bad_from[i], "udp");
        if(n == 0) continue;
        int q = 0;
        for(size_t k = 0; k < n; k++) if(o[k] == '"') q++;
        TEST_ASSERT_TRUE_MESSAGE((q % 2) == 0, "ungerade Zahl von Anfuehrungszeichen");
        TEST_ASSERT_EQUAL_CHAR('{', o[0]);
        TEST_ASSERT_EQUAL_CHAR('}', o[n-1]);
    }

    // Zu kleiner Zielpuffer: lieber gar nichts als ein halbes Dokument.
    char tiny[24];
    TEST_ASSERT_EQUAL_size_t(0, buildExternAckJson(tiny, sizeof(tiny), 1, 1, "DK5EN-90", "udp"));
}


// ===========================================================================
// MeshCom 5 (Konzept docs/meshcom5-topologie/ 4.11, Anhang E, Stufe 3):
// a frame injected from the server leaves the gateway with its destination
// path reset to the destination before checkVia() -- a via of the sender's
// region must not reach local LoRa, where nobody named in it relays. Checked
// on every copy the handler produces: the TX-ring entry (decoded back), the
// phone copies (addBLEOutBuffer) and the display (sendDisplayText()/
// sendDisplayPosition()). Both twins must also agree byte for byte.
// ===========================================================================

struct ViaResetOutcome
{
    std::vector<uint8_t> tx;          // TX-ring entry (frame bytes only)
    std::string tx_path;              // its decoded msg_destination_path
    std::vector<std::string> ble_paths;
    std::vector<std::vector<uint8_t>> ble;
    std::vector<std::string> display_paths;
};

static ViaResetOutcome run_via_reset_case(int side, char type, const char *dest_path,
                                          bool via_on, const char *node_via, uint32_t msg_id)
{
    recorder_reset();
    bVIA = via_on;
    if (node_via)
        snprintf(meshcom_settings.node_via, sizeof(meshcom_settings.node_via), "%s", node_via);

    const char *payload = (type == '!') ? "4812.34N/01123.45E#via-reset" : "via-reset";
    uint8_t buf[BUF_CAP];
    memset(buf, 0, sizeof(buf));
    uint16_t len = build_gate_datagram(buf, "DK5EN-92", dest_path, type, payload, msg_id);

    if (side)
        handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    else
        handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));

    ViaResetOutcome o;
    int used = 0;
    for (int i = 0; i < MAX_RING; i++)
    {
        if (ringBuffer[i][0] == 0)
            continue;
        used++;
        o.tx.assign(ringBuffer[i] + 2, ringBuffer[i] + 2 + ringBuffer[i][0]);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "%s '%c' %s: exactly one TX-ring entry expected",
             side ? "nrf52" : "esp32", type, dest_path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, used, msg);

    uint8_t dec[UDP_TX_BUF_SIZE + 5];
    memset(dec, 0, sizeof(dec));
    memcpy(dec, o.tx.data(), o.tx.size());
    struct aprsMessage m;
    initAPRS(m, type);
    snprintf(msg, sizeof(msg), "%s '%c' %s: TX-ring entry does not decode",
             side ? "nrf52" : "esp32", type, dest_path);
    TEST_ASSERT_TRUE_MESSAGE(decodeAPRS(dec, (uint16_t)o.tx.size(), m) != 0, msg);
    o.tx_path = m.msg_destination_path;

    for (const auto &b : g_ble)
    {
        o.ble.push_back(b);
        uint8_t bb[UDP_TX_BUF_SIZE + 5];
        memset(bb, 0, sizeof(bb));
        memcpy(bb, b.data(), std::min(b.size(), sizeof(bb)));
        struct aprsMessage bm;
        initAPRS(bm, type);
        if (decodeAPRS(bb, (uint16_t)b.size(), bm) != 0)
            o.ble_paths.push_back(bm.msg_destination_path);
    }
    o.display_paths = g_display_dest_path;
    return o;
}

static void check_via_reset(char type, const char *dest_path, bool via_on,
                            const char *node_via, const char *expect, uint32_t msg_id)
{
    ViaResetOutcome out[2];
    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        out[side] = run_via_reset_case(side, type, dest_path, via_on, node_via, msg_id);
        char msg[128];

        snprintf(msg, sizeof(msg), "%s '%c' %s bVIA=%d: TX-ring destination path",
                 name, type, dest_path, (int)via_on);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, out[side].tx_path.c_str(), msg);

        snprintf(msg, sizeof(msg), "%s '%c' %s: phone copy missing", name, type, dest_path);
        TEST_ASSERT_TRUE_MESSAGE(!out[side].ble_paths.empty(), msg);
        for (const auto &p : out[side].ble_paths)
        {
            snprintf(msg, sizeof(msg), "%s '%c' %s bVIA=%d: phone copy destination path",
                     name, type, dest_path, (int)via_on);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expect, p.c_str(), msg);
        }

        // The display sees the reset path: sendDisplayPosition() runs before
        // checkVia() (bare destination), sendDisplayText() after the first
        // checkVia() (with node_via, if set). Never the sender's via.
        const char *comma = strrchr(dest_path, ',');
        std::string bare = comma ? comma + 1 : dest_path;
        snprintf(msg, sizeof(msg), "%s '%c' %s: display not called", name, type, dest_path);
        TEST_ASSERT_TRUE_MESSAGE(!out[side].display_paths.empty(), msg);
        for (const auto &p : out[side].display_paths)
        {
            snprintf(msg, sizeof(msg), "%s '%c' %s bVIA=%d: display destination path '%s'",
                     name, type, dest_path, (int)via_on, p.c_str());
            TEST_ASSERT_TRUE_MESSAGE(p == bare || p == expect, msg);
        }
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "'%c' %s bVIA=%d: TX-ring bytes differ between twins",
             type, dest_path, (int)via_on);
    TEST_ASSERT_TRUE_MESSAGE(out[0].tx == out[1].tx, msg);
    snprintf(msg, sizeof(msg), "'%c' %s bVIA=%d: phone copies differ between twins",
             type, dest_path, (int)via_on);
    TEST_ASSERT_TRUE_MESSAGE(out[0].ble == out[1].ble, msg);
    // Display paths agree only without an own node_via. With one, a known
    // drift shows: the ESP32 handler calls sendDisplayPosition() before its
    // checkVia(), the nRF52 handler after it, so the ESP32 display sees "9"
    // and the nRF52 display "DK5EN-90,9". Both are free of the sender's via
    // (checked per side above); the order itself is not this fix's subject.
    if (!via_on)
    {
        snprintf(msg, sizeof(msg), "'%c' %s bVIA=%d: display paths differ between twins",
                 type, dest_path, (int)via_on);
        TEST_ASSERT_TRUE_MESSAGE(out[0].display_paths == out[1].display_paths, msg);
    }
}

static void test_regression_server_via_is_reset_before_checkvia_on_both(void)
{
    // foreign via from another region -> only the destination remains
    check_via_reset(':', "DB0XYZ-12,9", false, nullptr, "9", 0x5101);
    check_via_reset('!', "DB0XYZ-12,9", false, nullptr, "9", 0x5102);
    // own node_via still applies -- on the reset path, not appended to the foreign one
    check_via_reset(':', "DB0XYZ-12,9", true, "DK5EN-90", "DK5EN-90,9", 0x5103);
    check_via_reset('!', "DB0XYZ-12,9", true, "DK5EN-90", "DK5EN-90,9", 0x5104);
    // a frame already at its destination stays there (no "9,9")
    check_via_reset(':', "9", false, nullptr, "9", 0x5105);
    check_via_reset('!', "9", false, nullptr, "9", 0x5106);
}

int main(int, char **argv)
{
    g_argv0 = argv[0] ? argv[0] : "";

    UNITY_BEGIN();

    RUN_TEST(test_agreement_gate_text_message_decodes_and_relays_on_both);
    RUN_TEST(test_agreement_dedup_blocks_repeat_relay_on_both);
    RUN_TEST(test_agreement_extudp_forward_ahead_of_dedup_gate_on_both);
    RUN_TEST(test_regression_dm_dedup_reacks_without_redisplay_on_both);
    RUN_TEST(test_agreement_max_zeros_rejected_by_both);
    RUN_TEST(test_agreement_indicator_dispatch_prints_matching_gw_rx_type_lines);
    RUN_TEST(test_agreement_live_traffic_clears_the_heartbeat_warn_latch);
    RUN_TEST(test_agreement_conf_updates_node_call_and_short_identically);
    RUN_TEST(test_agreement_zero_scan_bound_matches_on_odd_length);
    RUN_TEST(test_regression_zero_scan_oob_byte_flips_verdict_before_fix);

    // DR-01/DR-02/DR-04/DR-05/DR-06/DR-07/DR-08/DR-09/DR-18/DR-19 (all
    // 2026-09-12 decided, implemented this wave): former DRIFT cases,
    // converted to AGREEMENT now that both platforms behave the same.
    RUN_TEST(test_agreement_extudp_requires_hasExternIPaddress_on_both);
    RUN_TEST(test_agreement_extudp_forwards_one_decodable_frame_per_type_and_rejects_unrecognised);
    RUN_TEST(test_agreement_extudp_length_arg_matches_after_cast_removed);
    RUN_TEST(test_agreement_dedup_gate_position_and_gateway_nopos_on_both);
    RUN_TEST(test_agreement_senddisplayposition_on_both);
    RUN_TEST(test_agreement_rx01_unconfigured_guard_on_both);
    RUN_TEST(test_agreement_decodeaprs_reject_suppresses_processing_on_both);
    RUN_TEST(test_agreement_conf_zero_address_guard_on_both);
    RUN_TEST(test_agreement_ack_phone_frame_attribution_on_both);
    RUN_TEST(test_agreement_extudp_ack_json_mirrors_ble_ack_on_both);
    RUN_TEST(test_extern_ack_json_is_valid_at_its_edges);
    RUN_TEST(test_agreement_max_zeros_returns_1_without_resetting_on_both);

    RUN_TEST(test_drift_kiss_server_relay_tap_is_esp32_only);

    RUN_TEST(test_regression_server_via_is_reset_before_checkvia_on_both);

    RUN_TEST(test_u1_corpus_ordered_sink_dump_both_platforms);

    return UNITY_END();
}
