// Twin test for the UDP-out ring drain (test plan U2 / section 4.4, audit
// rows D1-05/D1-06). "ring with N entries drains to identical datagrams and
// order on both sides; pointer movement per pass".
//
// Unlike the U6 country twin, this is ONE binary, not one per side: the two
// drains are named differently (sendMeshComUDP on ESP32, sendUDP on nRF52),
// so both translation units link together and share one udpOutRing
// (byte_fifo_t, src/byte_fifo.h). The same ring is therefore drained by both
// implementations in the same process, and the comparison is a real
// differential rather than two runs compared through files.
//
//   pio test -e native_udp_send_twin -f test_udp_send_twin
//
// This test exists because C2 alone did not make the drain testable. C2 made
// the socket write replaceable, which is what its plan row asked for, but
// sendMeshComUDP() still lived in udp_functions.cpp (esp_task_wdt.h,
// web_functions.h, ArduinoJson) and sendUDP() in nrf52_main.cpp (SPI, the
// WisBlock API). Neither compiled on a host. The U2 carve moved both bodies
// out unchanged; this is the first thing that could not have been written
// before it.
//
// Only two headers are shadowed (stubs/udp_functions.h, stubs/nrf_eth.h) plus
// three empty shims. loop_functions.h and loop_functions_extern.h are the
// REAL headers -- they already compile natively, as env:native_aprs has been
// proving for a while -- so the ring declarations, the display flags and the
// settings struct in this test are the shipped ones, not a paraphrase. The
// two that are shadowed are checked declaration-by-declaration against their
// originals by test/golden/twin_stub_lint.py: a stub whose types have drifted
// from the header under test passes, and passes about something that is not
// the shipped declaration.
//
// WHAT IS PINNED AS AGREEMENT and what is pinned as DRIFT is the point. The
// two sides are NOT expected to agree everywhere -- which of them is right is
// a drift-matrix decision, not this test's. What this test does is make every
// difference explicit and failing-on-change, so that a unification wave that
// alters one of them has to say so.

#include "../../src/mc_text.h"
#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <Arduino.h>
#include <IPAddress.h>
#include <configuration.h>
#include <aprs_functions.h>

#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <udp_functions.h>   // stub: the C2 primitives, defined below
#include <nrf_eth.h>         // stub: NrfETH, defined below
#include <udp_drain.h>

// ---------------------------------------------------------------------------
// The state the two drains read. On hardware these live in udp_functions.cpp
// (ESP32) and nrf52_main.cpp / NrfETH (nRF52).
// ---------------------------------------------------------------------------

bool bDisplayInfo = false;
bool bDisplayVia = false;
bool bDisplayCont = false;
bool bWIFIAP = false;
// RAM-Rueckgewinn (neo-ram-reclaim): the ring is now a byte_fifo_t
// (src/byte_fifo.h), declared extern in src/loop_functions_extern.h and
// defined in src/loop_functions.cpp on hardware. This test does not compile
// that TU, so it supplies the one definition here instead -- storage sized
// from RING_BYTES_UDP (src/configuration_global.h), the same constant the
// shipped ring uses for this memory class.
static uint8_t udpOutStore[RING_BYTES_UDP];
byte_fifo_t udpOutRing = BYTE_FIFO_INIT(udpOutStore);

bool hasIPaddress = true;
IPAddress node_hostip(44, 143, 8, 143);
bool udp_is_busy = false;
uint8_t err_cnt_udp_tx = 0;
uint8_t convBuffer[UDP_TX_BUF_SIZE + 50];
bool bUDPLOG = false;

s_meshcom_settings meshcom_settings;
NrfETH neth;

String getTimeString() { return String("00:00:00"); }

// aprs_functions.cpp's own externs. Same values and the same int-vs-uint8_t
// ODR reasoning as test_aprs_decode.cpp, so the decoder behaves here the way
// it does in the APRS suites.
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631, the bench board
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// ---------------------------------------------------------------------------
// The recording sink. Both sides are recorded at the same boundary: the bytes
// handed to the socket layer. On ESP32 that is the C2 primitives directly; on
// nRF52 NrfETH::sendUDP() is itself a thin wrapper over the nRF52 C2
// primitives (nrf_eth.cpp), so recording it records the same thing one call
// earlier.
// ---------------------------------------------------------------------------

struct Datagram
{
    std::vector<uint8_t> bytes;
    bool ended;          // endPacket() was reached for this one
};

static std::vector<Datagram> g_sent;
static std::vector<std::string> g_printed;   // "<prefix>|<source call>"

// ---------------------------------------------------------------------------
// Ordered sink-call log (test plan 4.4/9.1): every stub below appends one
// line here, IN CALL ORDER, alongside (not instead of) the vectors above --
// the existing named tests assert on those and stay unchanged. This is what
// the corpus dump at the bottom of the file writes out, one file per
// platform. Format: "NNN\tSINK\tdetail", NNN a 3-digit index restarting at
// 001 per corpus item (see log_reset()).
// ---------------------------------------------------------------------------
static std::vector<std::string> g_log;
static int g_log_seq = 0;

static void log_reset()
{
    g_log.clear();
    g_log_seq = 0;
}

static void log_sink(const char *token, const std::string &detail)
{
    g_log_seq++;
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%03d\t", g_log_seq);
    g_log.push_back(std::string(prefix) + token + "\t" + detail);
}

static std::string hex_render(const uint8_t *buf, size_t len)
{
    std::string s;
    s.reserve(len * 3);
    char b[4];
    for (size_t i = 0; i < len; i++)
    {
        if (i) s += ' ';
        snprintf(b, sizeof(b), "%02x", buf[i]);
        s += b;
    }
    return s;
}
// What decodeAPRS() actually made of the frame: payload and decoded length.
// Kept apart from g_printed because the payload sits at the END of the frame
// and is therefore the only field that notices a wrong aprs_len -- the source
// call is near the front and survives a 36-byte overrun unharmed.
static std::vector<std::string> g_payload;
static std::vector<int> g_decoded_len;
static std::vector<std::string> g_dropped;   // RX-01 logRxDropUnconfigured()
// DR-25: the outbound drain's OWN leak counter/marker, kept in a SEPARATE
// vector from g_dropped above so a test can assert the two never mix.
static std::vector<std::string> g_leaked;    // logTxLeakUnconfigured()
static int g_reset_udp = 0;                  // ESP32 resetMeshComUDP()
static int g_reset_dhcp = 0;                 // nRF52 resetDHCP()
static int g_count_tx_ok = 0, g_count_tx_fail = 0;

