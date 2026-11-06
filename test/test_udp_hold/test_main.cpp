// Host test for src/udp_hold.h: the text-message hold that survives a server
// outage.  pio test -e native_udp_hold
//
// The class is pure (time is a parameter), so every scenario runs in
// simulated milliseconds.  Frames are built the way addNodeData() builds them:
// 36-byte "DATA..." header, then the raw LoRa frame.  The text frame body is
// the hand-checked vector from test/test_aprs_epilogue (type 0x3A, "Hi").

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <udp_hold.h>

static uint8_t store[2048];
static UdpHold h;

static const uint8_t TEXT_BODY[] = {
    0x3A, 0x01, 0x00, 0x00, 0x00, 0x94, 'D', 'K', '5', 'E', 'N', '-', '9', '0', '>',
    '9', '9', '9', '9', 0x3A, 'H', 'i', 0x00, 0x09, 0x83, 0x05, 0x55, 35, 0x80 | 9, 'p', 0x7E,
};

// DATA frame: header + body. `type` overrides the LoRa type byte, `id` the msg
// id byte (to tell frames apart), `extra` pads the payload (bigger frames).
static uint16_t mk(uint8_t *out, uint8_t id, uint8_t type = 0x3A, uint16_t extra = 0)
{
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "DATA%08X%-9.9s%-4.4s%-1.1s%4i%4i03", 0xFFFFFFFFu, "DK5EN-99", "4.40", "a", -90, 7);
    memcpy(out, hdr, 36);
    memcpy(out + 36, TEXT_BODY, sizeof(TEXT_BODY));
    out[36] = type;
    out[37] = id;
    if (extra) memset(out + 36 + sizeof(TEXT_BODY), 'x', extra);
    return (uint16_t)(36 + sizeof(TEXT_BODY) + extra);
}

// DATA frame for an arbitrary text, laid out like encodeStartAPRS()/encodeAPRS():
//   0x3A | msg id (4) | hop (1) | "SRC>DEST:" | payload | 0x00 | hw mod fcs fcs fw lasthw '#' 0x7E
static uint16_t mkText(uint8_t *out, const char *src, const char *dest, const char *payload, uint8_t id = 1)
{
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "DATA%08X%-9.9s%-4.4s%-1.1s%4i%4i03", 0xFFFFFFFFu, "DK5EN-99", "4.40", "a", -90, 7);
    memcpy(out, hdr, 36);
    uint16_t n = 36;
    out[n++] = 0x3A;
    out[n++] = id; out[n++] = 0; out[n++] = 0; out[n++] = 0;
    out[n++] = 0x94;
    n += (uint16_t)snprintf((char *)out + n, 200, "%s>%s:%s", src, dest, payload);
    const uint8_t tail[] = { 0x00, 0x04, 0x03, 0x05, 0x55, 35, 0x04, '#', 0x7E };
    memcpy(out + n, tail, sizeof(tail));
    return (uint16_t)(n + sizeof(tail));
}

static const uint16_t FRAME_LEN = 36 + sizeof(TEXT_BODY);          // 67
static const uint16_t ENTRY = FRAME_LEN + UdpHold::ENTRY_OVERHEAD; // 72

static bool pushId(uint8_t id, uint32_t now, uint16_t extra = 0)
{
    uint8_t f[300];
    uint16_t n = mk(f, id, 0x3A, extra);
    return h.push(f, n, now);
}

// pull the next due replay frame, return its id byte or -1
static int pull(uint32_t now)
{
    uint8_t out[256];
    uint16_t n = 0;
    if (!h.nextReplay(now, out, sizeof(out), &n)) return -1;
    return out[37];
}

void setUp(void) { memset(store, 0xEE, sizeof(store)); h.init(store, sizeof(store)); }
void tearDown(void) {}

static void test_no_outage_releases_without_replay(void)
{
    h.signOfLife(0);
    pushId(1, 10000);
    h.signOfLife(30000);                        // frame only 20 s old: kept
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    h.signOfLife(60000);                        // 50 s old, server alive both sides: released
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.released());
    TEST_ASSERT_FALSE(h.replaying());
    TEST_ASSERT_EQUAL_INT(-1, pull(60000));
    TEST_ASSERT_EQUAL_INT(-1, pull(70000));
    TEST_ASSERT_EQUAL_UINT32(0, h.replayed());
    TEST_ASSERT_EQUAL_UINT8(0, h.takeEvents());
}

