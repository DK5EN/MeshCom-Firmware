// BLE phone harness (docs/ble-batt-campaign-20260929.md, BLE-N1 / BLE-N2).
//
// The node->phone path on the host: the REAL byte_fifo rings sized like the
// ESP32-S3 / nRF52 build (RING_BYTES_PHONE / RING_BYTES_PHONECOM), the REAL
// producer and drain in src/ble_phone_drain.h, the real blePhoneFrame(), and a
// fake phone as the notify sink. The fake phone can refuse a notify (BUSY,
// NimBLE out of mbufs / Bluefruit TX full), drop the link (DOWN), and cuts
// every frame to its negotiated MTU-3 the way a peer would.
//
// Register sizes in the burst tables are the JSON lengths (incl. the type
// byte) captured from the real nodes on 2026-09-29, Heltec V3 DK5EN-1 and
// RAK4631 DK5EN-90.
//
// REGRESSION PROOF. The pre-fix drain (pop before send, result ignored, uint8_t
// length) is kept below as oldDrainOne(). The overrun cases run through
// run_drain(), which uses it when the environment variable
// BLE_HARNESS_OLD_LOGIC is set:
//
//   BLE_HARNESS_OLD_LOGIC=1 pio test -e native_ble_phone_harness -f test_ble_phone_harness
//
// then shows the BUSY cases failing on the old logic; without it they pass on
// the fixed drain. test_old_logic_loses_a_frame_on_busy pins the old model in
// place (it must lose the frame), so the instrument itself is checked too.

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <configuration.h>     // RING_BYTES_*, MAX_MSG_LEN_PHONE, UDP_TX_BUF_SIZE
#include "ble_phone_drain.h"

// The header declares the firmware's global counters; the harness keeps its
// own and never touches this one.
BlePhoneStats g_blePhoneStats = {0, 0, 0, 0, 0, 0};

typedef std::vector<uint8_t> Bytes;

// --- rings, sized like production --------------------------------------------

static uint8_t phoneStore[RING_BYTES_PHONE];
static uint8_t comStore[RING_BYTES_PHONECOM];
static byte_fifo_t phoneRingT = BYTE_FIFO_INIT(phoneStore);
static byte_fifo_t comRingT = BYTE_FIFO_INIT(comStore);

static BlePhoneStats st;
static BlePhoneDrainState phoneState;
static BlePhoneDrainState comState;
static bool old_logic = false;

// --- the fake phone -----------------------------------------------------------

struct FakePhone
{
    uint16_t mtu;            // negotiated ATT MTU, payload limit is mtu-3
    int      busy_calls;     // next N sink calls answer BUSY
    uint8_t  busy_marker;    // frames whose first byte is this are ALWAYS BUSY (0 = none)
    bool     down;           // link down: DOWN, nothing recorded
    int      calls;          // sink calls, all outcomes
    std::vector<Bytes>    rx;      // what the phone received, cut to mtu-3
    std::vector<uint16_t> rx_len;  // length the node handed over
    void (*on_send)(void);   // runs inside the sink, before it answers (a producer racing the send)
};

static FakePhone phone;

static void phone_reset(uint16_t mtu)
{
    phone.mtu = mtu;
    phone.busy_calls = 0;
    phone.busy_marker = 0;
    phone.down = false;
    phone.calls = 0;
    phone.rx.clear();
    phone.rx_len.clear();
    phone.on_send = NULL;
}

static BlePhoneSend fake_sink(void *ctx, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    if (phone.on_send)
        phone.on_send();
    if (phone.down)
        return BLE_SEND_DOWN;
    phone.calls++;
    if (phone.busy_marker && buf[0] == phone.busy_marker)
        return BLE_SEND_BUSY;
    if (phone.busy_calls > 0)
    {
        phone.busy_calls--;
        return BLE_SEND_BUSY;
    }
    uint16_t lim = phone.mtu > 3 ? (uint16_t)(phone.mtu - 3) : 0;
    uint16_t n = len < lim ? len : lim;
    phone.rx.push_back(Bytes(buf, buf + n));
    phone.rx_len.push_back(len);
    return BLE_SEND_SENT;
}

// --- the pre-fix drain, kept as the regression model --------------------------
//
// sendToPhone() before BLE-N1: peek, POP, frame, send, ignore the result, and
// blelen+2 in a uint8_t.
static BlePhoneDrain oldDrainOne(byte_fifo_t *ring, BlePhoneSinkFn sink, uint8_t *wire, uint16_t wire_size)
{
    uint8_t snap[BLE_PHONE_SNAP_SIZE];
    uint8_t blelen = bf_peek(ring, snap, sizeof(snap));
    bf_pop(ring);
    if (blelen == 0)
        return BLE_DRAIN_EMPTY;
    memset(wire, 0, wire_size);
    if (!blePhoneFrame(snap, blelen, wire, wire_size))
        return BLE_DRAIN_BAD;
    blelen = (uint8_t)(blelen + 2);
    sink(NULL, wire, blelen);
    return BLE_DRAIN_SENT;
}

