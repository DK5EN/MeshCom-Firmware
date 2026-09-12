// Twin test for the UDP-out ring drain (test plan U2 / section 4.4, audit
// rows D1-05/D1-06). "ring with N entries drains to identical datagrams and
// order on both sides; pointer movement per pass".
//
// Unlike the U6 country twin, this is ONE binary, not one per side: the two
// drains are named differently (sendMeshComUDP on ESP32, sendUDP on nRF52),
// so both translation units link together and share one ringBufferUDPout.
// The same ring is therefore drained by both implementations in the same
// process, and the comparison is a real differential rather than two runs
// compared through files.
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

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

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
uint8_t ringBufferUDPout[MAX_RING_UDP][UDP_TX_BUF_SIZE + 20];
int udpWrite = 0;
int udpRead = 0;

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
// What decodeAPRS() actually made of the frame: payload and decoded length.
// Kept apart from g_printed because the payload sits at the END of the frame
// and is therefore the only field that notices a wrong aprs_len -- the source
// call is near the front and survives a 36-byte overrun unharmed.
static std::vector<std::string> g_payload;
static std::vector<int> g_decoded_len;
static std::vector<std::string> g_dropped;   // RX-01 logRxDropUnconfigured()
static int g_reset_udp = 0;                  // ESP32 resetMeshComUDP()
static int g_reset_dhcp = 0;                 // nRF52 resetDHCP()
static int g_count_tx_ok = 0, g_count_tx_fail = 0;

// When set, the next write fails. Models a refused/short socket write.
static bool g_write_fails = false;
// When set, the sink force-advances udpRead mid-send, modelling the ring-full
// eviction path in addRingPointer() overtaking the reader (CONC-16).
static bool g_evict_during_send = false;

static void recorder_reset()
{
    g_sent.clear();
    g_printed.clear();
    g_payload.clear();
    g_decoded_len.clear();
    g_dropped.clear();
    g_reset_udp = g_reset_dhcp = 0;
    g_count_tx_ok = g_count_tx_fail = 0;
    g_write_fails = false;
    g_evict_during_send = false;
    err_cnt_udp_tx = 0;
    udp_is_busy = false;
    neth.udp_is_busy = false;
    hasIPaddress = true;
    neth.hasIPaddress = true;
    bWIFIAP = false;
    node_hostip = IPAddress(44, 143, 8, 143);
    meshcom_settings.node_hasIPaddress = true;
}

static void sink_write(const uint8_t *buf, uint16_t len)
{
    Datagram d;
    d.bytes.assign(buf, buf + len);
    d.ended = false;
    g_sent.push_back(d);
    if (g_evict_during_send)
    {
        udpRead++;
        if (udpRead >= MAX_RING_UDP)
            udpRead = 0;
    }
}

// --- ESP32 side: the C2 primitives ---------------------------------------
bool udpBeginRaw_esp32() { return true; }

bool udpWriteRaw_esp32(const uint8_t *buf, uint16_t len)
{
    sink_write(buf, len);
    return !g_write_fails;
}

bool udpEndRaw_esp32()
{
    if (!g_sent.empty())
        g_sent.back().ended = true;
    return !g_write_fails;
}

void udpCountTx(bool ok) { ok ? g_count_tx_ok++ : g_count_tx_fail++; }
void resetMeshComUDP() { g_reset_udp++; }

void logRxDropUnconfigured(const char *call)
{
    g_dropped.push_back(call ? call : "(null)");
}

// --- nRF52 side: NrfETH ---------------------------------------------------
bool NrfETH::sendUDP(uint8_t buffer[UDP_TX_BUF_SIZE], uint16_t rx_buf_size)
{
    sink_write(buffer, rx_buf_size);
    if (!g_write_fails)
        g_sent.back().ended = true;
    return !g_write_fails;
}

int NrfETH::resetDHCP()
{
    g_reset_dhcp++;
    return 0;
}

// --- shared ---------------------------------------------------------------
void printBuffer_aprs(char *msg_source, struct aprsMessage &aprsMessage,
                      const char *tail)
{
    (void)tail;
    std::string s(msg_source ? msg_source : "");
    s += "|";
    s += aprsMessage.msg_source_call.c_str();
    g_printed.push_back(s);
    g_payload.push_back(aprsMessage.msg_payload.c_str());
    g_decoded_len.push_back((int)aprsMessage.msg_len);
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
    m.msg_source_path = String(src);
    m.msg_destination_path = String("9");
    m.msg_payload = String(text);
    m.msg_source_hw = 9;
    m.msg_source_mod = 0x88;
    m.msg_source_fw_version = 35;
    m.msg_last_hw = 0x80 | 9;
    m.msg_source_fw_sub_version = 'p';
    return encodeAPRS(out, m);
}