static void test_outage_then_recovery_replays_oldest_first_paced(void)
{
    h.signOfLife(0);
    h.signOfLife(30000);
    pushId(1, 40000); pushId(2, 50000); pushId(3, 60000);
    h.step(95000);                              // exactly 65 s: not yet an outage
    TEST_ASSERT_FALSE(h.outage());
    h.step(95001);
    TEST_ASSERT_TRUE(h.outage());
    TEST_ASSERT_EQUAL_UINT8(UdpHold::EV_OUTAGE_START, h.takeEvents());
    pushId(4, 120000);
    h.signOfLife(200000);                       // first sign: not enough
    TEST_ASSERT_FALSE(h.outage());
    TEST_ASSERT_TRUE(h.recovering());
    TEST_ASSERT_EQUAL_INT(-1, pull(200000));
    h.signOfLife(204999);                       // 1 ms short of the confirmation
    TEST_ASSERT_EQUAL_INT(-1, pull(204999));
    h.signOfLife(205000);                       // confirmed: replay starts
    TEST_ASSERT_TRUE(h.replaying());
    TEST_ASSERT_EQUAL_INT(1, pull(205000));     // oldest first, immediately
    TEST_ASSERT_EQUAL_INT(-1, pull(206999));    // 2 s pacing
    TEST_ASSERT_EQUAL_INT(2, pull(207000));
    TEST_ASSERT_EQUAL_INT(3, pull(209000));
    TEST_ASSERT_EQUAL_INT(4, pull(211000));
    TEST_ASSERT_FALSE(h.replaying());
    TEST_ASSERT_EQUAL_UINT32(4, h.replayed());
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.outages());
}

static void test_replay_sends_identical_bytes(void)
{
    uint8_t f[300], out[256];
    uint16_t n = mk(f, 9), got = 0;
    h.signOfLife(0);
    h.push(f, n, 1000);
    h.step(70000);
    h.signOfLife(100000); h.signOfLife(106000);
    TEST_ASSERT_TRUE(h.nextReplay(106000, out, sizeof(out), &got));
    TEST_ASSERT_EQUAL_UINT16(n, got);
    TEST_ASSERT_EQUAL_MEMORY(f, out, n);
    TEST_ASSERT_EQUAL_UINT32(1, h.pushed());         // replay did not re-enter the hold
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
}

static void test_single_sign_does_not_replay_and_new_outage_keeps_frames(void)
{
    h.signOfLife(0);
    pushId(1, 10000);
    h.step(70000);
    h.signOfLife(100000);                       // one stray sign (e.g. ETH bring-up)
    h.step(170000);                             // nothing more: outage again
    TEST_ASSERT_TRUE(h.outage());
    TEST_ASSERT_FALSE(h.recovering());
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(0, h.replayed());
    h.signOfLife(300000); h.signOfLife(310000);
    TEST_ASSERT_EQUAL_INT(1, pull(310000));
}

static void test_drop_oldest_when_full_and_exact_capacity(void)
{
    uint8_t s[ENTRY * 2];
    h.init(s, sizeof(s));                       // exactly two entries
    TEST_ASSERT_TRUE(pushId(1, 100));
    TEST_ASSERT_TRUE(pushId(2, 200));
    TEST_ASSERT_EQUAL_UINT16(2, h.held());
    TEST_ASSERT_EQUAL_UINT16(sizeof(s), h.usedBytes());
    TEST_ASSERT_EQUAL_UINT32(0, h.droppedFull());
    TEST_ASSERT_TRUE(pushId(3, 300));           // one byte over: oldest goes
    TEST_ASSERT_EQUAL_UINT16(2, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.droppedFull());
    h.signOfLife(0); h.step(70000); h.signOfLife(80000); h.signOfLife(90000);
    TEST_ASSERT_EQUAL_INT(2, pull(90000));
    TEST_ASSERT_EQUAL_INT(3, pull(92000));
}

static void test_single_entry_exact_fit_and_too_big(void)
{
    uint8_t s[ENTRY];
    h.init(s, sizeof(s));
    TEST_ASSERT_TRUE(pushId(1, 0));             // fits to the byte
    TEST_ASSERT_TRUE(pushId(2, 1));             // replaces it
    TEST_ASSERT_EQUAL_UINT32(1, h.droppedFull());
    TEST_ASSERT_FALSE(pushId(3, 2, 1));         // one byte bigger than the whole buffer
    TEST_ASSERT_EQUAL_UINT16(1, h.held());      // refused frame did not evict anything
    TEST_ASSERT_EQUAL_UINT32(1, h.refused());
}