static BlePhoneDrain run_drain(byte_fifo_t *ring, BlePhoneDrainState *state)
{
    uint8_t wire[MAX_MSG_LEN_PHONE];
    if (old_logic)
        return oldDrainOne(ring, fake_sink, wire, sizeof(wire));
    return blePhoneDrainOne(ring, state, &st, fake_sink, NULL, phone.mtu, wire, sizeof(wire), NULL);
}

// Drain windows until the ring is empty (or max_windows); one call = one window.
static int drain_all(byte_fifo_t *ring, BlePhoneDrainState *state, int max_windows)
{
    int w = 0;
    while (w < max_windows && !bf_empty(ring))
    {
        run_drain(ring, state);
        w++;
    }
    return w;
}

// --- frame builders, mirroring the firmware producers -------------------------

static Bytes make_frame(uint8_t type, size_t len, uint8_t seed)
{
    Bytes f(len);
    for (size_t i = 0; i < len; i++)
        f[i] = (uint8_t)(seed * 31u + i * 7u + 1u);
    if (len)
        f[0] = type;
    return f;
}

// addBLEComToOutBuffer(): clamp 245, no time tag.
static int push_com(const Bytes &f, uint16_t *len_out = NULL)
{
    uint16_t len = (uint16_t)f.size();
    int lost = blePhonePush(&comRingT, &st, f.data(), &len, 245, NULL);
    if (len_out)
        *len_out = len;
    return lost;
}

// addBLEOutBuffer(): 'D' keeps 255, everything else clamps to 251 and gets the
// 4-byte time tag.
static int push_msg(const Bytes &f, uint32_t unix_time, uint16_t *len_out = NULL)
{
    uint16_t len = (uint16_t)f.size();
    int lost;
    if (f[0] != 'D')
    {
        uint8_t tag[4];
        blePhoneTimeTag(unix_time, tag);
        lost = blePhonePush(&phoneRingT, &st, f.data(), &len, UDP_TX_BUF_SIZE - 4, tag);
    }
    else
        lost = blePhonePush(&phoneRingT, &st, f.data(), &len, UDP_TX_BUF_SIZE, NULL);
    if (len_out)
        *len_out = len;
    return lost;
}

// The telegram the phone must see, built WITHOUT blePhoneFrame(): the wire
// contract of the app. `cell` is the ring cell (frame + tag as stored).
static Bytes expected_wire(const Bytes &cell)
{
    Bytes w;
    size_t blelen = cell.size();
    if (cell[0] == 0x91)                       // MH: type byte discarded from the count
    {
        w.assign(cell.begin(), cell.begin() + (blelen - 1));
    }
    else if (cell[0] == 0x44)                  // JSON as is
    {
        w = cell;
    }
    else                                       // text / position: 0x40 in front
    {
        w.push_back(0x40);
        w.insert(w.end(), cell.begin(), cell.end());
    }
    w.resize(blelen + 2, 0);                   // wire length blelen+2, pad is zero
    return w;
}

static void assert_bytes(const Bytes &want, const Bytes &got, const char *what)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "%s: length", what);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)want.size(), (uint32_t)got.size(), msg);
    snprintf(msg, sizeof(msg), "%s: bytes", what);
    if (!want.empty())
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want.data(), got.data(), want.size(), msg);
}

void setUp(void)
{
    bf_reset(&phoneRingT);
    bf_reset(&comRingT);
    memset(&st, 0, sizeof(st));
    memset(&phoneState, 0, sizeof(phoneState));
    memset(&comState, 0, sizeof(comState));
    phone_reset(247);
    old_logic = getenv("BLE_HARNESS_OLD_LOGIC") != NULL;
}

void tearDown(void) {}

// =============================================================================
// happy: the real config burst
// =============================================================================

struct Reg { const char *name; uint16_t len; };