// Writes one slot at `slot` and returns the msg_len stored in byte 0.
static uint16_t put_slot(int slot, const char *src, const char *text)
{
    uint8_t frame[UDP_TX_BUF_SIZE];
    memset(frame, 0, sizeof(frame));
    uint16_t flen = build_frame(frame, src, text);

    memset(ringBufferUDPout[slot], 0, sizeof(ringBufferUDPout[0]));
    uint16_t msg_len = (uint16_t)(UDP_HDR + flen);
    ringBufferUDPout[slot][0] = (uint8_t)msg_len;
    // a recognisable header, so a side that sent the wrong offset is obvious
    for (int i = 0; i < UDP_HDR; i++)
        ringBufferUDPout[slot][1 + i] = (uint8_t)(0xC0 + i);
    memcpy(ringBufferUDPout[slot] + 1 + UDP_HDR, frame, flen);
    return msg_len;
}

static void fill_ring(int n, const char *src = "DK5EN-1")
{
    memset(ringBufferUDPout, 0, sizeof(ringBufferUDPout));
    for (int i = 0; i < n; i++)
    {
        char text[32];
        snprintf(text, sizeof(text), "slot %d", i);
        put_slot(i, src, text);
    }
    udpRead = 0;
    udpWrite = n % MAX_RING_UDP;
}

// Drains until the ring is empty or `max_passes` is reached, so a drain that
// fails to advance cannot hang the suite.
typedef void (*Drain)(void);
static int drain_all(Drain fn, int max_passes = MAX_RING_UDP * 2)
{
    int passes = 0;
    while (udpWrite != udpRead && passes < max_passes)
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
    // The wire bytes are ringBufferUDPout[slot][1 .. 1+msg_len), header
    // included. Pinned because "off by the 36-byte header" is exactly the
    // bug class the CONC-16 commit found in the *other* copy of this
    // arithmetic (the convBuffer copy below).
    recorder_reset();
    memset(ringBufferUDPout, 0, sizeof(ringBufferUDPout));
    uint16_t msg_len = put_slot(0, "DK5EN-1", "hello");
    // snapshot first: the drain zeroes the slot it just sent
    uint8_t before[UDP_TX_BUF_SIZE + 20];
    memcpy(before, ringBufferUDPout[0], sizeof(before));
    udpRead = 0;
    udpWrite = 1;
    sendMeshComUDP();

    TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
    TEST_ASSERT_EQUAL_INT(msg_len, (int)g_sent[0].bytes.size());
    TEST_ASSERT_EQUAL_MEMORY(before + 1, g_sent[0].bytes.data(), msg_len);
}

static void test_pointer_moves_one_slot_per_pass_and_wraps(void)
{
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        const char *name = side ? "nrf52" : "esp32";
        recorder_reset();
        fill_ring(3);
        // start near the end so the wrap is covered
        udpRead = MAX_RING_UDP - 1;
        udpWrite = 1;
        put_slot(MAX_RING_UDP - 1, "DK5EN-1", "wrap");

        char msg[64];
        fn();
        snprintf(msg, sizeof(msg), "%s: udpRead wraps to 0", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, udpRead, msg);
        fn();
        snprintf(msg, sizeof(msg), "%s: udpRead advances to 1", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, udpRead, msg);
        snprintf(msg, sizeof(msg), "%s: ring drained", name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(udpWrite, udpRead, msg);
    }
}

static void test_sent_slot_is_zeroed(void)
{
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        recorder_reset();
        fill_ring(1);
        fn();
        for (int i = 0; i < UDP_TX_BUF_SIZE; i++)
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ringBufferUDPout[0][i],
                                            side ? "nrf52 slot not zeroed"
                                                 : "esp32 slot not zeroed");
    }
}

static void test_empty_ring_sends_nothing(void)
{
    for (int side = 0; side < 2; side++)
    {
        recorder_reset();
        fill_ring(0);
        udpRead = udpWrite = 4;
        (side ? sendUDP : sendMeshComUDP)();
        TEST_ASSERT_EQUAL_INT(0, (int)g_sent.size());
        TEST_ASSERT_EQUAL_INT(4, udpRead);
    }
}