static void test_big_frame_evicts_as_many_as_needed(void)
{
    uint8_t s[200];
    h.init(s, sizeof(s));
    pushId(1, 0); pushId(2, 1);                 // 144 B used, 56 B free
    TEST_ASSERT_TRUE(pushId(3, 2, 33));         // entry 105: one drop is enough
    TEST_ASSERT_EQUAL_UINT16(2, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.droppedFull());
    h.init(s, sizeof(s));
    pushId(1, 0); pushId(2, 1);
    TEST_ASSERT_TRUE(pushId(3, 2, 83));         // entry 155: both older ones must go
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(2, h.droppedFull());
    TEST_ASSERT_EQUAL_UINT16(155, h.usedBytes());
}

static void test_age_expiry(void)
{
    h.signOfLife(0);
    pushId(1, 1000); pushId(2, 2000);
    h.step(1000 + UdpHold::MAX_AGE_MS);         // exactly 20 min: still held
    TEST_ASSERT_EQUAL_UINT16(2, h.held());
    h.step(1001 + UdpHold::MAX_AGE_MS);         // first one older than 20 min
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.droppedAge());
    TEST_ASSERT_TRUE(h.takeEvents() & UdpHold::EV_DROP_AGE);
    h.step(2001 + UdpHold::MAX_AGE_MS);
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(2, h.droppedAge());
}

static void test_millis_wrap(void)
{
    const uint32_t t0 = 0xFFFFFF00u;            // 256 ms before the wrap
    h.signOfLife(t0 - 20000);
    pushId(1, t0);
    h.signOfLife(t0 + 10000);                   // wrapped: 10 s old, kept
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    h.signOfLife(t0 + 40000);                   // 40 s old across the wrap: released
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    // outage + replay across the wrap
    pushId(2, t0 + 41000);
    h.step(t0 + 41000 + 70000);
    TEST_ASSERT_TRUE(h.outage());
    h.signOfLife(t0 + 150000); h.signOfLife(t0 + 156000);
    TEST_ASSERT_EQUAL_INT(2, pull(t0 + 156000));
    // age across the wrap
    h.init(store, sizeof(store));
    h.signOfLife(t0);
    pushId(5, t0);
    h.step(t0 + UdpHold::MAX_AGE_MS);
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    h.step(t0 + UdpHold::MAX_AGE_MS + 1);
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
}

static void test_new_outage_mid_replay_keeps_the_rest(void)
{
    h.signOfLife(0);
    pushId(1, 1000); pushId(2, 2000); pushId(3, 3000);
    h.step(70000);
    h.signOfLife(100000); h.signOfLife(106000);
    TEST_ASSERT_EQUAL_INT(1, pull(106000));
    h.step(106000 + 65001);                     // server gone again
    TEST_ASSERT_TRUE(h.outage());
    TEST_ASSERT_FALSE(h.replaying());
    TEST_ASSERT_EQUAL_INT(-1, pull(200000));
    TEST_ASSERT_EQUAL_UINT16(2, h.held());
    h.signOfLife(300000); h.signOfLife(305000);
    TEST_ASSERT_EQUAL_INT(2, pull(305000));
    TEST_ASSERT_EQUAL_INT(3, pull(307000));
    TEST_ASSERT_EQUAL_UINT32(3, h.replayed());
}

static void test_frames_pushed_during_replay_are_not_replayed(void)
{
    h.signOfLife(0);
    pushId(1, 1000);
    h.step(70000);
    h.signOfLife(100000); h.signOfLife(106000);
    pushId(2, 106500);                          // sent normally, arrives mid-replay
    TEST_ASSERT_EQUAL_INT(1, pull(106500));
    TEST_ASSERT_EQUAL_INT(-1, pull(110000));    // replay is over after the frames of the start
    TEST_ASSERT_FALSE(h.replaying());
    TEST_ASSERT_EQUAL_UINT16(1, h.held());      // normal release rule handles it later
    h.signOfLife(120000); h.signOfLife(140000);
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.replayed());
}

static void test_missed_beat_gap_replays_overlap(void)
{
    h.signOfLife(0);
    h.signOfLife(30000);
    pushId(1, 31000);
    h.signOfLife(31000 + 40000);                // 41 s gap (> 35 s), no outage declared
    TEST_ASSERT_TRUE(h.recovering());
    h.signOfLife(31000 + 70000);
    TEST_ASSERT_EQUAL_INT(1, pull(101000));
}

static void test_eviction_during_replay_shrinks_the_replay(void)
{
    uint8_t s[ENTRY * 3];
    h.init(s, sizeof(s));
    h.signOfLife(0);
    pushId(1, 1000); pushId(2, 2000); pushId(3, 3000);
    h.step(70000);
    h.signOfLife(100000); h.signOfLife(106000);
    pushId(4, 106500);                          // ring full: frame 1 is evicted from the replay
    TEST_ASSERT_EQUAL_INT(2, pull(106500));
    TEST_ASSERT_EQUAL_INT(3, pull(108500));
    TEST_ASSERT_EQUAL_INT(-1, pull(120000));    // 4 was sent normally, not part of the replay
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(2, h.replayed());
}

