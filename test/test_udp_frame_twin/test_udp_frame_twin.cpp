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

void logRxDropUnconfigured(const char *call)
{
    g_dropped.push_back(call ? call : "(null)");
}

void printBuffer_aprs(char *msg_source, struct aprsMessage &aprsMessage, const char *tail)
{
    PrintedAprs p;
    p.source = aprsMessage.msg_source_call.c_str();
    p.tail = tail ? tail : "";
    (void)msg_source;
    g_printed.push_back(p);
}

void addBLEOutBuffer(uint8_t *buffer, uint16_t len)
{
    g_ble.emplace_back(buffer, buffer + len);
}

void resetMeshComUDP() { g_reset_udp++; }

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
bool save_settings(void) { g_save_settings_calls++; return true; }

void sendDisplayText(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr)
{
    (void)aprsmsg; (void)rssi; (void)snr;
    g_sendDisplayText_calls++;
}

void sendDisplayPosition(struct aprsMessage &aprsmsg, int16_t rssi, int8_t snr)
{
    (void)aprsmsg; (void)rssi; (void)snr;
    g_sendDisplayPosition_calls++;
}

void SendAckMessage(String dest_call, unsigned int iAckId)
{
    g_ack_dest.push_back(dest_call.c_str());
    g_ack_ids.push_back(iAckId);
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
    // set of byte pairs (no odd-length over-read ambiguity, see D1), so
    // zerocount is identical (16) on both and both exceed MAX_ZEROS (6).
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

// ===========================================================================
// DRIFT: differences that exist today and must not change silently
// ===========================================================================

static void test_drift_zero_scan_bound_nrf52_overreads_by_one_byte(void)
{
    // ESP32: `for (i = 0; i + 1 < packetSize; i += 2)` -- with packetSize=7
    // reads pairs (0,1)(2,3)(4,5), never touching index 6 or beyond.
    // nRF52:  `for (i = 0; i < packetSize; i += 2)`     -- reads a FOURTH
    // pair (6,7), where byte 7 is one past the 7-byte logical datagram.
    // Fixed on ESP32 by commit cd88ae12 (SEC-05/SEC-06/BUG-12); never
    // applied to nRF52 (src/udp_frame.h, src/nrf52/udp_frame_nrf52.cpp:53).
    //
    // A fully-zeroed buffer with headroom makes this observable WITHOUT an
    // actual out-of-bounds access: the extra byte nRF52 reads is real,
    // allocated memory (the fixed-size UDP receive buffer on hardware is
    // exactly this shape -- fixed capacity, a shorter logical packetSize),
    // it is simply outside the 7 bytes this datagram claims to be. Seven
    // zero pairs would be needed to trip MAX_ZEROS (6) on the pair count
    // alone; ESP32 reads 3 (zerocount 6, accepts), nRF52 reads 4 (zerocount
    // 8, rejects) -- the SAME bytes, a different verdict, purely from the
    // loop bound.
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));   // whole buffer zero, including the tail
    const int len = 7;

    recorder_reset();
    uint8_t buf[BUF_CAP];
    copy_into(buf, tmpl, len);
    handleUdpFrame_esp32(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_reset_udp,
                                  "esp32 (3 pairs, zerocount 6) must still accept the frame");

    recorder_reset();
    copy_into(buf, tmpl, len);
    int rc = handleUdpFrame_nrf52(buf, len, IPAddress(1, 2, 3, 4));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, rc,
                                  "nrf52 (4 pairs via the one-byte over-read, zerocount 8) "
                                  "must reject -- if this became 0, the over-read was fixed "
                                  "and src/udp_frame.h's drift list is stale");
}

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

static void test_drift_max_zeros_return_value_and_reset_call_differ(void)
{
    // ESP32 returns void and calls resetMeshComUDP() itself; nRF52 returns 1
    // and calls nothing -- its caller resets DHCP instead (src/udp_frame.h,
    // src/nrf52/nrf_eth.h's own doc comment on handleUdpFrame_nrf52()).
    uint8_t tmpl[BUF_CAP];
    memset(tmpl, 0, sizeof(tmpl));
    const int len = 16;   // even length: both loops read identical pairs (see D1)

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

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_agreement_gate_text_message_decodes_and_relays_on_both);
    RUN_TEST(test_agreement_dedup_blocks_repeat_relay_on_both);
    RUN_TEST(test_agreement_max_zeros_rejected_by_both);
    RUN_TEST(test_agreement_indicator_dispatch_prints_matching_gw_rx_type_lines);
    RUN_TEST(test_agreement_conf_updates_node_call_and_short_identically);

    RUN_TEST(test_drift_zero_scan_bound_nrf52_overreads_by_one_byte);
    RUN_TEST(test_drift_extudp_requires_hasExternIPaddress_on_esp32_only);
    RUN_TEST(test_drift_extudp_gate_before_vs_after_msgtype_check);
    RUN_TEST(test_drift_extudp_length_arg_type_differs_but_is_unobservable_at_the_clamp);
    RUN_TEST(test_drift_dedup_gate_position_esp32_honors_gateway_nopos_nrf52_does_not);
    RUN_TEST(test_drift_senddisplayposition_esp32_only);
    RUN_TEST(test_drift_rx01_unconfigured_guard_esp32_only);
    RUN_TEST(test_drift_max_zeros_return_value_and_reset_call_differ);

    return UNITY_END();
}