// When set, the next write fails. Models a refused/short socket write; on
// ESP32 this ALSO fails the following udpEndRaw_esp32() (kept coupled: it is
// what the other, still-valid drift/agreement cases in this file mean by "a
// failed write"). On nRF52, NrfETH::sendUDP() is one call, so this alone
// models its whole send failing.
static bool g_write_fails = false;
// DR-24: fails ONLY udpEndRaw_esp32() (ESP32's real send result,
// endPacket()) while udpWriteRaw_esp32() still succeeds -- the realistic
// hardware case (WiFiUDP::write() only buffers and cannot fail for a
// non-empty frame). Has no nRF52 counterpart: NrfETH::sendUDP() wraps both
// and is driven by g_write_fails alone.
static bool g_end_fails = false;
// When set, the sink models a writer racing ahead of this read by pushing
// filler frames into udpOutRing until the frame being sent is evicted -- the
// byte_fifo_t equivalent of the ring-full eviction path bf_push2() runs
// internally (src/byte_fifo.cpp), which is what the CONC-16 guard defends
// against.
static bool g_evict_during_send = false;

static void recorder_reset()
{
    g_sent.clear();
    g_printed.clear();
    g_payload.clear();
    g_decoded_len.clear();
    g_dropped.clear();
    g_leaked.clear();
    log_reset();
    g_reset_udp = g_reset_dhcp = 0;
    g_count_tx_ok = g_count_tx_fail = 0;
    g_write_fails = false;
    g_end_fails = false;
    g_evict_during_send = false;
    err_cnt_udp_tx = 0;
    udp_is_busy = false;
    neth.udp_is_busy = false;
    hasIPaddress = true;
    neth.hasIPaddress = true;
    bWIFIAP = false;
    node_hostip = IPAddress(44, 143, 8, 143);
    // DR-21: resolved by default, mirroring node_hostip above, so every
    // OTHER test in this file (none of which touches this field) is
    // unaffected; test_agreement_both_refuse_an_unresolved_destination()
    // below overrides it.
    neth.udp_dest_addr = IPAddress(44, 143, 8, 143);
    meshcom_settings.node_hasIPaddress = true;
}

static void sink_write(const uint8_t *buf, uint16_t len)
{
    Datagram d;
    d.bytes.assign(buf, buf + len);
    d.ended = false;
    g_sent.push_back(d);
    log_sink("UDPWRITE", hex_render(buf, len));
    if (g_evict_during_send)
    {
        // CONC-16 guard test: the drain has already peeked the oldest
        // unread frame (still sitting at tail, not yet popped) and captured
        // its tail_gen. Push same-sized filler frames -- like the real
        // ~80-byte traffic byte_fifo.h measured -- until that frame is
        // evicted, observable as tail_gen ticking away from what it was
        // when this call started. RING_BYTES_UDP=2048 against ~80-byte
        // frames is about 26 pushes; the loop derives the count from the
        // ring's own state rather than hardcoding it.
        uint16_t gen_before = bf_tail_gen(&udpOutRing);
        uint8_t filler[80];
        memset(filler, 0xEE, sizeof(filler));
        int guard = 0;
        while (bf_tail_gen(&udpOutRing) == gen_before && guard++ < RING_BYTES_UDP)
            bf_push(&udpOutRing, filler, sizeof(filler));
    }
}

// --- ESP32 side: the C2 primitives ---------------------------------------
bool udpBeginRaw_esp32() { log_sink("UDPBEGIN", ""); return true; }

bool udpWriteRaw_esp32(const uint8_t *buf, uint16_t len)
{
    sink_write(buf, len);
    return !g_write_fails;
}

bool udpEndRaw_esp32()
{
    if (!g_sent.empty())
        g_sent.back().ended = true;
    // DR-24: this is the real send result on ESP32 -- fails if EITHER the
    // joint write/end flag or the end-only flag is set.
    bool ok = !(g_write_fails || g_end_fails);
    log_sink("UDPEND", ok ? "ok" : "fail");
    return ok;
}

void udpCountTx(bool ok)
{
    ok ? g_count_tx_ok++ : g_count_tx_fail++;
    log_sink("TXCOUNT", ok ? "ok" : "fail");
}

void resetMeshComUDP() { g_reset_udp++; log_sink("RESET", "udp"); }

void logRxDropUnconfigured(const char *call)
{
    std::string c = call ? call : "(null)";
    g_dropped.push_back(c);
    log_sink("DROP", c);
}

// DR-25: the outbound leak logger, shared by both platforms' drains -- kept
// in its own vector (g_leaked) so a test can assert it never overlaps with
// g_dropped (the RX drop counter's stub above).
void logTxLeakUnconfigured(const char *call)
{
    std::string c = call ? call : "(null)";
    g_leaked.push_back(c);
    log_sink("LEAK", c);
}

// --- nRF52 side: NrfETH ---------------------------------------------------
bool NrfETH::sendUDP(uint8_t buffer[UDP_TX_BUF_SIZE], uint16_t rx_buf_size)
{
    sink_write(buffer, rx_buf_size);
    if (!g_write_fails)
        g_sent.back().ended = true;
    // NrfETH::sendUDP() wraps write+endPacket in one hardware call; logged
    // as its own UDPEND so both platforms' logs carry the same tokens, even
    // though ESP32 reaches the ring via two stub calls and nRF52 via one.
    log_sink("UDPEND", g_write_fails ? "fail" : "ok");
    return !g_write_fails;
}

int NrfETH::resetDHCP()
{
    g_reset_dhcp++;
    log_sink("RESET", "dhcp");
    return 0;
}