static void test_only_text_frames(void)
{
    uint8_t f[300];
    uint16_t n;
    n = mk(f, 1, 0x3A);  TEST_ASSERT_TRUE(h.push(f, n, 0));        // text
    n = mk(f, 2, 0x21);  TEST_ASSERT_FALSE(h.push(f, n, 0));       // position
    n = mk(f, 3, 0x40);  TEST_ASSERT_FALSE(h.push(f, n, 0));       // HEY
    n = mk(f, 4, 0x41);  TEST_ASSERT_FALSE(h.push(f, n, 0));       // ACK
    n = mk(f, 5, 0x00);  TEST_ASSERT_FALSE(h.push(f, n, 0));
    n = mk(f, 6, 0x3A);  f[0] = 'K'; f[1] = 'E'; f[2] = 'E'; f[3] = 'P';
    TEST_ASSERT_FALSE(h.push(f, n, 0));                            // not a DATA datagram
    n = mk(f, 7, 0x3A);  memcpy(f, "STOR", 4);
    TEST_ASSERT_FALSE(h.push(f, n, 0));
    n = mk(f, 8, 0x3A);  TEST_ASSERT_FALSE(h.push(f, 36, 0));      // header only
    TEST_ASSERT_FALSE(h.push(f, 37 - 1, 0));
    // the real KEEP datagram as sendKEEP() builds it (incl. NUL)
    const uint8_t keep[] = "KEEPFFFFFFFFDK5EN-99 4.40a";
    TEST_ASSERT_FALSE(h.push(keep, sizeof(keep), 0));
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(9, h.refused());
    TEST_ASSERT_TRUE(UdpHold::isText(f, 0) == false);
    TEST_ASSERT_FALSE(UdpHold::isText(NULL, 100));
}

static void test_zero_and_oversize_refused(void)
{
    uint8_t f[400];
    uint16_t n = mk(f, 1, 0x3A, 200);                                // 267 B
    TEST_ASSERT_EQUAL_UINT16(267, n);
    TEST_ASSERT_FALSE(h.push(f, 0, 0));
    TEST_ASSERT_FALSE(h.push(f, 256, 0));
    TEST_ASSERT_FALSE(h.push(f, n, 0));
    n = mk(f, 1, 0x3A, 255 - FRAME_LEN);
    TEST_ASSERT_EQUAL_UINT16(255, n);
    TEST_ASSERT_TRUE(h.push(f, n, 0));                               // the maximum is fine
    TEST_ASSERT_EQUAL_UINT32(3, h.refused());
    UdpHold none;
    none.init(NULL, 0);
    TEST_ASSERT_FALSE(none.push(f, n, 0));                           // no buffer: refuses, no crash
}

///////////////////////////////////////////////////////////////////////////////
// runtime glue (tick, bench, synthetic writes)

static int s_sent;
static uint8_t s_last[300];
static uint16_t s_lastLen;
static int s_logs;
static char s_logbuf[600];
static int s_lock, s_unlock;
static void sink(uint8_t *b, uint16_t n) { s_sent++; s_lastLen = n; memcpy(s_last, b, n); }
static void logf_(const char *l) { s_logs++; strncat(s_logbuf, l, sizeof(s_logbuf) - strlen(s_logbuf) - 1); }
static void lk(void) { s_lock++; }
static void ulk(void) { s_unlock++; }

static void glue_reset(void)
{
    UdpHoldRt &r = udpHoldRt();
    memset(&r, 0, sizeof(r));
    s_sent = s_logs = s_lock = s_unlock = 0;
    s_logbuf[0] = 0;
    udpHoldAttach(store, sizeof(store), sink, logf_, lk, ulk);
}