// SN1 = 99 since TZ-01 (was 65): the frame is the type byte plus the JSON, and
// 65 + 34 = 99, where 34 = len(,"TZ":"<tz>") = 7 + 26 + 1 for the ESP32 default
// rule "CET-1CEST,M3.5.0,M10.5.0/3" (26 chars). The characters of a POSIX TZ
// (, / < > + - . :) need no JSON escaping. Worst case, a 39-char TZ with the
// longest VIACALL (39) and WSPWD (19): 1 + 167 JSON bytes = 168, still below
// BLE_JSON_PAYLOAD_MAX 244 (TZ is the last key, so fail-soft would drop it first).
static const Reg HELTEC[] = {
    {"I", 223}, {"IS1", 42}, {"SE", 224}, {"S1", 113}, {"SW", 129}, {"S2", 112},
    {"SN", 228}, {"SN1", 99}, {"W", 139}, {"G", 162}, {"SA", 59}, {"IO", 108},
    {"TM", 99}, {"AN", 132}, {"CONFFIN", 20}};

static const Reg RAK[] = {
    {"I", 223}, {"IS1", 42}, {"SE", 224}, {"S1", 113}, {"SW", 129}, {"S2", 112},
    {"SN", 228}, {"SN1", 99}, {"W", 139}, {"G", 162}, {"SA", 59}, {"IO", 108},
    {"TM", 99}, {"MH1", 206}, {"MH2", 207}, {"CONFFIN", 20}};

static void run_burst(const Reg *regs, size_t n)
{
    std::vector<Bytes> sent;
    for (size_t i = 0; i < n; i++)
    {
        // Register frames are 'D' JSON: buffer[0] = 0x44, length incl. the type byte.
        Bytes f = make_frame(0x44, regs[i].len, (uint8_t)(i + 1));
        uint16_t out_len = 0;
        int lost = push_com(f, &out_len);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, lost, regs[i].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(regs[i].len, out_len, regs[i].name);
        sent.push_back(f);
    }
    TEST_ASSERT_EQUAL_UINT16(n, bf_unread(&comRingT));

    // One frame per window, exactly n windows.
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&comRingT, &comState));
    TEST_ASSERT_TRUE(bf_empty(&comRingT));
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_EMPTY, run_drain(&comRingT, &comState));

    // Every frame once, in order, byte-exact.
    TEST_ASSERT_EQUAL_UINT32(n, (uint32_t)phone.rx.size());
    for (size_t i = 0; i < n; i++)
        assert_bytes(expected_wire(sent[i]), phone.rx[i], regs[i].name);

    if (old_logic)   // the pre-fix drain has no counters; only the bytes are comparable
        return;
    TEST_ASSERT_EQUAL_UINT32(n, st.sent);
    TEST_ASSERT_EQUAL_UINT32(0, st.retried);
    TEST_ASSERT_EQUAL_UINT32(0, st.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, st.truncated);
    TEST_ASSERT_EQUAL_UINT32(0, st.evicted);
    TEST_ASSERT_EQUAL_UINT16(247, st.last_mtu);
}

void test_happy_heltec_config_burst(void)
{
    run_burst(HELTEC, sizeof(HELTEC) / sizeof(HELTEC[0]));
}

void test_happy_rak_config_burst_with_mh_frames(void)
{
    run_burst(RAK, sizeof(RAK) / sizeof(RAK[0]));
}

void test_happy_message_ring_text_and_json_in_order(void)
{
    // Mesh text (':'), position ('!'), and a JSON ('D') mixed, as they arrive.
    // A 251 B text is 257 on the wire, so give the phone a big MTU here; the
    // cut at short MTUs has its own cases.
    phone_reset(512);
    std::vector<Bytes> cells;
    const uint8_t types[] = {':', '!', 'D', ':', '@', 'D', ':'};
    const uint16_t lens[] = {90, 40, 150, 200, 60, 20, 251};
    for (size_t i = 0; i < sizeof(types); i++)
    {
        Bytes f = make_frame(types[i], lens[i], (uint8_t)(i + 40));
        uint32_t t = 0x65A1B2C0u + (uint32_t)i;
        TEST_ASSERT_EQUAL_INT(0, push_msg(f, t));
        Bytes cell = f;
        if (types[i] != 'D')
        {
            uint8_t tag[4];
            blePhoneTimeTag(t, tag);
            cell.insert(cell.end(), tag, tag + 4);
        }
        cells.push_back(cell);
    }
    for (size_t i = 0; i < cells.size(); i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&phoneRingT, &phoneState));
    TEST_ASSERT_TRUE(bf_empty(&phoneRingT));
    TEST_ASSERT_EQUAL_UINT32(cells.size(), (uint32_t)phone.rx.size());
    // 'D' is 0x44, so make_frame's type 'D' takes the JSON arm; ':' '!' '@' the text arm.
    for (size_t i = 0; i < cells.size(); i++)
        assert_bytes(expected_wire(cells[i]), phone.rx[i], "msg frame");
}