// --- shared ---------------------------------------------------------------
void printBuffer_aprs(char *msg_source, struct aprsMessage &aprsMessage,
                      const char *tail)
{
    (void)tail;
    std::string s(msg_source ? msg_source : "");
    s += "|";
    s += aprsMessage.msg_source_call;
    g_printed.push_back(s);
    g_payload.push_back(aprsMessage.msg_payload);
    g_decoded_len.push_back((int)aprsMessage.msg_len);
    log_sink("PRINT", s + "|" + aprsMessage.msg_payload);
}

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

// A slot is: byte 0 = msg_len, then msg_len bytes starting at offset 1 -- a
// 36-byte UDP DATA header followed by the APRS frame. Built the way
// addUdpOutBuffer() builds it (udp_functions.cpp), so the length arithmetic
// the drain does (aprs_len = msg_len - 36) is exercised against the real
// shape rather than a convenient one.
static const int UDP_HDR = 36;

static uint16_t build_frame(uint8_t *out, const char *src, const char *text)
{
    // Field set taken from test_aprs_spec.cpp's encoder vector: encodeAPRS()
    // reads msg_source_path / msg_destination_path, not the _call fields --
    // decodeAPRS() derives the calls from the paths on the way back.
    // Destination 9 is the bench group; never "*" (2026-09-11 incident).
    struct aprsMessage m;
    initAPRS(m, ':');
    m.msg_id = 0x4711;
    m.max_hop = 3;
    m.msg_server = true;
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), src);
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "9");   // Bench-Gruppe 9, nie "*"
    mcSet(m.msg_payload, sizeof(m.msg_payload), text);
    m.msg_source_hw = 9;
    m.msg_source_mod = 0x88;
    m.msg_source_fw_version = 35;
    m.msg_last_hw = 0x80 | 9;
    m.msg_source_fw_sub_version = 'p';
    return encodeAPRS(out, m);
}

// Builds the on-wire payload for one frame into `out` -- the recognisable
// 36-byte header (0xC0..), then the APRS frame -- and returns its length
// (msg_len = UDP_HDR + frame length). Shared by put_slot() (which also
// pushes it onto udpOutRing) and any test that needs the exact bytes a
// drain should send, to compare against what it actually sent.
static uint16_t build_payload(uint8_t *out, const char *src, const char *text)
{
    uint8_t frame[UDP_TX_BUF_SIZE];
    memset(frame, 0, sizeof(frame));
    uint16_t flen = build_frame(frame, src, text);

    // a recognisable header, so a side that sent the wrong offset is obvious
    for (int i = 0; i < UDP_HDR; i++)
        out[i] = (uint8_t)(0xC0 + i);
    memcpy(out + UDP_HDR, frame, flen);
    return (uint16_t)(UDP_HDR + flen);
}

// Builds one frame and pushes it onto udpOutRing. Returns the msg_len a
// drain will read back via bf_peek().
static uint16_t put_slot(const char *src, const char *text)
{
    uint8_t payload[UDP_HDR + UDP_TX_BUF_SIZE];
    uint16_t msg_len = build_payload(payload, src, text);
    TEST_ASSERT_TRUE_MESSAGE(bf_push(&udpOutRing, payload, (uint8_t)msg_len) >= 0,
                             "put_slot: bf_push rejected the frame");
    return msg_len;
}

// Resets udpOutRing and pushes `n` frames onto it ("slot 0".."slot n-1").
// Returns the msg_len of the last frame pushed (0 if n == 0).
static uint16_t fill_ring(int n, const char *src = "DK5EN-1")
{
    bf_reset(&udpOutRing);
    uint16_t last_len = 0;
    for (int i = 0; i < n; i++)
    {
        char text[32];
        snprintf(text, sizeof(text), "slot %d", i);
        last_len = put_slot(src, text);
    }
    return last_len;
}

// Drains until the ring is empty or `max_passes` is reached, so a drain that
// fails to advance cannot hang the suite. Default bound is derived from the
// ring's own frame count at the call site (there is no fixed slot count to
// multiply by any more), plus a small margin.
typedef void (*Drain)(void);
static int drain_all(Drain fn, int max_passes = -1)
{
    if (max_passes < 0)
        max_passes = (int)bf_frames(&udpOutRing) + 4;
    int passes = 0;
    while (!bf_empty(&udpOutRing) && passes < max_passes)
    {
        fn();
        passes++;
    }
    return passes;
}

static std::vector<Datagram> run_side(Drain fn, int n, const char *src = "DK5EN-1")
{
    recorder_reset();
    fill_ring(n, src);
    drain_all(fn);
    return g_sent;
}

void setUp(void) { recorder_reset(); }
void tearDown(void) {}

// ---------------------------------------------------------------------------
// AGREEMENT: what both sides must keep doing identically
// ---------------------------------------------------------------------------

static void test_same_ring_drains_to_the_same_datagrams(void)
{
    const int N = 7;
    std::vector<Datagram> esp = run_side(sendMeshComUDP, N);
    std::vector<Datagram> nrf = run_side(sendUDP, N);

    TEST_ASSERT_EQUAL_INT_MESSAGE(N, (int)esp.size(), "ESP32 datagram count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(N, (int)nrf.size(), "nRF52 datagram count");

    for (int i = 0; i < N; i++)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "datagram %d length", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)esp[i].bytes.size(),
                                      (int)nrf[i].bytes.size(), msg);
        snprintf(msg, sizeof(msg), "datagram %d bytes", i);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(esp[i].bytes.data(), nrf[i].bytes.data(),
                                         esp[i].bytes.size(), msg);
    }
}

static void test_datagram_is_the_slot_from_offset_one(void)
{
    // The wire bytes are exactly the payload build_payload() built -- header
    // included. Pinned because "off by the 36-byte header" is exactly the
    // bug class the CONC-16 commit found in the *other* copy of this
    // arithmetic (the convBuffer copy below).
    recorder_reset();
    bf_reset(&udpOutRing);
    uint8_t expected[UDP_HDR + UDP_TX_BUF_SIZE];
    uint16_t msg_len = build_payload(expected, "DK5EN-1", "hello");
    TEST_ASSERT_TRUE(bf_push(&udpOutRing, expected, (uint8_t)msg_len) >= 0);

    sendMeshComUDP();

    TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
    TEST_ASSERT_EQUAL_INT(msg_len, (int)g_sent[0].bytes.size());
    TEST_ASSERT_EQUAL_MEMORY(expected, g_sent[0].bytes.data(), msg_len);
}