static void test_glue_tick_detects_timer_changes_and_replays(void)
{
    uint8_t f[300];
    glue_reset();
    udpHoldTick(0, 0);                          // first tick only records the timer
    udpHoldTick(1000, 1000);                    // sign of life
    uint16_t n = mk(f, 1);
    udpHoldCapture(f, n, 5000);
    f[37] = 2; udpHoldCapture(f, n, 6000);
    mk(f, 3, 0x21); udpHoldCapture(f, n, 7000);  // position: never held
    TEST_ASSERT_EQUAL_UINT16(2, udpHoldRt().hold.held());
    udpHoldTick(1000, 80000);                   // no change: outage
    TEST_ASSERT_TRUE(udpHoldRt().hold.outage());
    TEST_ASSERT_NOT_NULL(strstr(s_logbuf, "[GW];hold;outage;start;"));
    udpHoldTick(90000, 90000);                  // sign
    udpHoldTick(90000, 94000);
    TEST_ASSERT_EQUAL_INT(0, s_sent);
    udpHoldTick(95000, 95000);                  // confirmed: first frame goes out now
    TEST_ASSERT_EQUAL_INT(1, s_sent);
    TEST_ASSERT_EQUAL_UINT8(1, s_last[37]);
    udpHoldTick(95000, 96000);
    TEST_ASSERT_EQUAL_INT(1, s_sent);
    udpHoldTick(95000, 97000);
    TEST_ASSERT_EQUAL_INT(2, s_sent);
    TEST_ASSERT_EQUAL_UINT8(2, s_last[37]);
    TEST_ASSERT_NOT_NULL(strstr(s_logbuf, "[GW];hold;outage;end;"));
    TEST_ASSERT_NOT_NULL(strstr(s_logbuf, "[GW];hold;replay;start;n;2"));
    TEST_ASSERT_NOT_NULL(strstr(s_logbuf, "[GW];hold;replay;done;replayed;2"));
    TEST_ASSERT_EQUAL_INT(s_lock, s_unlock);
    TEST_ASSERT_TRUE(s_lock > 0);
    char st[200];
    udpHoldStatusLine(st, sizeof(st));
    TEST_ASSERT_NOT_NULL(strstr(st, "[GW];hold;state;ok;held;0;"));
}

static void test_glue_bench_outage_and_synthetic_writes_are_no_sign(void)
{
    glue_reset();
    udpHoldTick(500, 500);
    udpHoldTick(1000, 1000);
    udpHoldRt().benchOutage = true;
    TEST_ASSERT_TRUE(udpHoldBenchOutage());
    for (uint32_t t = 2000; t < 100000; t += 2000)
        udpHoldTick(t, t);                      // server "frames" keep arriving, ignored
    TEST_ASSERT_TRUE(udpHoldRt().hold.outage());
    udpHoldRt().benchOutage = false;
    // watchdog rewrites the timer: noted, no sign of life
    udpHoldNoteSynthetic(123456);
    udpHoldTick(123456, 110000);
    TEST_ASSERT_TRUE(udpHoldRt().hold.outage());
    udpHoldTick(123457, 111000);                // a real change counts again
    TEST_ASSERT_FALSE(udpHoldRt().hold.outage());
}

static void test_glue_off_is_a_no_op(void)
{
    UdpHoldRt &r = udpHoldRt();
    memset(&r, 0, sizeof(r));
    uint8_t f[300];
    uint16_t n = mk(f, 1);
    udpHoldCapture(f, n, 0);
    udpHoldTick(1, 1);
    udpHoldTick(2, 100000);
    char st[100];
    udpHoldStatusLine(st, sizeof(st));
    TEST_ASSERT_EQUAL_STRING("[GW];hold;state;off", st);
    TEST_ASSERT_EQUAL_UINT16(0, r.hold.held());
}

///////////////////////////////////////////////////////////////////////////////
// what is worth delivering late (isHoldable), real frame bytes

static bool holdable(const char *src, const char *dest, const char *payload)
{
    uint8_t f[300];
    uint16_t n = mkText(f, src, dest, payload);
    return UdpHold::isHoldable(f, n);
}

static void test_holdable_text_is_kept(void)
{
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "*", "Hallo Welt"));
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "9999", "Hi group"));
    TEST_ASSERT_TRUE(holdable("OE1ABC-1,DK5EN-12", "OE3XYZ-2", "dm text{123"));    // via path
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "WLNK-1", "winlink payload"));           // late is what they want
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "BBS", "BBS post: text with a colon"));
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "RM1x not a command"));      // only "RM1 " is one
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "see {ping} later"));        // not a prefix
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "thanks ack123"));           // not an ACK
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "ack123"));                  // text only starting with "ack": 6 chars
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "RM1"));                     // too short for a command
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "OE3XYZ-2", "100001"));                  // payload, not the destination
    // the shared test body ("Hi" to 9999)
    uint8_t f[300];
    uint16_t n = mk(f, 1);
    TEST_ASSERT_TRUE(UdpHold::isHoldable(f, n));
}