void test_time_tag_is_big_endian(void)
{
    uint8_t tag[4];
    blePhoneTimeTag(0x01020304u, tag);
    TEST_ASSERT_EQUAL_UINT8(0x01, tag[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, tag[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, tag[2]);
    TEST_ASSERT_EQUAL_UINT8(0x04, tag[3]);
}

// =============================================================================
// edge: clamp, zero length, max text frame, MTU
// =============================================================================

void test_edge_com_clamp_at_245(void)
{
    const uint16_t in[] = {244, 245, 246, 255};
    const uint16_t want[] = {244, 245, 245, 245};
    for (size_t i = 0; i < 4; i++)
    {
        uint16_t out = 0;
        Bytes f = make_frame(0x44, in[i], (uint8_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(0, push_com(f, &out));
        TEST_ASSERT_EQUAL_UINT16(want[i], out);
    }
    // What arrives is the clamped frame, byte-exact, at MTU 251 (limit 248).
    phone_reset(251);
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&comRingT, &comState));
    for (size_t i = 0; i < 4; i++)
    {
        Bytes f = make_frame(0x44, in[i], (uint8_t)(i + 1));
        f.resize(want[i]);
        assert_bytes(expected_wire(f), phone.rx[i], "clamped com frame");
    }
}

void test_edge_msg_clamp_text_251_json_255(void)
{
    // non-D: 4 tag bytes count, so the payload clamps at 251; D keeps 255.
    const uint8_t types[] = {':', ':', ':', 'D', 'D'};
    const uint16_t in[] = {250, 251, 252, 255, 256};
    const uint16_t want[] = {250, 251, 251, 255, 255};
    for (size_t i = 0; i < 5; i++)
    {
        uint16_t out = 0;
        TEST_ASSERT_EQUAL_INT(0, push_msg(make_frame(types[i], in[i], (uint8_t)(i + 1)), 1000 + i, &out));
        TEST_ASSERT_EQUAL_UINT16(want[i], out);
    }
    TEST_ASSERT_EQUAL_UINT16(5, bf_unread(&phoneRingT));
}

void test_edge_zero_length_frame_never_enters(void)
{
    TEST_ASSERT_EQUAL_INT(-1, push_com(Bytes()) );
    // blePhonePush needs a non-null pointer only for len > 0; an empty vector's data() is fine.
    TEST_ASSERT_TRUE(bf_empty(&comRingT));
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_EMPTY, run_drain(&comRingT, &comState));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)phone.rx.size());
}

void test_edge_one_byte_frame(void)
{
    Bytes f(1, 0x44);
    TEST_ASSERT_EQUAL_INT(0, push_com(f));
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&comRingT, &comState));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)phone.rx.size());
    assert_bytes(expected_wire(f), phone.rx[0], "1-byte JSON");   // 0x44 00 00
    TEST_ASSERT_EQUAL_UINT16(3, phone.rx_len[0]);
}

void test_edge_max_text_frame_sends_257_bytes(void)
{
    // 251 B text + 4 B tag = 255 B cell, wire length 257. In the old uint8_t
    // arithmetic blelen+2 wrapped to 1 and the phone got ONE byte.
    Bytes f = make_frame(':', 251, 9);
    TEST_ASSERT_EQUAL_INT(0, push_msg(f, 0xDEADBEEFu));
    phone_reset(512);   // no truncation in this case
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&phoneRingT, &phoneState));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)phone.rx.size());
    TEST_ASSERT_EQUAL_UINT16(257, phone.rx_len[0]);

    Bytes cell = f;
    uint8_t tag[4];
    blePhoneTimeTag(0xDEADBEEFu, tag);
    cell.insert(cell.end(), tag, tag + 4);
    assert_bytes(expected_wire(cell), phone.rx[0], "max text frame");
}