static void test_pointer_moves_one_slot_per_pass_and_wraps(void)
{
    // Byte-ring equivalent of the old slot-index wrap: there is no fixed
    // slot count to wrap around any more (that was purely an artefact of
    // the array-of-slots representation), so what is pinned instead is "N
    // pushed frames drain one per pass, in order, until the ring is empty".
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        fill_ring(4);

        char msg[64];
        fn();
        snprintf(msg, sizeof(msg), "%s: three frames left after the first pass", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)bf_unread(&udpOutRing), msg);
        fn();
        snprintf(msg, sizeof(msg), "%s: two frames left after the second pass", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing), msg);
        int passes = drain_all(fn);
        snprintf(msg, sizeof(msg), "%s: ring drained", name);
        TEST_ASSERT_TRUE_MESSAGE(bf_empty(&udpOutRing), msg);
        snprintf(msg, sizeof(msg), "%s: the two remaining frames took exactly two more passes", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, passes, msg);
    }
}

static void test_sent_slot_is_zeroed(void)
{
    // R1-03 predecessor: a slot ring zeroed the consumed slot after sending
    // it. byte_fifo_t has no per-slot storage to zero -- popping a frame
    // only moves tail past it (it stays on as history until the space is
    // needed, byte_fifo.h). What is pinned here instead is that the pop
    // actually happened: bf_unread() drops to 0 after the one frame in the
    // ring is sent.
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        recorder_reset();
        fill_ring(1);
        fn();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, bf_unread(&udpOutRing),
                                         side ? "nrf52 did not pop the sent frame"
                                              : "esp32 did not pop the sent frame");
    }
}

static void test_empty_ring_sends_nothing(void)
{
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        fill_ring(0);
        TEST_ASSERT_TRUE(bf_empty(&udpOutRing));
        (side ? sendUDP : sendMeshComUDP)();
        TEST_ASSERT_EQUAL_INT(0, (int)g_sent.size());
        TEST_ASSERT_TRUE(bf_empty(&udpOutRing));
    }
}

static void test_busy_flag_holds_the_slot_on_both_sides(void)
{
    recorder_reset();
    fill_ring(2);
    udp_is_busy = true;
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(), "esp32 sent while busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing), "esp32 advanced while busy");

    recorder_reset();
    fill_ring(2);
    neth.udp_is_busy = true;
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(), "nrf52 sent while busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing), "nrf52 advanced while busy");
}

static void test_mid_send_eviction_does_not_double_advance(void)
{
    // CONC-16 guard: a writer racing ahead through byte_fifo's own eviction
    // path (bf_push2, src/byte_fifo.cpp) must not be followed by an extra
    // pop from a reader that was already mid-send when it happened.
    bDisplayInfo = true;
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        bDisplayInfo = true;
        fill_ring(4);   // "slot 0".."slot 3"; slot 0 is the one about to send

        g_evict_during_send = true;
        fn();
        g_evict_during_send = false;

        char msg[112];
        // frames == unread confirms nothing has actually been popped: every
        // frame left in the ring, filler and real alike, is still unread --
        // an erroneous double-pop here would leave frames > unread (a
        // popped frame stays on as history and no longer counts as unread,
        // byte_fifo.h).
        snprintf(msg, sizeof(msg), "%s: guard popped on top of the eviction", name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(bf_frames(&udpOutRing), bf_unread(&udpOutRing), msg);
        snprintf(msg, sizeof(msg), "%s: decoded slot 0 before the eviction caught it", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("slot 0", g_payload.back().c_str(), msg);

        // The eviction took slot 0 -- already sent above, from the snapshot
        // the drain took before the eviction ran -- not slot 1 as well. The
        // next drain pass must reach slot 1, never slot 2.
        fn();
        snprintf(msg, sizeof(msg),
                "%s: next frame after the eviction is not slot 1 -- double-advanced", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("slot 1", g_payload.back().c_str(), msg);
    }
    bDisplayInfo = false;
}

// ---------------------------------------------------------------------------
// DRIFT: differences that exist today and must not change silently
// ---------------------------------------------------------------------------

static void test_drift_esp32_has_three_preconditions_nrf52_has_none(void)
{
    // ESP32 refuses to drain in AP mode, without an IP, and before the
    // server address resolves. nRF52 enters the drain regardless and lets
    // NrfETH::sendUDP() fail; there is no equivalent early return.
    const char *why[3] = {"bWIFIAP", "!hasIPaddress", "node_hostip == 0"};
    for (int c = 0; c < 3; c++)
    {
        recorder_reset();
        fill_ring(2);
        if (c == 0) bWIFIAP = true;
        if (c == 1) hasIPaddress = false;
        if (c == 2) node_hostip = IPAddress(0, 0, 0, 0);
        sendMeshComUDP();
        char msg[80];
        snprintf(msg, sizeof(msg), "esp32 drained despite %s", why[c]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing), msg);
    }

    // nRF52 under all three: still drains. Those globals are not even in
    // its scope; the point is that the guard has no counterpart.
    recorder_reset();
    fill_ring(2);
    bWIFIAP = true;
    hasIPaddress = false;
    node_hostip = IPAddress(0, 0, 0, 0);
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_sent.size(),
                                  "nrf52 grew a precondition -- drift row D1-05 "
                                  "changed, update the matrix");
}