static void test_rm1_command_and_reply_are_refused(void)
{
    // command: RM1 <ctr> <cmd...> <tag>{NNN, as rmBuildCommand()/sendMessage() put it on air
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "DK5EN-12", "RM1 123 reboot 0123456789abcdef{123"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "DK5EN-12", "RM1 124 set txpower 5 fedcba9876543210{045"));
    // reply: RM1 <ctr> <result> <tag>
    TEST_ASSERT_FALSE(holdable("DK5EN-12", "DK5EN-99", "RM1 123 ok 0123456789abcdef{001"));
    TEST_ASSERT_FALSE(holdable("DK5EN-12", "DK5EN-99", "RM1 123 err 0123456789abcdef"));
    // refused frames are counted and not stored
    uint8_t f[300];
    uint16_t n = mkText(f, "DK5EN-99", "DK5EN-12", "RM1 123 reboot 0123456789abcdef{123");
    TEST_ASSERT_FALSE(h.push(f, n, 0));
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.refused());
}

static void test_telemetry_acks_ping_pong_sota_are_refused(void)
{
    // telemetry: dest 100001, payload "<CALL 9>:T#..." / ":PARM." (loop_functions.cpp)
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "100001", "DK5EN-99 :T#001,12,34,56,78,90,00000000"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "100001", "DK5EN-99 :PARM.Temp,Hum,Press,Gas,Alt"));
    // text ACK to a call and to WLNK-1 (SendAckMessage())
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "OE3XYZ-2", "DK5EN-99 :ack123"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "WLNK-1", "ack0123"));
    // {ping} / {pong}
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "OE3XYZ-2", "{ping}"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "OE3XYZ-2", "{pong}{123}"));
    // APRS2SOTA
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "APRS2SOTA", "DK5EN-99 : OE/TI-001 145.500 FM"));
}

static void test_unparseable_frames_are_refused(void)
{
    uint8_t f[300];
    uint16_t n;
    // no '>' at all
    n = mkText(f, "DK5EN-99", "X", "y");
    uint8_t *gt = (uint8_t *)memchr(f + 42, '>', n - 42);
    TEST_ASSERT_NOT_NULL(gt);
    TEST_ASSERT_TRUE(UdpHold::isHoldable(f, n));
    *gt = '#';
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, n));
    // no ':' after the destination
    n = mkText(f, "DK5EN-99", "OE3XYZ-2", "text");
    uint8_t *co = (uint8_t *)memchr(f + 42, ':', n - 42);
    *co = '#';
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, n));
    // empty source / empty destination
    TEST_ASSERT_FALSE(holdable("", "OE3XYZ-2", "text"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "", "text"));
    // truncated right behind the header, and right behind the id/hop bytes
    n = mkText(f, "DK5EN-99", "*", "text");
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, 36 + 6));
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, 36 + 6 + 3));
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, 36 + 6 + 8));      // "DK5EN-99" without '>'
    // NUL inside the source path
    n = mkText(f, "DK5EN-99", "*", "text");
    f[44] = 0x00;
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, n));
    // NUL as the first byte after the id: the old type-only test held it
    n = mkText(f, "DK5EN-99", "*", "text");
    f[42] = 0x00;
    TEST_ASSERT_FALSE(UdpHold::isHoldable(f, n));
}

static void test_capture_reports_the_class(void)
{
    uint8_t f[300];
    uint16_t n;
    UdpHoldRt &r = udpHoldRt();
    memset(&r, 0, sizeof(r));
    n = mkText(f, "DK5EN-99", "*", "text");
    TEST_ASSERT_FALSE(udpHoldCapture(f, n, 0));                 // not attached
    TEST_ASSERT_FALSE(udpHoldLoraHoldable(f + 36, n - 36));
    udpHoldAttach(store, sizeof(store), NULL, NULL, NULL, NULL);
    TEST_ASSERT_TRUE(udpHoldCapture(f, n, 0));
    TEST_ASSERT_TRUE(udpHoldLoraHoldable(f + 36, n - 36));
    n = mkText(f, "DK5EN-99", "DK5EN-12", "RM1 123 reboot 0123456789abcdef{123");
    TEST_ASSERT_FALSE(udpHoldCapture(f, n, 0));
    TEST_ASSERT_FALSE(udpHoldLoraHoldable(f + 36, n - 36));
    TEST_ASSERT_EQUAL_UINT16(1, r.hold.held());
}

static void test_max_age_is_20_minutes(void)
{
    TEST_ASSERT_EQUAL_UINT32(20UL * 60UL * 1000UL, UdpHold::MAX_AGE_MS);
    TEST_ASSERT_EQUAL_UINT32(35000UL, UdpHold::SUSPECT_GAP_MS);   // decision: unchanged
}