static void test_busy_flag_holds_the_slot_on_both_sides(void)
{
    recorder_reset();
    fill_ring(2);
    udp_is_busy = true;
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(), "esp32 sent while busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, udpRead, "esp32 advanced while busy");

    recorder_reset();
    fill_ring(2);
    neth.udp_is_busy = true;
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_sent.size(), "nrf52 sent while busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, udpRead, "nrf52 advanced while busy");
}

static void test_mid_send_eviction_does_not_double_advance(void)
{
    // CONC-16 guard: a writer force-advancing udpRead past us through the
    // ring-full eviction path must not be followed by our own advance.
    for (int side = 0; side < 2; side++)
    {
        Drain fn = side ? sendUDP : sendMeshComUDP;
        recorder_reset();
        fill_ring(4);
        g_evict_during_send = true;
        fn();
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, udpRead,
                                      side ? "nrf52 double-advanced"
                                           : "esp32 double-advanced");
        // the evicted slot must also NOT have been zeroed by us
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ringBufferUDPout[1][0],
                                      side ? "nrf52 zeroed a slot it no longer owns"
                                           : "esp32 zeroed a slot it no longer owns");
    }
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
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, udpRead, msg);
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

static void test_drift_failed_write_decodes_on_esp32_only(void)
{
    // ESP32 decodes and prints the frame after the write regardless of the
    // result; nRF52 does both inside the `else` of the same check, so a
    // failed write means no decode at all. Same frame, same failure, two
    // behaviours.
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "nrf52 started printing on a failed write");

    bDisplayInfo = false;
}

static void test_drift_esp32_calls_endpacket_after_a_failed_write(void)
{
    // ESP32 runs udpEndRaw_esp32() even when the write failed (unless the
    // error limit tripped). nRF52's failure path is inside
    // NrfETH::sendUDP(), which has already ended the packet itself.
    recorder_reset();
    fill_ring(1);
    g_write_fails = true;
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
    TEST_ASSERT_TRUE_MESSAGE(g_sent[0].ended,
                             "esp32 skipped endPacket after a failed write");
}

static void test_drift_error_limit_esp32_retries_the_slot_nrf52_drops_it(void)
{
    // The sharpest difference in the pair. On the pass where err_cnt_udp_tx
    // reaches MAX_ERR_UDP_TX:
    //   ESP32  resets the socket and RETURNS BEFORE THE ADVANCE -- the slot
    //          stays in the ring for one more pass.
    //   nRF52  resets DHCP and FALLS THROUGH to the advance -- the slot is
    //          zeroed and dropped.
    // Neither side wedges: err_cnt_udp_tx is zeroed before ESP32's early
    // return, so the retained slot is dropped on the very next failing pass
    // (the assertion below shows nine of ten frames gone). And this whole
    // branch keys on udpWriteRaw_esp32(), which on real hardware cannot fail
    // for a non-empty frame (WiFiUDP::write() only buffers); the real result
    // is endPacket(), which the ESP32 drain logs and discards. Decided as
    // DR-24 (nrf52-correct, 2026-09-12): ESP32 keys on endPacket() and drops
    // like nRF52; this case then flips to an agreement pin. See
    // docs/testplan/drift-matrix-review-verdict-20260912.md, Finding 1.
    // The ring must hold more than MAX_ERR_UDP_TX slots: a failed write is
    // not by itself an early return on either side, so each of the first
    // nine passes still consumes a slot.
    const int N = MAX_RING_UDP - 1;
    TEST_ASSERT_TRUE_MESSAGE(N > MAX_ERR_UDP_TX,
                             "ring too small to reach the error limit");

    recorder_reset();
    fill_ring(N);
    g_write_fails = true;
    for (int i = 0; i < MAX_ERR_UDP_TX; i++)
        sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_reset_udp, "esp32 resetMeshComUDP count");
    // nine passes advanced; the tenth tripped the limit and returned BEFORE
    // the advance, so the slot it failed on is still in the ring.
    TEST_ASSERT_EQUAL_INT_MESSAGE(MAX_ERR_UDP_TX - 1, udpRead,
                                  "esp32 advanced past the slot it just failed");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, ringBufferUDPout[MAX_ERR_UDP_TX - 1][0],
                                  "esp32 zeroed the slot it kept for a retry");
    TEST_ASSERT_FALSE_MESSAGE(hasIPaddress, "esp32 kept hasIPaddress after reset");
    TEST_ASSERT_FALSE_MESSAGE(meshcom_settings.node_hasIPaddress,
                              "esp32 did not mirror hasIPaddress into settings");

    recorder_reset();
    fill_ring(N);
    g_write_fails = true;
    for (int i = 0; i < MAX_ERR_UDP_TX; i++)
        sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_reset_dhcp, "nrf52 resetDHCP count");
    // no early return: the tenth slot is zeroed and dropped like the others.
    TEST_ASSERT_EQUAL_INT_MESSAGE(MAX_ERR_UDP_TX, udpRead,
                                  "nrf52 stopped advancing on a failed write");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ringBufferUDPout[MAX_ERR_UDP_TX - 1][0],
                                    "nrf52 kept the slot it failed on");
    TEST_ASSERT_FALSE_MESSAGE(neth.hasIPaddress,
                              "nrf52 kept hasIPaddress after reset");
}