static void test_agreement_both_refuse_an_unresolved_destination(void)
{
    // DR-21, RE-DECIDED 2026-09-12 (fable Findings 1/2/12; operator: narrow
    // to parity), IMPLEMENTED: ESP32 refuses to drain when the resolved
    // gateway server address is still 0.0.0.0 (udp_drain_esp32.cpp,
    // node_hostip == 0) -- one of its three preconditions (the other two
    // are covered by test_drift_esp32_has_three_preconditions_nrf52_has_
    // none() above and stay drift, per the narrowed decision). nRF52 now has
    // its OWN equivalent early return on ITS destination-address concept
    // (neth.udp_dest_addr -- already read by udp_frame_nrf52.cpp's CONF
    // guard, DR-08), and ONLY that one: hasIPaddress is already the sole
    // caller's gate (gateway_service_nrf52.cpp:28-34) and nRF52 has no AP
    // mode, so porting the other two would be vacuous or wrong.
    recorder_reset();
    fill_ring(2);
    node_hostip = IPAddress(0, 0, 0, 0);
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(),
                                  "esp32 drained despite an unresolved gateway server address");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing),
                                  "esp32 advanced the ring despite an unresolved gateway server address");

    recorder_reset();
    fill_ring(2);
    neth.udp_dest_addr = IPAddress(0, 0, 0, 0);
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(),
                                  "nrf52 drained despite an unresolved destination address -- "
                                  "DR-21's early return regressed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)bf_unread(&udpOutRing),
                                  "nrf52 advanced the ring despite an unresolved destination address");
}

static void test_agreement_decode_and_print_survive_a_failed_send(void)
{
    // DR-22, DECIDED 2026-09-12 esp32-correct, IMPLEMENTED: both drains now
    // decode and print regardless of the send result -- ESP32 always did;
    // nRF52's decode/print moved out of the success-only branch. A failed
    // send is still worth knowing WHICH frame it was, not just how many (see
    // DR-24 below: the frame is dropped either way, no retry).
    bDisplayInfo = true;

    recorder_reset();
    fill_ring(1);
    g_write_fails = true;
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(),
                                  "esp32 stopped printing on a failed write");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_count_tx_fail,
                                  "esp32 lost its udpCountTx instrument");

    recorder_reset();
    fill_ring(1);
    g_write_fails = true;
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(),
                                  "nrf52 stopped printing on a failed write -- DR-22 regressed");

    bDisplayInfo = false;
}

static void test_drift_esp32_calls_endpacket_after_a_failed_write(void)
{
    // DR-23: both-valid, no action -- stays a drift pin, not an agreement
    // case. ESP32 runs udpEndRaw_esp32() unconditionally now (DR-24 removed
    // the one early return that used to skip it at the error limit).
    // nRF52's failure path is inside NrfETH::sendUDP(), which has already
    // ended the packet itself (nrf_eth.cpp:350-352, verified at the review)
    // -- a layering difference, not a functional gap. Kept as a warning: a
    // future refactor of either layer could drop the call on one side and
    // nothing would fail loudly.
    recorder_reset();
    fill_ring(1);
    g_write_fails = true;
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
    TEST_ASSERT_TRUE_MESSAGE(g_sent[0].ended,
                             "esp32 skipped endPacket after a failed write");
}

static void test_agreement_error_limit_drops_the_slot_on_both_sides(void)
{
    // DR-24, RE-DECIDED 2026-09-12 (fable Finding 1; nrf52-correct,
    // esp32-changes), IMPLEMENTED. The row's original ESP32 failure mode did
    // not exist: WiFiUDP::write() only buffers and cannot fail for a
    // non-empty frame, so keying the error-limit branch on
    // udpWriteRaw_esp32()'s result meant a real failure was never seen at
    // all. ESP32 now keys on udpEndRaw_esp32()'s result (the real send
    // result, endPacket()) and no longer returns early before the
    // advance/zero block -- same shape as nRF52: reset the link on the Nth
    // consecutive failure, log/count it, and ALWAYS drop the failing slot.
    // No retry on either side, so neither platform can wedge on one
    // permanently-failing frame.
    //
    // The two platforms fail through different knobs: ESP32's write always
    // succeeds here and only endPacket (g_end_fails) fails, matching what
    // can actually happen on hardware; nRF52's NrfETH::sendUDP() is one call
    // (g_write_fails fails the whole send), matching what actually can
    // happen there.
    // No fixed slot count to derive N from any more -- a generous margin
    // above MAX_ERR_UDP_TX that still comfortably fits RING_BYTES_UDP at the
    // ~80-byte frame size these frames build to (see byte_fifo.h).
    const int N = MAX_ERR_UDP_TX + 5;
    TEST_ASSERT_TRUE_MESSAGE(N > MAX_ERR_UDP_TX,
                             "ring too small to reach the error limit");

    recorder_reset();
    fill_ring(N);
    g_end_fails = true;
    for (int i = 0; i < MAX_ERR_UDP_TX; i++)
        sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_reset_udp, "esp32 resetMeshComUDP count");
    // N frames pushed, MAX_ERR_UDP_TX passes each dropping exactly one slot
    // (DR-24: no retry) -- N - MAX_ERR_UDP_TX must remain unread. There is
    // no per-slot zero to check any more (no equivalent in byte_fifo_t); this
    // unread count IS the "was it dropped, not kept" signal.
    TEST_ASSERT_EQUAL_INT_MESSAGE(N - MAX_ERR_UDP_TX, (int)bf_unread(&udpOutRing),
                                  "esp32 kept a failing slot instead of dropping it -- DR-24 regressed");
    TEST_ASSERT_FALSE_MESSAGE(hasIPaddress, "esp32 kept hasIPaddress after reset");
    TEST_ASSERT_FALSE_MESSAGE(meshcom_settings.node_hasIPaddress,
                              "esp32 did not mirror hasIPaddress into settings");

    recorder_reset();
    fill_ring(N);
    g_write_fails = true;
    for (int i = 0; i < MAX_ERR_UDP_TX; i++)
        sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_reset_dhcp, "nrf52 resetDHCP count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(N - MAX_ERR_UDP_TX, (int)bf_unread(&udpOutRing),
                                  "nrf52 stopped advancing on a failed write");
    TEST_ASSERT_FALSE_MESSAGE(neth.hasIPaddress,
                              "nrf52 kept hasIPaddress after reset");
}