static void test_frame_held_without_ip_is_replayed_not_released(void)
{
    // Text captured during a link drop never went out. The drop is too short to
    // make a sign gap suspect (signs 30 s apart), so without the marker the
    // normal rule would release the frame as delivered.
    uint8_t f[300], out[300];
    uint16_t n = mkText(f, "DK5EN-99", "DK5EN-12", "hello{123");
    h.signOfLife(0);
    h.signOfLife(30000);
    TEST_ASSERT_TRUE(h.push(f, n, 35000, false));   // no IP: not sent
    h.signOfLife(60000);
    h.signOfLife(90000);
    TEST_ASSERT_EQUAL_UINT32(0, h.released());
    uint16_t got = 0;
    TEST_ASSERT_TRUE(h.nextReplay(90000, out, sizeof(out), &got));
    TEST_ASSERT_EQUAL_UINT16(n, got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f, out, n);
}

static void test_frame_sent_with_ip_is_still_released(void)
{
    uint8_t f[300];
    uint16_t n = mkText(f, "DK5EN-99", "DK5EN-12", "hello{123");
    h.signOfLife(0);
    h.signOfLife(30000);
    TEST_ASSERT_TRUE(h.push(f, n, 35000, true));
    h.signOfLife(60000);
    h.signOfLife(90000);
    TEST_ASSERT_EQUAL_UINT32(1, h.released());
    TEST_ASSERT_FALSE(h.replaying());
}

static void test_node_control_texts_are_refused(void)
{
    // A late {MCP} switches a GPIO after the operator gave up, {SET} changes hop
    // settings, {CET} sets an old clock: same class as RM1.
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "DK5EN-12", "{MCP}a0 on{123"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "DK5EN-12", "{mcp}a0 on{123"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "DK5EN-12", "{SET}hop 5{123"));
    TEST_ASSERT_FALSE(holdable("DK5EN-99", "*", "{CET}2026-11-06 10:00{123"));
    TEST_ASSERT_TRUE(holdable("DK5EN-99", "DK5EN-12", "{MCP is just text{123"));
}

static void test_stamp_ahead_of_now_is_not_released(void)
{
    // A capture between the tick's millis() and the lock stamps the frame ahead
    // of the `now` the sign of life carries. Unsigned underflow made that frame
    // look ~49 days old and released it undelivered.
    h.signOfLife(0);
    h.signOfLife(30000);
    pushId(1, 60500);
    h.signOfLife(60000);                         // stamp 500 ms ahead of now
    TEST_ASSERT_EQUAL_UINT16(1, h.held());
    TEST_ASSERT_EQUAL_UINT32(0, h.released());
    h.signOfLife(90600);                         // now it is 30.1 s old: released as usual
    TEST_ASSERT_EQUAL_UINT16(0, h.held());
    TEST_ASSERT_EQUAL_UINT32(1, h.released());
}

static void test_glue_tick_without_ip_does_not_replay(void)
{
    uint8_t f[300];
    glue_reset();
    udpHoldTick(0, 0);
    udpHoldTick(1000, 1000);
    uint16_t n = mk(f, 1);
    udpHoldCapture(f, n, 5000);
    udpHoldTick(1000, 80000);                   // outage
    udpHoldTick(90000, 90000);
    udpHoldTick(95000, 95000, false);           // confirmed, but no IP address
    udpHoldTick(95000, 97000, false);
    udpHoldTick(95000, 99000, false);
    TEST_ASSERT_EQUAL_INT(0, s_sent);
    TEST_ASSERT_EQUAL_UINT16(1, udpHoldRt().hold.held());       // not popped
    TEST_ASSERT_TRUE(udpHoldRt().hold.replaying());
    udpHoldTick(95000, 100000, true);           // IP is back
    TEST_ASSERT_EQUAL_INT(1, s_sent);
    TEST_ASSERT_EQUAL_UINT16(0, udpHoldRt().hold.held());
}

static void test_glue_synthetic_write_masks_the_replay_start(void)
{
    // nRF52: upstream router dead, ten send failures, resetDHCP, ReopenUdp ->
    // startUDP rewrites last_upd_timer. The tick must not read that as the
    // server answering: the held text would be released as delivered.
    uint8_t f[300];
    glue_reset();
    udpHoldTick(0, 0);
    udpHoldTick(1000, 1000);
    uint16_t n = mk(f, 1);
    udpHoldCapture(f, n, 5000);
    udpHoldTick(1000, 80000);                   // outage
    TEST_ASSERT_TRUE(udpHoldRt().hold.outage());
    udpHoldNoteSynthetic(90000);                // startUDP(): last_upd_timer = millis()
    udpHoldTick(90000, 90000);
    udpHoldNoteSynthetic(96000);                // static re-apply writes it again
    udpHoldTick(96000, 96000);
    TEST_ASSERT_TRUE(udpHoldRt().hold.outage());
    TEST_ASSERT_FALSE(udpHoldRt().hold.recovering());
    TEST_ASSERT_EQUAL_UINT16(1, udpHoldRt().hold.held());
    TEST_ASSERT_EQUAL_INT(0, s_sent);
}