static void test_drift_rx01_unconfigured_guard_is_esp32_only(void)
{
    // RX-01 (BACKLOG 3.8k): a frame whose source is still the factory
    // callsign is counted and its debug print suppressed -- on ESP32. The
    // nRF52 drain has no such check and prints it like any other frame.
    // Note what neither side does: the datagram has already been sent by
    // this point on both. This is the second door, not the first.
    bDisplayInfo = true;

    recorder_reset();
    fill_ring(1, "XX0XXX-00");
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_sent.size(),
                                  "the frame is sent either way -- if this "
                                  "became 0, RX-01's second door grew teeth "
                                  "and the comment in the drain is stale");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_dropped.size(),
                                  "esp32 lost the RX-01 count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_printed.size(),
                                  "esp32 printed an unconfigured source");

    recorder_reset();
    fill_ring(1, "XX0XXX-00");
    sendUDP();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_dropped.size(),
                                  "nrf52 grew an RX-01 guard -- drift row "
                                  "changed, update the matrix");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)g_printed.size(),
                                  "nrf52 stopped printing the frame");

    bDisplayInfo = false;
}

static void test_drift_nrf52_prefixes_the_print_with_mesh_when_via_is_on(void)
{
    // bDisplayVia selects a different prefix on nRF52 ("[MESHu]...TX-UDP  ");
    // ESP32 has one prefix ("TX-UDP ") and ignores bDisplayVia entirely.
    // Note the trailing spaces differ too -- "TX-UDP " against "TX-UDP  ".
    bDisplayInfo = true;
    bDisplayVia = true;

    recorder_reset();
    fill_ring(1);
    sendMeshComUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("TX-UDP |DK5EN-1", g_printed[0].c_str());

    recorder_reset();
    fill_ring(1);
    sendUDP();
    TEST_ASSERT_EQUAL_INT(1, (int)g_printed.size());
    TEST_ASSERT_EQUAL_STRING("[MESHu]...TX-UDP  |DK5EN-1", g_printed[0].c_str());

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
        // fill_ring() writes "slot 0" as the text of the first slot
        fill_ring(1, "DK5EN-9");
        uint16_t msg_len = ringBufferUDPout[0][0];
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
        memset(ringBufferUDPout, 0, sizeof(ringBufferUDPout));
        ringBufferUDPout[0][0] = UDP_HDR;
        for (int i = 0; i < UDP_HDR; i++)
            ringBufferUDPout[0][1 + i] = (uint8_t)(0xC0 + i);
        udpRead = 0;
        udpWrite = 1;
        (side ? sendUDP : sendMeshComUDP)();
        TEST_ASSERT_EQUAL_INT(1, (int)g_sent.size());
        TEST_ASSERT_EQUAL_INT(UDP_HDR, (int)g_sent[0].bytes.size());
        TEST_ASSERT_EQUAL_INT(0, (int)g_printed.size());
        TEST_ASSERT_EQUAL_INT(1, udpRead);
    }
    bDisplayInfo = false;
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
    RUN_TEST(test_drift_failed_write_decodes_on_esp32_only);
    RUN_TEST(test_drift_esp32_calls_endpacket_after_a_failed_write);
    RUN_TEST(test_drift_error_limit_esp32_retries_the_slot_nrf52_drops_it);
    RUN_TEST(test_drift_rx01_unconfigured_guard_is_esp32_only);
    RUN_TEST(test_drift_nrf52_prefixes_the_print_with_mesh_when_via_is_on);

    RUN_TEST(test_frame_survives_the_drain_intact_on_both_sides);
    RUN_TEST(test_short_slot_is_not_decoded_on_either_side);

    return UNITY_END();
}