static void test_agreement_outbound_leak_counted_separately_on_both_sides(void)
{
    // DR-25, RE-DECIDED 2026-09-12 (review corrected verdict to both-wrong),
    // IMPLEMENTED: an unconfigured-source frame reaching this SECOND door
    // (the frame has already been sent by this point on both platforms) is
    // now counted on BOTH sides, under its OWN counter/marker
    // (stat_tx_leak_unconfigured / logTxLeakUnconfigured(),
    // lora_functions.cpp) -- NEVER the RX drop counter/marker used at the
    // two real doors (OnRxDone's primary guard and the inbound GATE) where a
    // frame really IS stopped. The separation is the whole point of the row:
    // a rising RX-drop count means the primary guard is working; a rising
    // TX-leak count means it is NOT. g_dropped and g_leaked are fed by two
    // separate stub functions (logRxDropUnconfigured / logTxLeakUnconfigured)
    // so this test can assert the counters never cross-contaminate.
    bDisplayInfo = true;

    recorder_reset();
    fill_ring(1, "XX0XXX-00");
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_sent.size(),
                                  "the frame is sent either way -- this is the "
                                  "second door, not the first");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_leaked.size(),
                                  "esp32 lost the DR-25 leak count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_dropped.size(),
                                  "esp32 counted the outbound leak under the RX drop counter -- "
                                  "the two events mean opposite things");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "esp32 printed an unconfigured source");

    recorder_reset();
    fill_ring(1, "XX0XXX-00");
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_sent.size(),
                                  "the frame is sent either way -- this is the "
                                  "second door, not the first");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_leaked.size(),
                                  "nrf52 did not grow the DR-25 leak count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_dropped.size(),
                                  "nrf52 counted the outbound leak under the RX drop counter -- "
                                  "the two events mean opposite things");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "nrf52 printed an unconfigured source");

    bDisplayInfo = false;
}

static void test_agreement_via_prefixes_the_print_on_both_sides(void)
{
    // DR-26, RE-DECIDED 2026-09-12 (fable Finding 8; operator: info gates,
    // via decorates), IMPLEMENTED: both drains now print only when
    // bDisplayInfo is set, and bDisplayVia only decorates the prefix when it
    // does -- "[MESHu]...TX-UDP  " with via on, "TX-UDP  " without (ESP32
    // adopted nRF52's two-trailing-space form). --via never adds a line that
    // --info off would have suppressed.
    recorder_reset();
    bDisplayInfo = true;
    bDisplayVia = true;
    fill_ring(1);
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("[MESHu]...TX-UDP  |DK5EN-1", g_printed[0].c_str());

    recorder_reset();
    bDisplayInfo = true;
    bDisplayVia = true;
    fill_ring(1);
    sendUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("[MESHu]...TX-UDP  |DK5EN-1", g_printed[0].c_str());

    recorder_reset();
    bDisplayInfo = true;
    bDisplayVia = false;
    fill_ring(1);
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("TX-UDP  |DK5EN-1", g_printed[0].c_str());

    recorder_reset();
    bDisplayInfo = true;
    bDisplayVia = false;
    fill_ring(1);
    sendUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("TX-UDP  |DK5EN-1", g_printed[0].c_str());

    // DR-26's other half: --via must never add a line that --info off would
    // have suppressed.
    recorder_reset();
    bDisplayInfo = false;
    bDisplayVia = true;
    fill_ring(1);
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "esp32: --via printed with --info off");

    recorder_reset();
    bDisplayInfo = false;
    bDisplayVia = true;
    fill_ring(1);
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "nrf52: --via printed with --info off");

    bDisplayVia = false;
    bDisplayInfo = false;
}

static void test_frame_survives_the_drain_intact_on_both_sides(void)
{
    // Both copies carry the CONC-16 follow-up fix (aprs_len = msg_len - 36).
    // If either regressed to msg_len, decodeAPRS() would be handed 36 bytes
    // of trailing zeros and the source call would not survive.
    //
    // MEASURED LIMIT, recorded so nobody re-derives it: mutating aprs_len
    // back to `msg_len` on EITHER side leaves this test green, and so does
    // asserting on the payload and on the decoded msg_len. That is not a
    // weak assertion, it is the arithmetic being unobservable at this
    // boundary -- decodeAPRS() walks the frame's own structure and barely
    // uses its `size` argument, and the snapshot the copy reads from is
    // oversized and zero-filled precisely so the 36-byte overrun lands in
    // padding. The CONC-16 follow-up fix is therefore a correctness fix for
    // the READ, not a behaviour change, and cannot be regression-tested from
    // here. Catching it needs a bounds check on the copy (ASan over the
    // drain, filed as a backlog item), not a stronger assertion here.
    //
    // What this test does pin is the decode contract across the drain: the
    // payload -- the LAST field of the frame, so the one most exposed to a
    // length error -- and the decoded length survive intact on both sides.
    bDisplayInfo = true;
    for (int side = 0; side < 2; side++)
    {
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        // fill_ring() writes "slot 0" as the text of the first slot, and
        // returns the msg_len of the last (here: only) frame it pushed.
        uint16_t msg_len = fill_ring(1, "DK5EN-9");
        (side ? sendUDP : sendMeshComUDP)();

        char msg[96];
        snprintf(msg, sizeof(msg), "%s did not decode", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(), msg);
        snprintf(msg, sizeof(msg), "%s: source call", name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_printed[0].c_str(), "DK5EN-9"), msg);
        snprintf(msg, sizeof(msg),
                 "%s: payload -- aprs_len is wrong if this carries a tail", name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("slot 0", g_payload[0].c_str(), msg);
        snprintf(msg, sizeof(msg), "%s: decoded length is msg_len-36", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(msg_len - UDP_HDR, g_decoded_len[0], msg);
    }
    bDisplayInfo = false;
}

static void test_short_slot_is_not_decoded_on_either_side(void)
{
    // msg_len <= 36 means aprs_len == 0: header only, nothing to decode.
    // Both sides must still send it and still advance.
    bDisplayInfo = true;
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        bf_reset(&udpOutRing);
        uint8_t hdr[UDP_HDR];
        for (int i = 0; i < UDP_HDR; i++)
            hdr[i] = (uint8_t)(0xC0 + i);
        TEST_ASSERT_TRUE(bf_push(&udpOutRing, hdr, UDP_HDR) >= 0);
        (side ? sendUDP : sendMeshComUDP)();
        TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
        TEST_ASSERT_EQUAL_INT(UDP_HDR, (int)g_sent[0].bytes.size());
        TEST_ASSERT_EQUAL_INT(0, (int)g_printed.size());
        TEST_ASSERT_TRUE(bf_empty(&udpOutRing));
    }
    bDisplayInfo = false;
}