// Frames > MTU-3 are counted, exactly. Wire length is blelen+2.
static void mtu_sweep(uint16_t mtu)
{
    setUp();
    phone_reset(mtu);
    // JSON frames of assorted cell lengths; through the message ring so 246..255 can be tested.
    const uint16_t lens[] = {1, 18, 19, 20, 100, 180, 182, 183, 241, 242, 243, 244, 245, 246, 255};
    uint32_t want_trunc = 0;
    size_t n = sizeof(lens) / sizeof(lens[0]);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, push_msg(make_frame('D', lens[i], (uint8_t)(i + 1)), 0));
        uint16_t lim = (uint16_t)(mtu - 3);
        if ((uint16_t)(lens[i] + 2) > lim)
            want_trunc++;
    }
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&phoneRingT, &phoneState));

    char msg[64];
    snprintf(msg, sizeof(msg), "truncated at MTU %u", (unsigned)mtu);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(want_trunc, st.truncated, msg);
    TEST_ASSERT_EQUAL_UINT32(n, st.sent);
    TEST_ASSERT_EQUAL_UINT16(mtu, st.last_mtu);

    // The phone side agrees: it received a cut frame exactly for those.
    uint32_t cut = 0;
    for (size_t i = 0; i < n; i++)
        if (phone.rx[i].size() < phone.rx_len[i])
            cut++;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(want_trunc, cut, msg);
    printf("MTU %3u: %u of %u frames longer than MTU-3, truncated counter %u\n",
           (unsigned)mtu, (unsigned)want_trunc, (unsigned)n, (unsigned)st.truncated);
}

void test_edge_truncation_counted_at_mtu_23(void)  { mtu_sweep(23); }
void test_edge_truncation_counted_at_mtu_185(void) { mtu_sweep(185); }
void test_edge_truncation_counted_at_mtu_247(void) { mtu_sweep(247); }

void test_edge_boundary_is_mtu_minus_3_inclusive(void)
{
    // wire length == MTU-3 fits, one more does not. MTU 247: limit 244, so a
    // cell of 242 (wire 244) fits and 243 (wire 245) is cut.
    TEST_ASSERT_EQUAL_INT(0, push_com(make_frame(0x44, 242, 1)));
    TEST_ASSERT_EQUAL_INT(0, push_com(make_frame(0x44, 243, 2)));
    run_drain(&comRingT, &comState);
    TEST_ASSERT_EQUAL_UINT32(0, st.truncated);
    run_drain(&comRingT, &comState);
    TEST_ASSERT_EQUAL_UINT32(1, st.truncated);
}

void test_edge_unknown_mtu_counts_nothing(void)
{
    phone_reset(0);   // caller does not know the MTU
    TEST_ASSERT_EQUAL_INT(0, push_msg(make_frame('D', 255, 1), 0));
    phone.mtu = 512;  // sink accepts everything; the drain was told 0 below
    uint8_t wire[MAX_MSG_LEN_PHONE];
    BlePhoneDrain r = blePhoneDrainOne(&phoneRingT, &phoneState, &st, fake_sink, NULL, 0,
                                       wire, sizeof(wire), NULL);
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, r);
    TEST_ASSERT_EQUAL_UINT32(0, st.truncated);
    TEST_ASSERT_EQUAL_UINT16(0, st.last_mtu);
}

void test_stats_format_is_compact(void)
{
    BlePhoneStats s = {12, 3, 1, 2, 4, 247};
    char out[72];
    blePhoneStatsFormat(&s, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("tx s12 r3 d1 t2 e4 mtu247", out);
    memset(&s, 0xFF, sizeof(s));
    blePhoneStatsFormat(&s, out, sizeof(out));
    TEST_ASSERT_TRUE(strlen(out) < sizeof(out));
}

// =============================================================================
// overrun: BUSY, DOWN, flood
// =============================================================================

// BLE-N1, the case that decides: the stack refuses k notifies, then takes them.
void test_overrun_busy_k_windows_then_sent_loses_nothing(void)
{
    for (int k = 0; k <= (int)BLE_PHONE_MAX_RETRIES; k++)
    {
        setUp();
        std::vector<Bytes> sent;
        for (int i = 0; i < 5; i++)
        {
            Bytes f = make_frame(0x44, 100 + i * 20, (uint8_t)(i + 1));
            TEST_ASSERT_EQUAL_INT(0, push_com(f));
            sent.push_back(f);
        }
        phone.busy_calls = k;   // the first k sink calls are refused
        int windows = drain_all(&comRingT, &comState, 100);

        char msg[64];
        snprintf(msg, sizeof(msg), "k=%d: frames received", k);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, (uint32_t)phone.rx.size(), msg);
        for (size_t i = 0; i < sent.size(); i++)
            assert_bytes(expected_wire(sent[i]), phone.rx[i], msg);
        if (old_logic)   // pre-fix drain: no retry, no counters -- the byte checks above are the proof
            continue;
        snprintf(msg, sizeof(msg), "k=%d: windows used", k);
        TEST_ASSERT_EQUAL_INT_MESSAGE(5 + k, windows, msg);
        snprintf(msg, sizeof(msg), "k=%d: retried counter", k);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)k, st.retried, msg);
        TEST_ASSERT_EQUAL_UINT32(0, st.dropped);
        TEST_ASSERT_EQUAL_UINT32(5, st.sent);
    }
}