// The four synthetic writers of the nRF52 ETH layer (startUDP, static re-apply,
// hardware-reset re-apply, fixed-IP bring-up) are not host-compilable: this
// scans the source. Every `last_upd_timer = millis();` in nrf_eth.cpp must be
// followed by udpHoldNoteSynthetic() on the next code line.
static int scan_unmasked_timer_writes(const char *path, int *writes)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char line[512];
    int bad = 0;
    bool pending = false;
    *writes = 0;
    while (fgets(line, sizeof(line), fp))
    {
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (*t == '\n' || *t == '\0') continue;
        if (pending && strncmp(t, "udpHoldNoteSynthetic(", 21) != 0) bad++;
        pending = false;
        if (strncmp(t, "last_upd_timer = millis();", 26) == 0)
        {
            pending = true;
            (*writes)++;
        }
    }
    if (pending) bad++;
    fclose(fp);
    return bad;
}

static void test_nrf_eth_synthetic_timer_writes_are_masked(void)
{
    int writes = 0;
    int bad = scan_unmasked_timer_writes("src/nrf52/nrf_eth.cpp", &writes);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, bad, "src/nrf52/nrf_eth.cpp not readable from the project dir");
    TEST_ASSERT_EQUAL_INT(4, writes);
    TEST_ASSERT_EQUAL_INT(0, bad);
}

static void test_status_line_copies_under_one_lock(void)
{
    // one balanced lock round, the line comes from the copied counters (that snprintf
    // runs outside the critical section is by reading only; not observable here)
    uint8_t f[300];
    glue_reset();
    uint16_t n = mk(f, 1);
    udpHoldCapture(f, n, 100);
    int l0 = s_lock;
    char st[300];
    udpHoldStatusLine(st, sizeof(st));
    TEST_ASSERT_EQUAL_INT(l0 + 1, s_lock);
    TEST_ASSERT_EQUAL_INT(s_lock, s_unlock);
    TEST_ASSERT_NOT_NULL(strstr(st, "held;1;bytes;"));
    TEST_ASSERT_NOT_NULL(strstr(st, ";cap;2048;pushed;1;"));
    char small[20];
    udpHoldStatusLine(small, sizeof(small));                   // truncates, no overrun
    TEST_ASSERT_EQUAL_INT(19, (int)strlen(small));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_outage_releases_without_replay);
    RUN_TEST(test_outage_then_recovery_replays_oldest_first_paced);
    RUN_TEST(test_replay_sends_identical_bytes);
    RUN_TEST(test_single_sign_does_not_replay_and_new_outage_keeps_frames);
    RUN_TEST(test_drop_oldest_when_full_and_exact_capacity);
    RUN_TEST(test_single_entry_exact_fit_and_too_big);
    RUN_TEST(test_big_frame_evicts_as_many_as_needed);
    RUN_TEST(test_age_expiry);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_new_outage_mid_replay_keeps_the_rest);
    RUN_TEST(test_frames_pushed_during_replay_are_not_replayed);
    RUN_TEST(test_missed_beat_gap_replays_overlap);
    RUN_TEST(test_eviction_during_replay_shrinks_the_replay);
    RUN_TEST(test_only_text_frames);
    RUN_TEST(test_zero_and_oversize_refused);
    RUN_TEST(test_glue_tick_detects_timer_changes_and_replays);
    RUN_TEST(test_glue_bench_outage_and_synthetic_writes_are_no_sign);
    RUN_TEST(test_glue_off_is_a_no_op);
    RUN_TEST(test_holdable_text_is_kept);
    RUN_TEST(test_rm1_command_and_reply_are_refused);
    RUN_TEST(test_telemetry_acks_ping_pong_sota_are_refused);
    RUN_TEST(test_unparseable_frames_are_refused);
    RUN_TEST(test_capture_reports_the_class);
    RUN_TEST(test_max_age_is_20_minutes);
    RUN_TEST(test_frame_held_without_ip_is_replayed_not_released);
    RUN_TEST(test_frame_sent_with_ip_is_still_released);
    RUN_TEST(test_node_control_texts_are_refused);
    RUN_TEST(test_stamp_ahead_of_now_is_not_released);
    RUN_TEST(test_glue_tick_without_ip_does_not_replay);
    RUN_TEST(test_glue_synthetic_write_masks_the_replay_start);
    RUN_TEST(test_nrf_eth_synthetic_timer_writes_are_masked);
    RUN_TEST(test_status_line_copies_under_one_lock);
    return UNITY_END();
}