// ---------------------------------------------------------------------------
// CORPUS DUMP (test plan 4.4/9.1): one ordered sink-call-sequence per corpus
// datagram, one file per platform -- u2-esp32.txt / u2-nrf52.txt. Additive to
// the named Unity cases above; those stay exactly as they are.
//
// This suite is the OUTBOUND drain, not the inbound parser U1 feeds directly:
// test/golden/corpus/udp1990/*.hex holds wire datagrams for the INCOMING UDP
// side (GATE/BEAT/CONF-indicator-wrapped, consumed by handleUdpFrame_*() in
// src/esp32|nrf52/udp_frame_*.cpp, which relays a decoded GATE frame to LoRa
// TX -- never back onto udpOutRing). There is no src/ code path that
// turns a corpus item into outbound-ring content, so nothing here invents
// one. Instead every corpus item's raw bytes are staged into the ring
// verbatim, the same way the hand-built cases above stage a frame via
// put_slot(): a recognisable dummy 36-byte header (0xC0+i, never inspected by
// the drain) followed by the datagram bytes unchanged. The drain is then run
// exactly as production code runs it and the real sink calls are recorded.
//
// Consequence, and the reason this is spelled out rather than left for the
// reader to notice: since every corpus item's own first byte is an indicator
// letter ('G'/'B'/'C'/'X', 0x47/0x42/0x43/0x58), never the APRS type marker
// (0x21/0x3A/0x40) the drain's own gate checks for, decodeAPRS() is never
// reached from this loop and PRINT/DROP never appear in the two files it
// writes. That is not a bug in the dump -- it is the honest, if unglamorous,
// answer to "what does the outbound drain do with the inbound corpus", and
// it is exactly why the sink log records what actually ran rather than what
// was expected to.
// ---------------------------------------------------------------------------

// Repo root, derived from __FILE__ rather than the working directory: a
// PlatformIO native test binary's CWD is not guaranteed, so a relative path
// to test/golden/corpus/udp1990/ could silently resolve to nothing (or to
// something else entirely) instead of failing loudly.
static bool path_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

static std::string repo_root()
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_udp_send_twin/test_udp_send_twin.cpp";

    if (!file.empty() && file[0] == '/')
    {
        // __FILE__ came out absolute: strip the known relative suffix.
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

    // __FILE__ came out relative (PlatformIO's normal case, compiled with
    // CWD == project root) -- but the test BINARY's own CWD when it runs is
    // NOT guaranteed, so a relative path formed from it could silently
    // resolve to nothing (or to an unrelated file). Re-derive the root
    // instead: walk upward from the actual runtime CWD until
    // "<candidate>/<file>" exists next to a platformio.ini, which is true at
    // exactly one place regardless of where the binary happens to be run
    // from, as long as that place is an ancestor of the CWD.
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

    TEST_FAIL_MESSAGE(("could not derive repo root: walked up from the runtime "
                       "cwd looking for '" + file + "' next to platformio.ini "
                       "and found neither").c_str());
    return "";
}

struct CorpusItem
{
    std::string filename;
    std::vector<uint8_t> bytes;
};

static bool hex_nibble(char c, uint8_t &out)
{
    if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = (uint8_t)(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = (uint8_t)(c - 'A' + 10); return true; }
    return false;
}

// Loads every *.hex file in `dir`, sorted by filename, skipping '#' comment
// lines. Fails the test loudly (never returns an empty/partial result
// silently) if the directory is missing or yields zero items -- a dump that
// quietly writes nothing is the false-comfort artefact this work exists to
// avoid.
static std::vector<CorpusItem> load_corpus(const std::string &dir)
{
    std::vector<CorpusItem> items;

    DIR *d = opendir(dir.c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(d, ("corpus directory missing: " + dir).c_str());

    std::vector<std::string> names;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr)
    {
        std::string name(ent->d_name);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".hex") == 0)
            names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());

    for (const auto &name : names)
    {
        std::ifstream f((dir + "/" + name).c_str());
        TEST_ASSERT_TRUE_MESSAGE(f.good(), ("could not open " + name).c_str());

        std::string hexstr;
        std::string line;
        while (std::getline(f, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos)
                continue;
            if (line[start] == '#')
                continue;
            hexstr += line.substr(start);
        }
        TEST_ASSERT_TRUE_MESSAGE(hexstr.size() % 2 == 0,
                                 ("odd hex digit count in " + name).c_str());

        CorpusItem item;
        item.filename = name;
        item.bytes.reserve(hexstr.size() / 2);
        for (size_t i = 0; i + 1 < hexstr.size(); i += 2)
        {
            uint8_t hi = 0, lo = 0;
            TEST_ASSERT_TRUE_MESSAGE(hex_nibble(hexstr[i], hi) && hex_nibble(hexstr[i + 1], lo),
                                     ("bad hex digit in " + name).c_str());
            item.bytes.push_back((uint8_t)((hi << 4) | lo));
        }
        items.push_back(std::move(item));
    }

    TEST_ASSERT_TRUE_MESSAGE(!items.empty(),
                             ("corpus directory yielded zero items: " + dir).c_str());
    return items;
}

// Stages one corpus item onto udpOutRing, same shape as put_slot(): the
// fixed dummy 36-byte header, then the datagram bytes verbatim, pushed as
// one frame. byte_fifo_t stores each frame's length as a uint8_t
// (src/byte_fifo.h), so a datagram that would overflow it cannot be staged
// this way -- returns false with a reason rather than truncating or
// wrapping it.
static bool stage_corpus_item(const std::vector<uint8_t> &bytes, std::string &why_not)
{
    size_t total = (size_t)UDP_HDR + bytes.size();
    if (total > 255)
    {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "exceeds UDP_TX_BUF_SIZE: %d-byte header + %d-byte payload = "
                 "%d > 255 (byte_fifo_t frame length is a uint8_t)",
                 UDP_HDR, (int)bytes.size(), (int)total);
        why_not = msg;
        return false;
    }

    uint8_t payload[UDP_HDR + 255];
    for (int i = 0; i < UDP_HDR; i++)
        payload[i] = (uint8_t)(0xC0 + i);
    if (!bytes.empty())
        memcpy(payload + UDP_HDR, bytes.data(), bytes.size());
    if (bf_push(&udpOutRing, payload, (uint8_t)total) < 0)
    {
        why_not = "rejected by bf_push (does not fit RING_BYTES_UDP)";
        return false;
    }
    return true;
}