// BLE-N1, the cap: the stack refuses one frame forever. It is dropped after
// BLE_PHONE_MAX_RETRIES retries, counted, and the frames behind it still flow.
void test_overrun_busy_forever_drops_after_cap_and_does_not_lock_up(void)
{
    Bytes a = make_frame(0x44, 100, 1);
    // The poison is a text frame: on the wire it starts with the 0x40 tag,
    // which the fake phone refuses for ever (busy_marker), the JSON frames
    // around it it takes.
    Bytes poison = make_frame(':', 90, 3);
    Bytes c = make_frame(0x44, 140, 4);
    TEST_ASSERT_EQUAL_INT(0, push_msg(a, 0));
    TEST_ASSERT_EQUAL_INT(0, push_msg(poison, 0));
    TEST_ASSERT_EQUAL_INT(0, push_msg(c, 0));
    phone.busy_marker = 0x40;   // the wire byte 0 of a text frame

    int windows = drain_all(&phoneRingT, &phoneState, 100);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)phone.rx.size(), "a and c arrive");
    assert_bytes(expected_wire(a), phone.rx[0], "a");
    assert_bytes(expected_wire(c), phone.rx[1], "c behind the dropped frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, st.dropped, "dropped counted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(BLE_PHONE_MAX_RETRIES, st.retried, "retries before the drop");
    // a (1) + poison (1 first try + cap retries + the dropping try) + c (1)
    TEST_ASSERT_EQUAL_INT(1 + (int)BLE_PHONE_MAX_RETRIES + 1 + 1, windows);
    TEST_ASSERT_TRUE(bf_empty(&phoneRingT));
    TEST_ASSERT_EQUAL_UINT32(2, st.sent);
}

// A DOWN link never pops and never counts against the frame.
void test_overrun_link_down_mid_burst_pops_nothing(void)
{
    std::vector<Bytes> sent;
    for (int i = 0; i < 8; i++)
    {
        Bytes f = make_frame(0x44, 60 + i * 10, (uint8_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(0, push_com(f));
        sent.push_back(f);
    }
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&comRingT, &comState));
    TEST_ASSERT_EQUAL_UINT16(5, bf_unread(&comRingT));

    phone.down = true;
    // Far more windows than the retry cap: DOWN is not a failure of the frame.
    for (int w = 0; w < 3 * (int)BLE_PHONE_MAX_RETRIES; w++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_DOWN, run_drain(&comRingT, &comState));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, bf_unread(&comRingT), "nothing popped while down");
    TEST_ASSERT_EQUAL_UINT32(0, st.dropped);
    TEST_ASSERT_EQUAL_UINT32(0, st.retried);

    phone.down = false;   // reconnect
    drain_all(&comRingT, &comState, 100);
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)phone.rx.size());
    for (size_t i = 0; i < sent.size(); i++)
        assert_bytes(expected_wire(sent[i]), phone.rx[i], "after reconnect");
    TEST_ASSERT_EQUAL_UINT32(8, st.sent);
}

// The retry budget belongs to the frame at the tail, not to the ring.
void test_retry_budget_follows_the_frame(void)
{
    // Frame A is refused 3 times, then a producer evicts it (ring flooded).
    Bytes a = make_frame(0x44, 200, 1);
    TEST_ASSERT_EQUAL_INT(0, push_com(a));
    phone.busy_calls = 1000;
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_RETRY, run_drain(&comRingT, &comState));

    // Fill until A is gone: each frame 246 B in the ring, 3072 / 246 = 12 fit.
    for (int i = 0; i < 13; i++)
        TEST_ASSERT_TRUE(push_com(make_frame(0x44, 245, (uint8_t)(10 + i))) >= 0);
    TEST_ASSERT_TRUE_MESSAGE(st.evicted >= 1, "A was evicted");

    // The new tail frame must get the FULL budget: refused cap times, still kept.
    for (int i = 0; i < (int)BLE_PHONE_MAX_RETRIES; i++)
        TEST_ASSERT_EQUAL_INT(BLE_DRAIN_RETRY, run_drain(&comRingT, &comState));
    TEST_ASSERT_EQUAL_UINT32(0, st.dropped);
    phone.busy_calls = 0;
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, run_drain(&comRingT, &comState));
}

// A producer evicting the tail frame WHILE it is being sent must not make the
// drain pop the next frame (bf_tail_gen guard).
void test_eviction_during_send_does_not_pop_the_next_frame(void)
{
    // A tiny private ring: two 120-byte frames fill it (2 * 121 = 242 of 260).
    static uint8_t tiny[260];
    byte_fifo_t ring = BYTE_FIFO_INIT(tiny);
    BlePhoneDrainState state = {0, 0, 0};
    Bytes a = make_frame(0x44, 120, 1);
    Bytes b = make_frame(0x44, 120, 2);
    TEST_ASSERT_EQUAL_INT(0, bf_push(&ring, a.data(), 120));
    TEST_ASSERT_EQUAL_INT(0, bf_push(&ring, b.data(), 120));

    static byte_fifo_t *ring_p;
    ring_p = &ring;
    // During the send of A, a producer pushes C: A (unread, being sent) is evicted.
    struct Racer { static void fire(void)
    {
        Bytes c = make_frame(0x44, 120, 3);
        bf_push(ring_p, c.data(), 120);
        phone.on_send = NULL;
    } };
    phone.on_send = Racer::fire;

    uint8_t wire[MAX_MSG_LEN_PHONE];
    BlePhoneDrain r = blePhoneDrainOne(&ring, &state, &st, fake_sink, NULL, phone.mtu, wire, sizeof(wire), NULL);
    TEST_ASSERT_EQUAL_INT(BLE_DRAIN_SENT, r);      // A did go out
    // B and C are still unread: the drain did NOT pop B.
    TEST_ASSERT_EQUAL_UINT16(2, bf_unread(&ring));
    uint8_t snap[256];
    TEST_ASSERT_EQUAL_UINT8(120, bf_peek(&ring, snap, sizeof(snap)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b.data(), snap, 120);
}

// The message ring gets flooded with mesh text while the 15-frame config burst
// drains at the real cadence (ESP32 esp32loop: com ring first, one frame per
// 300 ms; message ring one frame per 400 ms, only when the com ring is empty).
// The burst lives in its own ring, so it must arrive intact; what the flood
// costs is unread MESSAGE frames, and they are counted.
static void flood_run(int frames_per_second)
{
    setUp();
    // The burst.
    std::vector<Bytes> burst;
    for (size_t i = 0; i < sizeof(HELTEC) / sizeof(HELTEC[0]); i++)
    {
        Bytes f = make_frame(0x44, HELTEC[i].len, (uint8_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(0, push_com(f));
        burst.push_back(f);
    }

    // The flood: text frames with a running id in payload[1..2], 90 B payload
    // + 4 B time tag + 1 B length in the ring = 95 B per frame.
    const uint32_t cell = 90 + 4 + 1;
    const uint32_t capacity = RING_BYTES_PHONE / cell;
    uint32_t pushed = 0;
    uint32_t summed_lost = 0;
    uint32_t ble_wait = 0;
    const uint32_t tick = 100;
    const uint32_t burst_end = 15 * 300;           // first 15 com windows
    uint32_t now = 0;
    uint32_t acc = 0;                              // frames_per_second * tick / 1000, fractional

    while (!bf_empty(&comRingT) || now <= burst_end)
    {
        // producer (OnRxDone)
        if (now < burst_end)
        {
            acc += (uint32_t)frames_per_second * tick;
            while (acc >= 1000)
            {
                acc -= 1000;
                Bytes f = make_frame(':', 90, 0);
                f[1] = (uint8_t)(pushed >> 8);
                f[2] = (uint8_t)pushed;
                int lost = push_msg(f, 1000 + pushed);
                TEST_ASSERT_TRUE(lost >= 0);
                summed_lost += (uint32_t)lost;
                pushed++;
            }
        }
        // consumer, the esp32loop shape
        if (!bf_empty(&comRingT))
        {
            if (now - ble_wait >= 300)
            {
                run_drain(&comRingT, &comState);
                ble_wait = now;
            }
        }
        else if (!bf_empty(&phoneRingT))
        {
            if (now - ble_wait >= 400)
            {
                run_drain(&phoneRingT, &phoneState);
                ble_wait = now;
            }
        }
        now += tick;
        TEST_ASSERT_TRUE_MESSAGE(now < 60000, "burst never finished");
    }

    // The burst is intact: 15 frames, in order, byte-exact, nothing dropped.
    TEST_ASSERT_EQUAL_UINT32(burst.size(), (uint32_t)phone.rx.size());
    for (size_t i = 0; i < burst.size(); i++)
        assert_bytes(expected_wire(burst[i]), phone.rx[i], HELTEC[i].name);
    TEST_ASSERT_EQUAL_UINT32(0, st.dropped);

    // Nothing of the message ring was popped during the burst (com ring first),
    // so the ring holds exactly `capacity` frames and every push beyond that
    // evicted one unread frame.
    uint32_t want_evicted = pushed > capacity ? pushed - capacity : 0;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(want_evicted, st.evicted, "evicted counter vs model");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(want_evicted, summed_lost, "ring accounting");

    // Now drain the message ring: the SURVIVORS are the newest, in order.
    size_t burst_rx = phone.rx.size();
    drain_all(&phoneRingT, &phoneState, 1000);
    uint32_t survivors = (uint32_t)(phone.rx.size() - burst_rx);
    TEST_ASSERT_EQUAL_UINT32(pushed - want_evicted, survivors);
    for (uint32_t i = 0; i < survivors; i++)
    {
        const Bytes &w = phone.rx[burst_rx + i];
        uint32_t id = ((uint32_t)w[2] << 8) | w[3];   // wire: 0x40, ':', id_hi, id_lo
        TEST_ASSERT_EQUAL_UINT32(want_evicted + i, id);
    }

    printf("flood %2d msg/s during the 15-frame burst (%u ms): pushed %u, ring holds %u, "
           "evicted %u unread message frames, burst intact 15/15, dropped %u\n",
           frames_per_second, (unsigned)burst_end, (unsigned)pushed, (unsigned)capacity,
           (unsigned)st.evicted, (unsigned)st.dropped);
}

void test_overrun_flood_5_per_s_burst_intact_no_eviction(void)  { flood_run(5); }
void test_overrun_flood_10_per_s_burst_intact_evicts(void)      { flood_run(10); }
void test_overrun_flood_20_per_s_burst_intact_evicts(void)      { flood_run(20); }

// =============================================================================
// instrument check: the pre-fix drain loses the frame
// =============================================================================

void test_old_logic_loses_a_frame_on_busy(void)
{
    // Runs the OLD model directly, whatever BLE_HARNESS_OLD_LOGIC says.
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL_INT(0, push_com(make_frame(0x44, 100, (uint8_t)(i + 1))));
    phone.busy_calls = 1;   // the stack refuses the FIRST notify
    uint8_t wire[MAX_MSG_LEN_PHONE];
    while (!bf_empty(&comRingT))
        oldDrainOne(&comRingT, fake_sink, wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, (uint32_t)phone.rx.size(), "old logic: one of three frames lost");
}

void test_old_logic_wraps_the_length_of_a_max_text_frame(void)
{
    TEST_ASSERT_EQUAL_INT(0, push_msg(make_frame(':', 251, 5), 1));
    phone_reset(512);
    uint8_t wire[MAX_MSG_LEN_PHONE];
    oldDrainOne(&phoneRingT, fake_sink, wire, sizeof(wire));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)phone.rx.size());
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, phone.rx_len[0], "old logic: 255+2 wrapped to 1 byte");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_heltec_config_burst);
    RUN_TEST(test_happy_rak_config_burst_with_mh_frames);
    RUN_TEST(test_happy_message_ring_text_and_json_in_order);
    RUN_TEST(test_time_tag_is_big_endian);
    RUN_TEST(test_edge_com_clamp_at_245);
    RUN_TEST(test_edge_msg_clamp_text_251_json_255);
    RUN_TEST(test_edge_zero_length_frame_never_enters);
    RUN_TEST(test_edge_one_byte_frame);
    RUN_TEST(test_edge_max_text_frame_sends_257_bytes);
    RUN_TEST(test_edge_truncation_counted_at_mtu_23);
    RUN_TEST(test_edge_truncation_counted_at_mtu_185);
    RUN_TEST(test_edge_truncation_counted_at_mtu_247);
    RUN_TEST(test_edge_boundary_is_mtu_minus_3_inclusive);
    RUN_TEST(test_edge_unknown_mtu_counts_nothing);
    RUN_TEST(test_stats_format_is_compact);
    RUN_TEST(test_overrun_busy_k_windows_then_sent_loses_nothing);
    RUN_TEST(test_overrun_busy_forever_drops_after_cap_and_does_not_lock_up);
    RUN_TEST(test_overrun_link_down_mid_burst_pops_nothing);
    RUN_TEST(test_retry_budget_follows_the_frame);
    RUN_TEST(test_eviction_during_send_does_not_pop_the_next_frame);
    RUN_TEST(test_overrun_flood_5_per_s_burst_intact_no_eviction);
    RUN_TEST(test_overrun_flood_10_per_s_burst_intact_evicts);
    RUN_TEST(test_overrun_flood_20_per_s_burst_intact_evicts);
    RUN_TEST(test_old_logic_loses_a_frame_on_busy);
    RUN_TEST(test_old_logic_wraps_the_length_of_a_max_text_frame);
    return UNITY_END();
}