// REPRESENTATIVE SUBSET -- and why this is not the whole corpus.
//
// Driving all 37 items produced 37 blocks with exactly TWO distinct sink
// SHAPES: 36x `UDPBEGIN,UDPWRITE,UDPEND,TXCOUNT` (ESP32) against
// `UDPWRITE,UDPEND` (nRF52), plus the one oversize item that cannot be
// staged. Only the payload bytes varied, and those are just the corpus
// echoed back through a dummy header. The resulting before-diff was 20 kB
// -- three times U1's -- to express ONE fact 36 times, and it would churn
// wholesale on any change to the header or the hex rendering.
//
// The cause is above: this corpus is INBOUND wire format and no real code
// path turns an item into outbound-ring content, so the drain never reaches
// decodeAPRS() and the shape cannot vary with the payload. Per operator
// decision 2026-09-12 the dump therefore drives a representative subset:
// the first few items (the recurring shape) plus every item that is NOT
// applicable, so the not-applicable reasons stay on the record. The guard
// that matters -- the socket call SEQUENCE, which DR-23 warns a refactor of
// either layer could silently change, and which W6 is about to rewrite --
// is fully preserved by one block per shape.
//
// If a genuine outbound corpus is ever built (frames shaped as the drain
// would really receive them), drop the subset and drive all of it: the
// shapes would then actually vary and the bulk would be worth its size.
static std::vector<CorpusItem> representative_subset(const std::vector<CorpusItem> &all)
{
    const size_t KEEP_LEADING = 3;
    std::vector<CorpusItem> out;
    for (size_t i = 0; i < all.size(); i++)
    {
        // Same bound stage_corpus_item() enforces, asked without staging: a
        // byte_fifo_t frame length is a uint8_t. Kept in step with that
        // function by construction -- if its bound changes, this must
        // change with it.
        size_t total = (size_t)UDP_HDR + all[i].bytes.size();
        bool stageable = (total <= 255);
        if (i < KEEP_LEADING || !stageable)
            out.push_back(all[i]);
    }
    TEST_ASSERT_TRUE_MESSAGE(out.size() >= KEEP_LEADING,
                             "representative subset collapsed to nothing");
    return out;
}


// Drives every corpus item through one platform's drain and writes the
// ordered sink log to `out_path`. One item per ring slot, one drain pass per
// item, recorder_reset() (and with it log_reset()) between items so the
// NNN numbering in the output restarts at 001 per "=== <filename>" section.
static void dump_platform(const std::string &out_path, Drain fn)
{
    std::string root = repo_root();
    std::vector<CorpusItem> items = load_corpus(root + "/test/golden/corpus/udp1990");
    items = representative_subset(items);

    std::ofstream out(out_path.c_str(), std::ios::binary | std::ios::trunc);
    TEST_ASSERT_TRUE_MESSAGE(out.good(),
                             ("could not open output file: " + out_path).c_str());

    for (const auto &item : items)
    {
        out << "=== " << item.filename << "\n";

        recorder_reset();
        bf_reset(&udpOutRing);
        // decodeAPRS() runs its gate unconditionally; PRINT itself is gated
        // by bDisplayInfo (ESP32) / bDisplayInfo or bDisplayVia (nRF52) --
        // on, so a corpus item that DID carry a decodable frame at offset 0
        // would show it, matching how the named agreement/drift tests above
        // observe decode.
        bDisplayInfo = true;

        std::string why_not;
        if (!stage_corpus_item(item.bytes, why_not))
        {
            out << "(not applicable: " << why_not << ")\n";
            bDisplayInfo = false;
            continue;
        }

        fn();

        if (g_log.empty())
            out << "(no sink calls)\n";
        else
            for (const auto &line : g_log)
                out << line << "\n";

        bDisplayInfo = false;
    }
}

static void test_dump_u2_esp32(void)
{
    std::string path = repo_root() + "/test/golden/native/u2-esp32.txt";
    dump_platform(path, sendMeshComUDP);
}

static void test_dump_u2_nrf52(void)
{
    std::string path = repo_root() + "/test/golden/native/u2-nrf52.txt";
    dump_platform(path, sendUDP);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_same_ring_drains_to_the_same_datagrams);
    RUN_TEST(test_datagram_is_the_slot_from_offset_one);
    RUN_TEST(test_pointer_moves_one_slot_per_pass_and_wraps);
    RUN_TEST(test_sent_slot_is_zeroed);
    RUN_TEST(test_empty_ring_sends_nothing);
    RUN_TEST(test_busy_flag_holds_the_slot_on_both_sides);
    RUN_TEST(test_mid_send_eviction_does_not_double_advance);

    RUN_TEST(test_drift_esp32_has_three_preconditions_nrf52_has_none);
    RUN_TEST(test_agreement_both_refuse_an_unresolved_destination);
    RUN_TEST(test_agreement_decode_and_print_survive_a_failed_send);
    RUN_TEST(test_drift_esp32_calls_endpacket_after_a_failed_write);
    RUN_TEST(test_agreement_error_limit_drops_the_slot_on_both_sides);
    RUN_TEST(test_agreement_outbound_leak_counted_separately_on_both_sides);
    RUN_TEST(test_agreement_via_prefixes_the_print_on_both_sides);

    RUN_TEST(test_frame_survives_the_drain_intact_on_both_sides);
    RUN_TEST(test_short_slot_is_not_decoded_on_either_side);

    RUN_TEST(test_dump_u2_esp32);
    RUN_TEST(test_dump_u2_nrf52);

    return UNITY_END();
}
